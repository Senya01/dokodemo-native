"""Prepare the original SOFTKEY clip and indexed textures from DATA.BPM."""
from pathlib import Path
import struct

from extract_bpar import BparArchive
from graphics_gim import decode_gim
from graphics_ui import parse_cb2a
from iso_reader import Region


def nickname_media(root: BparArchive, boot: Region):
    common = BparArchive(root.payload(root.by_name("DATA.BPM")))
    name = "GUI/SOFTKEY/SOFTKEY.CB2A"
    clip = common.payload(common.by_name(name))
    scene = parse_cb2a(clip.read(0, clip.size), name)
    pixels = bytearray(b"TTX1" + struct.pack("<I", len(scene["sprites"])))
    for sprite in scene["sprites"]:
        image = common.payload(common.by_name(sprite["resource"].upper()))
        texture, = decode_gim(image.read(0, image.size))
        pixels.extend(struct.pack("<HH", texture.width, texture.height))
        pixels.extend(texture.rgba)
    bank = Region(bytes(pixels))
    # BOOT 22AC8 sets the mode-0/page-0 kana group pointers (22B14..22BB8).
    # 1FE68 -> 202FC overrides row 3/col 0 for type 1; PH1+4384 points
    # to the two permissible marks at D9144. Cell 11 is disabled for type 1.
    group_vas = (0xD9248, 0xD927C, 0xD935C, 0xD92A8, 0xD93A4, 0xD9318,
                 0xD93D0, 0xD9388, 0xD9414, 0xD9144, 0xD93BC)
    groups = bytearray()
    for va in group_vas:
        text = boot.read(0x80 + va, 96)
        groups.extend(text[:text.index(0) + 1])
    # 1FE8C and 1FEA4 supply the two type-0 pages initialized by 22AC8.
    # Regrouping the ASCII page or dropping tabs changes the accessible cells.
    word_pages = (
        (0xD9248, 0xD927C, 0xD935C, 0xD92A8, 0xD93A4, 0xD9318,
         0xD93D0, 0xD9388, 0xD9414, 0xD912C, 0xD93BC, 0xD914C),
        (0xD9578, 0xD95C8, 0xD95D0, 0xD95D8, 0xD95E0, 0xD95E8,
         0xD95F0, 0xD95FC, 0xD9604, 0xD95A4, 0xD95A0, 0xD914C),
    )
    # 207B0 and 20714 switch the same keys to katakana-first and
    # lowercase-first source rows after the player chose that character class.
    alternate_pages = (
        (0xD92E4, 0xD93E8, 0xD9468, 0xD942C, 0xD94D8, 0xD9494,
         0xD950C, 0xD94F0, 0xD9538, 0xD912C, 0xD9524, 0xD914C),
        (0xD9578, 0xD9550, 0xD9558, 0xD9560, 0xD9568, 0xD9570,
         0xD9580, 0xD958C, 0xD9594, 0xD95A4, 0xD95A0, 0xD914C),
    )

    # Type 2 uses the two descriptors initialized at 2A838 and 2A868.
    reading_pages = (
        (0xD95B0, 0xD9610, 0xD9628, 0xD9640, 0xD9660, 0xD966C,
         0xD9690, 0xD969C, 0xD96AC, 0xD96B8, 0xD96BC, 0xD914C),
        (0xD95A0, 0xD96C8, 0xD96CC, 0xD96D0, 0xD96D4, 0xD96D8,
         0xD96E4, 0xD96F4, 0xD96EC, 0xD96E0, 0xD95A0, 0xD914C),
    )

    def encode_groups(pages):
        packed = bytearray(b"WGR1")
        for page in pages:
            for va in page:
                at = 0x80 + va
                end = boot.find_nul(at, at + 96)
                if end < 0:
                    raise ValueError("Unterminated SOFTKEY character group")
                packed.extend(boot.read(at, end - at + 1))
        resource = Region(bytes(packed))
        return resource, resource.sha256()

    return {
        Path("assets/game/title/word-groups.bin"): encode_groups(word_pages),
        Path("assets/game/title/word-groups-alternate.bin"): encode_groups(alternate_pages),
        Path("assets/game/title/reading-groups.bin"): encode_groups(reading_pages),
        Path("assets/game/title/nickname-groups.bin"): (
            Region(bytes(groups)), "9c9974559e2534628fcf743a426a5629782161e4914a1174cf069b2e15e44348"),
        Path("assets/game/title/SOFTKEY.CB2A"): (
            clip, "4a4096fecd1478a4bd7e77d62670075f592aca586033adafc77c88961a67e36f"),
        Path("assets/game/title/softkey-textures.bin"): (
            bank, "257404b799b85ba4677eef5cebca45c65087f0c7751f90b3078bf166b5049490"),
    }
