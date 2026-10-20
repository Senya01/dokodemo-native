#include "adhoc_auth_module_native.h"

namespace dokodemo::recovered {
namespace {
const AdhocAuthModuleDescriptor descriptor{
    "sceNetAdhocAuth_Service",0x1006,0x0101,10816,23448,
    "c67770d8199bdd510fd932546285f1fe87674afd6426a5f9f2212b1901443f7f",
    "06af9b2a1283ad09042fb09d342ebc546471955b856a434a39e42ca6e1f619b7",
    {{{"sceNetIfhandle_driver",0,9,5,0,0,10},{"sceMemab_driver",0,1,5,0,10,11},
      {"SysclibForKernel",0,1,5,0,21,3},{"ThreadManForKernel",0,1,5,0,24,16},
      {"InterruptManagerForKernel",0,9,5,0,40,2}}},
    {{{0x15CFE3C0,0x3B10},{0x76BAD213,0x3B18},{0x1560F143,0x3B20},{0x9A6261EC,0x3B28},
      {0xF8825DC4,0x3B30},{0xF56FAC82,0x3B38},{0x0542835F,0x3B40},{0x773FC77C,0x3B48},
      {0x9CBA24D4,0x3B50},{0xC5623112,0x3B58},
      {0x01FD1BA0,0x3AB8},{0x0C134D3F,0x3AC0},{0xA4720EED,0x3AC8},{0xDC598EF3,0x3AD0},
      {0x8DAC2343,0x3AD8},{0xDAE8A629,0x3AE0},{0xCF49BCBB,0x3AE8},{0xE6D3ACE2,0x3AF0},
      {0xB54E1AA7,0x3AF8},{0xF1D2B9B0,0x3B00},{0x4DF566D3,0x3B08},
      {0xAB7592FF,0x3A20},{0x10F3BB61,0x3A28},{0x81D0D1F7,0x3A30},
      {0x278C0DF5,0x3A38},{0xD6DA4BA1,0x3A40},{0x28B6489C,0x3A48},{0x3F53E640,0x3A50},
      {0x4E3A1105,0x3A58},{0x8125221D,0x3A60},{0x86255ADA,0x3A68},{0xE9B3061E,0x3A70},
      {0x18260574,0x3A78},{0x6652B8CA,0x3A80},{0x7E65B999,0x3A88},{0x446D8DE6,0x3A90},
      {0x9FA03CD3,0x3A98},{0xF475845D,0x3AA0},{0x616403BA,0x3AA8},{0x293B45B8,0x3AB0},
      {0x092968F4,0x3A10},{0x5F10D406,0x3A18}}},
    {{{"",false,0,0x8000,0xD632ACDB,0x3A00},{"",false,0,0x8000,0xCEE8593C,0x3A08},
      {"",true,0,0x8000,0xF01D73A7,0x3C04},
      {"sceNetAdhocAuth_lib",false,0,0x4001,0x86004235,0x164},{"sceNetAdhocAuth_lib",false,0,0x4001,0x6074D8F1,0x228},
      {"sceNetAdhocAuth_lib",false,0,0x4001,0x89F2A732,0x35C},{"sceNetAdhocAuth_lib",false,0,0x4001,0x2E6AA271,0x6E4},
      {"sceNetAdhocAuth_lib",false,0,0x4001,0x6CE209A3,0x870},{"sceNetAdhocAuth_lib",false,0,0x4001,0x2AD8C677,0x9D4},
      {"sceNetAdhocAuth_lib",false,0,0x4001,0xBD144DA6,0xB94},{"sceNetAdhocAuth_lib",false,0,0x4001,0x1F9A90B8,0xD38},
      {"sceNetAdhocAuth_lib",false,0,0x4001,0x76F26AB0,0xF48},
      {"sceNetAdhocAuth_driver",false,0,1,0x86004235,0x164},{"sceNetAdhocAuth_driver",false,0,1,0x6074D8F1,0x228},
      {"sceNetAdhocAuth_driver",false,0,1,0x89F2A732,0x35C},{"sceNetAdhocAuth_driver",false,0,1,0x2E6AA271,0x6E4},
      {"sceNetAdhocAuth_driver",false,0,1,0x6CE209A3,0x870},{"sceNetAdhocAuth_driver",false,0,1,0x2AD8C677,0x9D4},
      {"sceNetAdhocAuth_driver",false,0,1,0xBD144DA6,0xB94},{"sceNetAdhocAuth_driver",false,0,1,0x1F9A90B8,0xD38},
      {"sceNetAdhocAuth_driver",false,0,1,0x76F26AB0,0xF48}}},
    0x3A00,0x3A00,0x3A08,
    {{0x03E00008,0x00001021}},{{0x03E00008,0x00001021}}
};
}
const AdhocAuthModuleDescriptor* FindAdhocAuthModuleDescriptor(std::string_view hash,uint32_t bytes) noexcept {
    return bytes==descriptor.encoded_bytes && hash==descriptor.encoded_sha256?&descriptor:nullptr;
}
bool AdhocAuthImportPolicyAllows(const AdhocAuthImportLibrary& library) noexcept {
    for(const auto& expected:descriptor.imports)
        if(std::string_view(library.name)==expected.name && library.version==expected.version &&
            library.attributes==expected.attributes && library.words==expected.words && library.variables==expected.variables &&
            library.first_function==expected.first_function && library.function_count==expected.function_count) return true;
    return false;
}
void InitializeAdhocAuthPrivateData(AdhocAuthPrivateDataSnapshot& state) noexcept {
    state={};state.present=true;
    // Unrelocated file3F50..3F58 is numeric loader-copy knowledge. This is not
    // a lifecycle store, resource sentinel interpretation or BSS-zero producer.
    for(uint32_t i=0;i<8;++i) {state.bytes[i]=0xFF;state.known[i]=1;}
    state.known_bytes=8;
}
// Actual anonymous D632ACDB/CEE8593C bindings: each JR RA; delay ADDU V0,ZERO,ZERO.
// Neither exact body reaches a following import stub or touches private bytes.
int32_t AdhocAuthStart3A00() noexcept {return 0;}
int32_t AdhocAuthStop3A08() noexcept {return 0;}
} // namespace dokodemo::recovered
