#include "room_options_renderer.h"
#include "room_item_actions_renderer.h"
#include "room_options.h"
#include <array>
#include <string_view>

namespace dokodemo::gameplay {
namespace {
constexpr Cb2aMedia screen_media{
    "OPTION.CB2A", 12346, "492109a53c465302387e0aca4fca316fa4b3705b4abe9a858645868c22c13102",
    "option-textures.bin", 257392, "0c83d4e82f9ef8c093cd9ce0c921a1a41d71ee23666e57b755bfbdebe94ceed2"};
constexpr Cb2aMedia dialog_media{
    "DIALOG.CB2A", 8499, "6223b5468c53196ed375403bb1c4d99270c8660e521210ae6d5f01c79cae75aa",
    "dialog-textures.bin", 7584, "4a094ae46295c2b8e31687c1de29270bc98554e7aa77865c353d5e51bc8b6c19"};
// DA4EC preview and DA500 confirmation, CP932.
constexpr std::string_view preview="\x82\xbd\x82\xbe\x82\xa2\x82\xdc\x83\x65\x83\x58\x83\x67\x92\x86\x81\x63";
constexpr std::string_view confirm="\x90\xdd\x92\xe8\x82\xf0\x94\xbd\x89\x66\x82\xb5\x82\xdc\x82\xb7\x82\xa9\x81\x48";
}

bool RoomOptionsRenderer::Load(const std::filesystem::path& directory, std::string& error) {
    const auto game=directory.parent_path();
    return screen_.Load(directory,screen_media,error) && dialog_.Load(game/"room-talk",dialog_media,error)
        && question_.Load(game/"title",item_question_media,error);
}

bool RoomOptionsRenderer::Draw(const RoomOptions& options, const recovered::PackageView& font,
                               std::vector<std::uint8_t>& vram, std::size_t offset) const {
    // Root 0 references the row marker and legend. 34DC4 draws the three
    // bars individually, sampling clip 2 at each authored attachment point.
    const std::vector<Cb2aClip> page{{0,1},{1,int(options.cursor())+1},{3,0},{4,1}};
    std::array<Cb2aPlacement,3> anchors{};
    auto locate=[&](unsigned node,int x,int y) {
        if (node>=97 && node<=99) anchors[node-97]={float(x-240),float(y-136)};
    };
    if (!screen_.Draw(0,page,vram,offset,{},locate,{{97,0},{98,0},{99,0}})) return false;
    for (unsigned i=0;i<anchors.size();++i) {
        const int bar=options.values()[i]+(i==options.cursor()?6:1);
        if (!screen_.Draw(2,{{2,bar}},vram,offset,anchors[i])) return false;
    }
    // Mode 4 at 16B20 uses root 3 of DIALOG, clip 4 frame 2. The
    // authored box remains full size while its text waits for appearance=1.
    if (options.cursor()==0 && options.help_progress()>0.f) {
        auto draw_help=[&](unsigned node,int x,int y) {
            if (node!=48 || options.help_progress()!=1.f) return;
            const auto& lines=options.help_lines();
            for (std::size_t i=0;i<lines.size();++i) {
                DrawUiText(preview.substr(0,2*lines[i]),x,y+18*int(i),font,vram,offset,
                           {85,47,13},1.f,{x,y,x+144,y+36},0.f);
            }
        };
        if (!dialog_.Draw(3,{{4,2}},vram,offset,{},draw_help)) return false;
    }
    const auto& prompt=options.prompt();
    return !prompt.visible() || DrawItemQuestion(question_,confirm,0,0,prompt.progress(),prompt.cursor(),font,vram,offset);
}
}
