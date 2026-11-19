"""Package the authored word memo scene and its indexed GIM resources."""
from pathlib import Path

from extract_bpar import BparArchive
from package_ui_scene import scene_media


def room_notebook_media(root):
    archive = BparArchive(root.payload(root.by_name("MKOTOBA.BPM")))
    return scene_media(archive, "GUI/KOTOBA_MEMO/MEMO.CB2A", "memo-textures.bin",
                       "361af2782e34f1d8ad3c03b27519adfd7122622c21c9ab0445ccb3fe18ad9169",
                       "ad287b89cb36cb459caad2f64cf59301cf0e9d1f05fcb4486318ebf16fb5fbe0",
                       Path("assets/game/notebook"))
