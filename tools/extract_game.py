"""Autonomous original ISO -> assets + SHA-256 inventory pipeline.

Examples (run from any working directory):
  python tools/extract_game.py extract --category non-font
  python tools/extract_game.py inventory
  python tools/extract_game.py select --category script --json
  python tools/extract_game.py extract --package KSNEKO.BPM --member NEKO.KSC

All defaults are anchored to this repository, never the translation project.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from contextlib import contextmanager
import csv
import fnmatch
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile

from extract_bpar import BparArchive
from iso_reader import CHUNK, FormatError, Iso9660, Region, open_iso, require, safe_name

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / "research/containers"
BP_PATH = "PSP_GAME/USRDIR/data/DATA.BP"
INDEX_FIELDS = ("package", "member", "category", "extension", "slot", "name_offset",
                "file_offset", "offset_in_package", "offset_in_data_bp", "offset_in_iso",
                "size", "link", "sha256", "magic_hex", "raw_path")
CATEGORIES = ("non-font", "font", "font-metadata", "script", "dictionary", "model",
              "audio", "video", "graphics", "other", "all")


def local_path(path: Path) -> Path:
    resolved = path.resolve()
    require(resolved.is_relative_to(ROOT), f"Output must stay in repository: {resolved}")
    owned = (REPORT, ROOT / "assets/iso", ROOT / "assets/raw")
    require(any(resolved.is_relative_to(root.resolve()) for root in owned),
            f"Output must stay in research/containers/, assets/iso/ or assets/raw/: {resolved}")
    return resolved


def destination(base: Path, name: str) -> Path:
    base = local_path(base)
    result = base.joinpath(*safe_name(name).split("/"))
    require(result.resolve().is_relative_to(base), f"Output path escapes extraction directory: {name}")
    return result


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(CHUNK), b""):
            digest.update(block)
    return digest.hexdigest()


@contextmanager
def atomic_file(path: Path, *, binary: bool = False):
    path = local_path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=path.name + ".", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "wb" if binary else "w", **({} if binary else {"encoding": "utf-8", "newline": ""})) as stream:
            yield stream
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def write_json(path: Path, value) -> None:
    with atomic_file(path) as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def write_verified(region: Region, path: Path, digest: str) -> bool:
    """Atomic copy, then reread output; an existing mismatch fails closed."""
    path = local_path(path)
    if path.exists():
        require(path.is_file() and path.stat().st_size == region.size and hash_file(path) == digest,
                f"Existing extraction differs from ISO; move it aside before retrying: {path}")
        return False
    with atomic_file(path, binary=True) as stream:
        for offset in range(0, region.size, CHUNK):
            stream.write(region.read(offset, min(CHUNK, region.size - offset)))
    require(path.stat().st_size == region.size and hash_file(path) == digest,
            f"Extracted file does not match source: {path}")
    return True


def category(name: str) -> str:
    # In this image 68 Fnnn packages contain Fnnnnn.GIM glyphs. The category is
    # based on the source path, not a guessed per-image font decoder.
    if re.fullmatch(r"F[0-9]{3}/F[0-9]+\.GIM", name, re.IGNORECASE):
        return "font"
    extension = Path(name).suffix.upper()
    return {".KSC": "script", ".DIC": "dictionary", ".A": "dictionary",
            ".DAK": "model", ".DVS": "model", ".CB2A": "model", ".ASC": "model",
            ".GIM": "graphics", ".PNG": "graphics", ".XFT": "font-metadata",
            ".AT3": "audio", ".SGD": "audio", ".PMF": "video"}.get(extension, "other")


def matches(row: dict, args) -> bool:
    cats = args.category or ["non-font"]
    selected = any(cat == "all" or cat == row["category"] or
                   (cat == "non-font" and row["category"] != "font") for cat in cats)
    return (selected and (not args.package or any(fnmatch.fnmatchcase(row["package"], pat) for pat in args.package))
            and (not args.member or any(fnmatch.fnmatchcase(row["member"], pat) for pat in args.member))
            and (not args.extension or row["extension"] in {ext.upper().lstrip(".") for ext in args.extension}))


@contextmanager
def reference_iso(args):
    meta_path = ROOT / "source-iso.json"
    metadata = json.loads(meta_path.read_text(encoding="utf-8-sig"))
    source_path = ROOT / metadata["relative_path"]
    require(source_path.resolve().is_relative_to(ROOT), "Source ISO path must be local to this repository")
    with open_iso(source_path) as region:
        require(region.size == metadata["size_bytes"], "Source ISO size differs from source-iso.json")
        digest = region.sha256()
        require(digest == metadata["sha256"], "Source ISO SHA-256 differs from source-iso.json")
        yield region, Iso9660(region), {"path": metadata["relative_path"], "size": region.size, "sha256": digest,
                                      "source_metadata_sha256": hash_file(meta_path)}


def copy_iso(iso: Iso9660, source: dict, args) -> dict:
    rows = []
    new_files = 0
    for entry in iso.files:
        payload = iso.source.subregion(entry.offset, entry.size)
        digest = payload.sha256()
        target = destination(ROOT / "assets/iso", entry.path)
        new_files += write_verified(payload, target, digest)
        rows.append({"path": entry.path, "extent": entry.extent, "offset_in_iso": entry.offset,
                     "size": entry.size, "record_offset_in_iso": entry.record_offset,
                     "sha256": digest, "output_path": target.relative_to(ROOT).as_posix()})
    manifest = {"schema_version": 1, "source": source, "volume_id": iso.volume_id,
                "volume_sectors": iso.volume_sectors, "files": rows, "file_count": len(rows),
                "directories": iso.directories, "path_tables": iso.path_tables,
                "descriptors": iso.descriptors, "file_aliases": iso.file_aliases}
    write_json(REPORT / "iso-manifest.json", manifest)
    print(f"ISO: verified/extracted {len(rows)} files ({new_files} new), {len(iso.directories)} directories", flush=True)
    return manifest


def scan_archives(iso: Iso9660, source: dict, args) -> dict:
    bp = iso.read_file(BP_PATH)
    top = BparArchive(bp, alignment=2048)
    require(top.count == 731 and all(m.name.endswith(".BPM") for m in top.members),
            "Reference DATA.BP does not contain the expected 731 BPMs")
    containers = [{"package": "DATA.BP", "offset_in_iso": bp.start, "offset_in_data_bp": 0,
                   **top.summary()}]
    gaps = [{"package": "DATA.BP", **g} for g in top.gaps]
    extensions: Counter = Counter()
    categories: Counter = Counter()
    sizes: Counter = Counter()
    signatures: dict[str, Counter] = defaultdict(Counter)
    selected = 0
    selected_bytes = 0
    copied = 0
    member_count = 0
    total_slots = 0
    leaf_paths: set[str] = {member.name.casefold() for member in top.members}
    unknown_nested = []
    extracted_rows = []
    index_path = REPORT / "members.tsv.gz"
    with atomic_file(index_path, binary=True) as index_stream:
        with gzip.GzipFile(fileobj=index_stream, mode="wb", filename="", mtime=0) as compressed:
            with io.TextIOWrapper(compressed, encoding="utf-8", newline="") as text_stream:
                writer = csv.DictWriter(text_stream, fieldnames=INDEX_FIELDS, delimiter="\t", lineterminator="\n")
                writer.writeheader()
                for number, member in enumerate(sorted(top.members, key=lambda m: m.name), 1):
                    region = top.payload(member)
                    archive = BparArchive(region)
                    total_slots += archive.slots
                    summary = {"package": member.name, "slot_in_data_bp": member.slot,
                               "offset_in_data_bp": member.offset, "offset_in_iso": region.start,
                               **archive.summary()}
                    containers.append(summary)
                    gaps.extend({"package": member.name, **gap} for gap in archive.gaps)
                    if args.command == "extract":
                        target = destination(ROOT / "assets/raw", member.name)
                        copied += write_verified(region, target, summary["sha256"])
                    for child in archive.members:
                        require(child.name.casefold() not in leaf_paths,
                                f"Member extraction path collision across BPMs: {child.name}")
                        leaf_paths.add(child.name.casefold())
                        payload = archive.payload(child)
                        magic = payload.read(0, min(16, payload.size))
                        digest = payload.sha256()
                        extension = Path(child.name).suffix.upper().lstrip(".")
                        kind = category(child.name)
                        row = {"package": member.name, "member": child.name, "category": kind,
                               "extension": extension, "slot": child.slot, "name_offset": child.name_offset,
                               "file_offset": child.file_offset, "offset_in_package": child.offset,
                               "offset_in_data_bp": member.offset + child.offset, "offset_in_iso": payload.start,
                               "size": child.size, "link": child.link, "sha256": digest,
                               "magic_hex": magic.hex(), "raw_path": f"assets/raw/{child.name}"}
                        writer.writerow(row)
                        member_count += 1
                        extensions[extension] += 1
                        categories[kind] += 1
                        sizes[kind] += child.size
                        signatures[extension][magic[:12].hex()] += 1
                        if len(magic) == 16:
                            c, s, n, h = struct.unpack("<4I", magic)
                            if 0 < c <= s and 16 + 16 * s + n <= h <= payload.size and n:
                                unknown_nested.append({"package": member.name, "member": child.name,
                                                       "offset_in_iso": payload.start, "size": payload.size})
                        if matches(row, args):
                            selected += 1
                            selected_bytes += child.size
                            if args.command == "extract":
                                target = destination(ROOT / "assets/raw", child.name)
                                copied += write_verified(payload, target, digest)
                                extracted_rows.append(row)
                    if number % 50 == 0 or number == top.count:
                        print(f"BPM {number}/{top.count}: {member_count:,} members checked, {selected:,} selected", flush=True)
    require(member_count == 556636, f"Unexpected reference member count: {member_count}")
    summary = {"schema_version": 1, "source": source, "data_bp": {"path_in_iso": BP_PATH,
               "offset_in_iso": bp.start, "size": bp.size, "sha256": containers[0]["sha256"]},
               "iso_files": len(iso.files), "bpm_count": top.count, "bpm_slots": total_slots,
               "member_count": member_count, "by_extension": dict(sorted(extensions.items())),
               "by_category": dict(sorted(categories.items())), "bytes_by_category": dict(sorted(sizes.items())),
               "index": {"path": index_path.relative_to(ROOT).as_posix(), "sha256": hash_file(index_path)},
               "padding": {"gap_count": len(gaps), "gap_bytes": sum(g["size"] for g in gaps), "nonzero_bytes": 0},
               "nested_header_candidates": unknown_nested, "validation": "all slots/names/payloads/links/padding checked",
               "errors": [], "semantics": "Extraction preserves bytes; game logic and content decoding are not implemented here."}
    write_json(REPORT / "containers.json", containers)
    write_json(REPORT / "gaps.json", gaps)
    write_json(REPORT / "magic-by-extension.json", {ext: dict(counts) for ext, counts in sorted(signatures.items())})
    write_json(REPORT / "summary.json", summary)
    if args.command == "extract":
        existing_manifest = REPORT / "extraction-manifest.json"
        previous_rows = []
        if existing_manifest.exists():
            previous = json.loads(existing_manifest.read_text(encoding="utf-8"))
            require(previous["source"] == source, "Previous extraction manifest uses another source")
            previous_rows = previous["files"]
        cumulative = {(row["package"], row["member"]): row for row in previous_rows}
        for row in extracted_rows:
            key = (row["package"], row["member"])
            require(key not in cumulative or cumulative[key] == row, "Extraction manifest member changed")
            cumulative[key] = row
        write_json(REPORT / "extraction-manifest.json", {"schema_version": 1, "source": source,
                   "selection": {"categories": args.category or ["non-font"], "packages": args.package,
                   "members": args.member, "extensions": args.extension}, "containers_verified": top.count,
                   "files": list(cumulative.values()), "selected_count": selected, "selected_bytes": selected_bytes,
                   "total_extracted_members": len(cumulative)})
    print(json.dumps({"bpm": top.count, "members": member_count, "categories": summary["by_category"],
                      "selected": selected, "selected_bytes": selected_bytes, "new_raw_files": copied}, ensure_ascii=False), flush=True)
    return summary


def read_index(path: Path | None = None):
    """Stream the validated gzip TSV index; integer fields are converted."""
    path = REPORT / "members.tsv.gz" if path is None else Path(path)
    with gzip.open(path, "rt", encoding="utf-8", newline="") as stream:
        reader = csv.DictReader(stream, delimiter="\t")
        require(tuple(reader.fieldnames or ()) == INDEX_FIELDS, "Unexpected member-index schema")
        for row in reader:
            for field in ("slot", "name_offset", "file_offset", "offset_in_package", "offset_in_data_bp",
                          "offset_in_iso", "size", "link"):
                row[field] = int(row[field])
            yield row


def select(args) -> None:
    index = REPORT / "members.tsv.gz"
    require(index.is_file(), "Run inventory or extract first to generate the full index")
    selected = 0
    size = 0
    output = local_path(ROOT / args.output) if args.output else None
    if output:
        require(output.is_relative_to(REPORT.resolve()), "Selection exports belong in research/containers/")
        protected = {"summary.json", "iso-manifest.json", "iso-inventory.json", "containers.json", "gaps.json",
                     "magic-by-extension.json", "members.tsv.gz", "extraction-manifest.json", "verification.json"}
        require(output.parent != REPORT.resolve() or output.name not in protected,
                "Selection output cannot replace a pipeline manifest or index")
    @contextmanager
    def selection_stream():
        if output:
            with atomic_file(output) as stream:
                yield stream
        else:
            yield sys.stdout
    with selection_stream() as stream:
        writer = None
        if not args.json:
            writer = csv.DictWriter(stream, fieldnames=INDEX_FIELDS, delimiter="\t", lineterminator="\n")
            writer.writeheader()
        for row in read_index():
            if matches(row, args):
                selected += 1
                size += row["size"]
                if not args.limit or selected <= args.limit:
                    if args.json:
                        stream.write(json.dumps(row, ensure_ascii=False) + "\n")
                    else:
                        writer.writerow(row)
    print(f"Selected {selected} members, {size} bytes" + (f"; emitted first {args.limit}" if args.limit else ""), file=sys.stderr)


def parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="command", required=True)
    for name in ("extract", "inventory", "select"):
        child = sub.add_parser(name, help={"extract": "validate all input, copy ISO + BPM + selected members",
                               "inventory": "validate/hash all ISO/BPM members without copying assets",
                               "select": "filter full member index without opening ISO"}[name])
        child.add_argument("--category", choices=CATEGORIES, action="append", help="repeatable; default non-font")
        child.add_argument("--package", action="append", help="case-sensitive BPM wildcard, repeatable")
        child.add_argument("--member", action="append", help="case-sensitive normalized member wildcard, repeatable")
        child.add_argument("--extension", action="append", help="extension, repeatable, e.g. KSC or .DAK")
        if name == "select":
            child.add_argument("--json", action="store_true", help="emit UTF-8 JSONL instead of TSV")
            child.add_argument("--limit", type=int, default=0, help="limit displayed rows; still count all matches")
            child.add_argument("--output", help="save selection inside this repository")
        if name == "extract":
            child.add_argument("--iso-only", action="store_true", help="copy and verify only the 50 ISO files")
    return p


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    try:
        if args.command == "select":
            require(args.limit >= 0, "Selection limit cannot be negative")
            select(args)
        else:
            with reference_iso(args) as (region, iso, source):
                require(len(iso.files) == 50, "Reference ISO must contain 50 files")
                if args.command == "extract":
                    copy_iso(iso, source, args)
                else:
                    rows = [{"path": e.path, "offset_in_iso": e.offset, "size": e.size,
                             "sha256": iso.source.subregion(e.offset, e.size).sha256()} for e in iso.files]
                    write_json(REPORT / "iso-inventory.json", {"source": source, "files": rows})
                if not getattr(args, "iso_only", False):
                    scan_archives(iso, source, args)
        return 0
    except (FormatError, UnicodeError, OSError, ValueError) as error:
        print(f"Extraction failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
