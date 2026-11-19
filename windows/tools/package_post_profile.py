"""Build model bundles for the original scene-zero room from ISO members."""
import struct
from pathlib import Path

from extract_bpar import BparArchive
from graphics_gim import decode_gim
from iso_reader import Region, require
from model_formats import parse_dvs


def mesh_blob(archive, model_path, motion_paths, texture_directory):
    def read(path):
        region = archive.payload(archive.by_name(path))
        return region.read(0, region.size)

    model = read(model_path)
    data = bytearray(struct.pack("<4sI", b"RSM1", len(motion_paths)))

    def blob(bits):
        data.extend(struct.pack("<I", len(bits)))
        data.extend(bits)

    blob(model)
    for path in motion_paths:
        blob(read(path))
    names = sorted({t["name"].upper() for m in parse_dvs(model)["materials"]
                    for t in m["textures"] if t["role_bit"] == 1})
    data.extend(struct.pack("<I", len(names)))
    for name in names:
        texture = decode_gim(read(texture_directory.rstrip("/") + "/" + name + ".GIM"))[0]
        require(texture.metadata["level"] == 0, "Expected base texture")
        blob(name.encode("ascii"))
        data.extend(struct.pack("<II", texture.width, texture.height))
        blob(texture.rgba)
    return bytes(data)


def post_profile_media(root):
    room = BparArchive(root.payload(root.by_name("BG_ROOM.BPM")))
    clips = ["TUZURA_BIG_OPEN", "TUZURA_SMALL_OPEN", "FUSUMA_ON", "FUSUMA_OFF"] + [f"LIGHT{i:02}" for i in range(1, 8)] + ["LIGHT_ON", "TV_ON", "TV_OFF"]
    files = {}
    # The five shipped character scripts use the same date-ranged scene0
    # registration table. Singleton resource AA (wall), AB (carpet) lead
    # its typed arrays. Record 196 with null endpoints terminates the list.
    prefix = struct.pack("<8I", 0x90000003, 0x200000aa, 0x20000101, 0x20001231,
                         0x90000003, 0x200000ab, 0x20000101, 0x20001231)
    rows = None
    for name in ("NEKO", "ROBO", "USAGI", "INU", "KAERU"):
        package = BparArchive(root.payload(root.by_name("KS" + name + ".BPM")))
        script = package.payload(package.by_name(name + ".KSC"))
        bits = script.read(0, script.size)
        require(bits.count(prefix) == 1, "Missing scene0 resource table")
        pos = bits.index(prefix)
        selected = []
        while True:
            tag, resource, lower, upper = struct.unpack_from("<4I", bits, pos)
            pos += 16
            require(tag == 0x90000003, "Malformed scene0 resource record")
            if (resource, lower, upper) == (0x200000c4, 0, 0):
                break
            require(0x200000aa <= resource <= 0x200000c3 and
                    0x20000101 <= lower <= upper <= 0x20001231, "Invalid room date range")
            selected.append((resource & 255, lower & 0xffff, upper & 0xffff))
        require(rows is None or rows == selected, "Unexpected actor-specific room decorations")
        rows = selected
    table = Region(b"".join(struct.pack("<3H", *row) for row in rows))
    files[Path("assets/game/post-profile/season-dates.bin")] = table, table.sha256()
    models = [("BG_ROOM", clips), ("CARPET0", []), ("WALLPAPER0", [])]
    models.extend((f"SEASON{i:02}", []) for i in range(1, 26))
    models.extend((f"SHIRITORI{i:02}", []) for i in range(1, 7))
    models.extend((f"GARBAGE{i:02}", []) for i in range(1, 6))
    for name, motions in models:
        data = mesh_blob(room, name + "/MODEL01.DVS", [name + "/" + m + ".DAK" for m in motions],
                         name + "/GIM")
        region = Region(data)
        files[Path("assets/game/post-profile") / (name.lower() + ".bin")] = region, region.sha256()
    return files
