"""Run the Windows meeting tests on loopback, with private installations and logs."""
import argparse
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
from tempfile import TemporaryDirectory


def private_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
        reservation.bind(("127.0.0.1", 0))
        return reservation.getsockname()[1]


def run_group(binary, cases, seconds):
    processes = []
    try:
        for folder, arguments in cases:
            folder.mkdir(parents=True, exist_ok=True)
            log = folder / "test.log"
            with log.open("xb") as stream:
                process = subprocess.Popen(
                    [binary, *map(str, arguments)],
                    creationflags=subprocess.CREATE_NO_WINDOW,
                    stdout=stream,
                    stderr=subprocess.STDOUT,
                )
            processes.append((process, folder))
        exits = []
        for process, folder in processes:
            code = process.wait(timeout=seconds)
            (folder / "exit.txt").write_text(str(code), encoding="ascii")
            print(folder.name, code, flush=True)
            sys.stdout.buffer.write((folder / "test.log").read_bytes())
            sys.stdout.buffer.flush()
            exits.append(code)
        return all(code == 0 for code in exits)
    finally:
        for process, _ in processes:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=5)


def test_channel(binary, output):
    for label, parameters in (
        ("pair", ((0, 0, 1), (0, 0, 2))),
        ("different-venues", ((1, 0, 0), (2, 0, 0))),
        ("different-modes", ((1, 0, 0), (1, 2, 0))),
    ):
        port = private_port()
        if not run_group(binary, ((output / label / str(index), (port, *spec))
                                  for index, spec in enumerate(parameters)), 12):
            return False
    return True


def test_room_exchange(binary, output, package, source, operation, paired_cancel=False):
    port = private_port()
    cancel_port = private_port()
    while cancel_port == port:
        cancel_port = private_port()
    cases = []
    sides = (1, 2) if paired_cancel else (1, 2, 0)
    gate = output / "ready"
    if paired_cancel:
        gate.mkdir(parents=True)
    for side in sides:
        install = output / str(side)
        donor = output / "inputs" / str(side) / source.name
        shutil.copytree(source, donor)
        shutil.copytree(package / "assets", install / "assets", copy_function=os.link)
        request = (install, operation, donor, port if side else cancel_port, side)
        cases.append((install, request + (gate,) if paired_cancel else request))
    if not run_group(binary, cases, 350):
        return False
    if operation == "--card-exchange-slot":
        return check_saved_cards(binary, output)
    if operation == "--gift-exchange-slot":
        return check_saved_gifts(binary, output, sides)
    return True


def slot_files(install):
    slot = install / "savedata" / "UCJS10002000"
    contents = {file.name: file.read_bytes() for file in slot.iterdir()}
    if contents.keys() != {"DATA.BIN", "PARAM.SFO", "ICON0.PNG", "ICON1.PMF", "PIC1.PNG", "SND0.AT3"}:
        raise ValueError("Meeting did not produce a six-file slot")
    return contents


def check_saved_cards(binary, output):
    original = [slot_files(output / str(side)) for side in (1, 2)]
    cases = [(output / "cold" / str(side), (output / str(side), "--cold-meeting-card", side))
             for side in (1, 2)]
    if not run_group(binary, cases, 120):
        return False
    records = []
    for folder, _ in cases:
        records.append(re.findall(rb"^cold_card own=\w+ peer=\w+ sent=([0-9a-f]{64}) received=([0-9a-f]{64})\r?$",
                                  (folder / "test.log").read_bytes(), re.MULTILINE))
    if (any(len(record) != 1 for record in records) or
            records[0][0] != records[1][0][::-1] or
            original != [slot_files(output / str(side)) for side in (1, 2)]):
        raise ValueError("Cold cards differ from the other player's own card or the slot changed")
    print("Both cold processes opened their peer's card and preserved all six files")
    return True


def check_saved_gifts(binary, output, sides):
    def inventory_receipt(folder, checkpoint):
        log = (folder / "test.log").read_bytes()
        records = re.findall(rb"^" + checkpoint + rb" inventory=([0-9a-f]{64})\r?$", log, re.MULTILINE)
        if len(records) != 1:
            raise ValueError("Missing inventory receipt")
        return records[0]

    installs = [output / str(side) for side in sides]
    original = [slot_files(install) for install in installs]
    cases = [(output / "cold" / install.name, (install, "--cold-meeting-gift")) for install in installs]
    if not run_group(binary, cases, 120):
        return False
    for install, (cold, _) in zip(installs, cases):
        if inventory_receipt(install, b"saved_gift") != inventory_receipt(cold, b"cold_gift"):
            raise ValueError("The cold gift inventory differs from System Save")
    if original != [slot_files(install) for install in installs]:
        raise ValueError("A cold gift session changed its slot")
    print(f"Cold gift bags matched all 170 inventory rows and six files for {len(sides)} slots")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("channel", "cards", "gifts", "shiritori"))
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path, nargs="?", help="keep the logs in a fresh directory")
    parser.add_argument("--package", type=Path)
    parser.add_argument("--source", type=Path)
    args = parser.parse_args()
    if os.name != "nt" or os.environ.get("DOKODEMO_TEST_AUDIO") != "null":
        parser.error("Tests need Windows and DOKODEMO_TEST_AUDIO=null before launch")
    if args.mode != "channel" and (not args.package or not args.source):
        parser.error("Room tests need a built game package and a private Toro slot")
    binary = args.binary.resolve(strict=True)
    package = args.package.resolve(strict=True) if args.package else None

    def run(output):
        if args.mode == "channel":
            return int(not test_channel(binary, output))
        operation = {"cards": "--card-exchange-slot", "gifts": "--gift-exchange-slot",
                     "shiritori": "--shiritori-slot"}[args.mode]
        source = args.source.resolve(strict=True)
        if not test_room_exchange(binary, output, package, source, operation):
            return 1
        if args.mode == "gifts" and not test_room_exchange(
                binary, output / "cancel-pair", package, source, operation, paired_cancel=True):
            return 1
        return 0

    if args.output:
        output = args.output.resolve()
        output.mkdir(parents=True)
        return run(output)
    with TemporaryDirectory(prefix="room-meeting-", dir=package.parent if package else None) as directory:
        return run(Path(directory))


if __name__ == "__main__":
    raise SystemExit(main())
