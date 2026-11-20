"""Project each character's cold dictionary and the filters used by ported routes."""
import struct
from pathlib import Path

from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_code, parse_dic, parse_header
from package_room_common_topic import collect_named_contacts, common_topic_vocabulary, contact_predicate_terms


FILTERS = (0x282D, 0x2837, 0x28DC, 0x28E2, 0x2931, 0x2AF0, 0x2AFF, 0xD4BF, 0xD4DB, 0xD4F8,
           0xDDB9, 0xDDC8, 0xDDD7, 0xDDE6, 0xDDF5, 0xDE10, 0xDE2B, 0xDE37, 0xDE3D,
           0xDE49, 0xDE4F, 0xDE5B, 0xDE61, 0xDE6D, 0xDE80, 0xCFE6, 0xCFF2, 0xCFFE,
           0xD00D, 0xD019, 0xD02F, 0xD041, 0xD04D, 0xD060, 0xD072, 0xD089)

# Dictionary, player dictionary, questions, and the saying selector at 179E.
# The first primary is used on a nonzero second draw. Zero selects the other.
ALTERNATE_LAYOUTS = {
    "USAGI": (0x19, 0x3152, 0x58e8, 0x251c, 5, (0x2532, 0x2541, 0x254b)),
    "INU": (0x19, 0x3152, 0x58f8, 0x23fa, 13, (0x2418, 0x2427, 0x2431)),
    "KAERU": (0x1c, 0x3155, 0x58e3, 0x2498, 5, (0x24ae, 0x24bd, 0x24c7)),
}
ALTERNATE_INTRO_FILTERS = {
    0x251c: (0x257e,0x97f9,0x9805,0x9814,0x9823,0x9832,0x9841,0x9850,0x9865,0x9877,0x9889,0x989b,0x9a95),
    0x23fa: (0x2464,0x2662,0x2671,0x2680,0x268f,0x9258,0x9267,0x927c,0x9288,0x929a,0x92af),
    0x2498: (0x24fa,0x2666,0x280b,0x28a3,0x28b2,0x28d5,0x94a2,0x94b8,0x94cd,0x94e2),
}


