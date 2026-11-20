"""Render GitHub-readable images from the same data as the interactive map."""
from html import escape


def format_progress_percent(success: int, total: int) -> str:
    return f"{success / total:.1%}".replace(".0%", "%")


def acceptance_counts(verification: dict) -> tuple[int, int]:
    results = verification["states"].values()
    return sum(status == "passed" for status in results), len(results)


def _badge(title: str, success: int, total: int, scope: str, complete: bool) -> str:
    percent = format_progress_percent(success, total)
    label = escape(f"{title} {percent}, {success} of {total} {scope}")
    color = "#257944" if complete else "#e8ba25"
    ink = "#fff" if complete else "#171717"
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="173" height="25" viewBox="0 0 173 25" role="img" aria-label="{label}">
  <title>{label}</title>
  <clipPath id="badge"><rect width="173" height="25" rx="4"/></clipPath>
  <g clip-path="url(#badge)">
    <rect width="121" height="25" fill="#444b55"/>
    <rect x="121" width="52" height="25" fill="{color}"/>
  </g>
  <g font-family="Verdana, sans-serif" font-size="11" text-anchor="middle">
    <text x="60.5" y="16.5" fill="#fff">{escape(title)}</text>
    <text x="147" y="16.5" fill="{ink}" font-weight="bold">{percent}</text>
  </g>
</svg>
"""


def badge_svg(verification: dict) -> str:
    passed, total = acceptance_counts(verification)
    return _badge("Windows acceptance", passed, total, "requirements verified", verification["ready"])


def code_badge_svg(code: dict) -> str:
    entries = [entry for group in code["groups"] if group["kind"]=="dispatch" for entry in group["items"]]
    matched = sum(bool(entry["lines"]) for entry in entries)
    return _badge("KSC room code", matched, len(entries), "dispatch IDs with a native branch",
                  matched==len(entries))


def text(x, y, value, size=13, color="#edf3f8", end=False):
    anchor = ' text-anchor="end"' if end else ""
    return f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}"{anchor}>{escape(value)}</text>'


def rectangle(x, y, width, height, color):
    return f'<rect x="{x}" y="{y}" width="{width}" height="{height}" fill="{color}"/>'


def code_svg(code: dict) -> str:
    """Create a preview with one square per dispatch ID or compilation unit."""
    found, missing, file = "#2f9b53", "#63707a", "#477fac"
    names = {
        "Команды KSC": "KSC opcodes",
        "Встроенные функции KSC": "KSC built-ins",
        "Вызовы GAME в комнате": "Room GAME IDs",
    }
    groups = [g for g in code["groups"] if g["kind"] != "files"]
    files = [g for g in code["groups"] if g["kind"] == "files"]
    width, gap, margin = 930, 10, 14
    legend = ((found, "Branch in room handler"), (missing, "No branch in room handler"), (file, "Windows source file"))
    svg = [text(margin, 23, "Native code map", 18)]
    for i, (color, label) in enumerate(legend):
        x = margin + i * 220
        svg.extend((rectangle(x, 36, 10, 10, color), text(x + 16, 45, label, 12)))

    def panels(panels, columns, top, cell):
        available = width - 2 * margin
        box = (available - (columns - 1) * gap) // columns
        row_size = (box - 15) // (cell + 2)
        for row in range(0, len(panels), columns):
            chunk = panels[row:row + columns]
            rows = max((len(g["items"]) + row_size - 1) // row_size for g in chunk)
            height = 36 + rows * (cell + 2)
            for col, group in enumerate(chunk):
                x = margin + col * (box + gap)
                items = group["items"]
                count = str(len(items))
                if group["kind"] == "dispatch":
                    count = f"{sum(bool(item['lines']) for item in items)}/{count}"
                svg.extend((rectangle(x, top, box, height, "#1b242c"),
                            text(x + 8, top + 19, names.get(group["title"], group["title"])),
                            text(x + box - 8, top + 19, count, 12, "#a9bdce", end=True)))
                for i, item in enumerate(items):
                    color = file if group["kind"] == "files" else found if item["lines"] else missing
                    svg.append(rectangle(x + 8 + (i % row_size) * (cell + 2),
                                         top + 28 + (i // row_size) * (cell + 2), cell, cell, color))
            top += height + gap
        return top

    bottom = panels(files, 4, panels(groups, 3, 60, 8), 7)
    height = bottom + margin - gap
    return ("<svg xmlns='http://www.w3.org/2000/svg' "
            f"width='{width}' height='{height}' viewBox='0 0 {width} {height}' role='img' "
            "aria-label='Native code map. Each square represents a dispatch ID or Windows source file.'>\n"
            "<title>Native code map</title>\n"
            + rectangle(0, 0, width, height, "#10161d")
            + "\n<g font-family='Segoe UI, Arial, sans-serif'>\n"
            + "\n".join(svg) + "\n</g>\n</svg>\n")
