"""Run the Windows meeting tests on loopback, with private installations and logs."""
import argparse
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys


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


def test_room_exchange(binary, output, package, source, operation):
    port = private_port()
    cancel_port = private_port()
    while cancel_port == port:
        cancel_port = private_port()
    cases = []
    for side in (1, 2, 0):
        install = output / str(side)
        donor = output / "inputs" / str(side) / source.name
        shutil.copytree(source, donor)
        shutil.copytree(package / "assets", install / "assets", copy_function=os.link)
        cases.append((install, (install, operation, donor,
                                port if side else cancel_port, side)))
    return run_group(binary, cases, 350)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("channel", "cards", "gifts", "shiritori"))
    parser.add_argument("binary", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--package", type=Path)
    parser.add_argument("--source", type=Path)
    args = parser.parse_args()
    if os.name != "nt" or os.environ.get("DOKODEMO_TEST_AUDIO") != "null":
        parser.error("Tests need Windows and DOKODEMO_TEST_AUDIO=null before launch")
    if args.mode != "channel" and (not args.package or not args.source):
        parser.error("Room tests need a built game package and a private Toro slot")
    output = args.output.resolve()
    output.mkdir(parents=True)
    binary = args.binary.resolve(strict=True)
    if args.mode == "channel":
        return int(not test_channel(binary, output))
    operation = {"cards": "--card-exchange-slot", "gifts": "--gift-exchange-slot",
                 "shiritori": "--shiritori-slot"}[args.mode]
    return int(not test_room_exchange(binary, output, args.package.resolve(strict=True),
                                      args.source.resolve(strict=True), operation))


if __name__ == "__main__":
    raise SystemExit(main())
