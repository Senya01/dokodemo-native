"""Project qualified room keyboard query groups from the shipped NJDC banks."""
from pathlib import Path
import struct

from iso_reader import Region, require
from room_word_na_dictionary import project_queries


def project_word_keys(members):
    blocked = set()
    for name, kind in (("NJCON",0xf0000),("NJBASE",0),("NJFZK",1),("NJTAN",2),
                       ("NJYOMI",0x10000),("NJLEARN",0x20001),("CUSTOM",3)):
        b = members[name]
        u16 = lambda i: struct.unpack_from(">H", b, i)[0]
        u32 = lambda i: struct.unpack_from(">I", b, i)[0]
        require(b[:8] == b"NJDC\x00\x01\x00\x00" and u32(8)==kind and
                b[-4:]==b"NJDC", "Unexpected NJDC container")
        # 9A0E8 routes stem, conjugation, yomi and learning dictionaries by type.
        # Rule dictionary NJCON supplies connections, with no reading entries.
        if kind==0xf0000:
            continue
        if kind==0x20001:
            # 9C310/9CE08 compare indexed readings in the fixed-width record ring.
            index, top = u32(0x1c), u32(0x20)
            count, capacity, stride = u32(0x24), u32(0x28), u32(0x2c)
            require(count<=capacity and stride==32 and index+count*2<=top and
                    top+capacity*stride<=len(b)-4, "Invalid NJLEARN ring")
            for i in range(count):
                slot=u16(index+2*i)
                require(slot<capacity, "Invalid NJLEARN entry index")
                entry=top+slot*stride
                if b[entry+5]:
                    lead=b[entry+5]
                    blocked.add(u16(entry+5) if lead>=128 else lead)
            continue
        header = 0x1c if kind==0x10000 else 0x28
        table, count, width = u32(header),u16(header+4),u16(header+6)
        require(width==2 and table+count*2<=len(b),"Invalid NJ reading alphabet")
        blocked.update(u16(table+2*i) for i in range(count))
    # None of the dictionary traversals can enter a matching node with these
    # initial units. Restrict projection to the softkey alphanumerics and
    # katakana whose grammatical fallback (A0AA4) copies the original unit.
    units=[*range(48,58),*range(65,91),*range(97,123),*range(0x8340,0x837f),*range(0x8380,0x8395)]
    keys=[x for x in units if x not in blocked]
    return struct.pack("<4sH",b"NJW1",len(keys))+struct.pack(f"<{len(keys)}H",*keys)


def dictionary_members(common):
    members={}
    for name in ("NJCON","NJBASE","NJFZK","NJTAN","NJYOMI","NJLEARN","CUSTOM"):
        region=common.payload(common.by_name("GUI/DIC/"+name+".A"))
        members[name]=region.read(0,region.size)
    return members


def room_word_conversion_media(common):
    members=dictionary_members(common)
    r=Region(project_word_keys(members))
    catalog=Region(project_queries(members))
    return {Path("assets/game/post-profile/room-word-keys.bin"):(r,r.sha256()),
            Path("assets/game/post-profile/room-word-dictionary.bin"):(catalog,catalog.sha256())}
