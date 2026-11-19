"""Bounded DVS, DAK and ASC readers for the original UCJS10002 v1.01 data.

Source-address fields are member-relative byte offsets. Opaque records remain
opaque: successfully consuming bytes does not imply reconstruction of a renderer.
"""
from __future__ import annotations
import math
import struct
from model_source import bounded, sha

class FormatError(ValueError):
    pass

class Reader:
    def __init__(self,data,start=0,end=None):
        self.data=data; self.p=start; self.end=len(data) if end is None else end
        self.spans=[]
        if not 0 <= start <= self.end <= len(data):
            raise FormatError('reader extent')
    def take(self,n,kind):
        if n < 0 or n > self.end-self.p:
            raise FormatError(f'{kind} at {self.p:#x}: {n:#x} exceeds section end {self.end:#x}')
        start=self.p; self.p+=n
        self.spans.append(dict(offset=start,bytes=n,kind=kind,sha256=sha(self.data[start:self.p])))
        return self.data[start:self.p]
    def unpack(self,fmt,kind):
        return struct.unpack(fmt,self.take(struct.calcsize(fmt),kind))
    def align(self):
        self.take((-self.p)%4,'alignment')
    def count(self,width,kind):
        n,=self.unpack('<I',kind+' count')
        return n,self.take(n*width,kind)

def finite(values,what):
    if not all(math.isfinite(x) for x in values):
        raise FormatError('nonfinite '+what)
    return list(values)

def scaled_positions(r,n,kind):
    scale,=r.unpack('<f',kind+' scale')
    finite([scale],kind+' scale')
    raw=list(struct.iter_unpack('<3h',r.take(n*6,kind+' signed16 xyz')))
    return dict(scale=scale,divisor=32767.0,raw_xyz=raw,
                xyz=[[x*scale/32767.0 for x in row] for row in raw])

def bounds(r):
    return dict(box=scaled_positions(r,8,'bounding box'),sphere=finite(r.unpack('<4f','bounding sphere'),'sphere'))

def primitive_records(raw,vertex_count,source_offset):
    r=Reader(raw)
    if not raw: return []
    count,=r.unpack('<h','primitive record count')
    if count<0: raise FormatError('negative primitive record count')
    records=[]
    modes={0:('triangle_strip',4),1:('triangle_fan',5),2:('triangles',3),8:('line_strip',2),9:('lines',1),16:('points',0)}
    for i in range(count):
        offset=r.p
        code,=r.unpack('<H','primitive descriptor')
        opcode,num=code>>8,code&255
        if opcode not in modes: raise FormatError(f'unsupported primitive code {opcode:#x}')
        rawids=r.take(num*2,'primitive vertex indices')
        ids=list(struct.unpack('<'+'h'*num,rawids))
        if any(x<0 or x>=vertex_count for x in ids): raise FormatError('primitive index outside geometry')
        name,ge_mode=modes[opcode]
        if name=='triangles' and num%3: raise FormatError('incomplete triangle list')
        records.append(dict(offset=source_offset+offset,opcode=opcode,ge_mode=ge_mode,topology=name,indices=ids))
    padding=r.take(r.end-r.p,'primitive alignment')
    if len(padding)>2 or any(padding): raise FormatError('unexpected primitive trailer')
    return records

def primitive_triangles(records):
    triangles=[]
    for record in records:
        ids=record['indices'];topology=record['topology']
        if topology=='triangle_strip':
            for k in range(len(ids)-2):
                t=ids[k:k+3] if k%2==0 else [ids[k+1],ids[k],ids[k+2]]
                if len(set(t))==3:triangles.append(t)
        elif topology=='triangles':triangles.extend(ids[k:k+3] for k in range(0,len(ids),3))
        elif topology=='triangle_fan':triangles.extend([ids[0],ids[k],ids[k+1]] for k in range(1,len(ids)-1))
    return triangles

