"""Decode cold NJDC prediction and terminal stem records for qualified room readings."""
import struct
from iso_reader import require


def number(data, offset, size):
    require(0 <= offset <= len(data)-size, "NJDC field outside dictionary")
    return int.from_bytes(data[offset:offset+size], "big")


def learned_prefix(data, key):
    index, area, count, capacity, stride = struct.unpack_from(">5I",data,0x1c)
    # 9C440 translates sorted lookup entries to ring slots. 9C4A4
    # compares the wrapped slot rank to return matches in recency order.
    cursor = number(data,0x32,2)
    eligible = []
    for i in range(count):
        slot = number(data,index+2*i,2)
        require(slot<capacity, "NJLEARN invalid slot")
        p = area+slot*stride
        a, b = number(data,p+1,2), number(data,p+3,2)
        nr, nw = a&127, b&127
        reading = data[p+5:p+5+nr]
        if reading.startswith(key):
            require(5+nr+nw<=stride,"Selected NJLEARN word spans multiple blocks")
            written = data[p+5+nr:p+5+nr+nw] or reading
            eligible.append((slot+(capacity if slot<cursor else 0),a>>7,b>>7,reading,written))
    return [row[1:] for row in sorted(eligible,reverse=True)]


def katakana(key):
    # 9AF04 converts the two hiragana ranges around the CP932 0x837F hole.
    result=bytearray()
    for i in range(0,len(key),2):
        cp=number(key,i,2)
        if 0x829f<=cp<0x82de:
            cp+=0xa1
        elif 0x82de<=cp<0x82f2:
            cp+=0xa2
        result.extend(cp.to_bytes(2,"big"))
    return bytes(result)


def root_stems(data,key,with_grade=False):
    # BASE and TAN use six-byte root nodes. The terminal sibling bit
    # and the stem offset share the final 24-bit word of each node.
    alphabet, count, width = struct.unpack_from(">IHH",data,0x28)
    require(width==2 and key and len(key)%width==0,"NJDC complete CP932 mora required")
    labels = [data[alphabet+i*width:alphabet+(i+1)*width] for i in range(count)]
    nodes, stems = struct.unpack_from(">II",data,0x30)
    start=nodes
    for i in range(0,len(key),width):
        glyph=key[i:i+width]
        if glyph not in labels:
            return []
        key_id=labels.index(glyph)+1
        for n in range(start,stems,6):
            info=number(data,n+3,3)
            if data[n]==key_id:
                break
            if data[n]>key_id or info & 0x400000:
                return []
        else:
            raise ValueError("Unterminated NJDC sibling list")
        if i+width<len(key):
            # 9AB08..48 reads a relative 17-bit child delta. Its low bit
            # occupies the high bit of byte 3; sibling rows are six bytes.
            child=(number(data,n+1,2)<<1)|(info>>23)
            if child==0x1ffff:
                return []
            require(child>0 and n+child*6<stems,"Invalid NJDC child reference")
            start=n+child*6
    offset=info&0x3fffff
    if offset==0x3fffff:
        return []
    length_bits,left_bits,right_bits=struct.unpack_from(">3H",data,0x38)
    head_size=1+(length_bits+left_bits+right_bits+7)//8
    pos,left_count,right_count=struct.unpack_from(">IHH",data,0x1c)
    require(number(data,0x24,2) in (1,2),"NJDC part-of-speech width")
    pos_width=number(data,0x24,2)
    records=[]
    p=stems+offset
    while p<len(data)-4:
        first_byte=data[p]
        bits=number(data,p+1,head_size-1)
        packed=(head_size-1)*8
        packed-=length_bits
        size=bits>>packed
        packed-=left_bits
        left=(bits>>packed)&((1<<left_bits)-1)
        packed-=right_bits
        right=(bits>>packed)&((1<<right_bits)-1)
        require(0<left<=left_count and 0<right<=right_count and
                p+head_size+size<=len(data)-4,"Invalid NJDC stem record")
        l=number(data,pos+(left-1)*pos_width,pos_width)
        r=number(data,pos+(left_count+right-1)*pos_width,pos_width)
        # 9A570..604: zero stored bytes select the reading, converted by
        # 9AF04 only when the decoded stem's 0x40 flag is set.
        spelling=data[p+head_size:p+head_size+size] if size else (katakana(key) if first_byte&0x40 else key)
        row = (l,r,key,spelling)
        # 9A690 stores the six-bit dictionary grade, 9A498 copies it
        # into the lexical record. Keep it for ranking competing stems.
        records.append((*row,first_byte&63) if with_grade else row)
        if first_byte&128:
            return records
        p+=head_size+size
    raise ValueError("Unterminated NJDC stem group")


