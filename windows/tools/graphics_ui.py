"""Checked structural export of original B2A0 UI data.

This exposes sprite anchors, node links and raw animation keys. It does not
claim to implement CB2A visibility, interpolation, callbacks or hit testing.
"""
from __future__ import annotations

import math
from pathlib import PurePosixPath
import struct

from graphics_gim import GraphicsError, checked, sha256


CHANNELS = ("sprite", "scale", "rotation", "position", "color")


def parse_cb2a(data: bytes, member: str) -> dict:
    magic, sc, sa, nc, na, ac, aa, roots = struct.unpack("<4s7i", checked(data, 0, 32))
    if magic != b"B2A0" or any(x >= 0 for x in (sc, nc, ac)) or not 0 < roots <= -nc:
        raise GraphicsError("Unsupported B2A0 header/count contract")
    sc, nc, ac = -sc, -nc, -ac
    checked(data, sa, sc * 8)
    checked(data, na, nc * 8)
    checked(data, aa, ac * 52)
    sprites = []
    for index in range(sc):
        at = sa + index * 8
        x, y, ptr = struct.unpack_from("<hhI", data, at)
        checked(data, ptr, 1)
        end = data.find(b"\0", ptr)
        if end < 0:
            raise GraphicsError("Unterminated CB2A resource name")
        name = data[ptr:end].decode("ascii", errors="strict").replace("\\", "/")
        if not name or any(part in ("", ".", "..") for part in name.split("/")):
            raise GraphicsError("Invalid CB2A sprite resource name")
        resource = PurePosixPath(member).parent / name
        if resource.suffix.lower() != ".gim":
            resource = resource.with_suffix(".GIM")
        sprites.append({"index": index + 1, "record_offset": at, "anchor": [x, y],
                        "name_offset": ptr, "name": name, "resource": resource.as_posix()})
    nodes = []
    for index in range(nc):
        at = na + index * 8
        sibling, child, flags, opaque = struct.unpack_from("<4H", data, at)
        if sibling >= nc or child >= nc:
            raise GraphicsError("CB2A node link outside node table")
        nodes.append({"index": index, "record_offset": at, "sibling": sibling,
                      "child": child, "flags": flags, "opaque": opaque})
    parents = {}
    for node in nodes:
        child, seen = node["child"], set()
        while child:
            if child in seen or (child in parents and parents[child] != node["index"]):
                raise GraphicsError("CB2A sibling cycle or multiple parent")
            seen.add(child)
            parents[child] = node["index"]
            child = nodes[child]["sibling"]
    animations = []
    for animation in range(ac):
        at = aa + animation * 52
        words = struct.unpack_from("<13i", data, at)
        channels = []
        for kind, channel in enumerate(CHANNELS):
            count, ptr = words[2 * kind:2 * kind + 2]
            if count > 0:
                raise GraphicsError("CB2A positive relocated track count unsupported")
            checked(data, ptr, -count * 8)
            tracks = []
            for track in range(-count):
                descriptor = ptr + track * 8
                node, keys, key_ptr = struct.unpack_from("<HhI", data, descriptor)
                if node >= nc or keys > 0:
                    raise GraphicsError("Invalid CB2A track node/key count")
                key_size = 16 if kind in (1, 3) else 12
                checked(data, key_ptr, -keys * key_size)
                values = []
                for key in range(-keys):
                    key_at = key_ptr + key * key_size
                    fmt = "<2fii" if kind in (1, 3) else "<fii" if kind == 2 else "<iii"
                    parts = struct.unpack_from(fmt, data, key_at)
                    if any(isinstance(x, float) and not math.isfinite(x) for x in parts):
                        raise GraphicsError("Nonfinite CB2A key value")
                    values.append({"offset": key_at, "value": list(parts[:-2]),
                                   "mode": parts[-2], "frame": parts[-1],
                                   "bytes": data[key_at:key_at + key_size].hex()})
                tracks.append({"node": node, "descriptor_offset": descriptor, "keys": values})
            channels.append({"channel": channel, "tracks": tracks})
        remap_node, remap_count, remap_ptr = struct.unpack_from("<HhI", data, at + 40)
        if remap_count > 0:
            raise GraphicsError("CB2A positive remap count unsupported")
        checked(data, remap_ptr, -remap_count * 16)
        remap = [{"offset": remap_ptr + i * 16, "words": list(struct.unpack_from("<4i", data, remap_ptr + i * 16))}
                 for i in range(-remap_count)]
        animations.append({"index": animation, "record_offset": at, "channels": channels,
                           "remap_node": remap_node, "remap_keys": remap, "end_frame": words[12]})
    return {"source_sha256": sha256(data), "member": member,
            "header": {"sprites": sc, "nodes": nc, "animations": ac, "roots_word": roots},
            "sprites": sprites, "nodes": nodes, "parents": parents, "animations": animations,
            "status": "structural_export_only",
            "unimplemented": ["interpolation_modes", "visibility", "color_application", "runtime_callbacks", "input_hit_testing", "time_remap_semantics"]}
