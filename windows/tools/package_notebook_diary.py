"""Pack the original notebook layout and the first-day Toro diary record."""
from pathlib import Path
import struct

from extract_bpar import BparArchive
from graphics_gim import decode_gim
from iso_reader import Region, require
from ksc_format import parse_header
from package_ui_scene import scene_media


def room_notebook_diary_media(root, boot):
    destination = Path("assets/game/notebook")
    diary = BparArchive(root.payload(root.by_name("ENIKKI.BPM")))
    files = scene_media(diary, "GUI/ENIKKI/ENIKKI.CB2A", "diary-textures.bin",
                        "448dd1de77376ed9707910b9124239b0aca2545ebf40c44296e3081c34f4eabd",
                        "59aea41db661cfe9f7bf752a3cf8eb74070495cb7a8d6cb95d119b03abfad1c3", destination)
    # A366 matches the day-one descriptor in NEKO's global diary table.
    ksc = BparArchive(root.payload(root.by_name("KSNEKO.BPM")))
    member = ksc.payload(ksc.by_name("NEKO.KSC"))
    script = member.read(0, member.size)
    header = parse_header(script)
    def global_word(index):
        return struct.unpack_from("<I", script, header.global_start + 4*index)[0]
    first_day_kind = 0x600000
    count = global_word(0x408f) & 0x3fff
    records = [global_word(0x4090+i) for i in range(count)]
    require(all(r >> 16 == 0xb004 for r in records), "Invalid diary descriptor table")
    matches = [i for i,r in enumerate(records) if global_word((r & 0xffff)+1) == (0x20000000 | first_day_kind)]
    require(len(matches) == 1, "Expected one matching first-day diary descriptor")
    diary_id = matches[0]
    descriptor = global_word(0x408f + 1 + diary_id)
    require(descriptor >> 16 == 0xb004, "Invalid day-one diary record reference")
    at = descriptor & 0xffff
    require(global_word(at) == 0x90000006 and global_word(at+1) == 0x20600000,
            "Invalid day-one diary event")
    text = global_word(at+6)
    require(text >> 28 == 5, "Expected diary text pool string")
    start = header.pool_start + (text & 0x0fffffff)
    template = script[start:script.index(0, start, header.pool_end)]
    require(template.count(b"$_YOU") == 1, "Expected day-one diary player substitution")
    # 3840C reads two words from the current actor's eight-byte image record.
    ph, = struct.unpack("<I", boot.read(28, 4))
    code, data = [struct.unpack("<I", boot.read(ph + 32*i + 4, 4))[0] for i in range(2)]
    table, = struct.unpack("<I", boot.read(data+0x62d8, 4))
    name_ptr, orientation = struct.unpack("<2I", boot.read(data+table+8*diary_id, 8))
    name = boot.read(code+name_ptr, 16).split(b"\0", 1)[0].decode("ascii").upper()
    pack = BparArchive(root.payload(root.by_name(name+".BPM")))
    gim = pack.payload(pack.by_name(name+".GIM"))
    picture, = decode_gim(gim.read(0, gim.size))
    require((picture.width, picture.height)==(192,144), "Expected landscape diary picture")
    kind = global_word(at+1) & 0x00f00000
    record = Region(struct.pack("<HHHI", len(template), orientation, diary_id, kind) + template + picture.rgba)
    files[destination/"first-diary.bin"] = record, record.sha256()
    return files
