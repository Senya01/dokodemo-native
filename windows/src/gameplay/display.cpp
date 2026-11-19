#include "display.h"

#include <utility>

namespace dokodemo::gameplay {
namespace {
bool HasVisibleBytes(const Framebuffer& buffer) {
    if (buffer.stride != 512 || buffer.format != 3 || !buffer.memory) return false;
    const auto visible_bytes = 4u * ((Display::height - 1u) * buffer.stride + Display::width);
    return buffer.offset <= buffer.memory->size()
        && visible_bytes <= buffer.memory->size() - buffer.offset;
}
}

bool Display::SetMode(unsigned mode, unsigned w, unsigned h) {
    if (mode != 0 || w != width || h != height) return false;
    configured_ = true;
    return true;
}

bool Display::SetFrameBuf(Framebuffer buffer, DisplaySync sync) {
    if (!configured_ || pending_ ||
        (sync != DisplaySync::vertical && sync != DisplaySync::horizontal)) return false;
    // AFE98(false) sends (null, 0, 0, next-vsync) via AFEDC.
    const bool disable = !buffer.memory && buffer.offset == 0 && buffer.stride == 0
        && buffer.format == 0 && sync == DisplaySync::vertical;
    if (!disable && !HasVisibleBytes(buffer)) return false;
    pending_ = Pending{std::move(buffer), sync};
    return true;
}

bool Display::OnBlank(DisplaySync event) {
    if (!pending_ || event != pending_->sync) return false;
    current_ = std::move(pending_->buffer);
    pending_.reset();
    return true;
}

bool Display::CopyPixels(std::vector<std::uint32_t>& destination) const {
    if (!current_ || !HasVisibleBytes(*current_)) return false;
    const auto& buffer = *current_;
    const auto* first = buffer.memory->data() + buffer.offset;
    destination.resize(width * height);
    for (unsigned y = 0; y < height; ++y) {
        const auto* row = first + y * buffer.stride * 4;
        for (unsigned x = 0; x < width; ++x) {
            const auto* rgba = row + x * 4;
            destination[y * width + x] = 0xff000000u |
                (std::uint32_t(rgba[0]) << 16) | (std::uint32_t(rgba[1]) << 8) | rgba[2];
        }
    }
    return true;
}
}
