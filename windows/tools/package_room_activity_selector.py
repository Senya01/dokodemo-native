"""Project the ordered activity rows used by each character's idle selector."""
from pathlib import Path
import struct

from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_header


# Filter call, ordered row table, result array, predicate, expected row count.
ACTIVITY_TABLES = {
    "NEKO": (0x13525, 0x6106, 0x24df, 0x96b6, 1523),
    "ROBO": (0x13f8d, 0x610e, 0x2596, 0x92d7, 1412),
    "USAGI": (0x13b2c, 0x610f, 0x21e4, 0x94f4, 1472),
    "INU": (0x145b7, 0x611f, 0x20c2, 0x9555, 1481),
    "KAERU": (0x140a8, 0x610a, 0x2160, 0x9375, 1430),
}


def project_activity_rows(script, species="NEKO"):
    header = parse_header(script)
    code = script[28:header.pool_start]
    site, table, output, predicate, row_count = ACTIVITY_TABLES[species]
    prefix = struct.pack("<3sBHBHBH4s", bytes.fromhex("910700"), 0x95, table,
                         0x99, output, 0x95, predicate, bytes.fromhex("8b600389"))
    require(code[site:site+len(prefix)] == prefix, f"{species} activity array/filter call changed")
    select = (struct.pack("<BHBH", 0x95, table, 0x99, output) +
              bytes.fromhex("9007008b110189f4f48001f47031"))
    require(code[site+24:site+24+len(select)] == select, f"{species} activity row lookup changed")

    def words(start, index, count):
        return struct.unpack_from(f"<{count}I", script, start + 4 * index)

    g = header.global_start
    conditions = words(g, predicate, 19)
    require(conditions[0] == 0x90000012, f"{species} activity predicate count changed")
    require(conditions[1::3] == tuple(0x20000000 | k for k in (0, 2, 3, 4, 5, 6)) and
            conditions[2::3] == tuple(0x20000000 | op for op in (0, 0, 6, 6, 6, 6)),
            f"{species} activity predicate shape changed")
    capacity, = words(header.constant_start, output, 1)
    require(capacity == 0x900002bc, f"{species} filter destination capacity changed")
    require(words(g, table, 1) == (0x90000000 | row_count,), f"{species} activity list count changed")
    out = bytearray(struct.pack("<4sHH", b"RAS1", row_count, capacity & 0xfff))
    for reference in words(g, table+1, row_count):
        require(reference >> 16 == 0xb004, f"Expected {species} global record reference")
        index = reference & 0xffff
        require(index + 8 <= header.global_count, f"{species} activity record outside global range")
        tag, *cells = words(g, index, 8)
        require(tag == 0x90000007 and all(w >> 16 == 0x2000 for w in cells),
                "Expected seven unsigned16 source integer values")
        out.extend(struct.pack("<7H", *(w & 0xffff for w in cells)))
    return bytes(out)


def room_activity_media(root):
    files = {}
    for species in ACTIVITY_TABLES:
        name = "toro" if species == "NEKO" else species.lower()
        archive = BparArchive(root.payload(root.by_name("KS"+species+".BPM")))
        image = archive.payload(archive.by_name(species+".KSC"))
        rows = Region(project_activity_rows(image.read(0, image.size), species))
        files[Path(f"assets/game/post-profile/{name}-room-activities.bin")] = rows, rows.sha256()
    return files
