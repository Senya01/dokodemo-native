"""Extract the first-day NEKO topic handlers, their message helpers and pool strings."""
from pathlib import Path
import struct

from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_code, parse_header
from package_room_secondary_topics import project_secondary_topics
from package_room_common_topic import common_topic_exclusions, phrase_entries, project_common_topic, table
from package_room_trait_topic import project_trait_topic
from package_room_teaching_category import project_teaching_category
from package_room_word_reuse import project_word_reuse


def project_topic_bodies(data, rows):
    h = parse_header(data)
    # 92E2 -> 91FF reduces the two selected greeting predicates into the
    # category, relationship and saying fields used in the native talk owner.
    def constant_array(index):
        at=h.constant_start+4*index
        header=struct.unpack_from("<I",data,at)[0]
        require(header>>16==0x9000,"Invalid teaching predicate header")
        return struct.unpack_from("<"+"I"*(header&65535),data,at+4)
    for table,group,mask in ((0xdd98,0xdd88,16),(0xddaa,0xdd9a,32)):
        require(constant_array(table)==(0xb0030000|group,) and
                constant_array(group)==tuple(0x20000000|v for v in
                        (0,6,4,11,7,2,7,0,0x8000,8,6,mask,12,6,4)),
                "Unrecognized Toro teaching category or relationship")
    require(constant_array(0x24cf)==(0x20000001,0x20000000,0),
            "Written-form lookup predicate changed")
    # G96C9 rows 74..82 supply these class-one functions.
    first_day = (0x846ab, 0x848f9, 0x84b63, 0x84d7a, 0x84e93, 0x84f41, 0x8506d, 0x8528b, 0x85439)
    ranges = list(zip(first_day, (*first_day[1:], 0x8566e)))
    ranges.extend(((0x123d,0x128c), (0x128c,0x12c2), (0x29b7,0x2a65),
                   (0x2a65,0x2b3a), (0x94d1,0x94e8), (0x94e8,0x9544),
                   (0x8f22,0x8f84), (0x8f84,0x90ae), (0x93ba,0x9408),
                   (0x3f43,0x3fad), (0x41cd,0x41f5), (0x8d3e2,0x8d3f1),
                   (0x3dd8,0x3e19), (0x74d0,0x75ce), (0x75ce,0x7726), (0x7726,0x77f8)))
    code = data[28:h.pool_start]
    blocks,strings = project_secondary_topics(data,rows)
    common,common_strings = project_common_topic(data)
    blocks.update(common)
    strings.update(common_strings)
    traits,trait_strings = project_trait_topic(data,rows)
    blocks.update(traits)
    strings.update(trait_strings)
    require(code[0x8d8f7:0x8d901] == bytes.fromhex("8b350521e2d308480400"),
            "Attention balloon style, call or clear changed")
    teaching=(0x8dba,0x8af9,0x8a57,0x87f8,0x7fe9,0x7f28,0x7b32,0x7c8b,0x71f6,0x715e,0x8134,0x8895,0x8947,0x89b6,0x4dd5,0x4414,0x3fad,0x4182,0x6abe,0x3e19,0xbfe)
    starts=[r['code_offset'] for r in rows if r['opcode']==0x30]
    ranges.extend((s,starts[starts.index(s)+1]) for s in teaching)
    # 8DFB1 calls the shared action-6 owner. Event 2 reads G98A2[2].
    _, callback = scheduled_room_row(data, h)
    scheduled = (0x89883, 0x8940d, 0x1a44, 0x1ed8, 0x89434, 0x89257, 0x8927e,
                 0x891e2, 0x89212, 0x2216, 0x2227, 0x21d2, 0x21ec, 0x2254, 0x7c068,
                 callback & 0x0fffffff, scheduled_room_row(data, h, 3)[1] & 0x0fffffff,
                 0x1f05, 0x7c385, 0x8d9ca, scheduled_room_row(data, h, 6)[1] & 0x0fffffff, 0x7c780,
                 scheduled_room_row(data, h, 4)[1] & 0x0fffffff, 0x7c9df,
                 scheduled_room_row(data, h, 5)[1] & 0x0fffffff, 0x7cbdf)
    ranges.extend((s, starts[starts.index(s)+1]) for s in scheduled)
    for start,end in ranges:
        end_opcode = 0x1b if start == 0x8d9ca else 0x31
        require(code[start] == 0x30 and code[end-1] == end_opcode and code[end] == 0x30,
                "NEKO topic bounds changed")
        blocks[start] = code[start:end]
        for row in rows:
            if start <= row["code_offset"] < end:
                for ref in row["references"]:
                    if ref["kind"] == "pool":
                        at = ref["target"]
                        begin = h.pool_start + at
                        strings[at] = data[begin:data.index(0,begin,h.pool_end)]
    extra,extra_strings=project_teaching_category(data,rows)
    blocks.update(extra)
    strings.update(extra_strings)
    reuse,reuse_strings=project_word_reuse(data,rows)
    blocks.update(reuse)
    strings.update(reuse_strings)
    return blocks, strings


