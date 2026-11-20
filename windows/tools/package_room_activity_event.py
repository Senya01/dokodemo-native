"""Project each actor's ordinary-conversation table from the local disc."""
import struct
from pathlib import Path

from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_header


def project_event(script, actor, entry, rows_index, predicate, count):
    header = parse_header(script)
    code = script[28:header.pool_start]
    def word(base, index):
        return struct.unpack_from("<I", script, base + 4 * index)[0]

    row_ref = struct.pack("<H", rows_index)
    require(code[entry:entry + 7] == bytes.fromhex("300e4e02000800") and
            code[entry + 0x1e:entry + 0x2a] ==
            b"\x95" + row_ref + bytes.fromhex("91020099") + struct.pack("<H", predicate) + bytes.fromhex("8b6003") and
            code[entry + 0x62:entry + 0x6d] ==
            b"\x95" + row_ref + bytes.fromhex("900c00f485f58451"),
            f"{actor} ordinary topic filter or consumption changed")
    require([word(header.constant_start, predicate + j) for j in range(4)] ==
            [0x90000003, 0x20000000, 0x20000000, 0],
            f"{actor} single predicate column 0 changed")
    require(code[entry + 0x72:entry + 0x7c] ==
            bytes.fromhex("3001") + b"\x95" + row_ref + bytes.fromhex("85f485f4b2"),
            f"{actor} birthday row initializer changed")
    g = header.global_start
    require(word(g, rows_index) == (0x90000000 | count), f"{actor} topic count")
    out = bytearray(struct.pack("<4sHH", b"REV1", count, code[entry + 5]))
    for j in range(count):
        ref = word(g, rows_index + 1 + j)
        require(ref >> 16 == 0xb004, f"{actor} topic row reference")
        row = ref & 0xffff
        size, category, fn = struct.unpack_from("<3I", script, g + 4 * row)
        require(size == 0x90000002 and category >> 28 == 2, f"{actor} topic row fields")
        choices = [fn]
        if fn >> 16 == 0xb003:
            index = fn & 0xffff
            length = word(header.constant_start, index)
            require(length >> 28 == 9 and 1 < (length & 0xffff) <= 15, f"{actor} indirect topic list")
            choices = [word(header.constant_start, index + 1 + n) for n in range(length & 0xffff)]
        require(all(choice >> 28 == 7 and code[choice & 0xfffffff] == 0x30 for choice in choices),
                f"{actor} topic function reference")
        out.extend(struct.pack("<IB", category & 0xfffffff, len(choices)))
        out.extend(struct.pack(f"<{len(choices)}I", *(choice & 0xfffffff for choice in choices)))
    return bytes(out)


def room_activity_event_media(root):
    files = {}
    for actor, stem, entry, rows, predicate, count in (
        ("NEKO", "toro", 0x88a23, 0x96c9, 0xe1c3, 118),
        ("ROBO", "robo", 0x665de, 0x92ea, 0xc97b, 72),
        ("USAGI", "usagi", 0x625a4, 0x9507, 0x9b4d, 102),
        ("INU", "inu", 0x65935, 0x9568, 0x951d, 113),
        ("KAERU", "kaeru", 0x664ab, 0x9388, 0x99b3, 90),
    ):
        archive = BparArchive(root.payload(root.by_name(f"KS{actor}.BPM")))
        ksc = archive.payload(archive.by_name(f"{actor}.KSC"))
        result = Region(project_event(ksc.read(0, ksc.size), actor, entry, rows, predicate, count))
        files[Path(f"assets/game/post-profile/{stem}-room-events.bin")] = (result, result.sha256())
    return files
