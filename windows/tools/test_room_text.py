"""Run native conversation checks using the font in this build's shipped DATA.BP."""
import argparse
import mmap
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

from extract_bpar import BparArchive
from iso_reader import Region


def check(game: Path, executable: Path, inventory: bool, title=False, fallback_all=False):
    data = game / "assets/iso/PSP_GAME/USRDIR/data/DATA.BP"
    assets = game / "assets/game"
    with TemporaryDirectory(prefix="dokodemo-xft-") as temporary:
        font = Path(temporary) / "FONT.XFT"
        with data.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
            disc = BparArchive(Region(mapped), alignment=2048)
            package = BparArchive(disc.payload(disc.by_name("DATA.BPM")))
            resource = package.payload(package.by_name("FONT/FONT.XFT"))
            font.write_bytes(resource.read(0, resource.size))
        args = [assets, font, temporary] if inventory else [assets / ("title" if title else "post-profile"), font]
        if fallback_all:
            args.append("--fallback-all")
        flags = getattr(subprocess, "CREATE_NO_WINDOW", 0)
        subprocess.run([executable, *args], check=True, creationflags=flags)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("game", type=Path)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--inventory", action="store_true")
    parser.add_argument("--title", action="store_true")
    parser.add_argument("--fallback-all", action="store_true")
    args = parser.parse_args()
    check(args.game.resolve(), args.executable.resolve(), args.inventory, args.title, args.fallback_all)
