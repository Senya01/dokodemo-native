#include "core_hash.h"
#include "recovered/game_boot_native.h"
#include "recovered/game_model_native.h"
#include "recovered/game_title_native.h"
#include "recovered/game_actor_runtime_native.h"
#include "recovered/game_actor_script_native.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <fstream>
#include <limits>
#include <stdexcept>
namespace dokodemo {
struct StartupRuntimeTestAccess {
    static std::array<int32_t,3> Application(recovered::GameContentBoot& content){return {{content.state_.current_scene,content.state_.previous_scene,content.state_.next_scene}};}
    static bool RequestApplication(recovered::GameContentBoot& content,int32_t scene){return content.RequestApplicationScene(scene);}
    static bool RequestApplicationFlag(recovered::GameContentBoot& c,int32_t scene,uint32_t flag){return c.RequestApplicationScene(scene,flag);}
    static bool Moving(recovered::GameContentBoot& c,std::shared_ptr<const recovered::GameImage> image){
        auto& ui=c.scene_ui_;ui.module_owner_=c.loadman_->control;ui.module_epoch_=c.loadman_->control->Snapshot().loadman.module_epoch;
        return ui.moving_.Initialize(ui.module_owner_,ui.module_epoch_,std::move(image));
    }
    static recovered::GameSceneUiBootstrap& SceneUi(recovered::GameContentBoot& c){return c.scene_ui_;}
    static bool ForeignMoving(recovered::GameSceneUiBootstrap& ui,std::shared_ptr<const void> module,std::shared_ptr<const recovered::GameImage> image){
        ui.module_owner_=std::move(module);ui.module_epoch_=1;return ui.moving_.Initialize(ui.module_owner_,1,std::move(image));
    }
    static std::weak_ptr<const recovered::GameModelAsset> OldScene(recovered::GameActorRuntime& a){return a.scene_asset_;}
    struct CompactScene {uint64_t epoch,frames;uint32_t placement_writes;bool regions;std::array<float,4> placement;};
    static CompactScene Scene(recovered::GameActorRuntime& a){return {a.state_.owner_epoch,a.state_.scene_frames,a.state_.source_placement_writes,a.state_.region_owners_live,a.state_.source_placement20};}
    static bool ForeignAdoptionRefuses(recovered::GameActorRuntime& a,script::Vm& vm,const recovered::GameOutingCommand& command){
        auto& binding=a.manager_switch_->loaded_models_.at(a.destination_state_.resource);auto original=binding;
        binding=std::shared_ptr<const recovered::GameModelAsset>(original.get(),[](const auto*){});
        const bool refused=!a.AdoptPreparedDestination(vm,command)&&!a.destination_state_.placement_pc;
        binding=std::move(original);return refused;
    }
    static bool TargetSourceOrder(recovered::GameActorRuntime& a){
        const auto s=a.destination_->Snapshot();recovered::GameBackgroundAppliedState applied;
        return a.destination_->AppliedEffects(a.background_module_.lock(),a.destination_state_.epoch,applied)
            &&s.bgm_selection.previous==0&&s.bgm_selection.after==applied.rng_before;
    }
    static bool TargetGlyphs(recovered::GameActorRuntime& a,script::Vm& vm){
        recovered::GameSupportSnapshot s;return a.destination_->ReadSupport(a.background_module_.lock(),a.destination_state_.epoch,vm,s)
            &&s.source_current_bound&&s.glyph_pixels_ready&&s.glyph_decoded==1&&s.bound_texture_nodes==1&&s.placements.size()==1;
    }
    static uint32_t TargetSamples(recovered::GameActorRuntime& a){return a.destination_->applied_.samples;}
    static bool CommitApplication(recovered::GameContentBoot& content){return content.CommitApplicationScene();}
    static recovered::GameActorRuntime& Actor(recovered::GameContentBoot& content){return content.actor_runtime_;}
    static recovered::GameFsStatus DrawPrepared(recovered::GameActorRuntime& actor,script::Vm& vm,recovered::GameGraphics& g){
        // Explicit synthetic component view includes this fixture's authored
        // motion translation(10,20,30). This is not the travel camera.
        const auto owner=actor.background_module_.lock();auto camera=recovered::ModelIdentity();camera[0]=camera[5]=camera[10]=0.02f;
        return actor.destination_->DrawSelected(owner,actor.destination_state_.epoch,vm,g,camera);
    }
    static bool SelectedComposition(recovered::GameActorRuntime& actor,const recovered::GameGraphics& graphics){
        const auto owner=actor.background_module_.lock();recovered::GameBackgroundAppliedState applied;
        if(!actor.destination_->AppliedEffects(owner,actor.destination_state_.epoch,applied))return false;
        const auto model=actor.destination_->Snapshot();const auto g=graphics.Snapshot();
        size_t selected_children=0;
        for(size_t i=0;i<applied.selected_entries.size();++i){
            const bool child=applied.selected_entries[i]%2==0; // this synthetic recipe's producer
            if(child==applied.child_poses[i].empty()||(child?(applied.child_phases[i]<0):(applied.child_phases[i]!=-1)))return false;
            selected_children+=child;
        }
        if(!g.mesh_pixels||g.mesh_triangles!=model.model.triangles+selected_children*model.auxiliary_model.triangles)
            std::cerr<<"selected-composition pixels="<<g.mesh_pixels<<" triangles="<<g.mesh_triangles<<" main="<<model.model.triangles
                <<" selected="<<applied.selected_entries.size()<<" child="<<model.auxiliary_model.triangles<<'\n';
        return model.auxiliary_entries==6&&model.auxiliary_children==3&&applied.selected_entries.size()<model.auxiliary_entries&&g.mesh_pixels>0
            &&g.mesh_triangles==model.model.triangles+selected_children*model.auxiliary_model.triangles;
    }

    static void RetireAuxChild(recovered::GameBackgroundPreparation& job,uint32_t index,bool drop){
        if(drop)job.auxiliary_entries_.at(index).child.reset();else job.auxiliary_entries_.at(index).child->Close();
    }
    static void AuxBadSelected(recovered::GameBackgroundPreparation& job,bool set){
        if(set)job.applied_.selected_entries.push_back(uint32_t(job.auxiliary_entries_.size()));else job.applied_.selected_entries.pop_back();
    }

