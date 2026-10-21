#include "game_text_win32.h"
#include <cstdint>
#include <windows.h>

namespace dokodemo {
bool EncodeGameText932(std::wstring_view text, std::string &cp932, std::string &error) {
    if (text.empty()) {
        cp932.clear();
        error.clear();
        return true;
    }
    if (text.size() > 256) {
        error = "Committed native text exceeds bounded transaction";
        return false;
    }
    for (size_t i = 0; i < text.size(); ++i) {
        const auto c = uint32_t(text[i]);
        if (c >= 0xD800 && c <= 0xDBFF) {
            if (i + 1 >= text.size() || text[i + 1] < 0xDC00 || text[i + 1] > 0xDFFF) {
                error = "Incomplete UTF16 scalar";
                return false;
            }
            ++i;
        } else if (c >= 0xDC00 && c <= 0xDFFF) {
            error = "Unpaired UTF16 scalar";
            return false;
        }
    }
    BOOL fallback = FALSE;
    const auto n = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, text.data(), int(text.size()), nullptr, 0,
                                       nullptr, &fallback);
    if (!n || fallback) {
        error = "Character is not representable in CP932";
        return false;
    }
    std::string out(size_t(n), '\0');
    fallback = FALSE;
    if (WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, text.data(), int(text.size()), out.data(), n, nullptr,
                            &fallback) != n ||
        fallback) {
        error = "CP932 conversion failed";
        return false;
    }
    const auto count = MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, out.data(), n, nullptr, 0);
    std::wstring roundtrip(size_t(count > 0 ? count : 0), L'\0');
    if (!count ||
        MultiByteToWideChar(932, MB_ERR_INVALID_CHARS, out.data(), n, roundtrip.data(), count) != count ||
        roundtrip != text) {
        error = "Lossy CP932 conversion refused";
        return false;
    }
    cp932 = std::move(out);
    error.clear();
    return true;
}
} // namespace dokodemo
