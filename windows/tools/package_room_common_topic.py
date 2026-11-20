"""Toro's ordinary fallback list and directly executable, single-conversation topics."""
from bisect import bisect_left
from pathlib import Path
import struct

from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_code, parse_header
from package_room_word_reuse import faces_word_lookup, ordinary_question_filters, project_word_reuse


def table(data, base, index):
    at = base + 4 * index
    header, = struct.unpack_from("<I", data, at)
    require(header >> 28 == 9, "Invalid KSC array header")
    count = header & 0x3fff
    return header, struct.unpack_from(f"<{count}I", data, at + 4)


def phrase_entries(data, h, index):
    """Plain nonempty CT array of script-pool text, in source order."""
    at = h.constant_start + 4 * index
    if index >= h.constant_count:
        return ()
    header, = struct.unpack_from("<I", data, at)
    count = header & 0xfff
    if not count or header != 0x90000000 | count or index + count >= h.constant_count:
        return ()
    entries = struct.unpack_from(f"<{count}I", data, at + 4)
    for entry in entries:
        start = h.pool_start + (entry & 0x0fffffff)
        if entry >> 28 != 5 or start >= h.pool_end or data.find(b"\0", start, h.pool_end) < 0:
            return ()
    return entries


def contact_predicate_terms(terms):
    return bool(terms) and len(terms) <= 96 and len(terms) % 3 == 0 and all(
        field >> 28 == 2 and field & 0xfffffff < 30 and op >> 28 == 2 and op & 0xfffffff <= 8
        and (arg >> 28 == 2 or (arg == 0 and op & 0xfffffff <= 1))
        for field, op, arg in zip(terms[0::3], terms[1::3], terms[2::3])
    )


def contact_word_filter(data, h, ct, flat=False):
    if ct>=h.constant_count or struct.unpack_from("<I",data,h.constant_start+4*ct)[0]>>28!=9:
        return False
    _, groups = table(data,h.constant_start,ct)
    if flat:
        return contact_predicate_terms(groups)
    return bool(groups) and len(groups) <= 16 and all(
        ref >> 16 == 0xb003 and contact_predicate_terms(table(data,h.constant_start,ref&0xffff)[1])
        for ref in groups
    )


NAMED_CONTACTS = {
    "ROBO": (0xbab1,0x5bdb8,0x2661,0x29b8,0x26a6,0x291a,0x2152,0x2926,0x6b2a7),
    "USAGI": (0x989d,0x6206e,0x2676,0x29cd,0x26bb,0x292f,0x2167,0x2586,0x67188),
    "INU": (0x92b1,0x654e7,0x2676,0x29cd,0x26bb,0x292f,0x2167,0x246c,0x6b35f),
    "KAERU": (0x9723,0x66058,0x2676,0x29cd,0x26bb,0x292f,0x2167,0x2502,0x6b170),
}


# Calls for the prompted-word family. These bodies keep their original
# character-specific addresses and use the current actor's vocabulary.
NAMED_PROMPTS = {
    "ROBO": (0x249f,(0x8269,0x9453,0x94b9,0x9344,0x8fcf,0x90f9,0x915e,0x91a4,0x924a,
                     0x3dc2,0x3e90,0x4001,0x2990,0x8295,0x8321,0x8427,0x856c,0x2911),(0x497c,0x492b,0x24a0)),
    "USAGI": (0x2137,(0x8269,0x9450,0x94b6,0x9341,0x8fcc,0x90f6,0x915b,0x91a1,0x9247,
                      0x3dfd,0x3ed4,0x403d,0x29a5,0x8295,0x8321,0x8427,0x856c,0x8622,0x86a3,0x87c6,
                      0x8843,0x88e0,0x8992,0x8a01,0x8aa2,0x8b44,0x8f6a,0x2926,0x96e5),(0x49c7,0x24b5)),
    "INU": (0x2019,(0x8208,0x93f2,0x9458,0x92e3,0x8f6e,0x9098,0x90fd,0x9143,0x91e9,
                    0x3df1,0x3ed2,0x4047,0x29a5,0x8234,0x82c0,0x83c6,0x850b,0x85c1,0x8642,0x8765,
                    0x87e2,0x887f,0x8931,0x89a0,0x8a41,0x8ae3,0x8f0c,0x2926,0x96c8,0x983d),(0x48d8,0x24b5)),
    "KAERU": (0x20bb,(0x8191,0x937b,0x93e1,0x926c,0x8ef7,0x9021,0x9086,0x90cc,0x9172,
                      0x3cc7,0x3d9b,0x3f2c,0x29a5,0x81bd,0x8249,0x834f,0x8494,0x854a,0x85cb,0x86ee,
                      0x876b,0x8808,0x88ba,0x8929,0x89ca,0x8a6c,0x8e95,0x2926,0x4a3b8),(0x489a,0x24b5)),
}


