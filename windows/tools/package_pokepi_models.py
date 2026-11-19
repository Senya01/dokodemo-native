"""Collect GuiTitle's authored DVS/DAK and diffuse texture inputs from TITLE.BPM."""
import hashlib
import struct

from graphics_gim import decode_gim
from iso_reader import Region, require
from model_formats import parse_dvs


def pokepi_models(title):
    bundle = bytearray(b'PKMD\x02\x00\x00\x00')
    textures = set()

    def member(path):
        region = title.payload(title.by_name("CHARMODEL_TITLE/" + path))
        return region.read(0, region.size)

    def append(data):
        bundle.extend(struct.pack('<I', len(data)))
        bundle.extend(data)

    append(member("LIGHT.DVS"))
    for name in ("TORO", "SUZUKI", "JUN", "PIERRE", "RICKY", "KURO"):
        model = member(name + ".DVS")
        append(model)
        for mat in parse_dvs(model)["materials"]:
            textures.update(ref["name"].upper() for ref in mat["textures"] if ref["role_bit"] == 1)
        motions = ["RUN_L"] if name == "KURO" else ["TATI", "RUN_L", "DANCE", name[0] + "_GURUGURU02"]
        for motion in motions:
            append(member(name + "_" + motion + ".DAK"))

    bundle.extend(struct.pack('<I', len(textures)))
    for name in sorted(textures):
        images = decode_gim(member("GIM/" + name + ".GIM"))
        require(all(im.metadata["picture"] == 0 and im.metadata["frame"] == 0 for im in images)
                and images and images[0].metadata["level"] == 0, "GuiTitle base diffuse level missing")
        image = images[0]
        append(name.encode("ascii"))
        bundle.extend(struct.pack("<II", image.width, image.height))
        append(image.rgba)
    data = bytes(bundle)
    return Region(data), hashlib.sha256(data).hexdigest()
