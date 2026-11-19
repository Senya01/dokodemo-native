"""Project NEKO's category questions and responses for learning a new word."""
from bisect import bisect_left

from iso_reader import require
from ksc_format import parse_code, parse_header


# Exact KSC function entries. Stop at the next decoded function (some endings
# jump back to a question instead of ending in a linear return instruction).
_QUESTIONS = (
    0x3E19, 0x77F8, 0x3CB7, 0x5B1F, 0x6102,  # opening and category ASK
    0x8108, 0x746B, 0x733A, 0x7237, 0x8161, 0x71A1, 0x4BE4, 0x43E0,
    0x5AF8, 0x594F, 0x586C, 0x59E3, 0x58CC,  # action direction and subtype
    0x5845, 0x570B, 0x564D, 0x57AB, 0x56B8,  # type and number of participants for category 16
    0x5E9B, 0x5E00, 0x5C65, 0x5BE6, 0x5D67, 0x5D07,  # place type, organization and shop subtypes
    0x6066, 0x600F,  # food subtype question and row update
)
_PERSON = (
    0x49DE, 0x004E, 0x0046, 0x42E1, 0x42FE, 0x4335, 0x43A8, 0x43C3,
    0x50CD, 0x4F77, 0x4E50, 0x502E, 0x4F24, 0x519C, 0x50F4,
)
_WORD = (0x6ABE, 0x6970, 0x68DE, 0x6A3D, 0x67E8)
_REACTIONS = (
    0x0BFE, 0x0281, 0x03D6, 0x0420, 0x0B40,  # category names used by 3FAD
    0x8C80, 0x8CC7, 0x8D7B, 0x8BFD, 0x47BE, 0x4818, 0x4860, 0x48A4,
    0x48DB, 0x4902, 0x41A2, 0x8BB0, 0x204C, 0x454F, 0x468C,
)


def project_teaching_category(data, rows=None):
    header = parse_header(data)
    code = data[28:header.pool_start]
    if rows is None:
        rows = parse_code(data)
    addresses = [row["code_offset"] for row in rows]
    entries = [row["code_offset"] for row in rows if row["opcode"] == 0x30]
    # 3CB7 pushes all 19 original pool strings before the list GUI. 77F8
    # tests the redo selection before switching on the category, while
    # 7B32's robot arm sets bit 1 of field 8 and returns the word row.
    require(code[0x3D05:0x3D08] == bytes.fromhex("8b2113") and
            code[0x7835:0x783B] == bytes.fromhex("9001008013c0") and
            code[0x7B86:0x7B98] == bytes.fromhex("95d23d900000f48008f58002679000007031"),
            "Unexpected teaching category ASK or robot subcategory")
    blocks, strings = {}, {}
    for start in _QUESTIONS + _PERSON + _WORD + _REACTIONS:
        i = bisect_left(entries, start)
        require(i + 1 < len(entries) and entries[i] == start, "Missing NEKO teaching function")
        end = entries[i + 1]
        blocks[start] = code[start:end]
        for row in rows[bisect_left(addresses, start):bisect_left(addresses, end)]:
            for ref in row["references"]:
                if ref["kind"] in ("pool", "pool_tagged28"):
                    at = ref["target"]
                    begin = header.pool_start + at
                    strings[at] = data[begin:data.index(0, begin, header.pool_end)]
    return blocks, strings
