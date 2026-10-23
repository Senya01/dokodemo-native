#include "recovered/game_actor_script_native.h"
#include "recovered/game_main_flow_native.h"
#include "recovered/game_value_array_native.h"
#include "game_choice_regressions.h"
#include "game_message_scope_regressions.h"
#include "game_post21_regressions.h"
#include "game_return_witness_regressions.h"
#include "game_post25_regressions.h"
#include "game_returned_interaction_regressions.h"
#include "game_conversation_activation_regressions.h"

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_natural_loop_regressions.h"
#endif

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_owned_conversation_regressions.h"
#endif

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_initial_common_regressions.h"
#endif
#include "game_input32_regressions.h"

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_inventory_action_regressions.h"
#endif

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_inventory_decorate_regressions.h"
#endif

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_inventory_discard_regressions.h"
#endif
#include "game_kana_regressions.h"

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_word_op55_regressions.h"
#endif

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_greeting_regressions.h"
#endif
#include "game_main_menu_regressions.h"

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_main_menu_callback_regressions.h"
#endif

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_menu_inventory_regressions.h"
#endif

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_menu_mark_regressions.h"
#endif

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_menu_place_regressions.h"
#endif

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_menu_callback_boundary_regressions.h"
#endif

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_menu_place_scratch_regressions.h"
#endif

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_menu_room_regressions.h"
#endif

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_menu_render_retirement_regressions.h"
#endif
#include "game_notebook_regressions.h"

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
#include "game_flow5_regressions.h"
#endif
#include "game_notebook_slot_probe_regressions.h"
#include "game_notebook_self_regressions.h"
#include "game_wordmemo_regressions.h"
#include "game_diary_regressions.h"
#include "game_system_option_regressions.h"
#include "game_options_open_regressions.h"
#include "game_options_right_regressions.h"
#include "game_options_edit_regressions.h"
#include "game_options_edit_edges.h"
#include "game_options_confirm_regressions.h"
#include "game_options_yes_regressions.h"
#include "game_options_row1_regressions.h"
#include "game_options_se5_regressions.h"
#include "game_options_voice_regressions.h"
#include "game_options_voice_edges.h"
#include "game_options_sequence_regressions.h"
#include "game_options_port_regressions.h"
#include "game_options_processor_regressions.h"
#include "game_options_control_regressions.h"
#include "game_options_wave_regressions.h"
#include "game_options_port24_regressions.h"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <type_traits>
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
            auto mount=path.substr(0,split);for(auto& c:mount)if(c>='a'&&c<='z')c=char(c-'a'+'A');
            Check(mount=="BG_ROOM"||mount=="NEKO"||mount=="DATA"||mount=="MOCHI","diagnostic actual game mounts only");
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
script::Program Fixture(std::vector<uint8_t> code,std::vector<uint32_t> globals={},std::vector<uint32_t> ctx={}){
    std::vector<uint8_t> b(28+code.size()+4*(ctx.size()+globals.size()));
    Word(b,0,0x0043534B);Word(b,4,0x20030204);Word(b,12,uint32_t(code.size()));
    b[24]=uint8_t(globals.size());b[25]=uint8_t(globals.size()>>8);
    b[26]=uint8_t(ctx.size());b[27]=uint8_t(ctx.size()>>8);
    std::copy(code.begin(),code.end(),b.begin()+28);
    for(size_t i=0;i<ctx.size();++i)Word(b,28+code.size()+4*i,ctx[i]);
    for(size_t i=0;i<globals.size();++i)Word(b,28+code.size()+4*(ctx.size()+i),globals[i]);
    script::Program p;script::Diagnostic error;Check(p.load_test_fixture(std::move(b),error),error.text().c_str());return p;
}
std::vector<uint8_t> Dictionary(){
    std::vector<uint8_t> b(34);Word(b,0,0x90000001);Word(b,4,0xB0000008);
    Word(b,8,0x90000003);Word(b,12,0x20000002);Word(b,16,0x60000018);Word(b,20,0x6000001D);
    const uint8_t text[]{0x83,0x6C,0x83,0x52,0,0x82,0xCB,0x82,0xB1,0};
    std::copy(std::begin(text),std::end(text),b.begin()+24);return b;
}
void BitwiseOrAndWordObservationChecks(){
    using script::Value;using script::Table;
    const auto program=Fixture({0x30,2,0x90,0,0,0x90,1,0,0xD5,0x70,0x38});
    constexpr uint32_t payloads[]{0,1,4,32,0x08000000,0x0FFFFFFF};
    for(uint32_t lt=0;lt<4;++lt)for(uint32_t rt=0;rt<4;++rt)for(auto l:payloads)for(auto r:payloads){
        Value left,right;left.kind=lt==3?Value::Kind::Timestamp:Value::Kind::Scalar;left.word=lt<<28|l;
        right.kind=rt==3?Value::Kind::Timestamp:Value::Kind::Scalar;right.word=rt<<28|r;
        script::Vm vm(program);vm.start({left,right});
        Check(vm.run().status==script::Status::Complete&&vm.result().kind==Value::Kind::Scalar&&vm.result().word==(0x20000000u|(l|r)),"D5 raw tag0..3 OR boundary matrix");
        Check(vm.d5_calls()==1&&vm.d5_first_pc()==8&&vm.d5_first_left_word()==left.word&&vm.d5_first_right_word()==right.word&&vm.d5_first_result_word()==vm.result().word,"D5 first execution evidence preserves ordered operands");
    }
    {
        const auto linked=Fixture({0x30,2,0x91,0,0,0x90,0,0,0x90,1,0,0xD5,0x51,0x90,0,0,0x70,0x38});
        script::Vm vm(linked);vm.start({Value::integer(32),Value::integer(4)});
        for(int i=0;i<4;++i)Check(vm.run(1).status==script::Status::HostYield,"D5 linked setup slice");
        const auto before=vm.current_witness();Check(before.current_pc==11&&before.operand_cells==3,"D5 leaves real destination below two scalars");
        Check(vm.run(1).status==script::Status::HostYield,"D5 scalar execution");const auto after=vm.current_witness();
        Check(after.current_pc==12&&after.operand_cells==2&&after.operands[0].kind==Value::Kind::Reference&&after.operands[0].index==0&&after.operands[1].word==0x20000024&&after.result.kind==before.result.kind&&after.result.word==before.result.word&&after.deadline_tick==before.deadline_tick&&after.active_blocks==before.active_blocks,"D5 consumes two scalar cells, keeps destination/result/deadline/blocks and PC+1");
        Check(vm.run().status==script::Status::Complete&&vm.result().word==0x20000024&&vm.read_slot(Table::Frame,0).word==0x20000024,"D5 actual following51 store and caller result");
        auto reference=Value{};reference.kind=Value::Kind::Reference;reference.table=Table::Global;
        for(auto unsupported:std::vector<Value>{Value{},reference,Value::string("owned")}){
            vm.start({unsupported,Value::integer(4)});const auto first=vm.run();
            Check(first.status==script::Status::Stopped&&first.pc==(unsupported.kind==Value::Kind::Unknown?5u:11u)&&vm.d5_calls()==0,"Unknown slot refuses before D5; native reference/string refuse at D5");
            Check(vm.run().diagnostic.text()==first.diagnostic.text(),"D5 refusal preserves first error");
        }
    }
    {
        static_assert(std::is_trivially_copyable<GameWordRowObservation>::value,"Word observation must not retain owners");
        std::vector<uint32_t> globals(0x3DD4,0);globals[0]=0x90000013;globals[0x3DD2]=0x90000001;
        const auto p=Fixture({0x30,0,0x80,1,0x44,0x38},globals);script::Vm vm(p);vm.start();
        const auto ref=[](uint16_t index){Value v;v.kind=Value::Kind::Reference;v.table=Table::Global;v.index=index;return v;};
        vm.store_reference(ref(0x3DD3),ref(0));vm.store_reference(ref(1),Value{});
        vm.store_reference(ref(2),Value::string("shirt"));vm.store_reference(ref(3),Value::string("SHIRT"));vm.store_reference(ref(13),Value::integer(128));
        uint32_t accesses=0;vm.access_observer([&](const Value&,bool){++accesses;});const auto before=vm.current_witness();
        const auto row=GameObserveWordRow(vm,77,0);const auto after=vm.current_witness();
        Check(row.valid&&row.generation==77&&row.run_id==before.run_id&&row.table==uint32_t(Table::Global)&&row.index==0&&row.record_header_word==0x90000013&&row.fields[0].kind==Value::Kind::Unknown&&row.fields[1].cp932_bytes==5&&row.fields[12].word==0x20000080,"Word row fixed typed/hash-only observation");
        Check(accesses==0&&before.current_pc==after.current_pc&&before.executed==after.executed&&before.frame_id==after.frame_id&&before.context_registered==after.context_registered&&before.clock_tick==after.clock_tick&&before.deadline_tick==after.deadline_tick,"Word row observation has no access/context/clock/execution side effects");
        Check(!GameObserveWordRow(vm,77,1).valid,"Word row rejects index outside low12 header");
        vm.store_reference(ref(0),Value::integer(19));Check(!GameObserveWordRow(vm,77,0).valid,"Word row rejects scalar header with same payload");
        Value header;header.kind=Value::Kind::ArrayHeader;header.word=0x90000013;vm.store_reference(ref(0),header);
        auto bad=ref(0);bad.table=Table(99);vm.store_reference(ref(0x3DD3),bad);Check(!GameObserveWordRow(vm,77,0).valid,"Word row rejects invalid table enum before VM fallback");
        bad.table=Table::Frame;vm.store_reference(ref(0x3DD3),bad);Check(!GameObserveWordRow(vm,77,0).valid,"Word row refuses unresolved weak frame owner");
        Check(row.valid&&row.fields[1].cp932_bytes==5&&row.fields[12].word==0x20000080,"Retained word witness is independent of later table mutation");
        vm.store_reference(ref(0x3DD3),ref(0));vm.run();vm.clock_tick(2);vm.run();
        Check(GameObserveWordRow(vm,77,0).valid,"Stopped/completed VM still owns readable cells until teardown; observation is not input admission");
    }
}

void StringLength53Checks(){
    using script::Value;using script::Table;
    for(const auto& text:std::vector<std::string>{"","shirt","SHIRT","Aa09","abcdefgh"}){
        const auto input=Value::string(text);Check(GameStringLength53({input}).word==Value::integer(int32_t(text.size())).word&&input.cp932==text,"Builtin53 ASCII character count, no normalization/input mutation");
    }
    for(char c='0';c<='z';++c)if((c<='9')||(c>='A'&&c<='Z')||c>='a')Check(GameStringLength53({Value::string(std::string(1,c))}).word==0x20000001,"Builtin53 all62 source-proved ASCII flags");
    Value nil;nil.kind=Value::Kind::Scalar;Check(GameStringLength53({nil}).word==0x20000000,"Builtin53 canonical NIL produces integer0, not NIL");
    Value ref;ref.kind=Value::Kind::Reference;
    auto embedded=Value::string("AB");embedded.cp932.assign("A\0B",3); // forged invalid boundary, factory already refuses NUL
    for(auto args:std::vector<std::vector<Value>>{{},{Value{}},{ref},{Value::integer(0)},{embedded},{Value::string("\x88")},{Value::string("a"),Value::string("b")}}){
        bool refused=false;try{(void)GameStringLength53(args);}catch(const std::exception&){refused=true;}Check(refused,"Builtin53 unsupported conversion/character/argc refuses");
    }
    const std::array<uint8_t,32> source_mask{{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xFE,0xFF,0xFF,0xFF,0,0,0,0,0,0,0,0,0xFF,0xFF,0xFF,0x7F}};
    for(unsigned b=1;b<256;++b){
        const bool two=(source_mask[b/8]>>(b%8))&1;std::string text(1,char(b));if(two)text.push_back('!');text.push_back('Z');
        Check(GameStringLength53({Value::string(text)}).word==0x20000002u,"all256 source flag decisions preserve byte-step count");
        if(two){bool refused=false;try{(void)GameStringLength53({Value::string(std::string(1,char(b)))});}catch(...){refused=true;}Check(refused,"all flagged truncated prefixes refuse");}
    }
    Check(GameStringLength53({Value::string("\x88\x9F\n!? \xA6")}).word==0x20000006u,"kanji halfwidth punctuation and control count independently of input admission");
    const auto p=Fixture({0x30,1,0x95,0,0,0x90,0,0,0x8B,0x53,1,0x89,0x51,0x94,0,0,0x70,0x38},{0});
    script::Vm vm(p);uint32_t calls=0;vm.host_service([&](uint8_t id,const std::vector<Value>& args){Check(id==0x53&&args.size()==1&&args[0].cp932=="shirt","Builtin53 real owned caller argument");++calls;return GameStringLength53(args);});
    vm.start({Value::string("shirt")});Check(vm.run().status==script::Status::Complete&&calls==1&&vm.result().word==0x20000005&&vm.read_slot(Table::Global,0).word==0x20000005&&vm.stack().empty(),"Builtin53 real dispatcher result89/51 to preserved destination and exact consumer");
}

