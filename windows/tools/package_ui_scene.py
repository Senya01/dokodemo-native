"""Prepare one original CB2A and its indexed sprite resources."""
import struct
from pathlib import Path

from extract_bpar import BparArchive
from graphics_gim import decode_gim
from graphics_ui import parse_cb2a
from iso_reader import Region


def scene_media(archive: BparArchive, clip_path: str, bank_name: str,
                clip_digest: str, bank_digest: str, directory: Path = Path("assets/game/title")):
    clip = archive.payload(archive.by_name(clip_path))
    scene = parse_cb2a(clip.read(0, clip.size), clip_path)
    texture_bank = bytearray(b"TTX1" + struct.pack("<I", len(scene["sprites"])))
    for sprite in scene["sprites"]:
        gim = archive.payload(archive.by_name(sprite["resource"].upper()))
        picture, = decode_gim(gim.read(0, gim.size))
        texture_bank.extend(struct.pack("<HH", picture.width, picture.height))
        texture_bank.extend(picture.rgba)
    return {
        directory / Path(clip_path).name: (clip, clip_digest),
        directory / bank_name: (Region(bytes(texture_bank)), bank_digest),
    }
