"""Selected cold Toro introduction bytecode from the shipped KSC.

Keep script PCs in the bundle. The native conversation owner reads branches,
operands and inline text at runtime, including the separate gift dialogue.
"""
from pathlib import Path
import struct

from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_code, parse_header
from package_room_common_topic import collect_named_contacts, named_contact_labels


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
    sets=line_break_sets(boot)
    out = bytearray(struct.pack("<4s5H", b"RTK3", len(spans), len(suffix),len(farewell),
                                *(len(s) for s in sets))) + suffix + farewell + b"".join(sets)
    for start, end in spans:
        require(code[start] == 0x30 and code[end-1] == 0x31,"Invalid NEKO routine bounds")
        out += struct.pack("<II",start,end-start) + code[start:end]
    return bytes(out)



def line_break_sets(boot):
    # BOOT 17614 queries two CP932 lists through 175B0. Type 1 attaches
    # to the preceding glyph. Type 2 opens a pair with the following glyph.
    sets=[]
    for field,length in ((0x4368,104),(0x4374,24)):
        text_va,=struct.unpack("<I",boot.read(0xe3c00+field,4))
        s=boot.read(0x80+text_va,length+1)
        require(s[-1:]==b"\0" and b"\0" not in s[:-1], "BOOT line-break dictionary")
        sets.append(s[:-1])
    return sets


# ROBO's event table row, first contact, present prompt/result and common
# farewell. Each projected routine keeps its original local control flow.
ROBO_INTRO_BLOCKS = (0x204d, 0x2152, 0x123d, 0x128c, 0x6746d, 0x5abfd,
                     0x67192, 0x67442, 0x6720a, 0x5be76)
ROBO_CONTACT_BLOCKS = (0x21d3, 0x21ed, 0x2217, 0x2228, 0x2247, 0x2255, 0x25b3, 0x2661, 0x29b8,
                       0x2be16, 0x35488, 0x35559, 0x4880d, 0x5105f,
                       0x5bdb8, 0x5bdc3, 0x5c29c, 0x5c7dc, 0x5cc52, 0x6b2a7)
ROBO_LESSON_BLOCKS = (0x346b4, 0x8269, 0x94b9, 0x9344, 0x915e, 0x90f9, 0x924a, 0x91a4,
                      0x8295, 0x8321, 0x8427, 0x856c, 0x8622, 0x86a3, 0x3e90, 0x3dc2,
                      0x8fcf, 0x8f6d, 0x420b, 0x3f25, 0x41bd, 0x8b44, 0x8aa2, 0x4dd7,
                      0x8843, 0x8034, 0x7f73, 0x7b7d, 0x6537, 0x6429, 0x64cb, 0x63c5,
                      0x7cd6, 0x71a9, 0x7241, 0x817f, 0x7771, 0x7619, 0x751b,
                      0x3fac, 0xbfe, 0x9ef, 0x9453, 0x94fa, 0x4001, 0x2990, 0x87c6,
                      0x8e05, 0x3e03, 0x7845, 0x3ca1, 0x3d6, 0x8ccb, 0x8d12, 0x8dc6,
                      0x8c48, 0x481d, 0x4865, 0x48a8, 0x48df, 0x4907, 0x41e4,
                      0xb80c, 0xb7f5, 0xb79d, 0xb5fd, 0xb340, 0xb361,
                      0x8153, 0x7282, 0x74b6, 0x7385, 0x8bfb, 0x4585, 0x46b3)
