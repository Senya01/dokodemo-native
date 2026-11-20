"""Build the coverage map from the repository's reviewed feature inventory."""
from collections import Counter
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
STATUSES = {"missing", "basic", "exact"}
PLACEHOLDER = "__NATIVE_COVERAGE_DATA__"


def build() -> tuple[str, Counter]:
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
    template = (ROOT / "tools/native_coverage.html").read_text(encoding="utf-8")
    if template.count(PLACEHOLDER) != 1:
        raise ValueError("Expected one coverage-data placeholder")
    payload = json.dumps(data, ensure_ascii=False, separators=(",", ":")).replace("<", "\\u003c")
    return template.replace(PLACEHOLDER, payload), counts


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: python tools/render_native_coverage.py <output.html>")
    fragment, status_counts = build()
    Path(sys.argv[1]).write_text(
        '<!doctype html>\n<html lang="ru" style="color-scheme:light dark"><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1">'
        '<title>Карта нативного порта Doko Demo Issyo</title>'
        '<style>body{max-width:1160px;margin:2rem auto;padding:0 1rem;'
        '--background:Canvas;--foreground:CanvasText;--muted:color-mix(in srgb,CanvasText 15%,Canvas);'
        '--border:color-mix(in srgb,CanvasText 32%,Canvas)}</style>'
        + fragment + "\n</html>\n",
        encoding="utf-8",
    )
    print(dict(sorted(status_counts.items())))
