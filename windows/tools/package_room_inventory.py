"""Read each actor's first-gift candidates and source text from its KSC."""
import struct
from pathlib import Path

from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_header


def first_gift_records(root, actor):
    archive = BparArchive(root.payload(root.by_name("KS" + actor + ".BPM")))
    script = archive.payload(archive.by_name(actor + ".KSC"))
    data = script.read(0, script.size)
    h = parse_header(data)
    # E.g. NEKO 3350 selects ctx+28[291B][builtin17(5)] then
    # allocates a 10-cell row and calls 3161 and 31B5 to fill it.
    sig = bytes.fromhex("80058b110189f4514e02000a009102009001002002")
    code = data[28:h.pool_start]
    require(code.count(sig) == 1, "Ambiguous first gift constructor")
    at = code.index(sig)
    require(code[at-3] == 0x99, "Missing literal candidate list")
    choices = int.from_bytes(code[at-2:at], "little")
    init = int.from_bytes(code[at+len(sig):at+len(sig)+3], "little")
    require(code[init:init+17] == bytes.fromhex("300290000085f5900100519000008003f5") and
            code[init+17] == 0x95, "Missing gift definition table")
    definitions = int.from_bytes(code[init+18:init+20], "little")

    def word(base, index):
        return struct.unpack_from("<I", data, base + 4*index)[0]

    # The initial vacancy of every physical slot is defined in each actor's
    # script. Read its own first-free search owner, rather than one actor ID.
    require(code[at-24:at-21] == bytes.fromhex("300d21"), "Missing first-gift allocator")
    vacancy = int.from_bytes(code[at-21:at-18], "little")
    require(code[vacancy:vacancy+11] == bytes.fromhex("30034e0000010091020095"),
            "Unexpected inventory vacancy lookup")
    inventory = int.from_bytes(code[vacancy+11:vacancy+13], "little")
    require(word(h.global_start, inventory) == 0x900000aa, "Expected 170 inventory rows")
    addresses = set()
    for slot in range(170):
        ref = word(h.global_start, inventory+1+slot)
        require(ref >> 16 == 0xb004, "Invalid cold inventory reference")
        index = ref & 0xffff
        addresses.add(index)
        begin = h.global_start + 4*index
        require(word(h.global_start,index) == 0x9000000a and data[begin+4:begin+44] == bytes(40),
                "Expected a vacant ten-cell inventory row")
    require(len(addresses) == 170, "Cold inventory rows unexpectedly share storage")
    require(word(h.constant_start, choices) == 0x90000005 and
            word(h.global_start, definitions) == 0x900000d0, "Invalid gift tables")
    for index in range(5):
        raw = word(h.constant_start, choices+1+index)
        require(0x20000000 <= raw < 0x20000000+208, "Invalid item ID")
        item = raw & 0xffff
        ref = word(h.global_start, definitions+item+1)
        require(ref >> 16 == 0xb004 and word(h.global_start, ref & 0xffff) == 0x9000000a,
                "Unexpected gift definition")
        yield data, h, item, ref & 0xffff


def room_inventory_media(root):
    out = bytearray(b"RIG2")
    for actor in ("NEKO", "ROBO", "USAGI", "INU", "KAERU"):
        for data, h, item, ref in first_gift_records(root, actor):
            property_word, = struct.unpack_from("<I", data, h.global_start + 4*(ref+1+9))
            require(property_word >> 28 == 2, "Expected integer first-gift property")
            out.extend(struct.pack("<HI", item, property_word))
            for field in (0, 1):
                string = struct.unpack_from("<I", data, h.global_start + 4*(ref+1+field))[0]
                require(string >> 28 == 5, "Expected a gift string")
                start = h.pool_start + (string & 0x0fffffff)
                end = data.index(0, start, h.pool_end)
                text = data[start:end]
                text.decode("cp932")
                out.extend(struct.pack("<H", len(text)))
                out.extend(text)
    blob = Region(bytes(out))
    return {Path("assets/game/post-profile/gift-templates.bin"): (blob, blob.sha256())}
