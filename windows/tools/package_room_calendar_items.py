"""Project NEKO calendar decoration records consumed by 3A9A from the retail KSC."""
from pathlib import Path
import struct

from iso_reader import Region, require
from package_room_inventory import first_gift_records


def room_calendar_item_media(root):
    data, h, _, _ = next(first_gift_records(root, "NEKO"))

    def word(index):
        return struct.unpack_from("<I", data, h.global_start + 4 * index)[0]

    def array(index):
        kind = word(index)
        require(kind >> 28 == 9, "Expected script array")
        return [word(index + n + 1) for n in range(kind & 0x3fff)]

    def row(ref):
        require(ref >> 16 == 0xb004, "Expected global row reference")
        return array(ref & 0xffff)

    # 3AA7 searches for a row with nil field 1, then replaces that row's
    # bounds with G9[2] & 0xffff. The two following comparisons use its date.
    require(array(0x60b9) == [0x20000001, 0x20000000, 0] and
            array(0x60bd) == [0x20000001, 0x20000005, 0, 0x20000002, 0x20000003, 0],
            "Calendar selection predicates changed")
    calendar = [row(ref) for ref in array(0x4eff)]
    layout = [row(ref) for ref in array(0x4eae)]
    catalogue = array(0x44ed)
    require(len(calendar) == 29 and len(layout) == 20 and
            [r[1] == 0 for r in calendar].count(True) == 1,
            "Unexpected NEKO calendar table shape")
    require(all(row(ref) == [0]*10 for ref in array(0x57a6)) and len(array(0x57a6)) == 20,
            "Seasonal row initialization differs")
    blob = bytearray(b"RCI1")
    for item, first, last in calendar:
        require(item >> 28 == 2 and first >> 28 == last >> 28 and
                (first >> 28 == 2 or first == last == 0), "Invalid calendar interval")
        definition = row(catalogue[item & 0xfffffff])
        require(definition[2] == 0, "31EF requires word selection for this item")
        mask = definition[7]
        require(mask >> 28 == 2 and mask & 0xfffff, "No supported calendar placement")
        # 3441 chooses the first set bit.
        bits = mask & 0xfffffff
        spot = (bits & -bits).bit_length() - 1
        blob.extend(struct.pack("<6I", item, first, last, definition[9],
                                0x20000000 | spot, layout[spot][0]))
    packed = Region(bytes(blob))
    return {Path("assets/game/post-profile/calendar-items.bin"): (packed, packed.sha256())}