ROBO_PENULTIMATE_BLOCKS = (
    0x46, 0x4e, 0x1a45, 0x1ed9, 0x206e, 0x217e, 0x291a, 0x2963,
    0x9d98, 0x9df4, 0x9e91, 0x9f36, 0x9fd8, 0x13ed7,
    0x5b05e, 0x67102, 0x67132, 0x67159, 0x67184, 0x67324, 0x6734b, 0x6761b,
    0x6769e, 0x6779a, 0x6b475, 0x6b491,
)
ROBO_FINAL_DAY_BLOCKS = (
    0x1f06, 0x2162, 0x2193, 0x5b42d, 0x5b6bc, 0x674e2, 0x67501, 0x6b69b, 0x6b729, 0x6b88b,
)
ROBO_EXTENSION_BLOCKS = (0x2a6e, 0x5b81e, 0x5bad9, 0x675a2, 0x6765c)
ROBO_PHRASES = (0x24da, 0x24e6, 0x24fa, 0x2506, 0x2528)
NAMED_INTROS = {
    "USAGI": (0x96a0, 0x63396, (
        0x123d, 0x128c, 0x2062, 0x2167, 0x22ec, 0x967f, 0x9e1e,
        0x5f952, 0x6211e, 0x630b1, 0x63130, 0x6336b, 0x63396,
    )),
    "INU": (0x972d, 0x675a6, (
        0x123d, 0x128c, 0x2062, 0x2167, 0x22ec, 0x983d, 0x988d, 0x9e8c, 0x9ed9, 0xa5f6, 0xa702, 0xa7ce,
        0x63749, 0x65588, 0x672cc, 0x67344, 0x6757b, 0x675a6,
    )),
    "KAERU": (0x94f1, 0x6739f, (
        0x123d, 0x128c, 0x2062, 0x2167, 0x9594, 0x9698, 0x96dc, 0x974a, 0x97bd, 0xa478, 0xa4cd, 0xa4e1,
        0x6278b, 0x660ff, 0x670d1, 0x6714b, 0x67374, 0x6739f,
    )),
}
NAMED_PENULTIMATE = {
    # Callback, actor motion helper, scene with the second record, bodies.
    "USAGI": (0x63544, 0x13a76, 0x63274, (
        0x5fc53, 0x62fcb, 0x63028, 0x6307c, 0x630a3, 0x6324d,
        0x635c7, 0x636c3, 0x67358, 0x67374,
    )),
    "INU": (0x67754, 0x14501, 0x67484, (
        0x63b00, 0x67235, 0x6725c, 0x6728f, 0x672be, 0x6745d,
        0x677d7, 0x678d3, 0x6b52d, 0x6b549,
    )),
    "KAERU": (0x6754d, 0x13ff2, 0x6727d, (
        0xa773, 0x62ad1, 0x66ffd, 0x6703f, 0x67093, 0x670bf, 0x67256,
        0x675d0, 0x676cc, 0x6b340, 0x6b35c,
    )),
}
NAMED_CALENDAR_BODIES = {
    # Events 3–6, GAME75 wrapper, diary table, and their scene-specific procedures.
    "USAGI": ((0x6342a, 0x63585, 0x634cb, 0x6340b), 0x6776e, 0x4090,
              (0x225c, 0x2278, 0x605d6, 0x60867, 0x61092, 0x61753, 0x6757e, 0x6760c)),
    "INU": ((0x6763a, 0x67795, 0x676db, 0x6761b), 0x6b943, 0x4090,
            (0x98b6, 0x98ee, 0x996b, 0x9fcd, 0x63fb4, 0x645be, 0x648d3, 0x64c52, 0x6b753, 0x6b7e1)),
    "KAERU": ((0x67433, 0x6758e, 0x674d4, 0x67414), 0x6b756, 0x4093,
              (0x62f85, 0x6325e, 0x63527, 0x639e5, 0x6b566, 0x6b5f4)),
}

def named_actor_entries(species):
    entries = NAMED_INTROS[species][2]
    callback, motion, scene, bodies = NAMED_PENULTIMATE[species]
    calendar, farewell, _, extra = NAMED_CALENDAR_BODIES[species]
    shared = (0x46, 0x4e, 0x1a5a, 0x1eee, 0x2083, 0x2177, 0x2193, 0x21a8, 0x21e8,
              0x2202, 0x222c, 0x223d, 0x226a, 0x292f, 0x2978)
    return tuple(sorted({*entries, *shared, 0x1f1b, callback, motion, scene, *bodies, *calendar, farewell, *extra}))


def robo_room_blocks(bits, instructions):
    return named_room_blocks(bits, instructions,
        (*ROBO_INTRO_BLOCKS, *ROBO_CONTACT_BLOCKS, *ROBO_LESSON_BLOCKS,
         *ROBO_PENULTIMATE_BLOCKS, *ROBO_FINAL_DAY_BLOCKS, *ROBO_EXTENSION_BLOCKS))


