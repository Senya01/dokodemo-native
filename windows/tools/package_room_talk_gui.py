"""Prepare the first conversation's original prompt and balloon UI resources."""
from pathlib import Path

from extract_bpar import BparArchive
from package_ui_scene import scene_media


def room_talk_gui_media(root: BparArchive):
    data = BparArchive(root.payload(root.by_name("DATA.BPM")))
    out = {}
    for name, clip_hash, textures_hash in (
        ("SYOUKAI", "c3c396f03d500d3628f1e7265d8a9b4878ad3344fb32bea45d85ebf1867eb240",
         "1ac2027a4583ffbb4f4905e315529a6389d7600cb8095cb40613cdc95a9f3d16"),
        ("DIALOG", "6223b5468c53196ed375403bb1c4d99270c8660e521210ae6d5f01c79cae75aa",
         "4a094ae46295c2b8e31687c1de29270bc98554e7aa77865c353d5e51bc8b6c19"),
    ):
        out.update(scene_media(data, f"GUI/{name}/{name}.CB2A", f"{name.lower()}-textures.bin",
                               clip_hash, textures_hash, Path("assets/game/room-talk")))
    return out