def scheduled_room_row(data, header, event=2):
    _, rows = table(data, header.global_start, 0x98a2)
    require(len(rows) == 8 and rows[event] >> 16 == 0xb004,
            "Invalid scheduled room event row")
    _, values = table(data, header.global_start, rows[event] & 0xffff)
    require(len(values) == 2 and values[0] >> 28 == 2 and values[1] >> 28 == 7,
            "Invalid scheduled room event motion or callback")
    require(struct.unpack_from("<I", data, header.constant_start + 4*0x2874)[0] == 0x20000000,
            "Invalid initial scheduled room camera selector")
    return values


def room_activity_topic_media(root):
    archive = BparArchive(root.payload(root.by_name("KSNEKO.BPM")))
    script = archive.payload(archive.by_name("NEKO.KSC"))
    data = script.read(0, script.size)
    h = parse_header(data)
    code = data[28:h.pool_start]
    rows = parse_code(data)
    blocks, strings = project_topic_bodies(data, rows)
    predicates={}
    phrase_lists={}
    # CF9D and D1A6 use CT2B0C[i][0] for four suggested question endings.
    _,endings=table(data,h.constant_start,0x2b0c)
    require(len(endings)==5 and all(x>>16==0xb003 for x in endings),"Invalid question ending groups")
    first=[table(data,h.constant_start,ref&0xffff)[1][0] for ref in endings]
    require(all(x>>28==5 for x in first[:4]) and first[4]==0,"Invalid question ending labels")
    phrase_lists[0x2b0c]=struct.pack("<5I",0x90000004,*first[:4])
    for word in first[:4]:
        offset=word&0x0fffffff
        pos=h.pool_start+offset
        strings[offset]=data[pos:data.index(0,pos,h.pool_end)]
    def include_strings(ct):
        entries = phrase_entries(data,h,ct)
        require(entries, "Invalid script string list")
        start = h.constant_start + 4*ct
        phrase_lists[ct] = data[start:start+4*(1+len(entries))]
        for entry in entries:
            offset = entry & 0x0fffffff
            pos = h.pool_start + offset
            strings[offset] = data[pos:data.index(0,pos,h.pool_end)]
    # The sixth argument to the night ghost lookup is iterated by 0FDD.
    for operand in (0x462fa,0x3491f):
        include_strings(struct.unpack_from("<H",code,operand)[0])
    for ct in common_topic_exclusions(data):
        include_strings(ct)
    ranges=iter(sorted(blocks.items()))
    start, body=next(ranges)
    for row in rows:
        at=row["code_offset"]
        while at>=start+len(body):
            next_block=next(ranges,None)
            if next_block is None:
                break
            start,body=next_block
        if not start<=at<start+len(body):
            continue
        if code[at:at+2] == b"\xf6\x00":
            if at == 0x1253e:
                # 1246F draws from G0's current sign slot rules. ROT3 carries the
                # three CT2DF7[11] fallback lists indexed by the live locals.
                require(code[at-9:at] == bytes.fromhex("900300900500f485f4"),
                        "Changed outing fallback phrase lookup")
                continue
            require(code[at-3] == 0x99, "Unresolved F6 phrase list")
            ct = struct.unpack_from("<H", code, at-2)[0]
            include_strings(ct)
        if code[at:at+3]!=bytes.fromhex("8b6302") or code[at-3]!=0x99: continue
        ct=int.from_bytes(code[at-2:at],"little")
        pos=h.constant_start+4*ct
        header=struct.unpack_from("<I",data,pos)[0]
        require(header>>16==0x9000,"Invalid BI63 predicate")
        predicates[ct]=data[pos+4:pos+4+4*(header&65535)]
    # Fresh script globals: five optional statements and two independent bit arrays.
    # Runtime owns mutable instances; these are only their initial cells.
    initial_globals = {}
    scheduled_rows = [word for event in (2, 3, 6, 4, 5) for word in scheduled_room_row(data, h, event)]
    initial_globals[0x98a2] = struct.pack("<11I", 0x9000000a, *scheduled_rows)
    _, diary_refs = table(data,h.global_start,0x408f)
    diaries = [table(data,h.global_start,ref & 0xffff)[1] for ref in diary_refs]
    for priority, expected in ((0x700000, (8,)), (0x900000, (7,)), (0x800000, (10, 11)), (0xa00000, (9,))):
        require(tuple(i for i,row in enumerate(diaries) if row[0]&0xf00000==priority)==expected,
                "Unexpected scheduled diary descriptors")
    for descriptor, priority, offset in ((8, 0x700000, 0x3ff), (7, 0x900000, 0x379),
                                         (10, 0x800000, 0x4ce), (11, 0x800000, 0x545),
                                         (9, 0xa00000, 0x477)):
        require(diaries[descriptor][0]==0x20000000|priority and
                diaries[descriptor][1]==0 and diaries[descriptor][-1]==0x50000000|offset,
                "Unexpected scheduled Toro diary")
        begin = h.pool_start+offset
        strings[offset] = data[begin:data.index(0,begin,h.pool_end)]
        require(strings[offset].count(b"$_YOU")== (0 if descriptor==9 else 2 if descriptor==11 else 1),
                "Unexpected scheduled diary substitution")
    # 89FC4 and 8A0CB substitute the chosen kind of exchange in Toro's lines.
    start=h.global_start+4*0x9a26
    header,*labels=struct.unpack_from("<4I",data,start)
    require(header==0x90000003 and all(cell>>28==5 for cell in labels),"Invalid exchange labels")
    initial_globals[0x9a26]=data[start:start+16]
    for cell in labels:
        offset=cell&0x0fffffff
        pos=h.pool_start+offset
        strings[offset]=data[pos:data.index(0,pos,h.pool_end)]
    for word_index,count in ((0x59a8,5), (0x59af,6), (0x59b6,6)):
        start = h.global_start + 4*word_index
        raw = data[start:start+4*(count+1)]
        require(raw == struct.pack("<I", 0x90000000 | count) + bytes(4*count),
                "Invalid fresh NEKO statement or conversation flag array")
        initial_globals[word_index] = raw
    # 2262 indexes the initial season-name array with (month % 12) / 3.
    require(code[0x2264:0x2267] == bytes.fromhex("95 92 60"), "Wrong season name owner")
    season_table = struct.unpack_from("<H",code,0x2265)[0]
    start = h.global_start + 4*season_table
    header, *cells = struct.unpack_from("<5I",data,start)
    require(header==0x90000004 and all(cell>>28==5 for cell in cells),"Invalid season name array")
    initial_globals[season_table] = data[start:start+20]
    for cell in cells:
        offset = cell & 0x0fffffff
        require(offset<h.pool_end-h.pool_start,"Season name outside script pool")
        pos = h.pool_start+offset
        strings[offset] = data[pos:data.index(0,pos,h.pool_end)]
    if 0x2275 in blocks:
        weekday=0x6097
        weekday_start=h.global_start+4*weekday
        weekday_header,*names=struct.unpack_from("<8I",data,weekday_start)
        require(weekday_header==0x90000007 and all(word>>28==5 for word in names),
                "Invalid weekday name array")
        initial_globals[weekday]=data[weekday_start:weekday_start+32]
        for word in names:
            begin=h.pool_start+(word&0xfffffff)
            strings[word&0xfffffff]=data[begin:data.index(0,begin,h.pool_end)]
    # The alien-story and rumor callers both dispatch a mode-zero procedure.
    procedure_lists = {}
    for site in (0x4f55e,0x7d8f2,0x97b8,0x9bed):
        require(code[site] == 0x25 and code[site+3] == 0, "Wrong continuation call")
        ct = struct.unpack_from("<H", code, site+1)[0]
        start = h.constant_start + 4*ct
        header, = struct.unpack_from("<I", data, start)
        count = header & 0xfff
        require(count and header == (0x90000000 | count) and ct+count < h.constant_count,
                "Invalid continuation table")
        targets = struct.unpack_from(f"<{count}I", data, start+4)
        require(all(word>>28 == 7 and (word & 0xfffffff) in blocks for word in targets),
                "Continuation table references an unprojected procedure")
        procedure_lists[ct]=data[start:start+4*(count+1)]
    sections = (blocks,strings,predicates,phrase_lists,initial_globals,procedure_lists)
    out = bytearray(struct.pack("<4s6I",b"RTP5",*(len(section) for section in sections)))
    for entries in sections:
        for at,payload in sorted(entries.items()):
            out += struct.pack("<II",at,len(payload)) + payload
    blob = Region(bytes(out))
    return {Path("assets/game/post-profile/toro-topics.bin"): (blob,blob.sha256())}
