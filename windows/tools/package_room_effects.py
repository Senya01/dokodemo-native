"""Package the one-note sounds used by the room motions in this build."""
from pathlib import Path
import hashlib
import struct
import sys

from audio_prepare import inspect_sgd
from iso_reader import Region, require
from model_formats import parse_dak
from package_title_effects import one_note


def motion_sounds(mesh):
    # RSM1 carries the original DVS followed by the selected original DAKs.
    magic, count, model_size = struct.unpack("<4sII", mesh.read(0, 12))
    require(magic == b"RSM1", "Expected room actor motion bundle")
    pos = 12 + model_size
    sounds = set()
    for _ in range(count):
        size, = struct.unpack("<I", mesh.read(pos, 4))
        motion = parse_dak(mesh.read(pos + 4, size))
        pos += 4 + size
        for obj in motion["objects"]:
            for track in obj["sections"]:
                if track["type"] == 0x40:
                    for tick, word in track["key_words"]:
                        if tick <= motion["duration_word"] and word >> 24 == 6:
                            sounds.add(((word >> 16) & 255, word & 255))
    return sounds


def room_effects(iso, files):
    entries = []
    absent = []
    secondary = files.get(Path("assets/game/post-profile/actor-5.bin"))
    secondary_sounds = motion_sounds(secondary[0]) if secondary else set()
    # Slot order is the BOOT loader's NEKO, ROBO, USAGI, INU, KAERU, SYS.
    banks = (
        ("SE_CHA_N", "7e54b7aa6ef75ef1907f793f7e672b0231ca651c665b2f1210c9f0d1735d1b82"),
        ("SE_CHA_R", "81f5f040915cfe135ed94c010da260153c86bc0fe20d1f939fdb8e46fc0cb616"),
        ("SE_CHA_U", "711ec561014993926da2bf089e05fa10b1a1debc75271c7763449568c685a90d"),
        ("SE_CHA_I", "6cab9a40d33aec91c7415511683d54dd3ac360c89bacc37c63338688c3b52b73"),
        ("SE_CHA_K", "5ee5ddb3fc743d845325a6ccddf1b98909f753055640c8327c0bafc52f6ad644"),
        ("SE_SYS", "06b0f1fd832a7ab28ed4ffec722f7174734ed8be5a15363e5f4478c3a2ebf020"),
    )
    for bank, (name, digest) in enumerate(banks):
        if bank == 5:
            selected = {(0, index) for index in range(15)}  # SE_SYS dry notes for live Options gain.
            selected.add((1, 8))  # 89942 -> 1ED8 -> GAME73 during the scheduled room event.
            # 88F8 sends actor 5's motion requests to SE_SYS as well.
            selected.update(secondary_sounds)
        else:
            mesh, _ = files[Path(f"assets/game/post-profile/actor-{bank}.bin")]
            selected = motion_sounds(mesh)
        region = iso.read_file(f"PSP_GAME/USRDIR/data/sound/se/{name}.sgd")
        data = region.read(0, region.size)
        require(hashlib.sha256(data).hexdigest() == digest, f"Unexpected source sound bank {name}")
        info = inspect_sgd(data)
        for group, index in sorted(selected):
            matches = [s for s in info["sequences"] if (s["group"], s["index"]) == (group, index)]
            if not matches and (bank < 5 or (group, index) in secondary_sounds):
                # This key exists in the motion. BOOT 558E4 resolves it to
                # null in this specific bank; 587F4 skips sequence startup.
                absent.append(struct.pack("<3I", bank, group, index))
                continue
            seq, = matches
            if bank != 5:
                controller_level = 3200
            elif group == 2 and index in (0, 1):
                controller_level = 3840  # SE_SYS notes selected by KURO's DAKs.
            else:
                controller_level = 3520
            pcm, rate, level = one_note(data, info, seq, controller_level)
            if sys.byteorder == "big":
                pcm.byteswap()
            entries.append(struct.pack("<6I", bank, group, index, rate, level, len(pcm)) + pcm.tobytes())
    body = (struct.pack("<4sI", b"RSE2", len(entries)) + b"".join(entries)
            + struct.pack("<I", len(absent)) + b"".join(absent))
    payload = Region(body + hashlib.sha256(body).digest())
    return {Path("assets/audio/room-effects.bin"): (payload, payload.sha256())}
