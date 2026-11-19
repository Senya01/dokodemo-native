#include "gameplay/retail_savedata_envelope.h"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
using dokodemo::gameplay::IsRetailProtectedSlot;
void Check(bool success,const char* reason) {
    if (!success) throw std::runtime_error(reason);
}
void Put(Bytes& b, std::size_t at, std::uint32_t value) {
    for (unsigned i=0;i<4;++i) b.at(at+i)=static_cast<std::uint8_t>(value>>(i*8));
}
Bytes Example() {
    const char* names[] = {"CATEGORY","SAVEDATA_DIRECTORY","SAVEDATA_PARAMS","SAVEDATA_FILE_LIST"};
    const std::uint32_t sizes[] = {3,13,128,3168};
    constexpr unsigned values=160;
    Bytes b(values+3+13+128+3168);
    Put(b,0,0x46535000); Put(b,4,0x101); Put(b,8,84); Put(b,12,values); Put(b,16,4);
    unsigned key=0,offset=0;
    for (unsigned i=0;i<4;++i) {
        const unsigned e=20+i*16;
        Put(b,e,key | ((i<2 ? 0x204u : 4u)<<16));
        Put(b,e+4,sizes[i]); Put(b,e+8,sizes[i]); Put(b,e+12,offset);
        const auto size=std::strlen(names[i])+1;
        std::memcpy(b.data()+84+key,names[i],size);
        key+=static_cast<unsigned>(size); offset+=sizes[i];
    }
    std::memcpy(b.data()+values,"MS",3);
    std::memcpy(b.data()+values+3,"UCJS10002000",13);
    b[values+16]=1;
    std::memcpy(b.data()+values+144,"DATA.BIN",9);
    return b;
}
} // namespace

int main(int argc,char** argv) {
    try {
        const auto sfo=Example();
        Check(IsRetailProtectedSlot(sfo,0x40010),"valid metadata");
        Check(!IsRetailProtectedSlot(sfo,0x40000),"bare staging size");
        auto b=sfo; b.resize(b.size()-1);
        Check(!IsRetailProtectedSlot(b,0x40010),"truncated list");
        b=sfo; Put(b,20+16+12,0xffffffff);
        Check(!IsRetailProtectedSlot(b,0x40010),"bogus offset");
        b=sfo; b[163]='X';
        Check(!IsRetailProtectedSlot(b,0x40010),"foreign slot");
        b=sfo; b[176]=0;
        Check(!IsRetailProtectedSlot(b,0x40010),"other mode");
        b=sfo; std::memcpy(b.data()+304+32,"DATA.BIN",9);
        Check(!IsRetailProtectedSlot(b,0x40010),"duplicate protected entry");
        if (argc==2) {
            const std::filesystem::path slot(argv[1]);
            std::ifstream file(slot/"PARAM.SFO",std::ios::binary);
            Check(bool(file),"oracle SFO open");
            const Bytes captured(std::istreambuf_iterator<char>(file),{});
            Check(IsRetailProtectedSlot(captured,std::filesystem::file_size(slot/"DATA.BIN")),
                  "actual utility wrapper");
        }
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
