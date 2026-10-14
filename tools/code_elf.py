"""Read-only ELF32/PSP-PRX analysis, without any emulator or external packages.

Addresses are unrelocated module VAs, not absolute PSP runtime addresses.
Optional Capstone output is supplemental and is not trusted for VFPU decoding.
"""
from __future__ import annotations
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
BOOT_SHA = "2b361beb4c55e08af2828713861429e768a1140f284a11f606fba5f580cd483c"
REG = ("zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 "
       "s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra").split()


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def signed(value: int, bits: int) -> int:
    return (value & ((1 << (bits - 1)) - 1)) - (value & (1 << (bits - 1)))


def decode(word: int, pc: int) -> str:
    """Small explicitly bounded MIPS/Allegrex formatter. Unknown stays a word."""
    op, rs, rt, rd, sh, fn = word >> 26, (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31, (word >> 6) & 31, word & 63
    r = lambda n: "$" + REG[n]
    imm, si = word & 65535, signed(word, 16)
    target = ((pc + 4) + si * 4) & 0xFFFFFFFF
    if word == 0:
        return "nop"
    if op == 0:
        if fn in (0, 2, 3):
            return f"{ {0:'sll',2:'srl',3:'sra'}[fn]} {r(rd)}, {r(rt)}, {sh}"
        if fn == 8:
            return f"jr {r(rs)}"
        if fn == 9:
            return f"jalr {r(rd)}, {r(rs)}"
        if fn in (0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2A, 0x2B):
            name = {0x20:'add',0x21:'addu',0x22:'sub',0x23:'subu',0x24:'and',0x25:'or',0x26:'xor',0x27:'nor',0x2A:'slt',0x2B:'sltu'}[fn]
            return f"{name} {r(rd)}, {r(rs)}, {r(rt)}"
    if op in (2, 3):
        return f"{('j' if op == 2 else 'jal')} 0x{((pc+4)&0xF0000000)|((word&0x3FFFFFF)<<2):08X}"
    if op == 1 and rt in (0, 1, 2, 3, 16, 17):
        return f"{ {0:'bltz',1:'bgez',2:'bltzl',3:'bgezl',16:'bltzal',17:'bgezal'}[rt]} {r(rs)}, 0x{target:08X}"
    if op in (4, 5, 0x14, 0x15):
        return f"{ {4:'beq',5:'bne',0x14:'beql',0x15:'bnel'}[op]} {r(rs)}, {r(rt)}, 0x{target:08X}"
    if op in (6, 7, 0x16, 0x17):
        return f"{ {6:'blez',7:'bgtz',0x16:'blezl',0x17:'bgtzl'}[op]} {r(rs)}, 0x{target:08X}"
    if op in (8, 9, 10, 11, 12, 13, 14):
        return f"{ {8:'addi',9:'addiu',10:'slti',11:'sltiu',12:'andi',13:'ori',14:'xori'}[op]} {r(rt)}, {r(rs)}, {si if op<12 else hex(imm)}"
    if op == 15:
        return f"lui {r(rt)}, 0x{imm:X}"
    if op in (0x20, 0x21, 0x23, 0x24, 0x25, 0x28, 0x29, 0x2B):
        return f"{ {0x20:'lb',0x21:'lh',0x23:'lw',0x24:'lbu',0x25:'lhu',0x28:'sb',0x29:'sh',0x2B:'sw'}[op]} {r(rt)}, {si}({r(rs)})"
    if op == 0x1F and fn == 0x20 and sh in (16, 24) and rs == 0:
        return f"{('seb' if sh==16 else 'seh')} {r(rd)}, {r(rt)}"
    return f".word 0x{word:08X} ; outside built-in decoder"


class ELF:
    def __init__(self, path: Path):
        self.path = path
        self.blob = path.read_bytes()
        if len(self.blob) < 52:
            raise ValueError("Truncated ELF")
        h = struct.unpack_from("<16sHHIIIIIHHHHHH", self.blob)
        if h[0][:7] != b"\x7fELF\x01\x01\x01" or h[2] != 8:
            raise ValueError("Expected little-endian ELF32 EM_MIPS")
        if h[9] != 32 or h[11] != 40:
            raise ValueError("Unexpected ELF table sizes")
        self.header = {"class": 32, "endian": "little", "type": h[1], "machine": h[2],
                       "entry_va": h[4], "flags": h[7], "section_count": h[12], "program_header_count": h[10]}
        self.segments = []
        for i in range(h[10]):
            v = struct.unpack("<8I", self.slice(h[5] + 32 * i, 32))
            self.segments.append(dict(zip(("type", "off", "va", "pa", "filesz", "memsz", "flags", "align"), v)))
        rows = [struct.unpack("<10I", self.slice(h[6] + 40 * i, 40)) for i in range(h[12])]
        names = self.slice(rows[h[13]][4], rows[h[13]][5])
        self.sections = []
        for index, v in enumerate(rows):
            sec = dict(zip(("name_offset", "type", "flags", "va", "off", "size", "link", "info", "align", "entsize"), v))
            sec["name"] = names[v[0]:].split(b"\0", 1)[0].decode("ascii")
            sec["index"] = index
            if sec["type"] != 8:
                sec["sha256"] = sha(self.slice(sec["off"], sec["size"]))
            self.sections.append(sec)
        self.relocations = {}
        self.relocation_records = []
        for sec in self.sections:
            if sec["type"] != 0x700000A0:
                continue
            if sec["size"] % 8:
                raise ValueError("Truncated PSP relocation table")
            for at in range(sec["off"], sec["off"] + sec["size"], 8):
                offset, info = struct.unpack("<II", self.slice(at, 8))
                origin, refer = (info >> 8) & 255, (info >> 16) & 255
                if origin >= len(self.segments) or refer >= len(self.segments):
                    raise ValueError("PSP relocation segment outside header")
                file_at = self.segments[origin]["off"] + offset
                rec = {"record_file_offset": at, "offset": offset, "info": info, "type": info & 255,
                       "origin_segment": origin, "referenced_segment": refer, "file_offset": file_at}
                self.relocation_records.append(rec)
                self.relocations[file_at] = rec

    def slice(self, at: int, size: int) -> bytes:
        if at < 0 or size < 0 or at + size > len(self.blob):
            raise ValueError("ELF read outside file")
        return self.blob[at:at + size]

    def section(self, name: str) -> dict:
        return next(sec for sec in self.sections if sec["name"] == name)

    def off(self, va: int) -> int:
        for seg in self.segments:
            if seg["type"] == 1 and seg["va"] <= va < seg["va"] + seg["filesz"]:
                return seg["off"] + va - seg["va"]
        raise ValueError(f"Unbacked VA 0x{va:X}")

    def u32(self, va: int) -> int:
        return struct.unpack("<I", self.slice(self.off(va), 4))[0]

    def cstring(self, va: int) -> str:
        at = self.off(va)
        return self.blob[at:self.blob.index(0, at)].decode("ascii")

    def evidence(self, start: int, end: int) -> dict:
        raw = self.slice(self.off(start), end - start)
        instructions = []
        for offset in range(0, len(raw), 4):
            word = struct.unpack_from("<I", raw, offset)[0]
            instructions.append({"va": start + offset, "file_offset": self.off(start + offset),
                                 "word": f"0x{word:08X}", "bytes": raw[offset:offset + 4].hex(),
                                 "instruction": decode(word, start + offset)})
        return {"start_va": start, "end_va_exclusive": end, "file_offset": self.off(start),
                "sha256": sha(raw), "raw_hex": raw.hex(), "instructions": instructions}

    def imports(self) -> list[dict]:
        sec = self.section(".lib.stub")
        at, end = sec["off"], sec["off"] + sec["size"]
        result = []
        while at < end:
            name, version, attr, words, varcount, count, nids, stubs = struct.unpack("<IHHBBHII", self.slice(at, 20))
            if words < 5 or at + words * 4 > end or varcount:
                raise ValueError("Unsupported import library record")
            functions = []
            for index in range(count):
                nid = self.u32(nids + index * 4)
                stub = stubs + index * 8
                functions.append({"index": index, "nid": f"0x{nid:08X}", "stub_va": stub,
                                  "stub_file_offset": self.off(stub), "stub_raw_hex": self.slice(self.off(stub), 8).hex()})
            result.append({"library": self.cstring(name), "version": version, "attributes": attr,
                           "record_file_offset": at, "nid_table_va": nids, "stub_table_va": stubs,
                           "function_count": count, "variable_count": varcount, "functions": functions})
            at += words * 4
        return result

    def unwind_ranges(self) -> list[dict]:
        """Read this source's .eh_frame absolute-pointer FDE ranges.

        FDEs are compiler-declared unwind ranges, not a complete symbol table.
        CIE augmentation zP has no R override: default DW_EH_PE_absptr.
        Reject unfamiliar CIE format rather than manufacturing a function count.
        """
        sec = self.section(".eh_frame")
        at, end, cies, result = sec["off"], sec["off"] + sec["size"], {}, []
        while at < end:
            size = struct.unpack("<I", self.slice(at, 4))[0]
            if size == 0:
                at += 4
                continue
            if size == 0xFFFFFFFF or size < 4 or at + 4 + size > end:
                raise ValueError("Unsupported .eh_frame record length")
            ident = struct.unpack("<I", self.slice(at + 4, 4))[0]
            if ident == 0:
                version = self.blob[at + 8]
                stop = self.blob.index(0, at + 9, at + 4 + size)
                augmentation = self.blob[at + 9:stop].decode("ascii")
                if version != 1 or "R" in augmentation or augmentation not in ("zP", "zPL", "z", ""):
                    raise ValueError(f"Unsupported CIE {version}/{augmentation}")
                cies[at] = {"file_offset": at, "augmentation": augmentation}
            else:
                cie = at + 4 - ident
                if cie not in cies:
                    raise ValueError("FDE CIE reference unresolved")
                start, length = struct.unpack("<II", self.slice(at + 8, 8))
                result.append({"fde_file_offset": at, "cie_file_offset": cie, "start_va": start,
                               "end_va_exclusive": start + length, "length": length,
                               "pointer_relocation": self.relocations.get(at + 8)})
            at += size + 4
        return result


def dispatch(elf: ELF, table: int, count: int, site: int, name: str) -> dict:
    rows = []
    containing = [item for item in elf.unwind_ranges() if item['start_va'] <= site < item['end_va_exclusive']]
    if len(containing) != 1:
        raise ValueError("Dispatcher unwind range not uniquely established")
    enclosing = containing[0]
    for opcode in range(count):
        at = elf.off(table + opcode * 4)
        handler = elf.u32(table + opcode * 4)
        sec = elf.section(".text")
        if handler % 4 or not sec["va"] <= handler < sec["va"] + sec["size"]:
            raise ValueError("Dispatch target outside text")
        if not enclosing['start_va'] <= handler < enclosing['end_va_exclusive']:
            raise ValueError("Handler outside established dispatcher unwind range")
        reloc = elf.relocations.get(at)
        if not reloc or reloc["type"] != 2 or reloc["referenced_segment"] != 0:
            raise ValueError("Dispatch entry lacks expected R_MIPS_32 relocation")
        rows.append({"opcode": opcode, "opcode_hex": f"0x{opcode:02X}", "table_entry_va": table + opcode * 4,
                     "table_entry_file_offset": at, "handler_va": handler,
                     "handler_file_offset": elf.off(handler), "raw_pointer_hex": elf.slice(at, 4).hex(),
                     "relocation": reloc,
                     "status": "verified_dispatch_edge_semantics_unimplemented",
                     "target_kind": "intrafunction_handler_basic_block",
                     "enclosing_unwind_range": {"start_va": enclosing['start_va'], "end_va_exclusive": enclosing['end_va_exclusive']}})
    data = elf.slice(elf.off(table), count * 4)
    return {"name": name, "table_va": table, "table_file_offset": elf.off(table), "site_va": site,
            "entry_count": count, "distinct_handler_count": len({row["handler_va"] for row in rows}),
            "raw_table_sha256": sha(data), "enclosing_unwind_range": enclosing, "entries": rows}


def analyze(elf: ELF, out: Path) -> dict:
    if sha(elf.blob) != BOOT_SHA:
        raise ValueError("Expected original unmodified v1.01 BOOT.BIN")
    out.mkdir(parents=True, exist_ok=True)
    text = elf.section(".text")
    calls, jumps, jalrs, jrs, prologues, majors = [], [], [], [], [], Counter()
    for offset in range(text["off"], text["off"] + text["size"], 4):
        word = struct.unpack_from("<I", elf.blob, offset)[0]
        va = text["va"] + offset - text["off"]
        op = word >> 26
        majors[op] += 1
        if op in (2, 3):
            target = ((va + 4) & 0xF0000000) | ((word & 0x3FFFFFF) << 2)
            (calls if op == 3 else jumps).append({"va": va, "file_offset": offset, "target_va": target,
                                                   "relocation": elf.relocations.get(offset)})
        if op == 0 and word & 63 in (8, 9):
            rs = (word >> 21) & 31
            (jalrs if word & 63 == 9 else jrs).append({"va": va, "file_offset": offset, "register": REG[rs],
                                                     "is_return": rs == 31})
        if word >> 16 == 0x27BD and signed(word, 16) < 0:
            prologues.append({"va": va, "file_offset": offset, "stack_bytes": -signed(word, 16)})
    unwind = elf.unwind_ranges()
    imports = elf.imports()
    module_sec = elf.section(".rodata.sceModuleInfo")
    attr, version, name, gp, export_start, export_end, import_start, import_end = struct.unpack("<HH28s5I", elf.slice(module_sec["off"], 52))
    module = {"name": name.split(b"\0")[0].decode("ascii"), "attributes": attr, "version": version,
              "gp": gp, "exports_start_va": export_start, "exports_end_va": export_end,
              "imports_start_va": import_start, "imports_end_va": import_end}
    # Each raw major-opcode bucket is exact, but contains any literal words inside
    # .text as well as instructions. VFPU classification is deliberately scoped.
    # PSP VFPU opcode-table provenance is in vfpu_encoding_source.json.
    vfpu_majors = {0x12, 0x18, 0x19, 0x1B, 0x34, 0x35, 0x36, 0x37, 0x3C, 0x3D, 0x3E, 0x3F, 0x32, 0x3A}
    summary = {"schema_version": 1, "source_file": "input/BOOT.BIN", "source_sha256": sha(elf.blob),
               "source_size": len(elf.blob), "address_convention": "unrelocated module VA; use section/segment mappings for file offsets",
               "elf_header": elf.header, "module": module, "segments": elf.segments, "sections": elf.sections,
               "compiler_comments": sorted({part.decode('ascii') for part in elf.slice(elf.section('.comment')['off'], elf.section('.comment')['size']).split(b'\0') if part}),
               "relocations": {"count": len(elf.relocation_records), "type_counts": dict(sorted(Counter(r['type'] for r in elf.relocation_records).items()))},
               "imports": {"library_count": len(imports), "function_count": sum(x['function_count'] for x in imports), "libraries": [{k:v for k,v in x.items() if k!='functions'} for x in imports]},
               "instruction_census": {"text_words": sum(majors.values()), "major_opcode_counts": {f"0x{k:02X}":v for k,v in sorted(majors.items())},
                                      "cop1_words": majors[0x11], "vfpu_candidate_major_words": sum(majors[k] for k in vfpu_majors),
                                      "vfpu_status": "raw-major encoding census; no claim that every text word is reachable instruction",
                                      "direct_jal_sites": len(calls), "direct_j_sites": len(jumps),
                                      "jalr_sites": len(jalrs), "jr_sites": len(jrs), "non_return_jr_sites": sum(not x['is_return'] for x in jrs)},
               "function_census": {"exact_total_function_count": None, "reason": "No .symtab/.dynsym; code includes tail calls, leaf routines, indirect entries, library code and data",
                                   "distinct_direct_jal_targets": len({x['target_va'] for x in calls}),
                                   "negative_sp_addiu_candidates": len(prologues), "compiler_unwind_fde_ranges": len(unwind),
                                   "distinct_unwind_starts": len({x['start_va'] for x in unwind}),
                                   "counts_are_not_added": True}}
    vm = dispatch(elf, 0xE01B8, 254, 0x6B9A4, "ksc_opcode")
    builtin = dispatch(elf, 0xDFD58, 241, 0x68574, "ksc_builtin_via_8B")
    regions = {"ksc_dispatch": elf.evidence(0x6B924, 0x6BA04),
               "builtin_dispatch": elf.evidence(0x684DC, 0x685F8),
               "builtin_opcode_8B": elf.evidence(0x70F9C, 0x70FAC),
               "cp932_glyph": elf.evidence(0x941C8, 0x9429C),
               "cp932_glyph_consumer": elf.evidence(0x9429C, 0x94324),
               "xft_width_caller": elf.evidence(0x93EB8, 0x93F2C),
               "integer_equals": elf.evidence(0x76960, 0x76990),
               "increment_value": elf.evidence(0x76990, 0x769F4)}
    # Pin critical raw instructions, independent of supplemental disassembly.
    pins = {0x6B984: 0x2E0200FE, 0x6B988: 0x5040000C, 0x6B990: 0x3C03000E,
            0x6B998: 0x246301B8, 0x6B9A4: 0x00800008, 0x68554: 0x2E2200F1,
            0x68568: 0x2463FD58, 0x68574: 0x00800008, 0x70F9C: 0x0C01A137,
            0x93EE4: 0x8084FFE0, 0x93EC0: 0x7C052E20}
    for va, expected in pins.items():
        if elf.u32(va) != expected:
            raise ValueError(f"Evidence pin mismatch at 0x{va:X}")
    outputs = {"elf_summary.json": summary, "imports.json": imports, "relocations.json": elf.relocation_records,
               "function_candidates.json": {"direct_calls": calls, "direct_jumps": jumps, "indirect_calls": jalrs,
                                             "indirect_jumps": jrs, "stack_prologue_candidates": prologues,
                                             "unwind_fde_ranges": unwind},
               "ksc_dispatch.json": {"source_sha256": sha(elf.blob), **vm,
                                     "guard": "byte opcode < 0xFE; FE/FF bypass indexed dispatch",
                                     "vm_function_entry_va": 0x6B924,
                                     "default_handler_va": 0x6B9B8,
                                     "default_is_not_native_port_noop": True},
               "ksc_builtins.json": {"source_sha256": sha(elf.blob), **builtin,
                                     "guard": "builtin byte < 0xF1; following byte is argument count",
                                     "caller_opcode": "0x8B", "dispatcher_function_entry_va": 0x684DC},
               "native_evidence.json": {"source_sha256": sha(elf.blob), "regions": regions,
                                        "glyph_callback_binding": {
                                            "consumer_entry_va": 0x9429C, "call_site_va": 0x942D8,
                                            "raw_hi16_instruction_va": 0x942B4, "raw_lo16_instruction_va": 0x942D4,
                                            "raw_pair_value": 0xE810, "referenced_segment_va": elf.segments[1]['va'],
                                            "resolved_callback_slot_va": 0xF2390,
                                            "callback_slot_value": elf.u32(0xF2390),
                                            "hi16_relocation": elf.relocations[elf.off(0x942B4)],
                                            "lo16_relocation": elf.relocations[elf.off(0x942D4)],
                                            "callback_relocation": elf.relocations[elf.off(0xF2390)]}}}
    outputs['xft_input_contract.json'] = {
        'source_sha256': sha(elf.blob), 'function_va': 0x941C8,
        'width_function_entry_va': 0x93EB8, 'width_read_instruction_va': 0x93EE4,
        'width_instruction_raw_hex': elf.slice(elf.off(0x93EE4), 4).hex(),
        'width_format': 'signed int8_t for font header mode byte at +0x0C < 2',
        'width_address': 'font_blob + u32(font_blob+0x10) + decoded_code - 0x20',
        'font_scale_field_offset': 0x1C, 'width_conversion': 'signed byte * float scale, truncate toward zero',
        'bounds_check': 'decoded_code >= 0x20 && decoded_code - 0x20 < glyph_count',
        'glyph_count_for_current_XFT': 8192,
        'glyph_count_evidence_owner': 'graphics task; this code task verifies the caller and mapping, not XFT dimensions',
        'accepted_byte_contract': {
            'raw_decoder': 'all byte values preserve original behavior; any lead >= 0x80 consumes two bytes',
            'read_glyph': 'non-null readable byte span of given length; at least one byte, or two if lead >= 0x80',
            'printable_ascii': '0x20..0x7E; atlas range still checked',
            'standard_SJIS_pairs': 'lead 0x81..0x9F or 0xE0..0xEF; trail 0x40..0x7E or 0x80..0xFC; atlas range additionally checked',
            'extension_policy': 'CP932 extensions and game-specific byte controls must be classified explicitly; no Unicode reinterpretation here',
            'NUL': 'decoded {0,1}; terminate text before atlas subtraction',
            'C0_controls': 'decoded lead <0x20; handle layout controls or fail, never index atlas',
            'halfwidth_kana': '0xA1..0xDF is not a supported one-byte glyph path; original consumes two and emits replacement code 0xEE',
            'invalid_input': 'read_glyph success is not encoding validity; do not use clamping or modulo to hide out-of-range glyphs'
        },
        'examples': [
            {'bytes_hex':'20', 'code':0x20, 'atlas_index':0, 'safe_for_8192':True},
            {'bytes_hex':'82a0', 'code':0x1A2, 'atlas_index':0x182, 'safe_for_8192':True},
            {'bytes_hex':'eb5e', 'code':0x201F, 'atlas_index':8191, 'safe_for_8192':True},
            {'bytes_hex':'eb5f', 'code':0x2020, 'atlas_index':8192, 'safe_for_8192':False},
            {'bytes_hex':'ec40', 'code':0x20C1, 'atlas_index':8353, 'safe_for_8192':False}
        ]
    }
    for filename, contents in outputs.items():
        (out / filename).write_text(json.dumps(contents, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if elf.u32(0xF2390) != 0x941C8:
        raise ValueError("Glyph callback target mismatch")
    # Native diagnostic metadata only, not a callable PSP address or handler
    # implementation. The native script engine must still implement semantics.
    header_lines = ['#pragma once', '#include <array>', '#include <cstdint>', '',
                    '// Generated by tools/code_elf.py from original BOOT; diagnostic addresses only.',
                    '// No opcode semantics or CPU runtime is supplied by these tables.',
                    'namespace dokodemo::recovered {']
    for variable, table in (('ksc_handler_va', vm), ('ksc_builtin_handler_va', builtin)):
        header_lines.append(f'inline constexpr std::array<std::uint32_t, {len(table["entries"])}> {variable} = {{')
        entries = table['entries']
        for start in range(0, len(entries), 8):
            header_lines.append('    ' + ', '.join(f'0x{row["handler_va"]:08X}u' for row in entries[start:start + 8]) + ',')
        header_lines.append('};')
    header_lines.extend(('} // namespace dokodemo::recovered', ''))
    (ROOT / 'src/recovered/ksc_dispatch_addresses.h').write_text('\n'.join(header_lines), encoding='utf-8')
    for filename, table in (("ksc_dispatch.tsv", vm), ("ksc_builtins.tsv", builtin)):
        fields = ("opcode_hex", "handler_va", "handler_file_offset", "table_entry_file_offset", "status")
        lines = ["\t".join(fields)]
        for entry in table["entries"]:
            lines.append("\t".join(f"0x{entry[k]:08X}" if isinstance(entry[k], int) else entry[k] for k in fields))
        (out / filename).write_text("\n".join(lines) + "\n", encoding="utf-8")
    lines = []
    for name, evidence in regions.items():
        lines.append(f"\n{name}: VA {evidence['start_va']:08X}..{evidence['end_va_exclusive']:08X} SHA256 {evidence['sha256']}")
        for i in evidence["instructions"]:
            lines.append(f"VA={i['va']:08X} FILE={i['file_offset']:08X} {i['bytes']} {i['instruction']}")
    (out / "native_evidence.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps({"sha256": summary["source_sha256"], "imports": summary["imports"]["function_count"],
                      "function_census": summary["function_census"], "vm_handlers": vm["distinct_handler_count"],
                      "builtin_handlers": builtin["distinct_handler_count"]}))
    return summary


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot", type=Path, default=ROOT / "research/code/input/BOOT.BIN")
    parser.add_argument("--output", type=Path, default=ROOT / "research/code")
    parser.add_argument("--disassemble", nargs=2, metavar=("START_VA", "END_VA"))
    args = parser.parse_args()
    elf = ELF(args.boot)
    if args.disassemble:
        evidence = elf.evidence(*(int(value, 0) for value in args.disassemble))
        for i in evidence["instructions"]:
            print(f"{i['va']:08X} {i['bytes']} {i['instruction']}")
    else:
        analyze(elf, args.output)


if __name__ == "__main__":
    main()
