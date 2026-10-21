#include "game_image_native.h"
#include "game_graphics_native.h"
#include <algorithm>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void Require(bool yes, const char *text) {
    if (!yes)
        throw std::runtime_error(text);
}
uint32_t Aligned(uint32_t x, uint32_t alignment) {
    Require(alignment && alignment <= 4096 && !(alignment & (alignment - 1)), "GIM alignment");
    return (x + alignment - 1) & ~(alignment - 1);
}
struct Block {
    uint32_t at, size, data, type;
    std::vector<Block> children;
};
Block ReadBlock(const PackageView &image, uint32_t at, uint32_t limit, unsigned depth) {
    Require(depth <= 16 && at <= limit && limit - at >= 16, "GIM block bounds/depth");
    const auto size = image.ReadLE32(at + 4), next = image.ReadLE32(at + 8), data = image.ReadLE32(at + 12);
    Block out{at, size, data, image.ReadLE16(at), {}};
    Require(image.ReadLE16(at + 2) == 16 && size >= 16 && size <= limit - at && data >= 16 && data <= size,
            "GIM block geometry");
    if (out.type == 2 || out.type == 3) {
        Require(next == data, "GIM container next/data");
        uint32_t child = at + data;
        while (child < at + size) {
            Require(out.children.size() < 4096, "GIM child cap");
            auto block = ReadBlock(image, child, at + size, depth + 1);
            child += block.size;
            out.children.push_back(std::move(block));
        }
    } else
        Require(next == size, "GIM noncontiguous leaf");
    return out;
}
struct Raster {
    uint32_t at = 0, format = 0, order = 0, width = 0, height = 0, bpp = 0;
    uint32_t pitch = 0, rows_align = 0, stride = 0, rows = 0, start = 0, end = 0, level_type = 0;
};
Raster ReadRaster(const PackageView &image, const Block &block) {
    const auto view = image.Subview(block.at + block.data, block.size - block.data);
    Require(view.Describe().size >= 48, "GIM raster header truncated");
    Raster h;
    h.at = block.at + block.data;
    h.format = view.ReadLE16(4);
    h.order = view.ReadLE16(6);
    h.width = view.ReadLE16(8);
    h.height = view.ReadLE16(10);
    h.bpp = view.ReadLE16(12);
    h.pitch = view.ReadLE16(14);
    h.rows_align = view.ReadLE16(16);
    h.level_type = view.ReadLE16(40);
    const uint32_t bits[] = {16, 16, 16, 32, 4, 8, 16, 32};
    Require(view.ReadLE16(0) == 48 && view.ReadLE16(2) == 0 && h.format < 8 && h.order <= 1 && h.width &&
                h.height && h.width <= 4096 && h.height <= 4096 && uint64_t(h.width) * h.height <= 4194304 &&
                h.bpp == bits[h.format] && !view.ReadLE32(36),
            "GIM raster format/domain");
    //Native software sampling chooses frame0/mip0. Check every mip's
    //extent first; this does not pretend to implement animated GIM frames.
    const auto levels=view.ReadLE16(42);
    Require(levels>=1&&levels<=16 && view.ReadLE16(44) == 3 && view.ReadLE16(46) == 1 &&
                (h.level_type == 1 || h.level_type == 2),
            "GIM animated frames/level type unsupported");
    const auto index = view.ReadLE32(24), begin = view.ReadLE32(28), end = view.ReadLE32(32);
    Require(index >= 48 && uint64_t(index) + uint64_t(levels)*4 <= begin && begin <= end && end <= view.Describe().size &&
                view.ReadLE32(index) == begin,
            "GIM raster offsets");
    uint32_t base_end=end;
    for(uint32_t level=0;level<levels;++level) {
        const auto start=view.ReadLE32(index+level*4);
        const auto stop=level+1<levels?view.ReadLE32(index+(level+1)*4):end;
        Require(start>=begin&&stop>start&&stop<=end,"GIM mip offset order");
        const auto w=h.level_type==2?h.width:(std::max)(1u,h.width>>level);
        const auto height=h.level_type==2?h.height:(std::max)(1u,h.height>>level);
        auto stride=Aligned((w*h.bpp+7)/8,h.pitch),rows=Aligned(height,h.rows_align);
        if(h.order){stride=Aligned(stride,16);rows=Aligned(rows,8);}
        Require(uint64_t(stride)*rows<=stop-start,"GIM mip extent truncated");
        if(!level)base_end=stop;
    }
    h.stride = Aligned((h.width * h.bpp + 7) / 8, h.pitch);
    h.rows = Aligned(h.height, h.rows_align);
    if (h.order) {
        h.stride = Aligned(h.stride, 16);
        h.rows = Aligned(h.rows, 8);
    }
    Require(uint64_t(h.stride) * h.rows <= base_end - begin, "GIM raster data truncated");
    h.start = h.at + begin;
    h.end = h.at + end;
    return h;
}
uint32_t Direct(uint32_t value, uint32_t format) {
    uint32_t r = 0, g = 0, b = 0, a = 255;
    if (format == 3)
        return NativeColorFromPspAbgr(value);
    if (format == 0) {
        r = value & 31;
        g = (value >> 5) & 63;
        b = (value >> 11) & 31;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
    } else if (format == 1) {
        r = value & 31;
        g = (value >> 5) & 31;
        b = (value >> 10) & 31;
        r = (r << 3) | (r >> 2);
        g = (g << 3) | (g >> 2);
        b = (b << 3) | (b >> 2);
        a = (value & 0x8000) ? 255 : 0;
    } else if (format == 2) {
        r = (value & 15) * 17;
        g = ((value >> 4) & 15) * 17;
        b = ((value >> 8) & 15) * 17;
        a = ((value >> 12) & 15) * 17;
    } else
        throw std::runtime_error("GIM palette must use a direct format");
    return (a << 24) | (r << 16) | (g << 8) | b;
}
uint32_t Value(const PackageView &image, const Raster &h, uint32_t x, uint32_t y) {
    const auto byte = (x * h.bpp) / 8;
    uint32_t at = y * h.stride + byte;
    if (h.order)
        at = ((y / 8) * (h.stride / 16) + byte / 16) * 128 + (y % 8) * 16 + byte % 16;
    at += h.start;
    if (h.bpp == 4)
        return (image.ReadU8(at) >> (4 * (x % 2))) & 15;
    if (h.bpp == 8)
        return image.ReadU8(at);
    if (h.bpp == 16)
        return image.ReadLE16(at);
    return image.ReadLE32(at);
}
} // namespace
GameFsStatus GameImage::DecodeGim(PackageView image) {
    Close();
    try {
        const auto size = image.Describe().size;
        constexpr uint8_t magic[16] = {'M', 'I', 'G', '.', '0', '0', '.', '1', 'P', 'S', 'P', 0, 0, 0, 0, 0};
        Require(size >= 32 && size <= UINT32_MAX, "GIM image size");
        for (unsigned i = 0; i < 16; ++i)
            Require(image.ReadU8(i) == magic[i], "GIM magic");
        const auto root = ReadBlock(image, 16, uint32_t(size), 0);
        Require(root.type == 2 && root.at + root.size == size, "GIM root extent");
        const Block *picture = nullptr;
        for (const auto &b : root.children)
            if (b.type == 3) {
                Require(!picture, "GIM multiple pictures unsupported");
                picture = &b;
            }
        Require(picture != nullptr, "GIM picture absent");
        const Block *pixels = nullptr, *palette = nullptr;
        for (const auto &b : picture->children) {
            if (b.type == 4) {
                Require(!pixels, "GIM duplicate raster");
                pixels = &b;
            } else if (b.type == 5) {
                Require(!palette, "GIM duplicate palette");
                palette = &b;
            } else
                Require(b.type == 255, "GIM unknown picture child");
        }
        Require(pixels != nullptr, "GIM pixels absent");
        const auto h = ReadRaster(image, *pixels);
        Require(h.level_type == 1, "GIM image level type");
        std::vector<uint32_t> colors;
        if (palette) {
            const auto p = ReadRaster(image, *palette);
            Require(p.format < 4 && p.level_type == 2, "GIM palette format");
            colors.reserve(size_t(p.width) * p.height);
            for (uint32_t y = 0; y < p.height; ++y)
                for (uint32_t x = 0; x < p.width; ++x)
                    colors.push_back(Direct(Value(image, p, x, y), p.format));
        }
        Require(h.format < 4 || !colors.empty(), "GIM indexed raster without palette");
        std::vector<uint32_t> decoded(size_t(h.width) * h.height);
        for (uint32_t y = 0; y < h.height; ++y)
            for (uint32_t x = 0; x < h.width; ++x) {
                const auto value = Value(image, h, x, y);
                if (h.format >= 4) {
                    Require(value < colors.size(), "GIM palette index out of bounds");
                    decoded[size_t(y) * h.width + x] = colors[value];
                } else
                    decoded[size_t(y) * h.width + x] = Direct(value, h.format);
            }
        image_ = std::move(image);
        pixels_ = std::move(decoded);
        width_ = h.width;
        height_ = h.height;
        return {};
    } catch (const std::exception &error) {
        return {GameFsCode::MalformedDirectory, 0, error.what()};
    }
}
} // namespace dokodemo::recovered
