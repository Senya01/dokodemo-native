"""Read original ISO/BPAR model inputs; writes only the models-owned directories.

No dependency on the translation project or third-party Python packages.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def sha(data):
    return hashlib.sha256(data).hexdigest()

def bounded(data, offset, size, what="range"):
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise ValueError(f"{what}: {offset:#x}+{size:#x} outside {len(data):#x}")
    return data[offset:offset+size]

def iso_files(data):
    """ISO9660 primary tree with checked directory records and both-endian fields."""
    pvd = bounded(data, 16*2048, 2048, "PVD")
    if pvd[:7] != b"\x01CD001\x01":
        raise ValueError("not primary ISO9660")
    files = {}
    seen = set()
    def record(d, p):
        length = d[p]
        r = bounded(d, p, length, "directory record")
        if length < 34 or 33+r[32] > length:
            raise ValueError("short directory record")
        lba, size = struct.unpack_from('<I', r, 2)[0], struct.unpack_from('<I', r, 10)[0]
        if lba != struct.unpack_from('>I', r, 6)[0] or size != struct.unpack_from('>I', r, 14)[0]:
            raise ValueError("inconsistent ISO endian fields")
        bounded(data, lba*2048, size, "ISO extent")
        return length, lba, size, r[25], r[33:33+r[32]]
    _, lba, size, _, _ = record(pvd, 156)
    def walk(lba, size, prefix):
        if (lba,size) in seen:
            raise ValueError("recursive directory extent")
        seen.add((lba,size))
        d = bounded(data, lba*2048, size)
        p = 0
        while p < len(d):
            if d[p] == 0:
                p = (p//2048+1)*2048
                continue
            n, child_lba, child_size, flags, name = record(d,p)
            p += n
            if name in (b'\0',b'\1'):
                continue
            name = name.decode('ascii').split(';')[0]
            if '/' in name or name in ('','..','.'):
                raise ValueError("unsafe ISO name")
            path = prefix+name
            if flags & 0x80:
                raise ValueError("multi-extent ISO file unsupported")
            if flags & 2:
                walk(child_lba,child_size,path+'/')
            else:
                if path in files:
                    raise ValueError("duplicate ISO path")
                files[path] = dict(offset=child_lba*2048, bytes=child_size)
    walk(lba,size,'')
    return files

def bpar(data):
    """Immediate BP/BPM slot table. Offsets are relative to header_size."""
    bounded(data,0,16,'BPAR header')
    count,slots,names_size,header = struct.unpack_from('<4I',data)
    names_at = 16+slots*16
    if not 0 < count <= slots or not names_at+names_size <= header <= len(data):
        raise ValueError('BPAR counts/table bounds')
    out = {}
    extents=[]
    for i in range(slots):
        rec = 16+16*i
        no,off,size,opaque = struct.unpack_from('<4I',data,rec)
        if no == 0xffffffff:
            if bounded(data,rec,16) != b'\xff'*16:
                raise ValueError('noncanonical inactive BPAR slot')
            continue
        if no >= names_size:
            raise ValueError('BPAR name offset')
        end = data.find(b'\0', names_at+no, names_at+names_size)
        if end < 0:
            raise ValueError('unterminated BPAR name')
        name = data[names_at+no:end].decode('ascii').replace('\\','/')
        if not name or name.startswith('/') or ':' in name or any(x in ('','..','.') for x in name.split('/')):
            raise ValueError('unsafe BPAR path')
        if name in out:
            raise ValueError('duplicate BPAR name')
        start = header+off
        bounded(data,start,size,'BPAR member')
        out[name] = dict(offset=start,bytes=size,record_offset=rec,opaque=opaque)
        extents.append((start,start+size))
    if len(out) != count:
        raise ValueError('BPAR active slot count')
    prev=header
    for start,end in sorted(extents):
        if start < prev:
            raise ValueError('BPAR overlap')
        prev=end
    return out

def extract():
    spec=json.loads((ROOT/'source-iso.json').read_text('utf-8-sig'))
    iso=ROOT/spec['relative_path']
    data=iso.read_bytes()
    if len(data)!=spec['size_bytes'] or sha(data)!=spec['sha256']:
        raise ValueError('original ISO pin mismatch')
    files=iso_files(data)
    bp_path='PSP_GAME/USRDIR/data/DATA.BP'
    bp_ref=files[bp_path]
    bp=bounded(data,bp_ref['offset'],bp_ref['bytes'])
    packages=bpar(bp)
    out=ROOT/'assets/native/models/source'
    out.mkdir(parents=True,exist_ok=True)
    rows=[]
    selected_packages=[]
    for name,p in packages.items():
        if not name.upper().endswith('.BPM'):
            continue
        blob=bounded(bp,p['offset'],p['bytes'])
        members=bpar(blob)
        if not any(x.upper().endswith('.DVS') for x in members):
            continue
        selected_packages.append(dict(name=name,sha256=sha(blob),**p))
        for member,m in members.items():
            if Path(member).suffix.upper() not in ('.DVS','.DAK','.ASC','.GIM'):
                continue
            payload=bounded(blob,m['offset'],m['bytes'])
            target=out/name/member
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(payload)
            rows.append(dict(package=name,member=member,path=target.relative_to(ROOT).as_posix(),
                             iso_path=bp_path,iso_offset=bp_ref['offset']+p['offset']+m['offset'],
                             package_offset=p['offset'],member_offset=m['offset'],
                             bytes=len(payload),sha256=sha(payload)))
    boot_path='PSP_GAME/SYSDIR/BOOT.BIN'
    br=files[boot_path]
    boot=bounded(data,br['offset'],br['bytes'])
    (out/'BOOT.BIN').write_bytes(boot)
    report=dict(schema='dokodemo-model-source-v1',iso=dict(path=spec['relative_path'],bytes=len(data),sha256=sha(data)),
                data_bp=bp_ref,boot=dict(iso_path=boot_path,path='assets/native/models/source/BOOT.BIN',sha256=sha(boot),**br),
                packages=selected_packages,rows=rows)
    target=ROOT/'research/models/source_manifest.json'
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    from collections import Counter
    print(json.dumps(dict(packages=len(selected_packages),members=len(rows),extensions=dict(Counter(Path(r['member']).suffix for r in rows))),indent=2))

if __name__=='__main__':
    argparse.ArgumentParser(description=__doc__).parse_args()
    extract()
