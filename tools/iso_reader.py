"""Strict read-only ISO9660 reader for the original single-volume PSP image.

Only Python's standard library is used. Unsupported layouts fail explicitly.
Addresses returned by this module are byte offsets in the original image.
"""
from __future__ import annotations

from dataclasses import dataclass
import hashlib
import mmap
from pathlib import Path
import re

SECTOR = 2048
CHUNK = 4 * 1024 * 1024


class FormatError(ValueError):
    """Input is malformed or uses an unsupported layout."""


def require(condition: bool, message: str) -> None:
    if not condition:
        raise FormatError(message)


def safe_name(name: str) -> str:
    """Normalize archive separators; reject unsafe Windows extraction paths."""
    name = name.replace("\\", "/")
    require(bool(name), "Empty resource path")
    for part in name.split("/"):
        require(part not in ("", ".", ".."), f"Unsafe resource path: {name!r}")
        require(not any(ord(c) < 32 or c in '<>:"|?*' for c in part),
                f"Invalid Windows resource path: {name!r}")
        require(not part.endswith((".", " ")), f"Ambiguous Windows resource path: {name!r}")
        require(not re.fullmatch(r"(?i:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])", part.split(".")[0]),
                f"Reserved Windows resource path: {name!r}")
    return name


class Region:
    """Bounded view of bytes/mmap, with checked reads and nested offsets."""
    def __init__(self, data: bytes | mmap.mmap, start: int = 0, size: int | None = None):
        self.data = data
        self.start = start
        self.size = len(data) - start if size is None else size
        require(0 <= start <= len(data) and 0 <= self.size <= len(data) - start,
                f"Region outside input: {start}+{self.size}/{len(data)}")

    def check(self, offset: int, size: int) -> None:
        require(0 <= offset <= self.size and 0 <= size <= self.size - offset,
                f"Read outside region: {offset}+{size}/{self.size} (base 0x{self.start:x})")

    def read(self, offset: int, size: int) -> bytes:
        self.check(offset, size)
        return self.data[self.start + offset:self.start + offset + size]

    def subregion(self, offset: int, size: int) -> Region:
        self.check(offset, size)
        return Region(self.data, self.start + offset, size)

    def find_nul(self, offset: int, end: int) -> int:
        self.check(offset, end - offset)
        found = self.data.find(b"\0", self.start + offset, self.start + end)
        return -1 if found < 0 else found - self.start

    def sha256(self) -> str:
        digest = hashlib.sha256()
        for offset in range(0, self.size, CHUNK):
            digest.update(self.read(offset, min(CHUNK, self.size - offset)))
        return digest.hexdigest()


@dataclass(frozen=True)
class IsoEntry:
    path: str
    extent: int
    size: int
    record_offset: int

    @property
    def offset(self) -> int:
        return self.extent * SECTOR


def both(data: bytes, at: int, width: int) -> int:
    require(0 <= at <= len(data) - 2 * width, "Truncated both-endian ISO field")
    value = int.from_bytes(data[at:at + width], "little")
    require(value == int.from_bytes(data[at + width:at + 2 * width], "big"),
            f"ISO endian copies disagree at field 0x{at:x}")
    return value


def rounded(size: int) -> int:
    return (size + SECTOR - 1) // SECTOR * SECTOR