def named_contact_labels(data, header, species):
    source_tables = {
        "ROBO": {0x609a: 4, 0x609f: 7},
        "USAGI": {0x609b: 4, 0x60a0: 7},
        "INU": {0x60ab: 4},
        "KAERU": {0x6096: 4},
    }
    labels = {}
    for index, count in source_tables[species].items():
        raw, cells = table(data, header.global_start, index)
        require(raw == (0x90000000 | count) and all(word >> 28 == 5 for word in cells),
                "Invalid calendar label array")
        labels[index] = [word & 0xfffffff for word in cells]
    return labels


def collect_named_contacts(data, species, rows):
    h = parse_header(data)
    code = data[28:h.pool_start]
    roster, entry, dispatcher, greeting, limit, random_range, mood, mood_tick, attention = NAMED_CONTACTS[species]
    pcs = [r["code_offset"] for r in rows]
    starts = [r["code_offset"] for r in rows if r["opcode"]==0x30] + [len(code)]
    entries = set(starts[:-1])
    _, topics = table(data,h.constant_start,roster)
    ops = {0x10,0x11,0x12,0x13,-1,0x20,0x21,0x25,0x30,0x31,0x44,0x48,0x4e,0x50,0x51,0x53,
           0x55,0x65,0x67,0x70,0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8e,
           0x90,0x91,0x94,0x95,0x99,0xb2,0xb3,0xb4,0xb8,0xb9,0xbb,0xbc,0xbd,0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc8,0xc9,
           0xd0,0xd1,0xd2,0xd4,0xe0,0xe1,0xe2,0xe3,0xf0,0xf4,0xf5,0xf6}
    flags,dictionary={"ROBO":(0x59aa,0x3dd2),"USAGI":(0x59ab,0x3dd3),
                      "INU":(0x59bb,0x3dd3),"KAERU":(0x59a6,0x3dd6)}[species]
    live_arrays = {dictionary,*(index+flags-0x59a2 for index in (0x59a8,0x59af,0x59b6))}
    live_arrays.update(named_contact_labels(data,h,species))
    for index,count in zip((0x59a8,0x59af,0x59b6),(5,6,6)):
        require(table(data,h.global_start,index+flags-0x59a2)[1]==(0,)*count,
                "Invalid actor contact history array")
    scratch, prompt_helpers, native_prompt_helpers = NAMED_PROMPTS[species]
    helpers = {0x128c,0x123d,greeting,limit,random_range,mood,*prompt_helpers}
    native = {0x11b0,0x1011,0x0fdd,*native_prompt_helpers}
    if species=="ROBO":
        # This shared trait procedure is already part of the verified ROBO bundle.
        native.add(0xb5fd)
    if species=="KAERU":
        native.add(0xf30)  # Ordered lookup without the temporary second condition.
    require(table(data,h.constant_start,scratch)[1]==(0,)*12,"Invalid prompted-word array")
    def instructions(pc):
        i=bisect_left(starts,pc)
        require(starts[i]==pc,"Missing named actor procedure")
        return rows[bisect_left(pcs,pc):bisect_left(pcs,starts[i+1])]
    admitted = {entry,dispatcher,attention,*helpers}
    filters, phrases = set(), {}
    analyses = {}
    analyzing = set()
    supported_calls = helpers | native

    def analyze(pc):
        if pc in supported_calls:
            return set(), set(), {}
        if pc in analyses:
            return analyses[pc]
        if pc in analyzing or pc not in entries:
            return None
        analyzing.add(pc)
        result = analyze_body(pc)
        analyzing.remove(pc)
        analyses[pc] = result
        return result

    def analyze_body(pc):
        commands=instructions(pc)
        local_filters, local_phrases = set(), {}
        bodies={pc}
        for i,row in enumerate(commands):
            op, at = row["opcode"],row["code_offset"]
            if op not in ops:
                return None
            if op in (0x20,0x21):
                dependency=analyze(row["references"][0]["target"])
                if dependency is None:
                    return None
                children, word_filters, phrase_lists = dependency
                bodies.update(children)
                local_filters.update(word_filters)
                local_phrases.update(phrase_lists)
            if op==0x25 and (species!="ROBO" or code[at+1:at+4]!=bytes.fromhex("982a00")):
                return None
            if op==0x8b:
                service,argc=code[at+1:at+3]
                if species=="ROBO" and pc==0x35023 and at==0x350f3:
                    require((service,argc)==(0xf0,2),"Unknown room period query")
                    continue
                if (service,argc) not in ((0x22,2),(0x23,0),(0x11,1),(0x53,1),(0x52,3),(0x63,2),
                                         (4,1),(9,0),(10,1),(11,1),(12,1)) and not (
                    service==0x21 and 1<=argc<=10
                ):
                    return None
            if op==0x94 and struct.unpack_from("<H",code,at+1)[0] not in (0x15,0x16,0x17,flags+1):
                return None
            if op==0x95:
                slot,=struct.unpack_from("<H",code,at+1)
                if species=="ROBO" and (slot,pc) in ((9,0x37865),(0x17,0x56ae5),
                                                      (0x17,0x56a31),(0x17,0x569fe)):
                    continue
                if slot not in live_arrays:
                    return None
            if op==0xf6:
                if species=="ROBO" and pc==0x2900 and at==0x2908:
                    require(code[at-3:at+2]==bytes.fromhex("900000f600"),"Unknown ROBO phrase helper")
                elif (code[at+1]!=0 or i==0 or commands[i-1]["opcode"]!=0x99
                      or not phrase_entries(data,h,struct.unpack_from("<H",code,at-2)[0])):
                    return None
            if op!=0x99:
                continue
            ct,=struct.unpack_from("<H",code,at+1)
            if ct in (scratch,mood_tick+1):
                continue
            values=phrase_entries(data,h,ct)
            if values and len(values)<=16:
                local_phrases[ct]=[value&0xfffffff for value in values]
            elif contact_word_filter(data,h,ct) or (
                    i+1<len(commands) and commands[i+1]["opcode"]==0x8b
                    and code[at+4:at+6]==bytes.fromhex("6302") and contact_word_filter(data,h,ct,flat=True)):
                local_filters.add(ct)
            else:
                return None
        if commands[-1]["opcode"]!=0x31:
            return None
        return bodies, local_filters, local_phrases

    def merge(projection):
        if projection is not None:
            bodies, word_filters, phrase_lists = projection
            admitted.update(bodies)
            filters.update(word_filters)
            phrases.update(phrase_lists)

    for ref in topics:
        require(ref>>28==7,"Invalid named contact entry")
        merge(analyze(ref&0xfffffff))

    for helper in prompt_helpers:
        for row in instructions(helper):
            if row["opcode"] in (0x20,0x21):
                merge(analyze(row["references"][0]["target"]))
    # The listed mood routine writes the same native clock owner used by
    # the actor's contact controller. The addresses differ by script.
    require(code[mood:mood+12]==b"\x30\x01\x80\x15\x90\0\0\x8b\xf0\x02\x99"+bytes((mood_tick&255,)),
            "Named contact mood binding changed")
    return admitted, filters, phrases


