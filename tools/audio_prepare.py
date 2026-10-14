"""Autonomous original-ISO audio extraction, inspection, and offline decoding.

No PSP runtime or neighbouring project is used. SGXD fields are verified against
the local file and cross-checked with the published vgmstream SGXD parser.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import wave
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "assets/native/audio"
RESEARCH = ROOT / "research/audio"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def u32(data, at):
    return struct.unpack_from("<I", data, at)[0]


def s32(data, at):
    return struct.unpack_from("<i", data, at)[0]


def string(data, at):
    if not 0 <= at < len(data):
        raise ValueError(f"String offset outside file: {at:#x}")
    end = data.find(b"\0", at)
    if end < 0:
        raise ValueError("Unterminated string")
    return data[at:end].decode("ascii")


def iso_files(data):
    pvd = 16 * 2048
    if data[pvd + 1:pvd + 6] != b"CD001":
        raise ValueError("Missing ISO9660 descriptor")

    def walk(prefix, base, size):
        pos = 0
        while pos < size:
            at = base + pos
            length = data[at]
            if not length:
                pos = (pos // 2048 + 1) * 2048
                continue
            if length < 34 or pos + length > size:
                raise ValueError("Invalid ISO directory record")
            extent, amount = u32(data, at + 2), u32(data, at + 10)
            if extent != struct.unpack_from(">I", data, at + 6)[0] or amount != struct.unpack_from(">I", data, at + 14)[0]:
                raise ValueError("ISO endian fields disagree")
            name = data[at + 33:at + 33 + data[at + 32]]
            pos += length
            if name in (b"\0", b"\1"):
                continue
            path = (prefix + "/" + name.decode("ascii").split(";")[0]).lstrip("/")
            if extent * 2048 + amount > len(data):
                raise ValueError("ISO file outside image")
            if data[at + 25] & 2:
                yield from walk(path, extent * 2048, amount)
            else:
                yield {"path": path, "iso_offset": extent * 2048, "size": amount}

    yield from walk("", u32(data, pvd + 158) * 2048, u32(data, pvd + 166))


def bp_members(data, base, size):
    count, slots, names_size, header = struct.unpack_from("<4I", data, base)
    names = 16 + 16 * slots
    if not 0 < count <= slots <= 1_000_000 or not names + names_size <= header <= size:
        raise ValueError("Invalid BP header")
    rows = []
    for index in range(slots):
        name_off, offset, length, link = struct.unpack_from("<4I", data, base + 16 + index * 16)
        if name_off == 0xFFFFFFFF:
            continue
        if name_off >= names_size or header + offset + length > size:
            raise ValueError("BP member outside archive")
        end = data.find(b"\0", base + names + name_off, base + names + names_size)
        if end < 0:
            raise ValueError("BP name has no terminator")
        name = data[base + names + name_off:end].decode("ascii").replace("\\", "/")
        rows.append({"path": name, "member_index": index, "offset_in_package": header + offset,
                     "iso_offset": base + header + offset, "size": length})
    if len(rows) != count:
        raise ValueError("BP active count mismatch")
    return rows


def inspect_sgd(data):
    if data[:4] != b"SGXD":
        raise ValueError("Expected SGXD")
    bank_name_offset, body, body_flags = struct.unpack_from("<3I", data, 4)
    if body > len(data) or body < 16 or (body_flags & 0x7FFFFFFF) != len(data) - body:
        raise ValueError("SGXD body size mismatch")
    result = {"bank_name": string(data, bank_name_offset), "body_offset": body,
              "body_size": len(data) - body, "chunks": [], "waves": [], "names": [],
              "regions": [], "sequences": []}
    at = 16
    while at < body:
        tag = data[at:at + 4].decode("ascii")
        length = u32(data, at + 4)
        if at + 8 + length > body or length < 8:
            raise ValueError("Invalid SGXD chunk")
        count = u32(data, at + 12)
        result["chunks"].append({"tag": tag, "offset": at, "payload_size": length, "count": count})
        if tag == "WAVE":
            if 8 + count * 56 > length:
                raise ValueError("WAVE table outside chunk")
            for index in range(count):
                w = at + 16 + index * 56
                offset = body + u32(data, w + 48)
                size = u32(data, w + 44)
                if offset + size > len(data):
                    raise ValueError("WAVE data outside file")
                name_off = u32(data, w + 4)
                result["waves"].append({"index": index, "record_offset": w,
                    "name": string(data, name_off) if name_off else "", "name_offset": name_off,
                    "codec": data[w + 8], "channels": data[w + 9], "sample_rate": u32(data, w + 12),
                    "info_type": u32(data, w + 16), "info_value": u32(data, w + 20),
                    "sample_count": s32(data, w + 32), "loop_start": s32(data, w + 36),
                    "loop_end": s32(data, w + 40), "data_offset": offset, "data_size": size,
                    "body_end_offset": u32(data, w + 52), "data_sha256": sha(data[offset:offset + size]),
                    "data_magic_hex": data[offset:offset + 16].hex()})
        elif tag == "NAME":
            if 8 + count * 8 > length:
                raise ValueError("NAME table outside chunk")
            for index in range(count):
                n = at + 16 + 8 * index
                ident, kind, off = struct.unpack_from("<HHI", data, n)
                result["names"].append({"index": index, "record_offset": n, "id": ident,
                                        "kind": kind, "request_type": kind >> 12, "sequence_group": kind & 255,
                                        "string_offset": off, "name": string(data, off)})
        elif tag == "RGND":
            for group in range(count):
                total, offset = struct.unpack_from("<II", data, at + 16 + group * 8)
                if not at + 16 <= offset < at + 8 + length or offset + total * 56 > at + 8 + length:
                    raise ValueError("Region table outside RGND")
                for index in range(total):
                    r = offset + index * 56
                    result["regions"].append({"group": group, "index": index, "record_offset": r,
                        "flags": u32(data, r), "name_offset": u32(data, r + 4),
                        "note_min": data[r + 24], "note_max": data[r + 25], "base_note": data[r + 28],
                        "fine_tuning": struct.unpack_from("<b", data, r + 29)[0],
                        "pitch": struct.unpack_from("<h", data, r + 30)[0],
                        "volume_l": struct.unpack_from("<h", data, r + 32)[0],
                        "volume_r": struct.unpack_from("<h", data, r + 34)[0],
                        "dry": struct.unpack_from("<h", data, r + 36)[0],
                        "wet": struct.unpack_from("<h", data, r + 38)[0],
                        "envelope_0": u32(data, r + 40), "envelope_1": u32(data, r + 44),
                        "wave_index": s32(data, r + 52)})
        elif tag == "SEQD":
            for group in range(count):
                table = u32(data, at + 16 + group * 4)
                if not table:
                    continue
                entries = u32(data, table + 4)
                for index in range(entries):
                    seq = u32(data, table + 8 + 4 * index)
                    if not seq:
                        continue
                    flags, name, form, resolution, vl, vr, size = struct.unpack_from("<IIhhhhI", data, seq)
                    if not at + 16 <= seq or seq + 20 + size > at + 8 + length:
                        raise ValueError("Sequence data outside SEQD")
                    commands = data[seq + 20:seq + 20 + size]
                    result["sequences"].append({"group": group, "index": index, "record_offset": seq,
                        "flags": flags, "name_offset": name, "format": form, "resolution": resolution,
                        "volume_l": vl, "volume_r": vr, "data_offset": seq + 20, "data_size": size,
                        "data_sha256": sha(commands), "data_hex": commands.hex(),
                        "disassembly": inspect_sesq(commands, seq + 20) if form == 0 else {"status": "unsupported_format"}})
        at += 8 + length
    if at != body:
        raise ValueError("SGXD chunks do not end at body")
    return result


# A structural disassembler, deliberately not a synthesizer. Missing operands
# retain their default marker. Unknown opcodes/operand types stop explicitly.
SESQ = {0xB0: ("sub_play", 4), 0xB1: ("sub_stop", 1), 0xB2: ("sub_stop_release", 1),
    0xB3: ("sub_status", 1), 0xC0: ("control", 6), 0xC1: ("adsr", 3), 0xC2: ("bend", 4),
    0xD0: ("play_region_note", 4), 0xD1: ("stop", 2), 0xD2: ("stop_release", 2),
    0xD3: ("port_status", 2), 0xD4: ("play_wave", 7), 0xD5: ("play_noise", 7),
    0xF0: ("return_zero", 0), 0xF1: ("wait", 1), 0xF2: ("jump", 1),
    0xF3: ("loop_begin", 1), 0xF4: ("loop_end", 0), 0xF5: ("jump_ne", 3),
    0xF6: ("jump_3", 3), 0xF7: ("jump_ge", 3), 0xF8: ("jump_lt", 3),
    0xF9: ("marker", 1), 0xFA: ("loop_break", 2), 0xFE: ("print", 1), 0xFF: ("end", 0)}


def inspect_sesq(data, base):
    pos = 0
    rows = []

    def value():
        nonlocal pos
        if pos == len(data) or data[pos] >= 0xA0:
            return {"kind": "default"}
        header = data[pos]
        pos += 1
        kind, size = header >> 4, header & 15
        if kind == 2:
            return {"kind": "random", "base": value(), "min": value(), "max": value()}
        if kind not in (0, 1, 3, 4) or pos + size > len(data):
            raise ValueError(f"Unsupported SESQ value {header:#x} at {base + pos - 1:#x}")
        digits = "".join(f"{byte:02x}" for byte in data[pos:pos + size])
        if any(char not in "0123456789" for char in digits):
            raise ValueError("Invalid SESQ BCD value")
        pos += size
        val = int(digits or "0")
        return {"kind": ("literal", "negative", "", "system_register", "register")[kind],
                "value": -val if kind == 1 else val}

    try:
        while pos < len(data):
            start = pos
            op = data[pos]
            pos += 1
            if op not in SESQ:
                raise ValueError(f"Unsupported SESQ opcode {op:#x} at {base + start:#x}")
            name, count = SESQ[op]
            args = [value() for _ in range(count)]
            rows.append({"offset": base + start, "opcode": op, "operation": name, "operands": args})
        return {"status": "parsed", "commands": rows}
    except ValueError as error:
        return {"status": "blocked", "error": str(error), "commands": rows, "stopped_offset": base + pos}


def run(command):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {command}\n{result.stderr}")
    return result


def probe(path, ffprobe):
    return json.loads(run([ffprobe, "-v", "error", "-show_streams", "-show_format", "-of", "json", path]).stdout)


def pcm_info(path):
    with wave.open(str(path), "rb") as wav:
        channels, width, rate, frames = wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getnframes()
        pcm = wav.readframes(frames)
    values = struct.iter_unpack("<h", pcm)
    peak = max((abs(v[0]) for v in values), default=0)
    if width != 2 or channels not in (1, 2) or not frames:
        raise ValueError(f"Unexpected PCM WAV format: {path}")
    return {"path": path.relative_to(ROOT).as_posix(), "sha256": sha(path.read_bytes()),
            "pcm_sha256": sha(pcm), "channels": channels, "bits_per_sample": width * 8,
            "sample_rate": rate, "frames": frames, "duration_seconds": frames / rate,
            "peak_absolute": peak}


def decode_assets(assets, ffmpeg, ffprobe, convert_video, decode_samples):
    clips, videos, samples = [], [], []
    for asset in assets:
        local = ROOT / asset["local_path"]
        sgd = asset.get("sgxd")
        stem = Path(asset["path"]).stem
        if sgd:
            for wav in sgd["waves"]:
                if wav["codec"] == 3 and decode_samples:
                    if wav["channels"] != 1 or wav["data_size"] % 16:
                        raise ValueError("Only mono aligned PS-ADPCM samples have been verified")
                    payload = local.read_bytes()[wav["data_offset"]:wav["data_offset"] + wav["data_size"]]
                    name = f"{stem}_{wav['index']:03d}"
                    source = OUT / "payload/samples" / (name + ".vag")
                    source.parent.mkdir(parents=True, exist_ok=True)
                    header = b"VAGp" + struct.pack(">III", 0x20, 0, len(payload)) + struct.pack(">I", wav["sample_rate"]) + bytes(12) + name.encode("ascii")[:16].ljust(16, b"\0")
                    source.write_bytes(header + payload)
                    output = OUT / "pcm/samples" / (name + ".wav")
                    output.parent.mkdir(parents=True, exist_ok=True)
                    command = [ffmpeg, "-nostdin", "-y", "-v", "error", "-i", source, "-map", "0:a:0",
                        "-c:a", "pcm_s16le", "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact", output]
                    log = run(command)
                    info = pcm_info(output)
                    storage_frames = len(payload) // 16 * 28
                    if info["frames"] != storage_frames:
                        raise ValueError(f"PS-ADPCM storage/PCM frames mismatch: {name}")
                    samples.append({"id": name, "role": "instrument_sample_not_sequence_render",
                        "source_path": asset["path"], "source_iso_offset": asset["iso_offset"], "source_sha256": asset["sha256"],
                        "wave_index": wav["index"], "wave_record_offset": wav["record_offset"],
                        "payload_offset": wav["data_offset"], "payload_sha256": wav["data_sha256"],
                        "vag_wrapper_size": 48, "vag_wrapper_sha256": sha(header),
                        "sgxd_sample_count_field": wav["sample_count"], "sgxd_loop_start_field": wav["loop_start"],
                        "sgxd_loop_end_field": wav["loop_end"], "storage_frames": storage_frames,
                        "sgxd_length_matches_storage": wav["sample_count"] == storage_frames,
                        "command": [str(p) for p in command], "stderr": log.stderr, **info})
                    continue
                if wav["codec"] != 4:
                    # PS-ADPCM samples remain indexed; their SGXD sample counts
                    # disagree with storage lengths and need engine analysis.
                    continue
                payload = local.read_bytes()[wav["data_offset"]:wav["data_offset"] + wav["data_size"]]
                source = OUT / "payload" / (stem + ".at3")
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_bytes(payload)
                output = OUT / "pcm" / (stem + ".wav")
                output.parent.mkdir(parents=True, exist_ok=True)
                # ATRAC3 fact[1] = encoder delay. FFmpeg's WAV demuxer does not
                # expose this delay as initial_padding for these files.
                fact = riff_chunks(payload).get("fact", b"")
                delay = u32(fact, 4) if len(fact) >= 8 else 0
                frames = wav["sample_count"]
                if len(fact) < 8 or u32(fact, 0) != frames:
                    raise ValueError(f"RIFF fact/SGXD frame count mismatch: {stem}")
                smpl = riff_chunks(payload).get("smpl", b"")
                if wav["loop_start"] >= 0 and wav["loop_end"] >= 0:
                    if len(smpl) < 60 or u32(smpl, 28) != 1 or u32(smpl, 44) != wav["loop_start"] + delay or u32(smpl, 48) + 1 != wav["loop_end"] + delay:
                        raise ValueError(f"RIFF smpl/SGXD loop mismatch: {stem}")
                command = [ffmpeg, "-nostdin", "-y", "-v", "error", "-i", source,
                    "-map", "0:a:0", "-af", f"atrim=start_sample={delay}:end_sample={delay + frames},asetpts=PTS-STARTPTS",
                    "-c:a", "pcm_s16le", "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact", output]
                log = run(command)
                info = pcm_info(output)
                if info["frames"] != frames or info["sample_rate"] != wav["sample_rate"]:
                    raise ValueError(f"SGXD/PCM frame count mismatch: {stem}")
                clips.append({"id": stem, "role": "music_stream", "source_path": asset["path"],
                    "source_iso_offset": asset["iso_offset"], "source_sha256": asset["sha256"],
                    "source_wave_index": wav["index"], "wave_record_offset": wav["record_offset"],
                    "payload_offset": wav["data_offset"], "payload_sha256": wav["data_sha256"],
                    "encoder_delay_frames": delay, "loop_start": wav["loop_start"], "loop_end": wav["loop_end"],
                    "command": [str(p) for p in command], "stderr": log.stderr, "source_probe": probe(source, ffprobe), **info})
        elif local.suffix.upper() == ".AT3":
            # The two AT3 icon sounds are byte-identical. Give each provenance
            # record its own unique ID; emit only one PCM file per source hash.
            prior = next((c for c in clips if c["source_sha256"] == asset["sha256"]), None)
            if prior:
                clips.append({**prior, "id": "savedata_SND0", "source_path": asset["path"],
                    "source_iso_offset": asset["iso_offset"], "alias_of": prior["id"]})
                continue
            output = OUT / "pcm" / "SND0.wav"
            output.parent.mkdir(parents=True, exist_ok=True)
            payload = local.read_bytes()
            fact = riff_chunks(payload).get("fact", b"")
            frames, delay = (u32(fact, 0), u32(fact, 4)) if len(fact) >= 8 else (0, 0)
            command = [ffmpeg, "-nostdin", "-y", "-v", "error", "-i", local, "-map", "0:a:0",
                "-af", f"atrim=start_sample={delay}:end_sample={delay + frames},asetpts=PTS-STARTPTS",
                "-c:a", "pcm_s16le", "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact", output]
            log = run(command)
            clips.append({"id": "SND0", "role": "psp_icon_audio", "source_path": asset["path"],
                "source_iso_offset": asset["iso_offset"], "source_sha256": asset["sha256"],
                "encoder_delay_frames": delay, "loop_start": -1, "loop_end": -1,
                "command": [str(p) for p in command], "stderr": log.stderr, "source_probe": probe(local, ffprobe), **pcm_info(output)})
        elif local.suffix.upper() == ".PMF":
            info = probe(local, ffprobe)
            item = {"source_path": asset["path"], "source_sha256": asset["sha256"],
                "source_iso_offset": asset["iso_offset"], "source_probe": info,
                "audio_stream_count": sum(s.get("codec_type") == "audio" for s in info["streams"])}
            if convert_video:
                output = OUT / "video" / ("savedata_icon.mp4" if "::" in asset["path"] else "disc_icon.mp4")
                output.parent.mkdir(parents=True, exist_ok=True)
                command = [ffmpeg, "-nostdin", "-y", "-v", "error", "-i", local, "-map", "0:v:0",
                    "-an", "-c:v", "copy", "-map_metadata", "-1", "-movflags", "+faststart", output]
                log = run(command)
                # Decode all remuxed frames to validate the payload, not merely
                # container metadata; framehash preserves audit evidence.
                verify = [ffmpeg, "-nostdin", "-v", "error", "-i", output, "-map", "0:v:0", "-f", "framehash", "-"]
                hashes = run(verify).stdout
                (RESEARCH / (output.stem + ".framehash.txt")).write_text(hashes, encoding="utf-8")
                original_verify = [ffmpeg, "-nostdin", "-v", "error", "-i", local, "-map", "0:v:0", "-f", "framehash", "-"]
                original_hashes = run(original_verify).stdout
                (RESEARCH / (output.stem + ".original.framehash.txt")).write_text(original_hashes, encoding="utf-8")
                frame_digests = lambda text: [line.split(",")[-1].strip() for line in text.splitlines() if line and not line.startswith("#")]
                if frame_digests(hashes) != frame_digests(original_hashes):
                    raise ValueError("Remux changed decoded PMF frames")
                item.update({"path": output.relative_to(ROOT).as_posix(), "sha256": sha(output.read_bytes()),
                    "command": [str(p) for p in command], "stderr": log.stderr, "decode_check": [str(p) for p in verify],
                    "original_decode_check": [str(p) for p in original_verify], "decoded_frames_match_original": True,
                    "decoded_frames": sum(bool(line) and not line.startswith("#") for line in hashes.splitlines()),
                    "output_probe": probe(output, ffprobe)})
            videos.append(item)
    return clips, videos, samples


def riff_chunks(data):
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("Expected RIFF WAVE payload")
    result = {}
    at = 12
    while at + 8 <= len(data):
        size = u32(data, at + 4)
        if at + 8 + size > len(data):
            raise ValueError("RIFF chunk outside file")
        result[data[at:at + 4].decode("ascii")] = data[at + 8:at + 8 + size]
        at += 8 + size + (size & 1)
    return result


def scenario_references(data, candidates, assets):
    labels = sorted({s["sgxd"]["bank_name"] for s in assets if "sgxd" in s}, key=len, reverse=True)
    pattern = re.compile(rb"(?<![A-Za-z0-9_])(" + b"|".join(re.escape(x.encode("ascii")) for x in labels) + rb")(?![A-Za-z0-9_])")
    refs = []
    for row in candidates:
        if not row["path"].upper().endswith((".KSC", "BOOT.BIN", "EBOOT.BIN")):
            continue
        blob = data[row["iso_offset"]:row["iso_offset"] + row["size"]]
        hits = list(pattern.finditer(blob))
        if hits:
            for hit in hits:
                refs.append({"source_path": row["path"], "source_sha256": sha(blob),
                    "source_iso_offset": row["iso_offset"], "offset": hit.start(),
                    "iso_offset": row["iso_offset"] + hit.start(), "bank_name": hit[1].decode("ascii"),
                    "context_hex": blob[max(0, hit.start() - 16):min(len(blob), hit.end() + 24)].hex(),
                    "status": "literal_reference_only_not_validated_call"})
    return refs


def sequence_events(assets):
    result = []
    for asset in assets:
        sgd = asset.get("sgxd", {})
        for seq in sgd.get("sequences", []):
            links = []
            for cmd in seq["disassembly"].get("commands", []):
                if cmd["operation"] != "play_region_note":
                    continue
                program, note = cmd["operands"][1:3]
                if program["kind"] == "default":
                    program = {"kind": "literal", "value": 0}
                if program["kind"] != "literal" or note["kind"] != "literal":
                    links.append({"status": "dynamic_lookup_unresolved", "command_offset": cmd["offset"]})
                    continue
                matches = [r for r in sgd["regions"] if r["group"] == program["value"] and r["note_min"] <= note["value"] <= r["note_max"]]
                links.append({"status": "static_region_match_not_runtime_validated", "command_offset": cmd["offset"],
                    "program": program["value"], "note": note["value"],
                    "regions": [{"index": r["index"], "record_offset": r["record_offset"],
                        "base_note": r["base_note"], "flags": r["flags"], "wave_index": r["wave_index"],
                        "wave_record_offset": sgd["waves"][r["wave_index"]]["record_offset"],
                        "sample_data_sha256": sgd["waves"][r["wave_index"]]["data_sha256"]} for r in matches]})
            result.append({"bank": sgd["bank_name"], "source_path": asset["path"], "source_sha256": asset["sha256"],
                "source_iso_offset": asset["iso_offset"], "sequence_group": seq["group"], "sequence_index": seq["index"],
                "sequence_record_offset": seq["record_offset"], "sequence_data_sha256": seq["data_sha256"],
                "format": seq["format"], "resolution": seq["resolution"], "region_links": links,
                "status": "indexed_not_synthesized", "script_call_link": "unresolved"})
    return result


def write_catalog(clips):
    lines = ["// Generated by tools/audio_prepare.py from the verified original ISO.",
        "#pragma once", "#include <cstdint>", "#include <string_view>", "namespace dokodemo::audio {",
        "struct CatalogClip { const char* id; const wchar_t* path; std::uint64_t frames; std::uint32_t rate; std::uint16_t channels; std::int64_t loop_begin; std::int64_t loop_end; };",
        "inline constexpr CatalogClip kOriginalClips[] = {"]
    for clip in clips:
        lines.append('    {"%s", L"%s", %dULL, %dU, %dU, %dLL, %dLL},' % (clip["id"], clip["path"], clip["frames"], clip["sample_rate"], clip["channels"], clip["loop_start"], clip["loop_end"]))
    lines.extend(["};", "inline const CatalogClip* FindOriginalClip(std::string_view id) noexcept {",
        "    for (const auto& clip : kOriginalClips) if (id == clip.id) return &clip;",
        "    return nullptr;", "}", "} // namespace dokodemo::audio", ""])
    (ROOT / "src/audio_catalog.h").write_text("\n".join(lines), encoding="utf-8")


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inspect-only", action="store_true")
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    parser.add_argument("--ffprobe", default=shutil.which("ffprobe"))
    parser.add_argument("--convert-video", action="store_true")
    parser.add_argument("--decode-samples", action="store_true", help="Decode raw PS-ADPCM instrument samples; does not synthesize sequences")
    args = parser.parse_args()
    spec = json.loads((ROOT / "source-iso.json").read_text(encoding="utf-8-sig"))
    iso = ROOT / spec["relative_path"]
    data = iso.read_bytes()
    if len(data) != spec["size_bytes"] or sha(data) != spec["sha256"]:
        raise ValueError("Original ISO hash/size does not match source-iso.json")
    files = list(iso_files(data))
    candidates = list(files)
    for row in files:
        if row["path"].upper().endswith("DATA.BP"):
            for package in bp_members(data, row["iso_offset"], row["size"]):
                for member in bp_members(data, package["iso_offset"], package["size"]):
                    candidates.append({**member, "package": package["path"],
                        "package_iso_offset": package["iso_offset"], "path": package["path"] + "::" + member["path"]})
    assets = []
    for row in candidates:
        if Path(row["path"]).suffix.upper() not in (".SGD", ".AT3", ".PMF"):
            continue
        payload = data[row["iso_offset"]:row["iso_offset"] + row["size"]]
        local = OUT / "source" / row["path"].replace("::", "/")
        local.parent.mkdir(parents=True, exist_ok=True)
        local.write_bytes(payload)
        item = {**row, "sha256": sha(payload), "local_path": local.relative_to(ROOT).as_posix()}
        if payload[:4] == b"SGXD":
            item["sgxd"] = inspect_sgd(payload)
        assets.append(item)
    index = {"iso_path": spec["relative_path"], "iso_sha256": spec["sha256"],
        "parser_reference": "https://github.com/vgmstream/vgmstream/blob/master/src/meta/sgxd.c",
        "sequence_reference": "https://github.com/Nenkai/010GameTemplates/blob/main/Sony/SGXD.bt",
        "assets": assets, "candidate_count": len(candidates)}
    write_json(RESEARCH / "index.json", index)
    refs = scenario_references(data, candidates, assets)
    write_json(RESEARCH / "scenario_references.json", refs)
    events = sequence_events(assets)
    write_json(OUT / "sequence_events.json", events)
    summary = {"audio_assets": len(assets), "sgxd_banks": sum("sgxd" in a for a in assets),
        "sequence_count": sum(len(a.get("sgxd", {}).get("sequences", [])) for a in assets),
        "sequence_status": dict(Counter(s["disassembly"]["status"] for a in assets for s in a.get("sgxd", {}).get("sequences", []))),
        "literal_references": len(refs)}
    if not args.inspect_only:
        if not args.ffmpeg or not args.ffprobe:
            raise ValueError("Offline preparation requires ffmpeg and ffprobe; runtime does not")
        clips, videos, samples = decode_assets(assets, args.ffmpeg, args.ffprobe, args.convert_video, args.decode_samples)
        manifest = {"schema_version": 1, "iso_sha256": spec["sha256"],
            "ffmpeg_version": run([args.ffmpeg, "-version"]).stdout,
            "ffmpeg_binary_sha256": sha(Path(args.ffmpeg).read_bytes()),
            "ffprobe_version": run([args.ffprobe, "-version"]).stdout,
            "ffprobe_binary_sha256": sha(Path(args.ffprobe).read_bytes()),
            "clips": clips, "videos": videos, "instrument_samples": samples,
            "sequence_synthesis_status": "not_implemented", "sequence_event_execution_status": "not_implemented"}
        write_json(OUT / "manifest.json", manifest)
        write_catalog(clips)
        summary.update({"pcm_clips": len(clips), "unique_pcm_files": len({c["path"] for c in clips}),
                        "decoded_instrument_samples": len(samples),
                        "pmf_videos": len(videos), "pcm_seconds": sum(c["duration_seconds"] for c in clips if "alias_of" not in c)})
    write_json(RESEARCH / "summary.json", summary)
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
