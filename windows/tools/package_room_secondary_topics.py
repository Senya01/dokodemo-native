"""The two NEKO room fallback calls after the ordinary topic and time filters."""
from hashlib import sha256
import struct

from iso_reader import require
from ksc_format import parse_code, parse_header


def project_secondary_topics(data, rows=None):
    header = parse_header(data)
    code = data[28:header.pool_start]
    # Native 27EE selects from this exact global array. Nil differs from an
    # empty KSC string. All six cells start nil on a newly adopted profile.
    def global_word(index):
        return struct.unpack_from("<I", data, header.global_start + index * 4)[0]

    require(global_word(0x5995) == 0x90000003, "NEKO rumor slot count changed")
    for slot, row in enumerate((0x5999, 0x599c, 0x599f)):
        require(global_word(0x5996 + slot) == 0xb0040000 + row and
                [global_word(row + i) for i in range(3)] == [0x90000002, 0, 0],
                "NEKO new-game rumor pair changed")
    require(sha256(code[0x27ee:0x2892]).hexdigest() ==
            "6ab5435cfc07b5ce1fac19a41abed845c5c1cbcf9118140cf46641ea3599c799",
            "NEKO rumor selection or slot consumption changed")

    ranges = ((0x4e27, 0x4e50), (0x89acc, 0x89af7),
              (0x7d629, 0x7d747), (0x7d747, 0x7d779), (0x7d779, 0x7d7ab),
              (0x7d7ab, 0x7d7d4), (0x7d7d4, 0x7d801), (0x7d801, 0x7d848),
              (0x7d848, 0x7d880), (0x7d880, 0x7d8f9))
    blocks = {}
    strings = {}
    if rows is None:
        rows = parse_code(data)
    for start, end in ranges:
        require(code[start] == 0x30 and code[end-1:end+1] == b"\x31\x30",
                "NEKO secondary topic bounds changed")
        blocks[start] = code[start:end]
        for row in rows:
            if start <= row["code_offset"] < end:
                for ref in row["references"]:
                    if ref["kind"] == "pool":
                        at = ref["target"]
                        begin = header.pool_start + at
                        strings[at] = data[begin:data.index(0, begin, header.pool_end)]
    return blocks, strings