def project(script, dictionary, *, robo=False, alternate=None):
    h = parse_header(script)
    out = bytearray(b'RVC4' if alternate else b'RVC3')
    normal_rows, friend_rows, questions, sayings, saying_count, alternate_filters = alternate or (
        0x18, 0x3151, 0x58e7 if robo else 0x58df, 0x28ce if robo else 0x2817, 5, ())

    def word(base, at):
        return struct.unpack_from('<I', script, base + 4 * at)[0]

    def array(base, at):
        header = word(base, at)
        require(header >> 16 == 0x9000, 'Expected KSC array')
        return [word(base, at + i + 1) for i in range(header & 65535)]

    def text(raw):
        # Several INU/USAGI sayings end with the game's 8540 glyph.
        checked = raw[:-2] if alternate and raw.endswith(b"\x85\x40") else raw
        checked.decode('cp932', 'strict')
        out.extend(struct.pack('<H', len(raw)))
        out.extend(raw)

    # 16F4 joins the two backing arrays. Cold GAME48 imports DIC into slots 1..162.
    # Representing the other slots as initially nil requires checking their real templates.
    for at, count, fields in ((normal_rows, 600, 19), (friend_rows, 100, 30)):
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
    if alternate:
        filters, exclusions = alternate_filters, ()
        nonzero, zero, secondary = filters
        operands = (bytes.fromhex("99") + struct.pack("<H", sayings) +
                    bytes.fromhex("f6005180028b11018911be170091010099") + struct.pack("<H", nonzero) +
                    bytes.fromhex("5110c5170091010099") + struct.pack("<H", zero) +
                    bytes.fromhex("5191020099") + struct.pack("<H", secondary) + b"\x51")
        require(script[28+0x17a3:28+0x17a3+len(operands)] == operands, "Unknown alternate saying selector")
        filters += ALTERNATE_INTRO_FILTERS[sayings]
    elif robo:
        filters = (0x28e1,0x28eb,0x291e,0x2951,0x2974,0x2987,0x29e5,0x2bec,0x2bfb,
                   0x2a0a,0x2a3b,0x2a42,0x2a49,0x2a50,0x2a57,0x2a5e,0x2a65,0x2a6c,0x2a73,
                   0x6d5f,0x7b8b,0x7bab,0x7bcd,0x7bdb,0x7be7,0x7bf3,0x7bff,0x7c1f,0x7c41,
                   0x7d6f,0x7d7e,0x9418,0xa668,0xa672,0xa685,0xa68f,0xbaa3,0xbaaf,0xbccc)
        exclusions = (0x7d71,0x7d80)
    else:
        common_filters, common_exclusions = common_topic_vocabulary(script)
        filters = sorted({*FILTERS, *common_filters, 0x286A})
        exclusions = sorted({0xD01B, 0xD04F, 0xD074, 0xD08B, 0xDE6F}.union(common_exclusions))
    if robo or alternate:
        species = "ROBO" if robo else next(name for name, layout in ALTERNATE_LAYOUTS.items() if layout==alternate)
        _, generic_filters, generic_phrases=collect_named_contacts(script,species,parse_code(script))
        filters=sorted(set(filters)|generic_filters)
        exclusions=sorted(set(exclusions)|generic_phrases.keys())
    out.extend(struct.pack('<H', len(filters)))
    for ct in filters:
        entries = array(h.constant_start, ct)
        if ct in (0x2464, 0x24fa, 0x257e, 0x286a, 0x291e):
            # BI60 receives the two terms directly, not a BI96 group table.
            require(entries == [0x20000000, 0x20000006, 0x20000020,
                                0x20000000, 0x20000007, 0x20000010],
                    'Unexpected stay-extension word filter')
            groups = [entries]
        elif robo and ct in (0x2a0a,0x2a3b,0x2a42,0x2a49,0x2a50,0x2a57,0x2a5e,0x2a65,0x2a6c,0x2a73):
            require(len(entries) == 6, "Invalid ROBO lesson predicate")
            groups = [entries]
        elif (robo or alternate) and contact_predicate_terms(entries):
            groups = [entries]
        else:
            require(all(ref >> 16 == 0xB003 for ref in entries), 'Invalid filter reference')
            groups = [array(h.constant_start, ref & 65535) for ref in entries]
        out.extend(struct.pack('<HH', ct, len(groups)))
        for terms in groups:
            require(len(terms) % 3 == 0, 'Invalid filter triples')
            out.extend(struct.pack('<H', len(terms) // 3))
            for field, op, arg in zip(terms[::3], terms[1::3], terms[2::3]):
                require(field >> 28 == op >> 28 == 2 and (field & 0xfffffff) < (30 if robo or alternate else 19) and
                        (op & 0xfffffff) < 9 and (arg >> 28 == 2 or (arg == 0 and (op & 255) <= 1)),
                        'Unsupported predicate')
                out.extend(struct.pack('<BBI', field & 255, op & 255, arg))
    out.extend(struct.pack('<H', len(exclusions)))
    for ct in exclusions:
        strings = array(h.constant_start, ct)
        out.extend(struct.pack('<HH', ct, len(strings)))
        for ref in strings:
            require(ref >> 28 == 5, 'Invalid word exclusion')
            at = h.pool_start + (ref & 0xfffffff)
            text(script[at:script.index(b'\0', at, h.pool_end)])
    relationships = array(h.global_start, questions)
    require(len(relationships) == 20, 'Invalid relationship table size')
    for ref in relationships:
        require(ref >> 16 == 0xB004, 'Invalid relationship reference')
        fields = array(h.global_start, ref & 65535)
        require(len(fields) == 7 and fields[0] >> 28 in (0, 2) and all(v == 0 for v in fields[1:]),
                'Invalid cold relationship record')
        out.extend(struct.pack('<I', fields[0]))
    choices = array(h.constant_start, sayings)
    require(len(choices) == saying_count, 'Invalid initial saying list')
    if alternate:
        out.extend(struct.pack('<4H', saying_count, *alternate_filters))
    for ref in choices:
        require(ref >> 28 == 5, 'Invalid initial saying')
        at = h.pool_start + (ref & 0xfffffff)
        text(script[at:script.index(b'\0', at, h.pool_end)])
    return bytes(out)


def room_vocabulary_media(root):
    files = {}
    for species in ("NEKO", "ROBO", *ALTERNATE_LAYOUTS):
        archive = BparArchive(root.payload(root.by_name("KS"+species+".BPM")))

        def member(name):
            region = archive.payload(archive.by_name(name))
            return region.read(0, region.size)

        region = Region(project(member(species+".KSC"), member(species+".DIC"),
                                robo=species=="ROBO", alternate=ALTERNATE_LAYOUTS.get(species)))
        files[Path(f"assets/game/post-profile/{species.lower()}-vocabulary.bin")] = region,region.sha256()
    return files
