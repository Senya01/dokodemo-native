"""Run the native Windows executable through startup and a new Toro room."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("package", type=Path, help="locally prepared media and decoder")
    args = parser.parse_args()
    if os.name != "nt" or os.environ.get("DOKODEMO_TEST_AUDIO") != "null":
        parser.error("Windows and DOKODEMO_TEST_AUDIO=null are required")
    source = args.executable.resolve(strict=True)
    package = args.package.resolve(strict=True)
    with TemporaryDirectory(prefix="startup-to-room-", dir=package.parent) as directory:
        root = Path(directory)
        for entry in ("assets", "decoder"):
            shutil.copytree(package / entry, root / entry, copy_function=os.link)
        executable = shutil.copy2(source, root / source.name)
        for mode in ("--verify-startup", "--verify-startup-to-room"):
            run = subprocess.run([executable, mode], cwd=root, creationflags=subprocess.CREATE_NO_WINDOW,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
            sys.stdout.buffer.write(run.stdout)
            sys.stdout.buffer.flush()
            if run.returncode:
                print(f"{mode} exited with code 0x{run.returncode:08X}")
                return 1
            if b"DOKODEMO_NULL_PCM_V1" not in run.stdout or b"title_frame=2" not in run.stdout:
                print(f"{mode} did not report the null audio path and startup frame")
                return 1
            if mode == "--verify-startup-to-room" and b"verified_first_room_activity" not in run.stdout:
                print(f"{mode} did not report a completed room activity")
                return 1
        if (root / "savedata").exists():
            print("Startup route unexpectedly wrote a slot")
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