def named_room_blocks(bits, instructions, entries):
    by_pc = {row["code_offset"]: i for i, row in enumerate(instructions)}
    blocks = {}
    for entry in entries:
        i = by_pc[entry]
        require(instructions[i]["opcode"] == 0x30, "Invalid named room entry")
        j = i+1
        while j < len(instructions) and instructions[j]["opcode"] != 0x30:
            j += 1
        ending = instructions[j-1]
        require(ending["opcode"] == 0x31 or
                (entry == 0xb5fd and bits[ending["offset"]:ending["end"]] == bytes.fromhex("1057b600")) or
                (entry in (0x6b88b, 0x6776e, 0x6b943, 0x6b756) and ending["opcode"] == 0x1b),
                "Unterminated named room routine")
        blocks[entry] = bits[instructions[i]["offset"]:instructions[j-1]["end"]]
    return blocks


def contact_rows(instructions,blocks):
    ranges = iter(sorted(blocks.items()))
    start, raw = next(ranges)
    selected=[]
    for op in instructions:
        pc=op["code_offset"]
        while pc>=start+len(raw):
            next_block=next(ranges,None)
            if next_block is None:
                return selected
            start,raw=next_block
        if start<=pc:
            selected.append(op)
    return selected


def robo_event_row(bits, header, event):
    return actor_event_row(bits, header, 0x940b, event)


def actor_event_row(bits, header, base, event):
    g = lambda i: struct.unpack_from("<I", bits, header.global_start + 4*i)[0]
    ref = g(base + 1 + event)
    require(g(base)==0x90000008 and ref >> 16 == 0xb004, "Unknown actor event table")
    row = ref & 65535
    require(g(row) == 0x90000002 and g(row+1) >> 28 == 2 and g(row+2) >> 28 == 7,
            "Invalid actor event row")
    return g(row+1) & 0xfffffff, g(row+2) & 0xfffffff


def write_contact_labels(bundle, labels, boot):
    # BOOT 75778 supplies this string when an inline operand is NIL.
    fallback = boot.read(0x80+0xe0760,5)
    require(fallback[-1:]==b"\0" and b"\0" not in fallback[:-1], "Invalid inline text fallback")
    bundle.extend(fallback[:-1])
    bundle.extend(struct.pack("<H",len(labels)))
    for index, refs in sorted(labels.items()):
        bundle.extend(struct.pack(f"<HH{len(refs)}I",index,len(refs),*refs))


