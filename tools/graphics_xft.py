"""Decode this game's ORIGINAL XFT1 atlas into BMP32/alpha8 and glyph metadata.

No replacement font is synthesized. Stored width bytes are exported separately
from runtime spacing; exact renderer scale/tracking must be recovered by callers.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import struct

from graphics_gim import GraphicsError, Image, bmp_bytes, png_bytes, sha256


def glyph_index(raw: bytes) -> int:
    """Game-style Shift-JIS to 96-column atlas mapping; no halfwidth kana."""
    if len(raw) == 1 and 0x20 <= raw[0] <= 0x7e:
        return raw[0] - 0x20
    if len(raw) != 2:
        raise GraphicsError(f"Unsupported font byte sequence: {raw.hex()}")
    lead, trail = raw
    if not (0x81 <= lead <= 0x9f or 0xe0 <= lead <= 0xfc):
        raise GraphicsError(f"Unsupported Shift-JIS lead byte: {raw.hex()}")
    if not (0x40 <= trail <= 0xfc and trail != 0x7f):
        raise GraphicsError(f"Unsupported Shift-JIS trail byte: {raw.hex()}")
    bias = -1 if lead < 0xa0 else 0x7f if lead < 0xf0 else 0x29
    row = (lead * 2 + bias) & 255
    if trail >= 0x9f:
        row = (row + 1) & 255
        column = (trail + 0x62) & 255
    else:
        column = trail - (0x40 if trail >= 0x80 else 0x3f)
    return row * 96 + column


def decode_xft(data: bytes) -> tuple[dict, bytes, list[Image]]:
    if len(data) < 0x3c or data[:4] != b"XFT1":
        raise GraphicsError("Expected XFT1 resource")
    gw, gh, count = struct.unpack_from("<HHI", data, 4)
    wa = struct.unpack_from("<I", data, 0x10)[0]
    pages = struct.unpack_from("<H", data, 0x18)[0]
    fmt, width, height, page_size, at, pf, pc, pa = struct.unpack_from("<8I", data, 0x1c)
    layout = (gw, gh, count, wa, pages, fmt, width, height, page_size, at, pf, pc, pa, len(data))
    supported = (16, 16, 8192, 0x3c, 32, 4, 256, 256, 32768, 0x2040, 3, 16, 0x102040, 0x102080)
    if layout != supported:
        raise GraphicsError(f"Unsupported XFT1 layout: {layout}")
    widths = data[wa:wa + count]
    if any(x > 16 for x in widths):
        raise GraphicsError("Unexpected original glyph width outside 0..16")
    palette = [data[pa + i * 4:pa + i * 4 + 4] for i in range(pc)]
    images = []
    for page in range(pages):
        raster = data[at + page * page_size:at + (page + 1) * page_size]
        rgba = b"".join(palette[(value >> shift) & 15] for value in raster for shift in (0, 4))
        images.append(Image(width, height, rgba, {"page": page, "source_offset": at + page * page_size}))
    header = {"magic": "XFT1", "source_sha256": sha256(data), "glyph_size": [gw, gh],
              "glyph_count": count, "widths_offset": wa, "page_count": pages,
              "atlas_size": [width, height], "page_bytes": page_size, "raster_offset": at,
              "palette_offset": pa, "palette_count": pc, "palette_rgba": [list(x) for x in palette],
              "layout": "linear INDEX4, low nibble first; RGBA8888 palette",
              "runtime_spacing_verified": False, "halfwidth_kana_supported": False}
    return header, widths, images


def glyph_image(pages: list[Image], index: int) -> Image:
    if not 0 <= index < len(pages) * 256:
        raise GraphicsError(f"Glyph index outside XFT atlas: {index}")
    page, slot = divmod(index, 256)
    x, y = (slot % 16) * 16, (slot // 16) * 16
    source = pages[page]
    rgba = b"".join(source.rgba[((y + row) * 256 + x) * 4:((y + row) * 256 + x + 16) * 4] for row in range(16))
    return Image(16, 16, rgba, {"glyph_index": index, "page": page, "atlas_xy": [x, y]})


def render_text(text: str, widths: bytes, pages: list[Image], *, tracking: int = 2) -> Image:
    """Preview using original glyph alpha; throws for unsupported characters."""
    rows = text.split("\n")
    indices = [[glyph_index(char.encode("cp932", errors="strict")) for char in row] for row in rows]
    if any(index >= len(widths) for row in indices for index in row):
        raise GraphicsError("Preview character outside original font")
    width = max(1, max(sum(widths[i] + tracking for i in row) - (tracking if row else 0) for row in indices))
    height = max(1, len(rows) * 18)
    rgba = bytearray(width * height * 4)
    for ri, row in enumerate(indices):
        x = 0
        for index in row:
            glyph = glyph_image(pages, index)
            for y in range(16):
                for gx in range(min(widths[index], 16)):
                    a = glyph.rgba[(y * 16 + gx) * 4 + 3]
                    if a:
                        at = ((ri * 18 + y) * width + x + gx) * 4
                        rgba[at:at + 4] = bytes((255, 255, 255, a))
            x += widths[index] + tracking
    return Image(width, height, bytes(rgba), {"text": text, "tracking": tracking, "runtime_layout_verified": False})


def export_xft(data: bytes, out: Path, provenance: dict | None = None) -> dict:
    header, widths, pages = decode_xft(data)
    out.mkdir(parents=True, exist_ok=True)
    records = []
    for page, image in enumerate(pages):
        bmp = bmp_bytes(image)
        alpha = image.rgba[3::4]
        (out / f"atlas_{page:02}.bmp").write_bytes(bmp)
        (out / f"atlas_{page:02}.alpha8").write_bytes(alpha)
        records.append({**image.summary(), "bmp": f"atlas_{page:02}.bmp", "bmp_sha256": sha256(bmp),
                        "alpha8": f"atlas_{page:02}.alpha8", "alpha8_sha256": sha256(alpha)})
    (out / "widths.bin").write_bytes(widths)
    mappings = []
    for lead in (None, *range(0x81, 0xa0), *range(0xe0, 0xfd)):
        for trail in (range(0x20, 0x7f) if lead is None else range(0x40, 0xfd)):
            if trail == 0x7f:
                continue
            raw = bytes((trail,)) if lead is None else bytes((lead, trail))
            try:
                char = raw.decode("cp932", errors="strict")
                index = glyph_index(raw)
            except (UnicodeError, GraphicsError):
                continue
            if index >= len(widths):
                continue
            page, slot = divmod(index, 256)
            mappings.append({"cp932": raw.hex(), "unicode": char, "glyph_index": index,
                             "width": widths[index], "page": page,
                             "x": (slot % 16) * 16, "y": (slot // 16) * 16})
    manifest = {"header": header, "provenance": provenance or {}, "all_stored_widths": list(widths),
                "widths_bin": "widths.bin", "widths_sha256": sha256(widths), "pages": records,
                "mapping_basis": "game-style CP932 96-column arithmetic; original BOOT evidence in research/graphics/font_decoder_evidence.json",
                "glyph_map": mappings}
    (out / "font_manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    preview = render_text("どこでもいっしょ\nトロとおはなし\nABCDEFGHIJKLMNOPQRSTUVWXYZ\n0123456789", widths, pages)
    (out / "original_font_preview.bmp").write_bytes(bmp_bytes(preview))
    (out / "original_font_preview.png").write_bytes(png_bytes(preview))
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    result = export_xft(args.source.read_bytes(), args.out)
    print(json.dumps({"pages": len(result["pages"]), "mapped_codes": len(result["glyph_map"]), "sha256": result["header"]["source_sha256"]}))


if __name__ == "__main__":
    main()
