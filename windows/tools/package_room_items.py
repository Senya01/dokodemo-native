"""Pack room models and authored placement choices for each actor's first gift."""
import struct
from pathlib import Path

from extract_bpar import BparArchive
from iso_reader import Region, require
from model_formats import parse_dak
from package_post_profile import mesh_blob
from package_room_inventory import first_gift_records


def room_item_media(root, boot):
    room = BparArchive(root.payload(root.by_name("BG_ROOM.BPM")))
    ph, = struct.unpack("<I", boot.read(28, 4))
    code, data = [struct.unpack("<I", boot.read(ph + 32*i + 4, 4))[0] for i in range(2)]
    files = {}
    masks = [0]*208
    directory = Path("assets/game/post-profile")
    for actor in ("NEKO", "ROBO", "USAGI", "INU", "KAERU"):
        layouts = None
        for script, header, item, definition in first_gift_records(root, actor):
            def global_word(index):
                require(index < header.global_count, "Gift layout reference out of range")
                return struct.unpack_from("<I", script, header.global_start + 4*index)[0]

            if layouts is None:
                # 3768 stores layouts[candidate][0] into inventory row[8].
                prefix = bytes.fromhex("9001008008f595")
                suffix = bytes.fromhex("901a00f485f451910500")
                commands = script[28:header.pool_start]
                pos = commands.find(prefix)
                matches = []
                while pos != -1:
                    word = pos + len(prefix)
                    if commands.startswith(suffix, word + 2):
                        matches.append(int.from_bytes(commands[word:word+2], "little"))
                    pos = commands.find(prefix, pos + 1)
                require(len(matches) == 1, "Missing unique room layout lookup")
                layouts = matches[0]
                require(global_word(layouts) == 0x90000014, "Invalid room layout table")
            mask = global_word(definition + 8)
            width = global_word(definition + 9)
            require(0x20000000 < mask < 0x20100000 and 0x20000000 < width < 0x20010000,
                    "Invalid first-gift layout fields")
            bits = mask & 0xfffff
            require(masks[item] in (0, bits), "Inconsistent first-gift layout mask")
            masks[item] = bits
            slots = []
            for bit in range(20):
                if mask & (1 << bit):
                    entry = global_word(layouts + 1 + bit)
                    require(entry >> 16 == 0xb004, "Invalid room layout pointer")
                    index = entry & 0xffff
                    require(global_word(index) == 0x90000002, "Invalid room layout record")
                    selector, half_span = global_word(index+1), global_word(index+2)
                    require(selector == 0 or 0x20000000 <= selector <= 0x20000009,
                            "Invalid room layout selector")
                    require(0x20000000 < half_span < 0x20010000, "Invalid room layout interval")
                    slots.append((selector & 0xffff, half_span & 0xffff))
            layout = struct.pack("<3H", item, width & 0xffff, len(slots))
            layout += b"".join(struct.pack("<2H", *slot) for slot in slots)
            path = directory / f"gift-{item}.layout"
            if path in files:
                require(files[path][0].read(0, files[path][0].size) == layout,
                        "Actor-specific gift needs a separate placement table")
                continue
            region = Region(layout)
            files[path] = region, region.sha256()

            # BOOT resource name pointers 636C and signed motion counts 66AC.
            ptr, = struct.unpack("<I", boot.read(data + 0x636c + 4*item, 4))
            name = boot.read(code + ptr, 32).split(b"\0", 1)[0].decode("ascii").upper()
            count, = struct.unpack("<b", boot.read(data + 0x66ac + item, 1))
            require(name.isalnum() and 0 <= count <= 3, "Invalid room model binding")
            motions = []
            for number in range(1, count + 1):
                path = f"{name}/MODEL{number:02}.DAK"
                clip = room.payload(room.by_name(path))
                tracks = parse_dak(clip.read(0, clip.size))
                sections = [section for obj in tracks["objects"] for section in obj["sections"]]
                if number == 2 and len(sections) == 1 and sections[0]["type"] == 2 and sections[0]["subtype"] == 11:
                    # AirPlane and SkyShip also have a procedural rotation.
                    # Its controller is separate from their keyed propeller clips.
                    continue
                require(number == 1 and tracks["flags"] == 1 and sections and
                        all(s["type"] == 2 and s["subtype"] == 2 and s["count"] for s in sections),
                        "Unexpected first-gift motion controller")
                motions.append(path)
            model = Region(mesh_blob(room, name + "/MODEL01.DVS", motions, name + "/GIM"))
            files[directory / f"gift-{item}.bin"] = model, model.sha256()
    mask_table = Region(struct.pack("<208I", *masks))
    files[directory / "gift-placement-masks.bin"] = mask_table, mask_table.sha256()
    return files
