#include "game_filesystem_win32.h"
#include "recovered/game_boot_native.h"
#include "recovered/game_scene_native.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace dokodemo;
using namespace dokodemo::recovered;
namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Word(std::vector<uint8_t>& bytes, size_t at, uint32_t word) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = uint8_t(word >> (8 * i));
}
std::vector<uint8_t> Fixture() {
    std::vector<uint8_t> bytes(8196, 0);
    Word(bytes, 0, 1); Word(bytes, 4, 1); Word(bytes, 8, 8); Word(bytes, 12, 4096);
    Word(bytes, 16, 0); Word(bytes, 20, 4096); Word(bytes, 24, 4); Word(bytes, 28, UINT32_MAX);
    const std::string name = "/A.BIN";
    for (size_t i = 0; i < name.size(); ++i) bytes[32 + i] = uint8_t(name[i]);
    Word(bytes, 8192, 0x04030201); return bytes;
}
void Write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    Check(bool(out), "write owned test fixture");
}
}
int main() {
    try {
        const auto root = std::filesystem::current_path() / ("game-fs-test-" + std::to_string(GetCurrentProcessId()));
        Check(std::filesystem::create_directory(root), "fresh fixture directory");
        StartupHostStatus created;
        std::shared_ptr<WindowsStartupBackend> backend(WindowsStartupBackend::Create(created));
        Check(bool(created) && bool(backend), "real Windows file/allocation backend");
        const auto valid = root / "valid.bp";
        Write(valid, Fixture());
        GameFilesystem fs;
        Check(bool(fs.Mount(CreateWindowsGameFileSource(backend, valid.wstring()))), "actual file open and checked root parse");
        auto state = fs.Snapshot();
        Check(state.mounted && state.io.file_live && state.io.file_bytes == 8196
            && state.directory_bytes == 40 && state.io.read_calls == 2 && state.io.transferred_bytes == 56,
            "mount reads header/directory only, not whole archive");
        BpDirectoryEntry entry;
        Check(bool(fs.Lookup("a.bin", entry)) && entry.offset == 8192 && entry.size == 4, "case-folded distant entry");
        Check(fs.Lookup("missing", entry).code == GameFsCode::NotFound && fs.Snapshot().io.read_calls == 2,
            "notfound is distinct and does not load payload");
        std::vector<uint8_t> scene_bytes(2 * 1024 * 1024, 0xA5);
        GameArena arena;
        Check(arena.Initialize(scene_bytes.data(), uint32_t(scene_bytes.size())), "PPK-like real native arena");
        GameGraphics graphics;
        Check(graphics.Initialize({}), "actual native graphics for source fast-clear/camera");
        GameScene9DC scene;
        Check(scene.InitializeWorkspaces(arena, graphics) && scene.BindFilesystem(fs), "9DC contexts/pools/root reference");
        const auto scene_state = scene.Snapshot();
        Check(scene_state.descriptor_offsets == std::array<uint32_t, 7>{{0, 1, 9, 17, 20, 24, 26}}
            && scene_state.group_count == 27 && scene_state.active_context == 1 && scene_state.finished_contexts == 1,
            "seven descriptors produce27 groups and switched active context");
        for (uint32_t i = 0; i < 2; ++i) {
            const auto elements = scene.PoolElements(i), scratch = scene.PoolScratch(i), context = scene.ContextBuffer(i);
            Check(elements.size == 8192 && scratch.size == 40016 && context.size == 716800, "actual owner spans");
            for (uint32_t at = 0; at < elements.size; ++at) Check(elements.bytes[at] == 0, "905F0 initializes every element");
            Check(scratch.bytes[0] == 0xA5 && scratch.bytes[scratch.size - 1] == 0xA5 && context.bytes[context.size - 1] == 0xA5,
                "unreached byte producers are not replaced by invented zero stores");
        }
        Check(scene_state.native_clear_count == 2 && scene.SelectRenderGroup(-1) && !scene.SelectRenderGroup(-28),
            "native segment selection and two grounded full-surface clears");
        Check(scene.InitializeCamera(graphics) && scene.InitializeResourceLookupA90() && scene.InitializeSubsystemRegistryAC4(),
            "actual typed light/camera/default clones/root reference/registration");
        Check(scene.Snapshot().returned_9dc && scene.Snapshot().scene_flags == 0x110
            && scene.Snapshot().camera_y < -579 && scene.Snapshot().camera_y > -580, "source final camera state");
        GameVertex projected;
        Check(scene.ProjectWorldPoint({{0, 0, 0}}, 0xFFFFFFFF, graphics, projected)
            && projected.x == 240 && projected.y == 136, "live camera projects target to canvas center");
        Check(!scene.ProjectWorldPoint({{0, -700, 0}}, 0xFFFFFFFF, graphics, projected), "behind-camera point refused");
        GameSceneLight prototype, clone;
        Check(scene.SetLightPosition(0, {{1, 2, 3, 1}}) && scene.GetLight(0, true, prototype) && scene.GetLight(0, false, clone)
            && prototype.position[0] == -40000 && clone.position[0] == 1, "mutable clone does not alias prototype");
        PackageView routed;
        Check(bool(scene.LoadRootResource("A.BIN", routed)) && routed.ReadU8(0) == 1,
            "bound resource lookup reaches real distant file I/O");
        Check(scene.Close() && !arena.Used() && !scene.ContextBuffer(0).bytes, "consumer releases before arena reset");
        Check(scene.LoadRootResource("A.BIN", routed).code == GameFsCode::Unavailable, "closed lookup refuses consumers");
        Check(scene.InitializeWorkspaces(arena, graphics) && scene.BindFilesystem(fs) && scene.InitializeCamera(graphics)
            && scene.InitializeResourceLookupA90() && scene.InitializeSubsystemRegistryAC4() && scene.Close() && !arena.Used(),
            "scene can acquire a fresh owner after complete close");
        std::vector<uint8_t> small_storage(1024 * 1024, 0x5A);
        GameArena small;
        Check(small.Initialize(small_storage.data(), uint32_t(small_storage.size())), "limited scene arena");
        GameScene9DC refused;
        Check(!refused.InitializeWorkspaces(small, graphics) && !small.Used() && !refused.Snapshot().workspaces_ready,
            "partial context allocation failure rolls back actual arena owners");
        GameVmBoot vm;
        Check(vm.Initialize74EF4(arena) && arena.Used() == 262144 && vm.InitializeManager93404(),
            "real VM child arena and16/32 manager");
        const auto before = vm.Snapshot();
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        Check(vm.Snapshot().timer_elapsed_us > before.timer_elapsed_us, "started virtual timer measures real monotonic elapsed time");
        auto* vm_block = static_cast<uint8_t*>(vm.Allocate(256));
        Check(vm_block && !vm.Close() && vm.Snapshot().timer_started, "VM teardown refuses live consumer allocation");
        vm_block[255] = 0xCD;
        Check(!vm.Release(vm_block + 1) && vm.Release(vm_block), "VM owns checked child allocations");
        for (uint32_t i = 0; i < 16; ++i) Check(vm.AcquireTask() == i, "manager free-list order");
        Check(vm.AcquireTask() == UINT32_MAX && !vm.Close(), "manager exhaustion and live-task cleanup refusal");
        for (uint32_t i = 0; i < 16; ++i) Check(vm.ReleaseTask(i), "manager task release");
        Check(!vm.ReleaseTask(0) && !vm.ReleaseTask(99) && vm.Close() && !arena.Used(), "double/foreign release refused,VM/parent ownership retired");
        const auto stopped = vm.Snapshot();
        Check(!stopped.timer_live && !stopped.arena_live && stopped.timer_elapsed_us > 0
            && vm.Snapshot().timer_elapsed_us == stopped.timer_elapsed_us && !vm.Allocate(1), "closed clock stops and arena access refuses");
        PackageView member;
        Check(bool(fs.LoadMember("A.BIN", member)) && member.ReadLE32(0) == 0x04030201, "real distant bounded read");
        member.WriteU8(0, 0xAA);
        Check(member.ReadU8(0) == 0xAA, "loaded RAM is mutable without a second payload copy");
        {
            std::ifstream disk(valid, std::ios::binary); disk.seekg(8192);
            Check(disk.get() == 1, "in-place RAM producer does not change read-only source file");
        }
        Check(fs.Snapshot().io.transferred_bytes == 64 && fs.Snapshot().io.live_allocations == 4,
            "member owns separate direct read allocation");
        Check(bool(fs.Close()) && !fs.Snapshot().io.file_live && !fs.Snapshot().io.live_allocations,
            "child/directory leases and file retired");
        bool stale = false;
        try { (void)member.ReadU8(0); } catch (const PackageStorageError& e) { stale = e.code() == PackageStorageCode::Unavailable; }
        Check(stale, "external member view refuses use after owner close");
        Check(bool(fs.Close()), "idempotent close");

        GameFilesystem missing;
        Check(missing.Mount(CreateWindowsGameFileSource(backend, (root / "absent.bp").wstring())).code == GameFsCode::MissingFile,
            "real missing file outcome");
        Check(bool(missing.Close()), "missing-file owner can close");
        std::vector<std::filesystem::path> cleanup{valid};
        for (unsigned kind = 0; kind < 4; ++kind) {
            auto bytes = Fixture();
            if (kind == 0) bytes.resize(10); // header itself truncated
            if (kind == 1) Word(bytes, 0, 0); // bad modulus
            if (kind == 2) Word(bytes, 28, 0); // cyclic directory chain
            if (kind == 3) Word(bytes, 20, 100000); // member outside actual file extent
            const auto path = root / ("bad" + std::to_string(kind) + ".bp");
            Write(path, bytes); cleanup.push_back(path);
            GameFilesystem bad;
            Check(bad.Mount(CreateWindowsGameFileSource(backend, path.wstring())).code == GameFsCode::MalformedDirectory,
                "corrupt/truncated/outofrange directory cannot publish mount");
            Check(!bad.Snapshot().mounted && bool(bad.Close()) && !bad.Snapshot().io.live_allocations,
                "failure retains then cleans its actual acquisitions");
        }
        auto source = CreateWindowsGameFileSource(backend, valid.wstring());
        Check(bool(source->Open()), "direct source open");
        PackageView absent;
        Check(source->Read(8192, 5, absent).code == GameFsCode::Bounds && source->Snapshot().read_calls == 0,
            "range guard uses actual extent before read/allocation");
        Check(bool(source->Close()) && bool(backend->Close()), "all Windows owners close without live borrows");
        // Only exact files created by this test; no recursive deletion.
        for (const auto& path : cleanup) Check(std::filesystem::remove(path), "remove owned fixture");
        Check(std::filesystem::remove(root), "remove empty owned fixture directory");
        std::cout << "game filesystem: real bounded I/O, checked directory, missing/corrupt/bounds and lifetime PASS\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
