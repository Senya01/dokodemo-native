"""Build the coverage map from the repository's reviewed feature inventory."""
import argparse
from collections import Counter
from hashlib import sha256
import json
from pathlib import Path
from native_code_inventory import collect_code
from render_native_status_images import (
    acceptance_counts, badge_svg, code_badge_svg, format_progress_percent, map_badge_svg, map_svg,
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
    passed, checks_total = acceptance_counts(verification)
    data["progress"] = {
        "implemented": counts["basic"] + counts["exact"], "exact": counts["exact"],
        "features": sum(counts.values()), "passed": passed, "checks": checks_total,
    }
    template = (ROOT / "tools/native_coverage.html").read_text(encoding="utf-8")
    if template.count(PLACEHOLDER) != 1:
        raise ValueError("Expected one coverage-data placeholder")
    payload = json.dumps(data, ensure_ascii=False, separators=(",", ":")).replace("<", "\\u003c")
    return template.replace(PLACEHOLDER, payload), counts, data


def update_readme(path: Path, progress: dict, badge: str, overview: str) -> None:
    start = "<!-- native-progress:start -->"
    end = "<!-- native-progress:end -->"
    original = path.read_text(encoding="utf-8")
    if original.count(start) != 1 or original.count(end) != 1 or original.index(start) > original.index(end):
        raise ValueError(f"Expected one native progress block in {path}")

    # GitHub's redirect for relative image URLs drops the cache key.
    images = "https://raw.githubusercontent.com/Senya01/dokodemo-native/main/windows/docs"
    interactive = "windows/docs/native-coverage.html"
    badge_version = sha256(badge.encode("utf-8")).hexdigest()[:12]
    map_version = sha256(overview.encode("utf-8")).hexdigest()[:12]
    implemented, exact, features = (progress[key] for key in ("implemented", "exact", "features"))
    passed, checks = progress["passed"], progress["checks"]
    percent = format_progress_percent(passed, checks)
    generated = (
        f"\n[![Native map progress]({images}/native-map-progress.svg?v={badge_version})]({interactive})\n\n"
        f"**Native implementations: {implemented}/{features} basic or better, {exact}/{features} byte-exact.** "
        f"**Windows acceptance: {percent} ({passed}/{checks}).**\n\n"
        f"[![Native port tile map]({images}/native-code-map.svg?v={map_version})]({interactive})\n"
    )
    updated = original[:original.index(start) + len(start)] + generated + original[original.index(end):]
    if original != updated:
        path.write_text(updated, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--boot", type=Path, help="Optional original v1.01 BOOT.BIN for a local comparison")
    parser.add_argument("--badge", type=Path)
    parser.add_argument("--code-badge", type=Path)
    parser.add_argument("--overview", type=Path)
    parser.add_argument("--map-badge", type=Path)
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
    if args.badge:
        args.badge.write_text(badge_svg(data["verification"]), encoding="utf-8", newline="\n")
    if args.code_badge:
        args.code_badge.write_text(code_badge_svg(data["code"]), encoding="utf-8", newline="\n")
    if args.overview or args.map_badge or args.readme:
        overview = map_svg(data)
        badge = map_badge_svg(data["progress"])
        if args.overview:
            args.overview.write_text(overview, encoding="utf-8", newline="\n")
        if args.map_badge:
            args.map_badge.write_text(badge, encoding="utf-8", newline="\n")
        if args.readme:
            update_readme(args.readme, data["progress"], badge, overview)
    print(dict(sorted(status_counts.items())))
