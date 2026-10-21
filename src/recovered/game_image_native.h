#pragma once
#include "game_filesystem_native.h"
#include <vector>

namespace dokodemo::recovered {
// Checked native resource decode. This is a CPU image adapter, not GU/GE
// emulation. Original pixels remain borrowed; decoded native ARGB is owned.
class GameImage final {
  public:
    GameFsStatus DecodeGim(PackageView image);
    uint32_t Width() const noexcept {
        return width_;
    }
    uint32_t Height() const noexcept {
        return height_;
    }
    const std::vector<uint32_t> &Pixels() const noexcept {
        return pixels_;
    }
    void Close() {
        std::vector<uint32_t>().swap(pixels_);
        image_.Reset();
        width_ = height_ = 0;
    }

  private:
    PackageView image_;
    uint32_t width_ = 0, height_ = 0;
    std::vector<uint32_t> pixels_;
};
} // namespace dokodemo::recovered
