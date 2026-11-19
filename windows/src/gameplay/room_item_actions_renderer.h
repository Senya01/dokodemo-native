#pragma once
#include "cb2a_scene.h"
#include "nickname_owner.h"
#include <algorithm>
#include <string_view>

namespace dokodemo::gameplay {
inline constexpr Cb2aMedia item_select_media{
    "SELECT.CB2A",11307,"6473316977b01dcce46022b643a364efae5f3a9e6da2770b67eda4c42e422d06",
    "select-textures.bin",8864,"ffbe0f582015f9761ba0b8da3e7b19b87ab9ff7daab0b74316424314cf09ed72"};

// BOOT FF24 fixes the row width at 160 pixels and the stride at 21.
// The calling scene supplies the SELECT labels and node 43 placement.
template<class Label>
bool DrawItemActions(const Cb2aScene& select, unsigned clip, unsigned frame, unsigned count,
                     unsigned cursor, float menu_y, Label label, const recovered::PackageView& font,
                     std::vector<std::uint8_t>& vram, std::size_t offset) {
    const std::vector<Cb2aClip> controls{{0,0,false},{clip,0,false},{25,0,false},
                                        {clip,int(frame),true},{0,int(count),true},{25,1,true}};
    bool rendered=true;
    auto actions=[&](unsigned n,int x,int y) {
        if (n!=34) return;
        for (unsigned row=0;row<count;++row) {
            const int py=y+21*int(row);
            const bool selected=row==cursor;
            if (row) rendered=rendered && select.Draw(3,controls,vram,offset,
                {float(x-240),float(py-136-21)});
            if (selected) {
                for (int iy=std::max(0,py);iy<std::min(272,py+17);++iy)
                    for (int ix=std::max(0,x);ix<std::min(480,x+160);++ix) {
                        auto dst=vram.data()+offset+4*(iy*512+ix);
                        dst[0]=234; dst[1]=125; dst[2]=126;
                    }
            }
            DrawProfileField(label(row),x+80,py,selected,font,vram,offset);
        }
    };
    return select.Draw(2,controls,vram,offset,{},actions,{{43,{},0.f,menu_y}}) && rendered;
}
inline constexpr Cb2aMedia item_question_media{
    "TRC.CB2A",6940,"ee9fedd4cbf141814dbd01b4edd99aa66c88f0eb88d4079212ce300d5fe52c33",
    "trc-textures.bin",17196,"eb91b97daa8d9ad7ed3c23c5c79bc9c9df35a08495df9a46748a85d788797be6"};

inline bool DrawItemQuestion(const Cb2aScene& question, std::string_view prompt,
                             float center_x, float center_y, float zoom, unsigned cursor,
                             const recovered::PackageView& font, std::vector<std::uint8_t>& vram,
                             std::size_t offset) {
    const int columns=(int(MeasureUiText(prompt,font))+15)/16;
    const float height=46;
    const float x=center_x-float(16*columns)*zoom*.5f-14,y=center_y-height*zoom*.5f-14;
    const std::vector<Cb2aClip> controls{{0,columns+20},{1,int(cursor+1)}};
    return question.Draw(0,controls,vram,offset,{},[&](unsigned node,int px,int py){
        if (node==22) DrawUiText(prompt,px,py,font,vram,offset,{85,47,13},zoom);
    },{{0,{},x,y,zoom,zoom}}) && question.Draw(1,controls,vram,offset,{}, {},
                                               {{1,{},center_x,y+height*zoom+4,zoom,zoom}});
}
}