def project_list(data, actor="NEKO"):
    h = parse_header(data)
    ct, g, count, partner_ct, partner_g, partner_count = {
        "NEKO": (0xd090, 0x6018, 1057, 0xe1f3, 0x607d, 48),
        "ROBO": (0xbab1, 0x6020, 512, 0xcae0, 0x6085, 40),
        "USAGI": (0x989d, 0x6021, 490, 0x9d5d, 0x6086, 33),
        "INU": (0x92b1, 0x6031, 512, 0x9608, 0x6096, 31),
        "KAERU": (0x9723, 0x601c, 547, 0x9a2b, 0x6081, 30),
    }[actor]
    if actor == "ROBO":
        code = data[28:h.pool_start]
        require(code[0x5bdb8:0x5bdc3] == bytes.fromhex("300099b1ba200161260031") and
                code[0x2672:0x2681] == bytes.fromhex("992c29900000f603206051982c2923") and
                code[0x679c6:0x679cc] == bytes.fromhex("25e0ca038560"),
                "ROBO fallback or partner selector changed")
    elif actor != "NEKO":
        ordinary, partner, command = {
            "USAGI": (0x6206e, 0x638ef, 0x86),
            "INU": (0x654e7, 0x67aff, 0x96),
            "KAERU": (0x66058, 0x678f8, 0x81),
        }[actor]
        code = data[28:h.pool_start]
        require(code[ordinary:ordinary+7] == b"\x30\x00\x99"+struct.pack("<H", ct)+b"\x20\x01" and
                code[partner:partner+6] == b"\x25"+struct.pack("<H", partner_ct)+bytes((3,command,0x60)),
                f"{actor} fallback or partner selector changed")
    header, procedures = table(data, h.constant_start, ct)
    require(header == 0x90000000 | count and all(v >> 28 == 7 for v in procedures), f"{actor} fallback changed")
    history, initial = table(data, h.global_start, g)
    require(history == 0x90000064 and not any(initial), f"{actor} cold F6 history changed")
    partner, targets = table(data, h.constant_start, partner_ct)
    second, recent = table(data, h.global_start, partner_g)
    require(partner == 0x90000000 | partner_count and all(v >> 28 == 7 for v in targets), "Invalid partner callback table")
    require(second == 0x90000014 and not any(recent), "Invalid partner callback history")
    words = (header, *procedures, history, *initial, partner, *targets, second, *recent)
    return b"RFL2" + struct.pack(f"<{len(words)}I", *words)


