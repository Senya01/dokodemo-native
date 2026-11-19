"""Extract the predicates and original question/answer forms of Toro's AD2A topic."""
from hashlib import sha256
from pathlib import Path
import struct

from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_code, parse_header


def project_trait_topic(data, rows=None):
    h = parse_header(data)
    code = data[28:h.pool_start]
    if rows is None:
        rows = parse_code(data)
    # These two selections and the vocabulary updates have native owners.
    for start, stop, digest in (
            (0xA942, 0xAAA0, "e08088fcf0889a51a1ef5c07c676c4555405bdaf2792400354f8d3f86832efa8"),
            (0xA8A2, 0xA8E1, "8ec927c9a35d613696b03f013e905c713fa7cb6bcddf958e1332debcbccbb75e"),
            (0x18A5, 0x18E6, "eda272b51f87b39b28b544dff7c2854d36fd414712af4913a48b7f876bea1ec9")):
        require(sha256(code[start:stop]).hexdigest() == digest, "Toro trait selection/store changed")

    blocks, strings = {}, {}
    for start, stop in ((0xA862, 0xA883), (0xA883, 0xA8A2), (0xAB1B, 0xACBB),
                        (0xACBB, 0xAD13), (0xAD13, 0xAD2A), (0xAD2A, 0xAD47)):
        ending = bytes.fromhex("1075ab00") if start == 0xAB1B else b"\x31"
        require(code[start] == 0x30 and code[stop] == 0x30 and
                code[stop-len(ending):stop] == ending, "Wrong trait routine extent")
        blocks[start] = code[start:stop]
        for row in rows:
            if start <= row["code_offset"] < stop:
                for ref in row["references"]:
                    if ref["kind"] == "pool":
                        at = ref["target"]
                        begin = h.pool_start + at
                        strings[at] = data[begin:data.index(0, begin, h.pool_end)]
    return blocks, strings


def room_word_traits_media(root):
    archive = BparArchive(root.payload(root.by_name("KSNEKO.BPM")))
    region = archive.payload(archive.by_name("NEKO.KSC"))
    data = region.read(0, region.size)
    h = parse_header(data)
    output = bytearray(b"RWT1")
    for ct in (0x2A9E, 0x2AB5, 0x2ACC):
        row = struct.unpack_from("<23I", data, h.constant_start + 4*ct)
        require(row[0] == 0x90000016, "Expected 22 KSC traits")
        for index, value in enumerate(row[1:]):
            if ct == 0x2ACC and not 6 <= index <= 12:
                require(value == 0, "Unexpected opposite adjective")
                output.extend(b"\0\0")
            else:
                require(value >> 28 == 5, "Expected trait string")
                start = h.pool_start + (value & 0x0fffffff)
                end = data.index(0, start, h.pool_end)
                form = data[start:end]
                require(form and form.decode("cp932"), "Empty trait string")
                output.extend(struct.pack("<H", len(form)))
                output.extend(form)
    blob = Region(bytes(output))
    return {Path("assets/game/post-profile/toro-trait-labels.bin"): (blob, blob.sha256())}
