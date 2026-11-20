"""Check cold and in-session letter events using locally supplied protected slots."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory


ACTORS = ("toro", "robo", "usagi", "inu", "kaeru")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("test", type=Path, help="freshly built post-profile-tests")
    parser.add_argument("media", type=Path, help="package with locally generated assets")
    parser.add_argument("slots", type=Path, nargs=5, metavar="SLOT",
                        help="protected save directories in Toro, Robo, Usagi, Inu, Kaeru order")
    parser.add_argument("--report", type=Path, required=True, help="new directory for the logs")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if os.environ.get("DOKODEMO_TEST_AUDIO") != "null" or args.jobs < 1:
        parser.error("Set DOKODEMO_TEST_AUDIO=null and use a positive job count.")
    test = args.test.resolve(strict=True)
    media = args.media.resolve(strict=True)
    slots = [path.resolve(strict=True) for path in args.slots]
    if not (media / "assets/game/post-profile/room-return-letters.bin").is_file():
        parser.error("Regenerate the package media before testing return letters.")
    args.report.mkdir()

    def run(case):
        index, live = case
        name = ACTORS[index]
        mode = "live" if live else "cold"
        operation = "--return-letter-live" if live else "--return-letter-slot"
        answer = (index + int(live)) % 2
        with TemporaryDirectory(prefix=f"letter-{name}-", dir=media.parent) as directory:
            install = Path(directory)
            shutil.copytree(media / "assets", install / "assets", copy_function=os.link)
            flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
            result = subprocess.run([test, install, operation, slots[index], str(answer)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=flags)
        (args.report / f"{name}-{mode}.log").write_bytes(result.stdout)
        expected = f"Letter actor {index} {mode} choice {answer+1} descriptor ".encode()
        passed = result.returncode == 0 and expected in result.stdout
        print(f"{name} {mode} {'PASS' if passed else 'FAIL'}", flush=True)
        return passed

    cases = [(i, live) for live in (False, True) for i in range(len(ACTORS))]
    with ThreadPoolExecutor(max_workers=args.jobs) as workers:
        results = list(workers.map(run, cases))
    print(f"{sum(results)}/{len(results)} return-letter routes passed", flush=True)
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
