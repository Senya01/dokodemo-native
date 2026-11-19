"""The home friend notebook's original NOTE scene and indexed sprites."""
from pathlib import Path

from extract_bpar import BparArchive
from package_ui_scene import scene_media


def room_notebook_extra_media(root):
    pack = BparArchive(root.payload(root.by_name("MTOMO.BPM")))
    return scene_media(pack, "GUI/TOMODACHI_NOTE/NOTE.CB2A", "note-textures.bin",
                       "bb08a19dc8f597f2fdc2421ba8697b2622a7201923fd6111e16b5c0680461958",
                       "2d84b736adc9cc93f2ad45007e396f14e6dd336d75612cbfb708407f9ad90d3c",
                       Path("assets/game/notebook"))
