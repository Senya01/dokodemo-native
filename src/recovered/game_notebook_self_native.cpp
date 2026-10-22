#include "game_notebook_native.h"
#include <cstdio>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void Require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
bool Nil(const script::Value& v){return v.kind==script::Value::Kind::Scalar&&v.word==0&&v.cp932.empty();}
int32_t Number(const script::Value& v){
    Require(v.kind==script::Value::Kind::Scalar&&v.word>>28==2&&v.cp932.empty(),
            "Unsupported NOTE self-details numeric conversion (requires actual tag2)");
    return int32_t(v.word<<4)>>4;
}
int32_t Digits41D4(uint32_t x){return int32_t(((x>>16)&15)*1000+((x>>8)&15)*100+((x>>4)&15)*10+(x&15));}
int32_t DateIndex14538(uint32_t x){
    if(x-0x101u>=0x1131u)return -1;
    static constexpr uint32_t limits[]={0x120,0x219,0x321,0x420,0x521,0x622,0x723,0x823,0x923,0x1024,0x1122,0x1222};
    static constexpr int32_t result[]={9,10,11,0,1,2,3,4,5,6,7,8};
    for(unsigned i=0;i<12;++i)if(x<limits[i])return result[i];
    return 9;
}
template<size_t N>std::string Lookup(const char* const(&table)[N],int32_t n){
    return uint32_t(n)<N&&table[n]?table[n]:"";
}
}
GameNotebookSelfImage GameObserveNotebookSelf(const script::Vm& vm,uint64_t generation,const script::Value& registered){
    GameNotebookSelfImage image;image.owner=GameObserveNotebookSlot(vm,generation,registered,0);
    Require(image.owner.valid,"NOTE self-details requires actual stable GAME11 index0");
    image.code_sha=vm.current_witness().code_sha256;
    auto slot=registered;Require(slot.index<UINT16_MAX,"NOTE self-details slot extent");++slot.index;
    auto row=vm.peek_reference(slot);
    Require(row.kind==script::Value::Kind::Reference&&vm.stable_value_owner(row),"NOTE self-details current row owner");
    for(uint32_t i=0;i<image.owner.cells;++i){
        Require(row.index<UINT16_MAX,"NOTE self-details row extent");++row.index;
        const auto value=vm.peek_reference(row);
        Require(vm.stable_value_owner(value),"NOTE self-details expired field owner");image.fields.push_back(value);
    }
    Require(GameObserveNotebookSlot(vm,generation,registered,0).sha==image.owner.sha,"NOTE self-details changed during passive capture");
    return image;
}
GameNotebookSelfProjection GameProjectNotebookSelf(const GameNotebookSelfImage& image){
    Require(image.owner.valid&&image.owner.cells==image.fields.size()&&image.fields.size()>=27,
            "NOTE self-details protected row shape");
    const auto& f=image.fields;
    Require(f[0].kind==script::Value::Kind::Scalar&&f[0].word==0x20000002&&f[0].cp932.empty(),
            "Unsupported NOTE self-details current record kind");
    GameNotebookSelfProjection out;
    static constexpr uint32_t fields[]={1,21,25,26};
    for(unsigned i=0;i<4;++i){const auto& v=f[fields[i]];if(Nil(v))continue;
        Require(v.kind==script::Value::Kind::String&&(v.word>>28==5||v.word>>28==6)&&v.cp932.size()<=1023,
                "Unsupported NOTE self-details75778 text conversion");
        Require(v.cp932.find('\0')==std::string::npos,"NOTE self-details embedded NUL text");
        out.text_present[i]=true;out.text[i]=v.cp932;
    }
    if(!Nil(f[22]))out.selector90=Number(f[22]);
    if(!Nil(f[9]))out.valueA0=Number(f[9])&3;
    if(!Nil(f[24]))out.valueA4=Number(f[24]);
    if(!Nil(f[23])){
        const uint32_t packed=uint32_t(Number(f[23]))&0x0FFFFFFF;
        const auto year=Digits41D4(packed>>16);
        out.year94=year==999?-1:year+1900;
        out.month98=Digits41D4((packed&0xFF00)>>8);out.day9C=Digits41D4(packed&255);
        out.valueA8=DateIndex14538(packed&65535);
    }
    return out;
}
bool GameNotebookSelfCallbackEmits(const GameNotebookSelfProjection& p,uint32_t node){
    if(node==211||node==212)return p.text_present[node-211];
    if(node==217||node==218)return p.text_present[node-215];
    if(node==213)return uint32_t(p.valueA0)<4;
    if(node==214)return uint32_t(p.valueA4)<5;
    if(node==216)return uint32_t(p.valueA8)<13;
    Require(node==215,"Unsupported NOTE preview callback node");return p.month98!=-1&&p.day9C!=-1;
}
std::string GameNotebookSelfCallbackText(const GameNotebookSelfProjection& p,uint32_t node){
    // PH1 tables42D8/42E4/42F8 with their actual PH0 R32 literal targets.
    // The overlapping entry at42E4 is retained; no guessed label substitution.
    static const char* const a0[]={"\x81\x48","\x92\x6A","\x8F\x97","\x82\x60\x8C\x5E"};
    static const char* const a4[]={"\x82\x60\x8C\x5E","\x82\x61\x8C\x5E","\x41\x42\x8C\x5E","\x82\x6E\x8C\x5E","\x81\x48"};
    static const char* const a8[]={"\x82\xA8\x82\xD0\x82\xC2\x82\xB6\x8D\xC0","\x82\xA8\x82\xA4\x82\xB5\x8D\xC0","\x82\xD3\x82\xBD\x82\xB2\x8D\xC0","\x82\xA9\x82\xC9\x8D\xC0","\x82\xB5\x82\xB5\x8D\xC0","\x82\xA8\x82\xC6\x82\xDF\x8D\xC0","\x82\xC4\x82\xF1\x82\xD1\x82\xF1\x8D\xC0","\x82\xB3\x82\xBB\x82\xE8\x8D\xC0","\x82\xA2\x82\xC4\x8D\xC0","\x82\xE2\x82\xAC\x8D\xC0","\x82\xDD\x82\xB8\x82\xAA\x82\xDF\x8D\xC0","\x82\xA4\x82\xA8\x8D\xC0","\x81\x48"};
    if(node==211||node==212)return p.text[node-211];
    if(node==217||node==218)return p.text[node-215];
    if(node==213)return Lookup(a0,p.valueA0);
    if(node==214)return Lookup(a4,p.valueA4);
    if(node==216)return Lookup(a8,p.valueA8);
    Require(node==215,"Unsupported NOTE preview callback node");
    if(p.month98==-1||p.day9C==-1)return {};
    char text[64];
    const auto count=p.year94==-1
        ?std::snprintf(text,sizeof(text),"%2d\x8C\x8E%2d\x93\xFA",p.month98,p.day9C)
        :std::snprintf(text,sizeof(text),"%4d\x94\x4E%2d\x8C\x8E%2d\x93\xFA",p.year94,p.month98,p.day9C);
    Require(count>=0&&size_t(count)<sizeof(text),"NOTE self-details date format extent");return {text,size_t(count)};
}
}
