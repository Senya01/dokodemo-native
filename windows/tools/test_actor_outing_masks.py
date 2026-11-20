"""Verify the first native System save for each alternate Pokepi."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("test", type=Path)
    parser.add_argument("package", type=Path, help="locally prepared game media")
    args = parser.parse_args()
    if os.name != "nt" or os.environ.get("DOKODEMO_TEST_AUDIO") != "null":
        parser.error("Windows and DOKODEMO_TEST_AUDIO=null are required")
    test, package = (path.resolve(strict=True) for path in (args.test, args.package))

    def run(actor):
        with TemporaryDirectory(prefix=f"first-outing-{actor}-", dir=package.parent) as directory:
            install = Path(directory)
            shutil.copytree(package / "assets", install / "assets", copy_function=os.link)
            return subprocess.run(
                [test, install, f"--{actor}-save"],
                creationflags=subprocess.CREATE_NO_WINDOW, timeout=300,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            )

    actors = ("robo", "usagi", "inu", "kaeru")
    passed = 0
    with ThreadPoolExecutor(max_workers=2) as workers:
        for actor, result in zip(actors, workers.map(run, actors)):
            print(f"{actor} first save: {'failed' if result.returncode else 'passed'}", flush=True)
            sys.stdout.buffer.write(result.stdout)
            sys.stdout.buffer.flush()
            passed += result.returncode == 0
    print(f"{passed}/{len(actors)} initial outing flags verified in protected saves")
    return int(passed != len(actors))


if __name__ == "__main__":
    raise SystemExit(main())
