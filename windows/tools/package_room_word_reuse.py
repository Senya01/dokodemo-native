"""Ordinary reading and relationship questions using Toro's live vocabulary."""
from bisect import bisect_left

from iso_reader import require
from ksc_format import parse_header


def ordinary_question_filters(data):
    code = data[28:parse_header(data).pool_start]
    filters = []
    # The garment and the two morning activities each use a pair of dictionary
    # predicates, a fallback spelling and the six-argument display-word lookup.
    for pc in (0x66d64, 0x82bff, 0x82c16):
        lookup = code[pc:pc+18]
        require(lookup[0] == lookup[3] == 0x99 and lookup[6] == 0x88 and
                lookup[10:] == bytes.fromhex("84 87 86 20 06 8c 12 00"), "Wrong paired word lookup")
        filters.extend(int.from_bytes(lookup[i+1:i+3], "little") for i in (0, 3))
    for pc in (0x5c35b, 0x3aeb8, 0x6dbb9):
        operands = code[pc:pc+17]
        require(operands[0] == operands[3] == 0x99
                and operands[6:] == bytes.fromhex("99 46 24 85 f4 84 20 06 6e 94 00"),
                "Wrong primary/secondary word filters or modal owner")
        filters.extend((int.from_bytes(operands[1:3], "little"),
                        int.from_bytes(operands[4:6], "little")))
    # These prompts pass a primary filter and a nil secondary filter.
    tail = bytes.fromhex("84 99 46 24 85 f4 84 20 06 6e 94 00")
    for pc in (0x5bbfb, 0x2b4c2, 0x568f1, 0x61e48, 0x1f0f0, 0x55f19, 0x65fe7, 0x3f0f2,
               0x50957, 0x59d64, 0x59dc3, 0x202fc, 0x57fd4, 0x65385, 0x6dc20, 0x5a90d, 0x5f2e2):
        require(code[pc] == 0x99 and code[pc+3:pc+3+len(tail)] == tail,
                "Wrong ordinary question filter or modal owner")
        filters.append(int.from_bytes(code[pc+1:pc+3],"little"))
    # 462B7 additionally supplies a list of excluded written forms to 946E.
    require(code[0x462f0:0x46301] == bytes.fromhex("99 c3 7d 84 99 46 24 85 f4 99 c5 7d 20 06 6e 94 00"),
            "Wrong night ghost word lookup")
    filters.append(int.from_bytes(code[0x462f1:0x462f3], "little"))
    # Seasonal clothing passes primary/secondary filters and a spelling exclusion list.
    require(code[0x34913:0x34926] == bytes.fromhex("99 db 65 99 e4 65 99 46 24 85 f4 99 e6 65 20 06 6e 94 00"),
            "Wrong seasonal clothing lookup")
    filters.extend((0x65db,0x65e4))
    # The two alien-story nouns use ordinary display forms. The third
    # continuation asks for a gesture by its written field, without marking it.
    for pc in (0x4f480, 0x4f495):
        require(code[pc] == 0x99 and code[pc+3:pc+5] == b"\x84\x88"
                and code[pc+8:pc+16] == bytes.fromhex("84 87 86 20 06 8c 12 00"),
                "Wrong alien-story display-word arguments")
        filters.append(int.from_bytes(code[pc+1:pc+3], "little"))
    require(code[0x2b79] == 0x99 and code[0x2b7c:0x2b83] == bytes.fromhex("84 86 20 03 b0 11 00"),
            "Wrong gesture lookup arguments")
    filters.append(int.from_bytes(code[0x2b7a:0x2b7c], "little"))
    # The occupation lesson passes one predicate to 92E2. Its two follow-up
    # nouns are ordinary display-word lookups with distinct fallbacks.
    require(code[0x1d6c8] == 0x99 and code[0x1d6cb:0x1d6d0] == bytes.fromhex("20 01 e2 92 00"),
            "Wrong occupation teaching predicate owner")
    filters.append(int.from_bytes(code[0x1d6c9:0x1d6cb], "little"))
    # Display-word lookups with a primary filter and a fallback string.
    for pc in (0x1d726, 0x1d73b, 0x55f3a, 0x66008, 0x3f113, 0x2e373, 0x75293, 0x3aedb,
               0x59de4, 0x59df9, 0x59e0e, 0x2031d, 0x810ae, 0x810c3, 0x8122a, 0x8543e, 0x85453,
               0x57ff5, 0x653a6, 0x653bb, 0x6dc41, 0x5a92e, 0x5db24, 0x5db39, 0x77844, 0x5f303, 0x3494a):
        require(code[pc] == 0x99 and code[pc+3:pc+5] == b"\x84\x88"
                and code[pc+8:pc+16] == bytes.fromhex("84 87 86 20 06 8c 12 00"),
                "Wrong follow-up word lookup")
        filters.append(int.from_bytes(code[pc+1:pc+3], "little"))
    return filters


def faces_word_lookup(data):
    # 1EEDA first resolves the display noun, even on a return visit.
    code = data[28:parse_header(data).pool_start]
    require(code[0x1eee5] == 0x99 and code[0x1eee8:0x1eeea] == b"\x84\x88"
            and code[0x1eeed] == 0x99 and code[0x1eef0:0x1eef7] == bytes.fromhex("87 86 20 06 8c 12 00"),
            "Wrong faces conversation lookup")
    return (int.from_bytes(code[0x1eee6:0x1eee8], "little"),
            int.from_bytes(code[0x1eeee:0x1eef0], "little"))


