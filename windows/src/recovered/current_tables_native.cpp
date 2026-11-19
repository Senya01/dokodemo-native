#include "current_tables_native.h"
#include <array>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace dokodemo::recovered {
namespace {
void need(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
int32_t signed28(uint32_t w) {
    const uint32_t p = w & 0x0fffffff;
    return p & 0x08000000 ? static_cast<int32_t>(p) - 0x10000000 : static_cast<int32_t>(p);
}
int32_t signed16(uint32_t w) { return w & 0x8000 ? static_cast<int32_t>(w & 0xffff) - 65536 : static_cast<int32_t>(w & 0xffff); }
uint32_t add(uint32_t a, uint32_t b) { return a + b; }
constexpr uint32_t base_cell = 0x1c93f8, globals_cell = 0x1c949c, globals_count_cell = 0x1c94a0;
constexpr uint64_t internal_identity = UINT64_MAX - 16;
CurrentTablesRegion scalar_region(uint32_t address, uint32_t width, uint32_t word, uint64_t identity) {
    CurrentTablesRegion r; r.base = address; r.identity = identity;
    r.bytes.resize(width); r.readable.assign(width, 1); r.writable.assign(width, 1);
    for (uint32_t i = 0; i < width; ++i) r.bytes[i] = static_cast<uint8_t>(word >> (8 * i));
    return r;
}
}

struct CurrentTablesMemory::State {
    struct Data {
        std::vector<uint8_t> owned_bytes, readable, writable, owned_initialized;
        std::shared_ptr<CurrentTablesStorageImage> shared_image;
        std::vector<uint8_t>& bytes() { return shared_image ? shared_image->bytes : owned_bytes; }
        const std::vector<uint8_t>& bytes() const { return shared_image ? shared_image->bytes : owned_bytes; }
        std::vector<uint8_t>& initialized() { return shared_image ? shared_image->initialized : owned_initialized; }
        const std::vector<uint8_t>& initialized() const { return shared_image ? shared_image->initialized : owned_initialized; }
    };
    struct Region { uint32_t base; uint64_t identity; bool live; std::shared_ptr<Data> data; };
    struct Log { CurrentTablesEffect effect; std::shared_ptr<const Log> left, right; uint64_t size = 1; };
    std::vector<Region> regions;
    std::array<std::shared_ptr<const Log>, 64> log{};
    uint64_t count = 0;
    size_t locate(uint32_t address, uint32_t width) const {
        need(width == 1 || width == 2 || width == 4, "CURRENT width must be1/2/4");
        need(address % width == 0, "CURRENT unaligned physical access");
        need(uint64_t(address) + width <= uint64_t(UINT32_MAX) + 1, "CURRENT physical access wraps");
        for (size_t i = 0; i < regions.size(); ++i) {
            const auto& r = regions[i];
            if (address >= r.base && uint64_t(address) + width <= uint64_t(r.base) + r.data->bytes().size()) {
                need(r.live && (!r.data->shared_image || r.data->shared_image->live), "CURRENT borrowed physical lifetime ended"); return i;
            }
        }
        throw std::runtime_error("CURRENT physical access outside mapped bytes");
    }
    size_t by_identity(uint64_t identity) const {
        for (size_t i = 0; i < regions.size(); ++i) if (regions[i].identity == identity) return i;
        throw std::runtime_error("CURRENT unknown allocation identity");
    }
    // Native snapshot COW, not a source clone. Detach all regions borrowing
    // the same backing once. External owner handles alone do not cause COW:
    // live mutable borrows deliberately write through until Data is shared.
    void prepare_write(size_t index) {
        const auto image = regions[index].data->shared_image;
        if (!image) {
            if (regions[index].data.use_count() != 1)
                regions[index].data = std::make_shared<Data>(*regions[index].data);
            return;
        }
        bool shared = false;
        for (const auto& r : regions)
            if (r.data->shared_image == image && r.data.use_count() != 1) shared = true;
        if (!shared) return;
        auto clone = std::make_shared<CurrentTablesStorageImage>(*image);
        for (auto& r : regions) if (r.data->shared_image == image) {
            r.data = std::make_shared<Data>(*r.data);
            r.data->shared_image = clone;
        }
    }
    void append(CurrentTablesEffect effect) {
        need(count != UINT64_MAX, "CURRENT effect counter overflow");
        auto node = std::make_shared<Log>(); node->effect = effect;
        size_t level = 0;
        while (log[level]) {
            auto parent = std::make_shared<Log>(); parent->left = log[level]; parent->right = node; parent->size = parent->left->size + node->size;
            log[level].reset(); node = std::move(parent); ++level;
        }
        log[level] = std::move(node); ++count;
    }
    static void collect(const std::shared_ptr<const Log>& node, std::vector<CurrentTablesEffect>& out) {
        if (!node) return;
        if (node->left) { collect(node->left, out); collect(node->right, out); }
        else out.push_back(node->effect);
    }
    static void collect_from(const std::shared_ptr<const Log>& node, uint64_t& skip, std::vector<CurrentTablesEffect>& out) {
        if (!node) return;
        if (skip >= node->size) { skip -= node->size; return; }
        if (node->left) { collect_from(node->left, skip, out); collect_from(node->right, skip, out); }
        else out.push_back(node->effect);
    }
};

CurrentTablesMemory::CurrentTablesMemory() : state_(std::make_shared<State>()) {}
void CurrentTablesMemory::detach() { if (state_.use_count() != 1) state_ = std::make_shared<State>(*state_); }
void CurrentTablesMemory::Map(CurrentTablesRegion input) {
    need(input.identity != 0 && !input.bytes.empty(), "CURRENT mapping needs nonzero identity and bytes");
    need(input.readable.size() == input.bytes.size() && input.writable.size() == input.bytes.size(), "CURRENT exact byte masks required");
    need(uint64_t(input.base) + input.bytes.size() <= uint64_t(UINT32_MAX) + 1, "CURRENT mapping wraps");
    for (size_t i = 0; i < input.bytes.size(); ++i)
        need(input.readable[i] <= 1 && input.writable[i] <= 1, "CURRENT masks must be0/1");
    for (const auto& r : state_->regions) {
        need(r.identity != input.identity, "CURRENT duplicate allocation identity");
        need(uint64_t(input.base) + input.bytes.size() <= r.base || uint64_t(r.base) + r.data->bytes().size() <= input.base, "CURRENT ambiguous physical region overlap");
    }
    auto data = std::make_shared<State::Data>(); data->owned_bytes = std::move(input.bytes);
    data->readable = std::move(input.readable); data->writable = std::move(input.writable);
    // Preserve the effective known/readable mask of prior owned mappings.
    data->owned_initialized = data->readable;
    detach(); state_->regions.push_back({input.base, input.identity, input.live, std::move(data)});
}
void CurrentTablesMemory::MapSharedImage(std::shared_ptr<CurrentTablesStorageImage> image,
                                        std::vector<uint8_t> readable, std::vector<uint8_t> writable) {
    need(image && image->live && image->initialized.size() == image->bytes.size(), "CURRENT shared image initialized-mask mismatch");
    need(std::all_of(image->initialized.begin(), image->initialized.end(), [](uint8_t x) { return x <= 1; }), "CURRENT initialized mask must be0/1");
    Map({image->source_address, image->identity, image->bytes, std::move(readable), std::move(writable), true});
    state_->regions.back().data->shared_image = std::move(image);
    state_->regions.back().data->owned_bytes.clear();
    state_->regions.back().data->owned_initialized.clear();
}
void CurrentTablesMemory::BindSharedImage(uint64_t identity, std::shared_ptr<CurrentTablesStorageImage> image) {
    const size_t index = state_->by_identity(identity); const auto& r = state_->regions[index];
    need(r.live && image && image->live, "CURRENT bind requires live region and storage");
    need(image->source_address == r.base && image->identity == r.identity &&
         image->bytes.size() == r.data->bytes().size() && image->bytes == r.data->bytes(),
         "CURRENT bind storage geometry/bytes differ");
    need(image->initialized.size() == image->bytes.size() &&
         std::all_of(image->initialized.begin(), image->initialized.end(), [](uint8_t x) { return x <= 1; }) &&
         image->initialized == r.data->initialized(), "CURRENT bind initialized knowledge differs");
    if (r.data->shared_image) {
        need(r.data->shared_image == image, "CURRENT bind cannot replace shared storage");
        return;
    }
    detach(); auto& target = state_->regions[index];
    if (target.data.use_count() != 1) target.data = std::make_shared<State::Data>(*target.data);
    target.data->shared_image = std::move(image);
    target.data->owned_bytes.clear(); target.data->owned_initialized.clear();
}
std::shared_ptr<CurrentTablesStorageImage> CurrentTablesMemory::SharedImage(uint64_t identity) const {
    return state_->regions[state_->by_identity(identity)].data->shared_image;
}
CurrentTablesMemory CurrentTablesMemory::Fork() const {
    CurrentTablesMemory out = *this; out.detach();
    std::vector<std::pair<std::shared_ptr<CurrentTablesStorageImage>, std::shared_ptr<CurrentTablesStorageImage>>> clones;
    for (auto& r : out.state_->regions) if (r.data->shared_image) {
        const auto original = r.data->shared_image;
        auto found = std::find_if(clones.begin(), clones.end(), [&](const auto& item) { return item.first == original; });
        if (found == clones.end()) {
            clones.emplace_back(original, std::make_shared<CurrentTablesStorageImage>(*original));
            found = clones.end() - 1;
        }
        r.data = std::make_shared<State::Data>(*r.data);
        r.data->shared_image = found->second;
    }
    return out;
}
uint32_t CurrentTablesMemory::Peek(uint32_t address, uint32_t width) const {
    const auto& r = state_->regions[state_->locate(address, width)];
    const size_t offset = address - r.base; uint32_t value = 0;
    for (uint32_t i = 0; i < width; ++i) {
        need(r.data->readable[offset + i] != 0, "CURRENT unreadable physical byte");
        need(r.data->initialized()[offset + i] != 0, "CURRENT uninitialized storage byte");
        value |= uint32_t(r.data->bytes()[offset + i]) << (8 * i);
    }
    return value;
}
uint32_t CurrentTablesMemory::Read(uint32_t address, uint32_t width) {
    const uint32_t value = Peek(address, width); const uint64_t identity = Identity(address, width);
    detach(); state_->append({CurrentTablesEffect::Kind::Read, address, width, 0, value, identity}); return value;
}
void CurrentTablesMemory::Write(uint32_t address, uint32_t width, uint32_t value) {
    const size_t index = state_->locate(address, width); const auto& original = state_->regions[index];
    const size_t offset = address - original.base; uint32_t before = 0;
    for (uint32_t i = 0; i < width; ++i) {
        need(original.data->writable[offset + i] != 0, "CURRENT readonly physical byte");
        before |= uint32_t(original.data->bytes()[offset + i]) << (8 * i);
    }
    detach(); state_->prepare_write(index); auto& r = state_->regions[index];
    for (uint32_t i = 0; i < width; ++i) {
        r.data->bytes()[offset + i] = static_cast<uint8_t>(value >> (8 * i));
        r.data->initialized()[offset + i] = 1;
    }
    if (width < 4) value &= (1u << (8 * width)) - 1;
    state_->append({CurrentTablesEffect::Kind::Write, address, width, before, value, r.identity});
}
void CurrentTablesMemory::EndLifetime(uint64_t identity) {
    const size_t index = state_->by_identity(identity); need(state_->regions[index].live, "CURRENT lifetime already ended");
    detach(); state_->prepare_write(index); auto& r = state_->regions[index]; r.live = false;
    if (r.data->shared_image) r.data->shared_image->live = false;
    state_->append({CurrentTablesEffect::Kind::LifetimeEnd, r.base, 0, 0, 0, identity});
}
bool CurrentTablesMemory::Live(uint64_t identity) const {
    for (const auto& r : state_->regions) if (r.identity == identity) return r.live && (!r.data->shared_image || r.data->shared_image->live);
    return false;
}
uint64_t CurrentTablesMemory::Identity(uint32_t address, uint32_t width) const { return state_->regions[state_->locate(address, width)].identity; }
const std::vector<uint8_t>& CurrentTablesMemory::Bytes(uint64_t identity) const { return state_->regions[state_->by_identity(identity)].data->bytes(); }
const std::vector<uint8_t>& CurrentTablesMemory::Readable(uint64_t identity) const { return state_->regions[state_->by_identity(identity)].data->readable; }
const std::vector<uint8_t>& CurrentTablesMemory::Writable(uint64_t identity) const { return state_->regions[state_->by_identity(identity)].data->writable; }
const std::vector<uint8_t>& CurrentTablesMemory::Initialized(uint64_t identity) const { return state_->regions[state_->by_identity(identity)].data->initialized(); }
std::vector<CurrentTablesRegion> CurrentTablesMemory::Capture() const {
    std::vector<CurrentTablesRegion> out;
    for (const auto& r : state_->regions) out.push_back({r.base, r.identity, r.data->bytes(), r.data->readable, r.data->writable, r.live && (!r.data->shared_image || r.data->shared_image->live)});
    return out;
}
std::vector<CurrentTablesSnapshotRegion> CurrentTablesMemory::CaptureWithInitialized() const {
    std::vector<CurrentTablesSnapshotRegion> out;
    for (const auto& r : state_->regions) out.push_back({r.base, r.identity, r.data->bytes(), r.data->readable, r.data->writable,
        r.data->initialized(), r.live && (!r.data->shared_image || r.data->shared_image->live)});
    return out;
}
std::vector<CurrentTablesEffect> CurrentTablesMemory::effects() const {
    std::vector<CurrentTablesEffect> out; out.reserve(static_cast<size_t>(state_->count));
    for (size_t i = state_->log.size(); i > 0; --i) State::collect(state_->log[i - 1], out);
    return out;
}
std::vector<CurrentTablesEffect> CurrentTablesMemory::effects_from(uint64_t first) const {
    need(first <= state_->count, "CURRENT effect start beyond log");
    std::vector<CurrentTablesEffect> out; out.reserve(static_cast<size_t>(state_->count - first));
    for (size_t i = state_->log.size(); i > 0; --i) State::collect_from(state_->log[i - 1], first, out);
    return out;
}
uint64_t CurrentTablesMemory::effect_count() const { return state_->count; }

void CurrentTablesRebase66DD0(CurrentTablesMemory& m, uint32_t ctx, uint32_t words, int32_t count, uint32_t module_base) {
    const uint32_t refcell = module_base + base_cell, globalcell = module_base + globals_cell;
    if (m.Read(refcell, 4) == 0) m.Write(refcell, 4, m.Read(add(ctx, 0x474), 4));
    for (int32_t i = 0; i < count; ++i, words += 4) {
        const uint32_t word = m.Read(words, 4), tag = word >> 28;
        if (tag == 5) {
            const uint32_t pool = m.Read(add(ctx, 0x474), 4), base = m.Read(refcell, 4);
            const uint32_t payload = add(pool, static_cast<uint32_t>(signed28(word))) - base;
            m.Write(words, 4, (word & 0xf0000000) | (payload & 0x0fffffff));
        } else if (tag == 11) {
            const int32_t payload = signed28(word);
            const int32_t selector = payload >= 0 ? payload / 65536 : (payload - 65535) / 65536;
            const uint32_t index = word & 0xffff;
            uint32_t pointer = 0;
            if (selector == 1) {
                const int32_t fp = signed16(m.Read(add(ctx, 0x5e), 2));
                pointer = m.Read(add(ctx, 0x20), 4) + 4 * (static_cast<uint32_t>(fp) + index);
            } else if (selector == 2) pointer = m.Read(add(ctx, 0x24), 4) + 4 * index;
            else if (selector == 3) pointer = m.Read(add(ctx, 0x28), 4) + 4 * index;
            else pointer = m.Read(globalcell, 4) + 4 * index;
            const uint32_t base = m.Read(refcell, 4), reread = m.Read(words, 4);
            m.Write(words, 4, (reread & 0xf0000000) | ((pointer - base) & 0x0fffffff));
        }
    }
}

void CurrentTablesCopyC2A48(CurrentTablesMemory& m, uint32_t destination, uint32_t source, uint32_t count) {
    need(count <= INT32_MAX, "CURRENT copy negative-length source domain excluded");
    if (((destination | source) & 3) == 0) {
        for (uint32_t blocks = count >> 4; blocks > 0; --blocks) {
            const uint32_t a = m.Read(source, 4), b = m.Read(source + 4, 4);
            const uint32_t c = m.Read(source + 8, 4), d = m.Read(source + 12, 4);
            m.Write(destination, 4, a); m.Write(destination + 4, 4, b);
            m.Write(destination + 8, 4, c); m.Write(destination + 12, 4, d);
            source += 16; destination += 16;
        }
        count &= 15;
        for (uint32_t words = count >> 2; words > 0; --words) {
            const uint32_t value = m.Read(source, 4); source += 4;
            m.Write(destination, 4, value); destination += 4;
        }
        count &= 3;
    } else {
        for (uint32_t words = count >> 2; words > 0; --words) {
            uint32_t value = m.Read(source + 3, 1) << 8;
            value = (value | m.Read(source + 2, 1)) << 8;
            value = (value | m.Read(source + 1, 1)) << 8;
            value |= m.Read(source, 1); source += 4;
            m.Write(destination, 1, value); value >>= 8;
            m.Write(destination + 1, 1, value); value >>= 8;
            m.Write(destination + 2, 1, value); value >>= 8;
            m.Write(destination + 3, 1, value); destination += 4;
        }
        count &= 3;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t value = m.Read(source, 1); ++source;
        m.Write(destination, 1, value); ++destination;
    }
}

CurrentTablesOwner CurrentTablesOwner::Load(const CurrentTablesInput& input) {
    CurrentTablesOwner out; const auto& p = input.mapping;
    need(p.allocator_boundary_available && p.borrowed_image_lifetime_available, "CURRENT allocator/storage boundary unavailable");
    need(input.image.size() >= 28 && input.image.size() <= UINT32_MAX, "CURRENT bounded KSC image required");
    need(input.source_sha256.size() == 64 && std::all_of(input.source_sha256.begin(), input.source_sha256.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }), "CURRENT source SHA metadata required");
    out.mapping_ = p; out.source_sha256_ = input.source_sha256;
    need(input.borrowed_image && input.borrowed_image->live && input.borrowed_image->source_address == p.image_base && input.borrowed_image->identity == p.image_identity && input.borrowed_image->bytes == input.image, "CURRENT live SAME storage image required");
    out.memory_.MapSharedImage(input.borrowed_image, input.readable, input.writable);
    auto mapped = [&](const CurrentTablesRegion& r, uint32_t base, uint64_t identity, uint32_t minimum) {
        need(r.base == base && r.identity == identity && r.live && r.bytes.size() >= minimum, "CURRENT exact supplied region mismatch");
        out.memory_.Map(r);
    };
    mapped(input.context, p.context_base, p.context_identity, 0x478);
    need(p.stack_count > 0 && p.stack_count <= 512, "CURRENT explicit stack bound required");
    mapped(input.stack, p.stack_base, p.stack_identity, 4 * p.stack_count);
    need(out.memory_.Peek(add(p.context_base, 0x20), 4) == p.stack_base && signed16(out.memory_.Peek(add(p.context_base, 0x5e), 2)) == p.frame_pointer, "CURRENT incoming stack/FP mapping differs");
    const uint32_t refcell = p.module_base + base_cell, globalcell = p.module_base + globals_cell, countcell = p.module_base + globals_count_cell;
    out.memory_.Map(scalar_region(refcell, 4, p.reference_base, internal_identity));
    out.memory_.Map(scalar_region(globalcell, 4, p.shared_globals_base, internal_identity + 1));
    out.memory_.Map(scalar_region(countcell, 2, p.shared_globals_count, internal_identity + 2));
    if (p.shared_globals_base != 0) {
        const uint64_t limit = uint64_t(p.shared_globals_base) + 4u * p.shared_globals_count;
        if (p.shared_globals_base >= p.image_base && limit <= uint64_t(p.image_base) + input.image.size()) {
            need(p.globals_identity == p.image_identity, "CURRENT same-image globals must keep SAME identity");
        } else if (input.retained_global_image) {
            const auto& g = input.retained_global_image;
            need(g->live && g->identity == p.globals_identity && p.shared_globals_base >= g->source_address && limit <= uint64_t(g->source_address) + g->bytes.size(), "CURRENT retained global lease differs");
            need(input.retained_globals.base == g->source_address && input.retained_globals.identity == g->identity && input.retained_globals.bytes == g->bytes, "CURRENT retained global masks differ");
            out.memory_.MapSharedImage(g, input.retained_globals.readable, input.retained_globals.writable);
        } else {
            throw std::runtime_error("CURRENT retained globals need a live shared image lease");
        }
    }
    auto& m = out.memory_; const uint32_t ctx = p.context_base, image = p.image_base;
    //66FFC..67018 publishes image/callback before checking/changing marker.
    m.Write(add(ctx, 0x14), 4, p.callback); m.Write(add(ctx, 0x0c), 4, image);
    need(m.Read(image, 2) == 0x534b, "CURRENT malformed KS invokes unrecovered error reporter");
    uint32_t cursor = add(image, 28);
    const bool already_rebased = m.Read(add(image, 2), 1) == 0x44;
    m.Write(add(image, 2), 1, 0x44);
    mapped(input.allocation_hook ? input.allocation_hook(p.module_base + 0x67024, p.module_base + 0x7c714, 8, p.code_object_base) : input.code_object, p.code_object_base, p.code_identity, 8);
    m.Write(p.code_object_base, 4, cursor); m.Write(add(p.code_object_base, 4), 4, 0);
    const uint32_t image_again = m.Read(add(ctx, 0x0c), 4);
    m.Write(add(ctx, 0x10), 4, p.code_object_base);
    const uint32_t code_size = m.Read(add(image_again, 12), 4);
    const uint32_t local_count = m.Read(add(image_again, 22), 2);
    const uint32_t pool_size = m.Read(add(image_again, 16), 4);
    cursor += code_size; m.Write(add(ctx, 0x474), 4, cursor); m.Write(add(ctx, 0x64), 2, local_count);
    cursor += pool_size;
    const uint64_t end = uint64_t(28) + code_size + pool_size + 4 * uint64_t(local_count);
    need(end <= input.image.size(), "CURRENT local image source outside bounded storage");
    mapped(input.allocation_hook ? input.allocation_hook(p.module_base + 0x6706c, p.module_base + 0x7c754, 4 + 4 * local_count, p.local_allocation_base) : input.local_allocation, p.local_allocation_base, p.local_identity, 4 + 4 * local_count);
    const uint32_t local = add(p.local_allocation_base, 4);
    m.Write(p.local_allocation_base, 4, local_count);
    for (uint32_t i = 0; i < local_count; ++i) m.Write(add(local, 4 * i), 4, 0);
    const uint32_t copy_count = m.Read(add(ctx, 0x64), 2);
    m.Write(add(ctx, 0x24), 4, local);
    CurrentTablesCopyC2A48(m, local, cursor, 4 * copy_count);
    const uint32_t loaded_image = m.Read(add(ctx, 0x0c), 4);
    const uint32_t locals = m.Read(add(ctx, 0x64), 2);
    const uint32_t ctx_count = m.Read(add(loaded_image, 26), 2);
    const uint32_t global = m.Read(globalcell, 4);
    cursor += 4 * locals;
    m.Write(add(ctx, 0x28), 4, cursor);
    const uint32_t ctx28 = cursor; cursor += 4 * ctx_count;
    m.Write(add(ctx, 0x66), 2, ctx_count);
    need(uint64_t(end) + 4 * uint64_t(ctx_count) <= input.image.size(), "CURRENT ctx28 outside bounded image");
    uint32_t global_count = p.shared_globals_count, global_address = global;
    uint32_t local_length = locals;
    if (global == 0) {
        global_count = m.Read(add(loaded_image, 24), 2);
        need(uint64_t(end) + 4 * uint64_t(ctx_count) + 4 * uint64_t(global_count) <= input.image.size(), "CURRENT globals outside bounded image");
        global_address = cursor; m.Write(globalcell, 4, cursor); m.Write(countcell, 2, global_count);
        CurrentTablesRebase66DD0(m, ctx, cursor, static_cast<int32_t>(global_count), p.module_base);
        local_length = m.Read(add(ctx, 0x64), 2);
    }
    const uint32_t local_words = m.Read(add(ctx, 0x24), 4);
    CurrentTablesRebase66DD0(m, ctx, local_words, static_cast<int32_t>(local_length), p.module_base);
    if (!already_rebased) {
        const uint32_t loaded_again = m.Read(add(ctx, 0x0c), 4);
        const uint32_t words = m.Read(add(ctx, 0x28), 4);
        const uint32_t length = m.Read(add(loaded_again, 26), 2);
        CurrentTablesRebase66DD0(m, ctx, words, static_cast<int32_t>(length), p.module_base);
    }
    out.layout_ = {image, static_cast<uint32_t>(input.image.size()), add(image, 28), code_size,
                   add(add(image, 28), code_size), pool_size, local, ctx28, global_address,
                   static_cast<uint16_t>(local_count), static_cast<uint16_t>(ctx_count), static_cast<uint16_t>(global_count)};
    out.loaded_ = true; return out;
}

