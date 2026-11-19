"""Project Toro's cold dictionary and the filters used by his first conversations."""
import struct
from pathlib import Path

from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_dic, parse_header
from package_room_common_topic import common_topic_vocabulary


FILTERS = (0x282D, 0x2837, 0x28DC, 0x28E2, 0x2931, 0x2AF0, 0x2AFF, 0xD4BF, 0xD4DB, 0xD4F8,
           0xDDB9, 0xDDC8, 0xDDD7, 0xDDE6, 0xDDF5, 0xDE10, 0xDE2B, 0xDE37, 0xDE3D,
           0xDE49, 0xDE4F, 0xDE5B, 0xDE61, 0xDE6D, 0xDE80)


def project(script, dictionary):
    h = parse_header(script)
    out = bytearray(b'RVC3')

    def word(base, at):
        return struct.unpack_from('<I', script, base + 4 * at)[0]

    def array(base, at):
        header = word(base, at)
        require(header >> 16 == 0x9000, 'Expected KSC array')
        return [word(base, at + i + 1) for i in range(header & 65535)]

    def text(raw):
        raw.decode('cp932', 'strict')
        out.extend(struct.pack('<H', len(raw)))
        out.extend(raw)

    # 16F4 joins the two backing arrays. Cold GAME48 imports DIC into slots 1..162.
    # Representing the other slots as initially nil requires checking their real templates.
    for at, count, fields in ((0x18, 600, 19), (0x3151, 100, 30)):
        refs = array(h.global_start, at)
        require(len(refs) == count, 'Wrong cold row count')
        for ref in refs:
            require(ref >> 16 == 0xB004 and array(h.global_start, ref & 65535) == [0] * fields,
                    'Unexpected nonempty cold record template')
    rows = parse_dic(dictionary)
    out.extend(struct.pack('<H', len(rows)))
    for row in rows:
        out.extend(struct.pack('<19I', *(v['raw_u32'] for v in row['words'][1:])))
        strings = {f['name']: bytes.fromhex(f['raw_hex']) for f in row['fields']}
        for name in ('written', 'reading', 'alternate'):
            text(strings.get(name, b''))
    common_filters, common_exclusions = common_topic_vocabulary(script)
    filters = sorted(set(FILTERS).union(common_filters))
    exclusions = sorted({0xDE6F}.union(common_exclusions))
    out.extend(struct.pack('<H', len(filters)))
    for ct in filters:
        groups = array(h.constant_start, ct)
        out.extend(struct.pack('<HH', ct, len(groups)))
        for ref in groups:
            require(ref >> 16 == 0xB003, 'Invalid filter reference')
            terms = array(h.constant_start, ref & 65535)
            require(len(terms) % 3 == 0 and all(v >> 28 == 2 for v in terms), 'Invalid filter triples')
            out.extend(struct.pack('<H', len(terms) // 3))
            for field, op, arg in zip(terms[::3], terms[1::3], terms[2::3]):
                require((field & 0xfffffff) < 19 and (op & 0xfffffff) < 9, 'Unsupported predicate')
                out.extend(struct.pack('<BBI', field & 255, op & 255, arg))
    out.extend(struct.pack('<H', len(exclusions)))
    for ct in exclusions:
        strings = array(h.constant_start, ct)
        out.extend(struct.pack('<HH', ct, len(strings)))
        for ref in strings:
            require(ref >> 28 == 5, 'Invalid word exclusion')
            at = h.pool_start + (ref & 0xfffffff)
            text(script[at:script.index(b'\0', at, h.pool_end)])
    relationships = array(h.global_start, 0x58DF)
    require(len(relationships) == 20, 'Invalid relationship table size')
    for ref in relationships:
        require(ref >> 16 == 0xB004, 'Invalid relationship reference')
        fields = array(h.global_start, ref & 65535)
        require(len(fields) == 7 and fields[0] >> 28 in (0, 2) and all(v == 0 for v in fields[1:]),
                'Invalid cold relationship record')
        out.extend(struct.pack('<I', fields[0]))
    choices = array(h.constant_start, 0x2817)
    require(len(choices) == 5, 'Invalid initial saying list')
    for ref in choices:
        require(ref >> 28 == 5, 'Invalid initial saying')
        at = h.pool_start + (ref & 0xfffffff)
        text(script[at:script.index(b'\0', at, h.pool_end)])
    return bytes(out)


def room_vocabulary_media(root):
    archive = BparArchive(root.payload(root.by_name('KSNEKO.BPM')))

    def member(name):
        region = archive.payload(archive.by_name(name))
        return region.read(0, region.size)

    region = Region(project(member('NEKO.KSC'), member('NEKO.DIC')))
    return {Path('assets/game/post-profile/neko-vocabulary.bin'): (region, region.sha256())}
