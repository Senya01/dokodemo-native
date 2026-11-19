"""The shipped NEKO ordinary-conversation callback table and its filter."""
import struct
from pathlib import Path

from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_header


def project_toro_event(script):
    header = parse_header(script)
    code = script[28:header.pool_start]
    def word(base, index):
        return struct.unpack_from("<I", script, base + 4 * index)[0]

    require(code[0x88a23:0x88a2a] == bytes.fromhex("300e4e02000800") and
            code[0x88a41:0x88a4d] == bytes.fromhex("95c99691020099c3e18b6003") and
            code[0x88a85:0x88a90] == bytes.fromhex("95c996900c00f485f58451"),
            "NEKO ordinary topic filter or consumption changed")
    require([word(header.constant_start, 0xe1c3 + j) for j in range(4)] ==
            [0x90000003, 0x20000000, 0x20000000, 0],
            "NEKO single predicate column 0 changed")
    g = header.global_start
    count = word(g, 0x96c9)
    require(count == 0x90000076, "NEKO topic count")
    out = bytearray(struct.pack("<4sHH", b"REV1", count & 0xffff, code[0x88a28]))
    for j in range(count & 0xffff):
        ref = word(g, 0x96ca + j)
        require(ref >> 16 == 0xb004, "NEKO topic row reference")
        row = ref & 0xffff
        size, category, fn = struct.unpack_from("<3I", script, g + 4 * row)
        require(size == 0x90000002 and category >> 28 == 2, "NEKO topic row fields")
        choices = [fn]
        if fn >> 16 == 0xb003:
            index = fn & 0xffff
            length = word(header.constant_start, index)
            require(length >> 28 == 9 and 1 < (length & 0xffff) <= 15, "NEKO indirect topic list")
            choices = [word(header.constant_start, index + 1 + n) for n in range(length & 0xffff)]
        require(all(choice >> 28 == 7 and code[choice & 0xfffffff] == 0x30 for choice in choices),
                "NEKO topic function reference")
        out.extend(struct.pack("<IB", category & 0xfffffff, len(choices)))
        out.extend(struct.pack(f"<{len(choices)}I", *(choice & 0xfffffff for choice in choices)))
    return bytes(out)


def room_activity_event_media(root):
    archive = BparArchive(root.payload(root.by_name("KSNEKO.BPM")))
    ksc = archive.payload(archive.by_name("NEKO.KSC"))
    result = Region(project_toro_event(ksc.read(0, ksc.size)))
    return {Path("assets/game/post-profile/toro-room-events.bin"): (result, result.sha256())}
