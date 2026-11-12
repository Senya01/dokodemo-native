#include "game_filesystem_win32.h"
#include "recovered/game_boot_native.h"
#include "recovered/game_scene_native.h"
#include "recovered/game_sound_control_native.h"
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
struct TextLease final:PackageStorageLease {
    std::vector<uint8_t> bytes,known;
    explicit TextLease(std::string s):bytes(s.begin(),s.end()){bytes.push_back(0);known.assign(bytes.size(),1);}
    bool Span(PackageStorageSpan& out,std::string&)const override{
        out={const_cast<uint8_t*>(bytes.data()),const_cast<uint8_t*>(known.data()),bytes.size(),
            uint64_t(reinterpret_cast<uintptr_t>(bytes.data())),uint64_t(reinterpret_cast<uintptr_t>(this)),1,1,0,true,false};return true;
    }
};
PackageView Text(const std::string& text){auto lease=std::make_shared<TextLease>(text);return PackageStorage::Attach(lease,lease->bytes.size());}
void RetainedWindowsFiles(const std::filesystem::path& root,const std::shared_ptr<WindowsStartupBackend>& backend){
    const auto a=root/"first.bin",b=root/"second.bin";
    Write(a,{1,2,3,4});Write(b,{9,8,7,6,5});
    // SAME ordinary factory/backend; only synthetic input paths and bytes.
    auto one=CreateWindowsGameFileSource(backend,a.wstring(),0x7C000000);
    auto two=CreateWindowsGameFileSource(backend,b.wstring(),0x7C000000);
    Check(bool(one->Open())&&bool(two->Open()),"independent live ordinary files");PackageView x,y;
    Check(bool(one->Read(0,4,x))&&bool(two->Read(0,5,y))&&x.ReadU8(0)==1&&y.ReadU8(0)==9&&!x.SharesBacking(y),
          "retained overlapping floors choose distinct real initialized bytes");
    const auto dx=x.Describe(),dy=y.Describe();
    Check(dx.source_address+4096<=dy.source_address||dy.source_address+4096<=dx.source_address,"page ranges do not overlap");
    StartupPartitionRequest exact;exact.bytes=4;exact.source_base=uint32_t(dx.source_address);
    Check(backend->Allocate(exact).status.code==StartupHostCode::SourceOverlap,"explicit startup address/overlap contract stays exact");
    Check(!backend->Close()&&x.ReadU8(3)==4&&y.ReadU8(4)==5,"live source/borrow prevents backend close");
    Check(bool(one->Close())&&bool(two->Close()),"independent files close");

    std::array<std::unique_ptr<GameFileSource>,4> files;std::array<PackageView,4> views;std::array<GameFsStatus,4> statuses;
    for(auto& file:files){file=CreateWindowsGameFileSource(backend,a.wstring(),0x7C000000);Check(bool(file->Open()),"concurrent current file");}
    std::array<std::thread,4> workers;
    for(size_t i=0;i<workers.size();++i)workers[i]=std::thread([&,i]{statuses[i]=files[i]->Read(0,4,views[i]);});
    for(auto& worker:workers)worker.join();
    for(size_t i=0;i<files.size();++i){Check(bool(statuses[i])&&views[i].ReadLE32(0)==0x04030201,"concurrent production file actual bytes");
        for(size_t j=0;j<i;++j)Check(views[i].Describe().source_address!=views[j].Describe().source_address,"shared allocator reserves ranges atomically");}
    for(auto& file:files)Check(bool(file->Close()),"concurrent owners retire after joins");

    const auto edge=backend->AllocateFileBuffer(4096,0xEFFFF000);
    Check(bool(edge.status)&&edge.source_base==0xEFFFF000,"bounded final page is usable");
    Check(backend->AllocateFileBuffer(4096,0xEFFFF000).status.code==StartupHostCode::SourceRange
        &&backend->AllocateFileBuffer(1,0xEFFFF001).status.code==StartupHostCode::SourceRange
        &&backend->AllocateFileBuffer(0x2000,0xEFFFF000).status.code==StartupHostCode::SourceRange,"exhausted/misaligned/oversize range refuses");
    Check(bool(backend->Free(edge.handle)),"release actual edge allocation");
    const auto retry=backend->AllocateFileBuffer(4096,0xEFFFF000);
    Check(bool(retry.status)&&retry.handle.serial!=edge.handle.serial&&bool(backend->Free(retry.handle)),"failed allocation can retry without stale identity");

    const auto prefix=root/"opaque";Check(std::filesystem::create_directory(prefix),"owned URI root");
    const auto bgm=prefix/"bgm";Check(std::filesystem::create_directory(bgm),"owned factory directory");
    const auto track=bgm/"test.sgd";Write(track,{1,2,3,4});
    GameBgmFileQueue queue;auto owner=std::make_shared<int>(1);const auto uri=Text("/opaque"),name=Text("test");
    Check(bool(queue.Initialize(owner,1,CreateWindowsGameFileFactory(backend,root.wstring(),0x7C000000))),"SAME ordinary Windows queue factory");
    GameBgmFileResult old,next;Check(bool(queue.Request(owner,1,7,uri,name,old))&&old.accepted
        &&bool(queue.Request(owner,1,7,uri,name,next))&&next.accepted,"same index queued requests own distinct actual files");
    Check(bool(queue.Pump(owner,1))&&queue.Read(7).output_request==old.request&&queue.Read(7).state==1,"old read publishes old identity only");
    Check(bool(queue.Pump(owner,1)),"old actual completion");PackageView old_bytes;Check(bool(queue.BorrowRequest(owner,1,7,old.request,old_bytes)),"old initialized borrow retained");
    Check(bool(queue.Pump(owner,1))&&queue.Read(7).state==2&&queue.Read(7).output_request==next.request,"new initialized output follows old");
    Check(bool(queue.Pump(owner,1)),"new actual completion");PackageView new_bytes;Check(bool(queue.Borrow(owner,1,7,new_bytes))
        &&old_bytes.ReadU8(0)==1&&new_bytes.ReadU8(0)==1&&!old_bytes.SharesBacking(new_bytes),"new initialized buffer not substituted from retained old output");
    GameBgmFileResult third;Check(bool(queue.Request(owner,1,7,uri,name,third))&&third.accepted
        &&queue.Read(7).output_request==next.request&&queue.Read(7).state==1,"rerequest completed current retains output separately");
    Check(bool(queue.Pump(owner,1))&&bool(queue.Pump(owner,1))&&queue.Read(7).state==5
        &&queue.Read(7).output_request==third.request&&queue.Read(7).retained_requests==3,"third actual file completes under its own identity");
    const auto held=old_bytes.Describe();StartupHostStatus borrowed;
    auto hold=backend->Borrow({held.owner,held.generation,held.serial},borrowed);Check(bool(borrowed),"independent real native borrow");
    Check(!queue.Close(),"close refuses actual still-borrowed old allocation");StartupRawBorrowSpan span;
    Check(bool(hold.Span(span))&&span.bytes[0]==1,"failed close retains real bytes");hold.Reset();Check(bool(queue.Close()),"close retry retires actual requests");
    Check(std::filesystem::remove(track)&&std::filesystem::remove(bgm)&&std::filesystem::remove(prefix),"remove only factory fixture");
    Check(std::filesystem::remove(a)&&std::filesystem::remove(b),"remove only owned synthetic files");
}
}
int main() {
    try {
        const auto root = std::filesystem::current_path() / ("game-fs-test-" + std::to_string(GetCurrentProcessId()));
        Check(std::filesystem::create_directory(root), "fresh fixture directory");
        StartupHostStatus created;
        std::shared_ptr<WindowsStartupBackend> backend(WindowsStartupBackend::Create(created));
        Check(bool(created) && bool(backend), "real Windows file/allocation backend");
        RetainedWindowsFiles(root,backend);
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