def room_fallback_list_media(root):
    files = {}
    for actor in ("NEKO", "ROBO", "USAGI", "INU", "KAERU"):
        name = "toro" if actor == "NEKO" else actor.lower()
        pack = BparArchive(root.payload(root.by_name(f"KS{actor}.BPM")))
        script = pack.payload(pack.by_name(f"{actor}.KSC"))
        resource = Region(project_list(script.read(0, script.size), actor))
        files[Path(f"assets/game/post-profile/{name}-fallback-list.bin")] = (resource, resource.sha256())
    return files


def outing_rows(data, h):
    _, refs = table(data, h.global_start, 0x59da)
    require(len(refs) == 133 and all(ref >> 16 == 0xb004 for ref in refs), "Invalid outing roster")
    for ref in refs:
        header, row = table(data, h.global_start, ref & 0xffff)
        require(header == 0x90000009, "Invalid outing descriptor")
        yield row


def outing_details(data, h):
    # 1246F indexes CT2DF7 by the selected scene kind minus one. Its seven
    # slots each hold a fallback phrase list followed by optional word rules.
    def values(ct):
        return table(data, h.constant_start, ct)[1]
    def reference(tag):
        require(tag >> 16 == 0xb003, "Invalid outing group reference")
        return tag & 0xffff
    roots = tuple(reference(tag) for tag in values(0x2df7))
    require(len(roots) == 19, "Unexpected destination detail roster")
    tables, filters = {}, set()
    for root in roots:
        tables[root] = values(root)
        for tag in tables[root]:
            if not tag:
                continue
            group = reference(tag)
            tables[group] = values(group)
            for index, entry in enumerate(tables[group]):
                item = reference(entry)
                tables[item] = values(item)
                if index:
                    require(len(tables[item]) == 2, "Invalid outing conditional text")
                    filters.add(reference(tables[item][1]))
    return roots, tables, filters


