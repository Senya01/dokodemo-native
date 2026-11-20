"""Drive Toro's ordinary outing menu to each destination and back home."""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
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
    parser.add_argument("slot", type=Path, help="Toro save directory with a learned word in row 163")
    parser.add_argument("--jobs", type=int, default=3)
    args = parser.parse_args()
    if os.name != "nt" or os.environ.get("DOKODEMO_TEST_AUDIO") != "null" or args.jobs < 1:
        parser.error("Windows, DOKODEMO_TEST_AUDIO=null and a positive job count are required")
    test = args.test.resolve(strict=True)
    package = args.package.resolve(strict=True)
    slot = args.slot.resolve(strict=True)
    if not (slot / "PARAM.SFO").is_file() or not (slot / "DATA.BIN").is_file():
        parser.error("The input must be a protected slot directory")
    scenes = (*range(1, 14), 17, 18)

    passed = 0
    with TemporaryDirectory(prefix="out-and-back-", dir=package.parent) as directory:
        snapshot = Path(directory) / slot.name
        shutil.copytree(slot, snapshot)

        def run(scene):
            install = Path(directory) / str(scene)
            shutil.copytree(package / "assets", install / "assets", copy_function=os.link)
            source = install / "input" / slot.name
            shutil.copytree(snapshot, source)
            return subprocess.run(
                [test, install, "--destination-slot", source, str(scene)],
                creationflags=subprocess.CREATE_NO_WINDOW, timeout=240,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            )

        with ThreadPoolExecutor(max_workers=args.jobs) as workers:
            checks = {workers.submit(run, scene): scene for scene in scenes}
            for check in as_completed(checks):
                result = check.result()
                sys.stdout.buffer.write(result.stdout)
                sys.stdout.buffer.flush()
                if result.returncode:
                    print(f"Destination {checks[check]} exited with 0x{result.returncode:08X}", flush=True)
                else:
                    passed += 1
    print(f"{passed}/{len(scenes)} destinations returned home")
    return int(passed != len(scenes))


if __name__ == "__main__":
    raise SystemExit(main())