    static recovered::GameInventory& Inventory(recovered::GameContentBoot& content){return content.inventory_;}
    static recovered::GameSceneBgm& Bgm(recovered::GameContentBoot& content){return *content.scene_bgm_;}
    static std::shared_ptr<recovered::GameManagerSwitchControl> Manager(recovered::GameContentBoot& content){return content.loadman_->control;}
    static bool ActorTick(recovered::GameContentBoot& content,recovered::GameFilesystem& fs,recovered::GameGraphics& graphics){return content.TickActorDictionary(fs,graphics);}
};
}
using namespace dokodemo::recovered;
namespace script=dokodemo::script;
namespace {
void Check(bool value, const char *text) {
    if (!value)
        throw std::runtime_error(text);
}
void W(std::vector<uint8_t> &b, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        b.push_back(uint8_t(v >> (i * 8)));
}
void H(std::vector<uint8_t> &b, int32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void F(std::vector<uint8_t> &b, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    W(b, v);
}
void Word(std::vector<uint8_t> &b, size_t at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        b.at(at + i) = uint8_t(value >> (i * 8));
}
void Bounds(std::vector<uint8_t> &b) {
    F(b, 1);
    for (unsigned i = 0; i < 24; ++i)
        H(b, 0);
    for (unsigned i = 0; i < 4; ++i)
        F(b, 0);
}
std::vector<uint8_t> Fixture(bool normals=false,const std::vector<std::array<int16_t,3>>* custom=nullptr,bool textured=false,float scale=1) {
    const std::vector<std::array<int16_t,3>> regular{{{-25000,-25000,0}},{{25000,-25000,0}},{{0,25000,0}}};
    const auto &positions=custom?*custom:regular;
    std::vector<uint8_t> b{'D', 'V', 'S', 0};
    b.resize(16);
    W(b, textured?24:6);
    W(b, textured?1:0);
    if(textured){
        const auto common=b.size();b.resize(common+36);b[common]=1;
        W(b,0);const auto name=b.size();b.resize(name+32);
        const std::string stem="CONTACT_TEST";std::copy(stem.begin(),stem.end(),b.begin()+name);
    }
    Bounds(b);
    H(b, 1);
    H(b, 0);
    for (unsigned i = 0; i < 4; ++i)
        H(b, 0);
    const auto node = b.size();
    W(b, 0);
    H(b, 1);
    for (auto v : {0, -1, -1, 255, 0})
        H(b, v);
    for (const auto &row :
         std::array<GameVector4, 4>{{{{0, 0, 0, 1}}, {{0, 0, 0, 1}}, {{0, 0, 0, 1}}, {{1, 1, 1, 1}}}})
        for (auto v : row)
            F(b, v);
    b.push_back(3);
    b.push_back(0);
    H(b, normals?0x4009:9);
    b.push_back(textured?0:255);
    b.push_back(0);
    H(b, int32_t(positions.size()));
    F(b, scale);
    for(const auto &p:positions)for(auto v:p)H(b,v);
    while (b.size() % 4)
        b.push_back(0);
    if(normals){
        for(size_t i=0;i<positions.size();++i)b.insert(b.end(),{0,0,126});
        while(b.size()%4)b.push_back(0);
    }
    b.insert(b.end(), {255, 255, 255, 128});
    if(textured)for(size_t i=0;i<positions.size();++i){F(b,.5f);F(b,.5f);}
    Bounds(b);
    W(b, 0);
    H(b, 0);
    H(b, 0);
    W(b, 3);
    H(b, 1);
    H(b, 0x0203);
    H(b, 0);
    H(b, 1);
    H(b, 2);
    H(b, 0);
    Word(b, node, uint32_t(b.size() / 4));
    return b;
}
struct Lease : PackageStorageLease {
    std::vector<uint8_t> b, known;
    bool live = true;
    explicit Lease(std::vector<uint8_t> data) : b(std::move(data)), known(b.size(), 1) {}
    bool Span(PackageStorageSpan &s, std::string &error) const override {
        if (!live) {
            error = "fixture retired";
            return false;
        }
        s.bytes = const_cast<uint8_t *>(b.data());
        s.initialized = const_cast<uint8_t *>(known.data());
        s.size = b.size();
        s.owner = s.host_address = uint64_t(reinterpret_cast<uintptr_t>(b.data()));
        s.generation = s.serial = 1;
        s.readable = s.writable = true;
        return true;
    }
};

struct BackgroundFileState {
    bool live=false,fail_read=false,fail_close=false;
    bool missing=false;
    uint32_t reads=0,closes=0;
    std::vector<std::shared_ptr<Lease>> buffers;
};
class BackgroundFile final:public GameFileSource {
    std::vector<uint8_t> bytes_;
    std::shared_ptr<BackgroundFileState> state_;
  public:
    BackgroundFile(std::vector<uint8_t> bytes,std::shared_ptr<BackgroundFileState> state)
        :bytes_(std::move(bytes)),state_(std::move(state)){}
    ~BackgroundFile()override{(void)Close();}
    GameFsStatus Open()override{if(state_->missing)return {GameFsCode::MissingFile,2,"synthetic missing file"};state_->live=true;return {};}
    GameFsStatus Read(uint64_t at,uint32_t count,PackageView& out)override{
        if(!state_->live||at>bytes_.size()||count>bytes_.size()-at||state_->fail_read)
            return {GameFsCode::IoFailure,0,"synthetic read failure"};
        auto lease=std::make_shared<Lease>(std::vector<uint8_t>(bytes_.begin()+size_t(at),bytes_.begin()+size_t(at)+count));
        state_->buffers.push_back(lease);out=PackageStorage::Attach(lease,count);++state_->reads;return {};
    }
    GameFsStatus Close()override{
        if(state_->live&&state_->fail_close)return {GameFsCode::IoFailure,0,"synthetic close failure"};
        if(state_->live){for(auto& b:state_->buffers)b->live=false;state_->live=false;++state_->closes;}
        return {};
    }
    GameFileIoSnapshot Snapshot()const override{GameFileIoSnapshot s;s.file_live=state_->live;s.file_bytes=bytes_.size();s.read_calls=state_->reads;return s;}
};
std::vector<uint8_t> BackgroundArchive(const std::vector<std::pair<std::string,std::vector<uint8_t>>>& entries){
    size_t names=0;for(const auto& e:entries)names+=e.first.size()+1;
    const auto payload=(16+entries.size()*16+names+15)&~size_t(15);
    std::vector<uint8_t> out(payload);Word(out,0,1);Word(out,4,uint32_t(entries.size()));Word(out,8,uint32_t(names));Word(out,12,uint32_t(payload));
    size_t name=16+entries.size()*16;
    for(size_t i=0;i<entries.size();++i){
        const auto& e=entries[i];const auto at=16+i*16;
        Word(out,at,uint32_t(name-16-entries.size()*16));Word(out,at+4,uint32_t(out.size()-payload));Word(out,at+8,uint32_t(e.second.size()));Word(out,at+12,i+1<entries.size()?uint32_t(i+1):UINT32_MAX);
        std::copy(e.first.begin(),e.first.end(),out.begin()+name);name+=e.first.size()+1;
        out.insert(out.end(),e.second.begin(),e.second.end());out.resize((out.size()+15)&~size_t(15));
    }
    return out;
}
void BgmTransportTests(){
    const auto view=[](const std::shared_ptr<Lease>& p){return PackageStorage::Attach(p,p->b.size());};
    const auto text=[](std::string s){std::vector<uint8_t> b(s.begin(),s.end());b.push_back(0);return std::make_shared<Lease>(std::move(b));};
    auto root=text("/SYNTHETIC/sound"),name=text("TEST_BANK");
    auto owner=std::make_shared<int>(1),foreign=std::make_shared<int>(2);
    std::vector<uint8_t> sgxd(16);Word(sgxd,0,0x44584753);Word(sgxd,8,16); //generated minimal format, no game payload
    auto io=std::make_shared<BackgroundFileState>();unsigned factories=0;
    const auto factory=[&](const std::string& path,std::unique_ptr<GameFileSource>& out)->GameFsStatus{
        Check(path=="/SYNTHETIC/sound/bgm/TEST_BANK.sgd","source path grammar reaches actual file factory");
        ++factories;out=std::make_unique<BackgroundFile>(sgxd,io);return {};};
    GameBgmFileQueue q;GameBgmFileResult r;
    Check(bool(q.Initialize(owner,1,factory)),"initialized real file queue owner");
    auto alias=std::shared_ptr<const void>(owner.get(),[](const void*){});
    Check(!q.Request(foreign,1,2,view(root),view(name),r)&&!q.Request(alias,1,2,view(root),view(name),r)
        &&!q.Request(owner,2,2,view(root),view(name),r)&&!factories,"foreign control block/owner/epoch cannot create an I/O request");
    Check(bool(q.Request(owner,1,2,view(root),view(name),r))&&r.accepted&&!r.cached&&r.wrapper_return==0
        &&q.Read(2).selector60==2&&q.Read(2).bank5C==-1&&!q.Read(2).buffer54&&!io->live,"accepted queue is not open, loaded, bank-ready or playback");
    PackageView image;Check(!q.Borrow(owner,1,2,image)&&!q.Request(owner,1,2,view(root),view(name),r),"pending output/replayed alias is not completed");
    Check(bool(q.Pump(owner,1))&&q.Read(2).state==2&&q.Read(2).buffer54&&q.Read(2).bytes==16&&q.Read(2).bank5C==-1
        &&!q.Borrow(owner,1,2,image),"real initialized buffer distinct from second-stage completion/5C");
    io->buffers[0]->b[0]^=1;Check(!q.Pump(owner,1)&&!q.Read(2).current&&q.Read(2).state==2,"buffer drift cannot publish late completion");io->buffers[0]->b[0]^=1;
    Check(bool(q.Pump(owner,1))&&bool(q.Borrow(owner,1,2,image))&&image.ReadLE32(0)==0x44584753&&q.Read(2).current,"completed owned file readback");
    GameSoundBank wrong_bank;auto foreign_image=std::make_shared<Lease>(sgxd);Check(bool(wrong_bank.InitializeSource17F0())&&bool(wrong_bank.Register(view(foreign_image)))
        &&!q.BindRegistered(owner,1,2,wrong_bank),"same bytes in a foreign allocation cannot supply cached5C");wrong_bank.Close();
    GameSoundBank bank;Check(bool(bank.InitializeSource17F0())&&bool(bank.Register(image)),"actual synthetic SGXD bank allocation, no playback");
    Check(bool(q.BindRegistered(owner,1,2,bank))&&q.Read(2).bank5C>0&&q.Read(2).bank5C!=int32_t(q.Read(2).bytes),"5C is actual registry handle, not file size");
    Check(bool(q.Request(owner,1,2,view(root),view(name),r))&&r.cached&&!r.accepted&&r.wrapper_return==q.Read(2).bank5C&&factories==1,"cached wrapper returns same current positive bank without new I/O");
    bank.Close();Check(!q.Read(2).current&&!q.Request(owner,1,2,view(root),view(name),r),"retired bank cannot become a cold/cache success");
    io->fail_close=true;Check(!q.Close()&&!q.Read(2).current&&!q.Initialize(owner,2,factory),"failed retirement retains actual file for retry; cannot restart");
    io->fail_close=false;Check(bool(q.Close())&&!io->live&&!q.Initialize(owner,1,factory),"actual retry retires backing; same epoch cannot revive old calls");bool stale=false;try{(void)image.ReadLE32(0);}catch(...){stale=true;}Check(stale,"retired exported file lease refuses");
    for(unsigned failure=0;failure<4;++failure){
        io=std::make_shared<BackgroundFileState>();Check(bool(q.Initialize(owner,2+failure,factory)),"fresh file epoch");
        Check(bool(q.Request(owner,2+failure,3,view(root),view(name),r)),"real failure begins with accepted request");
        if(failure==0)io->missing=true;
        if(failure==1)io->fail_read=true;
        if(failure==2)root->b[1]='Q';
        if(failure==3)owner.reset();
        Check(!q.Pump(owner,2+failure)&&!q.Read(3).buffer54&&q.Read(3).selector60==3&&q.Read(3).bank5C==-1,"open/read/input/owner failure is not immediate queue rejection or ready");
        if(failure<2){io->missing=io->fail_read=false;Check(bool(q.Pump(owner,2+failure))&&bool(q.Pump(owner,2+failure))&&q.Read(3).state==5,"actual supported I/O retry completes without changing wrapper result");}
        if(failure==2)root->b[1]='S';
        if(failure==3)owner=std::make_shared<int>(1);
        Check(bool(q.Close()),"failure cleanup");
    }
    io=std::make_shared<BackgroundFileState>();Check(bool(q.Initialize(owner,9,factory)),"finite queue initialization");
    for(unsigned i=0;i<8;++i)Check(bool(q.Request(owner,9,i,view(root),view(name),r))&&r.accepted,"eight actual pending output owners");
    const auto before=factories;Check(bool(q.Request(owner,9,8,view(root),view(name),r))&&!r.accepted&&r.wrapper_return==-1
        &&q.Read(8).selector60==-1&&factories==before,"real capacity refusal follows record60=-1 without a fake I/O request");Check(bool(q.Close()),"queue cleanup");
    Check(bool(q.Initialize(owner,10,[](const std::string&,std::unique_ptr<GameFileSource>&)->GameFsStatus{return {GameFsCode::Unavailable,0,"unimplemented provider"};})),"unknown provider fixture");
    r={true,true,123,456};Check(!q.Request(owner,10,0,view(root),view(name),r)&&r.wrapper_return==123&&q.Read(0).selector60==0&&q.Read(0).state==0&&!q.Read(0).buffer54,"unknown provider is not manufactured source failure/return-1");Check(bool(q.Close()),"unknown cleanup");

    io=std::make_shared<BackgroundFileState>();io->fail_close=true;Check(bool(q.Initialize(owner,11,[&](const std::string&,std::unique_ptr<GameFileSource>& out)->GameFsStatus{
        out=std::make_unique<BackgroundFile>(sgxd,io);Check(bool(out->Open()),"partial actual factory opens its file");throw std::runtime_error("synthetic factory exception after acquisition");})),"partial factory fixture");
    Check(!q.Request(owner,11,1,view(root),view(name),r)&&io->live&&!q.Close(),"throwing factory retains actual acquired file for failed-close retry");io->fail_close=false;Check(bool(q.Close())&&!io->live,"partial factory cleanup retry");
    // Different synthetic values across all four arrays; original BGM table is
    // external user data. Exactly one RNG call, with source one-step avoidance.
    std::vector<uint8_t> words;for(unsigned i=0;i<19;++i)W(words,(i*7+3)%25);auto lists=std::make_shared<Lease>(words);
    for(unsigned period=0;period<6;++period)for(uint64_t seed:{0,1,9}){
        RngNativeOwner rng;rng.ImportCurrentState64(seed,1,"synthetic BGM fixture");auto expected_rng=rng;
        const auto raw=expected_rng.NextC3234();const unsigned start=period<2?0:period==2?3:period==3?9:12;
        const unsigned modulo=period<2||period==3?2:period==2?5:6;
        const unsigned at=raw%modulo;const auto previous=int32_t(view(lists).ReadLE32((start+at)*4));GameBgmSelection selected;
        Check(bool(SelectBgm19B6C(period,previous,view(lists),rng,selected))&&selected.index==view(lists).ReadLE32((start+at+1)*4)
            &&selected.rng_calls==1&&selected.before==seed&&selected.after==expected_rng.CurrentSeed64()&&rng.CurrentSeed64()==selected.after,"source avoids old choice by next array entry, not another RNG draw");
    }
    RngNativeOwner rng;rng.ImportCurrentState64(2,2,"synthetic invalid BGM");GameBgmSelection untouched;untouched.index=99;
    Check(!SelectBgm19B6C(6,0,view(lists),rng,untouched)&&rng.CurrentSeed64()==2&&untouched.index==99,"unknown period refuses without invented return or random draw");
    Word(lists->b,0,25);Check(!SelectBgm19B6C(0,0,view(lists),rng,untouched)&&rng.CurrentSeed64()==2,"malformed current table refuses before RNG");Word(lists->b,0,3);
}
void BackgroundPreparationTests(){
    // Arbitrary synthetic names/geometry; no extracted registry/asset fixture.
    auto records=std::make_shared<Lease>(std::vector<uint8_t>(60));
    std::vector<std::pair<std::string,std::vector<uint8_t>>> packages;
    for(unsigned i=0;i<3;++i){const auto name=std::string("SYN_")+char('A'+i);
        std::copy(name.begin(),name.end(),records->b.begin()+i*20);
        packages.push_back({name+".BPM",BackgroundArchive({{name+"\\model01.DVS",Fixture()}})});
    }
    const auto file=BackgroundArchive(packages);
    auto owner=std::make_shared<int>(11),foreign=std::make_shared<int>(12);
    GameBackgroundPreparation job;GameGraphics graphics;Check(graphics.Initialize({}),"background offscreen renderer");
    const auto view=[&]{return PackageStorage::Attach(records,records->b.size());};
    const std::array<uint32_t,6> variants{{0,1,1,2,0,0}};
    for(uint32_t period=0;period<6;++period){
        auto io=std::make_shared<BackgroundFileState>();
        Check(bool(job.Begin(owner,period+1,{21,12,period,period+1},view(),std::make_unique<BackgroundFile>(file,io))),"real initialized pending background owner");
        auto pending=job.Snapshot();Check(pending.pending&&!pending.staged&&!pending.source_committed&&pending.resource==66+variants[period]&&pending.candidate==21,"live period lineage, not scene publication");
        auto foreign_control=std::shared_ptr<const void>(owner.get(),[](const void*){});
        Check(!job.Load(foreign_control,period+1)&&!job.Load(foreign,period+1)&&!job.Load(owner,period+2)&&io->reads==2,"foreign/stale epoch refused before member load");
        Check(bool(job.Load(owner,period+1)),"actual nested package/DVS clone load");
        auto ready=job.Snapshot();Check(ready.live&&ready.staged&&!ready.pending&&!ready.failed&&!ready.source_committed&&ready.model.vertices==3&&ready.variant==variants[period],"actual staged model only");
        Check(!job.Load(owner,period+1),"completed request cannot replay load");
        Check(graphics.Clear(0xFF000000)&&bool(job.Draw(owner,period+1,graphics,ModelIdentity()))&&graphics.Snapshot().mesh_pixels>0,"production staged clone render");
        Check(!job.Draw(foreign,period+1,graphics,ModelIdentity()),"foreign draw refused");
        Check(!job.Begin(owner,99,{1,12,0,99},view(),std::make_unique<BackgroundFile>(file,std::make_shared<BackgroundFileState>())),"pending/current cannot be overwritten");
        Check(bool(job.Close())&&io->closes==1&&!job.Snapshot().live,"clone/template retire before real file close");
        for(const auto& b:io->buffers)Check(!b->live,"every staged allocation retired");
        Check(!job.Draw(owner,period+1,graphics,ModelIdentity())&&bool(job.Close()),"retired draw and idempotent close");
    }
    auto io=std::make_shared<BackgroundFileState>();
    Check(!job.Begin(owner,0,{1,12,0,1},view(),std::make_unique<BackgroundFile>(file,io))&&!io->reads,"uninitialized epoch no I/O");
    Check(!job.Begin(owner,7,{1,11,0,7},view(),std::make_unique<BackgroundFile>(file,io)),"other selector unimplemented");
    Check(!job.Begin(owner,7,{1,12,6,7},view(),std::make_unique<BackgroundFile>(file,io)),"invalid period not default variant");
    records->b[0]='/';Check(!job.Begin(owner,7,{1,12,0,7},view(),std::make_unique<BackgroundFile>(file,io)),"unsafe registration no load");records->b[0]='S';
    Check(bool(job.Begin(owner,7,{1,12,0,7},view(),std::make_unique<BackgroundFile>(file,io))),"fresh owner after retirement");
    const auto reads=io->reads;records->b[1]='X';Check(!job.Load(owner,7)&&io->reads==reads&&!job.Snapshot().live,"current registration drift refuses before I/O");records->b[1]='Y';
    Check(bool(job.Close()),"drifted owner can retire");
    Check(!job.Begin(owner,7,{1,12,0,7},view(),std::make_unique<BackgroundFile>(file,io)),"retired epoch/request cannot rearm");
    io=std::make_shared<BackgroundFileState>();Check(bool(job.Begin(owner,8,{1,12,0,8},view(),std::make_unique<BackgroundFile>(file,io))),"failure pending");io->fail_read=true;
    Check(!job.Load(owner,8)&&job.Snapshot().failed&&!job.Snapshot().staged&&!job.Snapshot().source_committed,"failed load never becomes scene success");Check(bool(job.Close()),"failed load cleanup");
    io=std::make_shared<BackgroundFileState>();Check(bool(job.Begin(owner,9,{1,12,0,9},view(),std::make_unique<BackgroundFile>(file,io))),"weak owner pending");owner.reset();
    Check(!job.Snapshot().live&&!job.Load(foreign,9)&&bool(job.Close()),"owner expiration refuses, explicit file retirement");

    auto bad_model=Fixture();const auto parsed=dokodemo::model::parseDvs(bad_model);const auto node=parsed.nodes[0].source.offset;
    bad_model[node+4]=13;bad_model[node+5]=0;bad_model[node+6+74+6]=255;bad_model[node+6+74+7]=0;
    const auto bad_file=BackgroundArchive({{"SYN_A.BPM",BackgroundArchive({{"SYN_A\\model01.DVS",bad_model}})}});
    io=std::make_shared<BackgroundFileState>();Check(bool(job.Begin(foreign,10,{1,12,0,10},view(),std::make_unique<BackgroundFile>(bad_file,io))),"unadmitted marker tail pending");
    const auto unsupported=job.Load(foreign,10);const auto cut=job.Snapshot();
    Check(!unsupported&&unsupported.code==GameFsCode::Unavailable&&cut.failed&&cut.package_read&&!cut.staged&&!cut.source_committed&&cut.package_owner,"unadmitted marker tail refuses after package read, not ready");
    io->fail_close=true;Check(!job.Close()&&io->live&&!job.Snapshot().staged,"close failure retains backing for retry");
    Check(!job.Begin(foreign,11,{1,12,0,11},view(),std::make_unique<BackgroundFile>(file,std::make_shared<BackgroundFileState>())),"failed retirement cannot restart");
    io->fail_close=false;Check(bool(job.Close())&&!io->live,"explicit retirement retry succeeds");
    graphics.Close();
}

std::vector<uint8_t> AreaMarkerFixture(uint16_t flags=3,bool material=true,float geometry_scale=1){
    auto b=Fixture(false,nullptr,false,geometry_scale);auto parsed=dokodemo::model::parseDvs(b);
    if(material){
        const auto old=parsed.nodes[0].source.offset;
        b.insert(b.begin()+24,36,0);Word(b,16,15);Word(b,20,1);Word(b,old+36,uint32_t(b.size()/4));
        parsed=dokodemo::model::parseDvs(b);
    }
    const auto node=parsed.nodes[0].source.offset;std::vector<uint8_t> marker;
    W(marker,0);H(marker,13);H(marker,flags);H(marker,-1);H(marker,-1);H(marker,255);H(marker,0);
    // Synthetic pose and typed payload, not the extracted156B target nodes.
    for(const auto& v:std::array<GameVector4,4>{{{{5,8,11,1}},{{0,0,0,1}},{{0,0,.3826834324f,.9238795325f}},{{2,4,3,1}}}})
        for(auto x:v)F(marker,x);
    if(flags&1){auto optional=ModelIdentity();optional[12]=2;for(auto x:optional)F(marker,x);}
    H(marker,9);H(marker,7);H(marker,13);H(marker,material?0:255);W(marker,0x11223344);
    b.insert(b.begin()+node,marker.begin(),marker.end());Word(b,node,uint32_t((node+marker.size())/4));
    const auto mesh=node+marker.size();Word(b,mesh,uint32_t(b.size()/4));
    b[mesh+8]=b[mesh+9]=0; // geometry child really inherits marker0
    uint32_t one;const float f=1;std::memcpy(&one,&f,4);Word(b,mesh+16,one);
    b[parsed.bounds.source.offset+parsed.bounds.source.bytes]=2; // node count
    return b;
}
void AreaMarkerTests(){
    constexpr auto windows_basis=GameModelTransformPolicy::WindowsAreaMarkerCanonicalBasis;
    const auto near=[](float a,float b){return std::abs(a-b)<.0003f;};
    auto bytes=AreaMarkerFixture();const auto parsed=dokodemo::model::parseDvs(bytes);
    Check(parsed.nodes.size()==2&&parsed.nodes[0].areaMarker.field208==9&&parsed.nodes[0].areaMarker.field20C==7&&parsed.nodes[0].areaMarker.field214==13&&parsed.nodes[0].areaMarker.word210==0x11223344&&parsed.nodes[0].areaMarker.material==0,"source typed12B marker parse");
    auto lease=std::make_shared<Lease>(bytes);auto asset=std::make_shared<GameModelAsset>();
    const auto strict=asset->Decode(PackageStorage::Attach(lease,lease->b.size()));
    Check(!strict&&strict.code==GameFsCode::Unavailable&&!asset->Live(),"unknown PSP flag2 basis is refused by default");
    Check(bool(asset->Decode(PackageStorage::Attach(lease,lease->b.size()),nullptr,{},windows_basis)),"finite area-marker model load");
    Check(asset->Statistics().area_markers==1&&asset->Statistics().meshes==1&&asset->Statistics().vertices==3,"marker is typed metadata, no manufactured geometry");
    GameModelClone clone,second;Check(bool(clone.Initialize(asset))&&bool(second.Initialize(asset)),"independent marker clones share live immutable template");
    auto root=ModelIdentity();root[12]=100;root[13]=200;root[14]=300;
    Check(bool(clone.Update(root)),"marker oriented-scale hierarchy update");std::vector<GameAreaMarkerState> markers;
    Check(bool(clone.AreaMarkers(*asset,markers))&&markers.size()==1,"current owned marker-list consumer");const auto m=markers[0];
    Check(m.node==0&&m.parent==-1&&m.material==0&&m.parameters.word210==0x11223344&&near(m.world[0],3)&&near(m.world[1],1)&&near(m.world[4],1)&&near(m.world[5],3)&&near(m.world[10],3),"R O S inverseO order under named native canonical-basis policy");
    Check(near(m.world[12],105)&&near(m.world[13],208)&&near(m.final_matrix[12],111)&&near(m.final_matrix[13],210),"optional final matrix follows world, not a second parent");
    const auto& pose=static_cast<const GameModelClone&>(clone).Pose();Check(near(pose[1].world[12],108)&&near(pose[1].world[13],209),"child inherits marker world without optional correction twice");
    Check(bool(second.AreaMarkers(*asset,markers))&&near(markers[0].world[12],5),"clone transforms do not alias");
    GameModelAsset foreign;markers[0].node=999;Check(!clone.AreaMarkers(foreign,markers)&&markers[0].node==999,"foreign template cannot replace current copied receipt");
    auto nonfinite_root=root;nonfinite_root[12]=std::numeric_limits<float>::infinity();
    Check(!clone.Update(nonfinite_root)&&!clone.AreaMarkers(*asset,markers),"nonfinite composed marker matrix refuses receipt");
    Check(bool(clone.Update(root)),"valid explicit root restores pose");
    auto& retained_pose=clone.Pose();Check(bool(clone.Update(root)),"retained alias baseline update");
    retained_pose[0].position[0]=99;Check(!clone.AreaMarkers(*asset,markers),"retained input alias cannot reuse a stale marker receipt");
    retained_pose[0].position[0]=5;Check(bool(clone.Update(root)),"explicit producer restores consistent pose");
    retained_pose[0].world[12]+=1;Check(!clone.AreaMarkers(*asset,markers),"retained world alias drift is checked directly");
    Check(bool(clone.Update(root)),"world drift repaired only by explicit update");
    clone.Pose()[0].position[0]=6;Check(!clone.AreaMarkers(*asset,markers)&&bool(clone.Update(root))&&bool(clone.AreaMarkers(*asset,markers))&&near(markers[0].world[12],106),"mutable pose invalidates receipt until real update");
    clone.Pose()[0].material=-1;Check(bool(clone.Update(root))&&!clone.AreaMarkers(*asset,markers),"unclosed marker material writer refuses");clone.Pose()[0].material=0;
    clone.Pose()[0].scale_orientation={{0,0,0,0}};Check(!clone.Update(root)&&!clone.AreaMarkers(*asset,markers),"invalid quaternion never publishes marker matrix");clone.Pose()[0].scale_orientation={{0,0,.3826834324f,.9238795325f}};
    Check(bool(clone.Update(ModelIdentity())),"recover only after valid explicit pose update");
    GameGraphics graphics;Check(graphics.Initialize({})&&graphics.Clear(0xFF000000),"marker child raster");auto camera=ModelIdentity();camera[0]=camera[5]=camera[10]=.1f;camera[12]=-.6f;camera[13]=-.8f;camera[14]=-1.1f;
    const auto pixels=graphics.Snapshot().mesh_pixels;Check(bool(clone.Draw(graphics,camera))&&graphics.Snapshot().mesh_pixels>pixels,"geometry child renders through actual transformed marker parent");graphics.Close();
    for(uint16_t flags:{uint16_t(0),uint16_t(1),uint16_t(2)}){
        auto l=std::make_shared<Lease>(AreaMarkerFixture(flags,false));auto a=std::make_shared<GameModelAsset>();GameModelClone c;
        Check(bool(a->Decode(PackageStorage::Attach(l,l->b.size()),nullptr,{},flags&2?windows_basis:GameModelTransformPolicy::SourceCommon))&&bool(c.Initialize(a))&&bool(c.AreaMarkers(*a,markers)),"closed flags0..3 and material255 null binding");
        Check(markers[0].material==-1&&near(markers[0].world[0],flags&2?3.f:1.f),"source flag2 controls scale consumption");
        c.Close();Check(!c.AreaMarkers(*a,markers),"closed marker owner refuses");
    }
    for(unsigned kind=0;kind<4;++kind){
        auto b=AreaMarkerFixture();const auto p=dokodemo::model::parseDvs(b);const auto n=p.nodes[0].source.offset;const auto end=n+p.nodes[0].source.bytes;
        if(kind==0){b[n+6]=7;} // unknown common flag4
        if(kind==1){b[end-6]=3;} // foreign material index
        if(kind==2){Word(b,n,uint32_t((end-4)/4));} // truncated payload
        if(kind==3){b[n+8]=1;b[n+9]=0;} // actual parent cycle through geometry child
        auto l=std::make_shared<Lease>(b);auto a=std::make_shared<GameModelAsset>();auto status=a->Decode(PackageStorage::Attach(l,l->b.size()),nullptr,{},windows_basis);GameModelClone c;
        Check(!status||!c.Initialize(a),"malformed marker bounds/flags/material/cycle refuse");
    }
    lease->live=false;Check(!clone.AreaMarkers(*asset,markers)&&!clone.Update(root)&&!second.AreaMarkers(*asset,markers),"retired source template refuses every marker clone");clone.Close();second.Close();
}
struct ContactTextureSource final:GameResourceSource {
    uint8_t alpha=0;
    GameFsStatus OpenResource(const std::string& path,PackageView& out) override {
        if(path!="CONTACT_TEST.GIM")return {GameFsCode::NotFound,0,"synthetic contact texture path"};
        // Tiny synthetic 2x2 white RGBA image; no original texture pixels.
        std::vector<uint8_t> b(144,0);const char magic[]="MIG.00.1PSP";
        std::copy(std::begin(magic),std::end(magic),b.begin());
        const auto half=[&](size_t at,uint16_t v){b.at(at)=uint8_t(v);b.at(at+1)=uint8_t(v>>8);};
        for(size_t at:{16u,32u}){half(at,at==16?2:3);half(at+2,16);Word(b,at+4,uint32_t(b.size()-at));Word(b,at+8,16);Word(b,at+12,16);}
        half(48,4);half(50,16);Word(b,52,96);Word(b,56,96);Word(b,60,16);
        half(64,48);half(68,3);half(72,2);half(74,2);half(76,32);half(78,4);half(80,1);
        Word(b,88,48);Word(b,92,64);Word(b,96,80);half(104,1);half(106,1);half(108,3);half(110,1);Word(b,112,64);
        for(size_t at=128;at<144;at+=4)Word(b,at,0x00FFFFFFu|uint32_t(alpha)<<24);
        auto lease=std::make_shared<Lease>(std::move(b));out=PackageStorage::Attach(lease,lease->b.size());return {};
    }
};
bool Near(float a, float b) { return std::abs(a - b) < .0001f; }
void MathAndSkin() {
    const auto camera_rotation = ModelQuaternion({{-.70710678f, 0, 0, .70710678f}});
    const auto forward = ModelTransform(camera_rotation, {{0, 0, -1, 0}});
    Check(Near(forward[0], 0) && Near(forward[1], 1) && Near(forward[2], 0),
          "source conjugated quaternion maps camera forward+Y");
    auto a = ModelIdentity();
    a[12] = 3;
    a[13] = -7;
    a[14] = 11;
    const auto p = ModelTransform(ModelMultiply(a, ModelInverseRigid(a)), {{1, 2, 3, 1}});
    Check(Near(p[0], 1) && Near(p[1], 2) && Near(p[2], 3), "rigid bind inverse cancels translation");
    dokodemo::model::Geometry g;
    g.positions.push_back({1, 2, 3});
    g.weightRecords.assign(24, 0);
    Word(g.weightRecords, 0, 0xFFFF0001);
    Word(g.weightRecords, 4, 0xFFFFFFFF);
    Word(g.weightRecords, 8, 0x7FC00000);
    std::vector<GameMatrix> palette{ModelIdentity(), a};
    auto rigid = GameModelClone::Skin(g, 0, palette);
    Check(Near(rigid[0], 4) && Near(rigid[1], -5) && Near(rigid[2], 14),
          "index1negative ignores stored weight0 as actual CPU branch");
    Word(g.weightRecords, 0, 0x00010000);
    Word(g.weightRecords, 8, 0x3E800000);
    Word(g.weightRecords, 12, 0x3F400000);
    auto weighted = GameModelClone::Skin(g, 0, palette);
    Check(Near(weighted[0], 3.25f) && Near(weighted[1], -3.25f) && Near(weighted[2], 11.25f),
          "two palette weighted positions");
    Word(g.weightRecords, 0, 0xFFFF0002);
    bool refused = false;
    try {
        (void)GameModelClone::Skin(g, 0, palette);
    } catch (const std::exception &) {
        refused = true;
    }
    Check(refused, "out of palette does not read native memory");
}
void GeometryAndRaster() {
    auto lease = std::make_shared<Lease>(Fixture());
    auto asset = std::make_shared<GameModelAsset>();
    Check(bool(asset->Decode(PackageStorage::Attach(lease, lease->b.size()))),
          "checked DVS geometry consumer");
    Check(asset->Statistics().vertices == 3 && asset->Statistics().triangles == 1, "typed geometry arrays");
    GameModelClone a, b;
    Check(bool(a.Initialize(asset)) && bool(b.Initialize(asset)),
          "shared immutable asset,two actual mutable clones");
    a.Pose()[0].position[0] = .2f;
    Check(bool(a.Update(ModelIdentity())) && b.Pose()[0].position[0] == 0,
          "pose mutation never aliases another clone");
    GameGraphics graphics;
    GameGraphicsConfig config;
    config.width = config.height = config.stride = 32;
    config.scissor_right = config.scissor_bottom = 32;
    config.cull_face = false;
    Check(graphics.Initialize(config) && graphics.Clear(0xFF000000), "real raster surface");
    Check(bool(a.Draw(graphics, ModelIdentity())) && graphics.SwapBuffers(), "model Draw reaches renderer");
    Check(graphics.Snapshot().mesh_pixels > 0 && graphics.Snapshot().mesh_triangles == 1,
          "actual model triangle writes pixels");
    const uint32_t colors[]{0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF};
    GameTextureSpan texture{colors, 2, 2, 4};
    GameRasterPolicy policy;
    policy.two_sided = true;
    std::array<GameMeshVertex, 3> triangle{{{{{-.9f, -.9f, 0, 1}}, 0, 1, 0xFFFFFFFF},
                                            {{{.9f, -.9f, 0, 1}}, 1, 1, 0xFFFFFFFF},
                                            {{{0, .9f, -2, 1}}, .5f, 0, 0xFFFFFFFF}}};
    const auto before = graphics.Snapshot();
    Check(graphics.DrawMeshTriangle(triangle, texture, policy),
          "near-plane intersection clips rather than discards full triangle");
    Check(graphics.Snapshot().mesh_pixels > before.mesh_pixels && graphics.Snapshot().textured_triangles > 0,
          "clipped textured triangle writes actual samples");
    auto bad = triangle;
    bad[0].clip[0] = std::numeric_limits<float>::quiet_NaN();
    Check(!graphics.DrawMeshTriangle(bad, texture, policy), "nonfinite clip coordinate rejected");
    texture.count = 3;
    Check(!graphics.DrawMeshTriangle(triangle, texture, policy), "texture extent mismatch refused");
    lease->live = false;
    Check(!a.Draw(graphics, ModelIdentity()) && !b.Update(ModelIdentity()),
          "retired source lease cannot draw or animate");
    a.Close();
    b.Close();
    graphics.Close();
    Check(!graphics.Snapshot().live, "all clone/surface owners retire");
    auto malformed = Fixture();
    malformed[0] = 0;
    auto badlease = std::make_shared<Lease>(std::move(malformed));
    GameModelAsset badasset;
    Check(!badasset.Decode(PackageStorage::Attach(badlease, badlease->b.size())) && !badasset.Live(),
          "malformed source never publishes a live model");
}
void TexturedColorApproximation(){
    GameGraphics graphics;GameGraphicsConfig config;
    config.width=config.height=config.stride=16;
    config.scissor_right=config.scissor_bottom=16;
    config.cull_face=false;
    Check(graphics.Initialize(config),"color adapter raster initializes");
    const auto sample=[&](uint32_t vertex,uint32_t texel,int scale,bool textured=true){
        Check(graphics.Clear(0),"color adapter clear");
        std::array<GameMeshVertex,3> vertices{{
            {{{-1,-1,0,1}},0,0,vertex},{{{1,-1,0,1}},0,0,vertex},{{{0,1,0,1}},0,0,vertex}}};
        GameRasterPolicy policy;policy.two_sided=true;policy.blend=false;
        if(scale>=0)policy.windows_dvs_textured_modulation2x=scale!=0;
        const GameTextureSpan texture=textured?GameTextureSpan{&texel,1,1,1}:GameTextureSpan{};
        Check(graphics.DrawMeshTriangle(vertices,texture,policy)&&graphics.SwapBuffers(),"color adapter actual draw");
        GameGraphicsFrame frame;Check(graphics.CopyFront(frame),"color adapter actual readback");
        return frame.pixels[8*frame.stride+8];
    };
    Check(sample(0xFF7F7C6D,0xFFFDFDFD,false)==0xFF7E7B6C,"default/UI path retains1x modulation");
    Check(sample(0xFF808080,0xFFE0D9C9,-1)==0xFF706D65,"untouched default raster/UI policy remains1x");
    Check(sample(0xFF808080,0xFFE0D9C9,true)==0xFFE1DACA,"room neutral128 preserves actual fridge texture tone");
    Check(sample(0xFF808080,0xFF463329,true)==0xFF463329,"room neutral128 preserves actual floor texture tone");
    Check(sample(0xFF565656,0xFFE0D9C9,true)==0xFF979288,"room baked darker vertex colour remains darker under2x");
    Check(sample(0xFF7F7C6D,0xFFFDFDFD,true)==0xFFFCF6D8,"Toro real neutral texel reaches body within1 RGB");
    Check(sample(0xFFFDF7D9,0xFFFFFFFF,true,false)==0xFFFDF7D9,"untextured body stays source RGB");
    Check(sample(0x80C8C8C8,0x80404040,true)==0x40646464,"post-product2x preserves dark texels and alpha product");
    Check(sample(0xFFC8C8C8,0xFFFFFFFF,true)==0xFFFFFFFF,"post-product saturation clamps RGB");
}
void NormalLighting(){
    auto scale=ModelIdentity();scale[0]=2;scale[10]=3;
    scale[12]=99;scale[13]=-71;
    const auto normal=ModelTransformNormal(scale,{{1,1,0,0}});
    Check(Near(normal[0],.5f)&&Near(normal[1],1)&&normal[3]==0,"normal inverse transpose ignores translation");
    const auto tangent=ModelTransform(scale,{{1,-1,0,0}});
    Check(Near(normal[0]*tangent[0]+normal[1]*tangent[1],0),"nonuniform normal stays perpendicular to transformed tangent");
    auto rotate=ModelIdentity();rotate[0]=rotate[5]=0;rotate[1]=1;rotate[4]=-1;
    const auto turned=ModelTransformNormal(rotate,{{1,0,0,0}});
    Check(Near(turned[0],0)&&Near(turned[1],1),"normal follows bone rotation");
    auto shear=ModelIdentity();shear[4]=2;
    const auto sheared=ModelTransformNormal(shear,{{1,0,0,0}});
    Check(Near(sheared[0],1)&&Near(sheared[1],-2),"normal cofactor orientation handles shear");
    auto reflection=ModelIdentity();reflection[0]=-1;
    const auto reflected=ModelTransformNormal(reflection,{{1,0,0,0}});
    Check(Near(reflected[0],-1),"normal reflection preserves negative determinant sign");
    auto singular=ModelIdentity();singular[0]=0;bool refused=false;
    try{(void)ModelTransformNormal(singular,{{1,0,0,0}});}catch(const std::exception&){refused=true;}
    Check(refused,"singular normal matrix refuses");
    dokodemo::model::Geometry g;g.positions.push_back({0,0,0});g.normals.push_back({126,126,0});
    g.weightRecords.assign(24,0);Word(g.weightRecords,0,0x00010000);Word(g.weightRecords,4,0xFFFFFFFF);
    Word(g.weightRecords,8,0x3F000000);Word(g.weightRecords,12,0x3F000000);
    std::vector<GameMatrix> palette{scale,ModelIdentity()};
    const auto blended=GameModelClone::SkinNormal(g,0,palette);
    Check(Near(blended[0],.6f)&&Near(blended[1],.8f),"skin blends unnormalized contributions before final normalization");
    Word(g.weightRecords,0,0xFFFF0001);Word(g.weightRecords,8,0x7FC00000);
    const auto rigid=GameModelClone::SkinNormal(g,0,palette);
    Check(Near(rigid[0],.70710678f)&&Near(rigid[1],.70710678f),"rigid skin normal ignores unused NaN weights");
    Word(g.weightRecords,0,0xFFFF0002);refused=false;
    try{(void)GameModelClone::SkinNormal(g,0,palette);}catch(const std::exception&){refused=true;}
    Check(refused,"normal palette bounds refuse");
    const GameVector4 key{{-.3f,-.7f,.65f,0}},away{{.3f,.7f,-.65f,0}};
    Check(ModelWindowsActorSoftDiffuse(0x80FFFFFF,key)==0x80FFFFFF&&
          ModelWindowsActorSoftDiffuse(0x80FFFFFF,away)==0x80B8B8B8,"soft key/ambient preserve alpha without overbrightening");
    auto lease=std::make_shared<Lease>(Fixture(true));auto asset=std::make_shared<GameModelAsset>();
    Check(bool(asset->Decode(PackageStorage::Attach(lease,lease->b.size()))),"actual normal-bearing DVS admitted");
    GameModelClone clone;Check(bool(clone.Initialize(asset)),"normal-bearing clone initialized");
    GameGraphics graphics;GameGraphicsConfig config;config.width=config.height=config.stride=32;
    config.scissor_right=config.scissor_bottom=32;config.cull_face=false;
    Check(graphics.Initialize(config),"normal draw surface initialized");
    const auto sample=[&](GameModelLightingPolicy policy){
        Check(graphics.Clear(0)&&bool(clone.Draw(graphics,ModelIdentity(),GameModelColorPolicy::SourceRgb,policy))&&graphics.SwapBuffers(),"normal policy actual model draw");
        GameGraphicsFrame frame;Check(graphics.CopyFront(frame),"normal actual framebuffer readback");
        return frame.pixels[16*frame.stride+16];
    };
    Check(sample(GameModelLightingPolicy::Unlit)==0xFFFFFFFF,"normal-bearing default model stays unlit");
    const auto lit=sample(GameModelLightingPolicy::WindowsActorSoftDiffuse);
    Check((lit>>24)==255&&(lit&255)>184&&(lit&255)<255,"actor opt-in shades actual source-normal triangle");
    lease->live=false;
    Check(!clone.Draw(graphics,ModelIdentity(),GameModelColorPolicy::SourceRgb,GameModelLightingPolicy::WindowsActorSoftDiffuse),"lighting cannot draw a retired source lease");
    auto plain_lease=std::make_shared<Lease>(Fixture());auto plain_asset=std::make_shared<GameModelAsset>();
    Check(bool(plain_asset->Decode(PackageStorage::Attach(plain_lease,plain_lease->b.size()))),"normal-free fixture admitted");
    GameModelClone plain;Check(bool(plain.Initialize(plain_asset)),"normal-free clone initialized");
    Check(graphics.Clear(0)&&bool(plain.Draw(graphics,ModelIdentity()))&&graphics.SwapBuffers(),"actual default lighting argument");
    GameGraphicsFrame original;Check(graphics.CopyFront(original),"default model frame readback");
    Check(graphics.Clear(0)&&bool(plain.Draw(graphics,ModelIdentity(),GameModelColorPolicy::SourceRgb,
        GameModelLightingPolicy::WindowsActorSoftDiffuse))&&graphics.SwapBuffers(),"normal-free actor policy draw");
    GameGraphicsFrame unchanged;Check(graphics.CopyFront(unchanged)&&original.pixels==unchanged.pixels,
        "absent source normals stay original without fabricated replacement normals");
}
void ContactQueriesAndDecal(){
    auto floor_lease=std::make_shared<Lease>(Fixture());auto floor_asset=std::make_shared<GameModelAsset>();
    Check(bool(floor_asset->Decode(PackageStorage::Attach(floor_lease,floor_lease->b.size()))),"contact floor fixture admitted");
    GameModelClone floor;Check(bool(floor.Initialize(floor_asset)),"contact floor clone");
    auto root=ModelIdentity();root[14]=2.5f;Check(bool(floor.Update(root)),"floor has actual nonzero support height");
    bool found=false;float height=0;
    Check(bool(floor.HorizontalSurfaceBelow(0,0,3,2,found,height))&&found&&Near(height,2.5f),"find actual posed floor rather than assume Z0");
    Check(bool(floor.HorizontalSurfaceBelow(3,0,3,2,found,height))&&!found,"outside floor has no support");
    Check(bool(floor.HorizontalSurfaceBelow(0,0,2,2,found,height))&&!found,"ceiling rejects surface above actor");
    floor.Pose()[0].opacity=.5f;Check(bool(floor.Update(root)),"translucent floor pose");
    Check(bool(floor.HorizontalSurfaceBelow(0,0,3,2,found,height))&&!found,"translucent receiver is not opaque ground");
    floor.Pose()[0].opacity=1;root=ModelIdentity();root[5]=root[10]=0;root[6]=1;root[9]=-1;
    Check(bool(floor.Update(root))&&bool(floor.HorizontalSurfaceBelow(0,0,3,4,found,height))&&!found,"vertical wall is not horizontal support");
    const std::vector<std::array<int16_t,3>> body{{{-20000,-20000,0}},{{20000,-20000,0}},{{0,20000,30000}},{{0,0,-32000}}};
    auto actor_lease=std::make_shared<Lease>(Fixture(false,&body));auto actor_asset=std::make_shared<GameModelAsset>();
    Check(bool(actor_asset->Decode(PackageStorage::Attach(actor_lease,actor_lease->b.size()))),"posed contact fixture includes unused low vertex");
    GameModelClone actor;Check(bool(actor.Initialize(actor_asset)),"contact actor clone");root=ModelIdentity();root[12]=4;root[13]=5;root[14]=6;
    Check(bool(actor.Update(root)),"contact actor translated pose");GameContactFootprint footprint;
    Check(bool(actor.ContactFootprint(footprint))&&footprint.valid&&Near(footprint.foot_z,6)&&Near(footprint.body_height,30000.f/32767),
          "bottom uses referenced posed triangles, ignores unused low vertex and works without normals");
    Check(Near(footprint.x,4)&&footprint.y<5&&footprint.radius_x>0&&footprint.radius_y>0,"footprint follows actual lower-envelope XY");
    for(uint8_t alpha:{uint8_t(0),uint8_t(128),uint8_t(255)}){
        ContactTextureSource source;source.alpha=alpha;
        auto image=std::make_shared<Lease>(Fixture(false,&body,true));auto textured=std::make_shared<GameModelAsset>();
        Check(bool(textured->Decode(PackageStorage::Attach(image,image->b.size()),&source)),"synthetic alpha-bound contact model");
        GameModelClone caster;Check(bool(caster.Initialize(textured)),"textured caster clone");GameContactFootprint patch;
        Check(bool(caster.ContactFootprint(patch))&&patch.valid==(alpha==255),"only fully opaque diffuse texture is eligible as contact caster");
        GameGraphics raster;GameGraphicsConfig c;c.width=c.height=c.stride=16;c.scissor_right=c.scissor_bottom=16;c.cull_face=false;
        Check(raster.Initialize(c)&&raster.Clear(0)&&bool(caster.Draw(raster,ModelIdentity())),"alpha caster actual model draw");
        Check((raster.Snapshot().mesh_pixels>0)==(alpha!=0),"zero-alpha diffuse texture draws no pixels and casts no contact");
    }
    actor_lease->live=false;Check(!actor.ContactFootprint(footprint),"retired actor cannot produce contact");
    floor_lease->live=false;Check(!floor.HorizontalSurfaceBelow(0,0,3,4,found,height),"retired floor cannot produce support");
    GameGraphics graphics;GameGraphicsConfig config;config.width=config.height=config.stride=16;
    config.scissor_right=config.scissor_bottom=16;config.cull_face=false;
    Check(graphics.Initialize(config)&&graphics.Clear(0xFF808080),"contact raster surface");
    const auto camera=ModelIdentity();
    Check(graphics.DrawContactEllipse(camera,0,0,0,.8f,.8f,.5f)&&graphics.SwapBuffers(),"empty-depth decal call");
    GameGraphicsFrame frame;Check(graphics.CopyFront(frame)&&frame.pixels[7*16+7]==0xFF808080,"no rendered receiver means no shadow");
    const auto triangle=[&](std::array<GameVector4,3> p,uint32_t color){std::array<GameMeshVertex,3> v{};
        for(unsigned i=0;i<3;++i){v[i].clip=p[i];v[i].color=color;}GameRasterPolicy policy;policy.two_sided=true;
        Check(graphics.DrawMeshTriangle(v,{},policy),"contact receiver/occluder triangle");};
    Check(graphics.Clear(0xFF000000),"contact receiver clear");
    triangle({{{{-1,-1,0,1}},{{1,-1,0,1}},{{1,1,0,1}}}},0xFF808080);
    triangle({{{{-1,-1,0,1}},{{1,1,0,1}},{{-1,1,0,1}}}},0xFF808080);
    triangle({{{{.1f,-.4f,-.5f,1}},{{.9f,-.4f,-.5f,1}},{{.5f,.5f,-.5f,1}}}},0xFFFF0000);
    Check(graphics.DrawContactEllipse(camera,0,0,.00002f,.8f,.8f,.5f),"single-coverage depth-matched procedural decal");
    triangle({{{{-1,-1,.00002f,1}},{{1,-1,.00002f,1}},{{0,1,.00002f,1}}}},0xFF00FF00);
    Check(graphics.SwapBuffers()&&graphics.CopyFront(frame),"contact raster actual readback");
    const auto centre=frame.pixels[7*16+7];
    Check((centre&255)<128&&(centre&255)>50,"soft contact darkens rendered floor exactly once");
    Check(frame.pixels[8*16+12]==0xFFFF0000,"nearer occluder is untouched by floor decal");
    Check(frame.pixels[8*16+7]!=0xFF00FF00,"contact did not overwrite receiver depth with slightly farther plane");
    Check(!graphics.DrawContactEllipse(camera,0,0,0,-1,1,.5f),"invalid contact extent refused");
}
void Type12RegionDescriptor(){
    auto bytes=Fixture();Word(bytes,92,2);const auto node=bytes.size();W(bytes,0);H(bytes,12);
    for(auto v:{0,0,-1,255,0})H(bytes,v);
    for(const auto& row:std::array<GameVector4,4>{{{{3,4,5,1}},{{0,0,0,1}},{{0,0,0,1}},{{1,1,1,1}}}})
        for(auto v:row)F(bytes,v);
    H(bytes,1);H(bytes,1);F(bytes,1.25f);F(bytes,2.5f);Word(bytes,node,uint32_t(bytes.size()/4));
    auto lease=std::make_shared<Lease>(std::move(bytes));auto asset=std::make_shared<GameModelAsset>();
    Check(bool(asset->Decode(PackageStorage::Attach(lease,lease->b.size())))&&asset->Statistics().particle_emitters==1,
          "type12 source region payload admission");
    GameModelClone a,b;Check(bool(a.Initialize(asset))&&bool(b.Initialize(asset)),"emitter fields copied to separate native clones");
    Check(a.Pose()[1].emitter.first==1&&a.Pose()[1].emitter.second==1&&Near(a.Pose()[1].emitter.scalar0,1.25f),"exact12B selector/shape/dimension payload");
    a.Pose()[1].emitter.first=99;Check(b.Pose()[1].emitter.first==1,"type12 descriptor clone ownership");
    a.Pose()[0].position[0]=10;Check(bool(a.Update(ModelIdentity()))&&Near(a.Pose()[1].world[12],13),"emitter common parent transform propagated");
    Check(asset->Statistics().triangles==1,"particle descriptor creates no fabricated triangle/emission");
    GamePositionRegions regions;Check(bool(regions.Build(b))&&regions.Lists()[1].size()==1,"compile actual cloned type12 selection");
    Check(bool(regions.Build(a))&&regions.Lists()[1].empty(),"classifier uses current cloned field,not immutable source payload");
    lease->live=false;Check(!a.Update(ModelIdentity()),"retired emitter source refuses transform update");
}
std::vector<uint8_t> MotionFixture(bool unknown = false) {
    std::vector<uint8_t> b{'D', 'A', 'K', 0};
    W(b, 0);
    W(b, 1);
    W(b, 0);
    W(b, 160);
    const auto object = b.size();
    W(b, 0);
    H(b, 0);
    H(b, unknown ? 1 : 3);
    for (const auto &row :
         std::array<GameVector4, 4>{{{{0, 0, 0, 1}}, {{0, 0, 0, 1}}, {{0, 0, 0, 1}}, {{1, 1, 1, 1}}}})
        for (auto v : row)
            F(b, v);
    if(unknown){
        b.push_back(0x50);b.push_back(0);H(b,1);
        for(uint32_t v:{0u,1u,2u,3u})W(b,v);
        Word(b,object,uint32_t(b.size()/4));return b;
    }
    b.push_back(1);
    b.push_back(1);
    H(b, 2);
    for (auto v : {10.0f, 20.0f, 30.0f, 2.0f})
        F(b, v);
    W(b, 0);
    W(b, 0);
    W(b, 0);
    W(b, 160);
    W(b, 0xFFFFFFFF);
    W(b, 65535);
    b.push_back(2);
    b.push_back(1);
    H(b, 2);
    W(b, 0);
    W(b, 0);
    W(b, 0x7FFF0000);
    W(b, 160);
    W(b, 0);
    W(b, 0x5A825A82);
    b.push_back(0x30);
    b.push_back(0);
    H(b, 1);
    W(b, 0);
    W(b, 255);
    Word(b, object, uint32_t(b.size() / 4));
    return b;
}
void Motion() {
    auto lease = std::make_shared<Lease>(Fixture());
    auto asset = std::make_shared<GameModelAsset>();
    Check(bool(asset->Decode(PackageStorage::Attach(lease, lease->b.size()))), "motion target asset");
    GameModelClone clone;
    Check(bool(clone.Initialize(asset)), "motion clone");
    auto keys = std::make_shared<Lease>(MotionFixture());
    GameModelMotion motion;
    Check(bool(motion.Decode(PackageStorage::Attach(keys, keys->b.size()), *asset)), "finite DAK consumer");
    Check(bool(motion.Apply(clone, 0)) && Near(clone.Pose()[0].position[0], 10), "actual first key at time0");
    Check(bool(motion.Apply(clone, 80)) && Near(clone.Pose()[0].position[0], 11) &&
              Near(clone.Pose()[0].position[2], 31),
          "unsigned packed translation,prefix and integer time");
    Check(clone.Pose()[0].material == -1, "stepped255 material null");
    Check(Near(clone.Pose()[0].rotation[2], .38268f), "native shortest quaternion slerp");
    Check(bool(motion.Apply(clone, 160)) && Near(clone.Pose()[0].position[1], 22),
          "last key not a float time");
    GameModelPlayback channel;
    channel.Configure(motion.Statistics().duration,false);channel.Start();
    Check(!channel.Complete()&&bool(channel.Apply(motion,clone,160))&&channel.Snapshot().sampled_time==0
          &&channel.Snapshot().time==160&&!channel.Complete(),"admission/first actual pose is not channel completion");
    Check(bool(channel.Apply(motion,clone,160))&&channel.Complete()&&channel.Snapshot().sampled_time==160
          &&Near(clone.Pose()[0].position[1],22),"nonloop completes only after applying actual end pose");
    clone.Pose()[0].position[1]=-123;
    Check(bool(channel.Apply(motion,clone,160))&&channel.Complete()&&Near(clone.Pose()[0].position[1],22)
          &&channel.Snapshot().time==160,"finished2 reapplies final pose without advancing");
    channel.Start();Check(!channel.Complete()&&channel.Snapshot().time==0&&channel.Snapshot().loops==0,
          "89954 restart clears elapsed time and loop count");
    auto looping=MotionFixture();Word(looping,12,1);
    auto looping_keys=std::make_shared<Lease>(std::move(looping));GameModelMotion loop;
    Check(bool(loop.Decode(PackageStorage::Attach(looping_keys,looping_keys->b.size()),*asset)),"source file loop flag admitted");
    GameModelPlayback loop_channel;loop_channel.Configure(160,true);loop_channel.Start();
    for(unsigned i=0;i<7;++i)Check(bool(loop_channel.Apply(loop,clone,160))&&!loop_channel.Complete(),"loop duration never signals command ready");
    Check(loop_channel.Snapshot().loops==6&&loop_channel.Snapshot().state==3,"source loop reset and continue increment");
    loop_channel.Stop();const auto stopped=loop_channel.Snapshot().samples;
    Check(bool(loop_channel.Apply(loop,clone,160))&&loop_channel.Snapshot().samples==stopped&&!loop_channel.Complete(),
          "scene stop state1 is inactive,not finished2");
    std::vector<uint8_t> opacity{'D','A','K',0};W(opacity,2004072701);W(opacity,1);W(opacity,0);W(opacity,160);
    const auto opacity_object=opacity.size();W(opacity,0);H(opacity,0);H(opacity,1);
    for(const auto& row:std::array<GameVector4,4>{{{{0,0,0,1}},{{0,0,0,1}},{{0,0,0,1}},{{1,1,1,1}}}})
        for(auto value:row)F(opacity,value);
    opacity.push_back(0x50);opacity.push_back(15);H(opacity,2);
    W(opacity,0);F(opacity,0);F(opacity,0);W(opacity,0x7FC00000);
    W(opacity,160);F(opacity,1);W(opacity,0x7FC00000);F(opacity,0);
    Word(opacity,opacity_object,uint32_t(opacity.size()/4));
    auto alpha_keys=std::make_shared<Lease>(opacity);GameModelMotion alpha;
    Check(bool(alpha.Decode(PackageStorage::Attach(alpha_keys,alpha_keys->b.size()),*asset))
          &&bool(alpha.Apply(clone,80))&&clone.Pose()[0].opacity==0,
          "exact88E0C outgoing quiet-NaN tangent holds left opacity,not a ramp");
    Check(bool(alpha.Apply(clone,160))&&clone.Pose()[0].opacity==1,"opacity reaches next actual key");
    Word(opacity,opacity_object+88,0); // first OUT tangent finite;next IN remains NaN
    auto incoming_keys=std::make_shared<Lease>(opacity);GameModelMotion incoming;
    Check(bool(incoming.Decode(PackageStorage::Attach(incoming_keys,incoming_keys->b.size()),*asset))
          &&!incoming.Apply(clone,80),"unclosed incoming-only NaN does not become a fabricated scalar");
    keys->live = false;
    Check(!motion.Apply(clone, 0), "retired motion lease refused");
    channel.Start();Check(!channel.Apply(motion,clone,160)&&!channel.Complete(),"expired motion cannot manufacture finished state");
    auto invalid = std::make_shared<Lease>(MotionFixture(true));
    GameModelMotion bad;
    Check(bool(bad.Decode(PackageStorage::Attach(invalid, invalid->b.size()), *asset))&&bad.Statistics().deferred_sections==1,
          "inactive unknown track retained,not executed at template admission");
    const auto prior=clone.Pose()[0].position;
    Check(!bad.Apply(clone,0)&&clone.Pose()[0].position==prior,"active unknown track refused before partial pose mutation");
}

void BackgroundModelMotionTests(){
    auto owner=std::make_shared<int>(1),foreign=std::make_shared<int>(2);
    auto records=std::make_shared<Lease>(std::vector<uint8_t>(60));
    for(unsigned i=0;i<3;++i){const auto name=std::string("MOTION_")+char('A'+i);std::copy(name.begin(),name.end(),records->b.begin()+i*20);}
    std::vector<std::pair<std::string,std::vector<uint8_t>>> entries{{"MOTION_A\\model01.DVS",Fixture()}};
    for(unsigned i=1;i<=4;++i)entries.push_back({"MOTION_A\\model0"+std::to_string(i)+".DAK",MotionFixture(i==4)});
    const auto file=BackgroundArchive({{"MOTION_A.BPM",BackgroundArchive(entries)}});
    for(int count:{0,2,4,-1,5}){
        GameBackgroundPreparation job;auto io=std::make_shared<BackgroundFileState>();
        auto counts=std::make_shared<Lease>(std::vector<uint8_t>{9,uint8_t(count),8,0,7,4});
        Check(bool(job.Begin(owner,1,{27,12,0,1},PackageStorage::Attach(records,60),std::make_unique<BackgroundFile>(file,io)))&&bool(job.Load(owner,1)),"model-motion real package stage");
        Check(!job.LoadModelMotions(foreign,1,PackageStorage::Attach(counts,6))&&!job.Snapshot().model_motions_staged,"foreign model-motion owner refuses");
        const auto status=job.LoadModelMotions(owner,1,PackageStorage::Attach(counts,6));const auto q=job.Snapshot();
        if(count>4)Check(!status&&q.failed&&!q.staged&&!q.model_motions_staged&&!q.source_committed,"count outside four source slots fails before template publication");
        else{
            Check(bool(status)&&q.model_motions_staged&&q.model_motions==uint32_t((std::max)(0,count))&&!q.source_committed,"source signed count, actual templates, no playback/scene ready");
            Check(q.model_motion_deferred==(count==4?1u:0u),"inactive deferred tracks preserved, never played by staging");
            Check(!job.LoadModelMotions(owner,1,PackageStorage::Attach(counts,6)),"template stage cannot replay");
            counts->live=false;std::vector<GameAreaMarkerState> markers;
            Check(!job.Snapshot().staged&&!job.AreaMarkers(owner,1,markers),"retired recipe no longer supplies current factory lineage");
        }
        Check(bool(job.Close())&&io->closes==1,"templates retire before backing file");
    }
    GameBackgroundPreparation job;auto io=std::make_shared<BackgroundFileState>();
    Check(bool(job.Begin(owner,1,{27,12,0,1},PackageStorage::Attach(records,60),std::make_unique<BackgroundFile>(file,io)))&&bool(job.Load(owner,1)),"missing recipe model staged");
    Check(!job.LoadModelMotions(owner,1,{})&&!job.Snapshot().model_motions_staged&&bool(job.Close()),"unknown recipe is not empty/default controls");
}
void BackgroundAuxiliaryTests(){
    // Entirely generated ASCII recipes/models/motions, not source table bytes.
    auto records=std::make_shared<Lease>(std::vector<uint8_t>(60));
    for(unsigned i=0;i<3;++i){const std::string n="AUX_"+std::to_string(i);std::copy(n.begin(),n.end(),records->b.begin()+i*20);}
    const auto view=[](const std::shared_ptr<Lease>& l){return PackageStorage::Attach(l,l->b.size());};
    const auto chars=[](std::string s){std::vector<uint8_t> b(s.begin(),s.end());b.push_back(0);return std::make_shared<Lease>(std::move(b));};
    auto format=chars("%s\\%s\\SYN_CHILD.DVS");
    std::array<std::shared_ptr<Lease>,3> texts{{chars("walk.dak"),chars("idle.dak"),chars("scene.dak")}};
    const GameAuxiliaryNameResolver resolve=[&](uint32_t token,PackageView& out)->GameFsStatus{
        if(token<101||token>103)return {GameFsCode::Unavailable,0,"synthetic unknown token"};
        out=view(texts[token-101]);return {};
    };
    auto recipe=std::make_shared<Lease>(std::vector<uint8_t>(36));
    Word(recipe->b,0,101);Word(recipe->b,4,102);Word(recipe->b,20,103); // row1 slot2 only
    const std::vector<std::array<int16_t,3>> quad{{{-20000,-20000,0}},{{20000,-20000,0}},{{20000,20000,0}},{{-20000,20000,0}}};
    const auto nested=BackgroundArchive({{"AUX_0\\model01.DVS",Fixture(false,&quad)},
        {"AUX_0\\SYN_CHILD.DVS",Fixture()},{"AUX_0\\walk.dak",MotionFixture()},
        {"AUX_0\\idle.dak",MotionFixture()},{"AUX_0\\scene.dak",MotionFixture()}});
    const auto file=BackgroundArchive({{"AUX_0.BPM",nested}});
    auto owner=std::make_shared<int>(31),foreign=std::make_shared<int>(32);
    GameBackgroundPreparation job;auto io=std::make_shared<BackgroundFileState>();uint64_t generation=0;
    const auto begin=[&](const std::vector<uint8_t>& bytes){++generation;io=std::make_shared<BackgroundFileState>();
        Check(bool(job.Begin(owner,generation,{47,12,0,generation},view(records),std::make_unique<BackgroundFile>(bytes,io)))&&bool(job.Load(owner,generation)),"auxiliary initialized real main owner");};
    begin(file);const auto reads=io->reads;
    std::vector<GameAuxiliaryBindingState> bindings;
    Check(!job.LoadAuxiliary(foreign,generation,view(recipe),view(format),resolve)
          &&!job.LoadAuxiliary(owner,generation+1,view(recipe),view(format),resolve)&&io->reads==reads,"foreign/stale auxiliary request no load");
    Check(bool(job.LoadAuxiliary(owner,generation,view(recipe),view(format),resolve)),"actual triplet/model/DAK typed owner construction");
    auto state=job.Snapshot();Check(state.auxiliary_staged&&state.auxiliary_entries==2&&state.auxiliary_children==1
        &&state.auxiliary_templates==3&&state.auxiliary_bindings==4&&!state.source_committed,"slot2 nonzero continues5070; distinct real bindings not ready");
    Check(bool(job.Auxiliary(owner,generation,bindings))&&bindings.size()==4,"current copied binding readback");
    for(const auto& b:bindings){Check(b.playback.state==(b.target==GameAuxiliaryTarget::MainTemplate?1u:3u)&&b.playback.time==0&&b.playback.samples==0
        &&b.playback.speed==4800&&b.playback.stop_limit==-1&&b.playback.duration==160,"88728/89954 initialization without sampling/selection");
        Check(b.model.vertices==(b.target==GameAuxiliaryTarget::Child?3u:4u),"DAK owns correct child/template/visible-clone target");}
    Check(bindings[2].target==GameAuxiliaryTarget::MainTemplate&&bindings[3].target==GameAuxiliaryTarget::MainClone,"independent template and visible clone controls");
    GameGraphics graphics;Check(graphics.Initialize({})&&graphics.Clear(0xFF000000)
        &&bool(job.DrawAuxiliary(owner,generation,0,graphics,ModelIdentity()))&&graphics.Snapshot().mesh_pixels>0,"actual auxiliary child offscreen draw");
    Check(!job.DrawAuxiliary(owner,generation,1,graphics,ModelIdentity())&&!job.LoadAuxiliary(owner,generation,view(recipe),view(format),resolve),"no fabricated child and no load replay");
    Check(bool(job.Auxiliary(owner,generation,bindings))&&bindings[0].playback.samples==0,"preview leaves controls/selected state unchanged");
    texts[0]->b[0]='W';bindings[0].entry=99;
    Check(!job.Auxiliary(owner,generation,bindings)&&bindings[0].entry==99&&!job.Snapshot().auxiliary_staged
        &&!job.Draw(owner,generation,graphics,ModelIdentity()),"name/recipe drift rejects current bindings without rebasing output");texts[0]->b[0]='w';
    io->fail_close=true;Check(!job.Close()&&io->live&&!job.Snapshot().auxiliary_staged,"failed backing retirement keeps owner for retry");
    Check(!job.Begin(owner,generation+1,{47,12,0,generation+1},view(records),std::make_unique<BackgroundFile>(file,io)),"failed close prevents fresh reuse");
    io->fail_close=false;Check(bool(job.Close())&&!io->live,"auxiliary controls/templates retire before file close retry");
    Check(!job.Auxiliary(owner,generation,bindings),"retired owner cannot read prior controls");
    // Each malformed/unknown input is a fresh real request, never an empty list.
    for(unsigned failure=0;failure<8;++failure){
        begin(file);auto bad=std::make_shared<Lease>(recipe->b);PackageView r=view(bad),f=view(format);
        GameAuxiliaryNameResolver resolver=resolve;
        if(failure==0)r={};
        if(failure==1){bad->b.resize(24);bad->known.resize(24);r=view(bad);} // missing terminator
        if(failure==2){bad->b.resize(48);bad->known.resize(48,1);r=view(bad);} // bytes after sentinel
        if(failure==3)resolver={};
        if(failure==4){bad->known[4]=0;}
        if(failure==5){Word(bad->b,0,0);} // slot1 has no child producer
        if(failure==6){texts[0]->b[0]='/';}
        if(failure==7)f={};
        Check(!job.LoadAuxiliary(owner,generation,r,f,resolver)&&job.Snapshot().failed
              &&!job.Snapshot().auxiliary_staged&&!job.Snapshot().source_committed,"malformed/unknown auxiliary recipe never publishes");
        texts[0]->b[0]='w';Check(bool(job.Close()),"bad recipe teardown");
    }
    const auto missing=BackgroundArchive({{"AUX_0.BPM",BackgroundArchive({{"AUX_0\\model01.DVS",Fixture()}})}});
    begin(missing);Check(!job.LoadAuxiliary(owner,generation,view(recipe),view(format),resolve)
        &&job.Snapshot().failed&&!job.Snapshot().staged,"missing mandatory child model not optional success");Check(bool(job.Close()),"failed-load cleanup");
    begin(file);Check(bool(job.LoadAuxiliary(owner,generation,view(recipe),view(format),resolve)),"fresh epoch after failed load retries real construction");
    recipe->live=false;Check(!job.Snapshot().staged&&!job.Auxiliary(owner,generation,bindings),"retired recipe prevents live readback");
    recipe->live=true;owner.reset();Check(!job.Auxiliary(foreign,generation,bindings)&&!job.Snapshot().live,"expired owning control block refuses");
    Check(bool(job.Close()),"final auxiliary teardown");
    owner=std::make_shared<int>(33);begin(file);auto empty=std::make_shared<Lease>(std::vector<uint8_t>(12));
    const auto before=io->reads;Check(bool(job.LoadAuxiliary(owner,generation,view(empty),{},{}))
        &&job.Snapshot().auxiliary_staged&&!job.Snapshot().auxiliary_entries&&io->reads==before,"actual all-zero terminator is the only empty source recipe branch");
    Check(bool(job.Close()),"empty recipe owner retires");graphics.Close();
}
void BackgroundEffectsTests(){
    const auto view=[](const std::shared_ptr<Lease>& p){return PackageStorage::Attach(p,p->b.size());};
    const auto text=[](std::string s){std::vector<uint8_t> b(s.begin(),s.end());b.push_back(0);return std::make_shared<Lease>(std::move(b));};
    auto records=std::make_shared<Lease>(std::vector<uint8_t>(60));
    for(unsigned i=0;i<3;++i){const std::string n="FX_"+std::to_string(i);std::copy(n.begin(),n.end(),records->b.begin()+i*20);}
    auto counts=std::make_shared<Lease>(std::vector<uint8_t>{1,0,0,0,255,0});
    auto empty_aux=std::make_shared<Lease>(std::vector<uint8_t>(12));
    auto format=text("%s\\%s\\SYN_FX%02d.DAK");
    const auto archive=[&](bool missing){
        std::vector<std::pair<std::string,std::vector<uint8_t>>> packages;
        for(unsigned v=0;v<3;++v){const auto n="FX_"+std::to_string(v);
            std::vector<std::pair<std::string,std::vector<uint8_t>>> entries{{n+"\\model01.DVS",Fixture()}};
            for(unsigned i=1;i<=7;++i)if(!missing||i!=4)entries.push_back({n+"\\SYN_FX0"+std::to_string(i)+".DAK",MotionFixture(i==7)});
            packages.push_back({n+".BPM",BackgroundArchive(entries)});
        }return BackgroundArchive(packages);
    };
    const auto file=archive(false);auto owner=std::make_shared<int>(1),foreign=std::make_shared<int>(2);
    GameBackgroundPreparation job;std::shared_ptr<BackgroundFileState> io;uint64_t epoch=0;
    const auto begin=[&](const std::vector<uint8_t>& bytes,uint32_t period=0){
        ++epoch;io=std::make_shared<BackgroundFileState>();
        Check(bool(job.Begin(owner,epoch,{17,12,period,epoch},view(records),std::make_unique<BackgroundFile>(bytes,io)))
            &&bool(job.Load(owner,epoch))&&bool(job.LoadModelMotions(owner,epoch,view(counts)))
            &&bool(job.LoadAuxiliary(owner,epoch,view(empty_aux),{},{})),"effects current main/count/auxiliary owners");
    };
    const auto reduced=GameBackgroundEffectsPolicy::WindowsReducedBackgroundEffects;
    begin(file);const auto reads=io->reads;
    Check(!job.PrepareEffects(owner,epoch,GameBackgroundEffectsPolicy::Source5448,view(format))
        &&!job.Snapshot().effects_prepared&&!job.Snapshot().failed&&io->reads==reads,"strict unknown5448 never becomes empty or ready");
    std::shared_ptr<const void> alias(owner.get(),[](const void*){});
    Check(!job.PrepareEffects(alias,epoch,reduced,view(format))&&!job.PrepareEffects(foreign,epoch,reduced,view(format))
        &&!job.PrepareEffects(owner,epoch+1,reduced,view(format))&&io->reads==reads,"alias-control-block/foreign/stale effects rejected before I/O");
    Check(bool(job.PrepareEffects(owner,epoch,reduced,view(format))),"real seven DAK/control construction under explicit reduced policy");
    const auto state=job.Snapshot();Check(state.effects_prepared&&state.secondary_motions_omitted&&state.special_motion_not_applicable
        &&state.light_templates==7&&state.light_bindings==7&&state.light_deferred==1&&!state.source_committed,"selected573C absent; inactive deferred templates not activated");
    std::vector<GameBackgroundLightBinding> out;
    Check(bool(job.LightBindings(owner,epoch,out))&&out.size()==7,"actual light binding copied readback");
    for(unsigned i=0;i<7;++i)Check(out[i].slot==i&&out[i].playback.state==1&&out[i].playback.time==0
        &&out[i].playback.samples==0&&out[i].playback.speed==4800&&out[i].motion.duration==160,"constructor controls no invented72B8 calendar sample");
    Check(!job.PrepareEffects(owner,epoch,reduced,view(format)),"no effects stage replay");
    out[0].slot=99;const auto original=format->b[8];format->b[8]='X';GameGraphics g;Check(g.Initialize({}),"effects offscreen guards");
    Check(!job.LightBindings(owner,epoch,out)&&out[0].slot==99&&!job.Snapshot().effects_prepared
        &&!job.Draw(owner,epoch,g,ModelIdentity()),"format drift refuses whole current staged owner without rebasing output");format->b[8]=original;
    io->fail_close=true;Check(!job.Close()&&!job.Snapshot().effects_prepared&&io->live,"control/template teardown retained on file close failure");
    Check(!job.Begin(owner,epoch+1,{17,12,0,epoch+1},view(records),std::make_unique<BackgroundFile>(file,io)),"failed close cannot restart");
    io->fail_close=false;Check(bool(job.Close())&&!io->live&&!job.LightBindings(owner,epoch,out),"retry retires backing and copied owner access");
    // Variant1 really has zero presence, so it consumes no path format or DAK.
    begin(file,1);const auto no_reads=io->reads;
    Check(bool(job.PrepareEffects(owner,epoch,reduced,{}))&&job.Snapshot().light_templates==0&&io->reads==no_reads
        &&bool(job.LightBindings(owner,epoch,out))&&out.empty(),"actual5610 zero branch distinct from unknown5448");Check(bool(job.Close()),"zero-presence retirement");
    // Signed negative presence is nonzero in source; it also loads all seven.
    begin(file,3);Check(bool(job.PrepareEffects(owner,epoch,reduced,view(format)))&&job.Snapshot().light_bindings==7,"signed negative presence is not false");
    counts->b[4]=0;Check(!job.LightBindings(owner,epoch,out)&&!job.Snapshot().staged,"same recipe-byte drift invalidates current bindings");counts->b[4]=255;Check(bool(job.Close()),"presence drift teardown");
    for(const auto& bad:{std::string("%s\\%s\\../%02d"),std::string("%s\\%s\\X%d"),std::string("%s\\%s\\X%02d%02d")}){
        begin(file);auto malformed=text(bad);Check(!job.PrepareEffects(owner,epoch,reduced,view(malformed))
            &&job.Snapshot().failed&&!job.Snapshot().effects_prepared&&!job.Snapshot().staged,"malformed format cannot publish bindings");Check(bool(job.Close()),"bad format cleanup");
    }
    begin(archive(true));Check(!job.PrepareEffects(owner,epoch,reduced,view(format))&&job.Snapshot().failed
        &&!job.Snapshot().effects_prepared&&!job.Snapshot().staged,"failed fourth DAK load never publishes partial controls");Check(bool(job.Close()),"partial-load cleanup");
    begin(file);Check(bool(job.PrepareEffects(owner,epoch,reduced,view(format))),"new epoch retries real load");
    format->live=false;Check(!job.LightBindings(owner,epoch,out)&&!job.Snapshot().live,"retired format refuses current binding");format->live=true;
    owner.reset();Check(!job.LightBindings(foreign,epoch,out)&&!job.Snapshot().live,"expired owner no effects authority");Check(bool(job.Close()),"final effects cleanup");g.Close();
}
void BackgroundSamplingTests(){
    {auto seed=std::make_unique<RngNativeOwner>();seed->ImportCurrentState64(1,7,"synthetic retired owner");
     auto copy=*seed;auto input=std::make_shared<GameBackgroundRuntime>();
     Check(bool(input->Initialize([]{return std::array<uint32_t,7>{{0,0,0,0,1,1,2026}};},*seed)),"borrow real RNG object lifetime");
     seed.reset();Check(copy.Available()&&!input->Live(),"COW copy cannot keep destroyed RNG object binding live");}

    const auto view=[](const std::shared_ptr<Lease>& p){return PackageStorage::Attach(p,p->b.size());};
    auto records=std::make_shared<Lease>(std::vector<uint8_t>(60));
    for(unsigned i=0;i<3;++i){const std::string n="SAMPLE_"+std::to_string(i);std::copy(n.begin(),n.end(),records->b.begin()+i*20);}
    auto counts=std::make_shared<Lease>(std::vector<uint8_t>{1,0,1,0,1,0});
    auto recipe=std::make_shared<Lease>(std::vector<uint8_t>(7*12));
    for(unsigned i=0;i<6;++i){Word(recipe->b,i*12,101);if(i%2)Word(recipe->b,i*12+8,102);}
    const auto chars=[](std::string s){std::vector<uint8_t> b(s.begin(),s.end());b.push_back(0);return std::make_shared<Lease>(std::move(b));};
    auto aux_format=chars("%s\\%s\\SYN_MODEL.DVS"),light_format=chars("%s\\%s\\FX%02d.DAK"),name=chars("A.dak");
    const auto resolver=[&](uint32_t token,PackageView& out)->GameFsStatus{if(token!=101&&token!=102)return {GameFsCode::Unavailable,0,"synthetic token"};out=view(name);return {};};
    std::vector<std::pair<std::string,std::vector<uint8_t>>> entries{{"SAMPLE_0\\model01.DVS",Fixture()},{"SAMPLE_0\\SYN_MODEL.DVS",Fixture()},{"SAMPLE_0\\A.dak",MotionFixture()}};
    for(unsigned i=1;i<=7;++i)entries.push_back({"SAMPLE_0\\FX0"+std::to_string(i)+".DAK",MotionFixture()});
    const auto file=BackgroundArchive({{"SAMPLE_0.BPM",BackgroundArchive(entries)}});
    for(uint64_t seed:{0,1,2,3}){ // fixed synthetic cases, no live seed search
        RngNativeOwner rng;rng.ImportCurrentState64(seed,91,"synthetic sampling fixture");
        std::array<uint32_t,7> calendar{{0,0,0,0,1,1,2026}};unsigned reads=0;
        auto runtime=std::make_shared<GameBackgroundRuntime>();
        Check(bool(runtime->Initialize([&]{++reads;return calendar;},rng)),"initialize current clock and actual RNG lineage");
        const std::shared_ptr<const void> owner=runtime;
        GameBackgroundPreparation job;auto io=std::make_shared<BackgroundFileState>();
        uint64_t after_bgm=seed;
        const auto prepare=[&](uint64_t epoch){Check(bool(job.Begin(owner,epoch,{1,12,0,epoch},view(records),std::make_unique<BackgroundFile>(file,io))),"pending source resource job before BGM");
            if(seed==3&&epoch==1){
                auto old=std::make_shared<Lease>(std::vector<uint8_t>(4));Word(old->b,0,24);
                std::vector<uint8_t> data;for(unsigned i=0;i<19;++i)W(data,(i*7+3)%25);auto list=std::make_shared<Lease>(data);
                auto path=chars("/SYNTHETIC/sound"),bgm_name=chars("SAMPLE");auto bgm_io=std::make_shared<BackgroundFileState>();
                unsigned factory_calls=0,name_calls=0;
                const auto make_file=[&](const std::string&,std::unique_ptr<GameFileSource>& out)->GameFsStatus{++factory_calls;out=std::make_unique<BackgroundFile>(std::vector<uint8_t>(16,3),bgm_io);return {};};
                const auto names=[&](uint32_t){++name_calls;return view(bgm_name);};
                RngNativeOwner independent;independent.ImportCurrentState64(99,92,"synthetic foreign BGM lineage");
                auto other_runtime=std::make_shared<GameBackgroundRuntime>();
                Check(bool(other_runtime->Initialize([&]{return calendar;},independent)),"distinct live runtime with independent RNG");
                auto same_address=std::shared_ptr<GameBackgroundRuntime>(runtime.get(),[](GameBackgroundRuntime*){});
                Check(!job.RequestBgm(owner,epoch,other_runtime,view(old),view(list),view(path),names,make_file)
                    &&!job.RequestBgm(owner,epoch,same_address,view(old),view(list),view(path),names,make_file)
                    &&rng.CurrentSeed64()==seed&&independent.CurrentSeed64()==99&&!job.Snapshot().bgm_attempted
                    &&job.Snapshot().bgm_choice==-1&&!job.Snapshot().bgm_selection.rng_calls&&!factory_calls&&!name_calls&&!reads,
                    "current ownerA cannot select from runtimeB or same-address foreign control block; no job/RNG/provider effects");
                auto other_owner=std::make_shared<int>(1);
                Check(!job.RequestBgm(other_owner,epoch,runtime,view(old),view(list),view(path),names,make_file)&&rng.CurrentSeed64()==seed,"foreign pending job cannot consume shared RNG");
                Check(bool(job.RequestBgm(owner,epoch,runtime,view(old),view(list),view(path),names,make_file)),"source BGM selection/file request on same pending job before background init");
                after_bgm=rng.CurrentSeed64();Check(job.Snapshot().bgm_selection.before==seed&&job.Snapshot().bgm_selection.after==after_bgm
                    &&job.Snapshot().bgm_requested&&job.BgmFile().state==1&&!reads,"BGM actual RNG effect precedes clock/auxiliary calls, accepted is not loaded");
                bgm_io->missing=true;Check(!job.PumpBgm(owner,epoch)&&job.BgmFile().state==1,"supported open error retained, not a scene all-audio-success gate");
                Check(!job.RequestBgm(owner,epoch,runtime,view(old),view(list),view(path),names,make_file)&&rng.CurrentSeed64()==after_bgm,"replay cannot repeat scene BGM selection");
            }
            Check(bool(job.Load(owner,epoch))
            &&bool(job.LoadModelMotions(owner,epoch,view(counts)))&&bool(job.LoadAuxiliary(owner,epoch,view(recipe),view(aux_format),resolver))
            &&bool(job.PrepareEffects(owner,epoch,GameBackgroundEffectsPolicy::WindowsReducedBackgroundEffects,view(light_format))),"real current staged sampling owner");};
        prepare(1);auto foreign=std::make_shared<GameBackgroundRuntime>();Check(bool(foreign->Initialize([&]{return calendar;},rng)),"other valid producer");
        Check(!job.ActivateEffects(owner,1,foreign)&&!job.ActivateEffects(owner,2,runtime)&&rng.CurrentSeed64()==after_bgm&&!reads,"foreign/stale activation never reads or advances");
        calendar[1]=1; // changed since initialization: Read must use CURRENT producer, not a saved date
        Check(bool(job.ActivateEffects(owner,1,runtime))&&reads==1,"current clock read and source selection");
        uint64_t expected=after_bgm;unsigned calls=0;const auto next=[&](){++calls;expected=expected*UINT64_C(0x5851f42d4c957f2d)+1;return uint32_t(expected>>32)&0x7fffffff;};
        const auto n=next()%4;std::vector<uint32_t> pool{0,1,2,3,4,5},selected;
        for(unsigned k=0;k<n;++k){const auto at=next()%pool.size();selected.push_back(pool[at]);pool.erase(pool.begin()+at);}
        std::vector<int32_t> phases;for(unsigned k=0;k<n;++k)phases.push_back(int32_t(next()%6));
        GameBackgroundAppliedState applied;Check(bool(job.AppliedEffects(owner,1,applied))&&applied.selected_entries==selected&&applied.child_phases==phases
            &&applied.rng_calls==calls&&applied.rng_before==after_bgm&&applied.rng_after==expected&&rng.CurrentSeed64()==expected,"exact61E4 compaction and child-only extra RNG call order");
        Check(applied.calendar==calendar&&applied.light_slot==0&&!applied.samples,"month1 before day7 selects previous month row0, unsampled activation");
        Check(!job.ActivateEffects(owner,1,runtime)&&rng.CurrentSeed64()==expected&&reads==1,"activation cannot replay RNG or clock");
        Check(bool(job.SampleEffects(owner,1,4800))&&bool(job.AppliedEffects(owner,1,applied))&&applied.samples==1
            &&applied.main_pose[0].position[0]!=0&&applied.child_poses.size()==n,"actual main/child pose consumers sampled");
        for(const auto& poses:applied.child_poses)Check(!poses.empty()&&poses[0].position[0]==10,"selected child actual time0 DAK translation");
        const auto pose=applied.main_pose[0].position;
        Check(bool(job.SampleEffects(owner,1,4800))&&bool(job.AppliedEffects(owner,1,applied))&&applied.main_pose[0].position!=pose
            &&rng.CurrentSeed64()==expected&&reads==1,"speed30 advances real seasonal pose, no extra random or RTC reads");
        std::vector<GameBackgroundLightBinding> lights;Check(bool(job.LightBindings(owner,1,lights))&&lights[0].playback.speed==30
            &&lights[0].playback.sampled_time==90&&lights[0].playback.time==120,"source clock60 plus native30-per-frame sampling");
        for(unsigned i=1;i<7;++i)Check(lights[i].playback.state==1&&!lights[i].playback.samples,"unselected seasonal controls stay inactive");
        std::vector<GameAuxiliaryBindingState> bindings;Check(bool(job.Auxiliary(owner,1,bindings)),"selected auxiliary readback");
        for(const auto& b:bindings){const bool active=b.target!=GameAuxiliaryTarget::MainTemplate&&std::find(selected.begin(),selected.end(),b.entry)!=selected.end();
            Check(b.playback.samples==(active?2u:0u),"only source-selected non-template controls execute");}
        const auto oldlineage=rng.Lineage();rng.ImportCurrentState64(expected,91,"synthetic sampling fixture");
        Check(rng.Lineage()!=oldlineage&&!runtime->Live()&&!job.SampleEffects(owner,1,4800)&&!job.Snapshot().live,"same bytes reseed still revokes lineage and job");
        Check(bool(job.Close()),"retired producer controls/template cleanup");
    }
    // Bad current clock and producer retirement before/during sampling are refusals.
    for(unsigned failure=0;failure<4;++failure){
        RngNativeOwner rng;rng.ImportCurrentState64(2,92,"synthetic failure fixture");auto runtime=std::make_shared<GameBackgroundRuntime>();
        std::array<uint32_t,7> date{{0,1,0,0,7,1,2026}};
        Check(bool(runtime->Initialize([&]{if(failure==1)runtime->Close();return date;},rng)),"failure current runtime");
        const std::shared_ptr<const void> owner=runtime;GameBackgroundPreparation job;auto io=std::make_shared<BackgroundFileState>();
        auto bad_entries=entries;if(failure==3)bad_entries.back().second=MotionFixture(true);
        const auto input=BackgroundArchive({{"SAMPLE_0.BPM",BackgroundArchive(bad_entries)}});
        Check(bool(job.Begin(owner,1,{1,12,0,1},view(records),std::make_unique<BackgroundFile>(input,io)))&&bool(job.Load(owner,1))
            &&bool(job.LoadModelMotions(owner,1,view(counts)))&&bool(job.LoadAuxiliary(owner,1,view(recipe),view(aux_format),resolver))
            &&bool(job.PrepareEffects(owner,1,GameBackgroundEffectsPolicy::WindowsReducedBackgroundEffects,view(light_format))),"failure prepared real job");
        if(failure==0)date[5]=0;
        if(failure<2)Check(!job.ActivateEffects(owner,1,runtime)&&rng.CurrentSeed64()==2,"invalid/revoked clock never consumes seed");
        else if(failure==3){date[5]=6;Check(!job.ActivateEffects(owner,1,runtime)&&job.Snapshot().failed
            &&rng.CurrentSeed64()!=2&&!job.Snapshot().staged,"selected deferred consumer fails after actual source RNG effects, no success rollback");}
        else{Check(bool(job.ActivateEffects(owner,1,runtime)),"valid current activation before input drift");light_format->b[6]='Q';
            Check(!job.SampleEffects(owner,1,4800),"post-activation format drift refuses sampling");light_format->b[6]='F';}
        Check(bool(job.Close()),"failed current sampling owner closes");
    }
}

void SelectedOptionalAuxiliaryTests(){
    using namespace dokodemo::script;
    const auto view=[](const std::shared_ptr<Lease>& p){return PackageStorage::Attach(p,p->b.size());};
    const auto text=[](std::string s){std::vector<uint8_t> b(s.begin(),s.end());b.push_back(0);return std::make_shared<Lease>(std::move(b));};
    auto records=std::make_shared<Lease>(std::vector<uint8_t>(60));
    for(unsigned i=0;i<3;++i){const std::string name="MIX_"+std::to_string(i);std::copy(name.begin(),name.end(),records->b.begin()+i*20);}
    auto counts=std::make_shared<Lease>(std::vector<uint8_t>(6));auto recipe=std::make_shared<Lease>(std::vector<uint8_t>(84));
    for(unsigned i=0;i<6;++i){if(i%2)Word(recipe->b,i*12+8,11);else{Word(recipe->b,i*12,11);Word(recipe->b,i*12+4,11);}}
    auto model_format=text("%s\\%s\\CHILD.DVS"),motion_name=text("A.DAK"),glyph_format=text("F%03d\\F%03d\\F%05d.GIM");
    const auto archive=BackgroundArchive({{"MIX_0.BPM",BackgroundArchive({{"MIX_0\\model01.DVS",Fixture()},
        {"MIX_0\\CHILD.DVS",Fixture()},{"MIX_0\\A.DAK",MotionFixture()}})}});
    struct Templates:GameResourceSource {GameFsStatus OpenResource(const std::string&,PackageView& out)override{
        auto held=std::make_shared<Lease>(Fixture());out=PackageStorage::Attach(held,held->b.size());return {};}} templates;
    GameSupportTextMap map;auto keys=std::make_shared<Lease>(std::vector<uint8_t>(96,255));auto empty=text(""),integer=text("%d");
    map.keys=view(keys);for(auto& entry:map.expansions)entry=view(empty);map.fallback=view(empty);map.integer_format=view(integer);
    GameSupportTemplatePaths paths;auto model=text("SYN\\model.DVS"),prefix=text("SYN\\gim\\");for(auto& path:paths.models)path=view(model);paths.textures=view(prefix);
    std::vector<uint8_t> bytes(28+3+9*4);Word(bytes,0,0x0043534B);Word(bytes,4,0x20030204);Word(bytes,12,3);bytes[24]=9;
    bytes[28]=0x30;bytes[30]=0x38;Word(bytes,31,0x90000008);auto module=std::make_shared<Lease>(bytes);
    Program program;Diagnostic error;Check(program.load_test_fixture(bytes,error),"synthetic mixed auxiliary module");
    for(uint64_t seed:{1,2}){ // fixed component cases: main-only; main-only plus child
        RngNativeOwner rng;rng.ImportCurrentState64(seed,7,"synthetic mixed optional child");unsigned clocks=0;
        auto runtime=std::make_shared<GameBackgroundRuntime>();Check(bool(runtime->Initialize([&]{++clocks;return std::array<uint32_t,7>{{0,0,0,0,1,1,2026}};},rng)),"mixed actual clock/RNG owner");
        const std::shared_ptr<const void> owner=runtime;auto io=std::make_shared<BackgroundFileState>();GameBackgroundPreparation job;
        Check(bool(job.Begin(owner,1,{8,12,0,1},view(records),std::make_unique<BackgroundFile>(archive,io)))&&bool(job.Load(owner,1))
            &&bool(job.LoadModelMotions(owner,1,view(counts)))&&bool(job.LoadAuxiliary(owner,1,view(recipe),view(model_format),[&](uint32_t token,PackageView& out)->GameFsStatus{
                if(token!=11)return {GameFsCode::Unavailable,0,"synthetic token"};out=view(motion_name);return {};}))
            &&bool(job.PrepareEffects(owner,1,GameBackgroundEffectsPolicy::WindowsReducedBackgroundEffects,{}))&&bool(job.ActivateEffects(owner,1,runtime)),"real mixed producer/control construction");
        uint64_t expected=seed;unsigned calls=0;const auto next=[&](){++calls;expected=expected*UINT64_C(0x5851f42d4c957f2d)+1;return uint32_t(expected>>32)&0x7fffffff;};
        const auto n=next()%4;std::vector<uint32_t> pool{0,1,2,3,4,5},selected;std::vector<int32_t> phases;
        for(unsigned k=0;k<n;++k){const auto at=next()%pool.size();selected.push_back(pool[at]);pool.erase(pool.begin()+at);}
        for(const auto i:selected)phases.push_back(i%2?-1:int32_t(next()%6));
        Check(selected==(seed==1?std::vector<uint32_t>{3}:std::vector<uint32_t>{1,2}),"fixed synthetic cases cover selected main-only and selected child");
        Check(bool(job.SampleEffects(owner,1,0)),"sample genuine slot2 main and optional child");GameBackgroundAppliedState applied;
        Check(bool(job.AppliedEffects(owner,1,applied))&&applied.selected_entries==selected&&applied.child_phases==phases
            &&applied.rng_calls==calls&&rng.CurrentSeed64()==expected&&clocks==1&&applied.main_pose[0].position==GameVector4{{10,20,30,1}},
            "main-only slot2 changes actual main pose; child-only phase draws preserve source order/state");
        for(size_t i=0;i<selected.size();++i)Check(applied.child_poses[i].empty()==bool(selected[i]%2),"optional child absent only for real main-only producer");
        std::vector<GameAuxiliaryBindingState> bindings;Check(bool(job.Auxiliary(owner,1,bindings)),"mixed control readback");
        for(const auto& b:bindings){const bool active=b.target!=GameAuxiliaryTarget::MainTemplate&&std::find(selected.begin(),selected.end(),b.entry)!=selected.end();
            Check(b.playback.samples==(active?1u:0u),"slot2 visible-main sampled, template/unselected controls not sampled");}
        Vm vm(program);vm.start();const auto context=vm.context_value();Value ref;ref.kind=Value::Kind::Reference;ref.table=Table::Global;
        for(unsigned i=1;i<=8;++i){auto cell=ref;cell.index=uint16_t(i);vm.store_reference(cell,Value::string(""));}
        GameSupportRegistration registered;Check(bool(registered.Initialize(owner,1))&&bool(registered.Register16(vm,ref))
            &&bool(job.PrepareSupport(owner,1,vm,registered,view(module),map,templates,paths,view(glyph_format))),"real registered support stage before selected draw");
        // Retire an UNSELECTED child: draw must not visit it or add its triangles.
        dokodemo::StartupRuntimeTestAccess::RetireAuxChild(job,0,false);
        GameGraphics graphics;Check(graphics.Initialize({})&&graphics.Clear(0xFF555555),"mixed component surface");auto camera=ModelIdentity();camera[0]=camera[5]=camera[10]=.02f;
        Check(bool(job.DrawSelected(owner,1,vm,graphics,camera))&&graphics.Snapshot().mesh_pixels>0
            &&graphics.Snapshot().mesh_triangles==(seed==1?1u:2u),"main-only succeeds; selected child only contributes its actual triangle/pixels");
        const auto drawn=graphics.Snapshot().mesh_triangles;
        dokodemo::StartupRuntimeTestAccess::AuxBadSelected(job,true);Check(!job.DrawSelected(owner,1,vm,graphics,camera)&&graphics.Snapshot().mesh_triangles==drawn,"out-of-range selection refuses before raster effects");dokodemo::StartupRuntimeTestAccess::AuxBadSelected(job,false);
        if(seed==2){dokodemo::StartupRuntimeTestAccess::RetireAuxChild(job,2,false);
            Check(!job.DrawSelected(owner,1,vm,graphics,camera)&&graphics.Snapshot().mesh_triangles==drawn,"selected required RETIRED child refuses, not optional skip");
            dokodemo::StartupRuntimeTestAccess::RetireAuxChild(job,2,true);
            Check(!job.DrawSelected(owner,1,vm,graphics,camera)&&graphics.Snapshot().mesh_triangles==drawn,"selected required MISSING child refuses");}
        Check(rng.CurrentSeed64()==expected&&clocks==1,"all rendering/refusals preserve intrinsic RNG and clock");
        Check(bool(job.Close())&&!io->live,"mixed owner retirement");registered.Close();graphics.Close();runtime->Close();
    }
}

void SupportConsumerTests(){
    using namespace dokodemo::script;
    const auto view=[](const std::shared_ptr<Lease>& p){return PackageStorage::Attach(p,p->b.size());};
    const auto text=[](std::string s){std::vector<uint8_t> b(s.begin(),s.end());b.push_back(0);return std::make_shared<Lease>(std::move(b));};
    // Same public KSC format, entirely generated module/current values.
    std::vector<uint8_t> bytes(28+3+9*4);Word(bytes,0,0x0043534B);Word(bytes,4,0x20030204);Word(bytes,12,3);bytes[24]=9;
    bytes[28]=0x30;bytes[29]=0;bytes[30]=0x38;Word(bytes,31,0x90000008);
    auto module=std::make_shared<Lease>(bytes);Program program;Diagnostic error;Check(program.load_test_fixture(bytes,error),"synthetic support module");
    Vm vm(program),foreign(program);Check(vm.start().status==Status::Running&&foreign.start().status==Status::Running,"actual two VM contexts");
    const auto context=vm.context_value(),other=foreign.context_value();
    const auto put=[&](unsigned i,Value value){Value r;r.kind=Value::Kind::Reference;r.table=Table::Global;r.index=uint16_t(i+1);vm.store_reference(r,std::move(value));};
    Value nil;nil.kind=Value::Kind::Scalar;nil.word=0;
    const std::array<Value,8> values{{Value::string("AB"),Value::string("C"),Value::string("?"),nil,Value::integer(-3),Value::string(""),Value::string(std::string("\xA1Z",2)),Value::string(" x")}};
    for(unsigned i=0;i<8;++i)put(i,values[i]);
    auto keys=std::make_shared<Lease>(std::vector<uint8_t>(96,255));
    std::array<std::shared_ptr<Lease>,96> expansions;GameSupportTextMap map;map.keys=view(keys);
    for(unsigned i=0;i<96;++i){expansions[i]=text("");map.expansions[i]=view(expansions[i]);}
    const std::array<std::pair<char,std::string>,7> entries{{{'A',"x"},{'B',"y"},{'Z',"N"},{'-',"-"},{'3',"3"},{'x',"x"},{'C',""}}};
    for(unsigned i=0;i<entries.size();++i){keys->b[i]=uint8_t(entries[i].first);expansions[i]=text(entries[i].second);map.expansions[i]=view(expansions[i]);}
    auto fallback=text("Z"),format=text("%d");map.fallback=view(fallback);map.integer_format=view(format);
    auto owner=std::make_shared<int>(7),wrong=std::make_shared<int>(8);GameSupportTextOwner consumer;uint64_t epoch=1;
    const auto init=[&](){return consumer.Initialize(owner,epoch,vm,context,Table::Global,0,view(module),map);};
    Check(!consumer.Initialize(owner,epoch,vm,other,Table::Global,0,view(module),map),"foreign context not a current reference issuer");
    Check(bool(init()),"production current-reference/owned-map normalization");GameSupportSnapshot snap;
    Check(bool(consumer.Read(owner,epoch,vm,snap))&&snap.converted==8&&!snap.source_current_bound,"consumer success is not source field20 readiness");
    const auto cell=[&](unsigned n){return std::string(reinterpret_cast<const char*>(snap.cells[n].data()));};
    Check(cell(0)=="xy"&&cell(1).empty()&&cell(2).empty()&&cell(3)=="N"&&cell(4)=="-3"&&cell(5).empty()
        &&cell(6)==std::string("\xA1Z",2)&&cell(7)=="x","mapping first-match/drop, actual fallback/numeric and raw signed-byte-pair semantics");
    snap.converted=99;Check(!consumer.Read(wrong,epoch,vm,snap)&&!consumer.Read(owner,epoch+1,vm,snap)
        &&!consumer.Read(owner,epoch,foreign,snap)&&snap.converted==99,"foreign/stale/current-module readback remains unchanged");
    keys->b[0]='a';Check(!consumer.Read(owner,epoch,vm,snap),"current map drift refuses");keys->b[0]='A';
    put(0,Value::string("B"));Check(!consumer.Read(owner,epoch,vm,snap),"current VM value drift refuses without expected rebase");put(0,values[0]);
    struct Source final:GameResourceSource {
        unsigned calls=0,fail=0;std::vector<std::shared_ptr<Lease>> leases;
        GameFsStatus OpenResource(const std::string&,PackageView& out)override{
            ++calls;if(fail&&calls==fail)return {GameFsCode::NotFound,0,"synthetic real template load failure"};
            auto l=std::make_shared<Lease>(Fixture());leases.push_back(l);out=PackageStorage::Attach(l,l->b.size());return {};
        }
    } source;
    GameSupportTemplatePaths paths;std::array<std::shared_ptr<Lease>,18> names;
    for(unsigned i=0;i<18;++i){names[i]=text("SUPPORT_SYN_"+std::to_string(i)+".DVS");paths.models[i]=view(names[i]);}
    auto prefix=text("SUPPORT_SYN\\textures\\");paths.textures=view(prefix);
    Check(bool(consumer.LoadTemplates(owner,epoch,vm,source,paths))&&source.calls==18,"actual eighteen model parse/ownership consumers");
    auto parent_bytes=AreaMarkerFixture(0,false);auto parsed=dokodemo::model::parseDvs(parent_bytes);const auto end=parsed.nodes[0].source.offset+parsed.nodes[0].source.bytes;
    // Synthetic marker: text0, whole-label key1, style0. No original marker bytes.
    parent_bytes[end-12]=1;parent_bytes[end-11]=0;parent_bytes[end-10]=1;parent_bytes[end-9]=0;parent_bytes[end-8]=1;parent_bytes[end-7]=0;
    auto parent_source=std::make_shared<Lease>(parent_bytes);auto parent_asset=std::make_shared<GameModelAsset>();
    Check(bool(parent_asset->Decode(view(parent_source))),"actual synthetic marker template");auto parent=std::make_shared<GameModelClone>();Check(bool(parent->Initialize(parent_asset)),"actual parent clone");
    Check(bool(consumer.Attach(owner,epoch,vm,parent))&&bool(consumer.Read(owner,epoch,vm,snap))&&snap.placements.size()==1
        &&snap.placements[0].model==3&&snap.placements[0].characters==2&&snap.glyph_requests.size()==2,"length2/key1 selects template3 and queues actual two glyphs");
    Check(snap.glyph_requests[0].code=='x'&&snap.glyph_requests[0].style==0&&snap.glyph_requests[1].code=='y',"actual dedup request bytes/style");
    GameGraphics graphics;auto camera=ModelIdentity();camera[0]=camera[5]=camera[10]=.1f;camera[12]=-.6f;camera[13]=-.8f;camera[14]=-1.1f;
    Check(graphics.Initialize({})&&graphics.Clear(0xFF000000)&&bool(consumer.DrawPreview(owner,epoch,vm,graphics,camera))
        &&graphics.Snapshot().mesh_pixels>0,"actual attached child clone raster, no glyph-worker success stub");
    const auto original=snap.placements[0].parent_world;auto root=ModelIdentity();root[12]=2;Check(bool(parent->Update(root)),"actual parent update");
    Check(!consumer.Read(owner,epoch,vm,snap)&&bool(consumer.Update(owner,epoch,vm))&&bool(consumer.Read(owner,epoch,vm,snap))
        &&Near(snap.placements[0].parent_world[12],original[12]+2)&&snap.glyph_requests.size()==2,"same-marker world update and request dedup");
    auto& alias=parent->Pose();Check(bool(parent->Update(root)),"retained pose alias baseline");alias[0].world[12]+=1;
    Check(!consumer.Update(owner,epoch,vm)&&!consumer.Read(owner,epoch,vm,snap),"stale parent alias cannot attach fabricated world");Check(bool(parent->Update(root)),"explicit update restores valid pose");
    parent->Close();Check(bool(parent->Initialize(parent_asset))&&!consumer.Read(owner,epoch,vm,snap),"same allocation parent reinitialize has different clone generation");
    consumer.Close();++epoch;Check(bool(init()),"fresh epoch consumes real current data");source.calls=0;source.fail=6;
    Check(!consumer.LoadTemplates(owner,epoch,vm,source,paths)&&consumer.Progress().failed&&consumer.Progress().loaded==5&&!consumer.Progress().templates_loaded,"partial load retained, never ready");
    consumer.Close();++epoch;source.calls=0;source.fail=0;Check(bool(init())&&bool(consumer.LoadTemplates(owner,epoch,vm,source,paths)),"actual retry after explicit retirement");
    source.leases.back()->live=false;Check(!consumer.Read(owner,epoch,vm,snap),"retired actual template refuses");consumer.Close();
    ++epoch;put(3,Value{});Check(!init()&&consumer.Progress().failed&&consumer.Progress().converted==3
        &&consumer.Progress().cells[0][0]=='x',"native Unknown differs from source nil; source-sequential partial cells retained");consumer.Close();put(3,nil);
    ++epoch;put(2,Value::string(std::string(32,'A')));Check(!init()&&consumer.Progress().converted==2,"explicit bounded32-byte overflow refusal");consumer.Close();put(2,values[2]);
    ++epoch;put(2,Value::string(std::string(1,'\x81')));Check(!init()&&consumer.Progress().converted==2,"truncated source negative-byte pair refuses");consumer.Close();put(2,values[2]);
    // Traverse every selected template/glyph branch through real parse/clone consumers.
    for(const auto mode:std::array<uint16_t,8>{{0,1,2,3,4,11,12,13}}){
        ++epoch;Check(bool(init())&&bool(consumer.LoadTemplates(owner,epoch,vm,source,paths)),"fresh actual mode consumer");
        auto b=parent_bytes;b[end-10]=uint8_t(mode);b[end-9]=0;
        auto l=std::make_shared<Lease>(b);auto a=std::make_shared<GameModelAsset>();auto p=std::make_shared<GameModelClone>();
        Check(bool(a->Decode(view(l)))&&bool(p->Initialize(a))&&bool(consumer.Attach(owner,epoch,vm,p))&&bool(consumer.Read(owner,epoch,vm,snap)),"actual marker mode attachment");
        const uint32_t selected=mode==1?3:mode==2?11:(mode>=3&&mode<=10?0:1);
        const size_t glyphs=mode==1||mode==2?2:(mode==0||mode==13?0:1);
        Check(snap.placements[0].model==selected&&snap.glyph_requests.size()==glyphs,"mode key is not direct model ordinal");
        if(glyphs){const char expected=mode==4||mode==12?'y':'x';const int style=mode==2||mode>=11?-12:0;
            Check(snap.glyph_requests[0].code==uint16_t(expected)&&snap.glyph_requests[0].style==style,"selected whole/one-unit bytes and signed-low8 style");}
        consumer.Close();p->Close();
    }
    ++epoch;put(0,Value::string(std::string(9,'A')));Check(bool(init())&&bool(consumer.LoadTemplates(owner,epoch,vm,source,paths)),"nine text units remain owned, not truncated");
    parent->Close();Check(bool(parent->Initialize(parent_asset))&&bool(consumer.Attach(owner,epoch,vm,parent))&&bool(consumer.Read(owner,epoch,vm,snap))
        &&snap.placements[0].characters==8&&snap.placements[0].model==9&&snap.cells[0][8]=='x',"source count clamps8 without truncating normalized data");
    Check(!consumer.LoadTemplates(owner,epoch,vm,source,paths),"template replay refused");
    std::shared_ptr<const void> owner_alias(owner.get(),[](const void*){});Check(!consumer.Read(owner_alias,epoch,vm,snap),"same address foreign shared-control-block refuses");
    consumer.Close();parent->Close();put(0,values[0]);
    ++epoch;Check(bool(init())&&bool(consumer.LoadTemplates(owner,epoch,vm,source,paths)),"current data before invalid marker");
    auto bad_bytes=parent_bytes;bad_bytes[end-12]=9;auto bad_lease=std::make_shared<Lease>(bad_bytes);auto bad_asset=std::make_shared<GameModelAsset>();auto bad_parent=std::make_shared<GameModelClone>();
    Check(bool(bad_asset->Decode(view(bad_lease)))&&bool(bad_parent->Initialize(bad_asset))&&!consumer.Attach(owner,epoch,vm,bad_parent)
        &&consumer.Progress().failed&&!consumer.Progress().attached,"text-cell key9 refuses before attachment");consumer.Close();bad_parent->Close();
    ++epoch;auto short_keys=std::make_shared<Lease>(std::vector<uint8_t>(95,255));auto bad_map=map;bad_map.keys=view(short_keys);
    Check(!consumer.Initialize(owner,epoch,vm,context,Table::Global,0,view(module),bad_map),"exact map extent refuses malformed packet");consumer.Close();
    ++epoch;Check(bool(init()),"current data before user-input retirement");fallback->live=false;Check(!consumer.Read(owner,epoch,vm,snap),"retired fallback owner revokes consumer");fallback->live=true;consumer.Close();
    ++epoch;Check(bool(init()),"current owner reset after failed text");module->b[8]=1;Check(!consumer.Read(owner,epoch,vm,snap),"module alias drift refuses");module->b[8]=0;
    (void)vm.start();Check(!consumer.Read(owner,epoch,vm,snap),"VM restart revokes current run even if context remains registered");consumer.Close();graphics.Close();parent->Close();
}
std::vector<uint8_t> GlyphTestGim(uint32_t color){
    ContactTextureSource source;source.alpha=255;PackageView view;Check(bool(source.OpenResource("CONTACT_TEST.GIM",view)),"existing synthetic GIM builder");
    std::vector<uint8_t> b(size_t(view.Describe().size));for(size_t i=0;i<b.size();++i)b[i]=view.ReadU8(i);
    for(size_t at=128;at<144;at+=4)Word(b,at,color);return b;
}
std::vector<uint8_t> GlyphTestModel(unsigned count,bool uv=true){
    auto b=Fixture(false,nullptr,uv);const auto p=dokodemo::model::parseDvs(b);const auto start=p.nodes[0].source.offset;
    std::vector<uint8_t> node(b.begin()+start,b.end());b.resize(start);b[p.bounds.source.offset+p.bounds.source.bytes]=uint8_t(count);
    for(unsigned i=0;i<count;++i){const auto at=b.size();b.insert(b.end(),node.begin(),node.end());Word(b,at,uint32_t(b.size()/4));
        const float x=(float(i)-float(count-1)/2)*2;uint32_t word;std::memcpy(&word,&x,4);Word(b,at+16,word);}
    return b;
}
struct GlyphFixtureSource final:GameResourceSource{
        unsigned glyph_calls=0,fail=0;bool uv=true,malformed=false,style_missing=false;std::function<void()> during;
        std::vector<std::shared_ptr<Lease>> glyph_leases;
        GameFsStatus OpenResource(const std::string& path,PackageView& out)override{
            std::vector<uint8_t> b;
            if(path.find(".DVS")!=path.npos){const auto index=unsigned(std::stoul(path.substr(6)));const auto n=index<2?1:index<10?index-1:index-9;b=GlyphTestModel(n,uv);}
            else if(path=="SYN/CONTACT_TEST.GIM")b=GlyphTestGim(0xFFFFFFFF);
            else{++glyph_calls;if(during)during();if(glyph_calls==fail||style_missing)return {GameFsCode::NotFound,0,"synthetic missing actual glyph/style resource"};
                Check(path=="SYN000_000_00120.GIM"||path=="SYN000_000_00121.GIM"||path=="SYN-12_-12_00120.GIM"||path=="SYN-12_-12_00121.GIM"||path=="SYN000_000_41306.GIM","source tuple -> exact synthetic file request");
                b=GlyphTestGim(path.find("00121")!=path.npos?0xFFFF0000:0xFF0000FF);if(malformed&&glyph_calls==2)b[0]=0;
                auto lease=std::make_shared<Lease>(b);glyph_leases.push_back(lease);out=PackageStorage::Attach(lease,b.size());return {};}
            auto lease=std::make_shared<Lease>(b);out=PackageStorage::Attach(lease,b.size());return {};
        }
};
void SupportGlyphTests(){
    using namespace dokodemo::script;
    const auto view=[](const std::shared_ptr<Lease>& p){return PackageStorage::Attach(p,p->b.size());};
    const auto text=[](std::string s){std::vector<uint8_t> b(s.begin(),s.end());b.push_back(0);return std::make_shared<Lease>(b);};
    std::vector<uint8_t> bytes(28+3+9*4);Word(bytes,0,0x0043534B);Word(bytes,4,0x20030204);Word(bytes,12,3);bytes[24]=9;bytes[28]=0x30;bytes[30]=0x38;Word(bytes,31,0x90000008);
    auto module=std::make_shared<Lease>(bytes);Program program;Diagnostic error;Check(program.load_test_fixture(bytes,error),"glyph synthetic module");Vm vm(program);(void)vm.start();const auto context=vm.context_value();
    const auto put=[&](unsigned i,std::string value){Value r;r.kind=Value::Kind::Reference;r.table=Table::Global;r.index=uint16_t(i+1);vm.store_reference(r,Value::string(value));};
    for(unsigned i=0;i<8;++i)put(i,i?"":"AB");
    auto keys=std::make_shared<Lease>(std::vector<uint8_t>(96,255));keys->b[0]='A';keys->b[1]='B';GameSupportTextMap map;map.keys=view(keys);
    std::vector<std::shared_ptr<Lease>> data;for(unsigned i=0;i<96;++i){data.push_back(text(i==0?"x":i==1?"y":""));map.expansions[i]=view(data.back());}
    auto fallback=text(""),number=text("%d"),format=text("SYN%03d_%03d_%05d.GIM");map.fallback=view(fallback);map.integer_format=view(number);
    GameSupportTemplatePaths paths;for(unsigned i=0;i<18;++i){data.push_back(text("PANEL_"+std::to_string(i)+".DVS"));paths.models[i]=view(data.back());}data.push_back(text("SYN/"));paths.textures=view(data.back());
    GlyphFixtureSource source;
    auto owner=std::make_shared<int>(1),foreign=std::make_shared<int>(2);uint64_t epoch=0;GameSupportTextOwner worker;std::shared_ptr<GameModelClone> parent;
    const auto begin=[&](uint16_t mode,bool material=false){worker.Close();++epoch;source.glyph_calls=0;source.glyph_leases.clear();
        auto b=AreaMarkerFixture(0,material);auto p=dokodemo::model::parseDvs(b);const auto end=p.nodes[0].source.offset+p.nodes[0].source.bytes;
        b[end-12]=1;b[end-11]=0;b[end-10]=uint8_t(mode);b[end-9]=0;b[end-8]=1;b[end-7]=0;
        auto lease=std::make_shared<Lease>(b);auto asset=std::make_shared<GameModelAsset>();Check(bool(asset->Decode(view(lease))),"glyph actual parent source");parent=std::make_shared<GameModelClone>();Check(bool(parent->Initialize(asset)),"glyph actual parent clone");
        Check(bool(worker.Initialize(owner,epoch,vm,context,Table::Global,0,view(module),map))&&bool(worker.LoadTemplates(owner,epoch,vm,source,paths))&&bool(worker.Attach(owner,epoch,vm,parent)),"production glyph prerequisites");};
    GameGraphics graphics;Check(graphics.Initialize({}),"offscreen glyph raster");auto camera=ModelIdentity();camera[0]=camera[5]=camera[10]=.1f;camera[12]=-.6f;camera[13]=-.8f;camera[14]=-1.1f;GameSupportSnapshot snap;
    begin(1);Check(!worker.DrawGlyphPreview(owner,epoch,vm,graphics,camera),"allocation/queue alone is not glyph completion");
    Check(!worker.PrepareGlyphTextures(foreign,epoch,vm,source,view(format))&&source.glyph_calls==0,"foreign worker refused before I/O");
    Check(bool(worker.PrepareGlyphTextures(owner,epoch,vm,source,view(format)))&&bool(worker.Read(owner,epoch,vm,snap))&&snap.glyph_pixels_ready&&snap.glyph_decoded==2&&snap.bound_texture_nodes==2&&!snap.source_current_bound,"actual decode -> owned pixels -> two clone-node bindings, no source authority");
    Check(snap.textures.size()==2&&snap.textures[0].pixels_sha256!=snap.textures[1].pixels_sha256&&source.glyph_calls==2,"two real pixel digests; dedup only once per tuple");
    Check(graphics.Clear(0xFF000000)&&bool(worker.DrawGlyphPreview(owner,epoch,vm,graphics,camera))&&graphics.SwapBuffers(),"bound clone consumes decoded pixels");GameGraphicsFrame frame;Check(graphics.CopyFront(frame),"actual component frame");
    Check(std::count(frame.pixels.begin(),frame.pixels.end(),0xFFFF0000)>0&&std::count(frame.pixels.begin(),frame.pixels.end(),0xFF0000FF)>0,"both selected glyph textures reach actual raster");
    Check(!worker.PrepareGlyphTextures(owner,epoch,vm,source,view(format))&&source.glyph_calls==2,"worker replay does not load twice");
    format->b[0]='Q';Check(!worker.Read(owner,epoch,vm,snap),"format drift revokes current stage");format->b[0]='S';
    source.glyph_leases[0]->b[128]^=1;Check(!worker.Read(owner,epoch,vm,snap)&&!worker.DrawGlyphPreview(owner,epoch,vm,graphics,camera),"pixel backing drift prevents rendering");source.glyph_leases[0]->b[128]^=1;
    auto root=ModelIdentity();root[12]=1;Check(bool(parent->Update(root))&&!worker.Read(owner,epoch,vm,snap)&&bool(worker.Update(owner,epoch,vm))&&bool(worker.Read(owner,epoch,vm,snap))&&snap.glyph_pixels_ready,"parent world update keeps actual pixel bindings after explicit update");
    for(uint16_t mode:{uint16_t(2),uint16_t(4),uint16_t(12),uint16_t(13)}){begin(mode);Check(bool(worker.PrepareGlyphTextures(owner,epoch,vm,source,view(format)))&&bool(worker.Read(owner,epoch,vm,snap)),"whole/unit/signedstyle/absent branch actual worker");
        Check(snap.bound_texture_nodes==(mode==2?2u:mode==13?0u:1u),"selected glyph cardinality, no placeholder for absent unit");}
    put(0,"AA");begin(1);Check(bool(worker.PrepareGlyphTextures(owner,epoch,vm,source,view(format)))&&bool(worker.Read(owner,epoch,vm,snap))&&source.glyph_calls==1&&snap.bound_texture_nodes==2,"same tuple reuses one decoded image in two nodes");put(0,"AB");
    begin(1,true);Check(!worker.PrepareGlyphTextures(owner,epoch,vm,source,view(format))&&source.glyph_calls==0,"unclosed nonnull marker descriptor refuses before I/O");
    begin(1);source.fail=2;Check(!worker.PrepareGlyphTextures(owner,epoch,vm,source,view(format))&&worker.Progress().glyph_decoded==1&&!worker.Progress().glyph_pixels_ready&&worker.Progress().bound_texture_nodes==0,"partial load cannot publish textures");source.fail=0;
    begin(1);source.malformed=true;Check(!worker.PrepareGlyphTextures(owner,epoch,vm,source,view(format))&&worker.Progress().glyph_decoded==1&&!worker.Progress().glyph_pixels_ready,"partial decode cannot publish textures");source.malformed=false;
    begin(1);source.style_missing=true;Check(!worker.PrepareGlyphTextures(owner,epoch,vm,source,view(format)),"unsupported code/style has no success fallback");source.style_missing=false;
    begin(1);source.during=[&]{parent->Close();};Check(!worker.PrepareGlyphTextures(owner,epoch,vm,source,view(format))&&!worker.Progress().glyph_pixels_ready,"retired parent during decode prevents late binding");source.during={};
    begin(1);Check(bool(worker.PrepareGlyphTextures(owner,epoch,vm,source,view(format))),"retry under fresh current epoch");source.glyph_leases[0]->live=false;Check(!worker.Read(owner,epoch,vm,snap),"retired pixel input revokes worker");
    put(0,std::string("\xA1Z",2));begin(1);Check(bool(worker.PrepareGlyphTextures(owner,epoch,vm,source,view(format)))&&bool(worker.Read(owner,epoch,vm,snap))&&snap.textures[0].glyph.code==0xA15A,"raw pair lookup is not generic CP932 rewriting");put(0,"AB");
    source.uv=false;begin(1);Check(!worker.PrepareGlyphTextures(owner,epoch,vm,source,view(format))&&!worker.Progress().glyph_pixels_ready,"missing authored UV refuses material binding");source.uv=true;
    begin(1);auto bad=text("SYN%03d_%05d.GIM");Check(!worker.PrepareGlyphTextures(owner,epoch,vm,source,view(bad))&&source.glyph_calls==0,"malformed format refuses before file I/O");
    worker.Close();Check(!worker.Read(owner,epoch,vm,snap)&&!worker.DrawGlyphPreview(owner,epoch,vm,graphics,camera),"retired worker cannot reuse old texture receipt");parent->Close();graphics.Close();
    // Two clones share the immutable material; only one receives the override.
    auto ml=std::make_shared<Lease>(GlyphTestModel(1));auto asset=std::make_shared<GameModelAsset>();Check(bool(asset->Decode(view(ml),&source,"SYN/")),"material alias test template");GameModelClone a,b;Check(bool(a.Initialize(asset))&&bool(b.Initialize(asset)),"two clones same asset");
    auto gl=std::make_shared<Lease>(GlyphTestGim(0xFF0000FF));auto image=std::make_shared<GameImage>();Check(bool(image->DecodeGim(view(gl))),"owned glyph image");
    Check(!a.BindDiffuseTextures(a.Generation()+1,{{0,image}})&&bool(a.BindDiffuseTextures(a.Generation(),{{0,image}}))&&a.DiffuseTextureCount()==1&&b.DiffuseTextureCount()==0&&asset->Materials()[0].texture->Pixels()[0]==0xFFFFFFFF,"node binding never mutates shared material");
    Check(!a.BindDiffuseTextures(a.Generation(),{{0,image},{0,image}})&&a.DiffuseTextureCount()==1,"invalid upload preserves previous binding");const auto old=image->Generation();image->Close();Check(!a.DiffuseTexturesCurrent()&&b.DiffuseTexturesCurrent(),"image retirement isolated to bound clone");
    Check(bool(image->DecodeGim(view(gl)))&&image->Generation()!=old&&!a.DiffuseTexturesCurrent(),"same image allocation new decode cannot revive old generation");a.Close();b.Close();
}
void ManagerSupportJobTests(){
    using namespace dokodemo::script;
    const auto view=[](const std::shared_ptr<Lease>& p){return PackageStorage::Attach(p,p->b.size());};
    const auto text=[](std::string s){std::vector<uint8_t> b(s.begin(),s.end());b.push_back(0);return std::make_shared<Lease>(b);};
    auto keys=std::make_shared<Lease>(std::vector<uint8_t>(96,255));keys->b[0]='A';keys->b[1]='B';GameSupportTextMap map;map.keys=view(keys);
    std::vector<std::shared_ptr<Lease>> data;for(unsigned i=0;i<96;++i){data.push_back(text(i==0?"x":i==1?"y":""));map.expansions[i]=view(data.back());}
    auto fallback=text(""),number=text("%d"),format=text("SYN%03d_%03d_%05d.GIM");map.fallback=view(fallback);map.integer_format=view(number);
    GameSupportTemplatePaths paths;for(unsigned i=0;i<18;++i){data.push_back(text("PANEL_"+std::to_string(i)+".DVS"));paths.models[i]=view(data.back());}data.push_back(text("SYN/"));paths.textures=view(data.back());
    auto model=AreaMarkerFixture(0,false);const auto parsed=dokodemo::model::parseDvs(model);const auto end=parsed.nodes[0].source.offset+parsed.nodes[0].source.bytes;
    model[end-12]=1;model[end-10]=1;model[end-8]=1;
    auto names=std::make_shared<Lease>(std::vector<uint8_t>(60));for(unsigned i=0;i<3;++i)std::copy_n("BOUND_A",7,names->b.begin()+i*20);
    const auto file=BackgroundArchive({{"BOUND_A.BPM",BackgroundArchive({{"BOUND_A\\model01.DVS",model}})}});
    auto actor_data=std::make_shared<Lease>(std::vector<uint8_t>(20));GameTransition transition;Check(transition.Initialize({{0,0}}),"support real transition owner");
    GameActorRuntime runtime;Check(bool(runtime.Initialize(0,view(actor_data),transition)),"support actual manager constructor");
    Check(runtime.SupportRegistration().Snapshot().live&&!runtime.SupportRegistration().Snapshot().registered,"constructor is not a default current data packet");
    auto owner=std::make_shared<int>(17),foreign=std::make_shared<int>(18);GameBackgroundPreparation job;uint64_t epoch=0;
    GameGraphics graphics;Check(graphics.Initialize({}),"bound job offscreen raster");auto camera=ModelIdentity();camera[0]=camera[5]=camera[10]=.1f;camera[12]=-.6f;camera[13]=-.8f;camera[14]=-1.1f;
    const auto begin=[&]{Check(bool(job.Close()),"close previous support job");++epoch;auto io=std::make_shared<BackgroundFileState>();
        Check(bool(job.Begin(owner,epoch,{27,12,0,epoch},view(names),std::make_unique<BackgroundFile>(file,io)))&&bool(job.Load(owner,epoch)),"same request actual package/marker clone load");return io;};
    // Generated bytecode only. It executes the real reference, service, call,
    // callback queue, deadline and event-return paths; no context field writes.
    for(bool frame_binding:{false,true}){
        std::vector<uint8_t> code{0x30,uint8_t(frame_binding?9:0)};
        const auto ref=[&](uint8_t op,uint16_t i){code.push_back(op);H(code,i);};
        if(frame_binding){code.push_back(0x4E);H(code,0);H(code,8);ref(0x91,0);ref(0x95,0);code.push_back(0x57);}
        code.push_back(0x80);code.push_back(16);ref(frame_binding?0x91:0x95,0);code.insert(code.end(),{0x8B,0xF0,2});
        const auto call=code.size();code.insert(code.end(),{0x20,0,0,0,0});const auto continuation=code.size();code.push_back(0x38);
        const auto wait=code.size();code.insert(code.end(),{0x30,0,0x8C,0x8A,0,0,0,0x85,0x85,0x8B,0x27,4,0x80,1,0x44,0x31});
        const auto callback=code.size();code.insert(code.end(),{0x30,3,0x3A,0x32});
        for(unsigned i=0;i<3;++i){code[call+2+i]=uint8_t(wait>>(i*8));code[wait+4+i]=uint8_t(callback>>(i*8));}
        std::vector<uint8_t> bytes(28);Word(bytes,0,0x0043534B);Word(bytes,4,0x20030204);Word(bytes,12,uint32_t(code.size()));bytes[24]=9;bytes.insert(bytes.end(),code.begin(),code.end());
        W(bytes,0x90000008);for(unsigned i=0;i<8;++i)W(bytes,0);auto module=std::make_shared<Lease>(bytes);Program program;Diagnostic diagnostic;
        Check(program.load_test_fixture(bytes,diagnostic),"generated registration/wait module");Vm vm(program),other(program);Check(vm.start().status==Status::Running&&other.start().status==Status::Running,"two actual VM run owners");
        for(unsigned i=0;i<8;++i){Value r;r.kind=Value::Kind::Reference;r.table=Table::Global;r.index=uint16_t(i+1);vm.store_reference(r,Value::string(i?"":"AB"));}
        Value registered;VmStateWitness before_callback;unsigned registration_calls=0,posts=0;
        vm.host_service([&](uint8_t id,const std::vector<Value>& args){
            if(id==0xF0){Check(args.size()==2&&args[0].signed_integer()==16,"actual synthetic GAME16 arguments");registered=args[1];++registration_calls;
                const auto status=runtime.RegisterSupportData16(vm,registered);Check(bool(status),status.detail.c_str());Value result;result.kind=Value::Kind::Scalar;result.word=0x10000000;return result;}
            Check(id==0x27&&args.size()==4,"only actual callback builtin admitted");before_callback=vm.current_witness();++posts;return Value::integer(vm.post_callback(args[0],args[1],args[2],args[3]));});
        const auto first=vm.run(100);if(first.status==Status::Stopped)throw std::runtime_error(first.diagnostic.text());
        Check(first.status==Status::Yield&&registration_calls==1&&posts==1&&vm.last_callback_receipt()==1,"real service -> self callback -> one deadline yield");
        Check(vm.run(100).status==Status::Yield&&!vm.context_enabled()&&vm.callbacks_returned()==1&&vm.callback_depth()==0&&vm.queued_callbacks()==0,"3A/event32 actually restore disabled caller");
        const auto paused=vm.current_witness();Check(paused.code_entry==wait&&paused.caller_return_pc==continuation&&paused.call_depth==1&&paused.deadline_tick==1
            &&paused.frame_id==before_callback.frame_id&&paused.caller_frame_id==before_callback.caller_frame_id&&paused.result.word==0x20000000,"restored caller, result, stack and source deadline");
        const auto binding=runtime.SupportRegistration().Snapshot();Check(binding.registered&&binding.run==paused.run_id&&binding.table==uint32_t(frame_binding?Table::Frame:Table::Global)
            &&runtime.SupportRegistration().Current(vm)&&!runtime.SupportRegistration().Current(other),"current module/context/ref lineage, not another VM");
        if(frame_binding)Check(binding.frame==paused.caller_frame_id&&binding.frame!=paused.frame_id,"registered ancestor-frame borrow survives wait callee");
        const auto old_runtime=runtime.Snapshot();auto io=begin();GlyphFixtureSource source;GameSupportSnapshot support;
        Check(!job.PrepareSupport(foreign,epoch,vm,runtime.SupportRegistration(),view(module),map,source,paths,view(format))&&source.glyph_calls==0,"foreign current job refuses before glyph load");
        Check(bool(job.PrepareSupport(owner,epoch,vm,runtime.SupportRegistration(),view(module),map,source,paths,view(format)))&&bool(job.ReadSupport(owner,epoch,vm,support)),"production manager -> same-job normalize/load/clone/glyph consumer");
        Check(support.source_current_bound&&support.source_registration==binding.serial&&support.manager_epoch==binding.manager_epoch&&support.glyph_pixels_ready&&support.converted==8
            &&support.cells[0][0]=='x'&&support.cells[0][1]=='y'&&source.glyph_calls==2,"eight actual registered values and selected material pixels, no supplied ready");
        Check(!job.PrepareSupport(owner,epoch,vm,runtime.SupportRegistration(),view(module),map,source,paths,view(format))&&!job.Draw(owner,epoch,graphics,camera),"support cannot replay or be silently omitted by plain draw");
        Check(graphics.Clear(0xFF000000)&&bool(job.DrawWithSupport(owner,epoch,vm,graphics,camera))&&graphics.Snapshot().mesh_pixels>0,"same staged clone consumes actual owned glyph pixels");
        Check(!job.Snapshot().source_committed&&runtime.Snapshot().manager_scene14==old_runtime.manager_scene14&&runtime.Snapshot().manager_switch.requested18==old_runtime.manager_switch.requested18
            &&vm.current_witness().frame_id==paused.frame_id&&vm.pc()==paused.current_pc&&!vm.context_enabled()&&vm.deadline_tick()==1,"component preparation never commits scene, changes request, wakes or advances VM");
        auto cell=registered;++cell.index;vm.store_reference(cell,Value::string("B"));Check(!job.ReadSupport(owner,epoch,vm,support),"current record drift revokes staged readback");vm.store_reference(cell,Value::string("AB"));
        Check(bool(job.ReadSupport(owner,epoch,vm,support)),"exact unowned component value restoration is observable");
        Check(!runtime.RegisterSupportData16(vm,Value::integer(0))&&runtime.SupportRegistration().Snapshot().serial==binding.serial,"invalid replacement preserves actual binding");
        auto unbound=registered;unbound.index=UINT16_MAX;Check(!runtime.RegisterSupportData16(vm,unbound),"out-of-bounds registration does not supply nil");
        auto foreign_context=registered;foreign_context.context_owner=other.context_value().context_owner;
        Check(!runtime.RegisterSupportData16(vm,foreign_context)&&runtime.SupportRegistration().Snapshot().serial==binding.serial,"foreign typed context cannot replace this manager's current data");
        Check(bool(runtime.RegisterSupportData16(vm,registered))&&!job.ReadSupport(owner,epoch,vm,support),"genuine replacement invalidates old serial even at identical coordinate");
        io=begin();source.glyph_calls=0;source.fail=2;
        Check(!job.PrepareSupport(owner,epoch,vm,runtime.SupportRegistration(),view(module),map,source,paths,view(format))&&job.Snapshot().failed&&!job.Snapshot().staged
            &&!job.ReadSupport(owner,epoch,vm,support)&&!vm.context_enabled(),"partial real glyph load cannot publish support/job completion");
        Check(bool(job.Close())&&io->closes==1,"partial support retires before real file close");source.fail=0;source.glyph_calls=0;io=begin();
        source.during=[&]{names->b[0]='X';};
        Check(!job.PrepareSupport(owner,epoch,vm,runtime.SupportRegistration(),view(module),map,source,paths,view(format))&&job.Snapshot().failed&&!job.Snapshot().staged,"background registration drift during real glyph load prevents stage publication");
        names->b[0]='B';source.during={};source.glyph_calls=0;io=begin();
        Check(bool(job.PrepareSupport(owner,epoch,vm,runtime.SupportRegistration(),view(module),map,source,paths,view(format))),"fresh request can retry owned preparation");
        runtime.Close();Check(!job.ReadSupport(owner,epoch,vm,support)&&!job.DrawWithSupport(owner,epoch,vm,graphics,camera),"retired manager revokes an otherwise live staged job");
        Check(bool(runtime.Initialize(0,view(actor_data),transition))&&!runtime.SupportRegistration().Current(vm),"new manager epoch is not old registration authority");
        Check(bool(runtime.RegisterSupportData16(vm,registered)),"actual current packet can be explicitly rebound by the producer");
        const auto retired_run=vm.current_witness().run_id;Check(vm.start().status==Status::Running&&vm.current_witness().run_id!=retired_run&&!runtime.SupportRegistration().Current(vm)
            &&!job.ReadSupport(owner,epoch,vm,support),"module restart cannot revive old table/frame binding");Check(bool(job.Close()),"restart cleanup");
    }
    const auto old_epoch=runtime.Snapshot().owner_epoch;runtime.Close();Check(!runtime.SupportRegistration().Snapshot().live&&!runtime.SupportRegistration().Snapshot().registered,"actual manager retirement drops weak current data");
    Check(bool(runtime.Initialize(0,view(actor_data),transition))&&runtime.Snapshot().owner_epoch!=old_epoch&&!runtime.SupportRegistration().Snapshot().registered,"fresh constructor has no inherited current packet");runtime.Close();graphics.Close();

}
struct TitleSource final : GameResourceSource {
    bool fail = false;
    GameFsStatus OpenResource(const std::string &path, PackageView &out) override {
        if (fail)
            return {GameFsCode::NotFound, 0, "synthetic selected source failure"};
        auto lease =
            std::make_shared<Lease>(path.find(".DVS") != std::string::npos ? Fixture() : MotionFixture());
        out = PackageStorage::Attach(lease, lease->b.size());
        return {};
    }
};
std::vector<uint8_t> SyntheticMainData(){
    std::vector<uint8_t> b(1341110);Word(b,0,0x464C457F);b[4]=b[5]=1;b[16]=0xA0;b[17]=0xFF;b[18]=8;
    Word(b,28,52);Word(b,32,1310000);b[42]=32;b[44]=2;b[46]=40;b[48]=1;
    Word(b,52,1);Word(b,56,128);Word(b,68,932728);Word(b,84,1);Word(b,88,0xE3C00);Word(b,100,1341110-0xE3C00);
    Word(b,1310004,0x700000A0);Word(b,1310016,1310100);Word(b,1310020,29*8);
    for(unsigned i=0;i<25;++i){Word(b,1310100+i*8,0x6808+i*4);Word(b,1310104+i*8,0x102);
        Word(b,0xE3C00+0x6808+i*4,0xDE000+i*80);const auto name="sample"+std::to_string(i);std::copy(name.begin(),name.end(),b.begin()+128+0xDE000+i*80);}
    unsigned n=25;for(const auto pair:std::array<std::array<uint32_t,2>,4>{{{{0xD2D0,0x10005}},{{0xD2D8,0x10006}},{{0xBD44,5}},{{0xBD54,6}}}}){Word(b,1310100+n*8,pair[0]);Word(b,1310104+n*8,pair[1]);++n;}
    for(unsigned i=0;i<19;++i)Word(b,128+0xD8CC8+i*4,i);for(unsigned i=0;i<10;++i)Word(b,128+0xD85B8+i*4,i*11);
    const std::string root="/SYN";std::copy(root.begin(),root.end(),b.begin()+128+905276);
    for(unsigned row:{0u,12u})for(unsigned k=0;k<3;++k){const float v=float(17*row+3*k);uint32_t word=0;std::memcpy(&word,&v,4);Word(b,0xE3C00+0x4180+16*row+4*k,word);}
    return b;
}

std::vector<uint8_t> SyntheticOutingData(){
    auto b=SyntheticMainData();unsigned reloc=29;
    auto text=[&](unsigned segment,uint32_t offset,const std::string& value){const auto at=(segment?0xE3C00:128)+offset;
        std::copy(value.begin(),value.end(),b.begin()+at);b[at+value.size()]=0;};
    auto pointer=[&](uint32_t site,uint32_t value,unsigned segment){
        Word(b,0xE3C00+site,value);Word(b,1310100+reloc*8,site);Word(b,1310104+reloc*8,(segment<<16)|0x102);++reloc;};
    text(0,0xDC000,"motion");text(0,0xDC010,"panel");text(0,0xDC020,"x");
    for(unsigned row=0;row<6;++row){
        if(row%2)pointer(0x488+row*12+8,0xDC000,0); // main-only, no child producer
        else {pointer(0x488+row*12,0xDC000,0);pointer(0x488+row*12+4,0xDC000,0);}
    }
    pointer(0x9B0,0x488,1);for(unsigned i=0;i<18;++i)pointer(0x4ACC+i*4,0xDC010,0);
    for(unsigned i=0;i<96;++i){b[0xE3C00+0x4B14+i]=uint8_t(i);pointer(0x4B74+i*4,0xDC020,0);}
    Word(b,1310020,reloc*8);
    for(unsigned i=0;i<3;++i)text(1,0x43C4+(66+i)*20,"SYN_"+std::to_string(i));
    text(1,0x43C4+29*20,"TEXT");text(0,0xD3058,"%s\\%s\\model02.DVS");text(0,0xD307C,"%s\\light%d.DAK");
    text(0,0xD9C50,"F%03d\\F%03d\\F%05d.GIM");text(0,0xD9C68,"panels");
    text(0,0xD9C74,"%s\\%s\\%s");text(0,0xD9C80,"%s\\%s\\");text(0,0xE075C,"%d");text(0,0xE0760,"?");
    return b;
}
void OutingDataPacketTests(){
    const auto bytes=SyntheticOutingData();const auto digest=dokodemo::core::sha256(bytes.data(),bytes.size());auto io=std::make_shared<BackgroundFileState>();GameMainSceneData data;
    Check(bool(data.Load(std::make_unique<BackgroundFile>(bytes,io),digest)),"synthetic held DATA parser");
    std::shared_ptr<const GameOutingData> packet;Check(bool(data.Outing12(packet))&&packet&&packet->Current(),"selected DATA packet from actual relocations and held file");
    Check(packet->registrations.Describe().size==60&&packet->motion_counts.Describe().size==6&&packet->triplets.Describe().size==84
        &&packet->auxiliary_names.size()==1&&packet->support_paths.models[0].ReadU8(0)=='T',"recipe, eighteen paths, map and current DATA provenance");
    Check(bool(data.Close())&&!packet->Current(),"DATA retirement revokes generated text packet even while bytes retained");
    auto missing=bytes;Word(missing,1310100+29*8,0x4D0);GameMainSceneData bad;auto bad_io=std::make_shared<BackgroundFileState>();
    const auto hash=dokodemo::core::sha256(missing.data(),missing.size());Check(bool(bad.Load(std::make_unique<BackgroundFile>(missing,bad_io),hash)),"parser separates BGM and outing selected input domains");
    std::shared_ptr<const GameOutingData> unchanged=packet;Check(!bad.Outing12(unchanged)&&unchanged==packet,"missing selected relocation refuses without replacing output");Check(bool(bad.Close()),"bad packet cleanup");
}
int OutingPacketCli(const char* path,const char* identity){
    std::ifstream file(path,std::ios::binary);Check(bool(file),"prepared DATA file unavailable");
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});std::array<uint8_t,32> expected{};
    const std::string hex(identity);Check(hex.size()==64,"explicit expected DATA identity required");
    const auto nibble=[](char c){if(c>='0'&&c<='9')return unsigned(c-'0');if(c>='a'&&c<='f')return unsigned(c-'a'+10);throw std::runtime_error("DATA hex identity");};
    for(unsigned i=0;i<32;++i)expected[i]=uint8_t(nibble(hex[2*i])*16+nibble(hex[2*i+1]));
    GameMainSceneData data;auto io=std::make_shared<BackgroundFileState>();auto status=data.Load(std::make_unique<BackgroundFile>(std::move(bytes),io),expected);
    if(!status)throw std::runtime_error(status.detail);std::shared_ptr<const GameOutingData> packet;status=data.Outing12(packet);
    std::cout<<"OUTING_PACKET current="<<(status&&packet&&packet->Current())<<" detail="<<status.detail<<'\n';
    const bool ok=bool(status);Check(bool(data.Close()),"prepared DATA close");return ok?0:2;
}