bool CurrentTablesOwner::available() const {
    return loaded_ && memory_.Live(mapping_.image_identity) && memory_.Live(mapping_.local_identity) && memory_.Live(mapping_.context_identity) && memory_.Live(mapping_.stack_identity) && memory_.Live(mapping_.code_identity) && (mapping_.shared_globals_base == 0 || memory_.Live(mapping_.globals_identity));
}
CurrentTablesOwner CurrentTablesOwner::Fork() const { CurrentTablesOwner out = *this; out.memory_ = memory_.Fork(); return out; }
CurrentTablesAddress CurrentTablesOwner::Address(CurrentTable table, uint32_t index) const {
    need(available(), "CURRENT owner unavailable"); uint32_t base = 0, count = 0, stride = 4;
    switch (table) {
    case CurrentTable::Stack: base = mapping_.stack_base; count = mapping_.stack_count; break;
    case CurrentTable::Local: base = layout_.local; count = layout_.local_count; break;
    case CurrentTable::Ctx28: base = layout_.ctx28; count = layout_.ctx28_count; break;
    case CurrentTable::Global: base = layout_.global; count = layout_.global_count; break;
    case CurrentTable::Pool: base = layout_.pool; count = layout_.pool_size; stride = 1; break;
    case CurrentTable::Image: base = layout_.image; count = layout_.image_size; stride = 1; break;
    }
    need(index < count, "CURRENT table index outside mapped bound");
    const uint32_t address = base + stride * index;
    return {table, index, address, memory_.Identity(address, stride)};
}
uint32_t CurrentTablesOwner::LoadWord(CurrentTable table, uint32_t index) {
    need(table != CurrentTable::Image && table != CurrentTable::Pool, "CURRENT LoadWord needs a word table");
    return memory_.Read(Address(table, index).physical, 4);
}
void CurrentTablesOwner::StoreWord(CurrentTable table, uint32_t index, uint32_t word) {
    need(table != CurrentTable::Image && table != CurrentTable::Pool, "CURRENT StoreWord needs a word table");
    memory_.Write(Address(table, index).physical, 4, word);
}
bool CurrentTablesOwner::ResolvePhysical(uint32_t physical, CurrentTablesAddress& address) const {
    if (!available()) return false;
    for (auto table : {CurrentTable::Stack, CurrentTable::Local, CurrentTable::Ctx28, CurrentTable::Global}) {
        uint32_t base = 0, count = 0;
        if (table == CurrentTable::Stack) { base = mapping_.stack_base; count = mapping_.stack_count; }
        else if (table == CurrentTable::Local) { base = layout_.local; count = layout_.local_count; }
        else if (table == CurrentTable::Ctx28) { base = layout_.ctx28; count = layout_.ctx28_count; }
        else { base = layout_.global; count = layout_.global_count; }
        if (physical >= base && uint64_t(physical) < uint64_t(base) + 4 * uint64_t(count) && (physical - base) % 4 == 0) {
            address = Address(table, (physical - base) / 4); return true;
        }
    }
    return false;
}
uint32_t CurrentTablesOwner::reference_base() const { need(available(), "CURRENT owner unavailable"); return memory_.Peek(mapping_.module_base + base_cell, 4); }
bool CurrentTablesOwner::ResolveReference(uint32_t word, CurrentTablesAddress& address) const {
    return word >> 28 == 11 && available() && ResolvePhysical(reference_base() + static_cast<uint32_t>(signed28(word)), address);
}
bool CurrentTablesOwner::ResolvePoolReference(uint32_t word, CurrentTablesAddress& address) const {
    if (word >> 28 != 5 || !available()) return false;
    const uint32_t physical = reference_base() + static_cast<uint32_t>(signed28(word));
    if (physical < layout_.pool || uint64_t(physical) >= uint64_t(layout_.pool) + layout_.pool_size) return false;
    address = Address(CurrentTable::Pool, physical - layout_.pool); return true;
}
const std::vector<uint8_t>& CurrentTablesOwner::image_bytes() const { need(available(), "CURRENT owner unavailable"); return memory_.Bytes(mapping_.image_identity); }
const std::vector<uint8_t>& CurrentTablesOwner::image_readable() const { need(available(), "CURRENT owner unavailable"); return memory_.Readable(mapping_.image_identity); }
const std::vector<uint8_t>& CurrentTablesOwner::image_writable() const { need(available(), "CURRENT owner unavailable"); return memory_.Writable(mapping_.image_identity); }
const std::vector<uint8_t>& CurrentTablesOwner::local_bytes() const { need(available(), "CURRENT owner unavailable"); return memory_.Bytes(mapping_.local_identity); }
std::shared_ptr<CurrentTablesStorageImage> CurrentTablesOwner::shared_image() const { return memory_.SharedImage(mapping_.image_identity); }
void CurrentTablesOwner::EndBorrowedImageLifetime() { need(available(), "CURRENT owner unavailable"); memory_.EndLifetime(mapping_.image_identity); }
} // namespace dokodemo::recovered
