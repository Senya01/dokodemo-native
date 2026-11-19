// Title mesh subset of the retained GameGraphics homogeneous software
// rasterizer.
#include "pokepi_raster.h"
#include <algorithm>
#include <cmath>
#include <cstring>
namespace dokodemo::gameplay {
namespace {
// Title materials select linear filtering. Bilinear samples span adjacent
// texels across an edge only for the axes marked repeat in the DVS sampler.
struct FilterAxis {
    int a, b;
    float fraction;
};
FilterAxis Filter(float uv, unsigned size, bool repeat) {
    uv = repeat ? uv - std::floor(uv) : std::clamp(uv, 0.f, 1.f);
    const float coord = uv * float(size) - .5f;
    const auto base = int(std::floor(coord));
    const auto index = [&](int i) {
        if (!repeat)
            return std::clamp(i, 0, int(size) - 1);
        if (i < 0)
            return int(size) - 1;
        return i == int(size) ? 0 : i;
    };
    return {index(base), index(base + 1), coord - float(base)};
}
uint32_t Sample(const PokepiTexture &texture, unsigned repeat_axes, float u, float v) {
    const auto x = Filter(u, texture.width, repeat_axes & 1);
    const auto y = Filter(v, texture.height, repeat_axes & 2);
    const auto ptr = [&](int row, int column) {
        return texture.pixels.data() + 4 * (size_t(row) * texture.width + column);
    };
    const auto a = ptr(y.a, x.a), b = ptr(y.a, x.b), c = ptr(y.b, x.a), d = ptr(y.b, x.b);
    uint32_t result = 0;
    for (unsigned j = 0; j < 4; ++j) {
        const float top = a[j] + x.fraction * (float(b[j]) - a[j]);
        const float bottom = c[j] + x.fraction * (float(d[j]) - c[j]);
        result |= uint32_t(std::lround(top + y.fraction * (bottom - top))) << (8 * j);
    }
    return result;
}
} // namespace
bool DrawPokepiTriangle(const std::array<PokepiVertex, 3> &vertices, const PokepiTexture *texture,
                        unsigned repeat_axes, bool two_sided, std::vector<uint8_t> &vram, size_t offset) {
    struct Vertex {
        std::array<float, 4> p, color;
        std::array<float, 3> specular;
        float u, v;
    };
    std::vector<Vertex> polygon;
    polygon.reserve(12);
    for (const auto &v : vertices) {
        for (auto x : v.clip)
            if (!std::isfinite(x) || std::abs(x) > 1e12f)
                return false;
        if (!std::isfinite(v.u) || !std::isfinite(v.v) || std::abs(v.u) > 1e8f || std::abs(v.v) > 1e8f)
            return false;
        Vertex p{v.clip, {}, v.specular, v.u, v.v};
        for (unsigned j = 0; j < 4; ++j)
            p.color[j] = float((v.color >> (8 * j)) & 255);
        polygon.push_back(p);
    }

    // Clip against the native OpenGL-style homogeneous frustum plus positiveW.
    for (unsigned plane = 0; plane < 7 && !polygon.empty(); ++plane) {
        const auto distance = [plane](const Vertex &v) {
            if (plane == 6)
                return v.p[3] - 0.00001f;
            return v.p[3] + ((plane & 1) ? -v.p[plane / 2] : v.p[plane / 2]);
        };
        std::vector<Vertex> next;
        next.reserve(12);
        auto a = polygon.back();
        float da = distance(a);
        for (const auto &b : polygon) {
            const auto db = distance(b);
            if ((da >= 0) != (db >= 0)) {
                const auto t = da / (da - db);
                Vertex cut{};
                for (unsigned j = 0; j < 4; ++j) {
                    cut.p[j] = a.p[j] + (b.p[j] - a.p[j]) * t;
                    cut.color[j] = a.color[j] + (b.color[j] - a.color[j]) * t;
                }
                for (unsigned j = 0; j < 3; ++j)
                    cut.specular[j] = a.specular[j] + (b.specular[j] - a.specular[j]) * t;
                cut.u = a.u + (b.u - a.u) * t;
                cut.v = a.v + (b.v - a.v) * t;
                next.push_back(cut);
            }
            if (db >= 0)
                next.push_back(b);
            a = b;
            da = db;
        }
        polygon.swap(next);
    }
    if (polygon.size() < 3)
        return true;
    struct Screen {
        float x, y, z, invw, u, v;
        std::array<float, 4> color;
        std::array<float, 3> specular;
    };
    const auto screen = [](const Vertex &v) {
        const auto inv = 1 / v.p[3];
        Screen s{};
        s.x = (v.p[0] * inv + 1) * float(480) * .5f;
        s.y = (1 - v.p[1] * inv) * float(272) * .5f;
        s.z = v.p[2] * inv;
        s.invw = inv;
        s.u = v.u * inv;
        s.v = v.v * inv;
        for (unsigned j = 0; j < 4; ++j)
            s.color[j] = v.color[j] * inv;
        for (unsigned j = 0; j < 3; ++j)
            s.specular[j] = v.specular[j] * inv;
        return s;
    };
    const auto edge = [](const Screen &a, const Screen &b, float x, float y) {
        return (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
    };
    for (size_t fan = 2; fan < polygon.size(); ++fan) {
        std::array<Screen, 3> p{{screen(polygon[0]), screen(polygon[fan - 1]), screen(polygon[fan])}};
        const auto area = edge(p[0], p[1], p[2].x, p[2].y);
        if (std::abs(area) < .00001f)
            continue;
        if (!two_sided && area >= 0)
            continue;

        const auto left = (std::max)(0, int(std::floor((std::min)({p[0].x, p[1].x, p[2].x}))));
        const auto top = (std::max)(0, int(std::floor((std::min)({p[0].y, p[1].y, p[2].y}))));
        const auto right = (std::min)(int(480), int(std::ceil((std::max)({p[0].x, p[1].x, p[2].x}))));
        const auto bottom = (std::min)(int(272), int(std::ceil((std::max)({p[0].y, p[1].y, p[2].y}))));
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x) {

                const auto a = edge(p[1], p[2], float(x) + .5f, float(y) + .5f) / area;
                const auto b = edge(p[2], p[0], float(x) + .5f, float(y) + .5f) / area;
                const auto c = 1 - a - b;
                if (a < 0 || b < 0 || c < 0)
                    continue;
                const auto denominator = a * p[0].invw + b * p[1].invw + c * p[2].invw;
                if (denominator <= 0)
                    continue;
                uint32_t texel = 0xFFFFFFFF;
                if (texture) {
                    auto u = (a * p[0].u + b * p[1].u + c * p[2].u) / denominator;
                    auto v = (a * p[0].v + b * p[1].v + c * p[2].v) / denominator;
                    texel = Sample(*texture, repeat_axes, u, v);
                }
                uint32_t color = 0;
                for (unsigned j = 0; j < 4; ++j) {
                    const auto channel =
                        (a * p[0].color[j] + b * p[1].color[j] + c * p[2].color[j]) / denominator;
                    const auto value = uint32_t(std::lround((std::clamp)(channel, 0.0f, 255.0f)));

                    const auto specular =
                        j == 3
                            ? 0u
                            : uint32_t((a * p[0].specular[j] + b * p[1].specular[j] + c * p[2].specular[j]) /
                                       denominator);
                    color |= (std::min)(255u, (value * ((texel >> (8 * j)) & 255) + 127) / 255 + specular)
                             << (8 * j);
                }
                const auto z = (std::clamp)(a * p[0].z + b * p[1].z + c * p[2].z, -1.0f, 1.0f);
                const auto depth = uint16_t(std::lround((1 - z) * .5f * 65535));
                const size_t pixel = size_t(y) * 512 + x;
                auto *zp = vram.data() + 2 * 512 * 272 * 4 + 2 * pixel;
                uint16_t previous;
                std::memcpy(&previous, zp, sizeof(previous));
                const unsigned alpha = color >> 24;
                if (alpha && depth >= previous) {
                    auto *dst = vram.data() + offset + 4 * pixel;
                    for (unsigned j = 0; j < 3; ++j)
                        dst[j] = uint8_t((((color >> (8 * j)) & 255) * alpha + dst[j] * (255 - alpha) + 127) /
                                         255);
                    std::memcpy(zp, &depth, sizeof(depth));
                }
            }
    }
    return true;
}
} // namespace dokodemo::gameplay
