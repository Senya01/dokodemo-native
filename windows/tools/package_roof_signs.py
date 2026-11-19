"""Scene-12 AREAMARKERs, the registered SBOARD templates, and their glyph banks."""
from pathlib import Path
import struct

from extract_bpar import BparArchive
from graphics_gim import _block, raster_header
from iso_reader import Region, require
from model_formats import parse_dvs


def glyph_indices(raw):
    # Preserve the original INDEX4 nibbles and all sixteen RGBA palette entries.
    require(raw[:16] == b"MIG.00.1PSP\0\0\0\0\0", "Invalid roof glyph GIM")
    top = _block(raw, 16, len(raw))
    require(top.type == 2 and len(top.children) == 1 and top.children[0].type == 3,
            "Unexpected roof glyph picture count")
    blocks = top.children[0].children
    require(sorted(b.type for b in blocks) == [4, 5], "Unexpected roof glyph block layout")
    tex, pal = [raster_header(raw, b) for b in sorted(blocks, key=lambda b: b.type)]
    require((pal.format, pal.width, pal.height, pal.level_type) == (3, 16, 1, 2)
            and (tex.format, tex.width, tex.height, tex.level_type, tex.pitch_alignment) == (4, 16, 16, 1, 16)
            and all(h.frames == h.levels == 1 and h.order == 0 for h in (pal, tex)),
            "Unsupported roof glyph raster")
    p, t = [h.offset + h.offsets[0] for h in (pal, tex)]
    require(p + 64 <= pal.offset + pal.raster_end and t + 256 <= tex.offset + tex.raster_end,
            "Roof glyph palette or rows exceed raster")
    return raw[p:p+64] + b"".join(raw[t+y*16:t+y*16+8] for y in range(16))


def roof_sign_media(root, boot):
    directory = Path("assets/game/post-profile")
    files = {}
    def emit(name, data):
        region = Region(bytes(data))
        files[directory / name] = (region, region.sha256())
    def member(archive, name):
        region = archive.payload(archive.by_name(name))
        return region.read(0, region.size)

    ph, = struct.unpack("<I", boot.read(28, 4))
    text, data = [struct.unpack("<I", boot.read(ph+i*32+4, 4))[0] for i in range(2)]
    def cstring(at):
        raw = boot.read(at, 64)
        require(b"\0" in raw, "Unterminated roof text or path")
        return raw.split(b"\0", 1)[0]
    def pointed(at):
        offset, = struct.unpack("<I", boot.read(data+at, 4))
        return cstring(text+offset)
    def field(raw):
        require(len(raw) < 256, "Roof text field too long")
        return bytes([len(raw)]) + raw

    # 1B8F4 converts each cell through the BOOT single-byte expansion map.
    metadata = bytearray(b"RSG1") + field(cstring(text+0xe0760))
    keys = boot.read(data+0x4b14, 96)
    for i, key in enumerate(keys):
        metadata += bytes([key]) + field(pointed(0x4b74+4*i))

    styles = set()
    for reg in (66, 67, 68):
        name = cstring(data+0x43c4+reg*20).decode("ascii")
        archive = BparArchive(root.payload(root.by_name(name+".BPM")))
        raw = member(archive, name+"/MODEL01.DVS")
        model = parse_dvs(raw)
        markers = [n for n in model["nodes"] if n["type"] == 13]
        metadata += struct.pack("<II", reg, len(markers))
        for n in markers:
            cell, mode, style, material, _ = struct.unpack("<3HhI", raw[n["payload_offset"]:n["end"]])
            require(1 <= cell <= 8 and mode in (1, 2) and 0 <= material < len(model["materials"]),
                    "Unsupported rooftop sign attachment")
            require(n["common"]["flags"] == 3, "Rooftop sign transform changed")
            parent = n["common"]["short_fields"][1]
            while parent >= 0:
                common = model["nodes"][parent]["common"]
                require(common["flags"] in (0, 1), "Unsupported scaled rooftop sign ancestor")
                parent = common["short_fields"][1]
            desc = bytes.fromhex(model["materials"][material]["raw_common_hex"])
            ambient, diffuse, _, emission = struct.unpack_from("<4I", desc, 4)
            require(ambient == diffuse and not emission & 0xffffff and not desc[21],
                    "Unsupported rooftop sign lighting")
            style += -1 if mode == 1 else 499
            styles.add(style)
            metadata += struct.pack("<5I", n["index"], cell-1, mode, style, diffuse & 0xffffff)
    emit("roof-signs.bin", metadata)

    mount = cstring(data+0x43c4+29*20).decode("ascii")
    sboard = BparArchive(root.payload(root.by_name(mount+".BPM")))
    model_dir = cstring(text+0xd9c68).decode("ascii").upper()
    for i in range(2, 18):
        raw = member(sboard, model_dir+"/"+pointed(0x4acc+4*i).decode("ascii").upper())
        nodes = parse_dvs(raw)["nodes"]
        require(len(nodes) == 1+(i-2)%8 and all(n["type"] == 1 and n["geometry"]["vertex_count"]
                and n["geometry"]["material_index"] is not None for n in nodes), "Invalid sign template geometry")
        # Every material is rebound to an authored glyph before this template can draw.
        emit(f"roof-sign-panel-{i}.mesh", struct.pack("<4sII", b"RSM1", 0, len(raw))+raw+bytes(4))

    fmt = cstring(text+0xd9c50).decode("ascii").replace("\\", "/")
    for style in sorted(styles):
        mount, _, _ = (fmt % (style, style, 0)).split("/")
        archive = BparArchive(root.payload(root.by_name(mount+".BPM")))
        bank = bytearray(struct.pack("<4sI", b"RFG1", len(archive.members)))
        for e in sorted(archive.members, key=lambda m: m.name):
            code = int(Path(e.name).stem[1:])
            require(mount+"/"+e.name == fmt % (style, style, code) and 0 <= code <= 65535,
                    "Glyph bank filename is inconsistent with BOOT format")
            bank += struct.pack("<H", code) + glyph_indices(archive.payload(e).read(0, e.size))
        emit(f"roof-sign-font-{style}.bin", bank)
    return files
