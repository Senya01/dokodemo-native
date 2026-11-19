"""Prepare the Options screen from the game's OPTION.BPM sprite archive."""
from pathlib import Path

from extract_bpar import BparArchive
from package_ui_scene import scene_media


def room_options_media(root):
    pack = BparArchive(root.payload(root.by_name("OPTION.BPM")))
    return scene_media(pack, "GUI/OPTION/OPTION.CB2A", "option-textures.bin",
                       "492109a53c465302387e0aca4fca316fa4b3705b4abe9a858645868c22c13102",
                       "0c83d4e82f9ef8c093cd9ce0c921a1a41d71ee23666e57b755bfbdebe94ceed2",
                       Path("assets/game/post-profile"))