void AsciiPrefix71Checks(){
    using script::Value;using script::Table;
    for(char c='0';c<='z';++c)if(c<='9'||(c>='A'&&c<='Z')||c>='a'){
        const auto input=Value::string(std::string(1,c));const auto v=GameAsciiPrefix71({input});Check(v.kind==Value::Kind::Scalar&&v.word==0&&input.cp932==std::string(1,c),"Builtin71 proven ASCII branch returns Scalar NIL without editing input");
    }
    Value ref;ref.kind=Value::Kind::Reference;Value nil;nil.kind=Value::Kind::Scalar;
    for(auto args:std::vector<std::vector<Value>>{{},{Value{}},{nil},{ref},{Value::string("SH IRT")},{Value::string(std::string("\x88\x9F",2))},{Value::string("S"),Value::string("H")}}){
        bool refused=false;try{(void)GameAsciiPrefix71(args);}catch(const std::exception&){refused=true;}Check(refused,"Builtin71 unknown/nonASCII/empty/conversion domains refuse instead of arbitrary NIL");
    }
    const auto p=Fixture({0x30,1,0x80,9,0x70,0x95,0,0,0x90,0,0,0x8B,0x71,1,0x89,0x51,0x94,0,0,0x70,0x38},{0x20000009});
    script::Vm vm(p);uint32_t calls=0;vm.host_service([&](uint8_t id,const std::vector<Value>& args){Check(id==0x71&&args.size()==1&&args[0].cp932=="SHIRT","Builtin71 actual source uppercase argument");++calls;return GameAsciiPrefix71(args);});
    vm.start({Value::string("SHIRT")});Check(vm.run().status==script::Status::Complete&&calls==1&&vm.result().kind==Value::Kind::Scalar&&vm.result().word==0&&vm.read_slot(Table::Global,0).kind==Value::Kind::Scalar&&vm.read_slot(Table::Global,0).word==0&&vm.read_slot(Table::Frame,0).cp932=="SHIRT"&&vm.stack().empty(),"Builtin71 genuine NIL result replaces old9 and goes through89/51 consumer; caller string retained");
}

void KanaSuffix72Checks(){
    using script::Value;using script::Table;
    const std::vector<std::pair<std::string,std::string>> cases{
        {"",""},{"A",""},{"\x83\x41",""},{"\x82\xA0" "A",""},
        {"\x82\xA0\x82\xA2\x82\xA4","\x82\xA4"},
        {"\x82\x9F\x82\xC1\x81\x5B\x82\xF1","\x82\xF1"},
        {"\x82\xC1\x81\x5B",""},{"\x82\xA0\x82\xC1\x81\x5B","\x82\xA0"}};
    for(const auto& sample:cases){
        auto input=Value::string(sample.first);const auto result=GameKanaSuffix72({input});
        Check(input.cp932==sample.first,"Builtin72 leaves caller text unchanged");
        if(sample.second.empty())Check(result.kind==Value::Kind::Scalar&&result.word==0,"Builtin72 actual NIL branch");
        else Check(result.kind==Value::Kind::String&&result.word>>28==6&&result.cp932==sample.second,"Builtin72 last static text copied into native owning result");
    }
    for(const auto& args:std::vector<std::vector<Value>>{{},{Value{}},{Value::integer(0)},{Value::string(" ")},{Value::string("\x88\x9F")},{Value::string("a"),Value::string("b")}}){
        bool refused=false;try{(void)GameKanaSuffix72(args);}catch(const std::exception&){refused=true;}
        Check(refused,"Builtin72 unsupported conversion/domain refuses");
    }
    const auto p=Fixture({0x30,1,0x95,0,0,0x90,0,0,0x8B,0x72,1,0x89,0x51,0x94,0,0,0x70,0x38},{0});
    script::Vm vm(p);uint32_t calls=0;
    vm.host_service([&](uint8_t id,const std::vector<Value>& args){Check(id==0x72,"actual72 dispatcher");++calls;return GameKanaSuffix72(args);});
    vm.start({Value::string("\x82\xA0\x82\xF1")});
    Check(vm.run().status==script::Status::Complete&&calls==1&&vm.result().cp932=="\x82\xF1"
          &&vm.read_slot(Table::Global,0).cp932=="\x82\xF1"&&vm.stack().empty(),"Builtin72 owning result survives real89/51 consumer");
}
void NilBytePair0DChecks(){
    using script::Value;using script::Table;
    Value nil;nil.kind=Value::Kind::Scalar;const auto code=GameNilBytePair0D({nil});
    Check(code.kind==Value::Kind::Scalar&&code.word==0x20007383,"Builtin0D source NIL text first2 bytes form integer7383, not integer0");
    Value ref;ref.kind=Value::Kind::Reference;Value boolean;boolean.kind=Value::Kind::Scalar;boolean.word=0x10000000;
    for(auto args:std::vector<std::vector<Value>>{{},{Value{}},{ref},{boolean},{Value::integer(0)},{Value::string("SHIRT")},{nil,nil}}){bool refused=false;try{(void)GameNilBytePair0D(args);}catch(const std::exception&){refused=true;}Check(refused,"Builtin0D only source-closed canonicalNIL conversion is admitted");}
    const auto p=Fixture({0x30,1,0x95,0,0,0x90,0,0,0x8B,0x0D,1,0x89,0x51,0x94,0,0,0x70,0x38},{0});script::Vm vm(p);uint32_t calls=0;
    vm.host_service([&](uint8_t id,const std::vector<Value>& args){Check(id==0x0D,"Builtin0D linked actualid");++calls;return GameNilBytePair0D(args);});
    vm.start({nil});Check(vm.run().status==script::Status::Complete&&calls==1&&vm.result().word==0x20007383&&vm.read_slot(Table::Global,0).word==0x20007383&&vm.stack().empty(),"Builtin0D genuine dispatcher/result89/51 preserves real destination and integercode consumer");
}

void AsciiClass70OwnerChecks(){
    using script::Value;using script::Table;GameAsciiClass70Owner owner;
    const auto refuses=[&](const std::vector<Value>& args){bool denied=false;try{(void)owner.Query(args);}catch(const std::exception&){denied=true;}return denied;};
    Check(refuses({Value::string("SHIRT")}),"Builtin70 cannot query an unconstructed flag owner");
    owner.Begin(51);Check(owner.Live()&&owner.Generation()==51&&owner.CurrentWord()==0x10000000&&owner.OtherWord()==0x10000001,"Builtin70 explicit76A50 constructor values, not BSS/raw-data default");
    for(char c='0';c<='z';++c)if(c<='9'||(c>='A'&&c<='Z')||c>='a'){
        auto input=Value::string(std::string(1,c));const auto result=owner.Query({input});
        Check(result.kind==Value::Kind::Scalar&&result.word==0x10000000&&input.cp932==std::string(1,c),"Builtin70 ASCII route reads live FALSE flag without mutating owned text");
    }
    Value ref;ref.kind=Value::Kind::Reference;Value nil;nil.kind=Value::Kind::Scalar;
    for(auto args:std::vector<std::vector<Value>>{{},{Value{}},{ref},{nil},{Value::integer(0)},{Value::string("SH IRT")},{Value::string(std::string("\x88\x9F",2))},{Value::string("S"),Value::string("H")}})
        Check(refuses(args),"Builtin70 unsupported domain refuses instead of assuming the flag result");
    const auto p=Fixture({0x30,1,0x95,0,0,0x90,0,0,0x8B,0x70,1,0x89,0x51,0x94,0,0,0x70,0x38},{0x20000009});script::Vm vm(p);uint32_t calls=0;
    vm.host_service([&](uint8_t id,const std::vector<Value>& args){Check(id==0x70&&args.size()==1&&args[0].cp932=="SHIRT","Builtin70 reached real dispatch and retained caller argument");++calls;return owner.Query(args);});
    vm.start({Value::string("SHIRT")});Check(vm.run().status==script::Status::Complete&&calls==1&&vm.result().word==0x10000000&&vm.read_slot(Table::Global,0).word==0x10000000&&vm.read_slot(Table::Frame,0).cp932=="SHIRT"&&vm.stack().empty(),"Builtin70 constructor-owned Boolean passes through actual89/51 consumer, no scalar NIL substitute");
    owner.Close();Check(!owner.Live()&&owner.Generation()==0&&refuses({Value::string("SHIRT")}),"Builtin70 teardown revokes old flag owner");
    owner.Begin(52);Check(owner.Live()&&owner.Generation()==52&&owner.CurrentWord()==0x10000000,"Builtin70 new lifetime explicitly constructs defaults");
    owner.Begin(0);Check(!owner.Live()&&refuses({Value::string("SHIRT")}),"Builtin70 refuses generation-zero owner");
}

void OwnedCtxReferenceBranchChecks(){
    using script::Value;using script::Table;
    for(uint8_t op:{uint8_t(0x11),uint8_t(0x12)}){
        const auto p=Fixture({0x30,0,0x80,9,0x70,0x99,1,0,op,16,0,0,0x80,5,0x70,0x38,0x80,7,0x70,0x38},{},{0,0,0,0});
        script::Vm vm(p);vm.start();const auto context=vm.context_value();uint32_t accesses=0;
        vm.access_observer([&](const Value&,bool){++accesses;});const auto before=vm.current_witness();
        (void)vm.run(5);const auto cut=vm.current_witness();
        Check(!vm.stopped()&&cut.executed==before.executed+5&&cut.current_pc==(op==0x11?12u:16u)&&vm.stack().empty()&&vm.result().word==0x20000009&&accesses==0,
            "Owned Ctx28 raw0 reference is present, not scalarNIL or loaded NIL referent; native result preserved");
        Check(vm.ctx_reference_branches()==1&&vm.ctx_reference_first_pc()==8&&vm.ctx_reference_first_index()==1&&cut.context_word==context.word&&cut.frame_id==before.frame_id&&cut.deadline_tick==before.deadline_tick,
            "Ctx reference branch retains exact owning frame/registry/deadline and source counter");
        Check(vm.run().status==script::Status::Complete&&vm.result().word==(op==0x11?0x20000005u:0x20000007u),"Ctx branch continues to actual selected consumer");
    }
    const auto make=Fixture({0x30,0,0x99,1,0,0x70,0x38},{},{0,0});Value foreign;
    {
        script::Vm producer(make);producer.start();producer.context_value();producer.run();foreign=producer.result();
        const auto take=Fixture({0x30,1,0x90,0,0,0x11,12,0,0,0x80,5,0x70,0x38},{},{0,0});
        script::Vm receiver(take);receiver.start({foreign});receiver.context_value();const auto failed=receiver.run();
        Check(failed.status==script::Status::Stopped&&failed.diagnostic.pc==5&&receiver.ctx_reference_branches()==0,"Foreign Ctx28 reference cannot claim current owner by matching table/index");
    }
    const auto take=Fixture({0x30,1,0x90,0,0,0x11,12,0,0,0x80,5,0x70,0x38},{},{0,0});
    for(auto ref:std::vector<Value>{foreign,Value{}}){
        if(ref.kind==Value::Kind::Unknown){ref.kind=Value::Kind::Reference;ref.table=Table::Ctx28;ref.index=1;}
        script::Vm vm(take);vm.start({ref});vm.context_value();const auto failed=vm.run();
        Check(failed.status==script::Status::Stopped&&failed.diagnostic.pc==5&&vm.ctx_reference_branches()==0,"Expired or unstamped Ctx28 owner refuses; no fake presence");
        Check(vm.run().diagnostic.text()==failed.diagnostic.text(),"Ctx presence failure remains the first error");
    }
    {
        const auto p=Fixture({0x30,0,0x99,1,0,0x11,9,0,0,0x38},{},{0,0});script::Vm vm(p);vm.start();
        Check(vm.run().status==script::Status::Stopped&&!vm.current_witness().context_registered&&vm.ctx_reference_branches()==0,"Ctx presence does not allocate a registry to satisfy its own check");
    }
}