class Iso9660:
    def __init__(self, source: Region):
        self.source = source
        require(source.size % SECTOR == 0, "ISO is not sector-aligned")
        self.files: list[IsoEntry] = []
        self.directories: list[dict] = []
        self.path_tables: list[dict] = []
        self.descriptors: list[dict] = []
        self._protected = [(0, 16 * SECTOR, "system area")]
        primary = None
        for sector in range(16, min(source.size // SECTOR, 256)):
            data = source.read(sector * SECTOR, SECTOR)
            require(data[1:7] == b"CD001\1", f"Invalid volume descriptor at sector {sector}")
            self.descriptors.append({"type": data[0], "sector": sector})
            if data[0] == 255:
                break
            require(data[0] == 1 and primary is None,
                    "Only one primary ISO9660 volume is supported (no Joliet/boot descriptors)")
            primary = data
        else:
            raise FormatError("Volume descriptor terminator missing")
        require(primary is not None, "Primary volume descriptor missing")
        self._protected.append((16 * SECTOR, (sector + 1) * SECTOR, "descriptors"))
        self.volume_sectors = both(primary, 80, 4)
        require(self.volume_sectors * SECTOR == source.size, "Declared ISO size differs from image size")
        require(both(primary, 128, 2) == SECTOR, "Unsupported ISO block size")
        require(both(primary, 120, 2) == both(primary, 124, 2) == 1, "Multi-volume image unsupported")
        self.volume_id = primary[40:72].decode("ascii").rstrip(" ")
        table_size = both(primary, 132, 4)
        require(table_size > 0, "Missing path tables")
        for at, endian in ((140, "little"), (144, "little"), (148, "big"), (152, "big")):
            lba = int.from_bytes(primary[at:at + 4], endian)
            require(at not in (140, 148) or lba > 0, "Required path table missing")
            if lba:
                table = source.read(lba * SECTOR, table_size)
                self.path_tables.append({"sector": lba, "size": table_size, "endian": endian,
                                         "entries": self._parse_path_table(table, endian)})
                self._protected.append((lba * SECTOR, lba * SECTOR + rounded(table_size), f"path table {at}"))
        root = self._record(primary[156:156 + primary[156]])
        require(root[2] and root[3] == b"\0", "Invalid ISO root record")
        self._walk(root[0], root[1], "", (root[0], root[1]), ())
        expected_tables = []
        for directory in self.directories:
            expected_tables.append((directory["path"], directory["extent"]))
        for table in self.path_tables:
            paths = [""]
            actual = []
            for i, (name, extent, parent) in enumerate(table["entries"], 1):
                require(1 <= parent <= i and (i == 1 or parent < i), "Invalid path-table parent")
                if i == 1:
                    require(name == "" and parent == 1, "Invalid path-table root")
                    path = ""
                else:
                    path = safe_name(f"{paths[parent]}/{name}".lstrip("/"))
                paths.append(path)
                actual.append((path, extent))
            require(sorted(actual) == sorted(expected_tables), "Path table disagrees with directory tree")
        names = [f.path.casefold() for f in self.files] + [d["path"].casefold() for d in self.directories]
        require(len(names) == len(set(names)), "ISO paths collide on Windows")
        allocated = {(start, end): label for start, end, label in self._protected}
        metadata_allocations = set(allocated)
        file_extents: dict[tuple[int, int], list[str]] = {}
        for entry in self.files:
            if entry.size:
                key = (entry.offset, entry.offset + rounded(entry.size))
                require(key not in metadata_allocations, f"ISO file aliases metadata: {entry.path}")
                file_extents.setdefault((entry.offset, entry.size), []).append(entry.path)
                allocated[key] = entry.path
        intervals = sorted((a, b, label) for (a, b), label in allocated.items())
        for a, b, label in intervals:
            require(0 <= a < b <= source.size, f"ISO allocation outside image: {label}")
        for left, right in zip(intervals, intervals[1:]):
            require(left[1] <= right[0], f"ISO allocations overlap: {left[2]} / {right[2]}")
        # Same start with differing file lengths is not an exact alias.
        starts: dict[int, int] = {}
        for (start, size) in file_extents:
            require(start not in starts or starts[start] == size, "Conflicting ISO file aliases")
            starts[start] = size
        self.file_aliases = [paths for paths in file_extents.values() if len(paths) > 1]
        self.files.sort(key=lambda f: f.path)

    @staticmethod
    def _parse_path_table(table: bytes, endian: str) -> list[tuple[str, int, int]]:
        rows = []
        pos = 0
        while pos < len(table):
            require(pos + 8 <= len(table), "Truncated path-table record")
            length, ext_attr = table[pos:pos + 2]
            end = pos + 8 + length + length % 2
            require(length > 0 and ext_attr == 0 and end <= len(table), "Invalid path-table record")
            raw = table[pos + 8:pos + 8 + length]
            name = "" if raw == b"\0" else safe_name(raw.decode("ascii"))
            require("/" not in name, "Path-table name includes separator")
            require(length % 2 == 0 or table[end - 1] == 0, "Nonzero path-table padding")
            rows.append((name, int.from_bytes(table[pos + 2:pos + 6], endian),
                         int.from_bytes(table[pos + 6:pos + 8], endian)))
            pos = end
        return rows

    def _record(self, rec: bytes) -> tuple[int, int, bool, bytes]:
        require(len(rec) >= 34 and rec[0] == len(rec), "Invalid directory record length")
        name_end = 33 + rec[32]
        require(rec[32] > 0 and name_end <= len(rec), "Directory identifier out of bounds")
        require(rec[1] == rec[26] == rec[27] == 0 and not rec[25] & 0x80,
                "Extended attributes/interleaved/multi-extent records unsupported")
        require(rec[25] & ~3 == 0 and not rec[25] & 1, "Unsupported directory flags")
        require(both(rec, 28, 2) == 1, "Unexpected directory volume sequence")
        extent, size = both(rec, 2, 4), both(rec, 10, 4)
        self.source.check(extent * SECTOR, rounded(size))
        return extent, size, bool(rec[25] & 2), rec[33:name_end]

    def _walk(self, extent: int, size: int, prefix: str, parent: tuple[int, int], stack: tuple[int, ...]) -> None:
        require(extent not in stack and len(stack) < 64 and size > 0, "ISO directory cycle/depth/empty directory")
        require(not any(d["extent"] == extent for d in self.directories), "Directory extent alias unsupported")
        self.directories.append({"path": prefix, "extent": extent, "size": size})
        self._protected.append((extent * SECTOR, extent * SECTOR + rounded(size), f"directory {prefix}"))
        data = self.source.read(extent * SECTOR, size)
        pos = 0
        special: set[bytes] = set()
        while pos < len(data):
            length = data[pos]
            if not length:
                end = min(len(data), rounded(pos + 1))
                require(not any(data[pos:end]), "Nonzero directory sector padding")
                pos = end
                continue
            require(pos + length <= len(data) and pos % SECTOR + length <= SECTOR,
                    "ISO directory record crosses sector/extent boundary")
            at = extent * SECTOR + pos
            child_extent, child_size, is_dir, name = self._record(data[pos:pos + length])
            pos += length
            if name in (b"\0", b"\1"):
                expected = (extent, size) if name == b"\0" else parent
                require(is_dir and (child_extent, child_size) == expected and name not in special,
                        "Invalid dot/dotdot directory record")
                special.add(name)
                continue
            decoded = name.decode("ascii")
            require(";" not in decoded or re.fullmatch(r"[^;]+;[1-9][0-9]*", decoded) is not None,
                    "Invalid ISO version suffix")
            decoded = safe_name(decoded.split(";", 1)[0])
            require("/" not in decoded, "ISO identifier includes separator")
            path = f"{prefix}/{decoded}" if prefix else decoded
            if is_dir:
                self._walk(child_extent, child_size, path, (extent, size), stack + (extent,))
            else:
                self.files.append(IsoEntry(path, child_extent, child_size, at))
        require(special == {b"\0", b"\1"}, "Missing dot/dotdot directory record")

    def read_file(self, path: str) -> Region:
        found = [entry for entry in self.files if entry.path.casefold() == safe_name(path).casefold()]
        require(len(found) == 1, f"ISO file not found or ambiguous: {path}")
        entry = found[0]
        return self.source.subregion(entry.offset, entry.size)


def open_iso(path: Path):
    """Context manager returning Region; no writes to the source image."""
    from contextlib import contextmanager

    @contextmanager
    def manager():
        with Path(path).open("rb") as stream:
            require(Path(path).stat().st_size > 0, "Empty ISO")
            with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
                yield Region(mapped)
    return manager()
