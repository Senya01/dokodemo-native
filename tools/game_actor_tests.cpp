#include "recovered/game_actor_script_native.h"
#include "recovered/game_value_array_native.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace dokodemo;
using namespace dokodemo::recovered;
namespace {
void Check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
void Word(std::vector<uint8_t>& b,size_t at,uint32_t v){for(unsigned i=0;i<4;++i)b.at(at+i)=uint8_t(v>>(i*8));}
struct Lease final:PackageStorageLease {
    std::vector<uint8_t> bytes,known;bool live=true;
    explicit Lease(std::vector<uint8_t> data):bytes(std::move(data)),known(bytes.size(),1){}
    bool Span(PackageStorageSpan& s,std::string& error)const override{
        if(!live){error="test lease retired";return false;}
        s.bytes=const_cast<uint8_t*>(bytes.data());s.initialized=const_cast<uint8_t*>(known.data());
        s.size=bytes.size();s.host_address=uint64_t(reinterpret_cast<uintptr_t>(bytes.data()));
        s.owner=s.host_address;s.generation=s.serial=1;s.readable=true;s.writable=false;return true;
    }
    PackageView View() {return PackageStorage::Attach(std::shared_ptr<const Lease>(this,[](const Lease*){}),bytes.size());}
};
struct MissingResources final:GameResourceSource {
    uint32_t calls=0;
    GameFsStatus OpenResource(const std::string&,PackageView&)override{
        ++calls;return {GameFsCode::Unavailable,0,"Explicit test missing model"};
    }
};
class SelectedRoomResources final:public GameResourceSource {
  public:
    explicit SelectedRoomResources(std::ifstream& file):file_(file){}
    GameFsStatus OpenResource(const std::string& path,PackageView& out) override {
        try {
            const auto split=path.find('\\');Check(split!=std::string::npos,"diagnostic mounted request");
            const auto mount=path.substr(0,split);Check(mount=="BG_ROOM"||mount=="NEKO"||mount=="DATA","diagnostic actual game mounts only");
            auto& package=packages_[mount];
            if(!package.lease) {
                file_.clear();file_.seekg(0,std::ios::end);const auto extent=uint64_t(file_.tellg());
                std::vector<uint8_t> prefix(23312);file_.seekg(0);
                file_.read(reinterpret_cast<char*>(prefix.data()),prefix.size());Check(bool(file_),"root prefix full read");
                Lease root(std::move(prefix));const auto directory=BpDirectory::Parse(root.View(),extent);
                const auto selected=directory.Resolve(mount+".BPM");
                Check(selected.size&&selected.size<=128*1024*1024&&selected.offset+selected.size<=extent,"selected room extent");
                std::vector<uint8_t> bytes(size_t(selected.size));file_.seekg(selected.offset);
                file_.read(reinterpret_cast<char*>(bytes.data()),bytes.size());Check(bool(file_),"room package actual bounded read");
                package.lease=std::make_unique<Lease>(std::move(bytes));package.directory=BpDirectory::Parse(package.lease->View(),package.lease->bytes.size());
                std::cout<<mount<<" offset="<<selected.offset<<" bytes="<<selected.size<<'\n';
            }
            out=package.directory.Open(path.substr(split+1));std::cout<<"resource="<<path<<" bytes="<<out.Describe().size<<'\n';return {};
        }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
    }
  private:
    std::ifstream& file_;
    struct OwnedPackage{std::unique_ptr<Lease> lease;BpDirectory directory;};
    std::map<std::string,OwnedPackage> packages_;
};
script::Program Fixture(std::vector<uint8_t> code,std::vector<uint32_t> globals={}){
    std::vector<uint8_t> b(28+code.size()+4*globals.size());
    Word(b,0,0x0043534B);Word(b,4,0x20030204);Word(b,12,uint32_t(code.size()));
    b[24]=uint8_t(globals.size());b[25]=uint8_t(globals.size()>>8);
    std::copy(code.begin(),code.end(),b.begin()+28);
    for(size_t i=0;i<globals.size();++i)Word(b,28+code.size()+4*i,globals[i]);
    script::Program p;script::Diagnostic error;Check(p.load_test_fixture(std::move(b),error),error.text().c_str());return p;
}
std::vector<uint8_t> Dictionary(){
    std::vector<uint8_t> b(34);Word(b,0,0x90000001);Word(b,4,0xB0000008);
    Word(b,8,0x90000003);Word(b,12,0x20000002);Word(b,16,0x60000018);Word(b,20,0x6000001D);
    const uint8_t text[]{0x83,0x6C,0x83,0x52,0,0x82,0xCB,0x82,0xB1,0};
    std::copy(std::begin(text),std::end(text),b.begin()+24);return b;
}
void Focused(){
    // Actual relocation-backed scene clock row0 at VA E45C8. Assert each
    // boundary independently,including the early interval's special result5.
    const std::array<int16_t,12> clock_row0{{0,0,4,0,6,0,10,0,15,30,18,0}};
    for(const auto test: {std::array<uint32_t,3>{{0,0,5}},{{3,59,5}},{{4,0,0}},{{5,59,0}},
                         {{6,0,1}},{{9,59,1}},{{10,0,2}},{{15,29,2}},{{15,30,3}},
                         {{17,59,3}},{{18,0,4}},{{23,59,4}}})
        Check(ClassifySceneClock7398(clock_row0,test[0],test[1])==int32_t(test[2]),
              "source7398 actual row0 lower-inclusive/upper-exclusive boundary");
    Check(SceneSeason709C(10,21)==2&&SceneSeason709C(10,6)==3&&SceneSeason709C(1,1)==0
          &&SceneSeason709C(1,7)==1,"source709C seventh-day seasonal transition and January wrap");
    for(uint32_t row=0;row<7;++row){
        const auto points=SceneClockEndpoints7398(row);
        Check(ClassifySceneClock7398(points,0,0)==5&&ClassifySceneClock7398(points,23,59)==4,
              "all seven actual clock tables keep early/fallback results");
        for(uint32_t edge=1;edge<6;++edge){
            const auto minute=uint32_t(points[edge*2]*60+points[edge*2+1]);
            Check(ClassifySceneClock7398(points,minute/60,minute%60)==int32_t(edge)-1,
                  "all source clock rows transition at exact retained endpoint");
        }
    }
    bool bad_clock=false;try{(void)ClassifySceneClock7398(clock_row0,24,0);}catch(const std::exception&){bad_clock=true;}
    Check(bad_clock,"unavailable/invalid native civil clock cannot produce a scene period");
    auto p=Fixture({0x30,0,0x38},{0x90000001,0xB0000002,0x90000003,0x20000000,0,0,
                   0x90000003,0x20000001,0x20000000,0,0x90000003,0x20000000,0x20000001,0x20000004,0x90000001,0x20000309});
    script::Vm vm(p);Check(vm.start().status==script::Status::Running,"fixture starts");
    script::Value root;root.kind=script::Value::Kind::Reference;root.table=script::Table::Global;
    Lease input(Dictionary());GameActorDictionary dictionary;
    Check(bool(dictionary.Attach(input.View()))&&bool(dictionary.Import(vm,root)),"typed dictionary tree import");
    const auto before=dictionary.ReadRecord(vm,0);
    Check(before.first=="\x83\x6C\x83\x52"&&before.second=="\x82\xCB\x82\xB1","copied real CP932 units");
    Check(!dictionary.Snapshot().source_live&&dictionary.Snapshot().copied_strings==2,"source handle retired,owned strings");
    auto condition=root;condition.index=6;auto actions=root;actions.index=10;auto needle=root;needle.index=9;
    vm.store_reference(needle,script::Value::string(before.first));
    const auto match=GameValueArray98(vm,{root,condition,actions});
    auto record=vm.load_reference(vm.indexed_reference(root,0));
    Check(match.selected==1&&match.writes==1&&vm.load_reference(vm.indexed_reference(record,0)).signed_integer()==6,
          "real string predicate result and bitwise record mutation");
    auto output=root;output.index=14;
    const auto found=GameValueArray96(vm,{root,output,condition});
    Check(found.selected==1&&found.indices==std::vector<uint32_t>{0}&&vm.load_reference(vm.indexed_reference(output,0)).signed_integer()==0,
          "96 writes actual zero-based match indices and returns count");
    vm.store_reference(needle,script::Value::string("absent"));
    Check(GameValueArray98(vm,{root,condition,actions}).selected==0,"no-match result,no invented selection");
    input.live=false;Check(dictionary.ReadRecord(vm,0)==before,"imported strings survive input retirement");
    vm.store_reference(vm.indexed_reference(record,1),script::Value::string("changed"));
    Check(dictionary.ReadRecord(vm,0).first=="changed","query reads mutable game tree,not cached file text");
    dictionary.Close();bool refused=false;
    try{dictionary.ReadRecord(vm,0);}catch(const std::exception&){refused=true;}Check(refused,"closed dictionary refuses query");
    for(unsigned n=0;n<4;++n){
        auto data=Dictionary();
        if(n==0)Word(data,16,0x60FFFFFF); // string bounds
        if(n==1)data[24]=0x81,data[25]=0; // incomplete CP932
        if(n==2)Word(data,4,0xB0000000); // reference cycle
        if(n==3)Word(data,8,0x90000004); // mismatched destination extent
        Lease bad(std::move(data));Check(bool(dictionary.Attach(bad.View())),"root valid before deep import");
        const auto old=vm.load_reference(vm.indexed_reference(record,1)).cp932;
        Check(!dictionary.Import(vm,root)&&!dictionary.Snapshot().imported,"malformed tree refused");
        Check(vm.load_reference(vm.indexed_reference(record,1)).cp932==old,"failed import has no published partial write");
    }
    // Nested caller reference survives the native frame switch and is updated
    // by the real20/30/51/31 path. No host-success callback supplies the result.
    auto call=Fixture({0x30,1,0x91,0,0,0x80,7,0x51,0x91,0,0,0x20,1,22,0,0,
                       0x90,0,0,0x70,0x38,0x03,0x30,1,0x90,0,0,0x80,9,0x51,0x31});
    script::Vm caller(call);Check(caller.start().status==script::Status::Running,"nested start");
    Check(caller.run().status==script::Status::Complete&&caller.result().signed_integer()==9,"actual nested lvalue/result ABI");
    auto yield=Fixture({0x30,0,0x85,0x41,0x80,4,0x70,0x38});script::Vm scheduler(yield);
    scheduler.start();Check(scheduler.run().status==script::Status::Yield,"explicit native41 timestamp-slice adapter");
    scheduler.clock_tick(1);Check(scheduler.run().status==script::Status::Complete&&scheduler.result().signed_integer()==4,"next native slice resumes exact PC");
    auto unknown=Fixture({0x30,0,0x8B,0xEE,0,0x38});script::Vm unbound(unknown);unbound.start();
    Check(unbound.run().status==script::Status::Stopped,"unknown service is not successful return");
    auto retired=Fixture({0x30,0,0x21,10,0,0,0x89,0xF0,0x38,0x03,0x30,1,0x91,0,0,0x70,0x31});
    script::Vm expired(retired);expired.start();
    Check(expired.run().diagnostic.reason=="Retired native script frame reference","returned frame references expire rather than keeping cyclic owners");
    auto block=Fixture({0x30,0,0x18,11,0,0,0x85,0x41,0x80,7,0x70,0x1A,0x38});
    script::Vm scoped(block);scoped.start();
    Check(scoped.run().status==script::Status::Yield&&scoped.active_control_blocks()==1,
          "18 continues into body and preserves block across native timestamp slice");
    scoped.clock_tick(1);
    Check(scoped.run().status==script::Status::Complete&&scoped.result().signed_integer()==7&&
          scoped.control_blocks_entered()==1&&scoped.control_blocks_closed()==1&&scoped.active_control_blocks()==0,
          "1A closes only active block without changing result");
    auto nested=Fixture({0x30,0,0x18,10,0,0,0x21,12,0,0,0x1A,0x38,
                         0x30,0,0x18,21,0,0,0x80,9,0x70,0x1A,0x31});
    script::Vm unwind(nested);unwind.start();
    Check(unwind.run().status==script::Status::Complete&&unwind.result().signed_integer()==9&&
          unwind.control_blocks_entered()==2&&unwind.control_blocks_closed()==2&&unwind.active_control_blocks()==0,
          "callee closes its block then returns;caller block survives and closes normally");
    auto abnormal=Fixture({0x30,0,0x18,10,0,0,0x21,12,0,0,0x1A,0x38,
                           0x30,0,0x18,23,0,0,0x80,9,0x70,0x31,0x03,0x1A,0x38});
    script::Vm unclosed(abnormal);unclosed.start();
    Check(unclosed.run().diagnostic.reason=="Return through active block: source67548 unwind unresolved",
          "unresolved source abnormal unwind is not reconstructed as normal1A");
    auto foreign=Fixture({0x30,0,0x18,10,0,0,0x21,12,0,0,0x1A,0x38,0x30,0,0x1A,0x31});
    script::Vm bad_close(foreign);bad_close.start();
    Check(bad_close.run().status==script::Status::Stopped,"callee cannot close caller block");
    for(uint8_t selector: {uint8_t(0),uint8_t(1),uint8_t(2),uint8_t(255)}) {
        // Linked metadata is deliberately not adjacent to its prior body.
        // 89 captures raw result30,so no Boolean/forced branch can pass.
        std::vector<uint8_t> code{0x30,0,0x80,selector,0x13,
            0,0,0,0x20,15,0,0,0x89,0x70,0x38,
            1,0,0,0x10,25,0,0,0x89,0x70,0x38,
            0,0,0,0,0x89,0x70,0x38};
        if(selector==255)Word(code,5,0x2FFFFFFF);
        auto linked=Fixture(code);script::Vm sw(linked);sw.start();
        Check(sw.run().status==script::Status::Complete&&sw.switches_executed()==1,"linked switch executes selected body");
        const uint32_t expected=selector==0?0x20000000:selector==1?0x10000001:selector==255?0x2FFFFFFF:0;
        Check(sw.result().word==expected,"switch signed28 selection and raw case/default result retained");
        if(selector==0) {
            code[9]=5;bool rejected=false;
            try{(void)Fixture(code);}catch(const std::exception&){rejected=true;}
            Check(rejected,"cyclic linked metadata rejected before execution");
        }
    }
    auto spinning=Fixture({0x30,0,0x10,2,0,0});script::Vm responsive(spinning);responsive.start();
    Check(responsive.run(20).status==script::Status::HostYield&&!responsive.stopped()&&responsive.pc()==2,
          "native quota suspends without invented source yield or forced completion");
    Check(responsive.run(10).status==script::Status::HostYield&&responsive.executed()==30,
          "bounded loop resumes actual PC and live VM state");
    auto waiting=Fixture({0x30,0,0x8B,0x23,0,0x80,6,0x70,0x38});
    script::Vm event_wait(waiting);event_wait.host_service([&](uint8_t id,const std::vector<script::Value>&){
        Check(id==0x23,"wait builtin");event_wait.wait_for_event();script::Value nil;nil.kind=script::Value::Kind::Scalar;return nil;});
    event_wait.start();Check(event_wait.run().status==script::Status::Complete,"cold54one makes builtin23 immediate");
    event_wait.start();event_wait.clear_event_signal();
    Check(event_wait.run().status==script::Status::Waiting&&event_wait.pc()==5,"cleared54 waits at real postcall PC");
    Check(event_wait.run().status==script::Status::Waiting,"no automatic one-frame event completion");
    event_wait.signal_event();Check(event_wait.run().status==script::Status::Complete&&event_wait.result().signed_integer()==6,
          "explicit real event signal resumes result/control state");
    auto scopes=Fixture({0x30,0,0x0D,0x0B,0x80,8,0x70,0x0C,0x0E,0x38});
    script::Vm event_scope(scopes);event_scope.start();
    Check(event_scope.run().status==script::Status::Complete&&event_scope.result().signed_integer()==8&&event_scope.event_scope_depth()==0,
          "nested event and opcode-cost scopes preserve result and balance");
    Lease owner_input(Dictionary());GameTransition fade;Check(fade.Initialize({{0,0}}),"transition source-produced center");
    GameActorRuntime actor;Check(bool(actor.Initialize(0,owner_input.View(),fade)),"typed actor/camera owners admitted");
    Check(actor.WaitReady()&&actor.Snapshot().active_command==-1,"23FD0 actual cold258ready producer");
    Check(bool(actor.BindCamera(1))&&actor.Snapshot().camera_follows_actor&&actor.Snapshot().actor_position[2]==600,
          "72 binds live source actor point,not zero/null");
    Check(bool(actor.StartTransition())&&fade.Snapshot().group==5&&fade.Snapshot().state==1,
          "service6 starts real existing transition,not a returned success flag");
    int32_t absent_slot=3;
    Check(!actor.CreateModel(170,0,0,0,absent_slot)&&absent_slot==-1&&actor.Snapshot().model_slots==0,
          "service31 cannot allocate a successful slot without actual resource owner");
    Check(!actor.Snapshot().manager_request8&&bool(actor.RequestManager())&&actor.Snapshot().manager_request8,
          "zeroed1B188 manager receives actualservice0 request,not completion");
    owner_input.live=false;Check(!actor.WaitReady()&&!actor.BindCamera(0),"retired target lease blocks camera/wait service");
    actor.Close();fade.Retire();Check(!actor.Snapshot().live,"typed actor/camera owners retired");
    Lease model_input(Dictionary());GameTransition model_fade;Check(model_fade.Initialize({{0,0}}),"model error test fade");
    MissingResources missing;GameActorRuntime model_owner;
    Check(bool(model_owner.Initialize(0,model_input.View(),model_fade,&missing)),"model error owner initialized");
    int32_t failed_slot=0;
    Check(!model_owner.CreateModel(170,0,0,0,failed_slot)&&failed_slot==-1&&missing.calls==1&&model_owner.Snapshot().model_slots==0,
          "real resource failure cannot publish first inactive model slot");
    Check(!model_owner.CreateModel(170,1,0,0,failed_slot)&&missing.calls==1,"unrecovered category refuses before I/O");
    model_owner.Close();model_fade.Retire();
    for(const auto values: {std::pair<int,int>{-2,-1},{3,3},{7,6}}){
        auto comparison=Fixture({0x30,0,0x80,uint8_t(values.first),0x80,uint8_t(values.second),0xC3,0x70,0x38});
        script::Vm compare(comparison);compare.start();
        Check(compare.run().status==script::Status::Complete&&compare.result().word==(0x10000000u|uint32_t(values.first<=values.second)),
              "C3 compares signed left<=right with typed Boolean result");
    }
    auto deadline=Fixture({0x30,0,0x80,3,0x44,0x80,7,0x70,0x38});script::Vm timed(deadline);
    timed.clock_tick(10);timed.start();
    Check(timed.run().status==script::Status::Yield&&timed.deadline_writes()==1&&timed.timestamp_writes()==0,"44 writes deadline,not41timestamp");
    timed.clock_tick(12);Check(timed.run().status==script::Status::Yield,"real deadline keeps wait before ready tick");
    timed.clock_tick(13);Check(timed.run().status==script::Status::Complete&&timed.result().signed_integer()==7,"deadline resumes exact next instruction");
    auto addition=Fixture({0x30,0,0x80,0xFE,0x80,3,0xE0,0x70,0x38});script::Vm add(addition);add.start();
    Check(add.run().status==script::Status::Complete&&add.result().signed_integer()==1,"E0 signed scalar addition");
    auto nil_addition=Fixture({0x30,0,0x87,0x84,0xE0,0x70,0x38});script::Vm nil_add(nil_addition);nil_add.start();
    Check(nil_add.run().status==script::Status::Complete&&nil_add.result().word==0x10000001,"E0 RHS nil preserves Boolean type");
    auto raw_not=Fixture({0x30,0,0x80,0xFF,0xB1,0x70,0x38});script::Vm negate(raw_not);negate.start();
    Check(negate.run().status==script::Status::Complete&&negate.result().word==0x10000000,"B1 tests raw nonzero payload of negative integer");
}
void Selected(const char* path){
    // Optional new-engine diagnostic,not connected-game proof. Exactly the
    // already source-selected package is read;no extraction or ISO bootstrap.
    std::ifstream f(path,std::ios::binary);Check(bool(f),"DATA.BP open");
    std::vector<uint8_t> package(1058560);f.seekg(34031616);f.read(reinterpret_cast<char*>(package.data()),package.size());
    Check(bool(f),"selected package full read");Lease lease(std::move(package));
    const auto directory=BpDirectory::Parse(lease.View(),lease.bytes.size());
    GameProfileRecord profile;profile.live=true;profile.actor=0;profile.nickname="Toro";profile.player="Sen";
    profile.comment="test";profile.gender=1;profile.date=0x01261021;profile.blood=2;profile.calendar={3,0,0,12,21,10,2026};
    RngNativeOwner rng;rng.ApplyInitializerTimeT32(1,1,"Explicit diagnostic RNG fixture,not actual connected game seed");
    GameTransition transition;Check(transition.Initialize({{0,0}}),"diagnostic transition owner");
    SelectedRoomResources resources(f);
    PackageView scene_image;Check(bool(resources.OpenResource("BG_ROOM\\BG_ROOM\\model01.DVS",scene_image)),"actual scene DVS");
    model::Bytes scene_bytes(size_t(scene_image.Describe().size));for(size_t i=0;i<scene_bytes.size();++i)scene_bytes[i]=scene_image.ReadU8(i);
    const auto scene_data=model::parseDvs(scene_bytes);std::map<int,uint32_t> types;
    for(const auto& node:scene_data.nodes)++types[node.type];for(const auto& row:types)std::cout<<"scene_node_type="<<row.first<<" count="<<row.second<<'\n';
    std::map<int,uint32_t> modes,common_flags;for(const auto& node:scene_data.nodes){if(node.hasGeometry)++modes[node.geometry.mode];if(node.hasCommon)++common_flags[node.commonFlags];}
    for(const auto& row:modes)std::cout<<"scene_geometry_mode="<<row.first<<" count="<<row.second<<'\n';
    for(const auto& row:common_flags)std::cout<<"scene_common_flags="<<row.first<<" count="<<row.second<<'\n';
    for(const auto name:{"fusuma_off.dak","tuzura_big_open.dak","tuzura_small_open.dak"}){
        PackageView input;Check(bool(resources.OpenResource(std::string("BG_ROOM\\BG_ROOM\\")+name,input)),"selected active scene template");
        model::Bytes bytes(size_t(input.Describe().size));for(size_t i=0;i<bytes.size();++i)bytes[i]=input.ReadU8(i);
        const auto data=model::parseDak(bytes);
        for(const auto& object:data.objects)for(const auto& section:object.sections){
            std::cout<<"active_template="<<name<<" node="<<object.targetNodeIndex<<" type="<<section.type<<" sub="<<section.subtype<<" count="<<section.count<<" keys=";
            for(auto word:section.keyWords)std::cout<<word<<',';std::cout<<'\n';
        }
    }
    for(const auto name:{"light01.DAK","light02.DAK","light03.DAK","light04.DAK","light05.DAK","light06.DAK","light07.DAK","light_on.dak"}) {
        PackageView input;Check(bool(resources.OpenResource(std::string("BG_ROOM\\BG_ROOM\\")+name,input)),"actual seasonal-light template");
        model::Bytes bytes(size_t(input.Describe().size));for(size_t i=0;i<bytes.size();++i)bytes[i]=input.ReadU8(i);
        const auto data=model::parseDak(bytes);
        for(const auto& object:data.objects)for(const auto& section:object.sections)
            std::cout<<"light_template="<<name<<" node="<<object.targetNodeIndex<<" type="<<section.type<<" sub="<<section.subtype<<" count="<<section.count<<'\n';
    }
    GameActorRuntime runtime;Check(bool(runtime.Initialize(0,lease.View(),transition,&resources)),"source cold actor controls");
    const auto scene_open=runtime.InitializeScene0(profile.calendar);Check(bool(scene_open),scene_open.detail.c_str());
    GameActorScript game;const auto opened=game.Initialize(directory.Open("neko.dic"),directory.Open("NEKO.KSC"),"KSNEKO\\NEKO.KSC",profile,[]{return 0x30D72E10u;},&rng,&runtime); // explicit diagnostic clock fixture
    Check(bool(opened),opened.detail.c_str());
    for(unsigned i=0;i<600;++i){const auto status=game.Tick();const auto s=game.Snapshot();
        if(i<6||i%20==0||!status)
        std::cout<<"tick="<<s.ticks<<" pc="<<s.pc<<" op="<<s.last_opcode<<" steps="<<s.instructions
                 <<" imported="<<s.dictionary.imported<<" service="<<s.last_service<<" command="<<s.runtime.command_pc
                 <<" motion="<<s.runtime.channel_motion[0]<<" time="<<s.runtime.channels[0].time<<" pair3="<<s.runtime.scene_pair3_calls
                 <<" signals="<<s.event_signals<<" requested="<<s.runtime.requested_command<<" period="<<s.scene_period<<" detail="<<status.detail<<'\n';
        if(!status||s.awaiting_message||s.completed)break;
    }
    const auto s=game.Snapshot();std::cout<<"services=";for(auto id:s.service_ids)std::cout<<id<<',';std::cout<<'\n';
    std::cout<<"queries="<<s.array_query_calls<<" matches="<<s.array_query_matches<<" reads="<<s.dictionary_game_reads
        <<" writes="<<s.dictionary_game_writes<<" random="<<s.random_selections<<" root_returns="<<s.root_functions_returned<<'\n';
    std::cout<<"blocks="<<s.control_blocks_entered<<'/'<<s.control_blocks_closed<<" active="<<s.active_control_blocks
        <<" switches="<<s.switches<<" camera="<<s.runtime.camera_bindings<<" fades="<<s.runtime.transition_requests<<'\n';
    std::cout<<"asc176="<<s.runtime.asc176_ready<<'/'<<s.runtime.asc176_complete<<" runtimeError="<<s.runtime.command_error<<'\n';
    Check(s.scene_period==5&&s.scene_period_result_word==0x20000005u
          &&s.runtime.scene_period_table52==2&&s.runtime.season_controls==7&&s.runtime.scene_light_nodes==4
          &&s.runtime.scene_light_color_writes>0&&s.runtime.scene_light_scalar_writes==2&&s.runtime.scene_light_vector_writes==2,
          "actual seasonal templates/clock row/light field consumers and typed GAME36 result");
    Check(s.runtime.asc176_ready&&s.runtime.asc176_complete&&s.runtime.command_completions==4
          &&s.runtime.first_completed_commands[0]==176&&s.runtime.first_completed_commands[1]==153
          &&s.runtime.first_completed_commands[2]==157&&s.runtime.first_completed_commands[3]==178&&s.runtime.last_completed_command==178
          &&s.runtime.light_toggle_calls==1&&s.runtime.source_flag2A980==1&&s.source_flag2A980
          &&s.runtime.movement_completions==2&&s.runtime.movement_updates>2
          &&s.runtime.logical_xy==std::array<float,2>{{600,-300}}
          &&s.runtime.model_slots==uint32_t(std::count(s.service_ids.begin(),s.service_ids.end(),31u)),
          "real153 motions119/120 switch light and157 reaches coordinate16 without early ready");
    Check(s.position_result_word==0x10000001u&&s.runtime.position_queries>=2
          &&s.runtime.region_counts==std::array<uint32_t,3>{{1,2,14}}&&s.runtime.position_fallbacks==0
          &&s.runtime.position3B0==GameVector4{{600,-300,0,1}}
          &&s.game28_queries>0&&s.game28_result_word==0x10000000u&&s.runtime.source_flag2A97C==0,
          "actual region compilation and Boolean GAME19/GAME28 outputs drive continuation");
    Check(s.blocked&&s.last_service==44&&s.pc==0x297C&&s.event_signals==4&&!s.waiting_for_event
          &&s.last_service_argument_word==0x20000001u&&s.runtime.angular_completions==3
          &&!s.last_service_returned&&!s.runtime.command_blocked,
          "actual178 turn/completion precedes reached GAME44 camera request,not old19 failure");
    Check(!game.Tick()&&!game.Tick(),"blocked VM stays stopped while independent controls remain live");
    const auto after_stop=game.Snapshot();
    Check(after_stop.pc==s.pc&&after_stop.instructions==s.instructions&&after_stop.event_signals==s.event_signals
          &&after_stop.runtime.command_completions==4&&after_stop.runtime.actor_phases>s.runtime.actor_phases,
          "semantic stop cannot fabricate VM completion;real native owners keep pumping");
    game.Close();runtime.Close();transition.Retire();lease.live=false;
    Check(!runtime.Snapshot().live&&!runtime.Snapshot().actor_model_live&&!runtime.Snapshot().model_slots
          &&!runtime.Snapshot().actor_request_pending&&!runtime.Snapshot().selected_extension_pending,"selected actual actor/room owners retire");
}
}
int main(int argc,char** argv){try{Focused();if(argc==3&&std::string(argv[1])=="--selected-data")Selected(argv[2]);
    std::cout<<"actor dictionary/native VM: trees,CP932,aliases,mutation,bounds,lifetime,frames/yield/errors PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
