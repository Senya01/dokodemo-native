"""Install the native Windows decoder's pinned DLL closure into a local game package."""
from __future__ import annotations

import argparse
import hashlib
from importlib.metadata import PackageNotFoundError, distribution
import json
import os
from pathlib import Path
import sys
import tempfile
from urllib.request import urlretrieve
from zipfile import ZipFile, BadZipFile

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def wheel_payloads(wheel: Path, spec: dict) -> dict[str, bytes]:
    if digest(wheel.read_bytes()) != spec["sha256"]:
        raise ValueError(f"Decoder wheel hash differs from the pinned Windows build: {wheel}")
    with ZipFile(wheel) as archive:
        return {row["name"]: archive.read(row["member"]) for row in spec["files"]}


def load_payloads(wheel: Path | None, spec: dict) -> dict[str, bytes]:
    if wheel is not None:
        return wheel_payloads(wheel, spec)
    try:
        installed = distribution("av")
    except PackageNotFoundError:
        installed = None
    if installed and installed.version == "19.0.2":
        paths = {row["name"]: installed.locate_file(row["member"]) for row in spec["files"]}
        if all(path.is_file() for path in paths.values()):
            return {name: path.read_bytes() for name, path in paths.items()}
    with tempfile.TemporaryDirectory() as tmp:
        wheel = Path(tmp) / spec["wheel"]
        print(f"Downloading pinned Windows decoder from {spec['url']}", flush=True)
        urlretrieve(spec["url"], wheel)
        return wheel_payloads(wheel, spec)


def package(output: Path, wheel: Path | None) -> None:
    spec = json.loads((SCRIPTS / "native-decoder.json").read_text(encoding="utf-8"))
    files = load_payloads(wheel, spec)
    for row in spec["files"]:
        if digest(files[row["name"]]) != row["sha256"]:
            raise ValueError(f"Decoder input hash mismatch: {row['name']}")
    for name in ("COPYING.GPLv3", "COPYING.LGPLv3"):
        files[name] = (SCRIPTS / "decoder-licenses" / name).read_bytes()
    target = output.resolve() / "decoder"
    if target.resolve() != target:
        raise ValueError(f"Decoder directory redirects outside the expected location: {target}")
    target.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".package-", dir=target) as tmp:
        stage = Path(tmp)
        proofs = []
        for name, data in files.items():
            dest = target / name
            if dest.is_symlink():
                raise ValueError(f"Unexpected linked package file: {dest}")
            if not dest.is_file() or dest.read_bytes() != data:
                (stage / name).write_bytes(data)
                os.replace(stage / name, dest)
            checksum = digest(dest.read_bytes())
            if checksum != digest(data):
                raise ValueError(f"Packaged decoder differs from input: {dest}")
            proofs.append({"name": name, "size_bytes": len(data), "sha256": checksum})
        receipt = {key: value for key, value in spec.items() if key != "files"}
        receipt["files"] = proofs
        (stage / "manifest.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        os.replace(stage / "manifest.json", target / "manifest.json")
    print(f"Verified {len(proofs)} decoder files in {target}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="game package directory")
    parser.add_argument("--wheel", type=Path, help="pinned Windows PyAV wheel for offline builds")
    args = parser.parse_args()
    try:
        package(args.output, args.wheel)
    except (OSError, ValueError, KeyError, BadZipFile) as error:
        sys.exit(f"Native decoder packaging failed: {error}")
