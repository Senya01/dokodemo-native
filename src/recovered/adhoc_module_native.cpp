#include "adhoc_module_native.h"

namespace dokodemo::recovered {
namespace {
// Literal ordered tables from the ROOT-routed retained ELF source contract.
// Addresses are source addends; no relocation or runtime PSP placement is applied.
const AdhocModuleDescriptor descriptor{
    "sceNetAdhoc_Library",0x0006,0x0101,40140,40140,
    "47c98edb5d62e069bed77eb5afd69e3877ae3991a163876ea6012536a223826f",
    "47c98edb5d62e069bed77eb5afd69e3877ae3991a163876ea6012536a223826f",
    {{{"sceNetIfhandle_lib",0,0x4001,5,0,0,8},{"sceWlanDrv_lib",0,0x4001,5,0,8,4},
      {"sceNet",0,0x0001,5,0,12,1},{"sceNet_lib",0,0x0001,5,0,13,19},
      {"ThreadManForUser",0,0x4001,5,0,32,2}}},
    {{{0x15CFE3C0,0x6F5C},{0x76BAD213,0x6F64},{0xBFF3CEA5,0x6F6C},{0x3E8DD3F8,0x6F74},
      {0x9A6261EC,0x6F7C},{0xF8825DC4,0x6F84},{0xF56FAC82,0x6F8C},{0x49EDBB18,0x6F94},
      {0x5E7C8D94,0x7034},{0x5ED4049A,0x703C},{0xB4D7CB74,0x7044},{0xA447103A,0x704C},
      {0x0BF0A3AE,0x6F54},
      {0x1858883D,0x6F9C},{0x25CC373A,0x6FA4},{0xA8B6205A,0x6FAC},{0xA93A93E9,0x6FB4},
      {0x6B294EE4,0x6FBC},{0xF6DB0A0B,0x6FC4},{0x7574FDA1,0x6FCC},{0xCA3CF5EB,0x6FD4},
      {0x757085B0,0x6FDC},{0x94B44F26,0x6FE4},{0x515B2F33,0x6FEC},{0x05D525E4,0x6FF4},
      {0xD60225A3,0x6FFC},{0x2E005032,0x7004},{0x33B230BD,0x700C},{0x1F94AFD9,0x7014},
      {0x0A5A8751,0x701C},{0xB3A48B7F,0x7024},{0x949F1FBB,0x702C},
      {0x3F53E640,0x6F44},{0x82BC5777,0x6F4C}}},
    {{{"",false,0,0x8000,0xD632ACDB,0x6D5C},{"",false,0,0x8000,0xCEE8593C,0x6D64},
      {"",true,0,0x8000,0xF01D73A7,0x7104},
      {"sceNetAdhoc",false,0,0x0001,0xE1D621D7,0x0000},{"sceNetAdhoc",false,0,0x0001,0xA62C6F57,0x0124},
      {"sceNetAdhoc",false,0,0x0001,0x7A662D6B,0x0460},{"sceNetAdhoc",false,0,0x0001,0x73BFD52D,0x09E4},
      {"sceNetAdhoc",false,0,0x0001,0x4D2CE199,0x0BE4},{"sceNetAdhoc",false,0,0x0001,0x6F92741B,0x10A8},
      {"sceNetAdhoc",false,0,0x0001,0xABED3790,0x11D4},{"sceNetAdhoc",false,0,0x0001,0xDFE53E03,0x1EA4},
      {"sceNetAdhoc",false,0,0x0001,0x7F27BB5E,0x13C4},{"sceNetAdhoc",false,0,0x0001,0xC7C1FC57,0x28A0},
      {"sceNetAdhoc",false,0,0x0001,0x877F6D66,0x2CDC},{"sceNetAdhoc",false,0,0x0001,0xFC6FC07B,0x2DF0},
      {"sceNetAdhoc",false,0,0x0001,0xE08BDAC1,0x2F98},{"sceNetAdhoc",false,0,0x0001,0x9DF81198,0x3094},
      {"sceNetAdhoc",false,0,0x0001,0x4DA4C788,0x325C},{"sceNetAdhoc",false,0,0x0001,0x8BEA2B3E,0x3448},
      {"sceNetAdhoc",false,0,0x0001,0x9AC2EEAC,0x3640},{"sceNetAdhoc",false,0,0x0001,0x157E6225,0x37FC},
      {"sceNetAdhoc",false,0,0x0001,0xB9685118,0x5238},{"sceNetAdhoc",false,0,0x0001,0x7F75C338,0x66FC},
      {"sceNetAdhoc",false,0,0x0001,0x3278AB0C,0x68FC},{"sceNetAdhoc",false,0,0x0001,0x98C204C8,0x67B8},
      {"sceNetAdhoc",false,0,0x0001,0xFA324B4E,0x69AC},{"sceNetAdhoc",false,0,0x0001,0xA0229362,0x69E4},
      {"sceNetAdhoc",false,0,0x0001,0x0B2228E9,0x6A58},
      {"sceNetAdhoc_lib",false,0,0x0001,0x67346A2A,0x1EDC},{"sceNetAdhoc_lib",false,0,0x0001,0x62B875A8,0x47AC},
      {"sceNetAdhoc_lib",false,0,0x0001,0x2CA12790,0x47E8},{"sceNetAdhoc_lib",false,0,0x0001,0xAF6DFB15,0x69C8}}},
    0x6D5C,0x6D5C,0x6D64,
    {{0x03E00008,0x00001021}},{{0x03E00008,0x00001021}}
};
}
const AdhocModuleDescriptor* FindAdhocModuleDescriptor(std::string_view hash,uint32_t bytes) noexcept {
    return bytes==descriptor.encoded_bytes && hash==descriptor.encoded_sha256?&descriptor:nullptr;
}
bool AdhocImportPolicyAllows(const AdhocImportLibrary& library) noexcept {
    if(!library.name) return false;
    for(const auto& expected:descriptor.imports)
        if(std::string_view(library.name)==expected.name && library.version==expected.version &&
            library.attributes==expected.attributes && library.words==expected.words && library.variables==expected.variables &&
            library.first_function==expected.first_function && library.function_count==expected.function_count) return true;
    return false;
}
void InitializeAdhocPrivateData(AdhocPrivateDataSnapshot& state) noexcept {
    state={};state.present=true;
    // Exact nonrelocated file7470..7474 copy. This is not a lifecycle store,
    // handle sentinel interpretation, networking readiness or BSS-zero proof.
    for(uint32_t i=0;i<4;++i) {state.bytes[i]=0xFF;state.known[i]=1;}
    state.known_bytes=4;
}
// Bound anonymous D632ACDB/CEE8593C: JR RA; delay ADDU V0,ZERO,ZERO.
// Each complete body has no calls, private access, resource effect or fallthrough.
int32_t AdhocStart6D5C() noexcept {return 0;}
int32_t AdhocStop6D64() noexcept {return 0;}
} // namespace dokodemo::recovered
