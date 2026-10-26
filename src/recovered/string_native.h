#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dokodemo::recovered {
// Conditional source memory premises. These fields do not prove the original
// arena constructor/producer ran, and defaults authorize no source string read.
struct StringFixture {
    bool enabled = false;
    bool bss_window_mapped = false;       // canonical BOOT BSS10E490, raw addend2A910.
    bool heap_ready = false;
    bool memory_disjoint = false;
    bool allocator_policy_known = false;
    bool forward_positive = true;        // source manager+16,6395C/63318.
    uint32_t common_base = 0x3000;
    uint32_t heap_base = 0x400000;
    uint32_t heap_size = 0x40000;
    uint32_t conversion_cursor = 0;      // explicit mapped75778 cursor, modulo4.
    std::vector<uint8_t> initial_heap;    // explicit mapped bytes, including readable padding.
    std::vector<uint8_t> bss_window;      // exactly16 bytes, valid NUL and aligned strcpy tail.
};

struct StringArenaSummary {
    bool known = false;
    uint32_t allocations = 0, reallocations = 0, releases = 0, copies = 0;
    uint32_t live_allocations = 0;
    uint32_t conversion_cursor = 0;
    std::string sha256;
    bool operator==(const StringArenaSummary& other) const;
};

// Real portable byte storage and source allocator policy, with checked virtual
// addresses. No PSP instruction execution or host pointer reinterpretation.
// Copy-on-write makes StartupProbe's protective candidate transaction isolated.
// The component's valid success paths preserve source pointer/free-list policy;
// checked refusal differs from source partial writes/assertion/fault behavior.
class StringArena {
public:
    StringArena() = default;
    explicit StringArena(const StringFixture& fixture);
    // Fresh NONMATCHING Windows storage, not imported PSP readiness premises.
    // Owns/initializes the actual byte buffer and verifies allocator headers.
    static StringArena WindowsOwned(uint32_t base, uint32_t size, uint32_t common_base);
    // Import an explicit current mapped arena, preserving all bytes and live
    // block identities. The caller supplies current state, never fresh BSS.
    static StringArena FromMappedStorage(const StringFixture& fixture,
        const std::vector<uint8_t>& control, const std::vector<uint8_t>& heap);
    bool ready() const;
    uint32_t common_base() const;
    uint32_t allocate(int32_t bytes);      //63318,62FA8/630F0. Signed direction selector.
    uint32_t reallocate(uint32_t pointer, uint32_t bytes); //63680 valid ordinary blocks.
    void free(uint32_t pointer);          //63378; NULL is a source no-op.
    uint32_t allocation_size(uint32_t pointer) const;
    std::vector<uint8_t> bytes(uint32_t pointer, uint32_t count) const;
    void store(uint32_t pointer, const std::vector<uint8_t>& bytes);
    uint32_t word(uint32_t pointer) const;
    void store_word(uint32_t pointer, uint32_t word);
    uint32_t pointer(uint32_t tagged_word) const; //signed28 relative to common base.
    uint32_t owned_word(uint32_t pointer) const;
    uint32_t resolve75778(uint32_t tagged_word);
    uint32_t conversion_cursor() const;
    void release7515C(uint32_t& value);
    void assign753E0(uint32_t& value, uint32_t source_pointer);
    void copy7581C(uint32_t& destination, uint32_t source_word);
    void assign_mapped753E0(uint32_t& value, const std::vector<uint8_t>& source,
                          uint32_t source_address = 0x2A910);
    StringArenaSummary summary() const;
    // Exact compact source-mapped control/heap bytes for proof harnesses.
    std::vector<uint8_t> control_bytes() const;
    std::vector<uint8_t> heap_bytes() const;
    const std::map<uint32_t, uint32_t>& live_blocks() const;
private:
    struct Storage;
    std::shared_ptr<Storage> storage_;
    void unique();
    void initialize(uint32_t base,uint32_t size,uint32_t common,bool forward,
                    const std::vector<uint8_t>& initial_heap,uint32_t cursor);
    uint32_t read(uint32_t address, unsigned width = 4) const;
    void write(uint32_t address, uint32_t value, unsigned width = 4);
    uint32_t rounded(uint32_t bytes) const;
    uint32_t allocation_id() const;
    uint32_t allocate_forward(uint32_t bytes);
    uint32_t allocate_backward(uint32_t bytes);
    void init_header(uint32_t header, uint32_t prev, uint32_t next);
    void insert_free(uint32_t header);
    void move_free(uint32_t old_header, uint32_t new_header);
    uint32_t previous_free(uint32_t header) const;
    void validate_owned(uint32_t pointer) const;
    void assign(uint32_t& value, const std::vector<uint8_t>& source, uint32_t source_address,
                bool source_owned, uint32_t source_pointer);
};

// Host1D43C selector0 only; all other selectors remain explicit barriers.
void StringService20Selector0(StringArena& arena, const StringFixture& fixture,
                             uint32_t selector, uint32_t& result);
} // namespace dokodemo::recovered
