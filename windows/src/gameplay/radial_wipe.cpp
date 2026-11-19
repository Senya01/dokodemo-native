#include "radial_wipe.h"
#include "display.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {
namespace {
constexpr int width = Display::width, height = Display::height, stride = 512;
constexpr float pi = 3.14159265358979323846f;
constexpr int segments = 60;

struct Vertex {
    int x, y, alpha;
};

Vertex Point(float radius, float cosine, float sine, int alpha) {
    // 10F04 constructs the through-mode vertex, truncating after adding
    // the screen center, and stores the result as two signed 16-bit values.
    return {static_cast<int>(240.0f + radius * cosine),
            static_cast<int>(136.0f + radius * sine), alpha};
}

int Edge(Vertex a, Vertex b, int twice_x, int twice_y) {
    return (b.x - a.x) * (twice_y - 2*a.y) - (b.y - a.y) * (twice_x - 2*a.x);
}

bool TopLeft(Vertex a, Vertex b) {
    return a.y > b.y || (a.y == b.y && a.x < b.x);
}

void Triangle(std::uint8_t* buffer, Vertex a, Vertex b, Vertex c) {
    // The source strip emits outer,inner pairs clockwise in screen space.
    // Reversed strips (rim extending past maximum_radius) are backfaces.
    const int area = Edge(a,b,2*c.x,2*c.y);
    if (area >= 0) return;
    std::swap(b,c);
    const int x0 = std::max(0,std::min({a.x,b.x,c.x}));
    const int x1 = std::min(width,std::max({a.x,b.x,c.x}));
    const int y0 = std::max(0,std::min({a.y,b.y,c.y}));
    const int y1 = std::min(height,std::max({a.y,b.y,c.y}));
    const int t0 = TopLeft(b,c) ? 0 : 1;
    const int t1 = TopLeft(c,a) ? 0 : 1;
    const int t2 = TopLeft(a,b) ? 0 : 1;
    const bool black = a.alpha == 255 && b.alpha == 255 && c.alpha == 255;
    for (int y = y0; y < y1; ++y) {
        auto* pixel = buffer + (y*stride+x0)*4;
        int w0 = Edge(b,c,2*x0+1,2*y+1);
        int w1 = Edge(c,a,2*x0+1,2*y+1);
        int w2 = -area - w0 - w1;
        for (int x = x0; x < x1; ++x, pixel += 4) {
            if (w0 >= t0 && w1 >= t1 && w2 >= t2) {
                if (black) {
                    pixel[0] = pixel[1] = pixel[2] = 0;
                } else {
                    const int alpha = (w0*a.alpha + w1*b.alpha + w2*c.alpha - area/2) / -area;
                    for (int k = 0; k < 3; ++k)
                        pixel[k] = static_cast<std::uint8_t>((pixel[k]*(255-alpha)+127)/255);
                }
            }
            w0 += 2*(b.y-c.y);
            w1 += 2*(c.y-a.y);
            w2 += 2*(a.y-b.y);
        }
    }
}
} // namespace

void ApplyRadialWipe(float radius, std::vector<std::uint8_t>& vram, std::size_t offset) {
    const float maximum = std::hypot(240.5f, 136.5f);
    if (offset > vram.size() || vram.size()-offset < ((height-1)*stride+width)*4 ||
        !std::isfinite(radius) || radius < -50 || radius > maximum)
        throw std::invalid_argument("Invalid title transition surface or radius");
    std::uint8_t* surface = vram.data()+offset;
    Vertex prev_inner{}, prev_rim{}, prev_edge{};
    float angle = -pi;
    for (int i = 0; i <= segments; ++i, angle += (2*pi)/segments) {
        const float cosine = std::cos(angle), sine = std::sin(angle);
        // BOOT 1175C/11780/1179C choose a zero inner radius below zero,
        // and an opaque inner vertex strictly below -25. 117D4 follows
        // that feathered strip with the opaque strip out to the corners.
        Vertex inner = Point(std::max(radius,0.0f),cosine,sine,radius < -25 ? 255 : 0);
        Vertex rim = Point(radius+50,cosine,sine,255);
        Vertex edge = Point(maximum,cosine,sine,255);
        if (i) {
            Triangle(surface,prev_rim,prev_inner,rim);
            Triangle(surface,rim,prev_inner,inner);
            Triangle(surface,prev_edge,prev_rim,edge);
            Triangle(surface,edge,prev_rim,rim);
        }
        prev_inner = inner;
        prev_rim = rim;
        prev_edge = edge;
    }
}
} // namespace dokodemo::gameplay