def parse_dvs(data):
    r=Reader(data)
    head=r.take(16,'DVS header')
    if head[:4]!=b'DVS\0':
        raise FormatError('DVS magic')
    units,count_word=r.unpack('<II','material section header')
    count=struct.unpack('<h',data[20:22])[0]
    end=units*4
    if count<0 or not 24<=end<=len(data):
        raise FormatError('material count/end')
    mr=Reader(data,r.p,end)
    materials=[]
    for i in range(count):
        start=mr.p
        raw=mr.take(36,'material common')
        flags=struct.unpack_from('<H',raw)[0]
        refs=[]
        for bit in (1,2,4):
            if flags&bit:
                off=mr.p; desc=mr.take(36,'texture descriptor')
                name=desc[4:].split(b'\0',1)[0]
                try: text=name.decode('ascii')
                except UnicodeDecodeError as e: raise FormatError('non-ASCII texture name') from e
                refs.append(dict(role_bit=bit,offset=off,name=text,raw_hex=desc.hex()))
        materials.append(dict(index=i,offset=start,end=mr.p,flags=flags,raw_common_hex=raw.hex(),textures=refs))
    padding=mr.take(end-mr.p,'material padding')
    r.p=end
    model_bounds=bounds(r)
    nh=r.p
    node_header=r.take(12,'node table header')
    count=struct.unpack_from('<h',node_header)[0]
    if count<0 or count>(len(data)-r.p)//6:
        raise FormatError('node count')
    nodes=[]
    for i in range(count):
        start=r.p
        units,typ=r.unpack('<Ih','node header')
        end=units*4
        if not r.p<=end<=len(data):
            raise FormatError(f'node {i} end {end:#x} at {start:#x}')
        nr=Reader(data,r.p,end)
        node=dict(index=i,offset=start,end=end,type=typ)
        if typ not in (0,2,8,11) and 0<=typ<14:
            common_at=nr.p
            raw=nr.take(74,'node common')
            flags,=struct.unpack_from('<H',raw)
            node['common']=dict(offset=common_at,flags=flags,short_fields=list(struct.unpack_from('<5h',raw)),
                                vectors4=[finite(struct.unpack_from('<4f',raw,10+16*j),'node vector') for j in range(4)],
                                raw_hex=raw.hex())
            if flags&1:
                node['common']['optional_matrix4']=finite(nr.unpack('<16f','optional common matrix'),'matrix')
        node['payload_offset']=nr.p
        if typ==1:
            mode,small,flags,material,unknown,n=nr.unpack('<BBHBBh','geometry header')
            if n<0 or material!=255 and material>=len(materials):
                raise FormatError('geometry vertex count or material reference')
            mf=0 if material==255 else materials[material]['flags']
            geom=dict(mode=mode,small_signed=small if small<128 else small-256,flags=flags,
                      material_index=None if material==255 else material,unknown_byte=unknown,vertex_count=n)
            if flags&0x169:
                geom['positions_offset']=nr.p
                geom['positions']=scaled_positions(nr,n,'positions');nr.align()
            if flags&0x149:
                if flags&0x4000 or mf&2:
                    geom['normals_s8']=list(struct.iter_unpack('<3b',nr.take(3*n,'signed8 normals')));nr.align()
                color_count=n if flags&0x1000 else 1
                geom['rgba8']=list(struct.iter_unpack('<4B',nr.take(4*color_count,'RGBA colors')))
                if mf&5:
                    geom['uv_offset']=nr.p
                    geom['uv']=[finite(uv,'UV') for uv in struct.iter_unpack('<2f',nr.take(8*n,'UV float2'))]
                geom['bounds']=bounds(nr)
                if flags&4:
                    bone_count,=nr.unpack('<I','bone count')
                    if bone_count>=21:
                        raise FormatError('bone count reaches native early-return limit')
                    geom['bones']=[]
                    for bi in range(bone_count):
                        off=nr.p
                        ident,=nr.unpack('<I','bone ID word')
                        matrix=finite(nr.unpack('<16f','bone matrix'),'bone matrix')
                        geom['bones'].append(dict(offset=off,id_word=ident,matrix4=matrix))
                    geom['weights_offset']=nr.p
                    raw=nr.take(24*n,'vertex weight records')
                    geom['weight_records_hex']=raw.hex()
                if flags&0x2041:
                    geom['triangles_offset']=nr.p
                    num,raw=nr.count(8,'triangle signed16 records')
                    geom['triangle_records_s16']=list(struct.iter_unpack('<4h',raw))
                    if any(x<0 or x>=n for t in geom['triangle_records_s16'] for x in t[:3]):
                        raise FormatError('triangle index outside geometry')
                    a,b=nr.unpack('<hh','secondary triangle counts')
                    if a<0 or b<0:
                        raise FormatError('negative secondary triangle count')
                    geom['secondary_triangle_counts']=[a,b]
                    geom['secondary_triangle_records_s16']=list(struct.iter_unpack('<4h',nr.take(8*(a+b),'secondary triangle records')))
                geom['primitive_streams']=[]
                for j in range(3 if mode!=4 and flags&0x2000 else 1):
                    at=nr.p; _,raw=nr.count(4,'primitive words')
                    geom['primitive_streams'].append(dict(offset=at,words=list(struct.unpack('<'+'I'*(len(raw)//4),raw)),
                                                         records=primitive_records(raw,n,at+4)))
            node['geometry']=geom
        elif typ==4:
            raw=nr.take(24,'type4 parameters')
            flags,v=struct.unpack_from('<Hh',raw,2)
            if v: nr.take(4,'type4 conditional scalar')
            if flags&2: node['external_name_raw_hex']=nr.take(32,'type4 external texture name').hex()
        elif typ==5:
            raw=nr.take(12,'type5 parameters')
            v,flags=struct.unpack_from('<hH',raw)
            if flags&0x30: nr.take(4,'type5 conditional scalar')
            if v==3: nr.take(8,'type5 pair')
            elif v==2: nr.take(12,'type5 triple')
        elif typ in (12,13):
            raw=nr.take(12,'type12/13 parameters')
            if typ==13:
                mi=struct.unpack_from('<h',raw,6)[0]
                if mi!=255 and not 0<=mi<len(materials):
                    raise FormatError('type13 material index')
        elif typ not in (0,2,3,6,7,8,9,10,11):
            node['unsupported_type']=True
        node['uninterpreted_offset']=nr.p
        residual=nr.take(end-nr.p,'uninterpreted node tail')
        node['uninterpreted_bytes']=len(residual)
        node['uninterpreted_sha256']=sha(residual)
        node['uninterpreted_nonzero']=bool(any(residual))
        node['streams']=nr.spans
        nodes.append(node);r.p=end
    tail=r.take(r.end-r.p,'post-node tail')
    return dict(format='DVS',bytes=len(data),sha256=sha(data),header_hex=head.hex(),material_end=mr.end,
                material_count_word=count_word,materials=materials,material_padding_bytes=len(padding),
                bounds=model_bounds,node_table_offset=nh,node_header_hex=node_header.hex(),nodes=nodes,
                post_node_tail_bytes=len(tail),post_node_tail_sha256=sha(tail))

COMPONENTS={0x10:4,0x20:4,0x21:4,0x30:2,0x31:4,0x38:10,0x39:2,
            0x40:2,0x41:3,0x42:2,0x50:4,0x60:4,0x61:10,
            0x70:4,0x71:4,0x72:4,0x73:4,0x80:4,0x81:4,0x90:4,0xf1:10,0xf2:1}

def parse_dak(data):
    r=Reader(data)
    h=r.take(20,'DAK header')
    if h[:4]!=b'DAK\0': raise FormatError('DAK magic')
    count,flags,duration=struct.unpack_from('<III',h,8)
    if count>(len(data)-20)//8: raise FormatError('DAK object count')
    objects=[]
    for i in range(count):
        start=r.p
        units,target,nsec=r.unpack('<Ihh','DAK object header')
        end=units*4
        if not r.p<=end<=len(data) or nsec<0: raise FormatError('DAK object end/count')
        obj=dict(index=i,offset=start,end=end,target_node_index=target,section_count=nsec,sections=[])
        nr=Reader(data,r.p,end)
        if target>=0:
            obj['transform_offset']=nr.p
            obj['transform4']=finite(nr.unpack('<16f','DAK object transform'),'DAK transform')
        for j in range(nsec):
            at=nr.p
            typ,sub,num=nr.unpack('<BBh','DAK section header')
            if num<0: raise FormatError('DAK negative key count')
            section=dict(index=j,offset=at,type=typ,subtype=sub,count=num)
            if typ==0x10: section['extra_header_hex']=nr.take(4,'DAK type10 prefix').hex()
            comp=COMPONENTS.get(typ)
            if typ in (1,2):
                comp={1:3,2:4,15:10 if typ==1 else 7}.get(sub)
                if typ==1: section['prefix4']=finite(nr.unpack('<4f','DAK type1 prefix'),'DAK prefix')
            section['components']=comp
            if comp is None:
                # Runtime component size is not known. Do not guess a 12/20-byte
                # procedural trailer even for a zero-count section.
                section.update(status='unsupported_runtime_components',remaining_declared_sections=nsec-j-1)
                obj['sections'].append(section)
                break
            section['payload_offset']=nr.p
            raw=nr.take(num*comp*4,'DAK key words')
            section['key_words']=[list(x) for x in struct.iter_unpack('<'+'I'*comp,raw)]
            if sub==11:
                section['procedural_trailer_hex']=nr.take(12 if comp==4 else 20,'DAK procedural trailer').hex()
            section.update(end=nr.p,status='bounded_numeric_words')
            obj['sections'].append(section)
        obj['uninterpreted_offset']=nr.p
        residual=nr.take(end-nr.p,'DAK uninterpreted object tail')
        obj.update(uninterpreted_bytes=len(residual),uninterpreted_sha256=sha(residual),uninterpreted_nonzero=bool(any(residual)),streams=nr.spans)
        objects.append(obj);r.p=end
    tail=r.take(r.end-r.p,'DAK post-object tail')
    return dict(format='DAK',bytes=len(data),sha256=sha(data),flags=flags,duration_word=duration,objects=objects,
                post_object_tail_bytes=len(tail),post_object_tail_sha256=sha(tail),
                semantics='Typed word arrays only; packed interpolation and playback are not implemented.')

def parse_asc(data):
    r=Reader(data)
    h=r.take(16,'ASC header')
    if h[:4]!=b'ASC\0': raise FormatError('ASC magic')
    words=list(struct.unpack_from('<3I',h,4))
    # AFT header is observed at the header's byte offset word, not inferred from
    # scanning arbitrary ASCII. The AFT size is preserved as an untyped word.
    aft=words[0]
    ah=bounded(data,aft,16,'ASC AFT header')
    if ah[:4]!=b'AFT\0': raise FormatError('ASC AFT marker')
    names_start=aft+struct.unpack_from('<I',ah,4)[0]
    name_bytes=struct.unpack_from('<I',ah,8)[0]
    raw=bounded(data,names_start,name_bytes,'AFT name table')
    names=[]
    p=0
    while p<len(raw):
        end=raw.find(b'\0',p)
        if end<0: raise FormatError('AFT unterminated name')
        if end>p:
            try: name=raw[p:end].decode('ascii')
            except UnicodeDecodeError as e: raise FormatError('AFT non-ASCII name') from e
            names.append(dict(offset=names_start+p,name=name))
        p=end+1
    ar=Reader(data,names_start+name_bytes)
    blocks=[]
    for i in range(words[2]):
        at=ar.p;header=ar.take(16,'ASQ header')
        if header[:4]!=b'ASQ\0': raise FormatError('ASQ marker in declared block chain')
        hs,size,opaque=struct.unpack_from('<III',header,4)
        if hs!=16: raise FormatError('unsupported ASQ header size')
        raw=ar.take(size,'ASQ opaque payload')
        if size%4: raise FormatError('unaligned ASQ payload')
        blocks.append(dict(index=i,offset=at,end=ar.p,header_bytes=hs,payload_bytes=size,opaque_word=opaque,
                           words=list(struct.unpack('<'+'I'*(size//4),raw)),sha256=sha(data[at:ar.p])))
    if len(names)!=words[1]: raise FormatError('ASC name count differs from AFT table')
    tail=ar.take(ar.end-ar.p,'ASC post-ASQ tail')
    return dict(format='ASC',bytes=len(data),sha256=sha(data),header_words=words,aft_offset=aft,aft_header_hex=ah.hex(),
                name_table_offset=names_start,name_table_bytes=name_bytes,names=names,blocks=blocks,
                post_block_tail_bytes=len(tail),post_block_tail_sha256=sha(tail),
                semantics='AFT names and bounded ASQ blocks; ASQ command meanings and execution are not decoded.')