void MainSceneDataTests(){
    OutingDataPacketTests();
    const auto bytes=SyntheticMainData();const auto digest=dokodemo::core::sha256(bytes.data(),bytes.size());auto io=std::make_shared<BackgroundFileState>();GameMainSceneData data;
    Check(bool(data.Load(std::make_unique<BackgroundFile>(bytes,io),digest))&&data.Snapshot().live&&data.Snapshot().names==25,"actual owning file/parser with explicitly synthetic expected identity");
    Check(data.Lists().ReadLE32(8)==2&&data.Gains().ReadLE32(36)==99&&data.Name(24).ReadU8(0)=='s',"selected DATA names/list/gain use real held views");
    std::array<float,4> position{};Check(bool(data.Placement(12,position))&&position==std::array<float,4>{{204,207,210,0}},"relocation-resolved PH1 placement distinct from PH0 heading");
    const auto old=position;Check(!data.Placement(1,position)&&position==old,"unadmitted placement row refuses without output mutation");
    const auto held=data.Name(0),list=data.Lists();list.WriteLE32(0,77);Check(!data.Snapshot().live,"selected DATA alias drift revokes current readback");
    try{(void)data.Name(0);Check(false,"drifted DATA getter refusal");}catch(const std::exception&){}list.WriteLE32(0,0);Check(data.Snapshot().live,"restored actual bytes recover unchanged held DATA");io->fail_close=true;Check(!data.Close()&&!data.Snapshot().live&&io->live,"failed close retains file owner; DATA output retired");io->fail_close=false;Check(bool(data.Close())&&!io->live,"main DATA cleanup retries");
    try{(void)data.Lists();Check(false,"retired DATA getter refusal");}catch(const std::exception&){}
    auto invalid=bytes;Word(invalid,1310100,0x680C);GameMainSceneData duplicate;auto bad_io=std::make_shared<BackgroundFileState>();
    Check(!duplicate.Load(std::make_unique<BackgroundFile>(invalid,bad_io),dokodemo::core::sha256(invalid.data(),invalid.size()))&&!duplicate.Snapshot().live&&bool(duplicate.Close()),"duplicate/missing exact selected relocation refuses before binding");
    auto overflow=bytes;Word(overflow,32,0xFFFFFFF0);GameMainSceneData malformed;auto malformed_io=std::make_shared<BackgroundFileState>();
    Check(!malformed.Load(std::make_unique<BackgroundFile>(overflow,malformed_io),dokodemo::core::sha256(overflow.data(),overflow.size()))&&bool(malformed.Close()),"overflowing header metadata is refused before arithmetic wraps");
    GameMainSceneData foreign;auto other=std::make_shared<BackgroundFileState>();Check(!foreign.Load(std::make_unique<BackgroundFile>(bytes,other),{})&&!foreign.Snapshot().live&&bool(foreign.Close()),"actual expected-identity mismatch cannot issue source input");
}
void SceneBgmCurrentOwnerTests(){
    const auto view=[](const std::shared_ptr<Lease>& p){return PackageStorage::Attach(p,p->b.size());};
    auto lists=std::make_shared<Lease>(std::vector<uint8_t>(76)),gains=std::make_shared<Lease>(std::vector<uint8_t>(40));
    for(unsigned i=0;i<19;++i)Word(lists->b,i*4,i);for(unsigned i=0;i<10;++i)Word(gains->b,i*4,i*11);
    auto root=std::make_shared<Lease>(std::vector<uint8_t>{'/','S','Y','N',0});std::array<std::shared_ptr<Lease>,25> names;
    for(unsigned i=0;i<25;++i){const auto text="sample"+std::to_string(i);std::vector<uint8_t> b(text.begin(),text.end());b.push_back(0);names[i]=std::make_shared<Lease>(b);}
    std::vector<uint8_t> backing(4*1024*1024);GameArena arena;GameVmBoot manager;
    Check(arena.Initialize(backing.data(),uint32_t(backing.size()))&&manager.Initialize74EF4(arena)&&manager.InitializeManager93404(),"real native content arena/module");
    auto content=std::make_unique<GameContentBoot>();Check(content->InitializeBB8(arena,manager)&&content->Reset24994(),"actual owning settings constructor/root18");
    RngNativeOwner rng;rng.ImportCurrentState64(91,7,"synthetic scene BGM lineage");content->SetTitleRng(rng);
    unsigned reads=0,names_read=0,factories=0;std::array<uint32_t,7> calendar{{0,0,0,12,12,5,2026}};
    Check(bool(content->SetBackgroundRuntime([&]{++reads;return calendar;})),"same real initialized clock/RNG/module producer");auto runtime=content->BackgroundRuntime().lock();
    const auto name=[&](uint32_t i){++names_read;return view(names.at(i));};std::vector<std::shared_ptr<BackgroundFileState>> io;
    const auto factory=[&](const std::string&,std::unique_ptr<GameFileSource>& out)->GameFsStatus{++factories;auto s=std::make_shared<BackgroundFileState>();io.push_back(s);out=std::make_unique<BackgroundFile>(std::vector<uint8_t>(16,3),s);return {};};
    auto alias=std::shared_ptr<GameBackgroundRuntime>(runtime.get(),[](GameBackgroundRuntime*){});GameSceneBgm bgm,bad;
    Check(!bad.Initialize(alias,1,*content,view(lists),view(gains),view(root),name,factory),"same-address foreign controlblock cannot initialize scene BGM");
    Check(bool(bgm.Initialize(runtime,1,*content,view(lists),view(gains),view(root),name,factory)),"current settings/clock/data BGM owner");int32_t current=-77;
    Check(!bgm.CurrentChoice(current)&&current==-77,"cold constructor4BC0 is not an already executed initial selection");
    auto input=std::make_shared<Lease>(std::vector<uint8_t>(16));GameTransition fade;Check(fade.Initialize({{0,0}}),"scene clock transition");TitleSource resources;GameActorRuntime actor,foreign;
    Check(bool(foreign.Initialize(0,view(input),fade,&resources))&&!bgm.BeginInitial(runtime,foreign)&&!reads&&!names_read&&!factories&&rng.CurrentSeed64()==91,"foreign/unbound actor module refuses BEFORE clock/selection/file effects");
    Check(bool(actor.Initialize(0,view(input),fade,&resources,runtime))&&!bgm.BeginInitial(alias,actor)&&!reads,"initial source prefix keeps SAME runtime controlblock");
    Word(gains->b,0,1);Check(!bgm.BeginInitial(runtime,actor)&&!reads&&rng.CurrentSeed64()==91,"aliased held map drift refuses before source effects");Word(gains->b,0,0);
    {const auto initial_status=bgm.BeginInitial(runtime,actor);if(!initial_status)throw std::runtime_error(initial_status.detail);}const auto initial=bgm.Snapshot();
    Check(reads==1&&names_read==1&&factories==1&&initial.selected&&initial.requested&&initial.selection.rng_calls==1
        &&initial.selection.before==91&&initial.selection.after==rng.CurrentSeed64()&&initial.selection.previous==0
        &&initial.actor_epoch==actor.Snapshot().owner_epoch&&initial.initial_clock52==0&&actor.Snapshot().scene_period_table52==0
        &&!actor.Snapshot().scene_model_live&&bgm.CurrentChoice(current)&&current==initial.stored4BC,"actual stored period/4BC lineage, not fixture24/static6/late seasonal row");
    const auto seed=rng.CurrentSeed64();Check(!bgm.BeginInitial(runtime,actor)&&rng.CurrentSeed64()==seed,"initial request cannot replay RNG");
    io[0]->missing=true;Check(!bgm.Pump(runtime)&&bgm.Snapshot().file.current&&bgm.Snapshot().file.state==1&&bgm.Snapshot().file.selector60>=0,"later genuine missing I/O remains pending, not wrapperfailure");
    Check(bool(actor.InitializeScene0(initial.calendar)),"background5F58 still runs despite ignored later BGM I/O failure");io[0]->missing=false;
    Check(bool(bgm.Pump(runtime))&&bool(bgm.Pump(runtime))&&bgm.Snapshot().file.state==5&&bgm.Snapshot().file.bytes==16&&bgm.Snapshot().file.bank5C==-1,"actual file buffer/read completion stays distinct from bank5C");
    Check(bool(bgm.RequestControl(runtime,7))&&bgm.Snapshot().file.control.pending&&bgm.Snapshot().file.control.gain0C==88
        &&bgm.Snapshot().file.control.bound&&bgm.Snapshot().file.control.source_owner==content->OptionSoundOwner().owner
        &&content->OptionSoundOwner().bgm_attempts==1&&bgm.Snapshot().file.control.handle14==-1&&bgm.Snapshot().control_return==0&&bgm.Snapshot().file.control.argument28==7
        &&bgm.Snapshot().file.control.linked_request==bgm.Snapshot().file.request,"4AD4C actually reads live root18 via14834 and links SAME owned file record; no playback");
    auto settings=content->OptionSettings();uint32_t word=999;settings.words[2]=5;
    Check(!ReadBgmGain14834(settings,view(gains),word)&&word==999,"synthetic index10 is not an initialized gain word");settings.words[2]=6;
    Check(bool(ReadBgmGain14834(settings,view(gains),word))&&word==0,"synthetic unsigned>=11 uses actual source fallback index0");
    auto records=std::make_shared<Lease>(std::vector<uint8_t>(60));std::vector<std::pair<std::string,std::vector<uint8_t>>> packages;
    for(unsigned i=0;i<3;++i){const auto n=std::string("SYN_")+char('A'+i);std::copy(n.begin(),n.end(),records->b.begin()+i*20);packages.push_back({n+".BPM",BackgroundArchive({{n+"\\model01.DVS",Fixture()}})});}
    GameBackgroundPreparation uncached;auto uncached_io=std::make_shared<BackgroundFileState>();
    Check(bool(uncached.Begin(runtime,2,{20,12,initial.period4B4,8},view(records),std::make_unique<BackgroundFile>(BackgroundArchive(packages),uncached_io)))
        &&!uncached.RequestBgm(runtime,2,runtime,bgm)&&!uncached.Snapshot().bgm_attempted&&rng.CurrentSeed64()==seed
        &&factories==1&&bool(uncached.Close()),"initial4B4 is not selected1B114 cache authority; refusal has no BGM effects");
    const auto cache_reads=reads;
    Check(!bgm.RefreshCachedPeriod(alias,actor)&&!bgm.RefreshCachedPeriod(runtime,foreign)&&reads==cache_reads&&rng.CurrentSeed64()==seed,"cached-period foreign scene/runtime refuse before clock/RNG");
    Check(bool(bgm.RefreshCachedPeriod(runtime,actor))&&bgm.Snapshot().cache_refreshed&&bgm.Snapshot().cache_revision==1
        &&rng.CurrentSeed64()==seed,"selected1B114 reads SAME current4DC clock and caches4B4/4AC without RNG");
    const auto period=bgm.Snapshot().period4B4;
    GameBackgroundPreparation job;auto target_io=std::make_shared<BackgroundFileState>();
    Check(bool(job.Begin(runtime,2,{21,12,period,9},view(records),std::make_unique<BackgroundFile>(BackgroundArchive(packages),target_io))),"pending target current typed packet owner");
    Check(!job.RequestBgm(runtime,2,alias,bgm)&&rng.CurrentSeed64()==seed,"target same-address foreign runtime refuses without RNG");
    Check(bool(job.RequestBgm(runtime,2,runtime,bgm))&&job.Snapshot().bgm_selection.previous==initial.stored4BC
        &&job.Snapshot().bgm_selection.before==seed&&job.Snapshot().bgm_selection.after==rng.CurrentSeed64()
        &&bool(job.Load(runtime,2)),"target consumes REAL stored4BC/cached4B4 and SAME manager file pool before model stage");
    Check(bool(job.PumpBgm(runtime,2))&&bool(job.PumpBgm(runtime,2))&&job.BgmFile().current,"target file actual owned buffer completes");
    Check(bool(bgm.RefreshCachedPeriod(runtime,actor))&&!job.BgmFile().current&&!job.PumpBgm(runtime,2),"later clock-cache publication invalidates older target borrow instead of silently reusing it");
    Check(bool(job.Close())&&io[0]->live&&io[1]->live,"manager BGM files outlive target job retirement");
    io[0]->fail_close=true;Check(!bgm.Close()&&!bgm.CurrentChoice(current)&&io[0]->live,"failed file close preserves actual owner for retry");io[0]->fail_close=false;
    Check(bool(bgm.Close())&&!io[0]->live&&!io[1]->live&&!bad.Initialize(runtime,2,*content,view(lists),view(gains),view(root),name,factory),"cleanup then no second cold constructor in SAME module epoch");
    actor.Close();foreign.Close();Check(content->Close()&&manager.Close(),"current settings/module teardown after files");
}
void RoomAnchorLocalX(){
    //Independent authored-pose literals: a quarter-turn sends local X into Y.
    std::vector<GameModelPose> pose(142);for(auto& p:pose)p.world=ModelIdentity();
    GameMatrix root=ModelIdentity();int32_t anchor=-9;
    const std::array<int32_t,9> nodes{{54,55,56,141,61,57,58,59,60}};
    for(size_t i=0;i<nodes.size();++i){pose[size_t(nodes[i])].world[12]=float(nodes[i]);
        Check(bool(RoomModelRootNativeSceneAnchorLocalX(int32_t(i+1),0,pose,root,anchor))&&anchor==nodes[i]&&root[12]==float(nodes[i]),"source selector map uses actual authored node");}
    auto& m=pose[57].world;m=ModelIdentity();m[0]=0;m[1]=1;m[4]=-1;m[5]=0;m[12]=10;m[13]=20;m[14]=30;
    Check(bool(RoomModelRootNativeSceneAnchorLocalX(6,2,pose,root,anchor))&&anchor==57&&root[12]==10&&root[13]==22&&root[14]==30,
          "NativeSceneAnchorLocalX rotates local spacing before world translation");
    m[13]=40;Check(bool(RoomModelRootNativeSceneAnchorLocalX(6,-3,pose,root,anchor))&&root[12]==10&&root[13]==37,
          "fresh authored pose replaces old transform without accumulating spacing");
    const auto prior=root;const auto prior_anchor=anchor;
    Check(!RoomModelRootNativeSceneAnchorLocalX(10,0,pose,root,anchor)&&root==prior&&anchor==prior_anchor,"out-of-range selector preserves output");
    Check(!RoomModelRootNativeSceneAnchorLocalX(6,std::numeric_limits<float>::infinity(),pose,root,anchor)&&root==prior,"nonfinite source spacing refusal");
    auto short_pose=pose;short_pose.resize(57);
    Check(!RoomModelRootNativeSceneAnchorLocalX(6,0,short_pose,root,anchor)&&root==prior,"missing selected authored node refusal");
    m[0]=std::numeric_limits<float>::quiet_NaN();
    Check(!RoomModelRootNativeSceneAnchorLocalX(6,0,pose,root,anchor)&&root==prior,"invalid authored transform refusal");
    Check(bool(RoomModelRootNativeSceneAnchorLocalX(0,-5,{},root,anchor))&&anchor==-1&&root[12]==-5&&root[13]==0,
          "native unparented local-X policy has no fabricated scene owner");
}
void ContextActorTransactionPrefixTests(){
    using namespace dokodemo::script;
    const auto view=[](const std::shared_ptr<Lease>& p){return PackageStorage::Attach(p,p->b.size());};
    const auto text=[](std::string s){std::vector<uint8_t> b(s.begin(),s.end());b.push_back(0);return std::make_shared<Lease>(b);};
    auto path=text("SYNTHETIC_BYSTANDER.DVS"),prefix=text("");
    std::vector<uint8_t> parameter_bytes;for(float v:{40.f,2.f,90.f,4.f,2.f,8.f})F(parameter_bytes,v);
    auto params=std::make_shared<Lease>(parameter_bytes),selection=std::make_shared<Lease>(std::vector<uint8_t>(20));
    std::vector<uint8_t> module_bytes(31);Word(module_bytes,0,0x0043534B);Word(module_bytes,4,0x20030204);Word(module_bytes,12,3);module_bytes[28]=0x30;module_bytes[30]=0x38;
    Program program;Diagnostic error;Check(program.load_test_fixture(module_bytes,error),"context actor synthetic module");
    TitleSource resources;GameTransition transition;Check(transition.Initialize({{0,0}}),"context actor transition");GameActorRuntime runtime;
    const auto initialize=[&]{runtime.Close();Check(bool(runtime.Initialize(0,view(selection),transition,&resources))&&bool(runtime.InitializeScene0({{0,0,0,12,12,5,2026}})),"actual primary scene/control owner");};
    const auto create=[&]{return runtime.CreateContextActor5(resources,view(path),view(prefix),view(params));};
    initialize();Vm unknown(program);unknown.start();Check(bool(runtime.BindScriptContextActor1C0FC(unknown)),"actual context can record an unknown slot without inventing nil");
    Check(!runtime.RequestPendingScene12(unknown)&&!runtime.Snapshot().manager_switch.pending&&runtime.Snapshot().context_actor.presence==GameContextActorPresence::Unknown,"globally live primary actor is not node24 presence");
    Check(bool(create()),"real independent actor5 asset/clone/parameter owner");
    auto s=runtime.Snapshot();Check(s.context_actor.current&&s.context_actor.constructor_actor==5&&!s.context_actor.model_visible&&s.context_actor.logical==std::array<float,4>{{40,0,0,8}},"source ctor logical position and actual hidden model control");
    Check(!runtime.RequestPendingScene12(unknown)&&!runtime.BindScriptContextActor1C0FC(unknown),"constructor-time unknown cannot silently rebase to a later actor");
    Vm current(program),foreign(program);current.start();foreign.start();Check(bool(runtime.BindScriptContextActor1C0FC(current)),"new context copies the current actor5 owner");
    const auto before=runtime.Snapshot();const auto w=current.current_witness();
    Check(!runtime.RequestPendingScene12(foreign)&&!runtime.Snapshot().manager_switch.pending,"foreign context cannot supply a source actor");
    Check(bool(runtime.RequestPendingScene12(current)),"actual present branch source request and move/hide prefix");s=runtime.Snapshot();
    Check(s.manager_switch.pending&&s.manager_switch.requested18==12&&s.manager_switch.current14==0&&s.manager_switch.last_request_store_pcs==std::array<uint32_t,2>{{0x1C2E4,0x1C2EC}},"source pending1C before18, distinct from adoption/current14");
    Check(s.context_actor.logical==std::array<float,4>{{90,0,0,16}}&&s.context_actor.moves==2&&s.context_actor.hides==2&&!s.context_actor.model_visible
        &&s.actor_position==before.actor_position&&s.actor_root==before.actor_root&&s.scene_model_live,"GAME12 moves its context actor, leaves real primary/live scene owned");
    Check(!runtime.RequestPendingScene12(current)&&!runtime.ConsumeSameManager0()&&current.pc()==w.current_pc&&current.context_enabled()&&current.deadline_tick()==w.deadline_tick,"prefix replay/cancel-as-consume refused; no VM pause/wake/result supplied");
    initialize();Check(bool(create()),"fresh actor epoch");Vm old(program);old.start();Check(bool(runtime.BindScriptContextActor1C0FC(old)),"old context captures real actor");
    Check(bool(runtime.ClearContextActor1A1F4())&&!runtime.RequestPendingScene12(old)&&!runtime.Snapshot().manager_switch.pending,"retirement is stale nonnull, not promoted to nil");
    Vm nil(program);nil.start();Check(bool(runtime.BindScriptContextActor1C0FC(nil)),"later context copies real cleared slot");
    Check(bool(create())&&bool(runtime.RequestPendingScene12(nil))&&runtime.Snapshot().context_actor.moves==1&&runtime.Snapshot().context_actor.hides==1,"copied nil remains nil after a later global-slot constructor; does not touch new actor");
    initialize();Check(bool(create()),"drift actor epoch");Vm drift(program);drift.start();Check(bool(runtime.BindScriptContextActor1C0FC(drift)),"drift context owner");
    params->b[0]^=1;Check(!runtime.RequestPendingScene12(drift)&&!runtime.Snapshot().context_actor.current&&!runtime.Snapshot().manager_switch.pending,"held source constant drift refused before pending effects");params->b[0]^=1;
    drift.start();Check(!runtime.RequestPendingScene12(drift)&&bool(runtime.BindScriptContextActor1C0FC(drift))&&bool(runtime.RequestPendingScene12(drift)),"module restart needs a new real constructor association");
    initialize();Check(bool(runtime.CreateOrdinaryContextActor5()),"ordinary source actor5 model initializer");
    Vm same0(program);same0.start();Check(bool(runtime.BindScriptContextActor1C0FC(same0)),"context is created after ordinary actor5 constructor");
    const auto ordinary=runtime.Snapshot();Check(ordinary.context_actor.presence==GameContextActorPresence::Present&&!ordinary.context_actor.model_visible&&ordinary.context_actor.logical[0]==-2000,"actual ordinary model owner, hidden finite logical subset");
    Check(bool(runtime.RequestSameManager0(same0))&&runtime.Snapshot().context_actor.moves==ordinary.context_actor.moves+1
        &&runtime.Snapshot().context_actor.hides==ordinary.context_actor.hides+1&&runtime.Snapshot().manager_switch.requested18==0
        &&bool(runtime.ConsumeSameManager0()),"same0 uses its captured actor5 too, then actual equal-selector consume");
    Check(!runtime.RequestSameManager0(foreign),"unbound unrelated context cannot use same0 shortcut");
    initialize();resources.fail=true;Check(!create()&&runtime.Snapshot().context_actor.presence==GameContextActorPresence::Unknown,"failed actual model load does not publish actor or known absence");resources.fail=false;
    auto short_params=std::make_shared<Lease>(std::vector<uint8_t>(20));Check(!runtime.CreateContextActor5(resources,view(path),view(prefix),view(short_params))&&bool(create()),"malformed constructor input refuses, fresh real retry works");
    Vm retired(program);retired.start();Check(bool(runtime.BindScriptContextActor1C0FC(retired)),"retirement bound owner");runtime.Close();Check(!runtime.RequestPendingScene12(retired)&&!runtime.Snapshot().context_actor.current,"retired manager cannot consume an old context binding");
    // Invalid synthetic request angle reaches the native finite-FP refusal
    // after source pending and XYZ stores; those completed effects must stay.
    std::vector<uint8_t> overflow;for(float v:{40.f,0.f,90.f,(std::numeric_limits<float>::max)(),1.f,(std::numeric_limits<float>::max)()})F(overflow,v);
    params->b=overflow;initialize();Check(bool(create()),"finite initial angle with later malformed request angle");
    retired.start();Check(bool(runtime.BindScriptContextActor1C0FC(retired))&&!runtime.RequestPendingScene12(retired),"partial request fails at consumed nonfinite angle");s=runtime.Snapshot();
    Check(s.manager_switch.pending&&s.manager_switch.requested18==12&&s.manager_switch.current14==0&&s.context_actor.logical==std::array<float,4>{{90,0,0,0}}
        &&s.context_actor.hides==1&&s.context_actor.moves==1,"failed request preserves pending/XYZ prefix without publishing hide/adoption/completion");runtime.Close();
}
void RoomModelLifecycle(){
    //Synthetic DVS through the actual resource/asset/clone owner,not a fake slot.
    struct RoomSource final:GameResourceSource{
        bool missing=false;uint32_t calls=0;std::vector<std::weak_ptr<Lease>> leases;
        GameFsStatus OpenResource(const std::string& path,PackageView& out)override{
            ++calls;if(missing)return {GameFsCode::NotFound,0,"selected room fixture absent"};
            Check(path=="BG_ROOM\\Wallpaper0\\model01.DVS","source-selected static resource path");
            auto lease=std::make_shared<Lease>(Fixture());leases.push_back(lease);out=PackageStorage::Attach(lease,lease->b.size());return {};
        }
    } resources;
    auto selection=std::make_shared<Lease>(Fixture());GameTransition transition;
    Check(transition.Initialize({{0,0}}),"room slot owner transition");GameActorRuntime room;
    Check(bool(room.Initialize(0,PackageStorage::Attach(selection,selection->b.size()),transition,&resources)),"room slot source owner");
    {GameGraphics boot;uint32_t overlays=0;
        Check(bool(room.Draw(boot,[&](GameGraphics&){++overlays;return GameFsStatus{};}))&&overlays==0&&room.Snapshot().model_frames==0,
              "bootstrap before first room/camera has no admitted frame to publish");}
    int32_t a=-1,b=-1;
    Check(bool(room.CreateModel(170,0,0,-5,a))&&a==0&&bool(room.CreateModel(170,0,0,1,b))&&b==1,"real two-clone slot admission with stored spacing");
    auto s=room.Snapshot();Check(s.model_slots==2&&s.model_publications==2&&s.room_models[0].source_x==-5&&s.room_models[0].native_root[12]==-5,"native unparented controller retains actual source spacing");
    const auto requests=resources.calls;
    int32_t absent=-1;Check(!room.CreateModel(170,0,6,0,absent)&&absent==-1&&resources.calls==requests,"missing scene owner refuses before asset acquisition");
    Check(bool(room.UpdateModel(a,0,7))&&resources.calls==requests&&room.Snapshot().model_updates==1
          &&room.Snapshot().model_slots==2&&room.Snapshot().room_models[0].resource==170,"GAME33 updates only controller fields without reload/count change");
    Check(!room.UpdateModel(a,1,0)&&room.Snapshot().room_models[0].source_x==7,"missing authored scene owner transactional refusal");
    Check(bool(room.RetireModel(a))&&room.Snapshot().room_models[0].retirement==1&&!resources.leases[0].expired(),"retire request retains clone and borrowed source");
    {const auto r=room.Snapshot();const auto& w=r.model_retirement_history.back();Check(w.request==1&&w.slot==a&&w.resource==170&&w.generation==r.room_models[0].generation
       &&w.requested_phase==r.room_owner_phases&&!w.disabled_phase&&!w.destroyed_phase&&!w.asset_owner_expired,"request witness cannot precredit disable/destruction");}
    Check(bool(room.AdvanceActorFrame())&&room.Snapshot().room_models[0].retirement==2&&room.Snapshot().model_slots==2
          &&!resources.leases[0].expired(),"first source owner phase disables but retains");
    {const auto r=room.Snapshot();const auto& w=r.model_retirement_history.back();Check(w.disabled_phase==w.requested_phase+1&&!w.destroyed_phase&&w.assets_retained_at_disable
       &&w.clone_retained_at_disable&&w.nodes_at_disable>0&&!w.asset_owner_expired&&!resources.leases[0].expired(),"natural disable retains actual clone asset and borrowed lease");}
    Check(bool(room.RetireModel(a))&&room.Snapshot().room_models[0].retirement==1,"repeated source request restarts retirement");
    Check(bool(room.AdvanceActorFrame())&&!resources.leases[0].expired()&&bool(room.AdvanceActorFrame()),"two natural phases after repeated request");
    s=room.Snapshot();Check(!s.room_models[0].active&&s.model_slots==1&&s.model_retirements==1&&resources.leases[0].expired()
          &&!resources.leases[1].expired(),"only selected model source retired after deferred destruction");
    {const auto& w=s.model_retirement_history.back();const auto& first=s.model_retirement_history.front();Check(w.request==2&&w.requested_phase==1&&w.disabled_phase==2&&w.destroyed_phase==3
       &&w.asset_owner_expired&&w.clone_owner_cleared&&!first.destroyed_phase,"repeated request has distinct history;only newest request destroys the exact asset owner");}
    const auto old=s.room_models[0].generation;int32_t reused=-1;
    resources.missing=true;Check(!room.CreateModel(170,0,0,2,reused)&&reused==-1&&room.Snapshot().model_slots==1,"missing resource never publishes recycled slot");
    resources.missing=false;Check(bool(room.CreateModel(170,0,0,2,reused))&&reused==0&&room.Snapshot().room_models[0].generation>old,"first inactive slot has fresh native generation");
    for(unsigned i=0;i<20;++i)Check(bool(room.RetireModel(b)),"bounded passive history repeated source request");
    {const auto r=room.Snapshot();Check(r.model_retirement_history.size()==16&&r.model_retirement_history_dropped==6&&r.model_slots==2&&!resources.leases[1].expired(),"history overflow drops evidence only,never retires a source owner");}
    Check(bool(room.RetireModel(b))&&bool(room.RetireModel(reused))&&bool(room.AdvanceActorFrame())&&bool(room.AdvanceActorFrame())
          &&room.Snapshot().model_slots==0,"final room slots retire naturally");
    GameGraphics graphics;GameGraphicsConfig config;config.width=config.height=config.stride=32;config.scissor_right=config.scissor_bottom=32;
    Check(graphics.Initialize(config)&&bool(room.BindCamera(1)),"empty room still has live renderer/camera");uint32_t overlays=0;
    Check(bool(room.Draw(graphics,[&](GameGraphics& g){++overlays;return g.Clear(0xFF123456)?GameFsStatus{}:GameFsStatus{GameFsCode::Unavailable,0,"overlay clear"};})),"zero room slots still present owned overlay");
    GameGraphicsFrame frame;Check(overlays==1&&graphics.CopyFront(frame)&&frame.pixels[0]==0xFF123456&&room.Snapshot().model_frames==1,"empty-room frame is cleared/swapped,not stale");
    graphics.Close();
    room.Close();Check(resources.leases[1].expired()&&resources.leases[2].expired()&&!room.UpdateModel(0,0,0)&&!room.RetireModel(0),"close releases every clone/source and refuses late operation");
    transition.Retire();
}
struct ActorSource final:GameResourceSource {
    bool fail_motion=false;
    bool scene_assets=false,season_missing=false,camera_triangle=false;
    std::vector<std::string> requests;
    GameFsStatus OpenResource(const std::string& path,PackageView& out) override {
        requests.push_back(path);std::vector<uint8_t> bytes;
        if(season_missing&&path.find("BG_ROOM\\BG_ROOM\\light")==0)return {GameFsCode::NotFound,0,"Explicit missing optional season fixture"};
        if(path.find(".ASC")!=std::string::npos){
            W(bytes,0x00435341);W(bytes,16);W(bytes,124);W(bytes,0);
            for(unsigned i=0;i<=176;++i){
                std::vector<uint8_t> payload;
                if(i==0){for(unsigned n=0;n<124;++n){const auto name="m"+std::to_string(n);payload.insert(payload.end(),name.begin(),name.end());payload.push_back(0);}}
                else if(i==153){for(uint32_t word:{0x8030u,5u,0u,0x8030u,11u,15u,0x8030u,2u,90u,
                    0x8040u,1u,0x8010u,119u,8u,0x8030u,8u,1u,0x8010u,120u,8u,0x8040u,0u,0x8031u,0x8000u})W(payload,word);}
                else if(i==157){for(uint32_t word:{0x8030u,5u,0u,0x8030u,0u,1u,0x8030u,4u,16u,0x8031u,0x8000u})W(payload,word);}
                else if(i==176){for(uint32_t word:{0x8040u,1u,0x8030u,3u,9u,0x8030u,5u,0u,0x8030u,0u,1u,
                    0x8030u,2u,90u,0x8030u,10u,3u,0x8010u,123u,4u,0x8031u,0x8000u})W(payload,word);}
                else {W(payload,0x8010);W(payload,8);W(payload,8);}
                W(bytes,0);W(bytes,16);W(bytes,uint32_t(payload.size()));W(bytes,0);bytes.insert(bytes.end(),payload.begin(),payload.end());
            }
        }else if(path.find(".DVS")!=std::string::npos){
            const std::vector<std::array<int16_t,3>> triangle{{{-20000,0,0}},{{20000,0,0}},{{0,0,30000}}};
            bytes=camera_triangle?Fixture(false,&triangle,false,1200):Fixture();
        }
        else if(path=="DATA\\SEQ\\sequence_coord.bin"){bytes.resize(136);Word(bytes,72,1830);Word(bytes,76,uint32_t(-27));Word(bytes,120,1820);Word(bytes,124,uint32_t(-627));Word(bytes,128,600);Word(bytes,132,uint32_t(-300));}
        else if(scene_assets&&path.find("BG_ROOM\\BG_ROOM\\") == 0){bytes=MotionFixture();}
        else if(path.find("m8.DAK")!=std::string::npos){
            if(fail_motion)return {GameFsCode::MissingFile,0,"synthetic actual motion8 failure"};
            bytes=MotionFixture();Word(bytes,12,1);
        }else if(path.find("m123.DAK")!=std::string::npos||path.find("m119.DAK")!=std::string::npos||path.find("m120.DAK")!=std::string::npos){bytes=MotionFixture();
        }else return {GameFsCode::NotFound,0,"Explicit missing synthetic scene owner"};
        auto lease=std::make_shared<Lease>(std::move(bytes));out=PackageStorage::Attach(lease,lease->b.size());return {};
    }
};
void CameraTargetRender(){
    auto lease=std::make_shared<Lease>(std::vector<uint8_t>(16));ActorSource source;source.camera_triangle=true;
    GameTransition fade;Check(fade.Initialize({{0,0}}),"camera effect actual transition");fade.Open(true);
    GameActorRuntime runtime;Check(bool(runtime.Initialize(0,PackageStorage::Attach(lease,16),fade,&source))&&bool(runtime.BindCamera(1)),"current primary camera40 owner");
    int32_t slot=-1;Check(bool(runtime.CreateModel(170,0,0,0,slot))&&bool(runtime.RequestCamera(1))&&bool(runtime.AdvanceActorFrame()),"real camera request consumes current typed primary point");
    GameGraphics g;Check(g.Initialize({})&&bool(runtime.Draw(g)),"production render before7980");GameGraphicsFrame before,after;Check(g.CopyFront(before),"owned before frame");
    const auto old=runtime.Snapshot();Check(old.camera_fov_degrees==30&&old.camera_target_current&&old.camera_target_position==GameVector4{{0,0,600,1}}
        &&bool(runtime.RefreshCamera7980())&&runtime.Snapshot().camera_fov_degrees==18,"7980 modes1/2 read source controlY0, not selected screenshot/render root");
    Check(bool(runtime.Draw(g))&&g.CopyFront(after)&&before.pixels!=after.pixels&&runtime.Snapshot().model_pixel_writes>0,"same posed owned model actually consumes new FOV in raster");
    Check(runtime.Snapshot().camera_ratio==old.camera_ratio&&runtime.Snapshot().camera_completions==old.camera_completions,"7980 is not an interpolation/job completion");
    runtime.Close();Check(!runtime.Snapshot().camera_target_current&&!runtime.RefreshCamera7980(),"retired current camera control refuses");g.Close();
}

