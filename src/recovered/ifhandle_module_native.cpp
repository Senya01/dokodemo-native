#include "ifhandle_module_native.h"

namespace dokodemo::recovered {
namespace {
// SOURCE_CONTRACT.v1.md bd224d13... and ELF_STRUCTURE.v1.json f10c0e34...
// Exact offline association only: no runtime crypto, relocation or PSP placement.
constexpr IfhandleModuleDescriptor Descriptor{
    "sceNetIfhandle_Service",0x1006,0x0101,10832,21824,
    "c2803caab3ad288f91e03f5ac7cc5c5c05d8f945f92cfc55ca42da60d6fd916e",
    "355458da4d29a98808ce3fb52423a0f6de5b8a450039220f885f3b1c2102f12b",
    {{{"SysclibForKernel",0,1,5,0,0,5}, {"ThreadManForKernel",0,1,5,0,5,12},
      {"InterruptManagerForKernel",0,9,5,0,17,2}, {"KDebugForKernel",0,1,5,0,19,1}}},
    {{{0xC0AB8932,0x347C}, {0xB49A7697,0x3484}, {0xAB7592FF,0x348C},
      {0x10F3BB61,0x3494}, {0x81D0D1F7,0x349C}, {0xD6DA4BA1,0x34A4},
      {0x28B6489C,0x34AC}, {0x3F53E640,0x34B4}, {0x4E3A1105,0x34BC},
      {0x58B1F937,0x34C4}, {0x1FB15A32,0x34CC}, {0x56C039B5,0x34D4},
      {0x89B3D48C,0x34DC}, {0xAF36D708,0x34E4}, {0xB736E9FF,0x34EC},
      {0x39810265,0x34F4}, {0x369ED59D,0x34FC}, {0x092968F4,0x3464},
      {0x5F10D406,0x346C}, {0x84F370BC,0x3474}}},
    {{{"",false,0,0x8000,0xD632ACDB,0x3454}, {"",false,0,0x8000,0xCEE8593C,0x345C},
      {"",true,0,0x8000,0xF01D73A7,0x35B0},
      {"sceNetIfhandle",false,0,0x4001,0xC80181A2,0x11A4},
      {"sceNetIfhandle",false,0,0x4001,0xFD8585E1,0x121C},
      {"sceNetIfhandle_lib",false,0,0x4001,0x30F69334,0x9C8},
      {"sceNetIfhandle_lib",false,0,0x4001,0xB9096E48,0},
      {"sceNetIfhandle_lib",false,0,0x4001,0xB8188F96,0xA68},
      {"sceNetIfhandle_lib",false,0,0x4001,0x4FB43BCE,0xAF4},
      {"sceNetIfhandle_lib",false,0,0x4001,0xB1F5BB87,0x9C},
      {"sceNetIfhandle_lib",false,0,0x4001,0x8FCB05A1,0xB80},
      {"sceNetIfhandle_lib",false,0,0x4001,0xEAD3A759,0xC18},
      {"sceNetIfhandle_lib",false,0,0x4001,0x0296C7D6,0xCB0},
      {"sceNetIfhandle_lib",false,0,0x4001,0xE440A7D8,0xD68},
      {"sceNetIfhandle_lib",false,0,0x4001,0x30602CE9,0xDE0},
      {"sceNetIfhandle_lib",false,0,0x4001,0xD5DA7B3C,0xE10},
      {"sceNetIfhandle_lib",false,0,0x4001,0x2162EE67,0xE5C},
      {"sceNetIfhandle_lib",false,0,0x4001,0x0C391E9F,0x1650},
      {"sceNetIfhandle_lib",false,0,0x4001,0x29ED84C5,0x16B0},
      {"sceNetIfhandle_lib",false,0,0x4001,0x15CFE3C0,0x17B0},
      {"sceNetIfhandle_lib",false,0,0x4001,0x76BAD213,0x183C},
      {"sceNetIfhandle_lib",false,0,0x4001,0x4C2886CB,0x1704},
      {"sceNetIfhandle_lib",false,0,0x4001,0x0FB8AE0D,0x2890},
      {"sceNetIfhandle_lib",false,0,0x4001,0x5FB31C72,0x2B44},
      {"sceNetIfhandle_lib",false,0,0x4001,0x62B20015,0x2C24},
      {"sceNetIfhandle_lib",false,0,0x4001,0xBFF3CEA5,0x2058},
      {"sceNetIfhandle_lib",false,0,0x4001,0x3E8DD3F8,0x2A38},
      {"sceNetIfhandle_lib",false,0,0x4001,0x1560F143,0x26B0},
      {"sceNetIfhandle_lib",false,0,0x4001,0x9A6261EC,0x1F00},
      {"sceNetIfhandle_lib",false,0,0x4001,0x456E3146,0x2980},
      {"sceNetIfhandle_lib",false,0,0x4001,0x955F2924,0x1CA4},
      {"sceNetIfhandle_lib",false,0,0x4001,0x6AB53C27,0x29DC},
      {"sceNetIfhandle_lib",false,0,0x4001,0xF8825DC4,0x1948},
      {"sceNetIfhandle_lib",false,0,0x4001,0xF56FAC82,0x1A54},
      {"sceNetIfhandle_lib",false,0,0x4001,0xA493AA5F,0x28C0},
      {"sceNetIfhandle_lib",false,0,0x4001,0x59F0D619,0x18C4},
      {"sceNetIfhandle_lib",false,0,0x4001,0x4CF15C43,0x2918},
      {"sceNetIfhandle_lib",false,0,0x4001,0xC3325FDC,0x1B74},
      {"sceNetIfhandle_lib",false,0,0x4001,0xE80F00A4,0x3010},
      {"sceNetIfhandle_lib",false,0,0x4001,0x49EDBB18,0x21C8},
      {"sceNetIfhandle_driver",false,0,9,0x30F69334,0x9C8},
      {"sceNetIfhandle_driver",false,0,9,0xB9096E48,0},
      {"sceNetIfhandle_driver",false,0,9,0xB8188F96,0xA68},
      {"sceNetIfhandle_driver",false,0,9,0x4FB43BCE,0xAF4},
      {"sceNetIfhandle_driver",false,0,9,0xB1F5BB87,0x9C},
      {"sceNetIfhandle_driver",false,0,9,0x8FCB05A1,0xB80},
      {"sceNetIfhandle_driver",false,0,9,0xEAD3A759,0xC18},
      {"sceNetIfhandle_driver",false,0,9,0x0296C7D6,0xCB0},
      {"sceNetIfhandle_driver",false,0,9,0xE440A7D8,0xD68},
      {"sceNetIfhandle_driver",false,0,9,0x30602CE9,0xDE0},
      {"sceNetIfhandle_driver",false,0,9,0xD5DA7B3C,0xE10},
      {"sceNetIfhandle_driver",false,0,9,0x2162EE67,0xE5C},
      {"sceNetIfhandle_driver",false,0,9,0x16042084,0x180},
      {"sceNetIfhandle_driver",false,0,9,0xC9344A59,0xE8C},
      {"sceNetIfhandle_driver",false,0,9,0xAE81C0CB,0x2B0},
      {"sceNetIfhandle_driver",false,0,9,0x54D1AEA1,0x420},
      {"sceNetIfhandle_driver",false,0,9,0xF94BAF52,0xF3C},
      {"sceNetIfhandle_driver",false,0,9,0x263767F6,0x67C},
      {"sceNetIfhandle_driver",false,0,9,0xE2F4F1C9,0x738},
      {"sceNetIfhandle_driver",false,0,9,0xC28F6FF2,0x820},
      {"sceNetIfhandle_driver",false,0,9,0x16246B99,0xF68},
      {"sceNetIfhandle_driver",false,0,9,0xC6D14282,0xFCC},
      {"sceNetIfhandle_driver",false,0,9,0xD5AD6DEA,0xFE8},
      {"sceNetIfhandle_driver",false,0,9,0x0C391E9F,0x1650},
      {"sceNetIfhandle_driver",false,0,9,0x29ED84C5,0x16B0},
      {"sceNetIfhandle_driver",false,0,9,0x15CFE3C0,0x17B0},
      {"sceNetIfhandle_driver",false,0,9,0x76BAD213,0x183C},
      {"sceNetIfhandle_driver",false,0,9,0x4C2886CB,0x1704},
      {"sceNetIfhandle_driver",false,0,9,0x0FB8AE0D,0x2890},
      {"sceNetIfhandle_driver",false,0,9,0x5FB31C72,0x2B44},
      {"sceNetIfhandle_driver",false,0,9,0x62B20015,0x2C24},
      {"sceNetIfhandle_driver",false,0,9,0xBFF3CEA5,0x2058},
      {"sceNetIfhandle_driver",false,0,9,0x3E8DD3F8,0x2A38},
      {"sceNetIfhandle_driver",false,0,9,0x1560F143,0x26B0},
      {"sceNetIfhandle_driver",false,0,9,0x9A6261EC,0x1F00},
      {"sceNetIfhandle_driver",false,0,9,0x456E3146,0x2980},
      {"sceNetIfhandle_driver",false,0,9,0x955F2924,0x1CA4},
      {"sceNetIfhandle_driver",false,0,9,0x6AB53C27,0x29DC},
      {"sceNetIfhandle_driver",false,0,9,0xF8825DC4,0x1948},
      {"sceNetIfhandle_driver",false,0,9,0xF56FAC82,0x1A54},
      {"sceNetIfhandle_driver",false,0,9,0xA493AA5F,0x28C0},
      {"sceNetIfhandle_driver",false,0,9,0x59F0D619,0x18C4},
      {"sceNetIfhandle_driver",false,0,9,0x4CF15C43,0x2918},
      {"sceNetIfhandle_driver",false,0,9,0xC3325FDC,0x1B74},
      {"sceNetIfhandle_driver",false,0,9,0xE80F00A4,0x3010},
      {"sceNetIfhandle_driver",false,0,9,0x49EDBB18,0x21C8},
      {"sceNetIfhandle_driver",false,0,9,0x0542835F,0xFFC},
      {"sceNetIfhandle_driver",false,0,9,0x773FC77C,0x1068},
      {"sceNetIfhandle_driver",false,0,9,0x9CBA24D4,0x10B0},
      {"sceNetIfhandle_driver",false,0,9,0xC5623112,0x1130},
      {"sceNetIfhandle_driver",false,0,9,0xC80181A2,0x11A4},
      {"sceNetIfhandle_driver",false,0,9,0xFD8585E1,0x121C}}},
    0x3454,0x3454,0x345C,{{0x03E00008,0x00001021}},{{0x03E00008,0x00001021}}
};
constexpr std::array<uint8_t,16> FileData{{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0,0,
                                        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}};
}
const IfhandleModuleDescriptor* FindIfhandleModuleDescriptor(std::string_view hash, uint32_t size) noexcept {
    return size==Descriptor.encoded_bytes && hash==Descriptor.encoded_sha256 ? &Descriptor : nullptr;
}
bool IfhandleImportPolicyAllows(const IfhandleImportLibrary& row) noexcept {
    if(!row.name) return false;
    for(const auto& expected:Descriptor.imports) if(std::string_view(row.name)==expected.name)
        return row.version==expected.version && row.attributes==expected.attributes && row.words==5 &&
            row.variables==0 && row.first_function==expected.first_function && row.function_count==expected.function_count;
    return false;
}
int32_t IfhandleStart3454() noexcept { return 0; } // exact bound3454/3458
int32_t IfhandleStop345C() noexcept { return 0; } // independent bound345C/3460
void InitializeIfhandlePrivateData(IfhandlePrivateDataSnapshot& state) noexcept {
    state=IfhandlePrivateDataSnapshot{}; // host storage only; mask remains authoritative
    state.present=true;state.known_bytes=uint32_t(FileData.size());
    for(uint32_t i=0;i<FileData.size();++i) { state.bytes[i]=FileData[i];state.known[i]=1; }
}
} // namespace dokodemo::recovered
