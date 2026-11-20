"""Teach Robo through a native conversation, then verify the saved word in a new process."""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory


def check(test, install, operation, checkpoint):
    result = subprocess.run([test, install, operation], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            creationflags=subprocess.CREATE_NO_WINDOW, timeout=360)
    sys.stdout.buffer.write(result.stdout)
    sys.stdout.buffer.flush()
    records = re.findall(checkpoint + rb" (row=163 written=[0-9a-f]{64} reading=[0-9a-f]{64})\r?$",
                         result.stdout, re.MULTILINE)
    if result.returncode or len(records) != 1:
        raise ValueError(f"{operation} failed or omitted its word receipt (0x{result.returncode:08X})")
    return records[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("test", type=Path)
    parser.add_argument("package", type=Path, help="locally prepared media")
    args = parser.parse_args()
    if os.name != "nt" or os.environ.get("DOKODEMO_TEST_AUDIO") != "null":
        parser.error("Windows and DOKODEMO_TEST_AUDIO=null are required")
    test = args.test.resolve(strict=True)
    package = args.package.resolve(strict=True)
    with TemporaryDirectory(prefix="contact-save-", dir=package.parent) as directory:
        install = Path(directory)
        shutil.copytree(package / "assets", install / "assets", copy_function=os.link)
        saved = check(test, install, "--contact-save-write", b"saved_contact_word")
        slot = install / "savedata" / "UCJS10002000"
        first = {file.name: file.read_bytes() for file in slot.iterdir()}
        if first.keys() != {"DATA.BIN", "PARAM.SFO", "ICON0.PNG", "ICON1.PMF", "PIC1.PNG", "SND0.AT3"}:
            raise ValueError("System menu did not create the six protected slot files")
        loaded = check(test, install, "--contact-save-read", b"loaded_contact_word")
        if loaded != saved or first != {file.name: file.read_bytes() for file in slot.iterdir()}:
            raise ValueError("The second process changed the slot or restored a different word")
    print("Verified lesson, memo, System Save and cold memo with an unchanged six-file slot")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
