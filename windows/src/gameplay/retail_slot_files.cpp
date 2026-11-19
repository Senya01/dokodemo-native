#include "retail_slot_files.h"
#include "retail_slot_lock.h"
#include "retail_savedata_crypto.h"
#include "retail_savedata_envelope.h"
#include "retail_save_slot.h"
#include "core_hash.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <string>

namespace dokodemo::gameplay {
namespace {
namespace fs=std::filesystem;
using Bytes=std::vector<std::uint8_t>;
constexpr std::array<const char*,6> names{"DATA.BIN","PARAM.SFO","ICON0.PNG","ICON1.PMF","PIC1.PNG","SND0.AT3"};
void Require(bool valid,const char* reason) { if(!valid) throw std::runtime_error(reason); }
std::uint32_t Get(const Bytes& b,size_t p,unsigned width=4) {
    std::uint32_t v=0;
    for(unsigned k=0;k<width;++k) v|=std::uint32_t(b.at(p+k))<<(8*k);
    return v;
}
void Put(Bytes& b,size_t p,std::uint32_t v,unsigned width=4) {
    for(unsigned k=0;k<width;++k) b.at(p+k)=std::uint8_t(v>>(8*k));
}
Bytes Read(const fs::path& p,size_t limit) {
    Require(!SlotReparsePoint(p) && fs::is_regular_file(p),"Missing or invalid savedata file");
    std::ifstream f(p,std::ios::binary|std::ios::ate);
    const auto length=f.tellg();
    Require(f.good() && length>0 && length<=std::streamoff(limit),"Unexpected savedata file size");
    Bytes b(static_cast<size_t>(length));
    f.seekg(0);
    Require(bool(f.read(reinterpret_cast<char*>(b.data()),std::streamsize(b.size()))),"Incomplete savedata read");
    return b;
}
void Write(const fs::path& p,const Bytes& b) {
    std::ofstream f(p,std::ios::binary);
    f.exceptions(std::ios::badbit|std::ios::failbit);
    f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));
    f.flush();
    f.close();
}
std::array<Bytes,6> Media(const fs::path& game) {
    const Bytes b=Read(game/"assets/game/savedata/slot-media.bin",512*1024);
    Require(core::sha256_hex(b.data(),b.size())=="85d10015fef910c75bb521b6859682988db37f32e736d16dc57e5bccd24293fd",
            "Incorrect savedata media for Toro");
    size_t at=4;
    std::array<Bytes,6> parts;
    for(auto& part:parts) {
        const auto length=Get(b,at);
        at+=4;
        Require(length<=b.size()-at,"Truncated savedata media");
        part.assign(b.begin()+at,b.begin()+at+length);
        at+=length;
    }
    Require(at==b.size(),"Trailing savedata media");
    return parts;
}
Bytes Text(std::string_view s) {
    Bytes b(s.begin(),s.end());
    b.push_back(0);
    return b;
}
Bytes BuildSfo(const Bytes& title,const Bytes& detail,const RetailFileMac& mac) {
    Bytes list(99*32);
    std::copy_n("DATA.BIN",8,list.begin());
    std::copy(mac.begin(),mac.end(),list.begin()+13);
    struct Entry { const char* key; unsigned type,capacity; Bytes value; };
    std::array<Entry,8> entries{{
        {"CATEGORY",0x204,4,Text("MS")},{"PARENTAL_LEVEL",0x404,4,{1,0,0,0}},
        {"SAVEDATA_DETAIL",0x204,1024,detail},{"SAVEDATA_DIRECTORY",0x204,64,Text("UCJS10002000")},
        {"SAVEDATA_FILE_LIST",4,3168,std::move(list)},{"SAVEDATA_PARAMS",4,128,Bytes(128)},
        {"SAVEDATA_TITLE",0x204,128,title},{"TITLE",0x204,128,title}
    }};
    const size_t keys=20+16*entries.size();
    size_t values=keys;
    for(const auto& e:entries) values+=std::string_view(e.key).size()+1;
    values=(values+3)&~size_t(3);
    Bytes sfo(values);
    Put(sfo,0,0x46535000); Put(sfo,4,0x101); Put(sfo,8,unsigned(keys)); Put(sfo,12,unsigned(values));
    Put(sfo,16,unsigned(entries.size()));
    size_t key=keys, index=20, params=0;
    for(const auto& e:entries) {
        auto name=Text(e.key);
        Require(e.value.size()<=e.capacity,"Savedata metadata exceeds retail allocation");
        Put(sfo,index,unsigned(key-keys),2);
        Put(sfo,index+2,e.type,2);
        Put(sfo,index+4,unsigned(e.value.size())); Put(sfo,index+8,e.capacity);
        Put(sfo,index+12,unsigned(sfo.size()-values));
        std::copy(name.begin(),name.end(),sfo.begin()+key);
        if(std::string_view(e.key)=="SAVEDATA_PARAMS") params=sfo.size();
        const auto pos=sfo.size();
        sfo.resize(pos+e.capacity);
        std::copy(e.value.begin(),e.value.end(),sfo.begin()+pos);
        key+=name.size(); index+=16;
    }
    StampRetailSfo(sfo,params);
    return sfo;
}
Bytes ReadSet(const fs::path& dir) {
    Require(!SlotReparsePoint(dir) && fs::is_directory(dir),"Invalid savedata slot");
    const auto sfo=Read(dir/names[1],65536);
    const auto data=Read(dir/names[0],kRetailUtilitySize+16);
    Require(IsRetailProtectedSlot(sfo,data.size()),"Incomplete or unrecognized savedata metadata");
    const size_t keys=Get(sfo,8), values=Get(sfo,12);
    size_t params=0, list=0;
    for(unsigned i=0;i<Get(sfo,16);++i) {
        const size_t e=20+16*i, pos=values+Get(sfo,e+12);
        const std::string_view key(reinterpret_cast<const char*>(sfo.data()+keys+Get(sfo,e,2)));
        if(key=="SAVEDATA_PARAMS") params=pos;
        if(key=="SAVEDATA_FILE_LIST") list=pos;
    }
    Require(VerifyRetailSfo(sfo,params),"Savedata metadata failed integrity check");
    for(size_t i=0;i<99;++i) {
        const auto row=sfo.data()+list+32*i;
        if(std::equal(row,row+9,"DATA.BIN")) {
            RetailFileMac mac;
            std::copy_n(row+13,mac.size(),mac.begin());
            return OpenRetailData(data,mac);
        }
    }
    throw std::runtime_error("No DATA.BIN in savedata index");
}
fs::path Sibling(const fs::path& slot,const char* suffix) {
    auto path=slot;
    path+=suffix;
    return path;
}
void CheckNames(const fs::path& dir) {
    Require(!SlotReparsePoint(dir) && fs::is_directory(dir),"Invalid savedata directory");
    for(const auto& entry:fs::directory_iterator(dir)) {
        const auto name=entry.path().filename().string();
        Require(std::find(names.begin(),names.end(),name)!=names.end() &&
                !SlotReparsePoint(entry.path()) && entry.is_regular_file(),"Unexpected file in savedata slot");
    }
}
void RemoveSet(const fs::path& dir) {
    CheckNames(dir);
    for(auto name:names) fs::remove(dir/name);
    fs::remove(dir);
}
fs::path UnusedSibling(const fs::path& slot,const char* suffix) {
    auto path=Sibling(slot,suffix);
    for(unsigned i=1;fs::exists(path);++i) {
        Require(i<=1024,"Too many private savedata directories");
        path=Sibling(slot,(std::string(suffix)+"."+std::to_string(i)).c_str());
    }
    return path;
}
void Retire(const fs::path& slot,const fs::path& backup) {
    CheckNames(backup);
    ReadSet(backup);
    const auto retired=UnusedSibling(slot,".retired");
    fs::rename(backup,retired);
    // Even if subsequent deletion is interrupted, the live and recovery names remain usable.
    try { RemoveSet(retired); } catch(const fs::filesystem_error&) {}
}
void Recover(const fs::path& slot) {
    auto backup=Sibling(slot,".previous");
    if(!fs::exists(slot) && fs::exists(backup)) {
        ReadSet(backup);
        fs::rename(backup,slot);
    }
}
void ValidatePath(const fs::path& slot) {
    Require(slot.filename()=="UCJS10002000" && !SlotReparsePoint(slot) &&
            !SlotReparsePoint(slot.parent_path()),"Unexpected savedata slot path");
}
} // namespace
std::optional<Bytes> LoadRetailSlot(const fs::path& slot) {
    ValidatePath(slot);
    if(!fs::exists(slot.parent_path())) return {};
    RetailSlotLock lock(slot);
    Recover(slot);
    if(!fs::exists(slot)) return {};
    return ReadSet(slot);
}
Bytes ReadRetailSlot(const fs::path& slot) {
    auto result=LoadRetailSlot(slot);
    Require(result.has_value(),"No local savedata slot");
    return std::move(*result);
}
void WriteRetailSlot(const fs::path& slot,const fs::path& game,const Bytes& request) {
    ValidatePath(slot);
    const auto decoded=DecodeRetailUtilityRequest(request,RetailSaveProfile{});
    Require(decoded.profile.ReadProfile().actor==0,"Savedata media for this actor unavailable");
    auto files=Media(game);
    const auto protected_data=ProtectRetailData(request);
    auto sfo=BuildSfo(files[0],files[1],protected_data.mac);
    files[0]=protected_data.bytes;
    files[1]=std::move(sfo);
    fs::create_directories(slot.parent_path());
    RetailSlotLock lock(slot);
    Recover(slot);
    const auto backup=Sibling(slot,".previous");
    const bool replacing=fs::exists(slot);
    if(replacing) {
        CheckNames(slot);
        ReadSet(slot); // Never overwrite an incomplete or unauthenticated prior save.
    }
    if(fs::exists(backup)) {
        Require(replacing,"Unresolved savedata backup");
        Retire(slot,backup);
    }
    // Preserve a terminated writer's private stage; only the canonical slot or backup can be authoritative.
    const auto stage=UnusedSibling(slot,".pending");
    Require(fs::create_directory(stage),"Cannot create private savedata staging directory");
    bool moved=false;
    try {
        for(size_t i=0;i<files.size();++i) Write(stage/names[i],files[i]);
        Require(ReadSet(stage)==request,"Staged savedata differs from the current session");
        for(size_t i=2;i<files.size();++i)
            Require(Read(stage/names[i],files[i].size())==files[i],"Incomplete savedata media write");
        if(replacing) { fs::rename(slot,backup); moved=true; }
        fs::rename(stage,slot);
    } catch(...) {
        if(moved && !fs::exists(slot)) fs::rename(backup,slot);
        RemoveSet(stage);
        throw;
    }
    if(moved) {
        // Publication already succeeded. A locked backup can be cleaned up on the next save.
        try { Retire(slot,backup); } catch(const std::exception&) {}
    }
}
} // namespace dokodemo::gameplay
