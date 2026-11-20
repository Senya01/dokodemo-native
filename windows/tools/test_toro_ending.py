"""Exercise linked Toro stays in a new private Windows installation."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory

from test_slot_recovery import FILES, SLOT, require, snapshot


def run_case(test, root, *arguments):
    run = subprocess.run([test, root, *arguments], creationflags=subprocess.CREATE_NO_WINDOW,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=660)
    sys.stdout.buffer.write(run.stdout)
    sys.stdout.buffer.flush()
    return run.returncode


def run_calendar(test, root):
    slot = root / "savedata" / SLOT
    partial = b"incomplete staging, never eligible for recovery"
    status = run_case(test, root, "--calendar-seam", "seed")
    if status:
        return status
    seeded = snapshot(slot)
    require(seeded.keys() == set(FILES), "The fresh game must write all slot members")
    slot.rename(slot.with_suffix(".previous"))
    staged = slot.with_suffix(".pending")
    staged.mkdir()
    (staged / "DATA.BIN").write_bytes(partial)
    for phase in ("midnight", "finish", "continue"):
        print(f"Calendar seam: starting {phase} in an independent process", flush=True)
        status = run_case(test, root, "--calendar-seam", phase)
        if status:
            return status
        require(snapshot(slot).keys() == set(FILES), f"Missing slot members after {phase}")
        require(not slot.with_suffix(".previous").exists(), f"Backup remained after {phase}")
        require(snapshot(staged) == {"DATA.BIN": partial}, f"Interrupted stage changed after {phase}")
        if phase == "midnight":
            require(snapshot(root / "recovered-before-midnight") == seeded,
                    "Cold-start recovery changed the preceding generation")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("test", type=Path)
    parser.add_argument("package", type=Path, help="locally prepared game media")
    parser.add_argument("--calendar", action="store_true")
    args = parser.parse_args()
    if os.name != "nt" or os.environ.get("DOKODEMO_TEST_AUDIO") != "null":
        parser.error("Windows and DOKODEMO_TEST_AUDIO=null are required")
    test, package = (path.resolve(strict=True) for path in (args.test, args.package))
    with TemporaryDirectory(prefix="toro-stay-", dir=package.parent) as directory:
        root = Path(directory)
        shutil.copytree(package / "assets", root / "assets", copy_function=os.link)
        if args.calendar:
            return run_calendar(test, root)
        return run_case(test, root, "--toro-ending-linked")


if __name__ == "__main__":
    raise SystemExit(main())