def connected(con,left,right):
    if not left or not right:
        return False
    stride=(number(con,0x1c,2)+7)//8
    col=left-1
    return bool(con[number(con,0x20,4)+(right-1)*stride+col//8] & (0x80>>(col%8)))


def terminal_stems(members,key):
    con=members["NJCON"]
    return [r for r in root_stems(members["NJBASE"],key) if connected(con,number(con,0x28,2),r[1])]


def catalog_groups(members,key):
    con=members["NJCON"]
    base=terminal_stems(members,key)
    if len({r[:2] for r in base})>1:
        # The admitted competing classes contribute the same spelling.
        # Records remain in dictionary order for spelling deduplication.
        require(len({r[3] for r in base})==1, "Competing terminal grammar")
    static=base+root_stems(members["NJTAN"],key)
    ll,rr=struct.unpack_from(">HH",con,0x2c)
    # A0B94/A0CEC contribute the raw reading and katakana alternative.
    static.extend(((ll,rr,key,key),(ll,rr,key,katakana(key))))
    learned=learned_prefix(members["NJLEARN"],key)
    return learned,static


def encode_groups(learned,static):
    out=bytearray()
    for l,r,reading,written in learned+static:
        out.extend(struct.pack("<4H",l,r,len(reading),len(written)))
        out.extend(reading+written)
    return bytes(out)


def project_na(members):
    learned,static=catalog_groups(members,"な".encode("cp932"))
    return struct.pack("<4sHH",b"NJN1",len(learned),len(static))+encode_groups(learned,static)


def particle_group(members,key,pairs):
    """One BASE head plus one terminal FZK, with the same suffix for kana heads."""
    con = members["NJCON"]
    end_class, start_class, raw_left, raw_right = struct.unpack_from(">4H", con, 0x28)
    prefix, second = key[:2], key[2:]
    heads = root_stems(members["NJBASE"],prefix,True)
    tails = root_stems(members["NJFZK"],second,True)
    joined = [(h,t) for h in heads for t in tails if
              connected(con,h[0],start_class) and connected(con,t[0],h[1])
              and connected(con,end_class,t[1])]
    # A suffix cannot open a clause by itself. Reject extra competing
    # dictionary families only when their head accepts the initial class.
    reachable = {(h,t) for h,t in pairs if connected(con,h[0],start_class)}
    if not joined or {(h[:4],t[:4]) for h,t in joined} != reachable:
        return None
    terminal = [t for t in tails if connected(con,end_class,t[1])]
    if any(t[2]!=second or t[3]!=second or t[4] for t in terminal):
        return None
    whole = [h for h in root_stems(members["NJBASE"],key,True) if connected(con,end_class,h[1])]
    if whole:
        highest = max(h[4] for h in whole)
        if len({h[3] for h in whole if h[4]==highest})>1:
            # BOOT's preliminary first-result search can prefer one of the
            # tied spellings before the graded enumeration (e.g. 茄子/那須).
            return None
    # Without a whole stem, other analyses can split the phrase into two
    # independent words. Retain the previously verified simple-particle case.
    if not whole and (len({t for _,t in joined})!=1 or
                      not connected(con,joined[0][1][0],raw_right) or len(pairs)!=len(joined)):
        return None
    # All supported endings use the second kana verbatim and add no grade.
    # BOOT visits the first compatible ending before same-score alternatives.
    # Its later spelling filter keeps the earliest such row.
    suffix_by_head = {}
    for predecessor in range(1,number(con,0x1e,2)+1):
        for t in terminal:
            if connected(con,t[0],predecessor):
                suffix_by_head[predecessor] = t[1]
                break
    # On a tie the whole BASE form precedes an attached ending. For
    # example the second, joined 亡き is discarded by the spelling filter.
    graded = [(h[4],h[:4]) for h in whole]
    graded.extend((h[4],(h[0],suffix_by_head[h[1]],key,h[3]+second))
                  for h in heads if connected(con,h[0],start_class) and h[1] in suffix_by_head)
    static = [row for _,row in sorted(graded,key=lambda item:-item[0])]
    tan = root_stems(members["NJTAN"],key,True)
    if tan and any(h[4]<=max(r[4] for r in tan) for h,_ in joined):
        return None  # Cross-bank grade ties have their own enumeration order.
    learned = learned_prefix(members["NJLEARN"],key)
    predicted = {r[3] for r in learned[:9]}
    if ({r[3] for r in static} & {r[3] for r in tan}) - predicted:
        return None
    static.extend(r[:4] for r in tan)
    if raw_right in suffix_by_head:
        static.append((raw_left,suffix_by_head[raw_right],key,katakana(prefix)+second))
    static.extend(((raw_left,raw_right,key,key),(raw_left,raw_right,key,katakana(key))))
    return (learned,static),suffix_by_head


def qualified_queries(members):
    prefix = "な".encode("cp932")
    con = members["NJCON"]
    end_class = number(con, 0x28, 2)
    stems = ("NJBASE", "NJFZK", "NJTAN")
    first = [row for name in stems for row in root_stems(members[name], prefix)]
    yield prefix, catalog_groups(members, prefix), None
    for scalar in range(0x829f, 0x82f2):
        second = scalar.to_bytes(2, "big")
        key = prefix + second
        whole = terminal_stems(members, key)
        parts = {row[:2] for row in whole}
        spellings = {row[3] for row in whole}
        # A1CC8 final-left bit and A366C predecessor connection bit.
        # Keep only whole stems for which no two-part suffix can connect
        # with a first-mora stem and legally finish this phrase.
        tail = [row for name in stems for row in root_stems(members[name], second)
                if connected(con, end_class, row[1])]
        pairs = [(head,suffix) for head in first for suffix in tail if connected(con,suffix[0],head[1])]
        if pairs:
            group = particle_group(members,key,pairs)
            if group:
                yield key,*group
            continue
        if whole and not (len(parts)>1 and len(spellings)>1):
            yield key, catalog_groups(members, key), None


def project_queries(members):
    groups = list(qualified_queries(members))
    out = bytearray(struct.pack("<4sH", b"NJQ4", len(groups)))
    for key, (learned, forms), suffix in groups:
        out.extend(struct.pack("<3H", len(key), len(learned), len(forms)))
        out.extend(key + encode_groups(learned, forms))
        endings = suffix or {}
        out.extend(struct.pack("<H",len(endings)))
        for predecessor,right in endings.items():
            out.extend(struct.pack("<HH",predecessor,right))
        if endings:
            out.extend(first_result_records(members,key,endings))
    return bytes(out)


def first_result_records(members,key,endings):
    """Immutable contributions to the first-result search, before ranked enumeration."""
    con = members["NJCON"]
    end,start = struct.unpack_from(">HH",con,0x28)
    out = bytearray()
    for part in (key,key[:2]):
        # A26EC resets the lexical ordinal for this length. A270C..2718
        # limits 98F64 to the first five returned rows before A2C64 merges
        # duplicates. Learned/registered exact rows consume slots at lookup.
        rows = [r for r in root_stems(members["NJBASE"],part,True) if connected(con,r[0],start)][:5]
        out.extend(struct.pack("<H",len(rows)))
        for left,right,_,written,grade in rows:
            if part==key:
                eligible = connected(con,end,right)
            else:
                eligible = right in endings
                if eligible:
                    right = endings[right]
                    written += key[2:]
            out.extend(struct.pack("<H",eligible))
            if eligible:
                out.extend(struct.pack("<H",grade))
                out.extend(encode_groups([],[(left,right,key,written)]))
    return out
