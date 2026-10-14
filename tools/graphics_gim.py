"""Autonomous little-endian PSP GIM reader and Win32 BMP exporter.

No external modules, SDK, GimConv executable, or emulator is used. The module
decodes every picture, mip level and frame; unsupported formats fail explicitly.
RGBA bytes returned by decode_gim are top-down and have straight alpha.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass, asdict
import hashlib
import json
from pathlib import Path
import struct
import zlib


class GraphicsError(ValueError):
    pass


FORMATS = {0: ("RGB565", 16), 1: ("RGBA5551", 16), 2: ("RGBA4444", 16),
           3: ("RGBA8888", 32), 4: ("INDEX4", 4), 5: ("INDEX8", 8),
           6: ("INDEX16", 16), 7: ("INDEX32", 32)}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def align(value: int, alignment: int) -> int:
    if alignment <= 0 or alignment & (alignment - 1):
        raise GraphicsError(f"Invalid alignment {alignment}")
    return (value + alignment - 1) & -alignment


def checked(data: bytes, start: int, size: int, end: int | None = None) -> bytes:
    limit = len(data) if end is None else min(end, len(data))
    if start < 0 or size < 0 or start + size > limit:
        raise GraphicsError(f"Read outside resource: 0x{start:x}+0x{size:x}, end=0x{limit:x}")
    return data[start:start + size]


@dataclass(frozen=True)
class Block:
    offset: int
    type: int
    size: int
    next_offset: int
    data_offset: int
    children: tuple["Block", ...] = ()


def _block(data: bytes, at: int, limit: int, depth: int = 0) -> Block:
    if depth > 16:
        raise GraphicsError("GIM block nesting exceeds limit")
    typ, header, size, nxt, payload = struct.unpack("<HHIII", checked(data, at, 16, limit))
    if header != 16 or size < 16 or at + size > limit or not 16 <= payload <= size:
        raise GraphicsError(f"Invalid block at 0x{at:x}: type={typ}, size={size}, data={payload}")
    children = []
    if typ in (2, 3):
        if nxt != payload:
            raise GraphicsError(f"Container child pointer differs from data pointer at 0x{at:x}")
        pos = at + payload
        while pos < at + size:
            child = _block(data, pos, at + size, depth + 1)
            children.append(child)
            pos += child.size
        if pos != at + size:
            raise GraphicsError("Invalid container boundary")
    elif nxt != size:
        raise GraphicsError(f"Noncontiguous leaf at 0x{at:x}: next={nxt}, size={size}")
    return Block(at, typ, size, nxt, payload, tuple(children))


@dataclass(frozen=True)
class RasterHeader:
    offset: int
    format: int
    order: int
    width: int
    height: int
    bpp: int
    pitch_alignment: int
    height_alignment: int
    level_type: int
    levels: int
    frame_type: int
    frames: int
    offsets: tuple[int, ...]
    raster_end: int
    chunk_end: int


def raster_header(data: bytes, block: Block) -> RasterHeader:
    at, end = block.offset + block.data_offset, block.offset + block.size
    h = checked(data, at, 48, end)
    hsize, reserved, fmt, order, width, height, bpp, pitch, ha = struct.unpack_from("<9H", h)
    index_at, raster_start, raster_end, mask = struct.unpack_from("<4I", h, 24)
    lt, levels, ft, frames = struct.unpack_from("<4H", h, 40)
    if hsize != 48 or reserved or fmt not in FORMATS or order not in (0, 1):
        raise GraphicsError(f"Unsupported raster header at 0x{at:x}: fmt={fmt}, order={order}, size={hsize}")
    if bpp != FORMATS[fmt][1] or not width or not height or mask:
        raise GraphicsError(f"Invalid dimensions/bpp or unsupported plane mask at 0x{at:x}")
    if not 1 <= levels <= 16 or not 1 <= frames <= 1024 or ft != 3 or lt not in (1, 2):
        raise GraphicsError(f"Unsupported levels/frames at 0x{at:x}")
    align(1, pitch)
    align(1, ha)
    if not hsize <= index_at < raster_start <= raster_end <= end - at:
        raise GraphicsError(f"Invalid raster/index bounds at 0x{at:x}")
    offsets = struct.unpack(f"<{levels * frames}I", checked(data, at + index_at, levels * frames * 4, at + raster_start))
    if offsets[0] != raster_start or any(x < raster_start or x >= raster_end for x in offsets):
        raise GraphicsError(f"Raster offsets outside payload at 0x{at:x}")
    if any(a >= b for a, b in zip(offsets, offsets[1:])):
        raise GraphicsError(f"Raster offsets not strictly increasing at 0x{at:x}")
    return RasterHeader(at, fmt, order, width, height, bpp, pitch, ha,
                        lt, levels, ft, frames, tuple(offsets), raster_end, end)


def unswizzle(raw: bytes, stride: int, rows: int) -> bytes:
    """PSP byte blocks: 16 horizontal bytes by 8 rows (not Morton order)."""
    if stride % 16 or rows % 8 or len(raw) != stride * rows:
        raise GraphicsError("Swizzled raster requires whole 16-byte x 8-row blocks")
    output = bytearray(len(raw))
    blocks_across = stride // 16
    for y in range(rows):
        row_at = y * stride
        for block_x in range(blocks_across):
            src = ((y // 8) * blocks_across + block_x) * 128 + (y % 8) * 16
            dst = row_at + block_x * 16
            output[dst:dst + 16] = raw[src:src + 16]
    return bytes(output)


def direct_color(value: int, fmt: int) -> bytes:
    if fmt == 3:
        return value.to_bytes(4, "little")
    if fmt == 0:
        r, g, b = value & 31, (value >> 5) & 63, (value >> 11) & 31
        return bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255))
    if fmt == 1:
        r, g, b = value & 31, (value >> 5) & 31, (value >> 10) & 31
        return bytes(((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), 255 if value & 0x8000 else 0))
    if fmt == 2:
        return bytes(((value & 15) * 17, ((value >> 4) & 15) * 17,
                      ((value >> 8) & 15) * 17, ((value >> 12) & 15) * 17))
    raise GraphicsError(f"Not a direct color format: {fmt}")


def _raster(data: bytes, h: RasterHeader, frame: int, level: int, palette: bytes | None) -> tuple[int, int, bytes, dict]:
    if not 0 <= frame < h.frames or not 0 <= level < h.levels:
        raise GraphicsError("Frame/level outside raster")
    # Palette levels do not shrink; image mip levels do.
    width, height = (h.width, h.height) if h.level_type == 2 else (max(1, h.width >> level), max(1, h.height >> level))
    stride = align((width * h.bpp + 7) // 8, h.pitch_alignment)
    rows = align(height, h.height_alignment)
    if h.order == 1:
        stride, rows = align(stride, 16), align(rows, 8)
    index = frame * h.levels + level
    start = h.offset + h.offsets[index]
    limit = h.offset + (h.offsets[index + 1] if index + 1 < len(h.offsets) else h.raster_end)
    raw = checked(data, start, stride * rows, limit)
    if h.order == 1:
        raw = unswizzle(raw, stride, rows)
    if h.format >= 4 and (palette is None or len(palette) % 4):
        raise GraphicsError("Indexed image is missing a valid palette")
    output = bytearray(width * height * 4)
    for y in range(height):
        row = raw[y * stride:(y + 1) * stride]
        for x in range(width):
            if h.bpp == 4:
                value = (row[x // 2] >> (4 * (x % 2))) & 15
            else:
                size = h.bpp // 8
                value = int.from_bytes(row[x * size:(x + 1) * size], "little")
            if h.format < 4:
                pixel = direct_color(value, h.format)
            else:
                if value >= len(palette) // 4:
                    raise GraphicsError(f"Palette index {value} outside {len(palette) // 4} colors at x={x}, y={y}")
                pixel = palette[value * 4:value * 4 + 4]
            dst = (y * width + x) * 4
            output[dst:dst + 4] = pixel
    return width, height, bytes(output), {"raster_offset": start, "raster_bytes": stride * rows,
                                         "stride_bytes": stride, "stored_rows": rows}


@dataclass(frozen=True)
class Image:
    width: int
    height: int
    rgba: bytes
    metadata: dict

    def summary(self) -> dict:
        return {**self.metadata, "width": self.width, "height": self.height,
                "rgba_sha256": sha256(self.rgba)}


def decode_gim(data: bytes) -> list[Image]:
    if checked(data, 0, 16) != b"MIG.00.1PSP\0\0\0\0\0":
        raise GraphicsError("Expected a little-endian PSP GIM signature")
    root = _block(data, 16, len(data))
    if root.type != 2 or root.offset + root.size != len(data):
        raise GraphicsError("Root does not span the GIM resource")
    output = []
    pictures = [x for x in root.children if x.type == 3]
    for pi, picture in enumerate(pictures):
        images = [x for x in picture.children if x.type == 4]
        palettes = [x for x in picture.children if x.type == 5]
        if len(images) != 1 or len(palettes) > 1 or any(x.type not in (4, 5, 255) for x in picture.children):
            raise GraphicsError(f"Unsupported picture block topology at 0x{picture.offset:x}")
        h = raster_header(data, images[0])
        pal_h = raster_header(data, palettes[0]) if palettes else None
        if h.level_type != 1 or (pal_h and (pal_h.format >= 4 or pal_h.level_type != 2)):
            raise GraphicsError("Invalid image/palette level type or palette format")
        for frame in range(h.frames):
            for level in range(h.levels):
                palette = None
                if pal_h:
                    _, _, palette, _ = _raster(data, pal_h, min(frame, pal_h.frames - 1), min(level, pal_h.levels - 1), None)
                w, ht, rgba, provenance = _raster(data, h, frame, level, palette)
                output.append(Image(w, ht, rgba, {"picture": pi, "frame": frame, "level": level,
                    "image_block_offset": images[0].offset, "format": FORMATS[h.format][0],
                    "format_code": h.format, "pixel_order": "swizzled" if h.order else "linear",
                    "level_count": h.levels, "frame_count": h.frames,
                    "palette_format": FORMATS[pal_h.format][0] if pal_h else None,
                    "palette_block_offset": palettes[0].offset if pal_h else None,
                    **provenance}))
    if not output:
        raise GraphicsError("GIM contains no image pictures")
    return output


def bmp_bytes(image: Image, bpp: int = 32) -> bytes:
    """Win32-compatible BITMAPINFOHEADER/BI_RGB; BGRA32 or opaque BGR24.

    Alpha is preserved in byte 3 of each BGRA32 pixel. BI_RGB itself does not
    declare an alpha mask; renderers must deliberately consume/premultiply it.
    The V4/BI_BITFIELDS variant was rejected by Windows LoadImageW in testing.
    """
    w, h, rgba = image.width, image.height, image.rgba
    if len(rgba) != w * h * 4 or bpp not in (24, 32):
        raise GraphicsError("Invalid image or BMP bit depth")
    stride = align(w * (bpp // 8), 4)
    pixels = bytearray(stride * h)
    for y in range(h):
        dst = (h - 1 - y) * stride
        for x in range(w):
            at = (y * w + x) * 4
            r, g, b, a = rgba[at:at + 4]
            pixel = bytes((b, g, r, a)) if bpp == 32 else bytes((b, g, r))
            off = dst + x * (bpp // 8)
            pixels[off:off + len(pixel)] = pixel
    header = struct.pack("<IiiHHIIiiII", 40, w, h, 1, bpp, 0,
                         len(pixels), 2835, 2835, 0, 0)
    offset = 14 + len(header)
    return struct.pack("<2sIHHI", b"BM", offset + len(pixels), 0, 0, offset) + header + pixels


def png_bytes(image: Image) -> bytes:
    """Small standard-library PNG writer, for visual verification only."""
    def chunk(kind: bytes, payload: bytes) -> bytes:
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)
    scanlines = b"".join(b"\0" + image.rgba[y * image.width * 4:(y + 1) * image.width * 4] for y in range(image.height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", image.width, image.height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b""))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--out", type=Path, required=True, help="Output directory")
    parser.add_argument("--bpp", type=int, choices=(24, 32), default=32)
    parser.add_argument("--png", action="store_true")
    args = parser.parse_args()
    data = args.source.read_bytes()
    args.out.mkdir(parents=True, exist_ok=True)
    manifest = {"source": str(args.source), "source_sha256": sha256(data), "images": []}
    for image in decode_gim(data):
        meta = image.summary()
        name = f"{args.source.stem}_p{meta['picture']}_f{meta['frame']}_m{meta['level']}"
        bmp = bmp_bytes(image, args.bpp)
        (args.out / (name + ".bmp")).write_bytes(bmp)
        meta.update(bmp=name + ".bmp", bmp_sha256=sha256(bmp))
        if args.png:
            (args.out / (name + ".png")).write_bytes(png_bytes(image))
        manifest["images"].append(meta)
    (args.out / (args.source.stem + ".json")).write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"source_sha256": manifest["source_sha256"], "images": len(manifest["images"]), "out": str(args.out)}))


if __name__ == "__main__":
    main()
