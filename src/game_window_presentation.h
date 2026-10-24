#pragma once
#include <algorithm>
#include <cstdint>
#include <numeric>

namespace native_port {
struct GameWindowViewport { int x=0,y=0,width=0,height=0; };
// Presentation only: whole source frame, centered, with exact aspect ratio.
// Prefer integer pixel scale; below1x use multiples of the reduced ratio.
inline GameWindowViewport FitGameWindow(int client_width,int client_height,
                                       int frame_width=480,int frame_height=272,int max_scale=0) {
    if(client_width<=0||client_height<=0||frame_width<=0||frame_height<=0)return {};
    int scale=(std::min)(client_width/frame_width,client_height/frame_height);
    if(max_scale>0)scale=(std::min)(scale,max_scale);
    int w=0,h=0;
    if(scale){w=frame_width*scale;h=frame_height*scale;}
    else {
        const auto divisor=std::gcd(frame_width,frame_height);
        const auto rw=frame_width/divisor,rh=frame_height/divisor;
        const auto units=(std::min)(client_width/rw,client_height/rh);
        w=rw*units;h=rh*units;
    }
    return {(client_width-w)/2,(client_height-h)/2,w,h};
}
}