def room_outing_media(root):
    pack = BparArchive(root.payload(root.by_name("KSNEKO.BPM")))
    script = pack.payload(pack.by_name("NEKO.KSC"))
    data = script.read(0, script.size)
    h = parse_header(data)
    code = data[28:h.pool_start]
    # Cold G59AE, followed by the mandatory actor-start 2C57 -> 2BC9(bit).
    initial, = struct.unpack_from("<I", data, h.global_start + 4*0x59ae)
    require(initial == 0x20000000 and code[0x8e15d:0x8e161] == bytes.fromhex("21572c00")
            and code[0x2c57:0x2c61] == bytes.fromhex("3000800c2001c92b0031")
            and code[0x2bd6:0x2be1] == bytes.fromhex("95ae598001900000e36731"), "Outing enable path changed")
    mask = (initial & 0x0fffffff) | (1 << code[0x2c5a])
    _, periods = table(data, h.constant_start, 0x28f6)
    _, indices = table(data, h.global_start, 0x5f92)
    require(len(periods) == 20 and all(v >> 28 == 2 for v in periods), "Invalid outing time table")
    require(len(indices) == 132 and sorted(indices) == list(range(0x20000001,0x20000085)),
            "Invalid initial outing index")
    result = bytearray(b"ROT5" + struct.pack("<I20B132B", mask, *(v&255 for v in periods+indices)))
    for row in outing_rows(data, h):
        kind, primary, secondary = row[:3]
        if not kind:
            require(not any(row), "Unexpected empty outing descriptor")
        else:
            require(kind >> 28 == 2 and 0 < (kind & 0xffff) < len(periods)
                    and primary >> 16 == 0xb003 and (not secondary or secondary >> 16 == 0xb003)
                    and row[4] >> 28 == 5, "Invalid outing fields")
        require(row[6] in ((0x10000000, 0x10000001) if kind else (0,)), "Invalid outing offer flag")
        result.extend(struct.pack("<BHHB", kind & 0xff, primary & 0xffff, secondary & 0xffff, row[6] & 1))
        for ref in (*row[3:6], *row[7:9]):
            if not ref:
                result.extend(struct.pack("<H", 0xffff))
                continue
            require(ref >> 28 == 5, "Invalid outing text reference")
            at = h.pool_start + (ref & 0x0fffffff)
            text = data[at:data.index(0, at, h.pool_end)]
            result.extend(struct.pack("<H", len(text)) + text)
    details, tables, _ = outing_details(data, h)
    result.extend(struct.pack("<20H", *details, len(tables)))
    for ct, entries in sorted(tables.items()):
        result.extend(struct.pack("<HH", ct, len(entries)))
        for tag in entries:
            result.extend(struct.pack("<I", tag))
            if tag >> 28 == 5:
                begin = h.pool_start + (tag & 0x0fffffff)
                text = data[begin:data.index(0, begin, h.pool_end)]
                result.extend(struct.pack("<H", len(text)) + text)
            else:
                require(not tag or tag >> 16 == 0xb003, "Invalid destination display rule")
    resource = Region(bytes(result))
    return {Path("assets/game/post-profile/toro-outings.bin"): (resource, resource.sha256())}


