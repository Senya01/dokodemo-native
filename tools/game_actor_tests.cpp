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
    scheduler.start();Check(scheduler.run().status==script::Status::Yield,"41 yields the interpreter slice");
    scheduler.clock_tick(1);Check(scheduler.run().status==script::Status::Complete&&scheduler.result().signed_integer()==4,"next native slice resumes exact PC");
    auto unknown=Fixture({0x30,0,0x8B,0xEE,0,0x38});script::Vm unbound(unknown);unbound.start();
    Check(unbound.run().status==script::Status::Stopped,"unknown service is not successful return");
    auto retired=Fixture({0x30,0,0x21,10,0,0,0x89,0xF0,0x38,0x03,0x30,1,0x91,0,0,0x70,0x31});
    script::Vm expired(retired);expired.start();
    Check(expired.run().diagnostic.reason=="Retired native script frame reference","returned frame references expire rather than keeping cyclic owners");
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
    GameActorScript game;const auto opened=game.Initialize(directory.Open("neko.dic"),directory.Open("NEKO.KSC"),"KSNEKO\\NEKO.KSC",profile,[]{return 0x30D72E10u;},&rng); // explicit diagnostic clock fixture
    Check(bool(opened),opened.detail.c_str());
    for(unsigned i=0;i<20;++i){const auto status=game.Tick();const auto s=game.Snapshot();
        std::cout<<"tick="<<s.ticks<<" pc="<<s.pc<<" op="<<s.last_opcode<<" steps="<<s.instructions
                 <<" imported="<<s.dictionary.imported<<" service="<<s.last_service<<" detail="<<status.detail<<'\n';
        if(!status||s.awaiting_message||s.completed)break;
    }
    const auto s=game.Snapshot();std::cout<<"services=";for(auto id:s.service_ids)std::cout<<id<<',';std::cout<<'\n';
    std::cout<<"queries="<<s.array_query_calls<<" matches="<<s.array_query_matches<<" reads="<<s.dictionary_game_reads
        <<" writes="<<s.dictionary_game_writes<<" random="<<s.random_selections<<" root_returns="<<s.root_functions_returned<<'\n';
    game.Close();lease.live=false;
}
}
int main(int argc,char** argv){try{Focused();if(argc==3&&std::string(argv[1])=="--selected-data")Selected(argv[2]);
    std::cout<<"actor dictionary/native VM: trees,CP932,aliases,mutation,bounds,lifetime,frames/yield/errors PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
