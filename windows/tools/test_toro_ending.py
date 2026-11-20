"""Exercise both Toro endings starting from a new private Windows game."""

import argparse
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
    with TemporaryDirectory(prefix="toro-stay-", dir=package.parent) as directory:
        root = Path(directory)
        shutil.copytree(package / "assets", root / "assets", copy_function=os.link)
        run = subprocess.run([test, root, "--toro-ending-linked"], creationflags=subprocess.CREATE_NO_WINDOW,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=660)
        sys.stdout.buffer.write(run.stdout)
        return run.returncode


if __name__ == "__main__":
    raise SystemExit(main())
