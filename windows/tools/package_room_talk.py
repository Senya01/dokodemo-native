"""Selected cold Toro introduction bytecode from the shipped KSC.

Keep script PCs in the bundle. The native conversation owner reads branches,
operands and inline text at runtime, including the separate gift dialogue.
"""
from pathlib import Path
import struct

from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_header


def project_toro_intro(bits, boot):
    h = parse_header(bits)
    code = bits[28:h.pool_start]
    # The cold introduction callback is element 1 in the actor script's
    # own command/function table. The caller supplies a genuinely new profile.
    def word(i):
        return struct.unpack_from("<I", bits, h.global_start+4*i)[0]
    require(word(0x98a2)==0x90000008 and word(0x98a4)==0xb00498ae and
            [word(i) for i in range(0x98ae,0x98b1)]==[0x90000002,0x2000003c,0x70089556],
            "Missing cold invitation command and registered callback")
    require(code[0x89558:0x8955c] == bytes.fromhex("2182bc07"), "NEKO cold handler")
    require(code[0x8995d:0x89967]==bytes.fromhex("859002002002f9990831") and
            code[0x8dfb1:0x8dfb9]==bytes.fromhex("218398081023e008") and
            code[0x8e023:0x8e029]==bytes.fromhex("86200132d908") and
            code[0x8d9ac:0x8d9b2]==bytes.fromhex("852001622900") and
            code[0x2977:0x297f]==bytes.fromhex("802c9000008bf002"),
            "NEKO first meeting return or camera control changed")
    require(word(9) == 0x9000000b and word(18) == 0 and word(0x17) >> 28 == 5,
            "NEKO history/speech table")
    at = h.pool_start + (word(0x17) & 0x0fffffff)
    suffix = bits[at:bits.index(0, at, h.pool_end)]
    farewell = bits[h.pool_start+0xa207:bits.index(0,h.pool_start+0xa207)]
    spans = [(0x204c,0x206d), (0x2151,0x2161), (0x7bc82,0x7c068), (0x7d548,0x7d629), (0x8928f,0x89325),
             (0x8952b,0x895a2)]
    # BOOT 17614 queries two CP932 lists through 175B0. Type 1 attaches
    # to the preceding glyph. Type 2 opens a pair with the following glyph.
    sets=[]
    for field,length in ((0x4368,104),(0x4374,24)):
        text_va,=struct.unpack("<I",boot.read(0xe3c00+field,4))
        s=boot.read(0x80+text_va,length+1)
        require(s[-1:]==b"\0" and b"\0" not in s[:-1], "BOOT line-break dictionary")
        sets.append(s[:-1])
    out = bytearray(struct.pack("<4s5H", b"RTK3", len(spans), len(suffix),len(farewell),
                                *(len(s) for s in sets))) + suffix + farewell + b"".join(sets)
    for start, end in spans:
        require(code[start] == 0x30 and code[end-1] == 0x31,"Invalid NEKO routine bounds")
        out += struct.pack("<II",start,end-start) + code[start:end]
    return bytes(out)


def room_talk_media(root,boot):
    pkg = BparArchive(root.payload(root.by_name("KSNEKO.BPM")))
    script = pkg.payload(pkg.by_name("NEKO.KSC"))
    image = Region(project_toro_intro(script.read(0, script.size),boot))
    return {Path("assets/game/post-profile/toro-introduction.bin"): (image,image.sha256())}
