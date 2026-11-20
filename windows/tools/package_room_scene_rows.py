"""Project the shared NEKO/ROBO home scene item descriptors."""
from pathlib import Path
import struct
from extract_bpar import BparArchive
from iso_reader import Region, require
from ksc_format import parse_header


def scene_rows_payload(root, species):
    pack = BparArchive(root.payload(root.by_name("KS"+species+".BPM")))
    script = pack.payload(pack.by_name(species+".KSC"))
    data = script.read(0, script.size)
    h = parse_header(data)

    def fields(index):
        header, = struct.unpack_from("<I", data, h.global_start + 4*index)
        require(header >> 28 == 9, "Expected NEKO scene array")
        size = header & 0x3fff
        require(index + size < h.global_count, "Invalid NEKO scene array length")
        return struct.unpack_from(f"<{size}I", data, h.global_start + 4*(index+1))

    def row(ref):
        require(ref >> 16 == 0xb004, "Expected NEKO global scene row")
        return fields(ref & 0xffff)

    def number(word):
        require(word >> 28 == 2, "Expected NEKO tagged integer")
        return word & 0x0fffffff

    if species == "NEKO":
        binding = (0x4f91, 0x5897, 0x44ed, 0x4eae)
    else:
        require(species == "ROBO", "Unsupported home scene binding")
        binding = (0x4f99, 0x589f, 0x44f5, 0x4eb6)
    source, cold, definitions, anchors = map(fields, binding)
    require(len(source) == len(cold) == 5, "Unexpected number of scene rows")
    payload = bytearray(b"GSR1")
    for pair, initial in zip(source, cold):
        require(row(initial) == (0,)*10, "Unknown initial scene row")
        item, selector = row(pair)
        desc = row(definitions[number(item)])
        require(len(desc)==10 and desc[2]==0, "Scene item requires a dictionary selection")
        bits = number(desc[7])
        require(bits != 0, "Scene item has no placement")
        first = (bits & -bits).bit_length()-1
        anchor = row(anchors[first])[0]
        payload.extend(struct.pack("<5I", item, selector, desc[9], 0x20000000|first, anchor))
    return bytes(payload)


def scene_rows_media(root):
    payload = scene_rows_payload(root, "NEKO")
    require(scene_rows_payload(root, "ROBO") == payload, "ROBO home scene descriptors differ")
    resource = Region(payload)
    return {Path("assets/game/post-profile/room-scene-rows.bin"): (resource,resource.sha256())}
