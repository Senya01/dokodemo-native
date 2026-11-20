"""Verify the protected slot transaction with independent Windows processes."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory


SLOT = "UCJS10002000"
FILES = ("DATA.BIN", "PARAM.SFO", "ICON0.PNG", "ICON1.PMF", "PIC1.PNG", "SND0.AT3")
PHASES = ("staging", *FILES, "validation", "backup", "publish", "retire")


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def snapshot(slot):
    return {file.name: file.read_bytes() for file in slot.iterdir()}


def damage(path):
    bits = bytearray(path.read_bytes())
    bits[len(bits) // 2] ^= 1
    path.write_bytes(bits)


def verify(test, package, reference, root):
    def run(slot, operation, point="", expected=0):
        args = [test, operation, package, reference, slot]
        if point:
            args.append(point)
        result = subprocess.run(args, creationflags=subprocess.CREATE_NO_WINDOW,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
        require(result.returncode == expected,
                f"{slot.parent.name}: {operation} {point} exited {result.returncode}\n"
                + result.stdout.decode(errors="replace"))

    seed_old, seed_new = (root / name / SLOT for name in ("initial", "replacement"))
    run(seed_old, "write-old")
    run(seed_old, "read-old")
    run(seed_new, "write-new")
    run(seed_new, "read-new")
    baseline = snapshot(seed_old)
    require(baseline.keys() == set(FILES), "The writer did not publish all six files")
    previews = {key: baseline[key] for key in FILES[2:]}
    require(snapshot(seed_new)["DATA.BIN"] != baseline["DATA.BIN"], "Generations did not differ")

    def clone(label):
        slot = root / label / SLOT
        shutil.copytree(seed_old, slot)
        return slot

    for index, name in enumerate(FILES):
        for problem in ("damaged", "missing"):
            slot = clone(f"{problem}-{name}")
            file = slot / name
            if problem == "damaged":
                damage(file)
            else:
                file.unlink()
            before = snapshot(slot)
            if index < 2:
                run(slot, "refuse")
                require(snapshot(slot) == before, "Refusal changed protected slot files")
            else:
                run(slot, "read-old")
                require(snapshot(slot) == before, "Loading changed a damaged preview")
                run(slot, "write-new")
                run(slot, "read-new")
                require({key: snapshot(slot)[key] for key in previews} == previews,
                        "A successful save did not restore all previews")
        slot = clone(f"stage-{name}")
        run(slot, "damage-stage", name)
        run(slot, "read-old")
        require(snapshot(slot) == baseline, "Staging damage replaced the earlier files")
    print("All six files: damage, missing input and corrupted staging checked", flush=True)

    for method in ("fault", "crash"):
        for phase in PHASES:
            slot = clone(f"{method}-{phase}")
            previous = slot.with_suffix(".previous")
            pending = slot.with_suffix(".pending")
            run(slot, method, phase, expected=73 if method == "crash" else 0)
            if method == "crash" and phase == "publish":
                require(not slot.exists() and snapshot(previous) == baseline and pending.is_dir(),
                        "Interrupted publication did not preserve both generations in their expected locations")
            if phase == "retire":
                require(snapshot(previous) == baseline, "Retirement did not keep the old generation intact")
                run(slot, "read-new")
                run(slot, "write-old")
                run(slot, "read-old")
                require(not previous.exists(), "A subsequent save did not finish the deferred retirement")
            else:
                staged = snapshot(pending) if pending.exists() else None
                run(slot, "read-old")
                require(snapshot(slot) == baseline and not previous.exists(), "Wrong generation after interrupted write")
                if staged is not None:
                    require(snapshot(pending) == staged, "Recovery modified interrupted staging")
        print(f"{len(PHASES)} {method} checkpoints and independent reloads passed", flush=True)

    for point in ("live", "data", "owner", "staged", "rollback", "retire"):
        slot = clone(f"sharing-{point}")
        run(slot, "sharing", point)
        run(slot, "read-new" if point == "retire" else "read-old")
        if point != "retire":
            require(snapshot(slot) == baseline, "Windows sharing denial changed the prior generation")
    print("Six Windows sharing denials and independent reloads passed", flush=True)

    slot = root / "unpublished-only" / SLOT
    pending = slot.with_suffix(".pending")
    shutil.copytree(seed_new, pending)
    staged = snapshot(pending)
    run(slot, "absent")
    run(slot, "write-old")
    run(slot, "read-old")
    require(snapshot(pending) == staged, "An abandoned stage became authoritative or was overwritten")

    slot = clone("damaged-backup")
    previous = slot.with_suffix(".previous")
    slot.rename(previous)
    damage(previous / "DATA.BIN")
    damaged = snapshot(previous)
    run(slot, "refuse")
    require(not slot.exists() and snapshot(previous) == damaged, "A damaged backup was promoted or replaced")
    shutil.copytree(seed_old, slot)
    run(slot, "read-old")
    run(slot, "write-new")
    run(slot, "read-new")
    require(snapshot(slot.with_suffix(".retired")) == damaged, "The stale damaged copy was discarded")
    print("Abandoned staging and invalid backup stayed separate from the authoritative slot", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("test", type=Path)
    parser.add_argument("package", type=Path)
    parser.add_argument("reference", type=Path)
    args = parser.parse_args()
    if os.name != "nt" or os.environ.get("DOKODEMO_TEST_AUDIO") != "null":
        parser.error("Windows and DOKODEMO_TEST_AUDIO=null are required")
    test, package, reference = (path.resolve(strict=True) for path in (args.test, args.package, args.reference))
    with TemporaryDirectory(prefix="slot-recovery-") as directory:
        verify(test, package, reference, Path(directory))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
