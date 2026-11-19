#include "gameplay/radial_wipe.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

int main() {
    constexpr int pitch = 512, width = 480, height = 272;
    constexpr std::size_t base = pitch * height * 4;
    // Right/bottom exclusive, original display captures. Apply the mask over
    // white to test just its coverage without reading any reference artwork.
    struct Sample { int submission; std::array<int,4> bounds; };
    constexpr Sample samples[] = {{360,{0,0,0,0}}, {361,{212,108,267,163}},
        {365,{175,71,304,200}}, {370,{129,25,350,246}},
        {375,{83,0,396,272}}, {380,{37,0,442,272}}};
    float radius = -50;
    for (int display = 359; display <= 380; ++display) {
        radius += std::hypot(240.5f,136.5f)/30;
        for (const auto& sample : samples) {
            if (display != sample.submission) continue;
            std::vector<std::uint8_t> vram(2*base,255);
            dokodemo::gameplay::ApplyRadialWipe(radius,vram,base);
            if (!std::all_of(vram.begin(),vram.begin()+base,[](auto c){return c==255;})) return 1;
            std::array<int,4> bounds{width,height,0,0};
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < pitch; ++x) {
                    const auto* p = &vram[base + 4*(y*pitch+x)];
                    if (p[3] != 255 || p[0] != p[1] || p[0] != p[2]) return 1;
                    if (x >= width) {
                        if (p[0] != 255) return 1;
                    } else if (p[0]) {
                        bounds[0] = std::min(bounds[0],x);
                        bounds[1] = std::min(bounds[1],y);
                        bounds[2] = std::max(bounds[2],x+1);
                        bounds[3] = std::max(bounds[3],y+1);
                    }
                }
            }
            if (!bounds[2]) bounds = {0,0,0,0};
            if (bounds != sample.bounds) {
                std::cerr << "Incorrect radial extent at display " << display << '\n';
                return 1;
            }
        }
    }
    std::cout << "Six original reveal extents and stride/stencil preservation passed\n";
}