void ExecutedDestinationPreparationTests(const std::function<void(const char*,const GameGraphicsFrame&)>& capture={}){
  static_assert(!std::is_copy_constructible<GameOutingData>::value,"selected DATA packet is not an editable copied receipt");
  for(unsigned failure=0;failure<3;++failure){
    const auto bytes=SyntheticOutingData();const auto hash=dokodemo::core::sha256(bytes.data(),bytes.size());GameMainSceneData data;
    auto data_io=std::make_shared<BackgroundFileState>();Check(bool(data.Load(std::make_unique<BackgroundFile>(bytes,data_io),hash)),"vertical current held DATA");
    std::shared_ptr<const GameOutingData> packet;Check(bool(data.Outing12(packet)),"vertical selected packet");
    std::vector<uint8_t> memory(4*1024*1024);GameArena arena;GameVmBoot module_owner;
    Check(arena.Initialize(memory.data(),uint32_t(memory.size()))&&module_owner.Initialize74EF4(arena)&&module_owner.InitializeManager93404(),"vertical ordinary module constructors");
    auto content=std::make_unique<GameContentBoot>();Check(content->InitializeBB8(arena,module_owner)&&content->Reset24994(),"vertical actual Content/LoadMan constructors");
    RngNativeOwner rng;rng.ImportCurrentState64(81,3,"synthetic executed-destination component");content->SetTitleRng(rng);
    Check(bool(content->SetBackgroundRuntime([]{return std::array<uint32_t,7>{{0,0,0,12,3,5,2026}};})),"vertical same clock/RNG initialized producer");
    auto audio_io=std::make_shared<BackgroundFileState>();std::weak_ptr<GameManagerSwitchControl> observed_manager;bool observed_store=false;
    Check(bool(content->BindInitialSceneBgm(data.Lists(),data.Gains(),data.Root(),[&](uint32_t i){return data.Name(i);},
      [&](const std::string&,std::unique_ptr<GameFileSource>& file)->GameFsStatus{
        if(const auto m=observed_manager.lock()){Check(m->Snapshot().loadman.stored_bgm4BC==content->SceneBgm().stored4BC,
          "real4BC write is visible to file factory BEFORE request effects, not at final preparation readback");observed_store=true;}
        file=std::make_unique<BackgroundFile>(std::vector<uint8_t>(8,4),audio_io);return {};}))
      &&bool(content->BindInitialScenePlacement(data)),"vertical ordinary initialization DATA before actor");
    unsigned factories=0;Check(bool(content->BindDestinationData(packet,[&](){++factories;return std::unique_ptr<GameFileSource>{};})),"same DATA/fresh factory binder, not selection");
    ActorSource resources;resources.scene_assets=true;resources.camera_triangle=true;
    auto& transition=dokodemo::StartupRuntimeTestAccess::SceneUi(*content).TitleTransitionOwner();Check(transition.Initialize({{0,0}}),"vertical SAME UI bootstrap radial constructor");
    Check(dokodemo::StartupRuntimeTestAccess::RequestApplicationFlag(*content,3,0x101)&&dokodemo::StartupRuntimeTestAccess::CommitApplication(*content),"actual a2 width/order component caller");
    {const auto l=content->Snapshot().ordinary_scene_owners.loadman;Check(l.application_flag_known&&l.application_flagEA0==1
      &&l.application_request_order==std::array<uint32_t,3>{{0x19AB0,0x19AB4,0x19ABC}}&&l.load_phase2C==0&&!l.change40,"19AA8 truncates byte and does NOT create load change40");}
    Check(dokodemo::StartupRuntimeTestAccess::RequestApplication(*content,4)&&dokodemo::StartupRuntimeTestAccess::CommitApplication(*content),"actual module initialization current4 history");
    auto selection=std::make_shared<Lease>(std::vector<uint8_t>(16));Check(bool(content->InitializeActorSceneOwners(0,PackageStorage::Attach(selection,16),resources,transition)),"vertical actual ordinary initial model/actor5 owner");
    {const auto l=content->Snapshot().ordinary_scene_owners.loadman;Check(l.application20==6&&l.initial_scene6_handoff&&l.initial_state188==1
      &&l.initial_store188_pc==0x183FC&&l.application_flag_known&&!l.application_flagEA0&&!l.change40
      &&content->Snapshot().current_scene==4,"actual19DD4 ->183FC store->19AA8(6,0), separate Windows host room projection");}
    auto moving_bytes=std::make_shared<Lease>(GlyphTestGim(0xFFFFFFFF));auto moving_image=std::make_shared<GameImage>();
    Check(bool(moving_image->DecodeGim(PackageStorage::Attach(moving_bytes,moving_bytes->b.size())))
      &&dokodemo::StartupRuntimeTestAccess::Moving(*content,moving_image),"actual synthetic GIM/moving constructor under SAME component module");
    auto& actor=dokodemo::StartupRuntimeTestAccess::Actor(*content);auto& bgm=dokodemo::StartupRuntimeTestAccess::Bgm(*content);const auto control=dokodemo::StartupRuntimeTestAccess::Manager(*content);observed_manager=control;
    std::vector<uint8_t> code;auto emit=[&](std::initializer_list<uint8_t> b){code.insert(code.end(),b);};
    auto slot=[&](uint8_t op,uint16_t i){emit({op,uint8_t(i),uint8_t(i>>8)});};
    auto call=[&](uint8_t n){const auto pc=uint32_t(code.size());emit({0x20,n,0,0,0});return pc;};
    emit({0x30,24});for(unsigned i=0;i<8;++i){slot(0x99,uint16_t(17+i));emit({0x8E,'A',0,0x51});}slot(0x99,16);emit({0x8B,0x70,1});
    emit({0x4E,13,0,6,0});slot(0x91,13);emit({0x80,1,0x84});GameOutingCommandAbi abi;abi.root=0;abi.candidates=abi.stored=0;abi.produce_call=call(3);
    slot(0x91,13);slot(0x99,0);abi.copy_call=call(2);emit({0x80,12,0x80,1});abi.travel_call=call(2);emit({0x31});
    abi.producer=uint32_t(code.size());emit({0x30,3});for(unsigned i=0;i<6;++i){slot(0x90,0);emit({0x80,uint8_t(i),0xF5});
      if(i==0)slot(0x90,1);else if(i==1){slot(0x95,0);slot(0x90,1);emit({0xF4});}else if(i==2)emit({0x80,7});else if(i==3||i==4)emit({0x8E,'T',0});else emit({0x84});emit({0x51});}emit({0x87,0x70,0x31});
    abi.copier=uint32_t(code.size());emit({0x30,2});slot(0x90,1);slot(0x90,0);emit({0x57,0x31});abi.travel=uint32_t(code.size());emit({0x30,2});slot(0x90,0);abi.game12=uint32_t(code.size());emit({0x8B,0x7E,1,0x31});
    const auto patch=[&](uint32_t at,uint32_t target){for(unsigned i=0;i<3;++i)code[at+2+i]=uint8_t(target>>(8*i));};patch(abi.produce_call,abi.producer);patch(abi.copy_call,abi.copier);patch(abi.travel_call,abi.travel);
    std::vector<uint8_t> ksc(28+code.size()+4*(25+13));Word(ksc,0,0x0043534B);Word(ksc,4,0x20030204);Word(ksc,12,uint32_t(code.size()));ksc[24]=13;ksc[26]=25;std::copy(code.begin(),code.end(),ksc.begin()+28);
    const auto ctx_at=uint32_t(28+code.size());Word(ksc,ctx_at,0x90000006);Word(ksc,ctx_at+16*4,0x90000008);
    const auto global_at=ctx_at+25*4;Word(ksc,global_at,0x90000002);Word(ksc,global_at+8,0xB0000003);Word(ksc,global_at+12,0x90000009);Word(ksc,global_at+16,0x2000000C);
    script::Program program;script::Diagnostic error;Check(program.load_test_fixture(ksc,error),error.text().c_str());script::Vm vm(program);vm.native_reserved_local_nil(true);vm.start();
    Check(bool(actor.BindScriptContextActor1C0FC(vm)),"actual constructor context captures this actor5 owner");auto image=std::make_shared<Lease>(ksc);const auto image_view=PackageStorage::Attach(image,image->b.size());
    GameOutingCommand command;Check(bool(command.Begin(vm,abi)),"vertical command actual VM root");vm.frame_observer([&](const script::FrameTransition& t){command.Observe(vm,t);});
    auto target_io=std::make_shared<BackgroundFileState>();bool consumed=false;
    vm.host_service([&](uint8_t id,const std::vector<script::Value>& args){
      if(id==0x70){Check(args.size()==1&&bool(actor.RegisterSupportData16(vm,args[0])),"actual eight typed current texts producer registration");return script::Value::integer(0);}
      Check(id==0x7E&&args.size()==1&&bool(command.Issue(vm,actor,args[0].signed_integer())),"executed producer/copy/operand issues same real context request");
      Check(!command.Issue(vm,actor,12)&&bool(actor.PublishPendingScene1B238(bgm)),"no issue replay; actual source request publication");
      GameMainSceneData foreign_data;auto foreign_io=std::make_shared<BackgroundFileState>();std::shared_ptr<const GameOutingData> foreign_packet;
      Check(bool(foreign_data.Load(std::make_unique<BackgroundFile>(bytes,foreign_io),hash))&&bool(foreign_data.Outing12(foreign_packet)),"independent live identical DATA is not this BGM owner");
      const auto seed_before_foreign=rng.CurrentSeed64();auto refused_io=std::make_shared<BackgroundFileState>();
      Check(!actor.PreparePendingDestination(vm,command,image_view,foreign_packet,bgm,std::make_unique<BackgroundFile>(std::vector<uint8_t>(4),refused_io))
        &&!actor.DestinationPreparation().attempted&&rng.CurrentSeed64()==seed_before_foreign&&!refused_io->live&&!refused_io->reads,
        "foreign identical DATA refuses before job/file/selection/RNG effects");Check(bool(foreign_data.Close()),"foreign packet retirement");
      Check(bool(actor.StartTransition()),"real GAME6 radial owner");
      for(unsigned i=0;i<100&&transition.Snapshot().state==1;++i)transition.Update();
      Check(transition.Snapshot().state==2,"real radial open update, no prefilled state");
      const auto before_dispatch=rng.CurrentSeed64();auto& ui=dokodemo::StartupRuntimeTestAccess::SceneUi(*content);
      const auto old_scene=dokodemo::StartupRuntimeTestAccess::OldScene(actor);
      GameSceneUiBootstrap foreign_ui;auto foreign_ui_module=std::make_shared<int>(1);
      Check(foreign_ui.TitleTransitionOwner().Initialize({{0,0}})&&dokodemo::StartupRuntimeTestAccess::ForeignMoving(foreign_ui,foreign_ui_module,moving_image),"independent LIVE foreign UI owners");
      Check(!actor.AdvanceDestinationLoad(vm,command,foreign_ui,bgm)&&control->Snapshot().loadman.load_phase2C==0
        &&!control->Snapshot().loadman.change40&&foreign_ui.Snapshot().moving.state==3&&rng.CurrentSeed64()==before_dispatch,
        "current request rejects independent LIVE UI before history/moving/RNG effects");
      if(failure==2){
        transition.Retire();const auto seed=rng.CurrentSeed64();
        Check(!actor.AdvanceDestinationLoad(vm,command,ui,bgm)&&control->Snapshot().loadman.load_phase2C==0
          &&!control->Snapshot().loadman.change40&&rng.CurrentSeed64()==seed&&!old_scene.expired(),"retired radial refuses before request history/RNG/old-owner mutations");
        consumed=true;return script::Value::integer(0);
      }
      if(capture&&!failure){
        Check(bool(actor.BindCamera(vm,1)),"component initial camera binds actual current primary");
        GameGraphics initial;GameGraphicsFrame frame;Check(initial.Initialize({})&&bool(actor.Draw(initial))&&initial.CopyFront(frame),"actual old-scene Runtime.Draw before transition");
        capture("synthetic-initial",frame);initial.Close();
      }
      Check(bool(actor.FinishTransitionGame7()),"real GAME7 same owner");
      Check(bool(actor.AdvanceDestinationLoad(vm,command,ui,bgm))&&control->Snapshot().loadman.load_phase2C==1
        &&control->Snapshot().loadman.change40&&control->Snapshot().loadman.change_writer==0x17E78,"actual17C98 loaded old31+cleared request ->byte40, not application20 inference");
      for(unsigned i=0;i<100&&control->Snapshot().loadman.phase28!=4;++i){
        transition.Update();const auto step=actor.AdvanceDestinationLoad(vm,command,ui,bgm);if(!step)throw std::runtime_error(step.detail);
      }
      const auto retired=control->Snapshot().loadman;
      Check(retired.phase28==4&&!retired.load_phase2C&&retired.change40&&retired.source_selector4DC==-1
        &&retired.rendered_selector4DC==0&&!old_scene.expired()&&retired.game_interval2C==20000u
        &&retired.render_state20==0&&retired.render_state21==0&&bgm.Snapshot().source_scene_retired
        &&bgm.Snapshot().selection_writer==0x19F94&&retired.stored_bgm4BC==0&&rng.CurrentSeed64()==before_dispatch
        &&ui.Snapshot().moving.current,"actual source retire/4BC=0 BEFORE target selection; old native render allocation retained, no extra RNG or fake voice");
      std::vector<std::pair<std::string,std::vector<uint8_t>>> packages;
      auto marker_model=AreaMarkerFixture(0,false,1000);const auto marker=dokodemo::model::parseDvs(marker_model);
      const auto end=marker.nodes[0].source.offset+marker.nodes[0].source.bytes;
      marker_model[end-12]=1;marker_model[end-11]=0;marker_model[end-10]=1;marker_model[end-9]=0;marker_model[end-8]=1;marker_model[end-7]=0;
      for(unsigned i=0;i<3;++i){const auto name="SYN_"+std::to_string(i);packages.push_back({name+".BPM",BackgroundArchive({{name+"\\model01.DVS",marker_model},
        {name+"\\model02.DVS",Fixture(false,nullptr,false,1000)},{name+"\\motion",MotionFixture()}})});}
      if(!failure){packages.push_back({"TEXT.BPM",BackgroundArchive({{"panels\\panel",GlyphTestModel(1)},
        {"panels\\CONTACT_TEST.GIM",GlyphTestGim(0xFFFFFFFF)}})});
        packages.push_back({"F000.BPM",BackgroundArchive({{"F000\\F00120.GIM",GlyphTestGim(0xFF0000FF)}})});}
      const auto before=rng.CurrentSeed64();auto status=actor.PreparePendingDestination(vm,command,image_view,packet,bgm,std::make_unique<BackgroundFile>(BackgroundArchive(packages),target_io));
      const auto state=actor.DestinationPreparation();
      if(bool(status)!=(failure==0))throw std::runtime_error("vertical stage: "+status.detail);
      Check(state.attempted&&state.prepared==(failure==0)&&state.failed==(failure!=0)&&state.candidate==1&&state.request==control->Snapshot().requests
        &&state.epoch==dokodemo::StartupRuntimeTestAccess::Scene(actor).epoch&&state.phase==(failure?8u:9u)&&rng.CurrentSeed64()!=before,"real current request stages actual selected job/aux/RNG/support, failure preserves effects");
      Check(control->Snapshot().loadman.stored_bgm4BC==bgm.Snapshot().stored4BC&&control->Snapshot().loadman.rendered_selector4DC==0
        &&control->Snapshot().current14==12&&vm.callbacks_posted()==0&&observed_store,"actual4BC source store, old render retained, no fabricated adoption/wake");
      const auto after=rng.CurrentSeed64();auto replay_io=std::make_shared<BackgroundFileState>();
      Check(!actor.PreparePendingDestination(vm,command,image_view,packet,bgm,std::make_unique<BackgroundFile>(BackgroundArchive(packages),replay_io))
        &&rng.CurrentSeed64()==after&&!replay_io->live&&replay_io->reads==0,"attempt cannot replay RNG/selection/open");
      if(!failure){
        Check(dokodemo::StartupRuntimeTestAccess::TargetSourceOrder(actor)&&dokodemo::StartupRuntimeTestAccess::TargetGlyphs(actor,vm),
          "real cleared4BC ->BGM before auxiliary RNG; actual same-marker support/glyph owned pixels bound from current eight texts");
        script::Vm foreign_vm(program);foreign_vm.start();const auto primary_writes=dokodemo::StartupRuntimeTestAccess::Scene(actor).placement_writes;
        Check(!actor.AdoptPreparedDestination(foreign_vm,command)&&dokodemo::StartupRuntimeTestAccess::Scene(actor).placement_writes==primary_writes
          &&dokodemo::StartupRuntimeTestAccess::ForeignAdoptionRefuses(actor,vm,command),"foreign run/context and same-address foreign registration CB refuse BEFORE placement/regions");
        Check(bool(actor.AdoptPreparedDestination(vm,command)),"executed command ->same prepared target adoption");
        const auto adopted=actor.DestinationPreparation();const auto l=control->Snapshot().loadman;
        const auto primary=dokodemo::StartupRuntimeTestAccess::Scene(actor).placement;
        std::array<float,4> expected_placement{};Check(bool(data.Placement(12,expected_placement)),"selected held row readback");
        Check(adopted.adopted&&adopted.placement_pc==0x19E94&&adopted.regions_pc==0x19EA0&&adopted.adoption_pc==0x185FC
          &&l.rendered_selector4DC==12&&l.source_selector4DC==12&&l.current_variant4A8==l.staged_variant4AC&&l.scene_phase44==1
          &&l.render_state20==1&&l.render_state21==1&&old_scene.expired()&&vm.callbacks_posted()==0
          &&primary==expected_placement,"real target placement/control vs primary40, BD28 before26B80, old owner retired, NO callback/wake");
        Check(!actor.AdoptPreparedDestination(vm,command)&&rng.CurrentSeed64()==after,"adoption replay refuses without RNG/placement");
        Check(bool(actor.BindCamera(vm,1)),"actual scoped context/primary camera bind");
        GameGraphics graphics;Check(graphics.Initialize({}),"adopted production renderer");
        Check(!actor.Draw(graphics),"adopted support cannot borrow an absent/latest VM");
        const auto samples=dokodemo::StartupRuntimeTestAccess::TargetSamples(actor);
        Check(!actor.AdvanceActorFrame(&foreign_vm)&&dokodemo::StartupRuntimeTestAccess::TargetSamples(actor)==samples,
          "foreign scoped VM refuses before target sampling/pose effects");
        const auto drawn=actor.Draw(graphics,{},&vm);if(!drawn)throw std::runtime_error(drawn.detail);
        Check(graphics.Snapshot().mesh_pixels>0&&dokodemo::StartupRuntimeTestAccess::Scene(actor).frames>0&&dokodemo::StartupRuntimeTestAccess::Scene(actor).regions,
          "actual target/main/support/selected-child composition consumed by Runtime.Draw, no Scene0 copy");
        if(capture){GameGraphicsFrame frame;Check(graphics.CopyFront(frame),"actual adopted target frame");capture("synthetic-adopted-transition-pending",frame);}
        // Explicit COMPONENT inspection request. This is NOT the unimplemented
        // source18568 continuation/event17 and must never grant ordinary input.
        Check(bool(actor.StartTransition()),"component-only GAME6 opens adopted composition for inspection");
        for(unsigned i=0;i<100&&transition.Snapshot().state==1;++i)transition.Update();
        Check(bool(actor.Draw(graphics,{},&vm))&&control->Snapshot().loadman.scene_phase44==1&&vm.callbacks_posted()==0,
          "component inspection changes only real radial pixels, not source phase/wake");
        if(capture){GameGraphicsFrame frame;Check(graphics.CopyFront(frame),"component inspection owned frame");capture("synthetic-inspection-extra-GAME6",frame);}
        Check(bool(actor.AdvanceActorFrame(&vm)),"actual target controls/support sampling after adoption");
        graphics.Close();
      }else Check(!actor.AdoptPreparedDestination(vm,command)&&!old_scene.expired()&&control->Snapshot().loadman.rendered_selector4DC==0,
        "missing target support retains old render owner; source4DC/4A8 partial publications are not adoption");
      target_io->fail_close=true;Check(!actor.CloseDestination()&&target_io->live,"failed close retains actual owned target for retry");target_io->fail_close=false;
      Check(bool(actor.CloseDestination())&&!target_io->live&&control->Snapshot().loadman.rendered_selector4DC==(failure?0:-1),"target retirement preserves actual current owner domain, not arrival");
      auto retired_replay=std::make_shared<BackgroundFileState>();const auto retired_seed=rng.CurrentSeed64();
      Check(!actor.PreparePendingDestination(vm,command,image_view,packet,bgm,std::make_unique<BackgroundFile>(BackgroundArchive(packages),retired_replay))
        &&!actor.DestinationPreparation().attempted&&!retired_replay->reads&&!retired_replay->live&&rng.CurrentSeed64()==retired_seed
        &&control->Snapshot().loadman.source_selector4DC==-1,"closed attempt cannot replay same request/source publication or target RNG");
      consumed=true;return script::Value::integer(0);
    });
    const auto result=vm.run(4096);if(result.status!=script::Status::Complete)throw std::runtime_error(result.diagnostic.text());
    Check(consumed&&factories==0,"component uses explicit synthetic file, ordinary bound factory has no unearned execution");
    const auto last_store=control->Snapshot().loadman.stored_bgm4BC;
    Check(bool(bgm.Close())&&control->Snapshot().loadman.stored_bgm4BC==last_store,"manager keeps actual last4BC after BGM/file owner retirement");
    command.Close();Check(content->Close()&&module_owner.Close()&&bool(data.Close()),"vertical target/script/current module/data cleanup");transition.Retire();
  }
}

