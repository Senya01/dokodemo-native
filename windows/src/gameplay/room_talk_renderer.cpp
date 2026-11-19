#include "room_talk_renderer.h"
#include <algorithm>
#include <cmath>

namespace dokodemo::gameplay {
namespace {
constexpr Cb2aMedia prompt{"SYOUKAI.CB2A",1516,
    "c3c396f03d500d3628f1e7265d8a9b4878ad3344fb32bea45d85ebf1867eb240",
    "syoukai-textures.bin",95004,"1ac2027a4583ffbb4f4905e315529a6389d7600cb8095cb40613cdc95a9f3d16"};
constexpr Cb2aMedia dialog{"DIALOG.CB2A",8499,
    "6223b5468c53196ed375403bb1c4d99270c8660e521210ae6d5f01c79cae75aa",
    "dialog-textures.bin",7584,"4a094ae46295c2b8e31687c1de29270bc98554e7aa77865c353d5e51bc8b6c19"};
}

bool RoomTalkRenderer::Load(const std::filesystem::path& dir,std::string& error) {
    return prompt_.Load(dir/"room-talk",prompt,error) && dialog_.Load(dir/"room-talk",dialog,error);
}
bool RoomTalkRenderer::DrawPrompt(std::vector<std::uint8_t>& vram,size_t offset,unsigned frame) const {
    return prompt_.Draw(1,{{1,int(std::clamp(frame,1u,8u))}},vram,offset);
}
bool RoomTalkRenderer::Project(const std::array<float,16>& m,float fov,const std::array<float,3>& body,
                               const std::array<float,3>& speaker,RoomSpeechPlacement& placement,unsigned rows) {
    auto project=[&](const std::array<float,3>& point,float& x,float& y) {
        auto component=[&](unsigned row) {
            return m[row]*point[0]+m[row+4]*point[1]+m[row+8]*point[2]+m[row+12];
        };
        // BOOT 44A8 projects the speech anchor independently of GE geometry.
        // 44C0 multiplies matrix[7] by source Z; 44D8 uses Z again for
        // matrix[11]. 4564 supplies 1 for zero W.
        const float w=m[3]*point[0]+m[7]*point[2]+m[11]*point[2]+m[15];
        const float reciprocal=w!=0.f ? 1.f/w : 1.f;
        x=(component(0)*reciprocal+1.f)*240.f;
        y=(1.f-component(1)*reciprocal)*136.f;
        return std::isfinite(x) && std::isfinite(y);
    };
    auto p=body;
    p[0]+=750.f*std::min(fov/18.f,1.3f);
    p[2]+=280.f;
    float tail_y;
    if (!project(p,placement.center_x,placement.center_y) ||
        !project(speaker,placement.tail_x,tail_y)) return false;
    placement.center_y-=9.f*rows+16.f+15.f;
    return true;
}
bool RoomTalkRenderer::DrawMessage(const RoomSpeechPlacement& p,float appearance,
                                   const std::vector<RoomTextLine>& lines,std::vector<std::uint8_t>& vram,
                                   size_t offset,const DrawRoomLine& draw,unsigned columns,unsigned rows) const {
    if (!columns || columns>10 || !rows || rows>4 || !(appearance>=0.f && appearance<=1.f)) return false;
    if (appearance==0.f) return true;
    // 153D4 default config is 9 columns x 4 rows. 16668 sizes the inner text
    // region, 16D40 keeps it inside the room's UI viewport (right edge 448).
    const int width=16*int(columns),height=18*int(rows);
    const float half_w=.5f*width*appearance,half_h=.5f*height*appearance;
    if (p.center_x< -half_w || p.center_x>480.f+half_w ||
        p.center_y< -half_h || p.center_y>272.f+half_h) return true;
    const float left=std::clamp(p.center_x,half_w,448.f-half_w)-half_w;
    const float top=std::clamp(p.center_y,half_h,272.f-half_h)-half_h;
    float tail_anchor_x=0,tail_anchor_y=0;
    auto visit=[&](unsigned node,int x,int y) {
        if (node==25) { tail_anchor_x=float(x); tail_anchor_y=float(y); }
        if (node==26 && appearance==1.f && draw)
            for (const auto& line:lines) draw(line,x,y,width,height);
    };
    // 168EC selects clip 1 frame (rows-1)*10+columns. Clip 0 selects
    // this mode's yellow panel materials, while 26 owns the text callback.
    // 97B18 writes root 0's actual position, replacing its (-14,-14)
    // template key. Its child 26 supplies the text inset after scaling.
    if (!dialog_.Draw(0,{{0,1},{1,int((rows-1)*10+columns),true}},vram,offset,
                      {left-240,top-136,appearance,appearance},visit,{{0,{},0.f,0.f}})) return false;
    if (appearance!=1.f) return true;
    // 169D8..16ADC clamps the tail to the bubble's original node 25 and
    // draws the separately bound HIGE (root 2, clip 3).
    const float tail_x=std::clamp(p.tail_x-240.f+50.f,tail_anchor_x-240.f,tail_anchor_x-240.f+float(width-16));
    return dialog_.Draw(2,{{3,1}},vram,offset,{tail_x,tail_anchor_y-136.f+float(height-18)});
}
}
