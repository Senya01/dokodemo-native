#pragma once
#include "../src/recovered/game_text_native.h"
#include "../src/recovered/game_actor_script_native.h"
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Portable synthetic authoring only; ROOT compiles/runs. No assets/Windows/RNG.
namespace dokodemo::kana_regression_detail {
using namespace recovered;
using script::Value;
using script::Table;
inline void Need(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(std::string("kana regression: ")+message);
}
template<class F> inline bool Refuses(F action) {
    try{action();}catch(const std::exception&){return true;}return false;
}
inline std::string Unit(uint16_t value) {
    std::string bytes;if(value>255)bytes.push_back(char(value>>8));
    if(value)bytes.push_back(char(value&255));return bytes;
}
struct Mapping {uint16_t text,aux;};
// Literal oracle, independent of normalizer arithmetic.
inline constexpr Mapping Kana[]{
    {0x815B,0},
    {0x829F,0x829F},{0x82A0,0x82A0},{0x82A1,0x82A1},{0x82A2,0x82A2},
    {0x82A3,0x82A3},{0x82A4,0x82A4},{0x82A5,0x82A5},{0x82A6,0x82A6},
    {0x82A7,0x82A7},{0x82A8,0x82A8},{0x82A9,0x82A9},{0x82AA,0x82AA},
    {0x82AB,0x82AB},{0x82AC,0x82AC},{0x82AD,0x82AD},{0x82AE,0x82AE},
    {0x82AF,0x82AF},{0x82B0,0x82B0},{0x82B1,0x82B1},{0x82B2,0x82B2},
    {0x82B3,0x82B3},{0x82B4,0x82B4},{0x82B5,0x82B5},{0x82B6,0x82B6},
    {0x82B7,0x82B7},{0x82B8,0x82B8},{0x82B9,0x82B9},{0x82BA,0x82BA},
    {0x82BB,0x82BB},{0x82BC,0x82BC},{0x82BD,0x82BD},{0x82BE,0x82BE},
    {0x82BF,0x82BF},{0x82C0,0x82C0},{0x82C1,0},{0x82C2,0x82C2},
    {0x82C3,0x82C3},{0x82C4,0x82C4},{0x82C5,0x82C5},{0x82C6,0x82C6},
    {0x82C7,0x82C7},{0x82C8,0x82C8},{0x82C9,0x82C9},{0x82CA,0x82CA},
    {0x82CB,0x82CB},{0x82CC,0x82CC},{0x82CD,0x82CD},{0x82CE,0x82CE},
    {0x82CF,0x82CF},{0x82D0,0x82D0},{0x82D1,0x82D1},{0x82D2,0x82D2},
    {0x82D3,0x82D3},{0x82D4,0x82D4},{0x82D5,0x82D5},{0x82D6,0x82D6},
    {0x82D7,0x82D7},{0x82D8,0x82D8},{0x82D9,0x82D9},{0x82DA,0x82DA},
    {0x82DB,0x82DB},{0x82DC,0x82DC},{0x82DD,0x82DD},{0x82DE,0x82DE},
    {0x82DF,0x82DF},{0x82E0,0x82E0},{0x82E1,0x82E1},{0x82E2,0x82E2},
    {0x82E3,0x82E3},{0x82E4,0x82E4},{0x82E5,0x82E5},{0x82E6,0x82E6},
    {0x82E7,0x82E7},{0x82E8,0x82E8},{0x82E9,0x82E9},{0x82EA,0x82EA},
    {0x82EB,0x82EB},{0x82EC,0x82EC},{0x82ED,0x82ED},{0x82EE,0x82EE},
    {0x82EF,0x82EF},{0x82F0,0x82F0},{0x82F1,0x82F1},
    {0x8340,0x829F},{0x8341,0x82A0},{0x8342,0x82A1},{0x8343,0x82A2},
    {0x8344,0x82A3},{0x8345,0x82A4},{0x8346,0x82A5},{0x8347,0x82A6},
    {0x8348,0x82A7},{0x8349,0x82A8},{0x834A,0x82A9},{0x834B,0x82AA},
    {0x834C,0x82AB},{0x834D,0x82AC},{0x834E,0x82AD},{0x834F,0x82AE},
    {0x8350,0x82AF},{0x8351,0x82B0},{0x8352,0x82B1},{0x8353,0x82B2},
    {0x8354,0x82B3},{0x8355,0x82B4},{0x8356,0x82B5},{0x8357,0x82B6},
    {0x8358,0x82B7},{0x8359,0x82B8},{0x835A,0x82B9},{0x835B,0x82BA},
    {0x835C,0x82BB},{0x835D,0x82BC},{0x835E,0x82BD},{0x835F,0x82BE},
    {0x8360,0x82BF},{0x8361,0x82C0},{0x8362,0},{0x8363,0x82C2},
    {0x8364,0x82C3},{0x8365,0x82C4},{0x8366,0x82C5},{0x8367,0x82C6},
    {0x8368,0x82C7},{0x8369,0x82C8},{0x836A,0x82C9},{0x836B,0x82CA},
    {0x836C,0x82CB},{0x836D,0x82CC},{0x836E,0x82CD},{0x836F,0x82CE},
    {0x8370,0x82CF},{0x8371,0x82D0},{0x8372,0x82D1},{0x8373,0x82D2},
    {0x8374,0x82D3},{0x8375,0x82D4},{0x8376,0x82D5},{0x8377,0x82D6},
    {0x8378,0x82D7},{0x8379,0x82D8},{0x837A,0x82D9},{0x837B,0x82DA},
    {0x837C,0x82DB},{0x837D,0x82DC},{0x837E,0x82DD},{0x8380,0x82DE},
    {0x8381,0x82DF},{0x8382,0x82E0},{0x8383,0x82E1},{0x8384,0x82E2},
    {0x8385,0x82E3},{0x8386,0x82E4},{0x8387,0x82E5},{0x8388,0x82E6},
    {0x8389,0x82E7},{0x838A,0x82E8},{0x838B,0x82E9},{0x838C,0x82EA},
    {0x838D,0x82EB},{0x838E,0x82EC},{0x838F,0x82ED},{0x8390,0x82EE},
    {0x8391,0x82EF},{0x8392,0x82F0},{0x8393,0x82F1}
};
inline constexpr std::string_view Ascii="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
inline constexpr std::string_view Upper="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFGHIJKLMNOPQRSTUVWXYZ";
static_assert(sizeof(Kana)/sizeof(Kana[0])==167&&Ascii.size()==62&&Upper.size()==62,"229 singleton oracle");
inline void Normalized(const std::string& text,const std::string& expected) {
    const auto before=text;std::string aux="sentinel auxiliary";
    Need(GameInput32TextAllowed(text),"admitted literal rejected by selector");
    Need(GameInput32Normalize(text,aux)&&aux==expected&&text==before,"literal auxiliary/source preservation");
}
inline void Denied(const std::string& text) {
    const auto before=text;std::string aux="sentinel auxiliary";
    Need(!GameInput32TextAllowed(text),"unsupported selector unit was admitted");
    Need(!GameInput32Normalize(text,aux)&&aux=="sentinel auxiliary"&&text==before,"failure must preserve both owners");
}
inline void Domain229() {
    Normalized("","");
    for(size_t i=0;i<Ascii.size();++i)Normalized(std::string(1,Ascii[i]),std::string(1,Upper[i]));
    for(const auto row:Kana)Normalized(Unit(row.text),Unit(row.aux));
    // Every possible single byte, then every high-bit-leading byte pair.
    // Membership comes from the literal oracle, not normalizer arithmetic.
    for(unsigned byte=0;byte<256;++byte)
        if(Ascii.find(char(byte))==std::string_view::npos)Denied(std::string(1,char(byte)));
    for(unsigned lead=128;lead<256;++lead)for(unsigned trail=0;trail<256;++trail) {
        const auto pair=uint16_t((lead<<8)|trail);
        const bool allowed=std::any_of(std::begin(Kana),std::end(Kana),[&](Mapping row){return row.text==pair;});
        if(!allowed){const auto bytes=Unit(pair);Denied(bytes);Denied("a"+bytes);}
    }
    // Explicit semantic exclusions distinguish valid CP932 from this domain.
    for(const auto& text:std::vector<std::string>{"-"," ",Unit(0x8140),Unit(0x82A0)+"-",Unit(0x8ABF),
        Unit(0x8260),Unit(0x824F),Unit(0x8394),Unit(0x8395),Unit(0x8396),std::string("a\0b",3)})Denied(text);
}
inline void LiteralAndBoundaries() {
    Normalized("\x83\x4B\x83\x70\x83\x7E\x83\x80\x83\x93","\x82\xAA\x82\xCF\x82\xDD\x82\xDE\x82\xF1");
    Normalized("\x82\xC1\x81\x5B\x83\x67\x83\x8D\x82\xC1\x81\x5B","\x82\xC6\x82\xEB\x82\xC1\x81\x5B");
    Normalized("\x83\x62\x81\x5B\x82\xC1\x83\x62","");
    Normalized(std::string("a")+"\x82\xC1\x81\x5B","A\x82\xC1\x81\x5B");
    Normalized("\x81\x5B\x82\xC1" "b\x81\x5B" "9","B\x81\x5B" "9");
    const std::string eight="\x83\x41\x83\x43\x83\x45\x83\x47\x83\x49\x83\x4B\x83\x70\x83\x93";
    const std::string expected="\x82\xA0\x82\xA2\x82\xA4\x82\xA6\x82\xA8\x82\xAA\x82\xCF\x82\xF1";
    Need(eight.size()==16,"eight kana fixture is16 bytes");Normalized(eight,expected);
    for(const auto& nine:std::vector<std::string>{eight+"a",eight+Unit(0x82A0),"123456789"}){
        std::string aux="unchanged";Need(GameInput32TextAllowed(nine),"selector has no8unit limit");
        Need(!GameInput32Normalize(nine,aux)&&aux=="unchanged","ninth source unit fails atomically");
    }
    std::string skipped;for(unsigned i=0;i<8;++i)skipped+=Unit(0x815B);Normalized(skipped,"");
    std::string aux="unchanged";Need(!GameInput32Normalize(skipped+Unit(0x82C1),aux)&&aux=="unchanged","dropped units still count toward8");
    Need(GameInput32TextAllowed(std::string(200,'a')),"selector admits multiple units without editor bound");
    GameNameBuffer buffer;Need(buffer.Assign(eight,8)&&buffer.Units()==8&&buffer.Cursor()==8,"editor eight kana16bytes");
    Need(!buffer.Insert(Unit(0x82A0))&&buffer.Bytes()==eight&&buffer.Cursor()==8,"ninth editor unit is atomic");
    buffer.Left();Need(buffer.Backspace()&&buffer.Units()==7&&buffer.Cursor()==6,"backspace one whole voiced kana");
    Need(buffer.Bytes()==std::string("\x83\x41\x83\x43\x83\x45\x83\x47\x83\x49\x83\x4B\x83\x93"),"backspace literal bytes");
    Need(buffer.Delete()&&buffer.Units()==6&&buffer.Cursor()==6&&buffer.Bytes().size()==12,"delete final whole kana");
    buffer.Home();Need(buffer.Insert("Z")&&buffer.Prefix()=="Z"&&buffer.Cursor()==1,"insertion does not split source units");
    const auto before=buffer.Bytes();Need(!buffer.Insert(Unit(0x82A0)+Unit(0x82A2))&&buffer.Bytes()==before&&buffer.Cursor()==1,"multiunit overflow is transactional");
    Need(!buffer.Insert(std::string("\x83",1))&&buffer.Bytes()==before,"truncated commit cannot split byte pair");
    GameNameEntry editor;Need(editor.Begin("",8),"kana editor begin");editor.ControlCompleted(2);
    const auto event=[&](GameTextKind kind,std::string_view bytes={}){GameTextEvent e;e.context=editor.Snapshot().context;e.kind=kind;e.bytes=uint16_t(bytes.size());std::copy(bytes.begin(),bytes.end(),e.text.begin());return e;};
    Need(editor.Apply(event(GameTextKind::FocusGained))&&editor.Apply(event(GameTextKind::Commit,eight)),"committed kana enters editor");
    Need(editor.Snapshot().units==8&&editor.Snapshot().staged==eight&&editor.Snapshot().source120.empty(),"staged owns source bytes before confirm");
    Need(!editor.Apply(event(GameTextKind::Commit,Unit(0x82A0)))&&editor.Snapshot().staged==eight,"ninth committed kana rejected");
    Need(editor.Apply(event(GameTextKind::Confirm))&&!editor.Result(),"kana result waits actual close2");editor.ControlCompleted(2);
    Need(editor.Result()&&*editor.Result()==eight,"editor returns original katakana, not normalized auxiliary");
    Normalized(*editor.Result(),expected);editor.Retire();
}

inline void Put(std::vector<uint8_t>& bytes,size_t at,uint32_t value,unsigned width=4){for(unsigned i=0;i<width;++i)bytes.at(at+i)=uint8_t(value>>(8*i));}
inline void Add(std::vector<uint8_t>& code,std::initializer_list<uint8_t> bytes){code.insert(code.end(),bytes.begin(),bytes.end());}
inline script::Program Fixture(std::vector<uint8_t> code){
    std::vector<uint8_t> bytes(28+code.size());Put(bytes,0,0x0043534B);Put(bytes,4,0x20030204);Put(bytes,12,uint32_t(code.size()));
    std::copy(code.begin(),code.end(),bytes.begin()+28);script::Program program;script::Diagnostic error;
    Need(program.load_test_fixture(std::move(bytes),error),"synthetic caller fixture loads");return program;
}
inline void At(script::Vm& vm,uint32_t pc){
    for(unsigned i=0;vm.pc()!=pc&&i<100;++i)Need(vm.step().status==script::Status::Running,"caller step to literal target");
    Need(vm.pc()==pc,"caller target reached in finite steps");
}
inline void InputResultCaller(const std::string& input,const std::string& expected){
    // Fresh real20->Input32->31->57->F4 path; no old suite is invoked.
    std::vector<uint8_t> code{0x30,6,0x4E,1,0,2,0,0x80,37,0x91,1,0};
    const auto call=uint32_t(code.size());Add(code,{0x20,1,0,0,0});
    Add(code,{0x91,4,0,0x89,0x51,0x91,1,0,0x90,4,0});const auto copy=uint32_t(code.size());
    Add(code,{0x57,0x91,1,0,0x85,0xF4,0x91,1,0,0x80,1,0xF4,0x8B,0x7D,2,0x38});
    const auto callee=uint32_t(code.size());Put(code,call+2,callee,3);
    Add(code,{0x30,1,0x91,0,0,0x8B,0x7A,1,0x80,2,0x8E,'k',0,0x8B,0x20,2,0x89,0x70,0x31});
    const auto idle=uint32_t(code.size());Add(code,{0x30,0,0x38});auto program=Fixture(std::move(code));script::Vm vm(program);
    Value weak_frame;unsigned consumers=0;
    vm.host_service([&](uint8_t id,const std::vector<Value>& args){
        if(id==0x7A){Need(args.size()==1&&args[0].kind==Value::Kind::Reference,"actual callee exports weak frame");weak_frame=args[0];return Value::integer(9);}
        Need(id==0x7D&&args.size()==2&&weak_frame.frame_owner.expired()&&vm.call_depth()==0,"consumer after true31 retirement");
        Need(args[0].kind==Value::Kind::String&&args[0].cp932==input&&args[1].kind==Value::Kind::String&&args[1].cp932==expected,"F4 reads owned text AND owned possibly-empty auxiliary");
        Need(vm.stack().size()==1&&vm.stack()[0].signed_integer()==37,"caller prefix operand survived31/57/F4");++consumers;return Value::integer(91);
    });
    vm.start({Value::string("kana prompt")});Need(vm.run(100).status==script::Status::Input&&vm.call_depth()==1,"actual callee requests Input32");
    std::string source=input,aux="old";Need(GameInput32Normalize(source,aux)&&aux==expected,"completion uses finite normalizer");
    Need(vm.complete_input(source,aux),"publish original nonempty text plus exact auxiliary");source="changed caller temporary";aux="changed auxiliary temporary";
    const auto result=vm.result();Need(result.kind==Value::Kind::Reference&&result.table==Table::InputResult,"completion returns weak owned result buffer");
    At(vm,copy);Need(weak_frame.frame_owner.expired()&&vm.load_reference(result).word==0x90000002,"31 expires frame, preserves result header");
    Need(vm.step().opcode==0x57,"actual array-copy57 executes");
    const auto copied_text=vm.read_slot(Table::Frame,2),copied_aux=vm.read_slot(Table::Frame,3);
    Need(copied_text.kind==Value::Kind::String&&copied_text.cp932==input&&copied_aux.kind==Value::Kind::String&&copied_aux.cp932==expected,"caller57 owns both strings including empty aux");
    vm.store_reference(vm.indexed_reference(result,0),Value::string("changed buffer text"));
    vm.store_reference(vm.indexed_reference(result,1),Value::string("changed buffer auxiliary"));
    Need(vm.run(50).status==script::Status::Complete&&consumers==1&&vm.result().signed_integer()==91,"one actual F4 host consumer completes");
    vm.start_at(idle,{});Need(result.frame_owner.expired()&&Refuses([&]{(void)vm.load_reference(result);}),"restart revokes old result owner");
    Need(copied_text.cp932==input&&copied_aux.kind==Value::Kind::String&&copied_aux.cp932==expected,"copied Values survive result retirement");
}
// Literal83-pointer oracle released in ROOT_KANA_WORD_MAP/MAP_VECTORS.v1.
inline constexpr uint16_t Prefix[]{
    0x82A0,0x82A0,0x82A2,0x82A2,0x82A4,0x82A4,0x82A6,0x82A6,0x82A8,0x82A8,0x82A9,0x82AA,
    0x82AB,0x82AC,0x82AD,0x82AE,0x82AF,0x82B0,0x82B1,0x82B2,0x82B3,0x82B4,0x82B5,0x82B6,
    0x82B7,0x82B8,0x82B9,0x82BA,0x82BB,0x82BC,0x82BD,0x82BE,0x82BF,0x82C0,0,0x82C2,
    0x82C3,0x82C4,0x82C5,0x82C6,0x82C7,0x82C8,0x82C9,0x82CA,0x82CB,0x82CC,0x82CD,0x82CE,
    0x82CF,0x82D0,0x82D1,0x82D2,0x82D3,0x82D4,0x82D5,0x82D6,0x82D7,0x82D8,0x82D9,0x82DA,
    0x82DB,0x82DC,0x82DD,0x82DE,0x82DF,0x82E0,0x82E2,0x82E2,0x82E4,0x82E4,0x82E6,0x82E6,
    0x82E7,0x82E8,0x82E9,0x82EA,0x82EB,0x82ED,0x82ED,0x82A2,0x82A6,0x82A8,0x82F1
};
static_assert(sizeof(Prefix)/sizeof(Prefix[0])==83,"literal source map83");
inline void WordCase(GameAsciiClass70Owner& owner,const std::string& text,uint32_t count,bool flag,uint16_t prefix){
    auto input=Value::string(text);const auto original=input;
    const auto c=owner.Query({input}),n=GameStringLength53({input});auto p=GameAsciiPrefix71({input});
    Need(c.kind==Value::Kind::Scalar&&c.word==(flag?0x10000001u:0x10000000u)&&n.kind==Value::Kind::Scalar&&n.word==(0x20000000u|count),"source Boolean class and tagged unit count");
    if(prefix){
        Need(p.kind==Value::Kind::String&&p.word>>28==6&&p.cp932==Unit(prefix),"static prefix becomes exact owned String6");
        const auto byte=GameNilBytePair0D({p});Need(byte.kind==Value::Kind::Scalar&&byte.word==(0x20000082u|(uint32_t(prefix&255)<<8)),"0D actual prefix little-endian integer");
        p.cp932="changed output";Need(GameAsciiPrefix71({input}).cp932==Unit(prefix),"returned prefix mutation cannot alter static text/source");
    }else Need(p.kind==Value::Kind::Scalar&&p.word==0,"prefix no-match is canonical NIL, not empty String");
    Need(input.kind==original.kind&&input.word==original.word&&input.cp932==text,"helpers preserve source type/tag/bytes");
}
inline void WordConsumers(){
    GameAsciiClass70Owner owner;auto hira=Value::string(Unit(0x82A0));
    Need(Refuses([&]{owner.Query({hira});}),"unconstructed class owner refuses");owner.Begin(701);
    Need(owner.Live()&&owner.Generation()==701&&owner.CurrentWord()==0x10000000&&owner.OtherWord()==0x10000001,"constructor-owned flags and generation");
    for(char c:Ascii)WordCase(owner,std::string(1,c),1,false,0);
    for(size_t i=0;i<167;++i){const auto prefix=i>=1&&i<=83?Prefix[i-1]:0;WordCase(owner,Unit(Kana[i].text),1,prefix!=0,uint16_t(prefix));}
    WordCase(owner,"",0,false,0);WordCase(owner,"\x81\x5B\x82\xC1",2,false,0);
    WordCase(owner,"\x82\xC1\x81\x5B\x82\x9F" "a",4,false,0x82A0);
    WordCase(owner,"\x81\x5B" "a\x82\xA0",3,false,0);
    WordCase(owner,"\x82\xA0\x83\x4A",2,false,0x82A0);
    std::string long_text;for(int i=0;i<15;++i)long_text+=Unit(0x82A0);WordCase(owner,long_text,15,true,0x82A0);
    WordCase(owner,long_text+Unit(0x82A0),16,false,0x82A0);
    Value nil;nil.kind=Value::Kind::Scalar;Need(GameStringLength53({nil}).word==0x20000000&&GameNilBytePair0D({nil}).word==0x20007383,"canonical NIL conversion domains");
    Value ref;ref.kind=Value::Kind::Reference;auto forged=hira;forged.word=0x50000000;
    for(const auto& args:std::vector<std::vector<Value>>{{},{Value{}},{ref},{nil},{Value::integer(0)},{forged},{Value::string("-")},{Value::string(Unit(0x889F))},{hira,hira}}){
        Need(Refuses([&]{owner.Query(args);})&&Refuses([&]{GameAsciiPrefix71(args);}),"70/71 refuse wrong shape/type/tag/unsupported text");
        if(args.size()!=1||args[0].kind!=Value::Kind::Scalar||args[0].word!=0){
            if(args.size()!=1||args[0].kind!=Value::Kind::String||args[0].word>>28!=6)Need(Refuses([&]{GameStringLength53(args);}),"53 refuses unsupported conversion");
            Need(Refuses([&]{GameNilBytePair0D(args);}),"0D refuses unsupported conversion");
        }
    }
    for(uint16_t bad:{uint16_t(0x829F),uint16_t(0x82C1),uint16_t(0x82E1),uint16_t(0x82EE),uint16_t(0x8341)})Need(Refuses([&]{GameNilBytePair0D({Value::string(Unit(bad))});}),"0D excludes non-pointee two-byte text");
    Need(Refuses([&]{GameNilBytePair0D({Value::string("AA")});}),"0D no arbitrary ASCII pair admission");
    const auto retained=GameAsciiPrefix71({hira});owner.Close();
    Need(!owner.Live()&&owner.Generation()==0&&Refuses([&]{owner.Query({hira});})&&retained.cp932==Unit(0x82A0),"owner close revokes query, copied prefix remains owned");
    owner.Begin(702);WordCase(owner,Unit(0x82A0),1,true,0x82A0);Need(owner.Generation()==702,"new generation");
    owner.Begin(0);Need(!owner.Live()&&Refuses([&]{owner.Query({hira});}),"zero generation refuses");
}
} // namespace dokodemo::kana_regression_detail

inline void GameKanaRegressionChecks(){
    using namespace dokodemo::kana_regression_detail;
    Domain229();LiteralAndBoundaries();WordConsumers();
    InputResultCaller("\x83\x67\x83\x8D","\x82\xC6\x82\xEB");
    InputResultCaller("\x82\xC1\x81\x5B","");
}
