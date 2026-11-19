"""Conservative original KSC/DIC structural decoder; no game-flow inference.

Layout facts cross-checked with research/scripts/reference/ksc_format.py and
original BOOT hash 2b361beb4c55e08af2828713861429e768a1140f284a11f606fba5f580cd483c.
The previous parser is research evidence, not a runtime dependency. A command
boundary or a stored code address does not assert executable control flow.
"""
from __future__ import annotations
from dataclasses import dataclass
import struct

HEADER_SIZE = 28
VERSION = 0x20030204
INLINE_OPS = {0x48, 0x49, 0x4A, 0x8E}
INLINE_INDEX_CONTROLS = {0x14, 0x15, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C}
NO_IMMEDIATE = set(bytes.fromhex(
    "00 03 08 0B 0C 0D 0E 14 1A 1B 1C 23 31 32 38 3A 3B 41 44 "
    "50 51 52 53 55 57 5B 5D 5F 61 64 65 67 6D 70 78 "
    "84 85 86 87 89 8C 8D B0 B1 B2 B3 B4 B8 B9 BA BB BC BD "
    "C0 C1 C2 C3 C4 C5 C8 C9 D0 D1 D2 D4 D5 D6 E0 E1 E2 E3 F0 F4 F5"))
FIXED = {0x10: 3, 0x11: 3, 0x12: 3, 0x15: 3, 0x18: 3,
         0x20: 4, 0x21: 3, 0x22: 1, 0x28: 4, 0x29: 3,
         0x30: 1, 0x43: 1, 0x4E: 4, 0x71: 2, 0x72: 2, 0x73: 2, 0x75: 2,
         0x80: 1, 0x81: 2, 0x82: 3, 0x83: 4, 0x88: 3, 0x8A: 3, 0x8B: 2,
         **{op: 2 for op in range(0x90, 0x9A)}, 0xA0: 1}


@dataclass(frozen=True)
class Header:
    entrypoint: int
    code_size: int
    pool_size: int
    field_14: int
    local_count: int
    global_count: int
    constant_count: int  # historical alias for ctx+28; immutability unproved

    @property
    def pool_start(self): return HEADER_SIZE + self.code_size
    @property
    def pool_end(self): return self.pool_start + self.pool_size
    @property
    def constant_start(self): return self.pool_end + 4 * self.local_count
    @property
    def global_start(self): return self.constant_start + 4 * self.constant_count
    @property
    def file_size(self): return self.global_start + 4 * self.global_count


def read_uint(data, at, width, limit):
    if width < 1 or at < 0 or at + width > limit or limit > len(data):
        raise ValueError(f"Truncated operand at file 0x{at:X}")
    return int.from_bytes(data[at:at + width], "little")


def parse_header(data):
    if len(data) < HEADER_SIZE:
        raise ValueError("Truncated KSC header")
    magic, version, *fields = struct.unpack_from("<5I4H", data)
    if magic != 0x0043534B or version != VERSION:
        raise ValueError("Unsupported KSC magic/version")
    header = Header(*fields)
    if header.file_size != len(data) or not 0 <= header.entrypoint < header.code_size:
        raise ValueError("KSC size equation or entrypoint invalid")
    return header


def inline_end(data, start, limit):
    pos = start
    controls = []
    while pos < limit:
        code = data[pos]
        if not code:
            return pos + 1, controls
        if code in INLINE_INDEX_CONTROLS:
            index = read_uint(data, pos + 1, 2, limit)
            controls.append(dict(offset=pos, opcode=code, index=index))
            pos += 3
        else:
            pos += 1
    raise ValueError(f"Unterminated inline at file 0x{start:X}")


