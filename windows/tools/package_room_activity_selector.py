"""Project the ordered NEKO activity rows used by the first room idle selector."""
from pathlib import Path
import struct

from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_header


def project_activity_rows(script):
    header = parse_header(script)
    code = script[28:header.pool_start]
    require(code[0x13525:0x13535] == bytes.fromhex("91070095066199df2495b6968b600389"),
            "NEKO activity array/filter call changed")
    require(code[0x1353d:0x13551] == bytes.fromhex("95066199df249007008b110189f4f48001f47031"),
            "NEKO activity row index or command extraction changed")

    def words(start, index, count):
        return struct.unpack_from(f"<{count}I", script, start + 4 * index)

    g = header.global_start
    conditions = words(g, 0x96b6, 19)
    require(conditions[0] == 0x90000012, "NEKO activity predicate count changed")
    require(conditions[1::3] == tuple(0x20000000 | k for k in (0, 2, 3, 4, 5, 6)) and
            conditions[2::3] == tuple(0x20000000 | op for op in (0, 0, 6, 6, 6, 6)),
            "NEKO activity predicate shape changed")
    capacity, = words(header.constant_start, 0x24df, 1)
    require(capacity == 0x900002bc, "NEKO filter destination capacity changed")
    count, = words(g, 0x6106, 1)
    require(count == 0x900005f3, "NEKO activity list count changed")
    count &= 0xfff
    out = bytearray(struct.pack("<4sHH", b"RAS1", count, capacity & 0xfff))
    for reference in words(g, 0x6107, count):
        require(reference >> 16 == 0xb004, "Expected NEKO global record reference")
        index = reference & 0xffff
        require(index + 8 <= header.global_count, "NEKO activity record outside global range")
        tag, *cells = words(g, index, 8)
        require(tag == 0x90000007 and all(w >> 16 == 0x2000 for w in cells),
                "Expected seven unsigned16 source integer values")
        out.extend(struct.pack("<7H", *(w & 0xffff for w in cells)))
    return bytes(out)


def room_activity_media(root):
    archive = BparArchive(root.payload(root.by_name("KSNEKO.BPM")))
    image = archive.payload(archive.by_name("NEKO.KSC"))
    rows = Region(project_activity_rows(image.read(0, image.size)))
    return {Path("assets/game/post-profile/toro-room-activities.bin"): (rows, rows.sha256())}