def collect(data):
    h = parse_header(data)
    code = data[28:h.pool_start]
    rows = parse_code(data)
    pcs = [r["code_offset"] for r in rows]
    procedures = [p for p, row in zip(pcs, rows) if row["opcode"] == 0x30] + [len(code)]
    _, candidates = table(data, h.constant_start, 0xd090)
    selected, strings, filters, excludes = {}, {}, {0x28dc, 0x28e2}, set()
    filters.update(ordinary_question_filters(data))
    filters.update(outing_details(data,h)[2])
    filters.update(ref & 0xffff for row in outing_rows(data, h) for ref in row[1:3] if ref)
    display_filter, exclusion = faces_word_lookup(data)
    filters.add(display_filter)
    excludes.add(exclusion)

    def body(pc):
        i = bisect_left(procedures, pc)
        require(procedures[i] == pc, "Invalid Toro function")
        end = procedures[i+1]
        return code[pc:end], rows[bisect_left(pcs, pc):bisect_left(pcs, end)]

    def include(pc, block, commands):
        selected[pc] = block
        for r in commands:
            for ref in r["references"]:
                if ref["kind"] == "pool":
                    off = ref["target"]
                    start = h.pool_start + off
                    strings[off] = data[start:data.index(0, start, h.pool_end)]

    # Preserve the retry loop and its source fallback. The caller reports a
    # concrete frontier for a drawn child whose dependencies are still unported.
    for pc in (0x7d43a, 0x2660, 0x2626, 0x0fd1, 0x4001, 0x2092, 0x2151, 0x2c61, 0x2d8a,
               0x12f39, 0x2d21, 0x870f, 0x2d3b, 0x12233, 0x130f3, 0x13195, 0x131b4, 0x13219, 0x1308e, 0x1346f, 0x2962, 0x206d,
               0x2192, 0x2161, 0x217d, 0x8d868, 0x8d7da, 0x8d5d0, 0x8d5b4, 0x20da,
               0x1264e, 0x1258b, 0x2919, 0x12437, 0x12577, 0x12558, 0x1246f, 0x1245a,
               0x2bc9, 0x1c57, 0x1c5b, 0x125d3, 0x11d76, 0x11d9f, 0x11d5c, 0x1320e, 0x2105, 0x3a91,
               0x8ddb5, 0x10c9b, 0xb009, 0xbe0d, 0x899f9, 0x1090f, 0xcaae, 0x108c6,
               0xa718, 0xca33, 0x1076a, 0xa59e, 0xc9cd, 0xa58f, 0xca71, 0x1043d, 0xa540, 0xca07,
               0x8b0bd, 0x8b10b, 0x8b0e2, 0x178d, 0x8aa9f, 0x8abae, 0x8ab3c,
               0x8d028, 0x89fc4, 0x89ff1, 0x8d025, 0x8a0cb, 0x4e, 0x46,
               0xd1fe, 0xd017, 0xb22b, 0xcf71, 0xcf9d, 0xcf2e, 0xb13c, 0xa769, 0xce95,
               0x2275, 0x94af, 0x9c8a, 0x50659,
               0x97a3,0x96fb,0x9655,0x95af,0x9b4a,0x9ae4,0x9a71,0x99fb,
               0x1ea6,0x2445,0x233f,0x21ab,0xad2a,0xad13,0xacbb,0xab1b):
        include(pc, *body(pc))
    require(table(data,h.constant_start,0x2a44)[1]==(
        0x20000000,0x20000006,0,0x20000000,0x20000007,0x20000010,
        0x20000001,0x20000000,0,0x20000003,0x20000000,0,0x20000005,0x20000000,0),
        "Prepared question match triples changed")

    require(table(data,h.constant_start,0x2b01)[1] ==
            (0x20000001,0x20000001,0,0x20000000,0x20000007,0x20000002),
            "Friends introduction notebook filter changed")
    require(table(data,h.constant_start,0xe25e)[1]==(0x20000000,0x20000006,0x20004000) and
            table(data,h.global_start,0x9a2a)[1]==(0x20000000,0x20000001,0),
            "Friends exchange eligibility filters changed")
    _, roster=table(data,h.global_start,0x9a35)
    require(len(roster)==10 and len(set(roster))==10 and all(
        ref>>16==0xb004 and table(data,h.global_start,ref&0xffff)[1]==(0,0) for ref in roster),
        "Invalid initial meeting roster")
    require(table(data, h.constant_start, 0x2a3c)[1] == (0x20000000, 0x20000006, 0),
            "Prepared question field mask filter changed")

    # This family invokes existing word-form/message services (128C, 29B7,
    # 2A65) with immediate filters/default words, followed by messages, actor
    # commands, optional ASK, and the source return. The RNG still draws from
    # every D090 entry in its original order.
    ops = {0x10,0x11,0x20,0x21,0x30,0x31,0x44,0x48,0x51,0x70,0x80,0x84,0x85,
           0x86,0x87,0x88,0x89,0x8b,0x8e,0x90,0x91,0x94,0x99,0xc0,0xc1,0xf0,0xf6}
    for raw in candidates:
        pc = raw & 0x0fffffff
        block, commands = body(pc)
        # Six entries first look up a matching real word and return false
        # immediately on -1. The direct trait questions use the already
        # recovered display-name, message and trait-write owners. The other
        # positive branches introduce additional teaching services; keep their
        # actual jump and the complete negative path through its early RET.
        reject_guard = (0x30,0x91,0x99,0x84,0x87,0x20,0x89,0x51,0x90,0x80,0xc0,0x11,0x86,0x70,0x31)
        if tuple(r["opcode"] for r in commands[:len(reject_guard)])==reject_guard:
            lookup, minus_one, branch = (commands[i]["code_offset"] for i in (5,9,11))
            end = commands[len(reject_guard)]["code_offset"]
            if (code[lookup:lookup+5]==bytes.fromhex("20 03 b0 11 00") and code[minus_one+1]==255 and
                    int.from_bytes(code[branch+1:branch+4],"little")==end and
                    code[commands[1]["code_offset"]+1:commands[1]["code_offset"]+3]==b"\0\0" and
                    code[commands[8]["code_offset"]+1:commands[8]["code_offset"]+3]==b"\0\0"):
                rest=commands[len(reject_guard):]
                calls={r["references"][0]["target"] for r in rest if r["opcode"] in (0x20,0x21)}
                if calls=={0x497a,0x29b7,0xa8a2}:
                    include(pc,block,commands)
                else:
                    include(pc,code[pc:end],commands[:len(reject_guard)])
                filters.add(struct.unpack_from("<H",code,commands[2]["code_offset"]+1)[0])
                continue
        if any(r["opcode"] not in ops for r in commands):
            continue
        local_filters, local_excludes = set(), set()
        for i, r in enumerate(commands):
            op, at = r["opcode"], r["code_offset"]
            # 94 16 00 / F0 reads the adopted player's name.
            if op == 0x94 and code[at:at+4] != bytes.fromhex("94 16 00 f0"):
                break
            if op == 0xf0 and (i == 0 or commands[i-1]["opcode"] != 0x94):
                break
            if op == 0xf6:
                if code[at+1] != 0 or i == 0 or commands[i-1]["opcode"] != 0x99:
                    break
                if not phrase_entries(data, h, struct.unpack_from("<H", code, at-2)[0]):
                    break
            if op == 0x8b and tuple(code[at+1:at+3]) not in ((0x22,2),(0x23,0),(0x21,2)):
                break
            if op == 0x21 and r["references"][0]["target"] not in (0x29b7, 0x2a65):
                break
            if op == 0x20:
                if code[at+1] != 6 or r["references"][0]["target"] != 0x128c or i < 6:
                    break
                args = commands[i-6:i]
                a,b,c,d,e,f = (q["opcode"] for q in args)
                if not (a==0x99 and b in (0x84,0x99) and c==0x88 and d in (0x84,0x99)
                        and e==0x87 and f==0x86):
                    break
                for n, dest in ((0,local_filters),(1,local_filters),(3,local_excludes)):
                    if args[n]["opcode"]==0x99:
                        dest.add(struct.unpack_from("<H",code,args[n]["code_offset"]+1)[0])
        else:
            include(pc, block, commands)
            filters.update(local_filters)
            excludes.update(local_excludes)
    # The recurring prompted-lesson and everyday-contact families call the
    # same native word, actor, answer, and clock owners. Preserve each actual
    # block and its data dependencies. Anything outside these operands keeps
    # its existing source boundary.
    calls = {0x128c,0x2151,0x2262,0x228f,0x26a5,0x2919,0x29b7,0x2a65,
             0x821e,0x92e2,0x93ba,0x9408,0x946e,0x94d1,0x138aa}
    carried, _ = project_word_reuse(data,rows)
    # Native substitutions for these two leaf addresses use the same live
    # dictionary and never need their original callback frames.
    calls.update(carried.keys() | {0x11b0,0x1ea6,0x2275,0x233f,0x2445,0x497a,0x94af,0x94e8,
                                   0x97a3,0x9b4a,0x9c8a,0xa8a2,0xad2a,0x50659})
    flow_ops = ops | {0x12,0x13,0x25,-1,0x4e,0x67,0x81,0x82,0x8a,0x91,0x95,0xc2,0xc3,0xc4,
                      0xbc,0xc5,0xc8,0xc9,0xd0,0xd2,0xd4,0xe0,0xe1,0xf4,0xf5}

    for raw in candidates:
        pc = raw & 0x0fffffff
        block, commands = body(pc)
        if selected.get(pc)==block:
            continue
        local_filters, local_phrases = set(), set()
        for i, r in enumerate(commands):
            op, at = r["opcode"], r["code_offset"]
            if op not in flow_ops:
                break
            if op in (0x20,0x21) and r["references"][0]["target"] not in calls:
                break
            if op == 0x25 and (code[at+3]!=0 or struct.unpack_from("<H",code,at+1)[0]
                               not in (0x29c6,0x29ca,0x75f6)):
                break
            if op == 0x8b:
                service, argc = code[at+1:at+3]
                if (service,argc) not in ((0x22,2),(0x23,0),(0x11,1),(0x63,2),(0x52,3)) and not (
                    service==0x21 and 1<=argc<=10
                ):
                    break
            if op == 0x94 and struct.unpack_from("<H",code,at+1)[0] not in (0x15,0x16,0x59a3):
                break
            if op == 0x95 and struct.unpack_from("<H",code,at+1)[0] not in (0x3dd2,0x59a8,0x59af,0x59b6):
                break
            if op == 0xf6 and (i==0 or commands[i-1]["opcode"]!=0x99 or code[at+1]!=0
                               or not phrase_entries(data,h,struct.unpack_from("<H",code,at-2)[0])):
                break
            if op != 0x99:
                continue
            ct, = struct.unpack_from("<H",code,at+1)
            if ct in (0x2446,0x2873):
                continue
            if i+1<len(commands) and code[r["end"]-28:r["end"]-25]==bytes.fromhex("8b6302"):
                _, terms=table(data,h.constant_start,ct)
                if not terms or len(terms)%3 or any(
                    f>>28!=2 or f&0xfffffff>=19 or k not in (0x20000000,0x20000006,0x20000007,0x20000008)
                    for f,k in zip(terms[::3],terms[1::3])
                ):
                    break
            elif phrase_entries(data,h,ct):
                local_phrases.add(ct)
            elif contact_word_filter(data,h,ct):
                local_filters.add(ct)
            else:
                break
        else:
            include(pc,block,commands)
            filters.update(local_filters)
            for ct in local_phrases:
                for entry in phrase_entries(data,h,ct):
                    offset=entry&0xfffffff
                    start=h.pool_start+offset
                    strings[offset]=data[start:data.index(0,start,h.pool_end)]
                # A phrase array may also serve as 128C's spelling exclusions.
                excludes.add(ct)
    return selected, strings, tuple(sorted(filters)), tuple(sorted(excludes))


def project_common_topic(data):
    blocks, strings, _, _ = collect(data)
    return blocks, strings


def common_topic_filters(data):
    return collect(data)[2]


def common_topic_exclusions(data):
    return collect(data)[3]


def common_topic_vocabulary(data):
    return collect(data)[2:]