def project_robo_intro(bits, boot):
    h = parse_header(bits)
    def word(base, i):
        return struct.unpack_from("<I", bits, base+4*i)[0]
    def text(offset):
        at = h.pool_start + offset
        return bits[at:bits.index(0, at, h.pool_end)]
    g = lambda i: word(h.global_start, i)
    c = lambda i: word(h.constant_start, i)
    require(robo_event_row(bits, h, 1) == (85, 0x6746d), "Unknown ROBO invitation motion or callback")
    scheduled = robo_event_row(bits, h, 2)
    require(scheduled == (79, 0x6761b), "Unknown ROBO penultimate-day motion or callback")
    departure = robo_event_row(bits, h, 3)
    require(departure == (48, 0x67501), "Unknown ROBO final-day motion or callback")
    reunion = robo_event_row(bits, h, 6)
    require(reunion == (85, 0x674e2), "Unknown ROBO reunion motion or callback")
    extended_eve = robo_event_row(bits, h, 4)
    extended_final = robo_event_row(bits, h, 5)
    require(extended_eve == (79, 0x6765c) and extended_final == (48, 0x675a2),
            "Unknown ROBO extended stay callbacks")
    require(g(0x408f) == 0x90000088, "Unknown ROBO diary table")
    diary_entries = ((7, 0x20900000, 0x305), (8, 0x20700000, 0x381),
                     (10, 0x20800000, 0x465), (11, 0x20800000, 0x4f1), (9, 0x20a00000, 0x405))
    for index, priority, offset in diary_entries:
        diary = g(0x4090 + index)
        row = diary & 0xffff
        require(diary >> 16 == 0xb004 and g(row) == 0x90000006 and g(row+1) == priority and
                g(row+6) == (0x50000000 | offset), "Unknown ROBO scheduled diary record")
    require(c(0x2a98) == 0x90000003, "Unknown ROBO procedure selector")
    reactions = [c(i) for i in range(0x2a99, 0x2a9c)]
    require(all(word >> 28 == 7 and word & 0xfffffff in ROBO_PENULTIMATE_BLOCKS for word in reactions),
            "Unknown ROBO reaction procedures")
    instructions = parse_code(bits)
    contacts, _, contact_phrases = collect_named_contacts(bits,"ROBO",instructions)
    blocks = robo_room_blocks(bits, instructions)
    blocks.update(named_room_blocks(bits,instructions,contacts))
    offsets = {ref["target"] for op in contact_rows(instructions,blocks)
               for ref in op["references"] if ref["kind"]=="pool"}
    offsets.update(offset for _, _, offset in diary_entries)
    labels = named_contact_labels(bits,h,"ROBO")
    offsets.update(ref for refs in labels.values() for ref in refs)
    phrases = {}
    phrases.update(contact_phrases)
    for refs in contact_phrases.values():
        offsets.update(refs)
    for key in ROBO_PHRASES:
        header = c(key)
        require(header >> 16 == 0x9000 and 1 <= (header&65535) <= 16, "Invalid ROBO phrase array")
        refs = [c(key+i+1) for i in range(header&65535)]
        require(all(r >> 28 == 5 for r in refs), "Expected ROBO phrase strings")
        phrases[key] = [r&0xfffffff for r in refs]
        offsets.update(phrases[key])
    require(g(0x17)>>28==5, "Expected ROBO speech suffix")
    suffix = text(g(0x17)&0xfffffff)
    sets = line_break_sets(boot)
    out = bytearray(struct.pack("<4s6H", b"RRI6", len(blocks),len(offsets),len(phrases),
                                len(suffix),*(len(v) for v in sets))) + suffix + b"".join(sets)
    for pc, raw in sorted(blocks.items()):
        out.extend(struct.pack("<II", pc,len(raw))+raw)
    for offset in sorted(offsets):
        string = text(offset)
        out.extend(struct.pack("<II",offset,len(string))+string)
    for key, refs in sorted(phrases.items()):
        out.extend(struct.pack("<HH",key,len(refs)))
        out.extend(struct.pack(f"<{len(refs)}I",*refs))
    out.extend(struct.pack("<13I", *scheduled, *departure, *reunion, *extended_eve, *extended_final,
                           *(word & 0xfffffff for word in reactions)))
    write_contact_labels(out,labels,boot)
    return bytes(out)

