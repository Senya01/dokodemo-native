#pragma once
#include "asset_store.h"
#include <array>

namespace native_port {
// Original XFT pixels and recovered BOOT CP932 indexing. Layout is an explicit
// diagnostic preview (stored width + 2px tracking); game timing is not inferred.
class GameFont {
public:
    bool Load(const std::filesystem::path& directory, std::wstring& error);
    bool RasterizePlainCp932(const std::string& text, Image& image, std::wstring& error) const;
    bool Loaded() const { return loaded_; }
private:
    bool loaded_ = false;
    std::array<unsigned char,8192> widths_{};
    std::array<Image,32> pages_;
};
}
