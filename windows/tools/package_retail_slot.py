"""Project the common disc media used by the local savedata utility."""
from pathlib import Path
from struct import pack
from iso_reader import Region, require
from extract_bpar import BparArchive


def retail_slot_files(iso):
    boot = iso.read_file("PSP_GAME/SYSDIR/BOOT.BIN")
    data = BparArchive(iso.read_file("PSP_GAME/USRDIR/data/DATA.BP"))
    common = BparArchive(data.payload(data.by_name("DATA.BPM")))
    title = boot.read(906044,25)
    detail = boot.read(906072,262)
    require(title.endswith(b"\0") and detail.endswith(b"\0"), "Missing savedata text terminator")
    media = [Region(title), Region(detail), common.payload(common.by_name("ICON/ICON0.PNG")),
             common.payload(common.by_name("ICON/ICON1.PMF")),
             iso.read_file("PSP_GAME/PIC1.PNG"), iso.read_file("PSP_GAME/SND0.AT3")]
    content = Region(b"RSM1"+b"".join(pack("<I",r.size)+r.read(0,r.size) for r in media))
    return {Path("assets/game/savedata/slot-media.bin"): (content,content.sha256())}
