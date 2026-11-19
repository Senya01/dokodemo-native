"""Checked BP/BPM parser; no decompression or format conversions are implied."""
from __future__ import annotations

from dataclasses import dataclass
import struct
from pathlib import Path

from iso_reader import FormatError, Region, require, safe_name

U32_MAX = 0xFFFFFFFF


@dataclass(frozen=True, slots=True)
class Member:
    name: str
    slot: int
    name_offset: int
    file_offset: int
    size: int
    link: int
    offset: int


class BparArchive:
    """Parse every slot and validate all names, ranges, links and padding.

    The fourth slot word is validated as a nullable slot reference for this
    image, without claiming the game's hash-chain lookup has been recovered.
    Member offsets are relative to the start of this container.
    """
    def __init__(self, region: Region, *, alignment: int = 16):
        require(alignment in (16, 2048), "Unsupported BP/BPM alignment profile")
        self.region = region
        self.count, self.slots, self.names_length, self.header_size = struct.unpack("<4I", region.read(0, 16))
        require(0 < self.count <= self.slots, "BP/BPM active count outside slot range")
        self.names_start = 16 + self.slots * 16
        self.names_end = self.names_start + self.names_length
        require(self.names_end <= self.header_size <= region.size, "BP/BPM table/name/header outside container")
        require(self.names_length > 0, "Empty BP/BPM name area")
        require(self.header_size % alignment == 0 and region.size % alignment == 0,
                "BP/BPM header/container alignment mismatch")
        self.members: list[Member] = []
        names: set[str] = set()
        names_used = bytearray(self.names_length)
        links: dict[int, int] = {}
        active: set[int] = set()
        for slot in range(self.slots):
            raw = region.read(16 + slot * 16, 16)
            no, relative, size, link = struct.unpack("<4I", raw)
            if no == U32_MAX:
                require(raw == b"\xff" * 16, f"Noncanonical inactive BP/BPM slot {slot}")
                continue
            require(no < self.names_length, f"BP/BPM slot {slot}: name offset outside declared name area")
            start = self.names_start + no
            end = region.find_nul(start, self.names_end)
            require(end >= 0, f"BP/BPM slot {slot}: unterminated name")
            name = safe_name(region.read(start, end - start).decode("ascii"))
            require(name.casefold() not in names, f"BP/BPM member collides on Windows: {name}")
            names.add(name.casefold())
            names_used[no:end - self.names_start + 1] = b"\1" * (end - start + 1)
            offset = self.header_size + relative
            region.check(offset, size)
            require(relative % alignment == 0, f"BP/BPM slot {slot}: unaligned payload")
            self.members.append(Member(name, slot, no, relative, size, link, offset))
            active.add(slot)
            if link != U32_MAX:
                links[slot] = link
        require(len(self.members) == self.count, "BP/BPM header count differs from active slots")
        for slot, target in links.items():
            require(target in active, f"BP/BPM slot {slot} links to inactive/out-of-bounds slot {target}")
        complete: set[int] = set()
        for initial in links:
            visiting: set[int] = set()
            cursor = initial
            while cursor in links and cursor not in complete:
                require(cursor not in visiting, f"BP/BPM link cycle from slot {initial}")
                visiting.add(cursor)
                cursor = links[cursor]
            complete.update(visiting)
        self.linked_slots = len(links)
        self.gaps: list[dict] = []
        self.zero_length_members = sum(member.size == 0 for member in self.members)
        self._gap("header_padding", self.names_end, self.header_size)
        name_bytes = region.read(self.names_start, self.names_length)
        unused = bytes(name_bytes[i]
                       for i, flag in enumerate(names_used) if not flag)
        self.unused_name_bytes = len(unused)
        self.unused_name_nonzero = len(unused) - unused.count(0)
        require(self.unused_name_nonzero == 0, "Nonzero unreferenced BP/BPM name bytes")
        end = self.header_size
        for member in sorted(self.members, key=lambda item: (item.offset, item.size, item.slot)):
            require(member.offset >= end, f"Overlapping BP/BPM payload: {member.name}")
            self._gap("payload", end, member.offset)
            end = member.offset + member.size
        self._gap("tail", end, region.size)

    def _gap(self, kind: str, start: int, end: int) -> None:
        self.region.check(start, end - start)
        if end == start:
            return
        data = self.region.read(start, end - start)
        nonzero = len(data) - data.count(0)
        require(nonzero == 0, f"Nonzero BP/BPM {kind} padding at 0x{start:x}")
        self.gaps.append({"kind": kind, "offset": start, "size": end - start, "nonzero": nonzero})

    def payload(self, member: Member) -> Region:
        return self.region.subregion(member.offset, member.size)

    def by_name(self, name: str) -> Member:
        wanted = safe_name(name)
        found = [member for member in self.members if member.name == wanted]
        require(len(found) == 1, f"BP/BPM member not found or ambiguous: {name}")
        return found[0]

    def summary(self) -> dict:
        return {"size": self.region.size, "count": self.count, "slots": self.slots,
                "inactive_slots": self.slots - self.count, "header_size": self.header_size,
                "names_length": self.names_length, "linked_slots": self.linked_slots,
                "unused_name_bytes": self.unused_name_bytes, "unused_name_nonzero": self.unused_name_nonzero,
                "zero_length_members": self.zero_length_members,
                "gap_count": len(self.gaps), "gap_bytes": sum(g["size"] for g in self.gaps),
                "sha256": self.region.sha256()}


def read_bpm_member(package_path: Path, member_name: str) -> bytes:
    """Read one member from a local BPM, validating the entire container first.

    This API is useful for font glyphs that remain indexed inside the copied
    BPM files instead of being materialized as hundreds of thousands of files.
    """
    archive = BparArchive(Region(Path(package_path).read_bytes()))
    member = archive.by_name(member_name)
    return archive.payload(member).read(0, member.size)
