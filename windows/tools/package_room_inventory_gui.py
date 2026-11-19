"""Prepare the original MOCHIMONO layout and its indexed GIM sprites."""
from pathlib import Path

from extract_bpar import BparArchive
from package_ui_scene import scene_media


def room_inventory_gui_media(root):
    archive = BparArchive(root.payload(root.by_name("MOCHI.BPM")))
    return scene_media(
        archive, "GUI/MOCHIMONO/MOCHIMONO.CB2A", "inventory-textures.bin",
        "ca7dc478112e51a46266815398dfff7f160ceba69cb034987a95299fbefe2a18",
        "eeeb357e12e384669ecfbc144d81d49bdb722257e9f1dbe38438cf47d5b6c33f",
        Path("assets/game/inventory"),
    )