//Keep the independent runtime-owner checks out of the VM fixture stack frame.
void RuntimeOwners(){
    Lease owner_input(Dictionary());GameTransition fade;Check(fade.Initialize({{0,0}}),"transition source-produced center");
    GameActorRuntime actor;Check(bool(actor.Initialize(0,owner_input.View(),fade)),"typed actor/camera owners admitted");
    Check(actor.WaitReady()&&actor.Snapshot().active_command==-1,"23FD0 actual cold258ready producer");
    Check(bool(actor.BindCamera(1))&&actor.Snapshot().camera_follows_actor&&actor.Snapshot().actor_position[2]==600,
          "72 binds live source actor point,not zero/null");
    Check(actor.Snapshot().manager_camera_request4C==-1&&bool(actor.RequestCamera(1))
          &&actor.Snapshot().manager_camera_request4C==1&&actor.Snapshot().camera_requests_consumed==0
          &&actor.Snapshot().camera_fov_degrees==30,"GAME44 setter publishes request without consuming or changing camera");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().manager_camera_request4C==-1
          &&actor.Snapshot().camera_manager_state28==4&&actor.Snapshot().camera_fov_degrees==30
          &&!actor.Snapshot().camera_complete2C,"23488 clear/start then camera samples ratio0;not completion");
    float fov=30;for(unsigned i=0;i<100&&!actor.Snapshot().camera_complete2C;++i){
        Check(bool(actor.AdvanceActorFrame()),"live camera interpolation update");
        const auto now=actor.Snapshot();Check(now.camera_fov_degrees<=fov&&now.camera_fov_degrees>=18,"monotone actual curve target,not forced result");fov=now.camera_fov_degrees;
    }
    Check(actor.Snapshot().camera_complete2C&&fov==18&&actor.Snapshot().camera_manager_state28==4
          &&actor.Snapshot().camera_completions==1&&actor.WaitReady(),"camera threshold completion is independent of BCFC cold ready");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().camera_manager_state28==0,"manager observes camera completion on later phase");
    Check(!actor.Snapshot().manager_request9&&bool(actor.RequestManager9())&&actor.Snapshot().manager_request9,"GAME3 sets separate source manager9 request,not transition completion");
    Check(bool(actor.RequestCamera(0))&&actor.Snapshot().camera_fov_degrees==18&&actor.Snapshot().camera_completions==1,"camera-out setter returns before interpolation or completion");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().camera_transition_mode1C==2&&actor.Snapshot().camera_ratio<1
        &&actor.Snapshot().camera_fov_degrees==18&&!actor.Snapshot().camera_complete2C,"out samples ratio1 before decreasing,retains first FOV");
    fov=18;for(unsigned i=0;i<100&&!actor.Snapshot().camera_complete2C;++i){Check(bool(actor.AdvanceActorFrame()),"real camera-out phase");
        const auto now=actor.Snapshot();Check(now.camera_fov_degrees>=fov&&now.camera_fov_degrees<=30,"camera-out monotone literal30 endpoint");fov=now.camera_fov_degrees;}
    Check(actor.Snapshot().camera_complete2C&&actor.Snapshot().camera_completions==2&&actor.Snapshot().camera_transition_mode1C==0&&fov==30,"out completes only ratio<0/currentFOV>=30");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().camera_manager_state28==0&&actor.Snapshot().manager_request9,"camera poll doesnot consume separate manager9 flag");
    Check(bool(actor.StartTransition())&&fade.Snapshot().group==5&&fade.Snapshot().state==1,
          "service6 starts real existing transition,not a returned success flag");
    int32_t absent_slot=3;
    Check(!actor.CreateModel(170,0,0,0,absent_slot)&&absent_slot==-1&&actor.Snapshot().model_slots==0,
          "service31 cannot allocate a successful slot without actual resource owner");
    {
        GameMainFlow flow;std::vector<std::pair<uint32_t,int32_t>> events;
        Check(bool(flow.Initialize(actor,[&](uint32_t event,int32_t arg){events.emplace_back(event,arg);return GameFsStatus{};})),"source constructor owns zeroed main-flow states");
        GameMainFlowGate gate;gate.fade_state=2;
        Check(flow.Snapshot().phase1C==0&&flow.Snapshot().return20==0&&flow.Snapshot().substate24==0,"never force mainflow directly to phase5/state4");
        Check(bool(actor.RequestManager())&&bool(flow.Tick(gate))&&flow.Snapshot().phase1C==1,"actual byte8 transitions phase0 to1");
        Check(bool(flow.Tick(gate))&&flow.Snapshot().phase1C==5&&!actor.Snapshot().manager_request8&&!actor.Snapshot().manager_request9,
              "phase1 byte8 branch clears9 then8 before phase5;old9 is cancelled not completed");
        Check(bool(flow.Tick(gate))&&flow.Snapshot().substate24==4&&flow.Snapshot().request9_consumed==0,"real substate0 clear happens before4");
        Check(bool(actor.RequestManager9())&&bool(actor.RequestCamera(1)),"independent later GAME3 andcamera setter admitted");
        gate.extension_busy=true;
        for(unsigned n=0;n<25;++n)Check(bool(flow.Tick(gate)),"busy gate remainspending without quota completion");
        Check(actor.Snapshot().manager_request9&&flow.Snapshot().substate24==4&&flow.Snapshot().request9_consumed==0,"actualbusy gate prevents9clear");
        gate.extension_busy=false;gate.manager_switch_pending=true;
        Check(!flow.Tick(gate)&&actor.Snapshot().manager_request9,"unimplemented realswitch cannotbypassgate orclear9");
        gate.manager_switch_pending=false;
        Check(bool(flow.Tick(gate))&&!actor.Snapshot().manager_request9&&flow.Snapshot().substate24==11
              &&actor.Snapshot().manager_camera_request4C==1,"2365C clearsrequest9 beforecamera requesthasbeenconsumed");
        Check(bool(flow.Tick(gate))&&flow.Snapshot().phase1C==0&&flow.Snapshot().return20==0&&flow.Snapshot().substate24==0
              &&flow.Snapshot().outer_returns==1,"236B0 latch returnsouter0;notLoadMan scenechange");
        gate.loader469=true;const auto returns=flow.Snapshot().outer_returns;
        flow.Input({0,true,false,1});flow.Input({Circle,true,false,2});Check(bool(flow.Tick(gate)),"stable469 blocksfreshCircle");
        Check(events.empty()&&flow.Snapshot().phase1C==0&&flow.Snapshot().outer_returns==returns,"UIclose/poll completion doesnotfabricate469zero or event");
        gate.choice_busy48=true;flow.Input({0,true,false,3});flow.Tick(gate);flow.Input({Cross,true,false,4});flow.Tick(gate);
        Check(events.empty()&&flow.Snapshot().input_suppressed48,"state9 main owner48 suppresses source Cross event12");
        gate.choice_busy48=false;flow.Tick(gate);Check(events.empty(),"held Cross cannot dispatch after SELECT clears48");
        //Separate gate0 fixture checks source input priority,not connected469 proof.
        gate.loader469=false;flow.Input({0,true,false,3});flow.Tick(gate);flow.Input({Circle,true,false,4});
        Check(bool(flow.Tick(gate))&&events==std::vector<std::pair<uint32_t,int32_t>>{{0,0}}&&flow.Snapshot().phase1C==1,
              "sourcephase0 freshCircle dispatches real0/0 thenphase1 evenwhen callbackcanbenoop");
        Check(bool(actor.RequestManager9())&&bool(flow.Tick(gate))&&flow.Snapshot().phase1C==0&&flow.Snapshot().phase1_request9_consumed==1,
              "separatephase1 byte9consumer returns0withoutsubstate11");
        flow.Close();Check(!flow.Input({Circle,true,false,5})&&!flow.Tick(gate),"retiredmainflow refuseslateinput/requests");
        Check(bool(actor.AdvanceActorFrame()),"remainingindependentcamera requeststillowned");
    }
    Check(!actor.Snapshot().manager_request8&&bool(actor.RequestManager())&&actor.Snapshot().manager_request8,
          "zeroed1B188 manager receives actualservice0 request,not completion");
    owner_input.live=false;Check(!actor.WaitReady()&&!actor.BindCamera(0)&&!actor.RequestCamera(1)&&!actor.RequestManager9(),"retired target lease blocks camera/wait/setter");
    actor.Close();fade.Retire();Check(!actor.Snapshot().live,"typed actor/camera owners retired");
    Lease model_input(Dictionary());GameTransition model_fade;Check(model_fade.Initialize({{0,0}}),"model error test fade");
    MissingResources missing;GameActorRuntime model_owner;
    Check(bool(model_owner.Initialize(0,model_input.View(),model_fade,&missing)),"model error owner initialized");
    int32_t failed_slot=0;
    Check(!model_owner.CreateModel(170,0,0,0,failed_slot)&&failed_slot==-1&&missing.calls==1&&model_owner.Snapshot().model_slots==0,
          "real resource failure cannot publish first inactive model slot");
    Check(!model_owner.CreateModel(170,1,0,0,failed_slot)&&missing.calls==1,"unrecovered category refuses before I/O");
    model_owner.Close();model_fade.Retire();
}
void Focused(){
    {
        GameTransition fade;
        Check(!GameMainMenuCanBorrowTransition(fade.Snapshot()),"menu refuses missing transition owner");
        Check(fade.Initialize({{0,0}})&&GameMainMenuCanBorrowTransition(fade.Snapshot()),"Windows menu borrows stationary closed endpoint");
        Check(fade.StartGameOpen()&&!GameMainMenuCanBorrowTransition(fade.Snapshot()),"menu refuses opening transition");
        for(unsigned i=0;i<40;++i)fade.Update();
        Check(fade.Snapshot().state==2&&GameMainMenuCanBorrowTransition(fade.Snapshot()),"menu borrows actual completed open endpoint");
        fade.CloseWipe();
        Check(!GameMainMenuCanBorrowTransition(fade.Snapshot()),"menu refuses closing transition");
        for(unsigned i=0;i<40;++i)fade.Update();
        Check(fade.Snapshot().state==0&&GameMainMenuCanBorrowTransition(fade.Snapshot()),"menu borrows actual inventory-close endpoint without restarting fade");
        fade.Retire();Check(!GameMainMenuCanBorrowTransition(fade.Snapshot()),"menu refuses retired transition owner");
    }
    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameInventoryActionRegressionChecks();
#endif

    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameWordOp55RegressionChecks();
#endif

    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameGreetingRegressionChecks();
#endif

    GameMainMenuRegressionChecks();
    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameMainMenuNilAbiRegressionChecks();
#endif

    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameMenuInventoryRegressionChecks();
#endif

    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameMenuMarkRegressionChecks();
#endif

    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameMenuPlaceRegressionChecks();
#endif

    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameMenuCallbackBoundaryRegressionChecks();
#endif

    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameMenuPlaceScratchRegressionChecks();
#endif

    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameMenuRoomRegressionChecks();
#endif

    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameMenuRenderRetirementRegressionChecks();
#endif

    ::GameNotebookRegressionChecks();
    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameFlow5RegressionChecks();
#endif

    ::GameNotebookSlotProbeRegressionChecks();
    ::GameNotebookSelfRegressionChecks();
    ::GameWordMemoRegressionChecks();
    ::GameDiaryRegressionChecks();
    ::GameSystemOptionRegressionChecks();
    ::GameOptionsOpenRegressionChecks();
    ::GameOptionsRightRegressionChecks();
    ::GameOptionsEditRegressionChecks();
    ::GameOptionsEditCompletionEdgeChecks();
    ::GameOptionsManualBarWorkingPoseChecks();
    ::GameOptionsConfirmRegressionChecks();
    ::GameOptionsYesRegressionChecks();
    ::GameOptionsRow1RegressionChecks();
    ::GameOptionsSe5RegressionChecks();
    GameOptionsVoiceRegressionChecks();
    GameOptionsVoiceSecondQueryChecks();
    GameOptionsSequenceRegressionChecks();
    GameOptionsPortRegressionChecks();
    GameOptionsProcessorRegressionChecks();
    GameOptionsControlRegressionChecks();
    GameOptionsWaveRegressionChecks();
    GameOptionsPort24RegressionChecks();
    {
        GameFontResource font;GameFontBitmap bitmap;GameFontDrawRecord glyphs[2]{};
        Check(bool(font.Initialize(dokodemo::diary_tests::Font())),"Diary spacing fixture FONT");
        Check(bool(font.RasterizeCp932({"A\nB"},8,bitmap,glyphs,2,0xFFFFFFFF,3,1))
              &&glyphs[1].y==3&&bitmap.height==6,"Diary source glyphheight1 plus gap2 newline");
        Check(bool(font.RasterizeCp932({"A\nB"},8,bitmap,glyphs,2))
              &&glyphs[1].y==23&&bitmap.height==46,"Default TRC line advance preserved");
    }
    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameInventoryDecorateRegressionChecks();
#endif

#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameInventoryDiscardRegressionChecks();
#endif
GameKanaRegressionChecks();
    OwnedCtxReferenceBranchChecks();
    AsciiClass70OwnerChecks();
    NilBytePair0DChecks();
    AsciiPrefix71Checks();KanaSuffix72Checks();
    StringLength53Checks();
    BitwiseOrAndWordObservationChecks();
    GameInput32RegressionChecks();
    GameConversationActivationRegressionChecks();
    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameNaturalLoopRegressionChecks();
#endif

    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameOwnedConversationRegressionChecks();
#endif

    
#ifdef DOKODEMO_PRIVATE_SOURCE_FIXTURES
GameInitialCommonRegressionChecks();
#endif

    const std::array<int32_t,10> modes{{INT32_MIN,-1,0,1,2,3,4,5,6,INT32_MAX}};
    const std::array<int32_t,10> selectors{{0,0,0,1,2,3,3,2,0,0}};
    for(size_t n=0;n<modes.size();++n)
        Check(GameActorMotionSelectorA690(modes[n])==selectors[n],"source A690 directional motion selector");
    GameReturnedInteractionRegressionChecks();
    GameChoiceRegressionChecks();
    GameMessageScopeRegressionChecks();
    GamePost21RegressionChecks();
    GameReturnWitnessRegressionChecks();
    GamePost25RegressionChecks();
    {
        auto predicate=Fixture({0x30,1,0x90,0,0,0xB3,0x70,0x38});
        std::vector<script::Value> inputs{script::Value::integer(0),script::Value::integer(1)};
        script::Value nil;nil.kind=script::Value::Kind::Scalar;inputs.push_back(nil);
        script::Value ref;ref.kind=script::Value::Kind::Reference;ref.table=script::Table::Global;inputs.push_back(ref);
        for(size_t i=0;i<inputs.size();++i){script::Vm vm(predicate);vm.start({inputs[i]});
            Check(vm.run().status==script::Status::Complete&&vm.result().word==(i==2?0x10000001u:0x10000000u),
                  "raw B3 tag0 NIL predicate differs from integer0 and typed reference raw0");
            Check(vm.b3_calls()==1&&vm.b3_pc()==5&&vm.b3_operand_word()==inputs[i].word
                &&vm.b3_operand_kind()==uint32_t(inputs[i].kind)&&vm.b3_result_word()==vm.result().word,
                  "B3 direct operand/result evidence retains native kind without fabricating a PSP reference word");
        }
    }
    {auto program=Fixture({0x30,1,0x90,0,0,0x70,0x38});script::Vm strict(program);strict.start();
        Check(strict.run().status==script::Status::Stopped,"standalone probe keeps unspecified frame slot Unknown");
        script::Vm owned(program);owned.native_reserved_local_nil(true);owned.start();
        Check(owned.run().status==script::Status::Complete&&owned.result().word==0,"named owning reserved-local policy initializes optional accumulator to nil");
        script::Vm missing_argument(program);missing_argument.native_reserved_local_nil(true);missing_argument.start({script::Value{}});
        Check(missing_argument.run().status==script::Status::Stopped,"native local policy never rewrites provided Unknown arguments");
    }
    {std::vector<uint32_t> cells(22,0x20000000);cells[0]=0x90000001;cells[1]=0xB0000002;cells[2]=0x90000013;
        auto program=Fixture({0x30,0,0x38},cells);script::Vm typed(program);typed.start();
        auto bytes=Dictionary();bytes.resize(98,0);Word(bytes,8,0x90000013);
        for(uint32_t field=0;field<19;++field)Word(bytes,12+4*field,0x20000000);
        Word(bytes,16,0x60000058);Word(bytes,20,0x6000005D);
        const uint8_t names[]{0x83,0x6C,0x83,0x52,0,0x82,0xCB,0x82,0xB1,0};
        std::copy(std::begin(names),std::end(names),bytes.begin()+88);
        Lease backing(std::move(bytes));GameActorDictionary records;script::Value root;root.kind=script::Value::Kind::Reference;root.table=script::Table::Global;
        Check(bool(records.Attach(backing.View()))&&bool(records.Import(typed,root)),"19-field live record import owns its strings");
        records.Capture(typed);const auto before=records.Snapshot().current_records.at(0);
        const auto row=typed.load_reference(typed.indexed_reference(root,0));
        typed.store_reference(typed.indexed_reference(row,14),script::Value::integer(8));
        typed.store_reference(typed.indexed_reference(row,15),script::Value::integer(16));
        backing.live=false;records.Capture(typed);const auto after=records.Snapshot().current_records.at(0);
        Check(before.numeric==after.numeric&&before.fields14_15==std::array<uint32_t,2>{{0x20000000,0x20000000}}
            &&after.fields14_15==std::array<uint32_t,2>{{0x20000008,0x20000010}}&&after.written==before.written,
            "six diagnostic fields can be unchanged while two live bit fields change after source lease retirement");
    }
    auto choice_program=Fixture({0x30,0,0x8E,'a',0,0x8E,'b',0,0x8B,0x21,2,0x31});
    script::Vm choice_vm(choice_program);Check(choice_vm.start().status==script::Status::Running,"choice fixture entry");
    const auto choice_event=choice_vm.run(20);
    Check(choice_event.status==script::Status::Choice&&choice_vm.choice_waiting()&&choice_vm.display_state()==9
        &&choice_vm.stack().empty()&&choice_vm.event_scope_depth()==1&&choice_vm.result().word==0,"697DC consumes operand span into owned pending state9/nesting,clears old result");
    const auto choice_pc=choice_vm.pc();
    Check(!choice_vm.complete_choice(1)&&choice_vm.pc()==choice_pc,"cannot publish before argument conversion");
    auto choice_args=choice_vm.take_choice_arguments();Check(choice_args.size()==2&&choice_args[0].cp932=="a"&&choice_args[1].cp932=="b","all arguments are options in original order");
    Check(choice_vm.run(20).status==script::Status::Waiting&&choice_vm.pc()==choice_pc,"pending SELECT cannot execute next caller branch");
    Check(!choice_vm.complete_choice(3)&&choice_vm.choice_waiting(),"out of range selection leaves VM pending");
    Check(choice_vm.complete_choice(2)&&choice_vm.result().word==0x20000002&&choice_vm.event_scope_depth()==0
        &&choice_vm.display_state()==2&&!choice_vm.complete_choice(1),"683E8 publishes actual integer2 once and drains owned nesting");
    Check(choice_vm.run(20).status==script::Status::Complete,"caller resumes only after choice result");
    auto complement_program=Fixture({0x30,0,0x80,2,0xB4,0x70,0x31});script::Vm complement(complement_program);complement.start();
    Check(complement.run().status==script::Status::Complete&&complement.result().word==0x2FFFFFFD,"B4 source integer bitwise complement retains tag2,not truth");
    for(const auto sample:std::array<std::array<int32_t,3>,6>{{{{8,1,4}},{{-8,1,-4}},{{-1,31,-1}},{{8,32,8}},{{16,-31,8}},{{-8,0,-8}}}}){
        auto program=Fixture({0x30,2,0x80,9,0x70,0x90,0,0,0x90,1,0,0xE2,0x38});
        script::Vm shifted(program);shifted.start({script::Value::integer(sample[0]),script::Value::integer(sample[1])});
        const auto event=shifted.run();
        Check(event.status==script::Status::Complete&&shifted.stack().size()==1&&shifted.stack().back().signed_integer()==sample[2]
            &&shifted.result().word==0x20000009&&shifted.e2_calls()==1,"E2 signed SRAV/count low5/netSP-1/result30 unchanged");
    }
    {auto program=Fixture({0x30,0,0x8E,'x',0,0x80,1,0xE2,0x38});script::Vm refused(program);refused.start();
        const auto stop=refused.run();Check(stop.status==script::Status::Stopped&&stop.opcode==0xE2&&refused.e2_calls()==0,"E2 unknown owned conversion refuses explicitly,not raw pointer/scalar fallback");}
    for(const auto sample:std::array<std::array<int32_t,3>,4>{{{{7,6,42}},{{-7,6,-42}},{{-7,-6,42}},{{0x07FFFFFF,2,-2}}}}){
        auto program=Fixture({0x30,2,0x80,9,0x70,0x90,0,0,0x90,1,0,0xD0,0x38});script::Vm multiplied(program);
        multiplied.start({script::Value::integer(sample[0]),script::Value::integer(sample[1])});
        Check(multiplied.run().status==script::Status::Complete&&multiplied.stack().size()==1&&multiplied.stack().back().signed_integer()==sample[2]
            &&multiplied.result().word==0x20000009&&multiplied.d0_calls()==1,"D0 low28 signed product/netSP-1/result30 preserved without host overflow");
    }
    {auto program=Fixture({0x30,0,0x80,7,0x84,0xD0,0x38});script::Vm right_nil(program);right_nil.start();
        Check(right_nil.run().status==script::Status::Complete&&right_nil.stack().back().word==0,"D0 RIGHT nil clears LEFT to nil,not integer0");}
    {auto program=Fixture({0x30,0,0x84,0x80,7,0xD0,0x38});script::Vm left_nil(program);left_nil.start();
        Check(left_nil.run().status==script::Status::Complete&&left_nil.stack().back().word==0x20000000,"D0 LEFT nil numeric multiplication produces integer0");}
    {auto program=Fixture({0x30,0,0x8E,'x',0,0x80,1,0xD0,0x38});script::Vm refused(program);refused.start();
        Check(refused.run().status==script::Status::Stopped&&refused.d0_calls()==0,"D0 unknown owned multiplication conversion remains explicit refusal");}
    auto indirect_args_program=Fixture({0x30,0,0x80,7,0x8A,18,0,0,0x80,2,0x80,3,0x22,2,0x89,0xE0,0x70,0x31,
        0x30,2,0x90,0,0,0x90,1,0,0xE0,0x70,0x31});
    script::Vm indirect_args(indirect_args_program);indirect_args.start();
    Check(indirect_args.run().status==script::Status::Complete&&indirect_args.result().word==0x2000000C,
        "22 target below two arguments preserves caller7 and actual callee2+3,then returns12");
    GameRecordStore diary;GameRecordInput original_record{12,"\x83\x67\x83\x8D"};
    const GameRecordDate record_date{{2026,10,24}};
    Check(!diary.Capture71(record_date,&original_record),"uninitialized static-bank native owner is not invented");
    diary.ResetCold249D0();
    for(uint32_t bank=0;bank<11;++bank)for(uint32_t row=0;row<28;++row){const auto& slot=diary.At(bank,row);
        Check(slot.flag==0&&slot.value==-1&&slot.date==GameRecordDate{{-1,-1,-1}}
              &&std::all_of(slot.payload.begin(),slot.payload.end(),[](uint8_t b){return b==0;}),"39B80 exact cold bank producers");}
    Check(GameRecordDateFromClockWord(0x30000000)==GameRecordDate{{2000,1,1}}
          &&GameRecordDateFromClockWord(0x30D72E15)==record_date,"same-live-clock current date is not old profile date");
    Check(bool(diary.Capture71(record_date,&original_record))&&diary.Snapshot().selected==0,"first empty record captures day");
    const auto owned_record=diary.At(0,0);original_record.text="changed temporary";
    Check(owned_record.payload==diary.At(0,0).payload&&owned_record.payload[0]==0x83&&owned_record.source_known_payload_bytes==5,"saved record owns CP932 prefix independent of input string");
    diary.At(0,0).flag=1;const auto protected_record=diary.At(0,0);
    Check(bool(diary.Capture71(record_date,&original_record))&&diary.Snapshot().last_protected
        &&diary.At(0,0).payload==protected_record.payload&&diary.At(0,0).date==protected_record.date&&diary.At(0,0).value==12,"negative-index protected flag1 has no date/value/text mutation");
    diary.At(0,0).flag=2;Check(bool(diary.Capture71(record_date,&original_record))&&diary.At(0,0).flag==0&&diary.Snapshot().selected==0,"only exactlyflag1 protects and same-day record is replaced");
    Check(bool(diary.Capture71({{2026,10,25}},&original_record))&&diary.Snapshot().selected==1,"next date chooses next empty row");
    diary.ResetCold249D0();diary.At(0,0).payload.fill(0xAB);
    Check(bool(diary.Capture71(record_date,nullptr))&&diary.At(0,0).value==-1&&diary.At(0,0).date==record_date
          &&diary.At(0,0).payload[199]==0xAB&&!diary.Snapshot().last_copied,"missing registry still stampsdate/ID but preserves old200B");
    GameRecordInput long_record{17,std::string(199,'x')+"\x83\x67"};
    Check(bool(diary.Capture71(record_date,&long_record))&&diary.At(0,0).payload[199]==0x83
          &&diary.At(0,0).source_known_payload_bytes==200,"fixed200B copy can end on CP932 lead;no invented terminator or boundary trim");
    for(uint32_t row=0;row<28;++row){diary.At(0,row).value=4;diary.At(0,row).date={{2001,1,1}};}
    Check(!diary.Capture71(record_date,&original_record)&&diary.Snapshot().capacity_errors==1
          &&diary.At(0,27).value==4&&diary.At(0,27).date==GameRecordDate{{2001,1,1}},"full source-OOB corner is explicit native capacity error,not replacement success");
    diary.Close();Check(!diary.Capture71(record_date,&original_record),"retired record bank rejects late writes");
    {auto registry_program=Fixture({0x30,0,0x38});std::vector<std::unique_ptr<script::Vm>> owners;
        for(uint32_t i=0;i<8;++i){auto vm=std::make_unique<script::Vm>(registry_program);vm->start();Check(vm->context_value().word==0x80000000u+i,"eight distinct live source context identities");owners.push_back(std::move(vm));}
        script::Vm ninth(registry_program);ninth.start();bool refused=false;try{(void)ninth.context_value();}catch(const std::exception&){refused=true;}
        Check(refused,"registry exhaustion explicit,not context0 fallback");}
    // Source27/32 owns a saved event envelope,not an ordinary return frame.
    auto modal_program=Fixture({0x30,0,0x80,9,0x70,0x80,1,0x44,0x80,7,0x70,0x38,0x30,3,0x3A,0x32});
    script::Vm modal(modal_program);modal.clock_tick(10);modal.start();
    Check(modal.run().status==script::Status::Yield&&modal.result().signed_integer()==9,"modal original result/deadline");
    const auto token=modal.context_value();script::Value callback;callback.kind=script::Value::Kind::Code;callback.word=0x7000000C;
    Check(modal.post_callback(token,callback,script::Value::string("owned"),script::Value::integer(8))==0,"live typed registry callback admitted");
    modal.clock_tick(12); //event time must not consume the saved signed23 wait
    Check(modal.run().status==script::Status::Yield&&!modal.context_enabled()&&modal.callbacks_returned()==1
          &&modal.pc()==8&&modal.result().signed_integer()==9,"3A32 restores result,PC,deadline and disables context");
    modal.clock_tick(20);const auto frozen=modal.executed();Check(modal.run().status==script::Status::Waiting&&modal.executed()==frozen,"clock doesnot wake disabled modal context");
    modal.resume_modal_context();Check(modal.run().status==script::Status::Complete&&modal.result().signed_integer()==7,"explicit close-policy resume preserves real continuation");
    script::Value retired_context;
    {script::Vm owner(modal_program);owner.start();retired_context=owner.context_value();}
    Check(modal.post_callback(retired_context,callback,{}, {})==-1,"retired registry token cannot alias reused slot");
    auto queued_program=Fixture({0x30,0,0x8B,0x27,0,0x80,1,0x44,0x38,0x30,3,0x3A,0x32});
    script::Vm queued(queued_program);queued.start();
    queued.host_service([&](uint8_t,const std::vector<script::Value>&){auto c=callback;c.word=0x70000009;
        Check(queued.post_callback(queued.context_value(),c,script::Value::string("copied"),script::Value::integer(0))==0,"callback enqueued inside interpreter scope");
        Check(queued.callbacks_returned()==0,"queued event cannot execute inside builtin argument cleanup");return script::Value::integer(42);});
    const auto queued_first=queued.run();if(queued_first.status!=script::Status::Yield)std::cerr<<"queue first status="<<int(queued_first.status)<<" "<<queued_first.diagnostic.text()<<'\n';
    Check(queued_first.status==script::Status::Yield&&queued.callbacks_returned()==0,"queued callback starts only after interpreter scope drains");
    Check(queued.run().status==script::Status::Yield&&queued.callbacks_returned()==1&&queued.result().signed_integer()==42&&!queued.context_enabled(),"queued callback restores builtin result ownership");
    queued.resume_modal_context();queued.clock_tick(2);Check(queued.run().status==script::Status::Complete,"queue return resumes original deadline");
    for(const uint8_t event_return:{uint8_t(0x32),uint8_t(0x1C)}){
    auto owner_program=Fixture({0x30,0,0x80,9,0x70,0x80,5,0x44,0x38,0x30,2,0x90,0,0,0x90,1,0,0x8B,0x7F,2,event_return});
    script::Vm owner_event(owner_program);owner_event.clock_tick(10);owner_event.start();
    Check(owner_event.run().status==script::Status::Yield,"owner-event original deadline entered");
    Check(owner_event.context_value().word>>28==8,"owner event has actual admitted context registry identity");
    auto owner_code=callback;owner_code.word=0x70000009;bool two_owner_args=false;
    owner_event.host_service([&](uint8_t id,const std::vector<script::Value>& args){
        bool absent=false;try{(void)owner_event.read_slot(script::Table::Frame,2);}catch(const std::exception&){absent=true;}
        two_owner_args=id==0x7F&&args.size()==2&&args[0].signed_integer()==12&&args[1].signed_integer()==34&&absent;
        return script::Value::string("temporary event result");});
    const auto prior_pc=owner_event.pc(),prior_calls=owner_event.call_depth();const auto prior_deadline=owner_event.deadline_tick();
    Check(owner_event.post_owner_event({},0,0)==1&&owner_event.pc()==prior_pc&&owner_event.deadline_tick()==prior_deadline,
          "non-code Local0 is genuine no-callback return1 without changing caller");
    Check(owner_event.post_owner_event(owner_code,12,34)==0,"internal event admitted without third callerID argument");
    owner_event.clock_tick(12);Check(owner_event.run().status==script::Status::Yield&&two_owner_args
        &&owner_event.callbacks_returned()==1&&owner_event.context_enabled()&&owner_event.pc()==prior_pc
        &&owner_event.call_depth()==prior_calls&&owner_event.deadline_tick()==17&&owner_event.result().signed_integer()==9,
        "two-arg32 return restores actual result/frame/PC and relative deadline,not forced success or early timer wake");
    owner_event.clock_tick(17);Check(owner_event.run().status==script::Status::Complete,"owner event resumes only original deadline continuation");
    }
    auto history_program=Fixture({0x30,0,0x95,0,0,0xF6,3,4,0,0x70,0x38},
                                {0x90000003,0x20000001,0x20000002,0x20000003,0x90004002,0x20000001,0x20000002});
    script::Vm history(history_program);uint32_t random_calls=0;history.random_source([&]{++random_calls;return 0u;});history.start();
    Check(history.run().status==script::Status::Complete&&history.result().signed_integer()==3&&random_calls==1
          &&history.read_slot(script::Table::Global,4).word==0x90000002&&history.read_slot(script::Table::Global,6).word==0x20000003,
          "F6 excludes actual scalar history,rotates packed cursor and owns selected value");
    auto code_history_program=Fixture({0x30,0,0x95,0,0,0xF6,3,3,0,0x70,0x38,0x30,0,0x31},
                                     {0x90000002,0x70000000,0x7000000B,0x90000001,0x70000000});
    script::Vm code_history(code_history_program);code_history.random_source([]{return 0u;});code_history.start();
    Check(code_history.run().status==script::Status::Complete&&code_history.result().kind==script::Value::Kind::Code
          &&code_history.result().word==0x7000000B&&code_history.read_slot(script::Table::Global,4).word==0x7000000B,
          "F6 actual function choices retain tag7/rawPC and exclude prior callback by payload");
    auto exhausted_history=Fixture({0x30,0,0x95,0,0,0xF6,3,2,0,0x70,0x38},{0x90000001,0x20000007,0x90000001,0x20000007});
    script::Vm exhausted(exhausted_history);exhausted.random_source([]{return 0u;});exhausted.start();
    Check(exhausted.run().status==script::Status::Complete&&exhausted.result().signed_integer()==7,"F6 all-excluded falls back to whole source list ratherthan empty/success stub");
    auto malformed_history=Fixture({0x30,0,0x95,0,0,0xF6,3,2,0,0x70,0x38},{0x90000001,0x20000007,0x90004001,0});
    script::Vm invalid_history(malformed_history);invalid_history.random_source([]{return 0u;});invalid_history.start();
    Check(invalid_history.run().status==script::Status::Stopped,"F6 invalid cursor refuses before history mutation");
    const auto replace=[](std::string source,std::string needle,std::string value){return GameStringReplace51({script::Value::string(source),script::Value::string(needle),script::Value::string(value)});};
    Check(replace("aaaaa","aa","b").cp932=="bba","51 advances past nonoverlapping source match");
    Check(replace("x/x","x","xx").cp932=="xx/xx","51 doesnot search newly inserted replacement");
    Check(replace("no match","absent","ignored").cp932=="no match"&&replace("delete","delete","").cp932.empty(),"51 owned unchanged and deletion results");
    const std::string cp932="\x83\x67\x83\x8D";
    script::Value source_nil;source_nil.kind=script::Value::Kind::Scalar;
    Check(GameStringReplace51({script::Value::string("$_OME"),script::Value::string("$_OME"),source_nil}).cp932=="\x83\x73\x81\x5B","75778 actual nil uses retained CP932 redaction string,not empty or identity");
    auto owned=replace("$_WORD/$_WORD","$_WORD",cp932);Check(owned.word>>28==6&&owned.cp932==cp932+"/"+cp932,"51 preserves raw multibyte replacement and owned tag");
    bool empty_refused=false;try{(void)replace("text","","value");}catch(const std::exception&){empty_refused=true;}
    Check(empty_refused,"51 explicit empty-needle trap refused,not identity success");
    bool budget_refused=false;try{(void)replace(std::string(1024*1024,'x'),"x","xx");}catch(const std::exception&){budget_refused=true;}
    Check(budget_refused&&owned.cp932==cp932+"/"+cp932,"51 allocation bound error preserves other owned result");
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
    const auto import_context=vm.context_value();const auto import_owner=vm.current_witness();
    Check(dictionary.ImportedRecordCount()==1&&dictionary.ImportedRecordsInRoot(vm,root)==1,"imported record matches its actual root");
    auto unsupported_root=root;unsupported_root.table=script::Table::Frame;
    Check(!dictionary.ImportedRecordsInRoot(vm,unsupported_root),"import proof shares the Global-only consumer domain");
    unsupported_root=root;unsupported_root.context_owner=import_context.context_owner;
    Check(!dictionary.ImportedRecordsInRoot(vm,unsupported_root),"borrowed Global container cannot replace current import owner");
    auto alternate=root;alternate.index=14;auto slot=root;slot.index=15;
    const auto old_head=vm.peek_reference(alternate),old_slot=vm.peek_reference(slot);
    auto imported_slot=root;imported_slot.index=1;const auto imported_ref=vm.peek_reference(imported_slot);
    script::Value one_header;one_header.kind=script::Value::Kind::ArrayHeader;one_header.word=0x90000001u;
    vm.store_reference(alternate,one_header);vm.store_reference(slot,imported_ref);
    Check(dictionary.ImportedRecordsInRoot(vm,alternate)==1,"different container with exact imported record aliases is admitted");
    auto foreign_ref=imported_ref;foreign_ref.context_owner=import_context.context_owner;vm.store_reference(slot,foreign_ref);
    Check(!dictionary.ImportedRecordsInRoot(vm,alternate),"same numeric record with different weak owner refuses");
    vm.store_reference(slot,script::Value::integer(0));Check(!dictionary.ImportedRecordsInRoot(vm,alternate),"missing imported row refuses");
    vm.store_reference(slot,imported_ref);auto duplicate=root;duplicate.index=6;
    std::array<script::Value,3> old_duplicate;for(uint32_t i=0;i<3;++i){auto at=duplicate;at.index=uint16_t(6+i);old_duplicate[i]=vm.peek_reference(at);}
    auto two_header=one_header;two_header.word=0x90000002u;vm.store_reference(duplicate,two_header);
    for(uint32_t i=1;i<3;++i){auto at=duplicate;at.index=uint16_t(6+i);vm.store_reference(at,imported_ref);}
    Check(!dictionary.ImportedRecordsInRoot(vm,duplicate),"duplicate claims cannot replace missing imported owners");
    for(uint32_t i=0;i<3;++i){auto at=duplicate;at.index=uint16_t(6+i);vm.store_reference(at,old_duplicate[i]);}
    vm.store_reference(alternate,old_head);vm.store_reference(slot,old_slot);
    Check(dictionary.ImportedRecordsInRoot(vm,root)==1&&dokodemo::notebook_tests::SameVm(import_owner,vm.current_witness()),"alias proof is passive and caller state unchanged");
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
    const auto single=GameValueArray99(vm,{record,condition});
    Check(single.selected==1&&single.rows==1&&single.predicates==1&&single.writes==0,
          "99 tests one owned object without mutation, not a container row scan");
    vm.store_reference(needle,script::Value::string("absent"));
    Check(GameValueArray98(vm,{root,condition,actions}).selected==0,"no-match result,no invented selection");
    Check(GameValueArray99(vm,{record,condition}).selected==0,"99 false predicate returns false");
    // Actual reached two-triple shape: field8 mask8 and field7 equality32768.
    auto p99=Fixture({0x30,0,0x38},{0x90000009,0,0,0,0,0,0,0,0x20008000,0x20000020,
                    0x90000006,0x20000008,0x20000006,0x20000008,0x20000007,0x20000000,0x20008000,
                    0x90000000,0x90000002,0x20000000,0x20000000});
    script::Vm v99(p99);Check(v99.start().status==script::Status::Running,"99 fixture starts");
    auto object=root,conditions=root;conditions.index=10;
    auto m99=GameValueArray99(v99,{object,conditions});
    Check(!m99.selected&&m99.predicates==1&&!m99.writes,"99 actual mask32&8 rejects and short-circuits");
    v99.store_reference(v99.indexed_reference(object,8),script::Value::integer(8));
    m99=GameValueArray99(v99,{object,conditions});
    Check(m99.selected==1&&m99.predicates==2&&!m99.writes,"99 ordered AND admits both true predicates");
    v99.store_reference(v99.indexed_reference(object,7),script::Value::integer(0));
    Check(!GameValueArray99(v99,{object,conditions}).selected,"99 later predicate cannot be skipped");
    auto empty=root;empty.index=17;Check(!GameValueArray99(v99,{object,empty}).selected,"99 empty predicate list is false");
    auto malformed=root;malformed.index=18;bool bad99=false;
    try{(void)GameValueArray99(v99,{object,malformed});}catch(const std::exception&){bad99=true;}
    Check(bad99,"99 malformed triple extent refuses");
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
    auto indirect_program=Fixture({0x30,0,0x8A,8,0,0,0x23,0x38,0x30,0,0x80,7,0x70,0x31});
    script::Vm indirect(indirect_program);indirect.start();Check(indirect.run().status==script::Status::Complete&&indirect.result().signed_integer()==7,
        "source23 tag7 call and returnPC continue caller without invented result");
    auto operand_call=Fixture({0x30,0,0x80,11,0x8A,11,0,0,0x23,0x70,0x38,0x30,0,0x80,7,0x70,0x31});
    script::Vm retained_operand(operand_call);retained_operand.start();
    Check(retained_operand.run().status==script::Status::Complete&&retained_operand.result().signed_integer()==11,
          "23 preserves caller operands below returnPC;callee result doesnot consume caller stack");
    auto bad_indirect_program=Fixture({0x30,0,0x80,8,0x23,0x38});script::Vm bad_indirect(bad_indirect_program);bad_indirect.start();
    Check(bad_indirect.run().status==script::Status::Stopped,"numeric rawaddress cannot replace source23 tag7 callee");
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
    RuntimeOwners();
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
    auto elapsed_program=Fixture({0x30,0,0x80,3,0x41,0x8D,0x70,0x38});script::Vm elapsed(elapsed_program);
    elapsed.clock_tick(10);elapsed.start();Check(elapsed.run().status==script::Status::Yield,"source41 writes timestamp13 under named native slice adapter");
    elapsed.clock_tick(14);Check(elapsed.run().status==script::Status::Complete&&elapsed.result().word==0x20000001,"8D reads elapsed tick from actual ctx18,not fixedzero");
    auto addition=Fixture({0x30,0,0x80,0xFE,0x80,3,0xE0,0x70,0x38});script::Vm add(addition);add.start();
    Check(add.run().status==script::Status::Complete&&add.result().signed_integer()==1,"E0 signed scalar addition");
    auto nil_addition=Fixture({0x30,0,0x87,0x84,0xE0,0x70,0x38});script::Vm nil_add(nil_addition);nil_add.start();
    Check(nil_add.run().status==script::Status::Complete&&nil_add.result().word==0x10000001,"E0 RHS nil preserves Boolean type");
    auto raw_not=Fixture({0x30,0,0x80,0xFF,0xB1,0x70,0x38});script::Vm negate(raw_not);negate.start();
    Check(negate.run().status==script::Status::Complete&&negate.result().word==0x10000000,"B1 tests raw nonzero payload of negative integer");
}
void Selected(const char* path,bool with_main_flow=false,bool after17=false,bool after19=false){
    // Optional new-engine diagnostic,not connected-game proof. Exactly the
    // already source-selected package is read;no extraction or ISO bootstrap.
    std::ifstream f(path,std::ios::binary);Check(bool(f),"DATA.BP open");
    std::vector<uint8_t> package(1058560);f.seekg(34031616);f.read(reinterpret_cast<char*>(package.data()),package.size());
    Check(bool(f),"selected package full read");Lease lease(std::move(package));
    const auto directory=BpDirectory::Parse(lease.View(),lease.bytes.size());
    if(after19){
        const auto image=directory.Open("NEKO.KSC");
        std::cout<<"ROOT_KSC_HEADER ";for(uint32_t i=0;i<28;++i){const auto byte=image.ReadU8(i);std::cout<<"0123456789abcdef"[byte>>4]<<"0123456789abcdef"[byte&15];}std::cout<<'\n';
        for(const auto window:std::array<std::pair<uint32_t,uint32_t>,7>{{{0x1820,0x140},{0xA7D0,0x160},{0xABB0,0xC0},{0x84E80,0xC0},{0xAC6F,384},{0xAB00,176},{0x85F0,384}}}){
            std::cout<<"ROOT_KSC_WINDOW pc="<<std::hex<<window.first<<std::dec<<" bytes="<<window.second<<" hex=";
            for(uint32_t i=0;i<window.second;++i){const auto byte=image.ReadU8(28+window.first+i);std::cout<<"0123456789abcdef"[byte>>4]<<"0123456789abcdef"[byte&15];}std::cout<<'\n';
        }
    }
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
    uint32_t fixture_clock=0x30D72E10u;bool fixture_advanced=false;
    GameRecordStore records;records.ResetCold249D0();
    std::ifstream sound_file(std::filesystem::path(path).parent_path()/"sound/se/SE_SYS.sgd",std::ios::binary|std::ios::ate);
    Check(bool(sound_file),"actual selected SE_SYS bank open");const auto sound_size=size_t(sound_file.tellg());
    std::vector<uint8_t> sound_bytes(sound_size);sound_file.seekg(0);sound_file.read(reinterpret_cast<char*>(sound_bytes.data()),sound_bytes.size());Check(bool(sound_file),"selected SE_SYS bounded full file");
    Lease sound_lease(std::move(sound_bytes));GameSoundBank sound;Check(bool(sound.Register(sound_lease.View())),"actual selected sound metadata");
    GameActorScript game;const auto opened=game.Initialize(directory.Open("neko.dic"),directory.Open("NEKO.KSC"),"KSNEKO\\NEKO.KSC",profile,[&]{return fixture_clock;},&rng,&runtime,&records,&sound); // explicit diagnostic clock fixture
    Check(bool(opened),opened.detail.c_str());
    GameMainFlow main_flow;bool selected_loader469=false;
    if(with_main_flow)Check(bool(main_flow.Initialize(runtime,[&](uint32_t id,int32_t arg){return game.PostMainFlowEvent(id,arg);})),"mainflow from genuine constructor beforefirstGAME0");
    const auto tick_game=[&]()->GameFsStatus{
        const auto before=game.Snapshot();
        if(with_main_flow&&!before.inventory_requested){
            GameMainFlowGate gate;gate.extension_busy=before.awaiting_message;gate.loader469=selected_loader469;gate.fade_state=transition.Snapshot().state;
            const auto status=main_flow.Tick(gate);if(!status)return status;
        }
        return game.Tick();
    };
    for(unsigned i=0;i<600;++i){const auto status=tick_game();const auto s=game.Snapshot();
        if(i>200&&!fixture_advanced&&s.runtime.intro_requests){fixture_clock+=5;fixture_advanced=true;std::cout<<"Explicit fixture advances calendar5minutes;NOT connected realclock evidence\n";}
        if(i<6||i%20==0||!status)
        std::cout<<"tick="<<s.ticks<<" pc="<<s.pc<<" op="<<s.last_opcode<<" steps="<<s.instructions
                 <<" imported="<<s.dictionary.imported<<" service="<<s.last_service<<" command="<<s.runtime.command_pc
                 <<" motion="<<s.runtime.channel_motion[0]<<" time="<<s.runtime.channels[0].time<<" pair3="<<s.runtime.scene_pair3_calls
                 <<" signals="<<s.event_signals<<" requested="<<s.runtime.requested_command<<" period="<<s.scene_period<<" detail="<<status.detail<<'\n';
        if(!status||s.awaiting_message||s.completed)break;
    }
    const auto s=game.Snapshot();std::cout<<"services=";for(auto id:s.service_ids)std::cout<<id<<',';std::cout<<'\n';
    std::cout<<"intro_args="<<std::hex<<s.intro_caller_words[0]<<','<<s.intro_caller_words[1]<<','<<s.intro_caller_words[2]<<std::dec<<" message="<<s.message<<" cameraFov="<<s.runtime.camera_fov_degrees<<" repeats="<<s.runtime.command_loop_repeats<<'\n';
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
    Check(!s.blocked&&s.awaiting_message&&s.messages==1&&s.camera_setter_returns==1
          &&s.camera_setter_result_word==0x10000000&&s.runtime.camera_completions==1
          &&s.runtime.camera_manager_state28==0&&s.runtime.camera_fov_degrees==19.5f
          &&s.runtime.intro_requests==1&&s.runtime.intro_closes==1&&!s.runtime.command_blocked,
          "actual setter returnsFalse,independentcamera completes,realintro andcallback reach KSC message");
    Check(s.event_repeat_signals>0&&s.runtime.command_loop_repeats>0,
          "ASC60 ready258 doesnot synthesize unreachable8031 or8000;live83 clip really completed first");
    Check(s.local_config_writes==1&&s.message_local_words==std::array<uint32_t,5>{{0x20000000,0x20000009,0x20000004,0x20000064,0x20000064}},
          "actualbuiltin35 fiveLocalwords feedrealtextownerconfiguration");
    for(unsigned i=0;i<30;++i)Check(bool(tick_game()),"realowners pump while messagewaits");
    auto held=game.Snapshot();Check(held.messages==1&&held.awaiting_message&&held.instructions==s.instructions
        &&held.runtime.actor_phases>s.runtime.actor_phases&&held.native_clock_ticks>s.native_clock_ticks,
        "messagewait cannot autoack and doesnot freeze actor/globalclockowners");
    Check(game.AcknowledgeMessage(),"explicit selected-data messageacknowledgement");
    for(unsigned i=0;i<600;++i){Check(bool(tick_game()),"actualcallback continues aftermessageack");if(game.Snapshot().messages==2)break;}
    const auto next=game.Snapshot();Check(next.messages==2&&next.awaiting_message&&next.message_acknowledgements==1&&!next.blocked,
        "actualKSC nextmessage/result afteroneack,not oldGAME44 failure oracle");
    const std::vector<uint32_t> message_pcs{0x7BC93,0x7BCB0,0x7BCDE,0x7BCF3,0x7BD1D,0x7BD42,0x7BD69,0x7BD95,0x7BDBC,0x7BEF0,0x7BF0F,0x89297,0x892A7,0x892C1,0x892E3};
    const std::vector<uint32_t> actor_commands{42,42,60,60,39,39,38,43,43,42,35,20,42,42,38};
    const auto second_steps=next.instructions;
    for(unsigned i=0;i<12;++i)Check(bool(tick_game()),"second context pumps live owners without input");
    Check(game.Snapshot().messages==2&&game.Snapshot().instructions==second_steps,"no hidden acknowledgement of second message");
    //Exactly thirteen separately requested diagnostic edges,one per known
    //actual context;this loop belongs to the test,never normal gameplay.
    for(unsigned message=2;message<15;++message){
        Check(game.Snapshot().awaiting_message&&game.Snapshot().messages==message,"separate actual message context");
        Check(game.AcknowledgeMessage()&&!game.AcknowledgeMessage(),"one edge cannot acknowledge a second context before it exists");
        for(unsigned i=0;i<2000;++i){const auto status=tick_game();Check(bool(status),status.detail.c_str());if(game.Snapshot().awaiting_message)break;}
        const auto current=game.Snapshot();
        Check(current.awaiting_message&&current.messages==message+1&&current.message_acknowledgements==message,"bounded actual conversation continues after fresh edge");
        Check(current.pc==message_pcs.at(message)&&current.runtime.requested_command==int32_t(actor_commands.at(message)),"actual callback branch and source-selected actor command");
    }
    const auto gift=game.Snapshot();
    Check(gift.message_pcs==message_pcs&&gift.message_actor_commands==actor_commands,"all actual messages retain ordered PCs and actor consumers");
    Check(gift.string_replace_calls==2&&gift.replacement_results.size()==2
          &&gift.replacement_results[0]==std::string("Toro\x82\xCC\x83\x56\x81\x5B\x83\x8B")
          &&gift.last_replacement.find("$_WORD")==std::string::npos&&gift.last_replacement.find("Toro")!=std::string::npos,
          "real51 result is owned,dynamically expanded into item record,not a forced builtin return");
    Check(!gift.blocked&&!gift.runtime.command_blocked&&gift.call_depth>0&&gift.message_acknowledgements==14,
          "introduction callback returns into actual gift function with live frame and input wait");
    std::cout<<"actual conversation15 messages14 acknowledgements,51results="<<gift.string_replace_calls<<" depth="<<gift.call_depth<<" PASS\n";
    Check(game.AcknowledgeMessage(),"inventory probe separate acknowledgement15");
    for(unsigned i=0;i<600;++i){auto status=tick_game();const auto probe=game.Snapshot();
        if(!status||(!probe.context_enabled&&probe.callbacks_returned)){
            std::cout<<"inventory callback pc="<<std::hex<<probe.pc<<std::dec<<" requests="<<probe.inventory_requests<<" enabled="<<probe.context_enabled<<" returned="<<probe.callbacks_returned<<" detail="<<status.detail<<'\n';break;
        }
        if(!status||probe.awaiting_message)break;
    }
    const auto requested=game.Snapshot();
    Check(!requested.blocked&&requested.inventory_requests==1&&requested.inventory_setter_result_word==0x10000000
          &&requested.inventory_items.size()==170&&!requested.context_enabled&&requested.callbacks_returned==1,"actual41False and27/32 suspend after ack15");
    GameInventory inventory;Check(bool(inventory.Request(requested.inventory_items)),"actual registered item rows request");
    Check(bool(game.BindScriptInventory(inventory.Snapshot().generation,requested.inventory_requests)),"bind actual script request separately from GUI generation");
    Check(bool(inventory.Tick(resources,transition)),"actual MOCHIMONO assets initialized");
    Check(inventory.Snapshot().stateB2C==0&&inventory.Snapshot().loader468==0,"constructor3 reset to open0 cannot complete");
    for(unsigned i=0;i<80&&inventory.Snapshot().phase!=3;++i){transition.Update();Check(bool(inventory.Tick(resources,transition)),"real open fade pending");}
    auto gui=inventory.Snapshot();Check(gui.phase==3&&gui.stateB2C==1&&gui.loader469==1&&!gui.loader468
        &&gui.selected_row==0&&gui.selected_id==27&&gui.selected_name==gift.replacement_results[0],"live source list identity/FONT text from registered cells");
    uint64_t sequence=0;
    const auto input=[&](uint32_t buttons,bool focused=true){GameInputFrame frame;frame.buttons=buttons;frame.focused=focused;frame.sequence=++sequence;
        inventory.Input(frame);Check(bool(inventory.Tick(resources,transition)),"inventory real input phase");};
    input(Cross);Check(inventory.Snapshot().phase==3,"held button at newcontext cannot close");
    input(0);input(Right);Check(inventory.Snapshot().filter==2&&inventory.Snapshot().count==0,"source filter0 empty list safe cursor");
    input(0);input(Right);Check(inventory.Snapshot().filter==3&&inventory.Snapshot().count==1,"category1 restores actual row");
    input(0,false);input(Circle,false);input(Circle,true);Check(!inventory.Snapshot().actions,"unfocused and inherited refocus edge cannot open actions");
    input(0);input(Circle);Check(inventory.Snapshot().actions&&inventory.Snapshot().action_opens==1,"Circle opens SELECT not inventory completion");
    input(Cross);Check(inventory.Snapshot().actions,"new SELECT rejects inherited held edge");
    input(0);input(Cross);Check(!inventory.Snapshot().actions&&inventory.Snapshot().action_cancels==1&&inventory.Snapshot().phase==3,"genuine SELECT cancel leaves inventory live and items unchanged");
    input(0);input(Circle);for(unsigned i=0;i<4;++i){input(0);input(Down);}input(0);input(Circle);
    Check(!inventory.Snapshot().actions&&inventory.Snapshot().select_result==5&&inventory.Snapshot().action_cancels==2,"actual fifth cancel label yields result5 without script item event");
    input(0);input(Cross);Check(inventory.Snapshot().stateB2C==2&&inventory.Snapshot().loader468==0,"close request remains pending despite stable poll1");
    for(unsigned i=0;i<4;++i)Check(bool(inventory.Tick(resources,transition)),"without fade progress cannot complete");
    Check(inventory.Snapshot().stateB2C==2&&!inventory.Snapshot().loader468,"no frame quota auto-completion");
    for(unsigned i=0;i<100&&inventory.Snapshot().phase!=6;++i){transition.Update();Check(bool(inventory.Tick(resources,transition)),"actual fade close to poll completion");}
    gui=inventory.Snapshot();Check(gui.phase==6&&gui.stateB2C==3&&gui.loader468==1&&gui.loader469==1&&!gui.request14&&gui.loader498==0&&gui.live,"completed poll distinct from deferred retirement");
    Check(!game.CompleteInventory(gui),"VM cannot resume before deferred destructor drains");
    Check(bool(inventory.Tick(resources,transition))&&inventory.ClosedAndRetired(),"deferred GUI destructor releases resource owners");
    Check(!inventory.Input({Cross,true,false,++sequence}),"retired GUI rejects late input");
    Check(bool(game.CompleteInventory(inventory.Snapshot())),"native close-only modal wake");selected_loader469=true;
    for(unsigned i=0;i<600;++i){const auto status=tick_game();const auto after=game.Snapshot();
        if(!status||after.awaiting_message){std::cout<<"after inventory pc="<<std::hex<<after.pc<<std::dec<<" messages="<<after.messages<<" ack="<<after.message_acknowledgements<<" service="<<after.last_service<<" detail="<<status.detail<<'\n';break;}}
    const auto after=game.Snapshot();Check(!after.blocked&&after.context_enabled&&after.inventory_completions==1
        &&after.game71_calls==1&&after.game71_result_word==0x10000000&&after.awaiting_message&&after.messages==16,
        "actual71 side effects then real frame return reaches new text consumer");
    const auto saved=records.Snapshot();
    Check(saved.live&&saved.writes==1&&saved.selected==0&&saved.bank0[0].value==12
        &&saved.bank0[0].date==GameRecordDate{{2026,10,24}}&&saved.bank0[0].date!=GameRecordDate{{2026,10,21}}
        &&after.record_cell0_writes==1&&after.record_cell0_before==after.record_cell0_after&&after.record_cell0_after==0,
        "actualGAME29 record/currentclock date distinct from confirmed profile;cell0 is destination byte");
    const auto& input_text=after.record_input_text;Check(input_text.size()==116&&std::equal(input_text.begin(),input_text.end(),saved.bank0[0].payload.begin(),[](char a,uint8_t b){return uint8_t(a)==b;}),"owned exact diary prefix survives temporary frame return");
    const auto held_steps=after.instructions;for(unsigned i=0;i<15;++i)Check(bool(tick_game()),"new message retains independent owners");
    Check(game.Snapshot().messages==16&&game.Snapshot().instructions==held_steps,"new text cannot autoack after inventory close");
    Check(game.AcknowledgeMessage(),"new message16 separate fresh acknowledgement");
    for(unsigned i=0;i<2000;++i){const auto status=tick_game();const auto state=game.Snapshot();
        if(!status||state.awaiting_message||(state.game3_calls==1&&state.runtime.camera_completions==2&&state.last_service==28&&state.waiting_deadline)){std::cout<<"post71 next pc="<<std::hex<<state.pc<<std::dec<<" messages="<<state.messages<<" ack="<<state.message_acknowledgements<<" cmd="<<state.runtime.requested_command<<" text=";
            for(unsigned char b:state.message)std::cout<<"0123456789abcdef"[b>>4]<<"0123456789abcdef"[b&15];std::cout<<" detail="<<status.detail<<'\n';break;}}
    {const auto tail=game.Snapshot();std::cout<<"post71 tail pc="<<std::hex<<tail.pc<<std::dec<<" service="<<tail.last_service<<" messages="<<tail.messages<<" complete="<<tail.completed<<" wait="<<tail.waiting_for_event<<" deadline="<<tail.waiting_deadline<<" field58="<<tail.runtime.source_field58<<" req9="<<tail.runtime.manager_request9<<" camera="<<tail.runtime.camera_completions<<" mode="<<tail.runtime.camera_transition_mode1C<<" command="<<tail.runtime.requested_command<<" commandComplete="<<tail.runtime.command_complete<<" steps="<<tail.instructions<<" detail="<<tail.error<<'\n';}
    const auto goodbye=game.Snapshot();Check(!goodbye.blocked&&goodbye.messages==16&&!goodbye.awaiting_message
        &&goodbye.message_acknowledgements==16&&goodbye.game3_calls==1&&goodbye.game3_result_word==0x10000000
        &&(with_main_flow?!goodbye.runtime.manager_request9:goodbye.runtime.manager_request9)&&goodbye.runtime.camera_completions==2&&goodbye.runtime.camera_transition_mode1C==0
        &&goodbye.runtime.camera_fov_degrees==30&&goodbye.last_service==28&&!goodbye.completed,
        "freshbye acknowledgement executes real manager9 setter,camera-out and live main loop;not forced session completion");
    Check(records.At(0,0).payload==saved.bank0[0].payload&&records.At(0,0).date==saved.bank0[0].date&&records.Live(),"persistent record survives inventory and subsequent temporary script contexts");
    if(with_main_flow){
        const auto flow=main_flow.Snapshot();
        std::cout<<"MAIN_FLOW phase="<<flow.phase1C<<" sub="<<flow.substate24<<" returns="<<flow.outer_returns<<" consumed="<<flow.request9_consumed<<" callback="<<std::hex<<goodbye.owner_event_callback_word<<" context="<<goodbye.context_word<<std::dec<<" depth="<<goodbye.callback_depth<<" stack="<<goodbye.operand_count<<" deadline="<<goodbye.deadline_tick<<'\n';
        Check(flow.phase5_entries==1&&flow.request9_consumed==1&&flow.outer_returns==1&&flow.phase1C==0&&flow.substate24==0,"actualbyte8/9 mainflow orderedreturn independentcamera");
        main_flow.Input({0,true,false,100});Check(bool(tick_game()),"real released owner input");
        GamePostModalInteraction route;
        auto unfinished=inventory.Snapshot();unfinished.phase=6;unfinished.retirements=0;
        Check(bool(route.Update(unfinished,main_flow.Snapshot(),game,true))&&!route.Snapshot().eligible
              &&!route.Input({0,true,false,90,0})&&!route.Input({Circle,true,false,91,0}),"pre-retirement input refuses");
        auto pending_flow=main_flow.Snapshot();pending_flow.outer_returns=0;pending_flow.phase1C=5;pending_flow.substate24=11;
        Check(bool(route.Update(inventory.Snapshot(),pending_flow,game,true))&&!route.Snapshot().eligible
              &&!route.Input({Circle,true,false,92,0}),"sub11 pending isnot genuine outer0 boundary");
        Check(bool(route.Update(inventory.Snapshot(),main_flow.Snapshot(),game,true)),"actualclose/retirement andgenuinemainflowreturn admit named native route");
        Check(route.Snapshot().eligible&&route.Snapshot().source_gate_blocked,"nativeeligible doesnot clear source469");
        const auto generation=route.Snapshot().generation;
        Check(!route.Input({Circle,true,false,101,generation}),"held Circle at context boundary refuses");
        Check(!route.Input({0,true,false,102,generation-1})&&!route.Input({Circle,true,false,103,generation-1}),"oldqueued context cannotarm");
        Check(!route.Input({0,false,false,104,generation})&&!route.Input({Circle,true,false,105,generation}),"refocused-held refuses");
        Check(!route.Input({0,true,false,106,generation}),"focused fresh release arms");
        Check(!route.Input({Circle,true,false,105,generation}),"old sequence cannot become fresh edge");
        const auto old_target=game.OwnerEventTarget();auto wrong_target=old_target;++wrong_target.generation;
        const auto calls_before=game.Snapshot().owner_event_calls;
        Check(!game.PostMainFlowEvent(0,0,&wrong_target)&&game.Snapshot().owner_event_calls==calls_before,"generation mismatch refuses before queue mutation");
        Check(route.Input({Circle,true,false,107,generation}),"fresh actual event0 posts through generationbound native context");
        Check(!route.Input({Circle,true,false,108,generation}),"oneowned event peredge/context");
        for(unsigned i=0;i<5000;++i){const auto status=tick_game();const auto future_state=game.Snapshot();
            if(!status||future_state.awaiting_message||i==4999){std::cout<<"MAIN_NEXT pc="<<std::hex<<future_state.pc<<std::dec<<" service="<<future_state.last_service<<" messages="<<future_state.messages<<" instructions="<<future_state.instructions<<" field58="<<future_state.runtime.source_field58<<" requested="<<future_state.runtime.requested_command<<" req8="<<future_state.runtime.manager_request8<<" req9="<<future_state.runtime.manager_request9<<" phase="<<main_flow.Snapshot().phase1C<<" ownerCalls="<<future_state.owner_event_calls<<" ownerResult="<<future_state.owner_event_result<<" detail="<<status.detail<<'\n';break;}}
        const auto spoken_again=game.Snapshot();
        Check(!spoken_again.blocked&&spoken_again.awaiting_message&&spoken_again.messages==17&&spoken_again.pc==0x29CD
            &&spoken_again.callbacks_returned==goodbye.callbacks_returned+1&&spoken_again.callback_depth==0&&spoken_again.queued_callbacks==0
            &&spoken_again.sound73_requests==1&&spoken_again.sound73_result_word==0x10000000&&spoken_again.sound73_playback_result==-1,
            "actual event0 returns via1C,SGXrefusal leaves sourceFALSE,and nextreal KSC message17 appears");
        Check(bool(route.Update(inventory.Snapshot(),main_flow.Snapshot(),game,true))&&route.Snapshot().callback_returned,"real callbackreturn separatefrompost/admission");
        std::cout<<"MAIN_TEXT17=";for(unsigned char b:spoken_again.message)std::cout<<"0123456789abcdef"[b>>4]<<"0123456789abcdef"[b&15];std::cout<<'\n';
        if(after17){
            auto target17=game.MessageTarget();const auto before_ack=game.Snapshot();
            auto stale=target17;++stale.generation;
            Check(!game.AcknowledgeMessage(&stale),"stale generation cannot acknowledge live message");
            stale=target17;++stale.pc;Check(!game.AcknowledgeMessage(&stale),"stale PC cannot acknowledge live message");
            stale=target17;++stale.message;Check(!game.AcknowledgeMessage(&stale),"old message identity cannot acknowledge successor");
            stale=target17;stale.context.context_owner.reset();Check(!game.AcknowledgeMessage(&stale),"expired registry token refuses message mutation");
            const auto after_refusal=game.Snapshot();
            Check(after_refusal.pc==before_ack.pc&&after_refusal.call_depth==before_ack.call_depth
                &&after_refusal.active_control_blocks==before_ack.active_control_blocks&&after_refusal.deadline_tick==before_ack.deadline_tick
                &&after_refusal.operand_count==before_ack.operand_count&&after_refusal.message_acknowledgements==before_ack.message_acknowledgements,
                "refused message targets preserve live frame/block/PC/deadline/operands");
            GameButtonGate message_gate;message_gate.Begin();
            Check(!message_gate.Observe({Circle,true,false,200},Circle),"message17 held interaction edge refuses");
            Check(!message_gate.Observe({Circle,false,false,201},Circle)&&!message_gate.Observe({Circle,true,false,202},Circle),"message17 refocused-held refuses");
            Check(!message_gate.Observe({0,true,false,203},Circle)&&message_gate.Observe({Circle,true,false,204},Circle)
                  &&game.AcknowledgeMessage(&target17),"message17 separate release/fresh acknowledgement");
            for(unsigned i=0;i<5000;++i){const auto status=tick_game();const auto next_state=game.Snapshot();
                if(!status||next_state.awaiting_message||i==4999){
                    std::cout<<"POST17 pc="<<std::hex<<next_state.pc<<std::dec<<" service="<<next_state.last_service
                        <<" messages="<<next_state.messages<<" ack="<<next_state.message_acknowledgements<<" callback="<<next_state.callbacks_returned
                        <<" depth="<<next_state.call_depth<<" command="<<next_state.runtime.requested_command<<" detail="<<status.detail<<" text=";
                    for(unsigned char b:next_state.message)std::cout<<"0123456789abcdef"[b>>4]<<"0123456789abcdef"[b&15];std::cout<<'\n';break;
                }
            }
            for(unsigned prompt=18;prompt<=20&&game.Snapshot().awaiting_message;++prompt){
                message_gate.Begin();Check(!message_gate.Observe({Circle,true,false,205+prompt*3},Circle),"newcontext cannot consume priorheld");
                Check(!message_gate.Observe({0,true,false,206+prompt*3},Circle)&&message_gate.Observe({Circle,true,false,207+prompt*3},Circle)
                      &&game.AcknowledgeMessage(),"explicit next source-selected diagnostic message edge");
                for(unsigned i=0;i<5000;++i){const auto status=tick_game();const auto next_state=game.Snapshot();
                    if(!status||next_state.awaiting_message||next_state.awaiting_choice||i==4999){
                        std::cout<<"POST"<<prompt<<" pc="<<std::hex<<next_state.pc<<std::dec<<" service="<<next_state.last_service<<" messages="<<next_state.messages
                            <<" ack="<<next_state.message_acknowledgements<<" command="<<next_state.runtime.requested_command<<" detail="<<status.detail<<" text=";
                        for(unsigned char b:next_state.message)std::cout<<"0123456789abcdef"[b>>4]<<"0123456789abcdef"[b&15];std::cout<<'\n';break;
                    }
                }
            }
            const auto choice_state=game.Snapshot();
            Check(choice_state.awaiting_choice&&!choice_state.blocked&&choice_state.pc==0xAC11&&choice_state.choice_options.size()==3,"actual builtin33 preserves all three options and waits atsource9");
            const auto target=game.ChoiceTarget();auto bad=target;++bad.request;
            Check(!game.CompleteChoice(bad,2)&&game.Snapshot().choice_completions==0,"stale request cannot publish choice result");
            Check(!game.CompleteChoice(target,4),"result outside three options cannot resume caller");
            GameChoiceController selected_choice;Check(selected_choice.Begin(choice_state.choice_options,30,choice_state.generation),"explicit selected-data choice fixture");
            selected_choice.Tick(2); //synthetic completed clip;actualclip completion is checked by window fixture
            selected_choice.Input({0,true,true,1,0,30,choice_state.generation});selected_choice.Tick(2);
            selected_choice.Input({Down,true,true,2,0,30,choice_state.generation});selected_choice.Tick(2);
            selected_choice.Input({0,true,true,3,0,30,choice_state.generation});selected_choice.Tick(2);
            selected_choice.Input({Circle,true,true,4,0,30,choice_state.generation});selected_choice.Tick(2);
            Check(selected_choice.Snapshot().state==2&&selected_choice.Snapshot().result==2,"actualsecond option selected without autoack");
            selected_choice.Tick(2);Check(bool(game.CompleteChoice(target,selected_choice.Snapshot().result)),"real owned VM accepts closed selection result2");
            Check(!game.CompleteChoice(target,1),"completed context cannot publish another result");
            for(unsigned i=0;i<5000;++i){const auto status=tick_game();const auto after_choice=game.Snapshot();
                if(!status||after_choice.awaiting_message||after_choice.awaiting_choice||i==4999){
                    std::cout<<"CHOICE_NEXT pc="<<std::hex<<after_choice.pc<<" result="<<after_choice.choice_result_word<<std::dec<<" messages="<<after_choice.messages<<" ack="<<after_choice.message_acknowledgements<<" command="<<after_choice.runtime.requested_command<<" callback="<<after_choice.callbacks_returned<<" detail="<<status.detail<<" text=";
                    for(unsigned char b:after_choice.message)std::cout<<"0123456789abcdef"[b>>4]<<"0123456789abcdef"[b&15];std::cout<<'\n';
                    Check(bool(status)&&after_choice.awaiting_message&&after_choice.messages==19&&after_choice.message_acknowledgements==18
                        &&after_choice.pc==0xA88B&&after_choice.runtime.requested_command==51&&after_choice.choice_result_word==0x20000002
                        &&after_choice.b4_calls>0&&after_choice.b4_operand_word>>28==2,
                        "fixed selected-data route must reach actual response19/actor51 through finite integer B4,not timeout/anotherchoice/quota");break;
                }
            }
            if(after19){
                const auto target19=game.MessageTarget();GameMessageInput fresh19;fresh19.Begin(19,target19.generation);
                Check(!fresh19.Observe({Circle,true,true,1,0,19,target19.generation}),"reply19 rejects held choice-confirm edge");
                Check(!fresh19.Observe({0,true,true,2,0,18,target19.generation}),"old queued context cannot arm19");
                Check(!fresh19.Observe({Circle,false,true,3,0,19,target19.generation})&&!fresh19.Observe({Circle,true,true,4,0,19,target19.generation}),"reply19 refocused-held refuses");
                Check(!fresh19.Observe({0,true,true,5,0,19,target19.generation})&&fresh19.Observe({Circle,true,true,6,0,19,target19.generation})
                    &&game.AcknowledgeMessage(&target19),"reply19 separate fresh edge bound to live owner");
                for(unsigned phase=0;phase<5000;++phase){const auto status=tick_game();const auto next19=game.Snapshot();
                    if(!status||next19.awaiting_message||next19.awaiting_choice||phase==4999){
                        std::cout<<"AFTER19 pc="<<std::hex<<next19.pc<<std::dec<<" service="<<next19.last_service<<" messages="<<next19.messages<<" ack="<<next19.message_acknowledgements
                            <<" command="<<next19.runtime.requested_command<<" callback="<<next19.callbacks_returned<<" depth="<<next19.call_depth<<" operands="<<next19.operand_count
                            <<" wait="<<next19.waiting_for_event<<" deadline="<<next19.deadline_tick<<" detail="<<status.detail<<" text=";
                        for(unsigned char b:next19.message)std::cout<<"0123456789abcdef"[b>>4]<<"0123456789abcdef"[b&15];std::cout<<'\n';
                        Check(bool(status)&&next19.awaiting_message&&next19.messages==20&&next19.message_acknowledgements==19&&next19.pc==0x84ED5&&next19.runtime.requested_command==39,
                            "fixed post19 fixture must reach actual20/39,not old19/quota/unsupported success");break;
                    }
                }
                for(unsigned expected_message=20;expected_message<=25&&game.Snapshot().awaiting_message;++expected_message){
                    const auto current_target=game.MessageTarget();GameMessageInput edge;edge.Begin(expected_message,current_target.generation);
                    Check(!edge.Observe({Circle,true,true,1,0,expected_message,current_target.generation})
                        &&!edge.Observe({0,true,true,2,0,expected_message-1,current_target.generation}),"each following context rejects held and old queued input");
                    Check(!edge.Observe({0,true,true,3,0,expected_message,current_target.generation})
                        &&edge.Observe({Circle,true,true,4,0,expected_message,current_target.generation})&&game.AcknowledgeMessage(&current_target),"separate explicit fixture edge for current message");
                    for(unsigned phase=0;phase<5000;++phase){const auto status=tick_game();const auto next_step=game.Snapshot();
                        if(!status||next_step.awaiting_message||next_step.awaiting_choice||phase==4999){
                            std::cout<<"FOLLOW"<<expected_message<<" pc="<<std::hex<<next_step.pc<<std::dec<<" service="<<next_step.last_service<<" messages="<<next_step.messages<<" ack="<<next_step.message_acknowledgements
                                <<" command="<<next_step.runtime.requested_command<<" callback="<<next_step.callbacks_returned<<" depth="<<next_step.call_depth<<" options="<<next_step.choice_options.size()
                                <<" wait="<<next_step.waiting_for_event<<" deadline="<<next_step.deadline_tick<<" detail="<<status.detail<<" text=";
                            for(unsigned char b:next_step.message)std::cout<<"0123456789abcdef"[b>>4]<<"0123456789abcdef"[b&15];std::cout<<'\n';
                            Check(bool(status)&&(next_step.awaiting_message||next_step.awaiting_choice),"actual next consumer required,quota is failure");
                            if(expected_message==20)Check(next_step.awaiting_message&&next_step.messages==21&&next_step.message_acknowledgements==20&&next_step.pc==0xABE9&&next_step.runtime.requested_command==41,"fixed second original question/actor41");
                            if(expected_message==21)Check(next_step.awaiting_choice&&next_step.choice_requests==2&&next_step.messages==21&&next_step.message_acknowledgements==21,"fresh21 reaches second actual SELECT");break;
                        }
                    }
                }
                const auto observed_dictionary=game.Snapshot().dictionary;
                constexpr uint32_t observed_fields[]{0,5,6,7,8,17};uint32_t net_changes=0;
                for(size_t row=0;row<observed_dictionary.current_records.size();++row){
                    const auto& before_row=choice_state.dictionary.current_records.at(row);const auto& after_row=observed_dictionary.current_records[row];
                    for(size_t field=0;field<6;++field)if(before_row.numeric[field]!=after_row.numeric[field]){
                        ++net_changes;std::cout<<"DIC_DELTA row="<<row<<" field="<<observed_fields[field]<<" before="<<std::hex<<before_row.numeric[field]<<" after="<<after_row.numeric[field]<<std::dec<<'\n';
                    }
                }
                std::cout<<"DIC_NET captured6fields="<<net_changes<<" actualWrites="<<game.Snapshot().dictionary_game_writes-choice_state.dictionary_game_writes<<'\n';
                uint32_t bit_changes=0;
                for(size_t row=0;row<observed_dictionary.current_records.size();++row)for(size_t field=0;field<2;++field){
                    const auto before=choice_state.dictionary.current_records.at(row).fields14_15[field],current_bits=observed_dictionary.current_records[row].fields14_15[field];
                    if(before!=current_bits){++bit_changes;std::cout<<"DIC_BIT_DELTA row="<<row<<" field="<<14+field<<" before="<<std::hex<<before<<" after="<<current_bits<<std::dec<<'\n';}
                }
                Check(net_changes==0&&bit_changes==2,"fixed first response changes two captured bit fields,not the six historical fields");
                if(game.Snapshot().awaiting_choice){
                    const auto second_target=game.ChoiceTarget();const auto second_before=game.Snapshot();
                    GameChoiceController second_choice;Check(second_choice.Begin(second_before.choice_options,40,second_target.generation),"second actual query owns a new SELECT context");
                    second_choice.Tick(2);second_choice.Input({0,true,true,1,0,40,second_target.generation});second_choice.Tick(2);
                    second_choice.Input({Circle,true,true,2,0,40,second_target.generation});second_choice.Tick(2);second_choice.Tick(2);
                    Check(second_choice.Snapshot().state==3&&bool(game.CompleteChoice(second_target,second_choice.Snapshot().result)),"second query first answer publishes only after explicit close");
                    for(unsigned phase=0;phase<5000;++phase){const auto status=tick_game();const auto returned=game.Snapshot();
                        if(!status||returned.awaiting_message||returned.awaiting_choice||phase==4999){
                            std::cout<<"SECOND_ANSWER pc="<<std::hex<<returned.pc<<std::dec<<" service="<<returned.last_service<<" messages="<<returned.messages<<" ack="<<returned.message_acknowledgements<<" command="<<returned.runtime.requested_command<<" detail="<<status.detail<<" text=";
                            for(unsigned char b:returned.message)std::cout<<"0123456789abcdef"[b>>4]<<"0123456789abcdef"[b&15];std::cout<<'\n';
                            Check(bool(status)&&returned.awaiting_message&&returned.messages==22&&returned.message_acknowledgements==21&&returned.pc==0xA86A&&returned.runtime.requested_command==69&&returned.choice_result_word==0x20000001,"fixed second answer actual22/69/result1 required,quota fails");break;
                        }
                    }
                }
            }
        }
        route.Close();Check(!route.Input({0,true,false,109,generation})&&!route.Input({Circle,true,false,110,generation}),"retired native interaction rejects lateinput");
        main_flow.Close();
    }
    //Separate synthetic list fixture,not live-game item mutation evidence.
    std::vector<GameInventoryItem> sort_items(3);for(uint32_t i=0;i<3;++i){auto& item=sort_items[i];item.row=i;item.id=int32_t(i);item.valid=true;item.category=int32_t(i);item.date_word=(i==0?10:i==1?30:20);item.name="fixture";}
    Check(bool(inventory.Request(sort_items))&&bool(inventory.Tick(resources,transition)),"separate list source-domain fixture");
    for(unsigned i=0;i<80&&inventory.Snapshot().phase!=3;++i){transition.Update();Check(bool(inventory.Tick(resources,transition)),"fixture open");}
    for(unsigned i=0;i<4;++i){input(0);input(Right);}Check(inventory.Snapshot().filter==5&&inventory.Snapshot().filtered_rows==std::vector<uint32_t>({1,2,0}),"source descending dates preserve original row identities");
    input(0);input(Down);input(0);input(Down);Check(inventory.Snapshot().selection==2,"navigate to list end");
    for(unsigned i=0;i<15;++i)input(Down);Check(inventory.Snapshot().selection==2,"held repeat stops at boundary");
    input(0);input(Down);Check(inventory.Snapshot().selection==0,"fresh first edge wraps at boundary");
    inventory.Close();Check(!inventory.Input({Circle,true,false,++sequence}),"synthetic owner retired");
    game.Close();records.Close();runtime.Close();transition.Retire();lease.live=false;
    Check(!game.AcknowledgeMessage(),"retired script rejects stale message acknowledgement");
    Check(!runtime.Snapshot().live&&!runtime.Snapshot().actor_model_live&&!runtime.Snapshot().model_slots
          &&!runtime.Snapshot().actor_request_pending&&!runtime.Snapshot().selected_extension_pending,"selected actual actor/room owners retire");
}
}
int main(int argc,char** argv){try{Focused();if(argc==3&&(std::string(argv[1])=="--selected-data"||std::string(argv[1])=="--manager9-data"||std::string(argv[1])=="--post17-data"||std::string(argv[1])=="--post19-data"))Selected(argv[2],std::string(argv[1])!="--selected-data",(std::string(argv[1])=="--post17-data"||std::string(argv[1])=="--post19-data"),std::string(argv[1])=="--post19-data");
    #ifndef DOKODEMO_PRIVATE_SOURCE_FIXTURES
    std::cout<<"source-only build: original-byte fixture groups excluded; public synthetic cases executed\n";
#endif
    std::cout<<"actor dictionary/native VM: trees,CP932,aliases,mutation,bounds,lifetime,frames/yield/errors PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
