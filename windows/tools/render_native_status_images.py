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


def map_badge_svg(progress: dict) -> str:
    implemented, exact, features = (progress[key] for key in ("implemented", "exact", "features"))
    passed, checks = progress["passed"], progress["checks"]
    basic_percent = format_progress_percent(implemented, features)
    exact_percent = format_progress_percent(exact, features)
    acceptance_percent = format_progress_percent(passed, checks)
    label = (f"Native map: basic or better {basic_percent} ({implemented}/{features}), "
             f"byte-exact {exact_percent} ({exact}/{features}), "
             f"Windows build verification {acceptance_percent} ({passed}/{checks})")
    green = ("#257944", "#fff")
    amber = ("#e8ba25", "#171717")
    segments = (
        ("Native map", 90, "#444b55", "#fff"),
        (f"Basic+ {basic_percent}", 102, *(green if implemented == features else amber)),
        (f"Byte-exact {exact_percent}", 112, "#295b84", "#fff"),
        (f"Windows checks {acceptance_percent}", 136, *(green if passed == checks else amber)),
    )
    width = sum(segment[1] for segment in segments)
    elements = [f'<title>{escape(label)}</title>',
                f'<clipPath id="badge"><rect width="{width}" height="25" rx="4"/></clipPath>',
                '<g clip-path="url(#badge)" font-family="Verdana,sans-serif" text-anchor="middle">']
    x = 0
    for caption, size, color, ink in segments:
        elements.extend((rectangle(x, 0, size, 25, color),
                         text(x + size / 2, 16.5, caption, 11, ink)))
        x += size
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="25" '
            f'viewBox="0 0 {width} 25" role="img" aria-label="{escape(label)}">\n'
            + "\n".join(elements) + "\n</g>\n</svg>\n")


def map_svg(data: dict) -> str:
    """Draw the implementations, acceptance checks and code inventory in one tile map."""
    features, verification, code = data["groups"], data["verification"], data["code"]
    feature_colors = {"basic": "#2f9b53", "exact": "#72b9ff", "missing": "#63707a"}
    check_colors = {"passed": "#2f9b53", "blocked": "#63707a",
                    "failed": "#d55e66", "unverified": "#333e48"}
    code_colors = {"present": "#2f9b53", "absent": "#63707a", "file": "#477fac"}
    width, gap, margin = 930, 9, 14
    svg = [text(margin, 25, "Native port map", 19)]

    def section(y, heading, legend):
        svg.append(text(margin, y, heading, 15))
        for x, (color, label) in enumerate(legend):
            left = margin + x * 218
            svg.extend((rectangle(left, y + 10, 9, 9, color), text(left + 14, y + 18, label, 11)))
        return y + 27

    def panels(groups, colors, columns, top, cell, layer):
        available = width - 2 * margin
        box = (available - (columns - 1) * gap) // columns
        row_size = (box - 15) // (cell + 2)
        for row in range(0, len(groups), columns):
            chunk = groups[row:row + columns]
            rows = max((len(items) + row_size - 1) // row_size for _, _, items in chunk)
            height = 34 + rows * (cell + 2)
            for col, (name, count, items) in enumerate(chunk):
                x = margin + col * (box + gap)
                svg.extend((rectangle(x, top, box, height, "#1b242c"),
                            text(x + 8, top + 17, name, 12),
                            text(x + box - 8, top + 17, count, 11, "#a9bdce", end=True)))
                for i, (name, state) in enumerate(items):
                    square = rectangle(x + 8 + i % row_size * (cell + 2),
                                       top + 24 + i // row_size * (cell + 2), cell, cell, colors[state])
                    svg.append(f'<g data-layer="{layer}" data-status="{state}">'
                               f'<title>{escape(name)}</title>{square}</g>')
            top += height + gap
        return top

    progress = data["progress"]
    implemented, exact, features_total = (progress[key] for key in ("implemented", "exact", "features"))
    feature_title = f"Implementations · {implemented}/{features_total} basic or better · {exact}/{features_total} byte-exact"
    top = section(48, feature_title, (
        (feature_colors["basic"], "Basic"), (feature_colors["exact"], "Byte-exact"),
        (feature_colors["missing"], "Missing"),
    ))
    feature_panels = []
    for group in features:
        items = [(item["title"], item["status"]) for item in group["items"]]
        count = sum(state != "missing" for _, state in items)
        feature_panels.append((group["name"], f"{count}/{len(items)}", items))
    top = panels(feature_panels, feature_colors, 4, top, 13, "implementation")

    top = section(top + 11, f'Windows build verification · {progress["passed"]}/{progress["checks"]} passed', (
        (check_colors["passed"], "Passed"), (check_colors["blocked"], "Open requirement"),
        (check_colors["failed"], "Test failed"), (check_colors["unverified"], "Unverified"),
    ))
    states = verification["states"]
    check_panels = []
    for module in verification["modules"]:
        items = [(part["title"], states[part["id"]]) for part in module["parts"]]
        count = sum(state == "passed" for _, state in items)
        check_panels.append((module["title"], f"{count}/{len(items)}", items))
    top = panels(check_panels, check_colors, 3, top, 13, "acceptance")

    top = section(top + 11, "Code inventory", (
        (code_colors["present"], "Branch or ID check present"),
        (code_colors["absent"], "Branch absent"), (code_colors["file"], "Windows source file"),
    ))
    commands, files = [], []
    for group in code["groups"]:
        name = group["title"]
        if group["kind"] == "files":
            items = [(item["source"], "file") for item in group["items"]]
            files.append((name, str(len(items)), items))
        else:
            items = [(item["label"], "present" if item["lines"] else "absent") for item in group["items"]]
            count = sum(state == "present" for _, state in items)
            commands.append((name, f"{count}/{len(items)}", items))
    top = panels(commands, code_colors, 3, top, 8, "dispatch")
    bottom = panels(files, code_colors, 4, top, 7, "file")
    height = bottom + margin - gap
    return (f"<svg xmlns='http://www.w3.org/2000/svg' width='{width}' height='{height}' "
            f"viewBox='0 0 {width} {height}' role='img' aria-label='Native port map'>\n"
            "<title>Native port map with implementations, Windows build verification and code inventory</title>\n"
            + rectangle(0, 0, width, height, "#10161d")
            + "\n<g font-family='Segoe UI, Arial, sans-serif'>\n"
            + "\n".join(svg) + "\n</g>\n</svg>\n")
