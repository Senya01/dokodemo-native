"""The authored full-room MMENU tree and its fifty original indexed GIM sprites."""
from pathlib import Path
from extract_bpar import BparArchive
from package_ui_scene import scene_media


def room_menu_media(root):
    pack = BparArchive(root.payload(root.by_name("DATA.BPM")))
    return scene_media(pack, "GUI/MAIN_MENU/MMENU.CB2A", "menu-textures.bin",
                       "5a8c8df5cfc2011850fa8c9403ef2cb8a071ed8d715e38cd50df5d4bc2ba896b",
                       "016761963e0de150c7604b7b0909b87c48ce8cd5a9dda4784e32400a28944d5c",
                       Path("assets/game/post-profile"))
