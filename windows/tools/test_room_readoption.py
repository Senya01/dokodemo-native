"""Exercise a new adoption from a locally supplied completed Toro slot."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("test", type=Path, help="freshly built post-profile-tests")
    parser.add_argument("package", type=Path, help="locally prepared Windows game package")
    parser.add_argument("slot", type=Path, help="completed Toro save directory with a learned word")
    args = parser.parse_args()
    if os.name != "nt" or os.environ.get("DOKODEMO_TEST_AUDIO") != "null":
        parser.error("Windows and DOKODEMO_TEST_AUDIO=null are required")
    test = args.test.resolve(strict=True)
    package = args.package.resolve(strict=True)
    slot = args.slot.resolve(strict=True)
    if not (slot / "PARAM.SFO").is_file() or not (slot / "DATA.BIN").is_file():
        parser.error("The input must be a protected slot directory")

    with TemporaryDirectory(prefix="readoption-", dir=package.parent) as directory:
        install = Path(directory)
        shutil.copytree(package / "assets", install / "assets", copy_function=os.link)
        result = subprocess.run(
            [test, install, "--replacement-slot", slot],
            creationflags=subprocess.CREATE_NO_WINDOW, timeout=480,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        )
    sys.stdout.buffer.write(result.stdout)
    if result.returncode:
        print(f"post-profile-tests exited with 0x{result.returncode:08X}")
    return int(result.returncode != 0)


if __name__ == "__main__":
    raise SystemExit(main())
