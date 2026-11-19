#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dokodemo::recovered { class PackageView; }
namespace dokodemo::gameplay {
class TitleRenderer;
float MeasureUiText(std::string_view text,const recovered::PackageView& font,float scale=1.f,float spacing=2.f);
void DrawUiText(std::string_view text,int left,int y,const recovered::PackageView& font,
                std::vector<std::uint8_t>& vram,std::size_t offset,std::array<unsigned,3> ink,
                float scale=1.f,std::array<int,4> clip={0,0,480,272},float spacing=2.f);
void DrawProfileField(std::string_view cp932, int center_x, int y, bool active,
                      const recovered::PackageView& font,
                      std::vector<std::uint8_t>& vram, std::size_t offset,
                      float horizontal_scale=1.f, int clip_top=0, int clip_bottom=272);
void DrawConfirmationText(std::string_view question,int left,int y,float scale,
                          const recovered::PackageView& font,
                          std::vector<std::uint8_t>& vram,std::size_t offset);

// TITLE 31C40 uses SOFTKEY mode 0/type 1. Public strings carry CP932 bytes.
class NicknameEditor {
public:
    bool Load(const std::filesystem::path& title_directory, std::string& error);
    void Open(unsigned actor, std::uint32_t buttons);
    void OpenPlayer(std::uint32_t buttons);
    void OpenName(std::string_view previous, std::uint32_t buttons);
    void OpenPhone(std::string_view previous, std::uint32_t buttons);
    void Step(std::uint32_t buttons);
    bool AppendCommitted(std::string_view cp932);
    bool EraseLast();
    bool Draw(const TitleRenderer& renderer, const recovered::PackageView& font,
              std::vector<std::uint8_t>& vram, std::size_t offset);
    const std::optional<std::string>& result() const { return result_; }
    const std::string& text() const { return value_; }
    bool editing() const { return phase_ == Phase::editing; }
    bool choosing_kana() const { return group_.has_value(); }
    unsigned se_mask() const { return se_mask_; }
private:
    struct Group {
        std::vector<std::vector<std::string>> rows;
        unsigned columns=0;
    };
    enum class Phase { idle, opening, editing, closing };
    Phase phase_ = Phase::idle;
    std::string value_;
    std::optional<std::string> result_;
    unsigned frame_ = 0, column_ = 1, row_ = 1;
    std::uint32_t previous_ = 0;
    std::array<unsigned,4> held_{};
    std::array<Group,11> groups_;
    std::optional<unsigned> group_;
    unsigned kana_x_=0, kana_y_=0;
    unsigned pressed_clip_=0, pressed_frame_=0;
    unsigned se_mask_=0;
    bool phone_=false;
    float text_scroll_=0;
    void Select();
    void Begin(std::string_view initial,std::uint32_t buttons,bool phone=false);
};
}