def parse_code(data):
    header = parse_header(data)
    limit = header.pool_start
    pos = HEADER_SIZE
    rows = []
    cases = {}

    def case(at):
        if at < HEADER_SIZE:
            raise ValueError("Switch header before code")
        value = read_uint(data, at, 4, limit)
        tag = value >> 28
        size = 7 if tag else 4
        refs = []
        if tag in (5, 7):
            refs.append(dict(at=at, width=4, kind="pool_tagged28" if tag == 5 else "code_tagged28", target=value & 0x0FFFFFFF))
        if tag:
            refs.append(dict(at=at + 4, width=3, kind="code", target=read_uint(data, at + 4, 3, limit)))
        return size, refs

    while pos < limit:
        start = pos
        opcode = data[pos]
        pos += 1
        row = dict(offset=start, code_offset=start - HEADER_SIZE, opcode=opcode, references=[])
        if start in cases:
            size, refs = case(start)
            pos = start + size
            row.update(opcode=-1, kind="switch_case", references=refs)
        elif opcode in INLINE_OPS:
            pos, controls = inline_end(data, pos, limit)
            row.update(text_offset=start + 1, controls=controls)
        elif opcode in (0xF6, 0xF7):
            selector = read_uint(data, pos, 1, limit)
            pos += 3 if selector else 1
        elif opcode in (0x24, 0x25):
            pos += opcode == 0x24
            selector = read_uint(data, pos + 2, 1, limit)
            pos += 5 if selector else 3
        elif opcode == 0x13:
            at = pos
            seen = set()
            while True:
                if at in seen:
                    raise ValueError(f"Cyclic switch chain at file 0x{at:X}")
                seen.add(at)
                size, refs = case(at)
                if at == pos:
                    row["references"] = refs
                    first_size = size
                else:
                    cases[at] = size
                if size == 4:
                    break
                at = HEADER_SIZE + refs[-1]["target"]
            pos += first_size
        else:
            if opcode not in FIXED and opcode not in NO_IMMEDIATE:
                raise ValueError(f"Unaudited opcode 0x{opcode:02X} at file 0x{start:X}, pc 0x{start - HEADER_SIZE:X}")
            pos += FIXED.get(opcode, 0)
            if opcode in (0x10, 0x11, 0x12, 0x15, 0x18, 0x21, 0x29, 0x8A):
                row["references"].append(dict(at=start + 1, width=3, kind="code", target=read_uint(data, start + 1, 3, limit)))
            elif opcode in (0x20, 0x28):
                row["references"].append(dict(at=start + 2, width=3, kind="code", target=read_uint(data, start + 2, 3, limit)))
            elif opcode == 0x88:
                row["references"].append(dict(at=start + 1, width=3, kind="pool", target=read_uint(data, start + 1, 3, limit)))
        if pos > limit:
            raise ValueError(f"Instruction exceeds code at file 0x{start:X}")
        row["end"] = pos
        rows.append(row)
    if not cases.keys() <= {row["offset"] for row in rows}:
        raise ValueError("Switch metadata overlaps an instruction")
    return rows


def table_entries(data):
    header = parse_header(data)
    for name, start, count in (("local", header.pool_end, header.local_count),
                                ("ctx28", header.constant_start, header.constant_count),
                                ("global", header.global_start, header.global_count)):
        for index in range(count):
            at = start + 4 * index
            value = read_uint(data, at, 4, len(data))
            yield dict(table=name, index=index, at=at, value=value,
                       tag=value >> 28, payload=value & 0x0FFFFFFF)


def references(data, rows=None):
    for row in rows if rows is not None else parse_code(data):
        for ref in row["references"]:
            yield dict(ref, command_offset=row["offset"])
    for row in table_entries(data):
        if row["tag"] in (5, 7):
            yield dict(at=row["at"], width=4, kind="pool_tagged28" if row["tag"] == 5 else "code_tagged28",
                       target=row["payload"], table=row["table"], index=row["index"])


