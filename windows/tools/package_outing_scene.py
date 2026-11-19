"""Package scene-12 registration, selected 50AC recipe and the model/light bundles."""
from pathlib import Path
import struct

from extract_bpar import BparArchive
from iso_reader import Region, require
from package_post_profile import mesh_blob


def outing_scene_media(root, boot):
    ph, = struct.unpack("<I", boot.read(28, 4))
    text, data = [struct.unpack("<I", boot.read(ph + i * 32 + 4, 4))[0] for i in range(2)]
    string = lambda at: boot.read(text + at, 48).split(b"\0")[0].decode("ascii")
    aux_model_fmt = string(0xd3058)
    light_fmt = string(0xd307c)
    recipe, = struct.unpack("<I", boot.read(data + 0x9b0, 4))
    require(recipe == 0x488, "Unknown scene-12 auxiliary recipe")
    rows = [struct.unpack("<3I", boot.read(data + recipe + i * 12, 12)) for i in range(7)]
    require(rows.pop() == (0, 0, 0), "Unterminated scene-12 auxiliary recipe")
    counts = boot.read(data + 0xa18, 6)
    # BD28 applies XYZ from the current 16-byte row and the separate fixed heading.
    pose = boot.read(data + 0x4180 + 16 * 12, 12) + boot.read(text + 0xd7ee0, 4)
    # 7454 reads the current scene selector from 24A98. Unlike the three
    # background models, all three share this one child-material mapping.
    face_node, = struct.unpack("<i", boot.read(data + 0xb0c + 4 * 12, 4))
    face_row, = struct.unpack("<I", boot.read(data + 0xc8c + 4 * 12, 4))
    require(face_node >= 0 and face_row, "Missing source roof child-material mapping")
    face = struct.pack("<i", face_node) + boot.read(data + face_row, 16)
    directory = Path("assets/game/post-profile")
    files = {}

    def save(name, raw):
        reg = Region(raw)
        files[directory / name] = reg, reg.sha256()

    for variant in range(3):
        reg = boot.read(data + 0x43c4 + (66 + variant) * 20, 20).split(b"\0")[0].decode("ascii").upper()
        package = BparArchive(root.payload(root.by_name(reg + ".BPM")))
        require(counts[variant * 2] and 1 <= counts[variant * 2 + 1] <= 4,
                "Unexpected scene-12 light or main motion count")
        # 5610 constructs the seven seasonal lights. 635C's selected main-only
        # animations are appended after them. Source slot 0 targets the KURO
        # child, slot 2 the main clone; this selected recipe never uses slot 1.
        def member(format_string, *fields):
            path = (format_string % fields).upper().split("\\")
            require(len(path) == 3 and path[0] == path[1] == reg, "Unexpected roof model path")
            return "/".join(path[1:])

        lights = [member(light_fmt, reg, reg, slot) for slot in range(1, 8)]
        main = list(lights)
        child = []
        indices = bytearray()
        for child_ptr, second_ptr, main_ptr in rows:
            require(not second_ptr and bool(child_ptr) != bool(main_ptr), "Unexpected 50AC roof motion target")
            clips = child if child_ptr else main
            path = reg + "/" + string(child_ptr or main_ptr).upper()
            header = package.payload(package.by_name(path)).read(0, 20)
            require(header[:4] == b"DAK\0", "Invalid roof effect motion")
            flags, = struct.unpack_from("<I", header, 12)
            indices += bytes([int(bool(child_ptr)), len(clips), flags & 1])
            clips.append(path)
        # 52AC admits MODEL01..MODELnn; 606C starts every resulting
        # controller before the independent 635C auxiliary selection.
        base_count = counts[variant * 2 + 1]
        indices.append(base_count)
        for i in range(1, base_count + 1):
            path = f"{reg}/MODEL{i:02}.DAK"
            header = package.payload(package.by_name(path)).read(0, 20)
            require(header[:4] == b"DAK\0", "Invalid roof main motion")
            flags, = struct.unpack_from("<I", header, 12)
            indices += bytes([len(main), flags & 1])
            main.append(path)
        textures = reg + "/GIM"
        stem = "roof-" + str(66 + variant)
        save(stem + ".bin", mesh_blob(package, reg + "/MODEL01.DVS", main, textures))
        save(stem + "-aux.bin", mesh_blob(package, member(aux_model_fmt, reg, reg), child, textures))
        save(stem + "-controls.bin", b"RUF3" + pose + face + indices)
    return files
