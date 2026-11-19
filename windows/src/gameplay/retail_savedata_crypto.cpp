#include "retail_savedata_crypto.h"
#include "retail_save_slot.h"
#include "platform/retail_cipher.h"
#include <algorithm>
#include <stdexcept>

namespace dokodemo::gameplay {
namespace {
using platform::AesBlock;
using platform::RetailAesCbc;
// Public KIRK interoperability table entries 03, 04, 53 (hex).
constexpr AesBlock mac_key{0x98,0x02,0xc4,0xe6,0xec,0x9e,0x9e,0x2f,0xfc,0x63,0x4c,0xe4,0x2f,0xbb,0x46,0x68};
constexpr AesBlock header_key{0x99,0x24,0x4c,0xd2,0x58,0xf5,0x1b,0xcb,0xb0,0x61,0x9c,0xa7,0x38,0x30,0x07,0x5f};
constexpr AesBlock stream_key{0xaf,0xfe,0x8e,0xb1,0x3d,0xd1,0x7e,0xd8,0x0a,0x61,0x24,0x1c,0x95,0x92,0x56,0xb6};

RetailFileMac AlignedCmac(std::vector<std::uint8_t> blocks) {
    // Savedata mode 1 uses AES-CMAC; both the file and padded SFO have a complete final block.
    if (blocks.empty() || blocks.size()%16) throw std::invalid_argument("Unaligned savedata MAC input");
    AesBlock zero{},subkey{};
    RetailAesCbc(mac_key,true,zero.data(),subkey.data(),16);
    const bool reduce=(subkey.front()&0x80)!=0;
    for (size_t i=0;i+1<subkey.size();++i)
        subkey[i]=static_cast<std::uint8_t>((subkey[i]<<1)|(subkey[i+1]>>7));
    subkey.back()=static_cast<std::uint8_t>((subkey.back()<<1)^(reduce?0x87:0));
    for (size_t i=0;i<16;++i) blocks[blocks.size()-16+i]^=subkey[i];
    RetailAesCbc(mac_key,true,blocks.data(),blocks.data(),blocks.size());
    RetailFileMac result;
    std::copy_n(blocks.end()-16,16,result.begin());
    return result;
}

bool SameMac(const std::uint8_t* left,const RetailFileMac& right) {
    unsigned different=0;
    for (size_t i=0;i<right.size();++i) different|=left[i]^right[i];
    return different==0;
}

void ApplyStream(const AesBlock& nonce,const std::uint8_t* input,std::uint8_t* output) {
    // CBC-decrypt a series of nonce[0..11] || counter_le, starting at counter one.
    std::vector<std::uint8_t> stream(kRetailUtilitySize);
    for (size_t at=0;at<stream.size();at+=16) {
        std::copy_n(nonce.begin(),12,stream.begin()+at);
        const auto counter=static_cast<std::uint32_t>(at/16+1);
        for (unsigned byte=0;byte<4;++byte)
            stream[at+12+byte]=static_cast<std::uint8_t>(counter>>(byte*8));
    }
    RetailAesCbc(stream_key,false,stream.data(),stream.data(),stream.size());
    for (size_t i=0;i<stream.size();++i) output[i]=input[i]^stream[i];
}

bool ParamsFit(const std::vector<std::uint8_t>& sfo,size_t offset) {
    return offset<=sfo.size() && sfo.size()-offset>=128;
}

RetailFileMac SfoMac(std::vector<std::uint8_t> image,size_t offset) {
    std::fill_n(image.begin()+offset+16,16,0);
    image.insert(image.end(),(16-image.size()%16)%16,0);
    return AlignedCmac(std::move(image));
}
} // namespace

ProtectedRetailData ProtectRetailData(const std::vector<std::uint8_t>& request) {
    if(request.size()!=kRetailUtilitySize) throw std::invalid_argument("Unexpected first-day utility size");
    ProtectedRetailData data;
    data.bytes.resize(request.size()+16);
    AesBlock nonce{};
    platform::RetailRandomBytes(nonce.data(),12);
    RetailAesCbc(header_key,true,nonce.data(),data.bytes.data(),16);
    ApplyStream(nonce,request.data(),data.bytes.data()+16);
    data.mac=AlignedCmac(data.bytes);
    return data;
}

std::vector<std::uint8_t> OpenRetailData(const std::vector<std::uint8_t>& bytes,const RetailFileMac& expected) {
    if(bytes.size()!=kRetailUtilitySize+16 || !SameMac(expected.data(),AlignedCmac(bytes)))
        throw std::invalid_argument("Invalid protected DATA.BIN size or MAC");
    AesBlock nonce{};
    RetailAesCbc(header_key,false,bytes.data(),nonce.data(),16);
    std::vector<std::uint8_t> request(kRetailUtilitySize);
    ApplyStream(nonce,bytes.data()+16,request.data());
    return request;
}

void StampRetailSfo(std::vector<std::uint8_t>& sfo,size_t offset) {
    if (!ParamsFit(sfo,offset)) throw std::invalid_argument("Missing SFO parameters");
    auto params=sfo.begin()+offset;
    std::fill_n(params,128,0);
    params[0]=1;
    // The private reference utility's first-day profile uses its documented
    // unavailable-device marker for the separate console-fuse field.
    std::fill_n(params+32,16,1);
    const auto mac=SfoMac(sfo,offset);
    std::copy(mac.begin(),mac.end(),params+16);
}

bool VerifyRetailSfo(const std::vector<std::uint8_t>& sfo,size_t offset) {
    if (!ParamsFit(sfo,offset)) return false;
    if (sfo[offset]!=1) return false;
    // Accept only the reference device profile. The actual mode-1 MAC
    // covers the marker, file list and other serialized SFO metadata.
    for(size_t i=1;i<128;++i) {
        if(i>=16 && i<32) continue;
        if(sfo[offset+i]!=(i>=32 && i<48 ? 1 : 0)) return false;
    }
    return SameMac(sfo.data()+offset+16,SfoMac(sfo,offset));
}
}