def pool_fields(data):
    header = parse_header(data)
    pos = header.pool_start
    while pos < header.pool_end:
        end = data.find(b"\0", pos, header.pool_end)
        if end < 0:
            raise ValueError(f"Unterminated pool at file 0x{pos:X}")
        yield pos, end + 1
        pos = end + 1


def validate(data):
    header = parse_header(data)
    rows = parse_code(data)
    starts = {row["code_offset"] for row in rows}
    pools = {a - header.pool_start for a, _ in pool_fields(data)}
    if header.entrypoint not in starts:
        raise ValueError("Entrypoint misses a command boundary")
    refs = list(references(data, rows))
    for ref in refs:
        if ref["kind"].startswith("code") and ref["target"] not in starts:
            raise ValueError(f"Code reference misses a boundary: {ref}")
        if ref["kind"].startswith("pool") and ref["target"] not in pools:
            raise ValueError(f"Pool reference misses a field boundary: {ref}")
    return header, rows, refs


def parse_dic(data):
    if len(data) < 4 or read_uint(data, 0, 4, len(data)) != 0x900000A2:
        raise ValueError("Unsupported original DIC top-level header")
    count = 162
    table_end = 4 + count * 4
    pointers = [read_uint(data, 4 + index * 4, 4, len(data)) for index in range(count)]
    starts = [p & 0xFFFFFF for p in pointers]
    if any(p >> 24 != 0xB0 for p in pointers) or starts[0] != table_end or starts[-1] >= len(data) or any(a >= b for a, b in zip(starts, starts[1:])):
        raise ValueError("Invalid DIC record pointers")
    rows = []
    for index, (start, end) in enumerate(zip(starts, starts[1:] + [len(data)])):
        if start + 0x50 > end:
            raise ValueError(f"Truncated DIC record {index}")
        words = [read_uint(data, start + at, 4, end) for at in range(0, 0x50, 4)]
        if words[0] != 0x90000013:
            raise ValueError("Unsupported DIC record header")
        if any(words[at // 4] >> 24 != tag for at, tag in
               [(4, 0x20), (0x10, 0x20), (0x14, 0x20), (0x18, 0x30), (0x1C, 0x30),
                (0x20, 0x20), (0x24, 0x20), (0x30, 0x20), (0x34, 0x20), (0x4C, 0x20)]):
            raise ValueError("Unsupported DIC metadata tags")
        if any(words[at // 4] for at in (0x28, 0x2C, 0x3C, 0x40, 0x44)) or (words[0x48 // 4] and words[0x48 // 4] >> 24 != 0x20):
            raise ValueError("Unsupported DIC opaque fields")
        expected = start + 0x50
        fields = []
        for field_at, name in ((8, "written"), (12, "reading"), (0x38, "alternate")):
            word = words[field_at // 4]
            if not word and name == "alternate":
                continue
            at = word & 0xFFFFFF
            if word >> 24 != 0x60 or at != expected:
                raise ValueError(f"Invalid DIC {name} pointer")
            stop = data.find(b"\0", at, end)
            if stop < 0:
                raise ValueError("Unterminated DIC string")
            raw = data[at:stop]
            text = raw.decode("cp932", "strict")
            fields.append(dict(name=name, offset=at, end=stop + 1, pointer_offset=start + field_at,
                               raw_hex=raw.hex(), text=text))
            expected = stop + 1
        initial = data[start + 0x4C:start + 0x4E].decode("cp932", "strict")
        if expected != end or len(initial) != 1 or fields[1]["text"][:1] != initial or len(fields[0]["text"]) != (words[4] & 0xFFFFFF):
            raise ValueError("DIC linked length/initial or packed extent mismatch")
        rows.append(dict(index=index, offset=start, end=end, pointer_offset=4 + index * 4,
                         words=[dict(offset=start + n * 4, raw_u32=value, tag=value >> 24,
                                     payload=value & 0xFFFFFF) for n, value in enumerate(words)],
                         fields=fields, initial=initial, initial_offset=start + 0x4C))
    return rows
