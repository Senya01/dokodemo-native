"""Inventory BOOT dispatch slots and native Windows compilation units."""
from hashlib import sha256
import json
from pathlib import Path
import re
import struct


ROOM = "src/gameplay/room_talk_state.cpp"
BOOT_SHA256 = "2b361beb4c55e08af2828713861429e768a1140f284a11f606fba5f580cd483c"
# Original BOOT v1.01 dispatch tables. The offsets, hashes and default
# targets are recorded in research/code/ksc_dispatch.json and ksc_builtins.json.
TABLES = (
    ("KSC opcodes", 918072, 254, 0x6B9B8,
     "90ec2f37500d83a9429e0f04d733fc2ef89064d7cd57226e30b3e043f10008ec"),
    ("KSC built-ins", 916952, 241, 0x685C4,
     "e1ac97815745b2860ff91e4d88bc992a9d730a4d90583e8fa305afbb87d8d931"),
)
CPP_TEXT = re.compile(r"""//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'""", re.DOTALL)
NUMBER = r"(0[xX][0-9a-fA-F]+|[0-9]+)"


def room_handlers(root):
    source = (root / ROOM).read_text(encoding="utf-8")
    source = CPP_TEXT.sub(lambda m: re.sub(r"[^\n]", " ", m[0]), source)
    start = source.index("void RoomTalkState::Execute(")
    source = source[:source.index("void RoomTalkState::FinishWordEntry(", start)]
    switch = re.search(r"\bswitch\s*\(op\)\s*(?=\{)", source[start:])
    if switch is None:
        raise ValueError("Room opcode dispatcher not found")
    begin = start + switch.end()
    depth = 0
    cases = {}
    builtin_chunk = None
    previous = None
    for match in re.finditer(r"[{}]|\bcase\s+" + NUMBER + r"\s*:|\bdefault\s*:", source[begin:]):
        token = match[0]
        if token == "{":
            depth += 1
        elif token == "}":
            depth -= 1
            if depth == 0:
                break
        elif depth == 1:
            if previous == 0x8B:
                builtin_chunk = (cases[previous][1], begin + match.start())
            previous = int(match[1], 0) if match[1] else None
            if previous is not None:
                if previous in cases:
                    raise ValueError(f"Duplicate room opcode {previous:#x}")
                cases[previous] = (source.count("\n", 0, begin + match.start()) + 1, begin + match.end())
    if depth != 0 or not builtin_chunk:
        raise ValueError("Room dispatcher or builtin case lacks a closing boundary")

    lo, hi = builtin_chunk
    builtin_lines = {}
    game_lines = {}
    for pattern, index in (
        (r"\bif\s*\(\s*id\s*==\s*" + NUMBER, builtin_lines),
        (r"\bservice\.data\s*==\s*" + NUMBER, game_lines),
    ):
        for match in re.finditer(pattern, source[lo:hi]):
            index.setdefault(int(match[1], 0), set()).add(1 + source.count("\n", 0, lo + match.start()))
    return (
        {index: [line] for index, (line, _) in cases.items()},
        {index: sorted(lines) for index, lines in builtin_lines.items()},
        {index: sorted(lines) for index, lines in game_lines.items()},
    )


def dispatch_group(boot, spec, native):
    title, offset, count, default, digest = spec
    raw = boot[offset:offset + count * 4]
    if sha256(raw).hexdigest() != digest:
        raise ValueError(f"Unexpected BOOT table at file offset {offset:#x}")
    entries = dict(enumerate(struct.unpack(f"<{count}I", raw)))
    active = {key: dest for key, dest in entries.items() if dest != default}
    if native.keys() - active.keys():
        raise ValueError(f"Native IDs outside original active slots: {sorted(native.keys() - active.keys())}")
    items = [
        {"label": f"0x{key:02X}", "target": f"0x{dest:08X}", "source": ROOM, "lines": native.get(key, [])}
        for key, dest in active.items()
    ]
    return {"title": title, "kind": "dispatch", "items": items, "slots": count, "fallback": count - len(active)}


def compilation_groups(root):
    manifests = (("dokodemo-core", "src/core-sources.txt"), ("dokodemo-win32", "src/windows-sources.txt"))
    groups = []
    seen = set()
    for name, path in manifests:
        paths = (root / path).read_text(encoding="utf-8").split()
        families = {}
        for entry in paths:
            source = Path(entry)
            if not source.is_relative_to("src") or source.suffix != ".cpp" or not (root / source).is_file():
                raise ValueError(f"{path}: invalid source {entry}")
            if entry in seen:
                raise ValueError(f"Source appears twice in game targets: {entry}")
            seen.add(entry)
            family = source.parts[1] if len(source.parts) > 2 else "src"
            families.setdefault(family, []).append({"label": source.name, "source": entry})
        groups.extend({"title": f"{name} / {family}", "kind": "files", "items": items}
                      for family, items in sorted(families.items()))
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    executable = re.search(r"add_executable\(dokodemo-game\s+WIN32\s+([^)]*)\)", cmake)
    if not executable or not executable[1].split():
        raise ValueError("Expected dokodemo-game Windows sources")
    items = []
    for entry in executable[1].split():
        source = Path(entry)
        if entry in seen or not source.is_relative_to("src") or source.suffix != ".cpp" or not (root / source).is_file():
            raise ValueError(f"Invalid or duplicate dokodemo-game source: {entry}")
        seen.add(entry)
        items.append({"label": source.name, "source": entry})
    groups.append({"title": "dokodemo-game", "kind": "files",
                   "items": items})
    return groups


def collect_code(root: Path, boot_path: Path | None = None):
    opcodes, builtins, game = room_handlers(root)
    groups = []
    boot_path = boot_path or root / "assets/iso/PSP_GAME/SYSDIR/BOOT.BIN"
    if boot_path.is_file():
        boot = boot_path.read_bytes()
        if sha256(boot).hexdigest() != BOOT_SHA256:
            raise ValueError("Code coverage expects original BOOT.BIN v1.01")
        groups.extend(dispatch_group(boot, spec, native) for spec, native in zip(TABLES, (opcodes, builtins)))
    else:
        saved = json.loads((root / "docs/native-dispatch-ids.json").read_bytes())["groups"]
        for snapshot, spec, native in zip(saved, TABLES, (opcodes, builtins), strict=True):
            title, _, slots, _, _ = spec
            ids = [int(label, 16) for label in snapshot["ids"]]
            if snapshot["title"] != title or snapshot["slots"] != slots or ids != sorted(set(ids)):
                raise ValueError(f"Invalid recorded dispatch inventory for {title}")
            if any(not 0 <= key < slots for key in ids) or native.keys() - set(ids):
                raise ValueError(f"Room dispatcher IDs differ from the recorded {title} table")
            items = [{"label": f"0x{key:02X}", "source": ROOM, "lines": native.get(key, [])} for key in ids]
            groups.append({"title": title, "kind": "dispatch", "items": items,
                           "slots": slots, "fallback": slots - len(ids)})
    groups.append({
        "title": "Room GAME IDs", "kind": "selectors",
        "items": [{"label": f"GAME{key}", "source": ROOM, "lines": lines} for key, lines in sorted(game.items())],
    })
    return {"groups": groups + compilation_groups(root)}