def project_word_reuse(data, rows):
    header = parse_header(data)
    code = data[28:header.pool_start]
    require(code[0x4e2c:0x4e36] == bytes.fromhex("99 31 29 84 87 20 03 b0 11 00")
            and code[0x4e45:0x4e50] == bytes.fromhex("90 00 00 20 01 f7 4c 00 87 70 31"),
            "Incorrect ordinary vocabulary selection or return")
    # 4C87 passes the original reading and mode 2 to the shared KSC editor owner.
    require(code[0x4caa:0x4cb7] == bytes.fromhex("91 03 00 90 02 00 80 02 20 03 29 49 00"),
            "Reading correction mode or initial buffer changed")
    spans = ((0x1488,0x1527), (0x41f5,0x4228), (0x4228,0x4254), (0x4254,0x427f),
             (0x427f,0x42b8), (0x42b8,0x42e1), (0x4c87,0x4cf7), (0x4cf7,0x4dd5),
             (0xc256,0xc332),
             # 5C31B reads a matching relationship or opens the existing word editor.
             (0x5c31b,0x5c4ae), (0x821e,0x824a), (0x946e,0x94af), (0x9408,0x946e),
             (0x92f9,0x93ba), (0x9113,0x9159), (0x0fdd,0x1011), (0x90ae,0x9113),
             (0x91ff,0x92e2), (0x9159,0x91ff), (0x824a,0x82d6), (0x82d6,0x83dc),
             (0x83dc,0x8521), (0x8521,0x85d7), (0x85d7,0x8658), (0x8658,0x870f),
             (0x3eba,0x3f43), (0x249f,0x24f4), (0x24f4,0x2573), (0x2573,0x25b2),
             # A fresh person from the conditional lesson can be famous or ordinary.
             (0x7e75,0x7f28), (0x5596,0x564d),
             # Same lookup/teaching owner, with a body-part filter and gender in its prompt.
             (0x5bbb9,0x5bcba), (0x22f5,0x2314),
             (0x2b484,0x2b5bc), (0x568aa,0x56a4a), (0x22d6,0x22f5), (0x61e0a,0x61f5e),
             (0x1eeda,0x1f2cb), (0x181a,0x186a), (0x186a,0x18a5), (0x26a5,0x26f4),
             (0x4f47b,0x4f565), (0x9972,0x99fb), (0x98f6,0x9972), (0x97bd,0x98f6),
             (0x2b74,0x2bc9), (0x2b3a,0x2b56), (0x2b56,0x2b74),
             # Occupation lesson, its first/repeat greeting and source criteria reducer.
             (0x1d66d,0x1d8b0), (0x138aa,0x138d5), (0x9bfe,0x9c8a), (0x92e2,0x92f9),
             # 7B32 calls 5358 for category 8. The selected occupation bit
             # (field 8 mask 4) returns the row immediately.
             (0x5358,0x545c), (0x59d,0x688), (0x55ee5,0x5605c),
             (0x65faa,0x66126), (0x2919,0x2945),
             # Future-life conversation with the player's favourite celebrity and a vehicle.
             (0x3f0b0,0x3f30d),
             # Outfit for a date, with a wearable, meeting place and shared colour draw.
             (0x3ae67,0x3b093),
             # The earthquake story checks a previously answered word trait.
             (0x5090b,0x50ad8), (0x9cb4,0x9d2c), (0x298f,0x29b7),
             # Toro tries to reconcile two people, using independently selected words.
             (0x59d30,0x59fcd),
             # Toro describes what people were doing on his first visit to a crowded place.
             (0x202b8,0x20484),
             # Where the player often goes, and the activity seen there.
             (0x57f95,0x580f3),
             # Vehicles at the player's familiar place and the couples' activity.
             (0x65345,0x65470),
             # Two relationship lookups and Toro's question about the selected action.
             (0x6db73,0x6dd95),
             # A friend buys something by mail and discovers a counterfeit.
             (0x5a8c7,0x5aaa4),
             # Toro talks about resting against a loved one after an activity.
             (0x5db1f,0x5dc31),
             # Staying in a favourite person's room until morning.
             (0x5f2a5,0x5f3ad),
             (0x348d6,0x34b85), (0x2262,0x2275),
             # The night ghost and the prompted person-class question.
             (0x462b7,0x46483), (0x545c,0x5596),
             # Time flies: read the clock once, ask the time, then refer to the player's body part.
             (0x7783f,0x77920), (0x228f,0x22d6), (0x756f1,0x7573a),
             # Men's or women's version of the selected garment.
             (0x66d5f,0x66ebb),
             # Scheduled morning conversation, its prelude and the two-string choice.
             (0x82bfa,0x82cb0), (0x8304e,0x830aa), (0x823b6,0x823fc), (0x2945,0x2962),
             # November conversations about voices in the grass and autumn colours.
             (0x810a0,0x8121c), (0x8121c,0x812e3),
             # The nuisance-call story changes Toro's mood only on the second answer.
             (0x2e36e,0x2e522),
             # A magazine release, its calendar question and the date accessors.
             (0x7528e,0x7531c), (0x74bbb,0x74c0c), (0x2246,0x2254), (0x2254,0x2262),
             (0x2216,0x2227), (0x2227,0x2238), (0x21d2,0x21ec), (0x21ec,0x2205))
    addresses = [row["code_offset"] for row in rows]
    blocks, strings = {}, {}
    for start, end in spans:
        require(code[start] == code[end] == 0x30 and code[end-1] == 0x31,
                "Invalid bounds of the reading question or its response")
        blocks[start] = code[start:end]
        for row in rows[bisect_left(addresses,start):bisect_left(addresses,end)]:
            for ref in row["references"]:
                if ref["kind"] == "pool":
                    index = ref["target"]
                    at = header.pool_start + index
                    strings[index] = data[at:data.index(0,at,header.pool_end)]
    return blocks, strings
