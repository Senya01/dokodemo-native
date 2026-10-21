#include "recovered/game_boot_native.h"
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using namespace dokodemo::recovered;
namespace {
void Check(bool v, const char* text) { if (!v) throw std::runtime_error(text); }
void Word(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b.at(at + i) = uint8_t(v >> (8 * i));
}
std::vector<uint8_t> Font() {
    std::vector<uint8_t> b(112, 0);
    Word(b, 0, 0x31544658); Word(b, 4, 0x00100010); Word(b, 0x18, 2);
    Word(b, 0x1C, 4); Word(b, 0x20, 4); Word(b, 0x24, 4);
    Word(b, 0x28, 16); Word(b, 0x2C, 64); Word(b, 0x34, 16); Word(b, 0x38, 96);
    b[64] = 0xAB; b[80] = 0xCD; return b;
}
std::vector<uint8_t> Package(const std::string& name, const std::vector<uint8_t>& member, uint32_t offset) {
    std::vector<uint8_t> b(size_t(offset) + member.size(), 0);
    Word(b, 0, 1); Word(b, 4, 1); Word(b, 8, uint32_t(name.size() + 2)); Word(b, 12, offset);
    Word(b, 16, 0); Word(b, 20, 0); Word(b, 24, uint32_t(member.size())); Word(b, 28, UINT32_MAX);
    b[32] = '/'; std::copy(name.begin(), name.end(), b.begin() + 33);
    std::copy(member.begin(), member.end(), b.begin() + offset); return b;
}
struct Lease : PackageStorageLease {
    std::vector<uint8_t> data, known;
    bool live = true;
    explicit Lease(std::vector<uint8_t> b) : data(std::move(b)), known(data.size(), 1) {}
    bool Span(PackageStorageSpan& out, std::string& error) const override {
        if (!live) { error = "fixture owner closed"; return false; }
        out.bytes = const_cast<uint8_t*>(data.data()); out.initialized = const_cast<uint8_t*>(known.data());
        out.size = data.size(); out.host_address = uint64_t(reinterpret_cast<uintptr_t>(data.data()));
        out.owner = out.host_address; out.generation = out.serial = 1; out.readable = out.writable = true; return true;
    }
};
struct Source : GameFileSource {
    std::vector<uint8_t> disk;
    std::vector<std::shared_ptr<Lease>> reads;
    GameFileIoSnapshot state;
    explicit Source(std::vector<uint8_t> b) : disk(std::move(b)) {}
    GameFsStatus Open() override { state.file_live = true; state.file_bytes = disk.size(); return {}; }
    GameFsStatus Read(uint64_t at, uint32_t count, PackageView& out) override {
        if (!state.file_live || at > disk.size() || count > disk.size() - at) return {GameFsCode::Bounds, 0, "fixture read bounds"};
        auto lease = std::make_shared<Lease>(std::vector<uint8_t>(disk.begin() + size_t(at), disk.begin() + size_t(at + count)));
        reads.push_back(lease); ++state.read_calls; state.transferred_bytes += count; ++state.live_allocations;
        out = PackageStorage::Attach(lease, count); return {};
    }
    GameFsStatus Close() override { for (auto& r : reads) r->live = false; state.file_live = false; state.live_allocations = 0; return {}; }
    GameFileIoSnapshot Snapshot() const override { return state; }
};
}
int main() {
    try {
        // Small explicitly synthetic packages exercise the SAME checked loader;
        // these are not claimed original game content or source filenames.
        GameFilesystem fs;
        Check(bool(fs.Mount(std::make_unique<Source>(Package("DATA.BPM", Package("FONT\\FONT.XFT", Font(), 128), 4096)))), "fixture mount");
        std::vector<uint8_t> backing(1024 * 1024, 0x5A);
        GameArena arena; Check(arena.Initialize(backing.data(), uint32_t(backing.size())), "native arena");
        GameVmBoot vm; Check(vm.Initialize74EF4(arena) && vm.InitializeManager93404(), "native task manager");
        const auto baseline = arena.Used();
        GameContentBoot boot;
        Check(boot.InitializeBB8(arena, vm) && boot.Snapshot().registered_records == 6, "two actual typed owners/six records");
        Check(!vm.Close(), "VM cannot retire live loading tasks");
        Check(boot.Reset24994() && boot.ConfigureImportCallback5334C(), "reset and explicit unavailable interrupt policy");
        Check(!boot.Snapshot().import_callback_registered && boot.StateWord(0x10) == 2 && boot.StateWord(0x14) == 3
            && boot.StateWord(0x18) == 3 && boot.StateWord(276 + 4) == UINT32_MAX
            && boot.StateWord(276 + 10 * 6196 + 27 * 220 + 0xD8) == UINT32_MAX
            && boot.StateWord(276 + 10 * 6196 + 0x1830) == UINT32_MAX
            && boot.StateWord(276 + 28) == 0 && boot.ResetLookupWord(0) == -1 && boot.ResetLookupWord(27) == -1,
            "39B80 explicit slot/entry/global sentinels after reset;no callback success invented");
        const std::array<uint32_t, 6> states{{2, 4, 5, 5, 6, 6}};
        for (size_t i = 0; i < states.size(); ++i) {
            Check(boot.TickLoading(fs) && boot.Snapshot().load_state == states[i], "source cold state order");
            Check(fs.Snapshot().io.read_calls == (i < 4 ? 2u : 3u), "node creation T4 does not read;actual member read T5");
            Check(boot.Snapshot().font_ready == (i == 5), "consumer only after real completion T6");
        }
        const auto ready = boot.Snapshot();
        Check(ready.blocked && ready.frontier_pc == 0x18EE8 && ready.requested_bits == 1 && ready.owned_bits == 1
            && ready.completed_bits == 1 && ready.current_scene == 1 && ready.previous_scene == -1, "no next constructor/scene success");
        const auto* texture = boot.Font().Texture(1);
        Check(texture && texture->format == 4 && texture->width == 4 && texture->height == 4
            && texture->pixels.ReadU8(0) == 0xCD && !boot.Font().Texture(2), "real typed descriptors borrow distinct original spans");
        PackageView stale = texture->pixels;
        Check(boot.Close() && arena.Used() == baseline && !boot.Snapshot().font_ready && !boot.Font().Texture(0), "content retires before parent");
        Check(vm.Close() && !arena.Used() && bool(fs.Close()), "all native tasks/allocations/file leases retire");
        bool refused = false;
        try { (void)stale.ReadU8(0); } catch (const PackageStorageError&) { refused = true; }
        Check(refused, "copied texture cannot read after file allocation retirement");

        for (unsigned bad = 0; bad < 4; ++bad) {
            auto bytes = Font();
            if (bad == 0) bytes[0] = 0;
            if (bad == 1) Word(bytes, 0x18, 0xFFFF); // negative signed source count
            if (bad == 2) Word(bytes, 0x2C, 100); // texture extent overflow
            if (bad == 3) Word(bytes, 0x38, 200); // auxiliary view outside image
            auto lease = std::make_shared<Lease>(bytes);
            GameFontResource font;
            Check(!font.Initialize(PackageStorage::Attach(lease, bytes.size())) && !font.Snapshot().ready,
                "malformed font cannot publish descriptors");
        }
        GameFilesystem absent;
        Check(bool(absent.Mount(std::make_unique<Source>(Package("OTHER.BPM", Font(), 4096)))), "missing-package fixture");
        Check(vm.Initialize74EF4(arena) && vm.InitializeManager93404() && boot.InitializeBB8(arena, vm)
            && boot.Reset24994() && boot.ConfigureImportCallback5334C(), "fresh lifecycle");
        for (unsigned i = 0; i < 4; ++i) Check(boot.TickLoading(absent), "before actual missing read");
        Check(!boot.TickLoading(absent) && !boot.Snapshot().font_ready && !boot.Snapshot().blocked
            && boot.Snapshot().status.code == GameFsCode::NotFound, "missing package is real failure,not completion");
        Check(boot.Close() && vm.Close() && bool(absent.Close()) && !arena.Used(), "failed load owns then releases its acquisitions");
        std::cout << "game content: cold state order,real nested views,font consumer,malformed/missing and retirement PASS\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
