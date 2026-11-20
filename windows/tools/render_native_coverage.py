"""Build the coverage map from the repository's reviewed feature inventory."""
import argparse
from collections import Counter
from hashlib import sha256
import json
from pathlib import Path
from native_code_inventory import collect_code
from render_native_status_images import (
    acceptance_counts, badge_svg, code_badge_svg, code_svg, format_progress_percent,
)


ROOT = Path(__file__).resolve().parents[1]
STATUSES = {"missing", "basic", "exact"}
PLACEHOLDER = "__NATIVE_COVERAGE_DATA__"


def build(boot: Path | None = None) -> tuple[str, Counter, dict]:
    data = json.loads((ROOT / "docs/native-coverage.json").read_text(encoding="utf-8"))
    identifiers = set()
    counts = Counter()
    for group in data["groups"]:
        if not group["name"] or not group["items"]:
            raise ValueError("Each group needs a name and at least one item")
        for item in group["items"]:
            if item["id"] in identifiers or item["status"] not in STATUSES:
                raise ValueError(f"Duplicate ID or invalid status: {item}")
            if not all(item[field].strip() for field in ("id", "title", "detail", "evidence")):
                raise ValueError(f"Incomplete coverage item: {item}")
            if not (ROOT / item["evidence"]).is_file():
                raise FileNotFoundError(item["evidence"])
            identifiers.add(item["id"])
            counts[item["status"]] += 1
    data["code"] = collect_code(ROOT, boot)
    data["verification"] = json.loads((ROOT / "docs/native-acceptance.json").read_bytes())
    verification = data["verification"]
    parts = [part for module in verification["modules"] for part in module["parts"]]
    checks = {part["id"] for part in parts}
    if len(checks) != len(parts) or checks != verification["states"].keys():
        raise ValueError("Acceptance snapshot has inconsistent checks and results")
    if not all(set(part["tiles"]) <= identifiers for part in parts):
        raise ValueError("Acceptance snapshot refers to an unknown feature")
    template = (ROOT / "tools/native_coverage.html").read_text(encoding="utf-8")
    if template.count(PLACEHOLDER) != 1:
        raise ValueError("Expected one coverage-data placeholder")
    payload = json.dumps(data, ensure_ascii=False, separators=(",", ":")).replace("<", "\\u003c")
    return template.replace(PLACEHOLDER, payload), counts, data


def update_readme(path: Path, verification: dict, acceptance_badge: str, code_badge: str) -> None:
    start = "<!-- native-progress:start -->"
    end = "<!-- native-progress:end -->"
    original = path.read_text(encoding="utf-8")
    if original.count(start) != 1 or original.count(end) != 1 or original.index(start) > original.index(end):
        raise ValueError(f"Expected one native progress block in {path}")

    passed, total = acceptance_counts(verification)
    percent = format_progress_percent(passed, total)
    scope = "windows/docs/PROGRESS.md#acceptance-and-code-map"
    # GitHub's redirect for relative image URLs drops the cache key.
    badges = "https://raw.githubusercontent.com/Senya01/dokodemo-native/main/windows/docs"
    acceptance_version = sha256(acceptance_badge.encode("utf-8")).hexdigest()[:12]
    code_version = sha256(code_badge.encode("utf-8")).hexdigest()[:12]
    progress = (
        f"\n**Windows port verification: {percent} ({passed} of {total} requirements).**\n\n"
        f"[![Windows acceptance]({badges}/native-progress.svg?v={acceptance_version})]({scope})\n"
        f"[![KSC room dispatch IDs]({badges}/native-code-progress.svg?v={code_version})]({scope})\n"
    )
    updated = original[:original.index(start) + len(start)] + progress + original[original.index(end):]
    if original != updated:
        path.write_text(updated, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--boot", type=Path, help="Optional original v1.01 BOOT.BIN for a local comparison")
    parser.add_argument("--badge", type=Path)
    parser.add_argument("--code-badge", type=Path)
    parser.add_argument("--overview", type=Path)
    parser.add_argument("--readme", type=Path, help="Update the main README progress block")
    args = parser.parse_args()
    if args.boot and not args.boot.is_file():
        parser.error("--boot must name an existing BOOT.BIN")
    fragment, status_counts, data = build(args.boot)
    args.output.write_text(
        '<!doctype html>\n<html lang="ru" style="color-scheme:light dark"><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1">'
        '<title>Карта нативного порта Doko Demo Issyo</title>'
        '<style>body{max-width:1160px;margin:2rem auto;padding:0 1rem;'
        '--background:Canvas;--foreground:CanvasText;--muted:color-mix(in srgb,CanvasText 15%,Canvas);'
        '--border:color-mix(in srgb,CanvasText 32%,Canvas)}</style>'
        + fragment + "\n</html>\n",
        encoding="utf-8",
        newline="\n",
    )
    acceptance_badge = badge_svg(data["verification"])
    code_badge = code_badge_svg(data["code"])
    if args.badge:
        args.badge.write_text(acceptance_badge, encoding="utf-8", newline="\n")
    if args.code_badge:
        args.code_badge.write_text(code_badge, encoding="utf-8", newline="\n")
    if args.readme:
        update_readme(args.readme, data["verification"], acceptance_badge, code_badge)
    if args.overview:
        args.overview.write_text(code_svg(data["code"]), encoding="utf-8", newline="\n")
    print(dict(sorted(status_counts.items())))
