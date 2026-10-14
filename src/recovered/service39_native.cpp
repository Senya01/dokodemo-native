#include "service39_native.h"
#include <limits>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
uint32_t address(Service39Address value) {
    const int64_t a = static_cast<int64_t>(value.table_base) + value.byte_offset;
    require(a >= 0 && a <= UINT32_MAX, "Service39 reference address wraps");
    const auto result = static_cast<uint32_t>(a);
    require(result % 4 == 0, "Service39 reference word unaligned");
    return result;
}
uint8_t mapped_byte(const Service39Input& input, uint32_t a) {
    bool found = false;
    uint8_t result = 0;
    for (const auto& map : input.mappings) {
        require(map.bytes.size() <= UINT32_MAX &&
                static_cast<uint64_t>(map.address) + map.bytes.size() <= 0x100000000ULL,
                "Service39 mapped window wraps");
        if (a < map.address || static_cast<uint64_t>(a - map.address) >= map.bytes.size()) continue;
        const uint8_t value = map.bytes[a - map.address];
        require(!found || result == value, "Service39 overlapping readable mirrors disagree");
        result = value;
        found = true;
    }
    require(found, "Service39 explicit readable mapping missing");
    return result;
}
uint32_t mapped_word(const Service39Input& input, uint32_t a) {
    require(a <= UINT32_MAX - 3, "Service39 reference word wraps");
    uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) result |= uint32_t(mapped_byte(input, a + i)) << (8 * i);
    return result;
}
std::vector<uint8_t> source_bytes(const StringArena& arena, const Service39Input& input,
                                  uint32_t pointer, bool owned) {
    uint32_t end = 0;
    for (const auto& live : arena.live_blocks()) {
        const uint32_t next = arena.word(live.first - 12);
        if (pointer >= live.first && pointer < next) { end = next; break; }
    }
    require(!owned || arena.live_blocks().count(pointer) != 0,
            "Service39 tag6 requires exact live owned payload");
    if (end) return arena.bytes(pointer, end - pointer);
    require(!owned, "Service39 owned source lifetime missing");
    // External borrowed windows are explicit. Read only as far as14F90 reads,
    // including the second byte for every high lead and the next lead byte.
    std::vector<uint8_t> result;
    uint32_t at = pointer;
    for (;;) {
        const uint8_t lead = mapped_byte(input, at);
        result.push_back(lead);
        if (!lead) return result;
        require(at != UINT32_MAX, "Service39 borrowed pointer wraps");
        ++at;
        if (lead >= 128) {
            result.push_back(mapped_byte(input, at));
            require(at != UINT32_MAX, "Service39 borrowed trail pointer wraps");
            ++at;
        }
    }
}
}
int32_t Service39Character(uint32_t code) {
    if (code - 48u < 10u || code - 65u < 26u) return static_cast<int32_t>(code);
    if (code - 97u < 26u) return static_cast<int32_t>(code - 32u);
    if (code == 45) return 0x815B;
    if (code - 0x824Fu < 10u) return static_cast<int32_t>(code) - 0x824C1;
    if (code - 0x8260u < 26u) return static_cast<int32_t>(code) - 0x821F;
    if (code - 0x8281u < 26u) return static_cast<int32_t>(code) - 0x821F;
    if (code - 0x829Fu < 83u) return static_cast<int32_t>(code);
    if (code - 0x8340u < 63u) return static_cast<int32_t>(code) - 161;
    if (code - 0x8380u < 20u) return static_cast<int32_t>(code) - 162;
    if (code == 0x8394) return 0x82D4;
    if (code == 0x8395 || code == 0x8396) return 0;
    if (code == 0x815B || code == 0x817C || code == 0x8160) return 0x815B;
    // Original signed SLT of889E against a1, then -(!comparison).
    const int64_t signed_code = code & 0x80000000u ? static_cast<int64_t>(code) - 0x100000000LL : code;
    return signed_code > 0x889E ? 0 : -1;
}
Service39Normalized Service39Normalize(const std::vector<uint8_t>& source, bool filter) {
    Service39Normalized result;
    std::size_t at = 0;
    auto read = [&]() {
        require(at < source.size(), "Service39 normalizer source read crosses mapped bytes");
        require(result.source_bytes_read != UINT32_MAX, "Service39 source read count overflow");
        ++result.source_bytes_read;
        return source[at++];
    };
    auto put = [&](uint8_t byte) {
        require(result.bytes.size() < 1024, "Service39 original router stack1024 overflow outside native domain");
        result.bytes.push_back(byte);
    };
    for (;;) {
        const uint8_t lead = read();
        if (!lead) { put(0); return result; }
        uint32_t code = lead;
        if (lead >= 128) code = (code << 8) | read();
        const int32_t converted = Service39Character(code);
        if (!converted) {
            result.all_converted = false;
            if (!filter) { put(static_cast<uint8_t>(code >> 8)); put(static_cast<uint8_t>(code)); }
            continue;
        }
        if (converted <= 0) continue;
        if (result.bytes.empty() && (converted == 0x815B || converted == 0x82C1)) continue;
        if (converted >= 256) put(static_cast<uint8_t>(static_cast<uint32_t>(converted) >> 8));
        put(static_cast<uint8_t>(converted));
    }
}
Service39Observation Service39Apply(StringArena& arena, const Service39Input& input, uint32_t& result) {
    require(input.result_address == 0,
            "Service39 requires explicitly distinct abstract result cell; all physical result addresses refuse");
    require(input.enabled && input.reference_readable && input.router_context_readable && arena.ready(),
            "Service39 explicit router/reference/string readiness premises missing");
    const uint32_t ref = address(input.reference);
    Service39Observation observation;
    observation.reference = input.reference;
    observation.reference_address = ref;
    observation.arena_before = arena.summary();
    observation.cursor_before = arena.conversion_cursor();
    const auto control = arena.control_bytes();
    auto control_word = [&](unsigned at) {
        uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= uint32_t(control[at + i]) << (8 * i);
        return value;
    };
    const uint32_t heap_base = control_word(4);
    const uint64_t heap_end = static_cast<uint64_t>(heap_base) + control_word(0);
    for (const auto& map : input.mappings) {
        const uint64_t end = static_cast<uint64_t>(map.address) + map.bytes.size();
        require(end <= heap_base || map.address >= heap_end,
                "Service39 external/table mapping overlaps authoritative STRING heap");
    }
    StringArena candidate = arena;
    //1C1B0..1C1CC retags and zeros payload WITHOUT freeing previous tag6.
    //That abandoned owned block remains live, even when it aliases the input.
    uint32_t candidate_result = observation.prologue_result;
    const uint32_t word = mapped_word(input, ref);
    require(word >> 28 == 5 || word >> 28 == 6, "Service39 getter outside recovered tag5/6 domain");
    observation.reference_word = word;
    observation.source_pointer = candidate.resolve75778(word);
    observation.normalized = Service39Normalize(source_bytes(candidate, input, observation.source_pointer, word >> 28 == 6));
    candidate.release7515C(candidate_result);
    auto padded = observation.normalized.bytes;
    padded.resize((padded.size() + 3) & ~std::size_t(3), uint8_t(0));
    candidate.assign_mapped753E0(candidate_result, padded, 0x07FFEBD0);
    observation.result_word = candidate_result;
    observation.cursor_after = candidate.conversion_cursor();
    observation.arena_after = candidate.summary();
    arena = candidate;
    result = candidate_result;
    return observation;
}
} // namespace dokodemo::recovered
