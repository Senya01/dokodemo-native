"""Prepare the one-note SE_SYS sequences used by the title and menus."""
import io
from array import array
from pathlib import Path
import sys
import wave

from audio_prepare import inspect_sgd
from iso_reader import Region, require


def decode_one_shot(data):
    """Decode mono PS ADPCM, including the last sounding block, before padding."""
    filters = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))
    previous = older = 0
    pcm = array("h")
    require(len(data) % 16 == 0, "Truncated SE ADPCM")
    for offset in range(0, len(data), 16):
        header, flag = data[offset:offset + 2]
        shift, predictor = header & 15, header >> 4
        require(shift <= 12 and predictor < len(filters) and flag in (0, 1, 4),
                "Unsupported ADPCM block in one-shot SE")
        a, b = filters[predictor]
        for byte in data[offset + 2:offset + 16]:
            for nibble in (byte & 15, byte >> 4):
                signed = (nibble ^ 8) - 8
                sample = (signed << (12 - shift)) + ((a * previous + b * older + 32) >> 6)
                older, previous = previous, max(-32768, min(32767, sample))
                pcm.append(previous)
        if flag == 1:
            return pcm
    raise ValueError("SE ADPCM lacks an end block")


def one_note(data, info, sequence, controller_level):
    index = sequence["index"]
    commands = sequence["disassembly"]["commands"]
    require([c["operation"] for c in commands] == ["play_region_note", "control", "control", "end"],
            f"Unexpected one-note sequence {index}")
    port, program, note, velocity = [op["value"] for op in commands[0]["operands"]]
    controls = [[op.get("value") for op in c["operands"]] for c in commands[1:3]]
    require(controls == [[port, 0, controller_level, None, None, None], [port, 1, 0, None, None, None]],
            f"Unsupported one-note controls in sequence {index}")
    region, = [r for r in info["regions"] if r["group"] == program and r["note_min"] <= note <= r["note_max"]]
    voice = info["waves"][region["wave_index"]]
    require(voice["codec"] == 3 and voice["channels"] == 1 and voice["loop_start"] == -1 and
            region["pitch"] == 4096 and velocity == 4096, f"Unsupported one-note voice {index}")
    pcm = decode_one_shot(data[voice["data_offset"]:][:voice["data_size"]])
    rate = round(voice["sample_rate"] * 2 ** ((note - region["base_note"] - region["fine_tuning"] / 128) / 12))
    return pcm, rate, region["volume_l"] * controller_level // 4096


def title_effects(bank):
    require(bank.sha256() == "06b0f1fd832a7ab28ed4ffec722f7174734ed8be5a15363e5f4478c3a2ebf020",
            "Unexpected SE_SYS bank")
    data = bank.read(0, bank.size)
    info = inspect_sgd(data)
    files = {}
    for sequence in info["sequences"]:
        index = sequence["index"]
        if sequence["group"] != 0 or not 0 <= index <= 14:
            continue
        pcm, rate, region_level = one_note(data, info, sequence, 3520)
        # Windows one-shot mix of the centered dry voice. Region, sequence
        # controller and cold sound-effect gain are applied before output.
        # Center pan is 2896/4096 per channel. Integer stages give the
        # cold-start SAS dry levels observed for sequences 4/0/3 (749/1071/1177).
        level = region_level * 70 // 128 * 2896 // 4096
        mixed = array("h")
        for sample in pcm:
            scaled = sample * level >> 12
            mixed.extend((scaled, scaled))
        if sys.byteorder == "big":
            mixed.byteswap()
        encoded = io.BytesIO()
        with wave.open(encoded, "wb") as wav:
            wav.setparams((2, 2, rate, 0, "NONE", "not compressed"))
            wav.writeframes(mixed.tobytes())
        result = Region(encoded.getvalue())
        files[Path(f"assets/audio/SE_SYS-{index}.wav")] = result, result.sha256()
    require(len(files) == 15, "Missing menu SE_SYS sequences")
    return files
