"""Drive ordinary outings from the native room menus and back home."""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import os
from pathlib import Path
import shutil
import subprocess
import sys
from tempfile import TemporaryDirectory


def run_named(test, package, jobs):
    actors = (("robo", 19), ("usagi", 3), ("inu", 16), ("kaeru", 11))
    slot = Path("savedata/UCJS10002000")
    with TemporaryDirectory(prefix="named-outings-", dir=package.parent) as directory:
        root = Path(directory)

        def launch(install, *operation):
            shutil.copytree(package / "assets", install / "assets", copy_function=os.link)
            return subprocess.run([test, install, *operation], creationflags=subprocess.CREATE_NO_WINDOW,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=350)

        def run(case):
            actor, scene = case
            start = root / actor / "start"
            created = launch(start, f"--{actor}-save")
            if created.returncode:
                return created
            return launch(root / actor / "route", "--destination-slot", start / slot, str(scene))

        passed = 0
        with ThreadPoolExecutor(max_workers=jobs) as workers:
            for (actor, _), result in zip(actors, workers.map(run, actors)):
                print(f"{actor} natural outing: {'failed' if result.returncode else 'passed'}", flush=True)
                sys.stdout.buffer.write(result.stdout)
                sys.stdout.buffer.flush()
                passed += result.returncode == 0
    print(f"{passed}/{len(actors)} alternate actors cancelled, left and returned twice")
    return int(passed != len(actors))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("test", type=Path, help="freshly built post-profile-tests")
    parser.add_argument("package", type=Path, help="locally prepared Windows game package")
    parser.add_argument("slot", nargs="?", type=Path, help="Toro save directory with a learned word in row 163")
    parser.add_argument("--named", action="store_true", help="Create new saves and use the other four characters")
    parser.add_argument("--jobs", type=int, default=3)
    args = parser.parse_args()
    if os.name != "nt" or os.environ.get("DOKODEMO_TEST_AUDIO") != "null" or args.jobs < 1:
        parser.error("Windows, DOKODEMO_TEST_AUDIO=null and a positive job count are required")
    test = args.test.resolve(strict=True)
    package = args.package.resolve(strict=True)
    if args.named:
        if args.slot is not None:
            parser.error("Named outing tests use new System saves.")
        return run_named(test, package, args.jobs)
    if args.slot is None:
        parser.error("Pass a Toro slot or use --named.")
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
