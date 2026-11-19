"""Pack the selected scene-zero ASC requests, control data and their DVS/DAKs."""
import struct
from pathlib import Path

from extract_bpar import BparArchive
from iso_reader import Region, require
from model_formats import parse_asc
from ksc_format import parse_header
from package_post_profile import mesh_blob
from package_room_activity_selector import project_activity_rows
from package_room_activity_topic import project_topic_bodies
from ksc_format import parse_code


def cold_room_actor_media(root, boot):
    def member(archive, name):
        part = archive.payload(archive.by_name(name))
        return part.read(0, part.size)

    # BD28, 8D78, 8EC4 and 08A88 use separate main-module data tables.
    ph = struct.unpack("<I", boot.read(28, 4))[0]
    code, data = [struct.unpack("<I", boot.read(ph + i * 32 + 4, 4))[0] for i in range(2)]
    def game_name(table, slot):
        ptr, = struct.unpack("<I", boot.read(data + table + slot * 4, 4))
        return boot.read(code + ptr, 12).split(b"\0")[0].decode("ascii")

    shared = BparArchive(root.payload(root.by_name("DATA.BPM")))
    coordinates = member(shared, "SEQ/SEQUENCE_COORD.BIN")
    files = {}
    routes = zip((0x8938B, 0x672A2, 0x631CB, 0x673DB, 0x671D4),
                 (0x8DE46, 0x6BD07, 0x67BEA, 0x6BDBF, 0x6BBD2),
                 (0x8D798, 0x6B659, 0x6753C, 0x6B711, 0x6B524),
                 (0x8D91B, 0x6B7DC, 0x676BF, 0x6B894, 0x6B6A7),
                 (0x8940D, 0x67324, 0x6324D, 0x6745D, 0x67256),
                 (0x1A62, 0x1A63, 0x1A78, 0x1A78, 0x1A78))
    for actor, (entrance, walk, turn, zoom, invite, repeat) in enumerate(routes):
        species = game_name(0xd7c, actor)
        body = game_name(0xd58, actor)
        group = BparArchive(root.payload(root.by_name(species + ".BPM")))
        scripts = BparArchive(root.payload(root.by_name("KS" + species + ".BPM")))
        script_file = member(scripts, species + ".KSC")
        script = script_file[28:]
        def at(pc, expected):
            return script.startswith(bytes.fromhex(expected), pc)
        require(at(entrance, "30 01 91 00 00 81 b0 00 51") and
                at(walk, "81 9d 00 20 01") and at(turn, "81 b2 00 20 01") and
                script[walk+5:walk+11] == script[turn+5:turn+11] and
                at(zoom, "80 01 20 01") and at(zoom+7, "80 0f 44") and
                at(invite, "30 03 80 2b 80 01 8b f0 02") and
                at(repeat, "90 01 00 84 8b 22 02 8b 23 00 80 3c 44"),
                "Unknown cold-room KSC entrance or greeting")
        # Each cold first conversation uses entry 1 of its species' event
        # table. Element 0 of that row is the requested invitation ASC, and
        # element 1 is the species-specific callback invoked by the script.
        lookup = (0x89896, 0x677AD, 0x636D6, 0x678E6, 0x676DF)[actor]
        require(at(lookup, "51 91 01 00 95"), "Unknown invitation table resolver")
        table, = struct.unpack_from("<H", script, lookup + 5)
        header = parse_header(script_file)
        def global_word(index):
            require(index < header.global_count, "Room greeting global index")
            return struct.unpack_from("<I", script_file, header.global_start + 4*index)[0]
        row_ref = global_word(table+2)
        require(global_word(table) == 0x90000008 and row_ref >> 16 == 0xb004,
                "Unknown invitation row address")
        row = row_ref & 0xffff
        require(global_word(row) == 0x90000002 and global_word(row+1) >> 28 == 2
                and global_word(row+2) >> 28 == 7, "Invalid invitation pair")
        invite_motion = global_word(row+1) & 0x0fffffff
        asc = parse_asc(member(group, species + "/" + species + ".ASC"))
        requests = {1, 5, 9, 153, 154, 157, 176, 178, invite_motion}
        activity_ids = set()
        if actor == 0:  # Reached NEKO first conversation and gift reply.
            requests.update((20, 29, 35, 36, 38, 39, 42, 43, 48, 60, 74, 87, 156, 186))
            # With caller argument 1, posture 3 uses this action and waits.
            require(at(0x8d68d, "81 93 00 20 01 6f 34 01"), "Unknown posture-three action")
            requests.add(struct.unpack_from("<H", script, 0x8d68e)[0])
            # 1308E calls 1346F with the door sequence, the following walk,
            # and the looping departure pose. Read each original ASC ID.
            # 1264E applies four scene-state ASCs after adopting the target.
            for pc in (0x130c1, 0x130cc, 0x130ea, 0x126ea, 0x126f5, 0x12700, 0x1270b):
                require(script[pc] == 0x81 and at(pc+3, "20 01 6f 34 01"),
                        "Unknown outing departure actor request")
                requests.add(struct.unpack_from("<H", script, pc+1)[0])
            # These decoded instructions push the requested ASC and nil
            # for builtin 22. Keep the bytecode site, so inline text and
            # other immediate operands cannot introduce a motion.
            for pc in (
                0x29b9, 0x2a75, 0x2a86,  # shared introductions
                0x3f53, 0x3f70, 0x3f8f, 0x41cf, 0x94d3,  # answer reactions
                0x846b1, 0x846ec, 0x84722, 0x84770, 0x847fc, 0x8480f,
                0x84841, 0x84861, 0x8489c, 0x848ff, 0x84951, 0x84986,
                0x849c6, 0x84a76, 0x84a92, 0x84ad9, 0x84b11,
                0x84f75, 0x84f94, 0x84fdd, 0x8501e, 0x8503d,
                0x8508a, 0x850a4, 0x850e3, 0x85118, 0x85149, 0x85160,
                0x85190, 0x851e3, 0x85220, 0x85250,
                0x852a6, 0x85302, 0x8533d, 0x8537c, 0x853a9, 0x853c7, 0x853f8,
            ):
                require(script[pc] == 0x80 and at(pc+2, "84 8b 22 02"),
                        "Unknown room conversation ASC call")
                requests.add(script[pc+1])
            # Include reactions in shared helpers as well as the selected topics.
            # For example 94E8's affirmative branch requests ASC 99 at 94FF.
            instructions = parse_code(script_file)
            topic_blocks, _ = project_topic_bodies(script_file, instructions)
            topic_start = None
            for op in instructions:
                pc = op["code_offset"]
                if pc in topic_blocks:
                    topic_start = pc
                if topic_start is not None and pc >= topic_start + len(topic_blocks[topic_start]):
                    topic_start = None
                if topic_start is not None and op["opcode"] == 0x80 and at(pc+2, "84 8b 22 02"):
                    requests.add(script[pc+1])
            # Scene-zero activity ASCs write actor+58 through key 5 and
            # actor+50 through key 0/motion completion. Key 6 switches the
            # GAME28 flag in either direction. Package the bounded row union;
            # the live script keeps using the complete ordered selector table.
            rows = project_activity_rows(script_file)
            for scene, request, field58, posture, period, mood, flag in struct.iter_unpack("<7H", rows[8:]):
                if scene == 12:
                    # All roof rows remain eligible for the live selector to
                    # evaluate. Its source moves use SEQUENCE_COORD 212..219.
                    activity_ids.add(request)
                elif (scene == 0 and field58 in (0, 1, 3, 11) and posture & 0xe and
                        period & 0x3f and mood & 0xb and flag & 3):
                    activity_ids.add(request)
            requests.update(activity_ids)
            # B834/B848/B85C rebind the standard stand/walk/run DAKs by
            # A690(+54), including quiet-hours mood changes during the return.
            requests.update((2, 3, 4, 6, 7, 8, 10, 11, 12))
            requests.add(13)  # B9B8: second channel of actor mode 3 uses BA60(13).
        commands = {i: asc["blocks"][i-1]["words"] for i in sorted(requests)}
        # 8010 operands refer to AFT motion names. Skip operands and stop at
        # root return; zero padding is outside the ASQ command stream.
        motions = set()
        points = {9, 15, 16}
        widths = {0x8000: 1, 0x8010: 3, 0x8020: 2, 0x8021: 1,
                  0x8030: 3, 0x8031: 1, 0x8040: 2, 0x8041: 1, 0x8042: 1, 0x8043: 1}
        for words in commands.values():
            index = 0
            while index < len(words):
                op = words[index]
                require(op in widths and index + widths[op] <= len(words), "Unsupported cold-room ASC")
                if op == 0x8000:
                    break
                if op == 0x8030 and words[index+1] in (3, 4, 11):
                    points.add(words[index+2])
                if op == 0x8010:
                    motions.add(words[index+1])
                index += widths[op]
        ordered = sorted(motions)
        dak = [species + "/" + asc["names"][i]["name"].upper() + ".DAK" for i in ordered]
        model = mesh_blob(group, species + "/" + body + ".DVS", dak, species + "/GIM")
        control = bytearray(b"CSA5" if actor == 0 else b"CSA4")
        def words(values):
            control.extend(struct.pack("<" + "I" * len(values), *values))
        anchor, = struct.unpack("<I", boot.read(code + 0xd7df8 + 4 * actor, 4))
        words([invite_motion, anchor, len(ordered), *ordered, len(commands)])
        for index, ops in commands.items():
            words([index,len(ops),*ops])
        words([len(activity_ids), *sorted(activity_ids)])
        words([len(points)])
        for index in sorted(points):
            words([index])
            control.extend(coordinates[8*index:8*index+8])
        # BE90/A928 assign the second controller to this species' signed
        # node list. The first controller supplies the complementary group.
        ptr, = struct.unpack("<I", boot.read(data + 0x3d6c + 4*actor, 4))
        nodes = []
        for n in range(128):
            node, = struct.unpack("<h", boot.read(data + ptr + 2*n, 2))
            if node < 0:
                break
            nodes.append(node)
        require(nodes and len(nodes) < 128, "Invalid actor channel node list")
        words([len(nodes), *nodes])
        control.extend(boot.read(data+0x4180,12))
        control.extend(boot.read(code+0xd7ee0,4))
        if actor == 0:
            control.extend(boot.read(data+0x4180+16*12,12))  # D2D0, roof selector 12
        for stride in range(0,32,4):
            ptr, = struct.unpack("<I",boot.read(data+0x4060+32*actor+stride,4))
            segments=[]
            while len(segments)<8:
                d,t=struct.unpack("<ii",boot.read(data+ptr+8*len(segments),8))
                if t==-1: break
                require(d>=0 and t>0,"Invalid room locomotion segment")
                segments.append((d,t))
            require(0<len(segments)<8,"Room locomotion table extent")
            words([len(segments), *(v for pair in segments for v in pair)])
        for ext, payload in (("bin",model),("ascbin",bytes(control))):
            region = Region(payload)
            files[Path(f"assets/game/post-profile/actor-{actor}.{ext}")] = region, region.sha256()
    return files