def project_named_intro(bits, boot, species):
    h = parse_header(bits)
    table, entry, _ = NAMED_INTROS[species]
    require(actor_event_row(bits, h, table, 1) == (85, entry),
            "Unknown first-day motion or callback for " + species)
    instructions = parse_code(bits)
    contacts, _, contact_phrases = collect_named_contacts(bits,species,instructions)
    blocks = named_room_blocks(bits, instructions, set(named_actor_entries(species)) | contacts)
    selected = contact_rows(instructions,blocks)
    selected_pc = {row["code_offset"] for row in selected}
    offsets = {ref["target"] for row in selected
               for ref in row["references"] if ref["kind"] == "pool"}
    phrases = dict(contact_phrases)
    for refs in contact_phrases.values():
        offsets.update(refs)
    for previous, current in zip(instructions, instructions[1:]):
        if current["opcode"] != 0xf6 or bits[current["offset"]+1] != 0 or current["code_offset"] not in selected_pc:
            continue
        if current["code_offset"]==0x292b:  # 2926 loads its incoming phrase array.
            require(previous["opcode"]==0x90,"Changed dynamic phrase operand")
            continue
        require(previous["opcode"] == 0x99, "Missing named actor phrase selector operand")
        key, = struct.unpack_from("<H", bits, previous["offset"]+1)
        header, = struct.unpack_from("<I", bits, h.constant_start+4*key)
        count = header & 0xffff
        require(header>>16 == 0x9000 and 1 <= count <= 16, "Invalid named actor phrase selector")
        values = struct.unpack_from(f"<{count}I", bits, h.constant_start+4*(key+1))
        require(all(word>>28 == 5 for word in values), "Invalid named actor phrase")
        phrases[key] = [value & 0xfffffff for value in values]
        offsets.update(phrases[key])
    def suffix_ref(index):
        raw, = struct.unpack_from("<I", bits, h.global_start+4*index)
        require(raw >> 28 == 5, "Missing actor speech suffix")
        return raw & 0xfffffff
    extra_suffixes = [(i, suffix_ref(i)) for i in range(0x18, 0x1c if species=="KAERU" else 0x19)]
    offsets.update(offset for _, offset in extra_suffixes)
    calendar, _, diary_table, _ = NAMED_CALENDAR_BODIES[species]
    def global_word(index):
        return struct.unpack_from("<I", bits, h.global_start+4*index)[0]
    require(global_word(diary_table) >> 16 == 0x9000, "Invalid named actor diary")
    diary_offsets = []
    for descriptor, priority in ((7,0x900000), (8,0x700000), (10,0x800000), (11,0x800000), (9,0xa00000)):
        ref = global_word(diary_table+1+descriptor)
        row = ref & 0xffff
        value = global_word(row+6)
        require(ref >> 16 == 0xb004 and global_word(row)==0x90000006 and
                global_word(row+1)==(0x20000000|priority) and value >> 28 == 5, "Invalid named actor diary row")
        diary_offsets.append(value & 0xfffffff)
    offsets.update(diary_offsets)
    labels = named_contact_labels(bits,h,species)
    offsets.update(ref for refs in labels.values() for ref in refs)
    def text(offset):
        at = h.pool_start+offset
        return bits[at:bits.index(0, at, h.pool_end)]
    suffix = text(suffix_ref(0x17))
    breaks = line_break_sets(boot)
    result = bytearray(struct.pack("<4s6H", b"RAI5", len(blocks), len(offsets), len(phrases),
                                   len(suffix), *(len(values) for values in breaks)))
    result.extend(suffix + b"".join(breaks))
    for pc, raw in sorted(blocks.items()):
        result.extend(struct.pack("<II", pc, len(raw)) + raw)
    for offset in sorted(offsets):
        raw = text(offset)
        result.extend(struct.pack("<II", offset, len(raw)) + raw)
    for key, refs in sorted(phrases.items()):
        result.extend(struct.pack(f"<HH{len(refs)}I", key,len(refs),*refs))
    result.extend(struct.pack("<H", len(extra_suffixes)))
    for index, offset in extra_suffixes:
        result.extend(struct.pack("<HI", index, offset))
    for event, (motion, entry) in enumerate(zip((79,48,79,48,85), (NAMED_PENULTIMATE[species][0], *calendar)),2):
        row = actor_event_row(bits, h, table, event)
        require(row==(motion,entry), "Unknown calendar motion or callback for " + species)
        result.extend(struct.pack("<II", *row))
    result.extend(struct.pack("<5I", *diary_offsets))
    write_contact_labels(result,labels,boot)
    return bytes(result)


def project_local_table(bits):
    header = parse_header(bits)
    words = struct.unpack_from(f"<{header.local_count}I", bits, header.pool_end)
    result = bytearray(struct.pack("<H",len(words)))
    for index, word in enumerate(words):
        tag, value = word >> 28, word & 0x0fffffff
        if word == 0:
            result.extend(struct.pack("<BH",0,0))
        elif tag == 9:
            require(value <= 0xfff and index + value < len(words),"Invalid room KSC local array")
            result.extend(struct.pack("<BH",9,value))
        else:
            require(tag == 5 and value < header.pool_size,"Unknown room KSC local cell")
            start = header.pool_start + value
            text = bits[start:bits.index(b"\0",start,header.pool_end)]
            result.extend(struct.pack("<BH",5,len(text)) + text)
    return result


