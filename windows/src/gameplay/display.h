#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace dokodemo::gameplay {

enum class DisplaySync : unsigned { horizontal = 0, vertical = 1 };

// A caller-owned, already rendered linear byte allocation. The source VRAM
// address is resolved by its owner before submitting this span. Offset is
// measured in bytes, stride in pixels. Source and display use one thread.
struct Framebuffer {
    std::shared_ptr<const std::vector<std::uint8_t>> memory;
    std::size_t offset;
    unsigned stride;
    unsigned format;
};

class Display {
public:
    static constexpr unsigned width = 480;
    static constexpr unsigned height = 272;

    // BOOT C30..C54, AFBFC, AFDD8: mode 0, 480x272, stride 512, format 3.
    bool SetMode(unsigned mode, unsigned w, unsigned h);
    // BOOT AFE88/AFEDC and AFF74 use sync 1 and 0, respectively.
    // Accept one outstanding change. Ordering multiple submissions between
    // blanking events and PSP return/error codes are outside this domain.
    bool SetFrameBuf(Framebuffer buffer, DisplaySync sync);
    bool OnBlank(DisplaySync event);
    // Produces top-down opaque BGRX pixels at frame granularity.
    // No output is published for an unknown or disabled framebuffer.
    bool CopyPixels(std::vector<std::uint32_t>& destination) const;
private:
    bool configured_ = false;
    std::optional<Framebuffer> current_;
    struct Pending { Framebuffer buffer; DisplaySync sync; };
    std::optional<Pending> pending_;
};

} // namespace dokodemo::gameplay