void OrdinaryContentDataTests(bool failed_clock=false){
    const auto bytes=SyntheticMainData();const auto hash=dokodemo::core::sha256(bytes.data(),bytes.size());GameMainSceneData data,foreign;auto data_io=std::make_shared<BackgroundFileState>(),foreign_io=std::make_shared<BackgroundFileState>();
    Check(bool(data.Load(std::make_unique<BackgroundFile>(bytes,data_io),hash))&&bool(foreign.Load(std::make_unique<BackgroundFile>(bytes,foreign_io),hash)),"explicit synthetic DATA native owners");
    std::vector<uint8_t> backing(4*1024*1024);GameArena arena;GameVmBoot vm;Check(arena.Initialize(backing.data(),uint32_t(backing.size()))&&vm.Initialize74EF4(arena)&&vm.InitializeManager93404(),"actual Content module parents");
    auto content=std::make_unique<GameContentBoot>();Check(content->InitializeBB8(arena,vm)&&content->Reset24994(),"ordinary Content owning constructors");
    RngNativeOwner rng;rng.ImportCurrentState64(81,3,"synthetic ordinary Content data lineage");content->SetTitleRng(rng);content->SetCalendar({{0,0,0,12,3,5,2026}});
    bool invalid_clock=false;
    Check(bool(content->SetBackgroundRuntime([&]{return std::array<uint32_t,7>{{0,0,0,invalid_clock?25u:12u,3,5,2026}};})),"current actual runtime/settings in synthetic clock domain");
    auto bgm_io=std::make_shared<BackgroundFileState>();unsigned factories=0;
    Check(!content->BindInitialScenePlacement(data)&&bool(content->BindInitialSceneBgm(data.Lists(),data.Gains(),data.Root(),[&](uint32_t i){return data.Name(i);},[&](const std::string&,std::unique_ptr<GameFileSource>& file)->GameFsStatus{++factories;file=std::make_unique<BackgroundFile>(std::vector<uint8_t>(16,7),bgm_io);return {};}))
        &&!content->BindInitialScenePlacement(foreign)&&bool(content->BindInitialScenePlacement(data))&&!content->SceneBgm().selected,"typed source DATA placement binds only SAME BGM data and BEFORE source selection/actor");
    ActorSource source;source.scene_assets=true;source.camera_triangle=true;GameTransition transition;Check(transition.Initialize({{0,0}}),"current Content transition");auto module=std::make_shared<Lease>(std::vector<uint8_t>(16));
    Check(bool(content->InitializeActorSceneOwners(0,PackageStorage::Attach(module,16),source,transition))&&factories==1&&content->SceneBgm().selected
        &&content->Snapshot().actor_script.runtime.source_placement_bound&&!content->BindInitialScenePlacement(data),"actual same production initialization body consumes data/initialBGM/placement/model before actor context");
    const auto control=dokodemo::StartupRuntimeTestAccess::Manager(*content);
    Check(dokodemo::StartupRuntimeTestAccess::RequestApplication(*content,3)
        &&dokodemo::StartupRuntimeTestAccess::CommitApplication(*content)
        &&!control->Snapshot().loadman.ui_current,
        "real native application3 dispatcher has no ordinary UI admission");
    Check(dokodemo::StartupRuntimeTestAccess::RequestApplication(*content,4)
        &&dokodemo::StartupRuntimeTestAccess::CommitApplication(*content)
        &&dokodemo::StartupRuntimeTestAccess::Application(*content)==std::array<int32_t,3>{{4,3,4}}
        &&control->Snapshot().loadman.application20==4&&control->Snapshot().loadman.ui_current,
        "real application3->4 request/commit uses SAME LoadMan history; no manually filled50/464");
    auto& actor=dokodemo::StartupRuntimeTestAccess::Actor(*content);
    const auto initialized=control->Snapshot();
    Check(initialized.bound&&initialized.epoch==actor.Snapshot().owner_epoch&&initialized.loadman.current
        &&initialized.loadman.module_epoch==actor.Snapshot().manager_switch.loadman.module_epoch
        &&initialized.loadman.transition50==0&&initialized.loadman.ui464==0,
        "Content constructor and actual actor/request path share ONE live owner, not a copied certificate");
    auto alias=std::shared_ptr<GameManagerSwitchControl>(control.get(),[](GameManagerSwitchControl*){});
    GameActorRuntime invalid;Check(!invalid.Initialize(0,PackageStorage::Attach(module,16),transition,&source,content->BackgroundRuntime().lock(),alias)
        &&!control->CurrentBorrow(alias)&&control->Snapshot().requests==0,"foreign control-block borrow refuses before constructor/resource effects");
    GameInventory foreign_inventory;Check(bool(foreign_inventory.Request({},true)),"foreign real synthetic UI request");
    Check(control->Snapshot().loadman.ui464==0,"unregistered foreign UI cannot replace current owner by shape");
    auto& inventory=dokodemo::StartupRuntimeTestAccess::Inventory(*content);
    Check(bool(inventory.Request({},true))&&control->Snapshot().loadman.ui464==0x14
        &&control->Snapshot().loadman.ui_generation==inventory.Snapshot().generation
        &&!actor.RequestSameManager0(0)&&!control->Snapshot().pending,
        "real registered current UI request blocks ordinary scene admission, no prefilled selector");
    inventory.Close();foreign_inventory.Close();Check(control->Snapshot().loadman.ui_current&&control->Snapshot().loadman.ui464==0,
        "actual owned cleanup retires the UI reference without changing half50");
    std::vector<uint8_t> test_module(31);Word(test_module,0,0x0043534B);Word(test_module,4,0x20030204);
    Word(test_module,12,3);test_module[28]=0x30;test_module[30]=0x38;
    dokodemo::script::Program program;dokodemo::script::Diagnostic error;
    Check(program.load_test_fixture(test_module,error),"synthetic current-context component program, no original bytecode");
    dokodemo::script::Vm current(program);current.start();Check(bool(actor.BindScriptContextActor1C0FC(current)),"actual current context captures ordinary actor5 owner");
    const auto prior=current.current_witness();
    Check(bool(actor.RequestSameManager0(current))&&control->Snapshot().pending
        &&control->Snapshot().requests==1&&control->Snapshot().current14==0&&control->Snapshot().requested18==0
        &&control->Snapshot().loadman.transition50==0&&bool(actor.ConsumeSameManager0()),
        "real context request/actor effects ->SAME current manager ->equal1B238; half50/464 unchanged by source");
    Check(!actor.ConsumeSameManager0()&&bool(actor.RequestPendingScene12(current))&&control->Snapshot().requested18==12
        &&control->Snapshot().current14==0&&control->Snapshot().loadman.transition50==0
        &&!actor.RequestPendingScene12(current)&&!actor.ConsumeSameManager0()
        &&current.current_witness().current_pc==prior.current_pc&&current.context_enabled(),
        "actual nonzero request prefix preserves rendered/current scene, refuses replay/adoption-as-cancel, never wakes VM");
    bool unknown_bit=true;Check(!control->ResourceRequest(31,unknown_bit)&&unknown_bit,
        "unpublished resource bits remain UNKNOWN, not host-zero source authority");
    auto& actual_bgm=dokodemo::StartupRuntimeTestAccess::Bgm(*content);GameSceneBgm unrelated_bgm;
    const auto seed_before_publication=rng.CurrentSeed64();const auto cache_before=actual_bgm.Snapshot().cache_revision;
    Check(!actor.PublishPendingScene1B238(unrelated_bgm)&&control->Snapshot().publication_phase==0
        &&!control->Snapshot().old_bit_known&&actual_bgm.Snapshot().cache_revision==cache_before,
        "foreign/missing BGM refuses before old clear/current clock/effects");
    GameGraphics kept;Check(kept.Initialize({})&&bool(actor.BindCamera(1))&&bool(actor.Draw(kept)),"actual old owned synthetic scene before pending publication");
    GameGraphicsFrame old_frame,new_frame;Check(kept.CopyFront(old_frame),"old owned frame");
    if(failed_clock){
        invalid_clock=true;const auto failed=content->TickPendingSceneRequest();const auto partial=control->Snapshot();
        Check(!failed&&partial.publication_phase==1&&partial.old_bit_known&&!partial.old_bit
            &&!partial.new_bit_known&&partial.current14==0&&partial.pending&&partial.requested18==12
            &&partial.loadman.current_variant4A8==1&&partial.loadman.rendered_selector4DC==0
            &&actual_bgm.Snapshot().cache_revision==cache_before&&rng.CurrentSeed64()==seed_before_publication,
            "real clock failure occurs AFTER old clear; preserves pending/current/rendered/unknown-new and RNG");
        invalid_clock=false;Check(!content->TickPendingSceneRequest()&&control->Snapshot().publication_phase==1
            &&actual_bgm.Snapshot().cache_revision==cache_before&&bool(actor.Draw(kept))&&kept.CopyFront(new_frame)
            &&new_frame.pixels==old_frame.pixels,"partial operation is not silently replayed/completed or old render discarded");
        kept.Close();actor.Close();Check(content->Close()&&bool(data.Close())&&bool(foreign.Close())&&vm.Close()
            &&!control->Snapshot().loadman.current,"partial owner/requests/resources cleanly retire");return;
    }
    const auto result=content->TickPendingSceneRequest();const auto published=control->Snapshot();
    Check(!result&&result.detail.find("destination DATA/file owner absent")!=std::string::npos&&published.publication_phase==3
        &&published.old_resource==31&&published.new_resource==66+published.loadman.staged_variant4AC
        &&published.old_bit_known&&!published.old_bit&&published.new_bit_known&&published.new_bit
        &&published.current14==12&&!published.pending&&published.requested18==12&&published.consumed==2
        &&published.loadman.current_variant4A8==1&&published.loadman.rendered_selector4DC==0&&published.loadman.rendered_current
        &&published.loadman.cached_period4B4==actual_bgm.Snapshot().period4B4
        &&actual_bgm.Snapshot().cache_revision==cache_before+1&&rng.CurrentSeed64()==seed_before_publication
        &&factories==1&&current.current_witness().current_pc==prior.current_pc&&current.context_enabled(),
        "real old-clear/fresh-cache/new-set/14/1C/18 publication, no target RNG/I-O/adoption or VM wake");
    Check(published.publication_order==std::array<uint32_t,6>{{0x1B284,0x1B290,0x1B29C,0x1B2A8,0x1B2AC,0x1B2B0}}
        &&bool(actor.Draw(kept))&&kept.CopyFront(new_frame)&&old_frame.pixels==new_frame.pixels,
        "source publication order and old rendered owners/pixels survive incomplete replacement");
    const auto revision=actual_bgm.Snapshot().cache_revision;
    Check(!content->TickPendingSceneRequest()&&!actor.PublishPendingScene1B238(actual_bgm)
        &&actual_bgm.Snapshot().cache_revision==revision&&control->Snapshot().publication_phase==3,
        "incomplete target never replays clock/stores or returns successful completion");kept.Close();
    const auto seed=rng.CurrentSeed64();bgm_io->missing=true;Check(bool(content->TickSceneBgmFiles())&&content->SceneBgm().file.state==1,"genuine later I/O failure stays pending and does not block source scene");bgm_io->missing=false;
    Check(bool(content->TickSceneBgmFiles())&&bool(content->TickSceneBgmFiles())&&content->SceneBgm().file.state==5&&content->SceneBgm().file.bytes==16
        &&content->SceneBgm().file.bank5C==-1&&rng.CurrentSeed64()==seed,"ordinary worker hook completes actual file bytes without RNG/playback/ready substitution");
    content->BackgroundRuntime().lock()->Close();GameFilesystem empty_fs;GameGraphics empty_graphics;
    Check(!content->TickSceneBgmFiles()&&!dokodemo::StartupRuntimeTestAccess::ActorTick(*content,empty_fs,empty_graphics)&&content->Snapshot().blocked
        &&content->Snapshot().frontier_pc==0&&content->Snapshot().status.detail.find("Native BGM worker:")==0,"unknown/retired worker is explicit native refusal, not I/O failure or invented source opcode PC");
    actor.Close();Check(control->Snapshot().live&&!control->Snapshot().bound&&!control->Snapshot().pending
        &&control->Snapshot().current14==12&&control->Snapshot().publication_phase==3&&!control->Snapshot().loadman.rendered_current
        &&control->Snapshot().loadman.current&&control->Snapshot().loadman.transition50==0,
        "actor retirement preserves module/source request history, no reconstruction/default reset");
    Check(content->Close()&&!bgm_io->live&&bool(data.Close())&&bool(foreign.Close())&&vm.Close(),"Content/BGM retire before user DATA/module");
    Check(!control->CurrentBorrow(control)&&!control->Snapshot().loadman.current&&!control->RequestSame0(initialized.epoch,0),
        "held borrower cannot consume after real Content/task/module retirement");
}
void PlacementLiveTests(){
    auto lease=std::make_shared<Lease>(std::vector<uint8_t>(16));ActorSource source;source.scene_assets=true;source.camera_triangle=true;
    GameTransition fade;Check(fade.Initialize({{0,0}}),"placement transition");fade.Open(true);RngNativeOwner rng;rng.ImportCurrentState64(7,2,"synthetic placement lineage");
    auto parent=std::make_shared<GameBackgroundRuntime>();Check(bool(parent->Initialize([]{return std::array<uint32_t,7>{{0,0,0,12,3,5,2026}};},rng)),"real native parent with explicit synthetic clock");
    GameActorRuntime actor;Check(bool(actor.Initialize(0,PackageStorage::Attach(lease,16),fade,&source,parent))&&bool(actor.InitializeScene0({{0,0,0,12,3,5,2026}},true))&&bool(actor.BindCamera(1)),"actual scene/region/primary owners before BD28");
    GameGraphics g;Check(g.Initialize({})&&bool(actor.Draw(g)),"before placement render");GameGraphicsFrame before,after;Check(g.CopyFront(before),"before placement frame");const auto point=actor.Snapshot().camera_target_position;
    std::vector<uint8_t> xyz;for(const float v:{-123.f,456.f,78.f})F(xyz,v);std::vector<uint8_t> heading;F(heading,-3.14159265358979323846f);auto row=std::make_shared<Lease>(xyz),angle=std::make_shared<Lease>(heading);
    const auto view=[](const std::shared_ptr<Lease>& v){return PackageStorage::Attach(v,v->b.size());};auto alias=std::shared_ptr<GameBackgroundRuntime>(parent.get(),[](GameBackgroundRuntime*){});
    Check(!actor.ApplyScenePlacementBD28(alias,0,view(row),view(angle))&&!actor.ApplyScenePlacementBD28(parent,12,view(row),view(angle))&&!actor.Snapshot().source_placement_bound,"foreign module and unadopted target refuse before effects");
    Check(bool(actor.ApplyScenePlacementBD28(parent,0,view(row),view(angle)))&&actor.Snapshot().source_placement20==std::array<float,4>{{-123,456,78,-3.14159265358979323846f}}
        &&actor.Snapshot().actor_root==GameVector4{{-123,456,78,1}}&&actor.Snapshot().camera_target_position==point,"BD28 logical20 XYZ/heading ->real99A8 model root; separate40 unmodified");
    bool region_result=false;Check(!actor.Snapshot().region_owners_live&&!actor.QueryActorPosition(region_result)
        &&bool(actor.CompileSceneRegions26B80())&&actor.Snapshot().region_owners_live&&!actor.CompileSceneRegions26B80(),"initial BD28 precedes real26B80; no collision/query readiness before compiled owner or silent replay");
    Check(bool(actor.Draw(g))&&g.CopyFront(after)&&before.pixels!=after.pixels&&actor.Snapshot().actor_pixel_writes>0,"production actual model/room/follow camera consumes posed primary placement");
    const auto old=actor.Snapshot();Word(row->b,0,0x7FC00000);Check(!actor.ApplyScenePlacementBD28(parent,0,view(row),view(angle))&&actor.Snapshot().source_placement_writes==old.source_placement_writes&&actor.Snapshot().actor_root==old.actor_root,"nonfinite DATA refusal preserves live pose");
    actor.Close();Check(!actor.Snapshot().source_placement_bound&&!actor.ApplyScenePlacementBD28(parent,0,view(row),view(angle)),"retired scene/primary placement refuses");parent->Close();g.Close();
}
void ActorPendingReplacement(){
    // Synthetic ASC/model owners; tests the source single pending slot,
    // not connected gameplay or additional opcode/effect coverage.
    auto lease=std::make_shared<Lease>(std::vector<uint8_t>(16));GameTransition fade;
    Check(fade.Initialize({{0,0}}),"pending actor fade");ActorSource source;GameActorRuntime actor;
    Check(bool(actor.Initialize(0,PackageStorage::Attach(lease,16),fade,&source)),"pending actor owner");
    Check(bool(actor.RequestActorEvent(176,0))&&bool(actor.RequestActorEvent(153,0)),"second positive request overwrites pending slot");
    const auto pending=actor.Snapshot();
    Check(pending.active_command==-1&&pending.requested_command==153&&pending.actor_request_pending
          &&pending.selected_extension_pending&&pending.event_requests==2&&pending.event_pending_replacements==1
          &&pending.command_pc==0&&pending.command_effects==0&&!actor.WaitReady(),"no active command or fake ready before natural owner pass");
    Check(!actor.RequestActorEvent(9999,0)&&!actor.RequestActorEvent(-1,0)&&!actor.RequestActorEvent(0,0)&&!actor.RequestActorEvent(176,1)
          &&actor.Snapshot().requested_command==153&&actor.Snapshot().event_requests==2,"bad replacement preserves accepted pending owner");
    Check(bool(actor.AdvanceActorFrame()),"natural owner consumes latest153");const auto active=actor.Snapshot();
    Check(active.active_command==153&&!active.actor_request_pending&&active.command_pc==12&&active.command_effects==1
          &&active.command_completions==0,"only latest pending command starts on natural phase");
    Check(bool(actor.RequestActorEvent(176,0))&&bool(actor.RequestActorEvent(157,0)),"replace again while old command active");
    const auto replaced=actor.Snapshot();
    Check(replaced.requested_command==157&&replaced.active_command==active.active_command
          &&replaced.command_pc==active.command_pc&&replaced.command_effects==active.command_effects
          &&replaced.effect_order==active.effect_order&&replaced.command_ready==active.command_ready
          &&replaced.command_repeat_ready258==active.command_repeat_ready258&&replaced.channel_admissions==active.channel_admissions
          &&replaced.command_completions==0&&replaced.event_pending_replacements==2&&!actor.WaitReady(),"pending writes retain active cursor flags motion and completion");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().active_command==157
          &&actor.Snapshot().command_pc==12&&actor.Snapshot().command_effects==1
          &&actor.Snapshot().command_completions==0,"natural next phase resets cursor for157 without completing153");
    Check(bool(actor.RequestActorEvent(176,0))&&bool(actor.AdvanceActorFrame())&&actor.Snapshot().command_flag25A,
          "176 first8040 produces true25A");
    Check(bool(actor.RequestActorEvent(157,0))&&actor.Snapshot().command_flag25A,
          "pending request retains active25A");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().active_command==157&&!actor.Snapshot().command_flag25A,
          "natural24700 clears old25A before157 first8030");
    Check(bool(actor.RequestActorEvent(176,0)),"pending owner before close");actor.Close();
    Check(!actor.RequestActorEvent(153,0)&&!actor.WaitReady()&&!actor.Snapshot().actor_request_pending,"close retires pending and active owners");
    GameActorRuntime moving;
    Check(bool(moving.Initialize(0,PackageStorage::Attach(lease,16),fade,&source))&&bool(moving.RequestActorEvent(153,0))
          &&bool(moving.AdvanceActorFrame())&&bool(moving.AdvanceActorFrame()),"nontrivial old movement fixture");
    const auto old=moving.Snapshot();
    Check(old.movement_pending&&old.pending_logical_motion==9&&!moving.RequestActorEvent(176,0)
          &&moving.Snapshot().active_command==old.active_command&&moving.Snapshot().command_pc==old.command_pc
          &&moving.Snapshot().event_requests==old.event_requests&&!moving.Snapshot().actor_request_pending,
          "unrecovered physical-mode interruption refuses transactionally; no new command waits behind old mode");
    moving.Close();
    fade.Retire();
}
void ActorChannelOrder(){
    auto lease=std::make_shared<Lease>(std::vector<uint8_t>(16));GameTransition fade;Check(fade.Initialize({{0,0}}),"actor synthetic fade");
    ActorSource source;GameActorRuntime actor;
    Check(bool(actor.Initialize(0,PackageStorage::Attach(lease,16),fade,&source))&&bool(actor.RequestActorEvent(176,0)),"live ASC actor request");
    Check(!actor.WaitReady(),"request/installation cannot signal VM54");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().command_effects==1&&!actor.WaitReady(),"first8040 only,not completion");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().pending_logical_motion==9&&actor.Snapshot().channel_admissions==0,
          "coordinate effect precedes actual logical9 handoff");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().first_channel_motion==8&&actor.Snapshot().channel_admissions==2
          &&actor.Snapshot().command_pc==32&&!actor.WaitReady(),"both real channels8 admitted before next ASC parameter");
    for(unsigned i=0;i<35;++i)Check(bool(actor.AdvanceActorFrame()),"actor phase with looping motion and source angular gate");
    auto state=actor.Snapshot();
    Check(state.command_blocked&&state.command_frontier==0x6D6C&&state.command_pc==56&&state.command_effects==5,
          "later motion cannot jump absent actual scene pair consumer");
    Check(state.channels[0].loop&&state.channels[0].loops>0&&!state.actor_control_ready&&!actor.WaitReady()
          &&state.angular_completions==1&&state.motion_samples>0,"loops/pose/rotation alone never command completion");
    Check(state.effect_order[0]==0x8040&&state.effect_order[1]==0x8030&&state.effect_order[4]==0x8030,
          "checked ASC operand effect order retained");
    actor.Close();Check(!actor.WaitReady()&&actor.Snapshot().channel_motion[0]==-1,"channels retired before resources");
    GameActorRuntime scene;source.scene_assets=true;
    Check(bool(scene.Initialize(0,PackageStorage::Attach(lease,16),fade,&source))&&bool(scene.InitializeScene0({{0,0,15,12,21,10,2026}})),"actual14-control scene typed owner");
    int32_t period=-1;Check(bool(scene.QueryScenePeriod(15,49,period))&&period==2
          &&scene.Snapshot().scene_period_table52==2&&scene.Snapshot().season_controls==7,
          "actual optional presence admits month/day row2,not constructor row0");
    Check(bool(scene.QueryScenePeriod(15,50,period))&&period==3,"scene-selected row2 endpoint used by GAME36");
    Check(scene.Snapshot().scene_controls==14&&scene.Snapshot().scene_pair_calls==2
          &&scene.Snapshot().scene_playback[10].state==3&&scene.Snapshot().scene_playback[13].state==3
          &&scene.Snapshot().scene_playback[8].state==1&&scene.Snapshot().scene_playback[11].state==1,
          "source startup pairs10/13 stop8/11 before start");
    Check(bool(scene.RequestActorEvent(176,0)),"paired actual command owner");
    for(unsigned i=0;i<55;++i)Check(bool(scene.AdvanceActorFrame()),"scene pair and nonloop command progression");
    const auto completed=scene.Snapshot();
    Check(completed.scene_pair3_calls==1&&completed.scene_last_pair==std::array<int32_t,2>{{3,2}}
          &&completed.scene_playback[2].state==1&&completed.scene_playback[3].state==2,
          "real pair3 stops2 and starts3,retained final pose");
    Check(completed.asc176_ready&&completed.asc176_complete&&completed.command_completions==1&&scene.WaitReady()
          &&completed.command_effects==9,"ready only after actual nonloop motion,end marker and root unwind");
    Check(bool(scene.RequestActorEvent(153,0))&&!scene.WaitReady(),"new switch request cannot inherit previous command readiness");
    bool moved_without_ready=false;
    for(unsigned i=0;i<70&&scene.Snapshot().command_completions<2;++i){
        Check(bool(scene.AdvanceActorFrame()),"checked switch actor phase");
        const auto moving=scene.Snapshot();if(moving.movement_pending&&moving.movement_updates){
            moved_without_ready=true;Check(!scene.WaitReady()&&moving.actor_root[1]!=-627,"intermediate live movement is not command completion");}
    }
    Check(moved_without_ready&&scene.Snapshot().last_completed_command==153
          &&scene.Snapshot().source_flag2A980==1&&scene.Snapshot().light_toggle_calls==1
          &&scene.Snapshot().light_on_playback.samples>0&&scene.Snapshot().movement_completions==1,
          "real motion end and parameter8 control/flag effects precede switch completion");
    Check(bool(scene.RequestActorEvent(157,0))&&!scene.WaitReady(),"mode3 target request remains pending");
    for(unsigned i=0;i<40&&scene.Snapshot().command_completions<3;++i)Check(bool(scene.AdvanceActorFrame()),"checked mode3 phase");
    Check(scene.Snapshot().last_completed_command==157&&scene.Snapshot().movement_completions==2
          &&scene.Snapshot().logical_xy==std::array<float,2>{{600,-300}}&&scene.WaitReady(),
          "mode3 completes only at the produced live target");
    bool position=false;Check(bool(scene.QueryActorPosition(position))&&position
          &&scene.Snapshot().position3B0==scene.Snapshot().actor_root&&scene.Snapshot().position_queries==1,
          "C0A8 copies actual root into owned query point without changing actor40");
    int32_t slot=-1;Check(bool(scene.CreateModel(170,0,0,0,slot))&&bool(scene.BindCamera(1)),"synthetic scene render owners");
    GameGraphics graphics;Check(graphics.Initialize({})&&bool(scene.Draw(graphics)),"native following-camera frame");
    Check(scene.Snapshot().camera_position==GameVector4{{600,-4700,980,1}}
          &&scene.Snapshot().actor_position==GameVector4{{0,0,600,1}},"explicit native root follow never overwrites unproved source actor40");
    graphics.Close();
    scene.Close();Check(!scene.Snapshot().scene_model_live&&!scene.Snapshot().scene_controls&&!scene.WaitReady(),"scene controls retired with actor");
    position=false;Check(!scene.QueryActorPosition(position)&&!position&&!scene.Snapshot().region_owners_live,
          "retired scene cannot fabricate GAME19 return");
    period=-7;Check(!scene.QueryScenePeriod(12,0,period)&&period==-7,"retired scene clock cannot return successful period");
    GameActorRuntime no_season;source.season_missing=true;
    Check(bool(no_season.Initialize(0,PackageStorage::Attach(lease,16),fade,&source))
          &&bool(no_season.InitializeScene0({{0,0,0,12,21,10,2026}})),"explicit NotFound optional controls retain real constructor row0");
    period=-1;Check(no_season.Snapshot().season_controls==0&&no_season.Snapshot().scene_period_table52==0
          &&bool(no_season.QueryScenePeriod(15,40,period))&&period==3,"absent template cannot fabricate seasonal row2");
    no_season.Close();source.season_missing=false;
    source.fail_motion=true;GameActorRuntime failed;Check(bool(failed.Initialize(0,PackageStorage::Attach(lease,16),fade,&source))
          &&bool(failed.RequestActorEvent(176,0)),"motion failure request owner");
    for(unsigned i=0;i<3;++i)Check(bool(failed.AdvanceActorFrame()),"checked motion error phase");
    Check(failed.Snapshot().command_blocked&&failed.Snapshot().channel_admissions==0&&!failed.WaitReady(),
          "missing DAK has no published control or ready signal");
    failed.Close();fade.Retire();
}
void PositionRegionTests(){
    GamePositionRegions owner;bool inside=false;GameVector4 point{{0,0,7,1}},copy=point;
    Check(!owner.ContainsSecond(point,inside)&&!inside,"unbuilt region owner refuses without result publication");
    Check(bool(owner.Initialize({}))&&bool(owner.ContainsSecond(point,inside))&&inside,"source empty second list means true");
    Check(bool(owner.FallbackSecond(point,copy))&&copy==point,"empty fallback preserves original value includingZ");
    GameRegionInput a;a.node=74;a.selector=1;a.shape=1;a.first=10;a.second=20;
    a.world[12]=30;a.world[13]=-5;
    GameRegionInput b=a;b.node=75;b.world[12]=70;
    Check(bool(owner.Initialize({a,b})),"two source-ordered owned quads");
    a.world[12]=999;
    Check(owner.Lists()[1][0].center[0]==30,"compiled regions own matrix-derived values");
    for(const auto p:{GameVector4{{30,-5,0,1}},GameVector4{{40,0,900,1}},GameVector4{{20,-10,-900,1}}}){
        inside=false;Check(bool(owner.ContainsSecond(p,inside))&&inside,"inclusive affine quad boundary ignores sourceZ");}
    inside=true;Check(bool(owner.ContainsSecond({{40.01f,0,0,1}},inside))&&!inside,"outside all source quads is false");
    Check(bool(owner.FallbackSecond({{50,-5,0,1}},copy))&&copy==GameVector4{{30,-5,0,1}},"equal center distance keeps first candidate,not nearest boundary");
    auto blocker=b;blocker.selector=2;blocker.first=100;blocker.second=100;blocker.world[12]=30;
    Check(bool(owner.Initialize({b,blocker}))&&bool(owner.FallbackSecond(point,copy))&&copy==GameVector4{{0,0,1,1}},"fully contained candidate is excluded from fallback");
    // Partial overlap does not fully contain a candidate.
    a.world[12]=30;blocker.first=10;blocker.second=10;
    Check(bool(owner.Initialize({a,blocker}))&&bool(owner.FallbackSecond({{50,-5,0,1}},copy))&&copy==GameVector4{{30,-5,0,1}},"partial overlap cannot remove fallback candidate");
    blocker.second=20;
    Check(bool(owner.Initialize({a,blocker}))&&bool(owner.FallbackSecond(point,copy))&&copy==GameVector4{{0,0,1,1}},"no eligible fallback returns ownedP with affineZ1");
    GameRegionInput circle;circle.node=1;circle.selector=1;circle.shape=2;circle.first=5;
    Check(bool(owner.Initialize({circle})),"circle region");
    inside=false;Check(bool(owner.ContainsSecond({{3,4,19,1}},inside))&&inside,"inclusive XY circle radius");
    inside=true;Check(bool(owner.ContainsSecond({{3,4.01f,0,1}},inside))&&!inside,"outside circle refuses membership");
    auto circular_blocker=circle;circular_blocker.selector=2;circular_blocker.first=10;
    Check(bool(owner.Initialize({circle,circular_blocker}))&&bool(owner.FallbackSecond(point,copy))&&copy==GameVector4{{0,0,1,1}},"fully contained circle excluded");
    circular_blocker.world[12]=12;
    Check(bool(owner.Initialize({circle,circular_blocker}))&&bool(owner.FallbackSecond(point,copy))&&copy==GameVector4{{0,0,0,1}},"partial circle overlap is not containment");
    circular_blocker.world=ModelIdentity();circular_blocker.first=5;
    Check(bool(owner.Initialize({circle,circular_blocker}))&&bool(owner.FallbackSecond(point,copy))&&copy==GameVector4{{0,0,0,1}},"circle contact is not strict full containment");
    GameRegionInput rotated=a;rotated.world=ModelIdentity();rotated.world[0]=0;rotated.world[1]=1;rotated.world[4]=-1;rotated.world[5]=0;
    Check(bool(owner.Initialize({rotated})),"quad uses actual transformed basis");
    inside=false;Check(bool(owner.ContainsSecond({{5,10,0,1}},inside))&&inside,"rotated source corner included");
    auto bad=rotated;bad.shape=3;Check(!owner.Initialize({rotated,bad})&&!owner.Live(),"unsupported shape cannot publish partial owner");
    bad=rotated;bad.world[0]=bad.world[1]=0;Check(!owner.Initialize({bad}),"degenerate compiled edge refuses");
    std::vector<GameRegionInput> full(257,circle);Check(!owner.Initialize(full),"original256 region cap enforced");
    Check(bool(owner.Initialize({circle})),"fresh owner after failure");owner.Close();
    inside=true;Check(!owner.ContainsSecond(point,inside)&&inside,"retirement refuses without fabricated Boolean");
}
void TitleLifetimeAndFade() {
    uint32_t segment = 0;
    Check(TitleMovementStep(2, 4, segment) == 0 && segment == 1 && TitleMovementStep(2, 5, segment) == 52,
          "Jun tuple step precedes next-segment publication");
    Check(TitleMovementStep(1, 99, segment) == 58 && TitleMovementStep(3, 99, segment) == 33,
          "signed integer distance*10/duration source steps");
    std::vector<uint8_t> memory(4 * 1024 * 1024);
    GameArena arena;
    Check(arena.Initialize(memory.data(), uint32_t(memory.size())), "TITLE synthetic arena");
    GameVmBoot manager;
    Check(manager.Initialize74EF4(arena) && manager.InitializeManager93404(), "TITLE manager");
    const auto baseline = arena.Used();
    TitleSource source;
    GameTitleScene scene;
    Check(bool(scene.Initialize(source, arena, manager, NativeTitleLayout::CharacterEntrance, 0)),
          "TITLE actual typed owner constructors over explicit synthetic assets");
    Check(scene.Snapshot().clones == 10 && scene.Snapshot().motions == 21 &&
              scene.Snapshot().visible_actors == 0,
          "all cloned owners initially hidden");
    GameGraphics graphics;
    Check(graphics.Initialize({}), "TITLE graphics");
    GameTransition fade;
    Check(fade.Initialize({{0, 0}}), "actual fade0 producer");
    Check(bool(scene.TickAndDraw(graphics, fade)) && scene.Snapshot().state == 0 &&
              scene.Snapshot().visible_actors == 0,
          "CC2 starts actual fade1,not forced visibility");
    for (unsigned i = 0; i < 40; ++i)
        Check(bool(scene.TickAndDraw(graphics, fade)), "finite title phase6/11");
    const auto state = scene.Snapshot();
    Check(state.state == 1 && fade.Snapshot().state == 2 && state.visible_actors == 5,
          "visibility only after natural fade2/CC1");
    Check(state.actors[0].x < 2500 && state.actors[5].x == 2500 && !state.actors[5].moving &&
              !state.actors[5].visible,
          "primary movement,cold kuro remains hidden without target");
    Check(!state.scene3_completed && !state.menu_input_implemented, "no fake scene completion/menu success");
    for (unsigned i = 0; i < 300; ++i)
        Check(bool(scene.TickAndDraw(graphics, fade)), "running through real arrivals");
    Check(scene.Snapshot().primary_arrivals >= 2 && scene.Snapshot().primary_restarts >= 2 &&
              !scene.Snapshot().arrival_branch_pending && scene.Snapshot().arrival_countdown == 3,
          "phase11 source arrival restarts cycle;only index0 decrements countdown");
    Check(scene.Close() && arena.Used() == baseline, "TITLE releases text/task/clone ownership");
    GameTitleScene failed;
    source.fail = true;
    Check(!failed.Initialize(source, arena, manager, NativeTitleLayout::CharacterEntrance, 0) &&
              failed.Close() && arena.Used() == baseline,
          "partial constructor failure has reachable cleanup");
    GameTitleScene profile;
    Check(!profile.Initialize(source, arena, manager, NativeTitleLayout::CharacterEntrance, 1) &&
              profile.Close(),
          "nonzero342D8 profile refused");
    graphics.Close();
    Check(manager.Close() && arena.Used() == 0, "all native owners closed");
    arena.Reset();
}
void ArrivalBranchTests() {
    GameTitleSnapshot state;
    state.state = 1;
    state.layout = 0;
    for (unsigned i = 0; i < 10; ++i) {
        auto &actor = state.actors[i];
        actor.type = i < 5 ? i : 5;
        actor.x = 2500;
        actor.y = 2000 + 500.0f * i;
        actor.target_x = -4000;
        actor.target_y = actor.y;
        actor.target_produced = i < 5;
        actor.moving = i < 5;
        actor.visible = actor.desired_visible = i < 5;
    }
    state.actors[1].x = -4000;
    state.actors[1].moving = false;
    TitleArrivalCycle(state, nullptr);
    Check(state.arrival_countdown == 4 && state.primary_restarts == 1 && state.rng_calls == 0 &&
              state.actors[1].x == 5700 && state.actors[1].target_y == 4000,
          "Suzuki arrival restarts source offset position without random/motion "
          "restart");
    state.actors[0].x = -4000;
    state.actors[0].moving = false;
    TitleArrivalCycle(state, nullptr);
    Check(state.arrival_countdown == 3 && state.actors[0].x == 2500, "only Toro decrements140");
    state.actors[0].x = -4000;
    state.selection = 0;
    TitleArrivalCycle(state, nullptr);
    Check(!state.actors[0].desired_visible && state.primary_restarts == 2,
          "selection suppresses attract restart");
    state.selection = -1;
    state.media_state = 1;
    TitleArrivalCycle(state, nullptr);
    Check(state.primary_restarts == 2, "media transition suppresses restart");
    state.media_state = 0;
    state.arrival_countdown = 0;
    RngNativeOwner rng;
    uint64_t seed = 0;
    for (;; ++seed) {
        RngNativeOwner candidate;
        candidate.ImportCurrentState64(seed, 1, "explicit deterministic fixture");
        if (candidate.NextC3234() % 64 == 0)
            break;
    }
    rng.ImportCurrentState64(seed, 1, "explicit deterministic fixture");
    TitleArrivalCycle(state, &rng);
    Check(state.kuro_selections == 1 && state.rng_calls == 1 && state.actors[5].moving &&
              state.actors[5].visible && !state.actors[0].visible,
          "zero countdown seeded64-way branch creates real kuro target");
    state.actors[5].x = state.actors[5].target_x;
    state.actors[5].y = state.actors[5].target_y;
    TitleArrivalCycle(state, &rng);
    Check(!state.actors[5].visible && state.rng_calls == 1,
          "paired visible kuro arrival clears only itself that invocation");
}
void TitleMenuLifecycle() {
    // Synthetic checked assets isolate source state/input/fade/ownership. The
    // connected window run separately validates actual CB2A/GIM/model bodies.
    struct MenuSource : GameResourceSource {
        TitleSource models;
        GameFsStatus OpenResource(const std::string &path, PackageView &out) override {
            if (path != "fixture.CB2A")
                return models.OpenResource(path, out);
            std::vector<uint8_t> b(32 + 404 * 8 + 30 * 52, 0);
            Word(b, 0, 0x30413242);
            Word(b, 8, 32);
            Word(b, 12, 404);
            Word(b, 16, 32);
            Word(b, 20, 30);
            Word(b, 24, 32 + 404 * 8);
            Word(b, 28, 30);
            auto lease = std::make_shared<Lease>(std::move(b));
            out = PackageStorage::Attach(lease, lease->b.size());
            return {};
        }
    } source;
    std::vector<uint8_t> memory(4 * 1024 * 1024);
    GameArena arena;
    Check(arena.Initialize(memory.data(), uint32_t(memory.size())), "menu arena");
    GameVmBoot manager;
    Check(manager.Initialize74EF4(arena) && manager.InitializeManager93404(), "menu manager");
    const auto baseline = arena.Used();
    auto ui = std::make_shared<GameUiResource>();
    Check(bool(ui->Load(source, "fixture.CB2A", "")), "synthetic UI table");
    GameUiInstance clone;
    Check(clone.Clone(ui), "independent menu clone");
    std::vector<uint8_t> f(96, 0);
    Word(f, 0, 0x31544658);
    Word(f, 4, 0x00100010);
    Word(f, 24, 1);
    Word(f, 32, 4);
    Word(f, 36, 4);
    Word(f, 40, 16);
    Word(f, 44, 64);
    Word(f, 52, 8);
    Word(f, 56, 80);
    auto lease = std::make_shared<Lease>(std::move(f));
    GameFontResource font;
    Check(bool(font.Initialize(PackageStorage::Attach(lease, lease->b.size()))), "live font prerequisite");
    RngNativeOwner rng;
    rng.ApplyInitializerTimeT32(1234, 1, "explicit menu test seed");
    GameTitleScene scene;
    Check(bool(scene.Initialize(source, arena, manager, NativeTitleLayout::CharacterEntrance, 0)) &&
              bool(scene.AttachMenu(clone, font, rng)),
          "title owns tasks and borrows actual-lifetime UI/font/RNG");
    GameGraphics graphics;
    Check(graphics.Initialize({}), "menu graphics");
    GameTransition fade;
    Check(fade.Initialize({{0, 0}}), "menu fade");
    const auto tick = [&](unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            Check(bool(scene.TickAndDraw(graphics, fade)), "menu phase state");
    };
    const auto await_root=[&](uint32_t root){
        for(unsigned i=0;i<160&&(scene.Snapshot().layout!=root||scene.Snapshot().state!=1);++i)tick(1);
        Check(scene.Snapshot().layout==root&&scene.Snapshot().state==1,"source fade must reach requested root within bounded test");
    };
    tick(40);
    scene.Input({Circle, true, true, 1});
    tick(1);
    Check(scene.Snapshot().confirmations == 0, "held prior-dialog Circle cannot choose New Game");
    scene.Input({0, true, true, 2});
    scene.Input({Down, true, true, 3});
    tick(1);
    Check(scene.Snapshot().menu_row == 1, "cold124=-1 prevents invented Continue enablement");
    scene.Input({0, true, true, 4});
    scene.Input({Circle, true, true, 5});
    tick(1);
    Check(scene.Snapshot().state == 2 && scene.Snapshot().layout == 0,
          "confirm begins genuine contracting fade");
    await_root(2);
    Check(scene.Snapshot().layout == 2 && scene.Snapshot().state == 1 && scene.Snapshot().confirmations == 1,
          "root2 waits actual close/open,held Circle not inherited");
    scene.Input({0, true, true, 6});
    scene.Input({Right, true, true, 7});
    tick(1);
    Check(scene.Snapshot().selected_actor == 1 && scene.Snapshot().selection_moving,
          "Right starts real carousel target");
    scene.Input({0, true, true, 8});
    tick(11);
    Check(!scene.Snapshot().selection_moving && scene.Snapshot().actors[1].x == 0 &&
              scene.Snapshot().actors[1].z == 0,
          "selected target clamps by200,root2 placement has Z0");
    scene.Input({Cross, true, true, 9});
    tick(1);
    await_root(0);
    Check(scene.Snapshot().layout == 0 && scene.Snapshot().state == 1 && !scene.Snapshot().scene3_completed,
          "Cancel returns through fade instead of scene completion");
    scene.Input({0, true, true, 10});
    scene.Input({Circle, true, true, 11});
    tick(1);
    await_root(2);
    scene.Input({0, true, true, 12});
    scene.Input({Circle, true, true, 13});
    tick(1);
    Check(scene.Snapshot().pending_root == -1 && !scene.Snapshot().selection_motion_complete,
          "motion3 starts without immediate result");
    GameFsStatus status;
    for (unsigned i = 0; i < 100 && status; ++i)
        status = scene.TickAndDraw(graphics, fade);
    Check(!status && status.code == GameFsCode::Unavailable && scene.Snapshot().frontier_pc == 0x1FD80 &&
              scene.Snapshot().selection_motion_complete && scene.Snapshot().state == 1 &&
              scene.Snapshot().layout == 3 && scene.Snapshot().actors[1].x == -600 &&
              !scene.Snapshot().scene3_completed,
          "actual nonloop motion/fade/root3 reach explicit unsupported SOFTKEY,never fake CC3");
    Check(!manager.Close() && scene.Close() && arena.Used() == baseline,
          "menu owner survives until joined cleanup");
    clone.Close();
    ui->Close();
    font.Close();
    graphics.Close();
    Check(manager.Close() && !arena.Used(), "all borrowed and owned state retires");
}
} // namespace
int main(int argc,char** argv) {
    try {
        if(argc==4&&std::string(argv[1])=="--outing-data")return OutingPacketCli(argv[2],argv[3]);
        AreaMarkerTests();
        BackgroundModelMotionTests();
        BackgroundAuxiliaryTests();
        BackgroundEffectsTests();
        BackgroundSamplingTests();
        SelectedOptionalAuxiliaryTests();
        SupportConsumerTests();
        SupportGlyphTests();
        ManagerSupportJobTests();
        ContextActorTransactionPrefixTests();
        BgmTransportTests();
        MainSceneDataTests();
        ExecutedDestinationPreparationTests();
        SceneBgmCurrentOwnerTests();
        BackgroundPreparationTests();
        MathAndSkin();
        GeometryAndRaster();
        TexturedColorApproximation();
        NormalLighting();
        ContactQueriesAndDecal();
        RoomAnchorLocalX();RoomModelLifecycle();
        Type12RegionDescriptor();
        Motion();
        ActorChannelOrder();
        ActorPendingReplacement();
        CameraTargetRender();
        PlacementLiveTests();
        OrdinaryContentDataTests();
        OrdinaryContentDataTests(true);
        PositionRegionTests();
        TitleLifetimeAndFade();
        ArrivalBranchTests();
        TitleMenuLifecycle();
        std::cout << "model clone/geometry,skin/source quaternion,clip/texture "
                     "pixels,errors/retirement PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
