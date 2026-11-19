#pragma once
#include "cb2a_scene.h"

namespace dokodemo::recovered { class PackageView; }
namespace dokodemo::gameplay {
class RoomAnswerRenderer {
public:
    bool Load(const std::filesystem::path& game_directory,std::string& error);
    bool Draw(const std::vector<std::string>& options,unsigned cursor,unsigned frame,bool closing,
              const recovered::PackageView& font,std::vector<std::uint8_t>& vram,std::size_t offset,
              unsigned active_ticks=0);
private:
    Cb2aScene select_;
    float scroll_=0, destination_=0;
    unsigned last_tick_=0;
    std::vector<std::uint8_t> clipped_;
};
}
