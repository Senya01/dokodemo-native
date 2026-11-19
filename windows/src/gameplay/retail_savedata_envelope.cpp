#include "retail_savedata_envelope.h"
#include <algorithm>
#include <cstring>
#include <string_view>

namespace dokodemo::gameplay {
namespace {
using Byte = std::uint8_t;

unsigned Half(const Byte* p) noexcept {
    return unsigned(p[0]) | (unsigned(p[1]) << 8);
}
std::uint32_t Word(const Byte* p) noexcept {
    return Half(p) | (std::uint32_t(Half(p + 2)) << 16);
}
bool Text(const Byte* data, std::size_t length, std::string_view wanted) noexcept {
    return length == wanted.size() + 1 && data[length - 1] == 0 &&
           std::memcmp(data, wanted.data(), wanted.size()) == 0;
}
} // namespace

bool IsRetailProtectedSlot(const std::vector<std::uint8_t>& sfo,
                           std::uintmax_t data_size) noexcept {
    if (data_size != 0x40010 || sfo.size() < 20 ||
        Word(sfo.data()) != 0x46535000 || Word(sfo.data()+4) != 0x101) return false;
    const std::size_t keys = Word(sfo.data()+8);
    const std::size_t values = Word(sfo.data()+12);
    const unsigned count = Word(sfo.data()+16);
    if (keys < 20 || keys > values || values > sfo.size() ||
        count > (keys-20)/16) return false;
    bool category = false, directory = false, file_list = false, params = false;
    for (unsigned i = 0; i < count; ++i) {
        const Byte* entry = sfo.data() + 20 + std::size_t(i)*16;
        const unsigned key = Half(entry), type = Half(entry+2);
        const std::size_t length = Word(entry+4), capacity = Word(entry+8), offset = Word(entry+12);
        if (key >= values-keys || offset > sfo.size()-values || length > capacity ||
            capacity > sfo.size()-values-offset) return false;
        const Byte* name = sfo.data()+keys+key;
        const Byte* end = std::find(name, static_cast<const Byte*>(sfo.data()+values), Byte{0});
        if (end == sfo.data()+values || end == name) return false;
        const std::string_view id(reinterpret_cast<const char*>(name), end-name);
        const Byte* value = sfo.data()+values+offset;
        if (id == "CATEGORY") {
            if (category || type != 0x204 || !Text(value,length,"MS")) return false;
            category = true;
        } else if (id == "SAVEDATA_DIRECTORY") {
            if (directory || type != 0x204 || !Text(value,length,"UCJS10002000")) return false;
            directory = true;
        } else if (id == "SAVEDATA_PARAMS") {
            if (params || type != 4 || length != 128 || value[0] != 1) return false;
            params = true;
        } else if (id == "SAVEDATA_FILE_LIST") {
            if (file_list || type != 4 || length != 99*32) return false;
            for (std::size_t at = 0; at < length; at += 32) {
                if (std::memcmp(value+at,"DATA.BIN",9) == 0) {
                    if (file_list) return false;
                    file_list = true;
                }
            }
            if (!file_list) return false;
        }
    }
    return category && directory && params && file_list;
}
} // namespace dokodemo::gameplay