LETTER_LAYOUTS = (
    # Event table, diary table, callback, response, and diary selector.
    (0x98a2, 0x408f, 0x895a2, 0x89325, 0xa4d2),
    (0x940b, 0x408f, 0x674b9, 0x6723f, 0xafb0),
    (0x96a0, 0x4090, 0x633e2, 0x63162, 0xa40e),
    (0x972d, 0x4090, 0x675f2, 0x6737a, 0xb109),
    (0x94f1, 0x4093, 0x673eb, 0x67175, 0xadda),
)


def project_return_letter(bits, actor):
    header = parse_header(bits)
    table, diary, entry, response, selector = LETTER_LAYOUTS[actor]
    motion, callback = actor_event_row(bits, header, table, 7)
    require(motion == 79 and callback == entry and bits[28+entry+16:28+entry+21] ==
            bytes((0x20, 1)) + selector.to_bytes(3, "little"),
            "Unexpected scheduled letter callback")
    instructions = parse_code(bits)
    entries = (entry, response, 0x9492) if actor == 4 else (entry, response)
    blocks = named_room_blocks(bits, instructions, entries)
    strings = sorted({ref["target"] for row in contact_rows(instructions, blocks)
                      for ref in row["references"] if ref["kind"] == "pool"})

    def text(offset):
        start = header.pool_start + offset
        return bits[start:bits.index(0, start, header.pool_end)]

    g = lambda i: struct.unpack_from("<I", bits, header.global_start+4*i)[0]
    require(g(diary) >> 16 == 0x9000, "Invalid letter descriptor table")
    descriptors = {}
    for index in range(g(diary) & 0xffff):
        ref = g(diary+1+index)
        require(ref >> 16 == 0xb004, "Invalid diary descriptor reference")
        row = ref & 0xffff
        flags = g(row+1)
        if flags & 0xf00000 != 0x500000:
            continue
        sender = flags & 15
        require((flags & ~15) == (0x20500000 | actor << 4) and sender < 5
                and sender not in descriptors and g(row) == 0x90000006
                and g(row+6) >> 28 == 5, "Invalid sender diary descriptor")
        descriptors[sender] = (index, text(g(row+6) & 0xfffffff))
    require(len(descriptors) == 5, "Missing letter sender descriptor")

    out = bytearray(struct.pack("<6I", table, motion, entry, selector, len(blocks), len(strings)))
    for pc, body in sorted(blocks.items()):
        out.extend(struct.pack("<2I", pc, len(body)) + body)
    for offset in strings:
        raw = text(offset)
        out.extend(struct.pack("<2I", offset, len(raw)) + raw)
    for sender in range(5):
        index, raw = descriptors[sender]
        out.extend(struct.pack("<2I", index, len(raw)) + raw)
    return out


def room_talk_media(root,boot):
    files = {}
    locals_blob = bytearray(b"RLS1")
    letters = bytearray(b"RLT1")
    for index, species in enumerate(("NEKO","ROBO","USAGI","INU","KAERU")):
        archive = BparArchive(root.payload(root.by_name("KS"+species+".BPM")))
        actor = archive.payload(archive.by_name(species+".KSC"))
        bits = actor.read(0,actor.size)
        if species == "NEKO":
            name, projected = "toro", project_toro_intro(bits,boot)
        elif species == "ROBO":
            name, projected = "robo", project_robo_intro(bits,boot)
        else:
            name, projected = species.lower(), project_named_intro(bits,boot,species)
        image = Region(projected)
        files[Path(f"assets/game/post-profile/{name}-introduction.bin")] = image, image.sha256()
        locals_blob.extend(project_local_table(bits))
        letter = project_return_letter(bits, index)
        letters.extend(struct.pack("<I", len(letter)) + letter)
    locals_image = Region(bytes(locals_blob))
    files[Path("assets/game/post-profile/room-ksc-locals.bin")] = locals_image, locals_image.sha256()
    letters_image = Region(bytes(letters))
    files[Path("assets/game/post-profile/room-return-letters.bin")] = letters_image, letters_image.sha256()
    return files
