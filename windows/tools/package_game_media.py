"""Package the native game's original media from the verified source image."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import tempfile

from audio_prepare import inspect_sgd
from extract_bpar import BparArchive
from extract_game import BP_PATH, ROOT, hash_file, reference_iso
from graphics_gim import decode_gim
from package_ui_scene import scene_media
from package_pokepi_models import pokepi_models
from package_nickname import nickname_media
from package_post_profile import post_profile_media
from package_cold_room_actor import cold_room_actor_media
from package_room_music import room_music
from package_room_talk_gui import room_talk_gui_media
from package_room_talk import room_talk_media
from package_room_activity_selector import room_activity_media
from package_room_activity_topic import room_activity_topic_media
from package_room_activity_event import room_activity_event_media
from package_room_vocabulary import room_vocabulary_media
from package_room_scene_rows import scene_rows_media
from package_room_calendar_items import room_calendar_item_media
from package_roof_signs import roof_sign_media
from package_outing_scene import outing_scene_media, other_outing_media
from package_room_common_topic import room_fallback_list_media, room_outing_media
from package_room_trait_topic import room_word_traits_media
from package_room_menu import room_menu_media
from package_room_options import room_options_media
from package_room_notebook import room_notebook_media
from package_notebook_extra import room_notebook_extra_media
from package_notebook_diary import room_notebook_diary_media
from package_retail_slot import retail_slot_files
from package_room_word_conversion import room_word_conversion_media
from package_room_inventory_gui import room_inventory_gui_media
from package_room_inventory import room_inventory_media
from package_room_items import room_item_media
from package_room_effects import room_effects
from package_title_effects import title_effects
from iso_reader import CHUNK, Iso9660, Region, require, safe_name

PRX_DIRECTORY = Path("PSP_GAME/USRDIR/prx")


def game_media(iso: Iso9660, modules: list[dict]) -> dict[Path, tuple[Region, str]]:
    files = {}
    for module in modules:
        filename = safe_name(module["filename"])
        require("/" not in filename and filename.lower().endswith(".prx"), f"Invalid module filename: {filename}")
        relative = PRX_DIRECTORY / filename
        target = Path("assets/iso") / relative
        require(target not in files, f"Duplicate module: {filename}")
        region = iso.read_file(relative.as_posix())
        require(region.size == module["size_bytes"], f"Unexpected module size: {filename}")
        files[target] = region, module["sha256"]

    for name, digest in (
        ("BGM_TITLE", "56f5c34d4ce23281978241c72ac5e88bc64f548a7c6d7da69c9d2355019c397b"),
        ("BGM_GUIDE", "f156f5f2a0a115aa58f9d68de6c690b826986bf02540c3f7e2e59c476bf536de"),
    ):
        music = iso.read_file(f"PSP_GAME/USRDIR/data/sound/bgm/{name}.sgd")
        waves = inspect_sgd(music.read(0, music.size))["waves"]
        require(len(waves) == 1 and waves[0]["name"] == name, f"Unexpected {name} bank")
        wave = waves[0]
        files[Path(f"assets/audio/{name}.at3")] = music.subregion(wave["data_offset"], wave["data_size"]), digest

    files.update(title_effects(iso.read_file("PSP_GAME/USRDIR/data/sound/se/SE_SYS.sgd")))
    boot = iso.read_file("PSP_GAME/SYSDIR/BOOT.BIN")
    files[Path("assets/game/boot/debug-font.bin")] = (
        boot.subregion(0xE1ED0, 768), "545d1be44fb2c2859e213977ecb5b76612d5632ee61281455dccccd731af08da",
    )
    # GuiTitle 314E4 appends these three CP932 strings to the replacement prompt.
    # Addresses are relative to BOOT's first load segment at file offset 0x80.
    replacement_lines = []
    for address, length in ((0xDA0E8, 51), (0xDA11C, 120), (0xDA198, 20)):
        source = boot.read(0x80 + address, length + 1)
        require(source[-1:] == b"\0" and b"\0" not in source[:-1], "Invalid replacement title text")
        message = source[:-1].decode("cp932")
        replacement_lines.extend(message[i:i + 18].encode("cp932") for i in range(0, len(message), 18))
    prompt = Region(b"\n".join(replacement_lines) + b"\n")
    files[Path("assets/game/title/replacement-prompt.txt")] = (
        prompt, "86cdaea27b43d086db27a905b683acbcced0bc99ca7d6ef7123bb1d943bffc7e",
    )
    archive = iso.read_file(BP_PATH)
    files[Path("assets/iso") / BP_PATH] = archive, "180fc4c79286449e9e5b0d9df81ee3003998d8ac72f87fa9450174d315fac134"
    root = BparArchive(archive, alignment=2048)
    title = BparArchive(root.payload(root.by_name("TITLE.BPM")))
    logos = (
        ("SCELOGO", (416, 51), "e54837b49aed329656ecd2eea68542679e7c2d54c0eadf697ed1cc04c1020fb3",
         "16b4edab4c0434d9c491cbf55504984254328c3c08c249992d061886b1fdd36d"),
        ("BEXIDELOGO", (192, 71), "ad0eb12e0f10e0ab73e75ea5e8f8eb79601d4afbf6165be953d970aa058a26bf",
         "87a9af6a708b25f61b143c5855a17abaa866ed0e1cbe300b77c4f8cdc2a31933"),
    )
    for name, dimensions, source_digest, rgba_digest in logos:
        logo = title.payload(title.by_name(f"GUI/TITLE/{name}.GIM"))
        require(logo.sha256() == source_digest, f"Unexpected {name} resource")
        pictures = decode_gim(logo.read(0, logo.size))
        require(len(pictures) == 1 and (pictures[0].width, pictures[0].height) == dimensions,
                f"Unexpected {name} dimensions")
        files[Path(f"assets/game/title/{name}.rgba")] = Region(pictures[0].rgba), rgba_digest
    files.update(scene_media(title, "GUI/TITLE/TITLE.CB2A", "scene-textures.bin",
                             "04635d29d45ff148c55e3acc3df40dcb0d261bda1146772ecec3b5d00ebc6bd4",
                             "a9bbecc85e222fc0af996f9ccc66cec0adf5dc2a688eda3b47b21eeeebc7c9e8"))
    common = BparArchive(root.payload(root.by_name("DATA.BPM")))
    files.update(scene_media(common, "GUI/SELECT/SELECT.CB2A", "select-textures.bin",
                             "6473316977b01dcce46022b643a364efae5f3a9e6da2770b67eda4c42e422d06",
                             "ffbe0f582015f9761ba0b8da3e7b19b87ab9ff7daab0b74316424314cf09ed72"))
    files.update(scene_media(common, "GUI/TRC/TRC.CB2A", "trc-textures.bin",
                             "ee9fedd4cbf141814dbd01b4edd99aa66c88f0eb88d4079212ce300d5fe52c33",
                             "eb91b97daa8d9ad7ed3c23c5c79bc9c9df35a08495df9a46748a85d788797be6"))
    files.update(post_profile_media(root))
    files.update(cold_room_actor_media(root, boot))
    files.update(room_effects(iso, files))
    files.update(room_music(iso))
    files.update(room_talk_gui_media(root))
    files.update(room_talk_media(root, boot))
    files.update(room_inventory_gui_media(root))
    files.update(room_inventory_media(root))
    files.update(room_item_media(root, boot))
    files.update(room_activity_media(root))
    files.update(room_activity_topic_media(root))
    files.update(room_activity_event_media(root))
    files.update(room_vocabulary_media(root))
    files.update(scene_rows_media(root))
    files.update(room_calendar_item_media(root))
    files.update(room_fallback_list_media(root))
    files.update(room_outing_media(root))
    files.update(outing_scene_media(root, boot))
    files.update(other_outing_media(root, boot))
    files.update(roof_sign_media(root, boot))
    files.update(room_word_traits_media(root))
    files.update(room_menu_media(root))
    files.update(room_options_media(root))
    files.update(room_notebook_media(root))
    files.update(room_notebook_extra_media(root))
    files.update(room_notebook_diary_media(root, boot))
    files.update(retail_slot_files(iso))
    files.update(room_word_conversion_media(common))
    files[Path("assets/game/title/pokepi-models.bin")] = pokepi_models(title)
    files.update(nickname_media(root,boot))
    return files


def package_game_media(output: Path) -> None:
    output = output.resolve()
    asset_root = output / "assets"
    require(not asset_root.resolve().is_relative_to((ROOT / "assets").resolve()),
            "Choose a package directory separate from the prepared project assets")
    modules = json.loads((ROOT / "scripts/startup-media.json").read_text(encoding="utf-8-sig"))
    require(bool(modules), "Empty startup media list")
    with reference_iso(None) as (_, iso, _):
        files = game_media(iso, modules)
        for relative, (region, digest) in files.items():
            dest = output / relative
            require(dest.resolve() == dest, f"Unexpected redirected media path: {dest}")
            require(region.sha256() == digest, f"Unexpected source digest: {relative}")

        output.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".game-media-", dir=output) as temporary:
            updates = []
            for relative, (region, digest) in files.items():
                dest = output / relative
                if dest.is_file() and dest.stat().st_size == region.size and hash_file(dest) == digest:
                    continue
                staged = Path(temporary) / relative
                staged.parent.mkdir(parents=True, exist_ok=True)
                with staged.open("wb") as stream:
                    for offset in range(0, region.size, CHUNK):
                        stream.write(region.read(offset, min(CHUNK, region.size - offset)))
                require(hash_file(staged) == digest, f"Staged media differs from source: {relative}")
                updates.append((staged, dest))
            for staged, dest in updates:
                dest.parent.mkdir(parents=True, exist_ok=True)
                os.replace(staged, dest)
    print(f"Verified {len(files)} native game media files in {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="game package directory")
    args = parser.parse_args()
    try:
        package_game_media(args.output)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Game media packaging failed: {error}")
