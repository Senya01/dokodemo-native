"""Toro's ordinary fallback list and directly executable, single-conversation topics."""
from bisect import bisect_left
from pathlib import Path
import struct

from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_code, parse_header
from package_room_word_reuse import faces_word_lookup, ordinary_question_filters


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


def project_list(data):
    h = parse_header(data)
    header, procedures = table(data, h.constant_start, 0xd090)
    require(header == 0x90000421 and all(v >> 28 == 7 for v in procedures), "Toro fallback D090 changed")
    history, initial = table(data, h.global_start, 0x6018)
    require(history == 0x90000064 and not any(initial), "Toro cold F6 word history changed")
    partner, targets = table(data, h.constant_start, 0xe1f3)
    second, recent = table(data, h.global_start, 0x607d)
    require(partner == 0x90000030 and all(v >> 28 == 7 for v in targets), "Invalid 89AAF callback table")
    require(second == 0x90000014 and not any(recent), "Invalid 89AAF callback history")
    words = (header, *procedures, history, *initial, partner, *targets, second, *recent)
    return b"RFL2" + struct.pack(f"<{len(words)}I", *words)


def room_fallback_list_media(root):
    pack = BparArchive(root.payload(root.by_name("KSNEKO.BPM")))
    script = pack.payload(pack.by_name("NEKO.KSC"))
    resource = Region(project_list(script.read(0, script.size)))
    return {Path("assets/game/post-profile/toro-fallback-list.bin"): (resource, resource.sha256())}


def outing_rows(data, h):
    _, refs = table(data, h.global_start, 0x59da)
    require(len(refs) == 133 and all(ref >> 16 == 0xb004 for ref in refs), "Invalid outing roster")
    for ref in refs:
        header, row = table(data, h.global_start, ref & 0xffff)
        require(header == 0x90000009, "Invalid outing descriptor")
        yield row


def roof_details(data, h):
    # 1246F indexes CT2DF7 by the selected scene kind minus one. Its seven
    # slots each hold a fallback phrase list followed by optional word rules.
    def values(ct):
        return table(data, h.constant_start, ct)[1]
    def reference(tag):
        require(tag >> 16 == 0xb003, "Invalid outing group reference")
        return tag & 0xffff
    root = reference(values(0x2df7)[12-1])
    tables, filters = {root:values(root)}, set()
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
    return root, tables, filters


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
    result = bytearray(b"ROT4" + struct.pack("<I20B132B", mask, *(v&255 for v in periods+indices)))
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
    detail, tables, _ = roof_details(data, h)
    result.extend(struct.pack("<HH", detail, len(tables)))
    for ct, entries in sorted(tables.items()):
        result.extend(struct.pack("<HH", ct, len(entries)))
        for tag in entries:
            result.extend(struct.pack("<I", tag))
            if tag >> 28 == 5:
                begin = h.pool_start + (tag & 0x0fffffff)
                text = data[begin:data.index(0, begin, h.pool_end)]
                result.extend(struct.pack("<H", len(text)) + text)
            else:
                require(not tag or tag >> 16 == 0xb003, "Invalid rooftop display rule")
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
    filters.update(roof_details(data,h)[2])
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
               0xd1fe, 0xd017, 0xb22b, 0xcf71, 0xcf9d, 0xcf2e, 0xb13c, 0xa769, 0xce95):
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
