#pragma once

#include "cb2a_scene.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace dokodemo::gameplay {

struct UiClipFrame { unsigned clip, frame; };
struct UiNodeOverride {
    unsigned node;
    std::optional<int> sprite;
    std::optional<float> x, y;
    std::optional<unsigned> bind_clip;
};

// GuiTitle draws its root panel behind the actors and nested controls in front.
class TitleRenderer {
public:
    bool Load(const std::filesystem::path& title_directory, std::string& error);
    bool LoadSoftkey(const std::filesystem::path& directory, std::string& error);
    bool LoadSelect(const std::filesystem::path& directory, std::string& error);
    bool LoadTrc(const std::filesystem::path& directory, std::string& error);
    using DrawSelectItem = std::function<void(unsigned,int,int,float,int,int)>;
    bool RenderSelect(unsigned mode, unsigned frame, bool closing, unsigned cursor,
                      unsigned count, float scroll_rows, std::vector<std::uint8_t>& vram,
                      std::size_t offset, const DrawSelectItem& draw_item) const;
    bool RenderConfirmation(float progress, unsigned cursor, std::vector<std::uint8_t>& vram,
                            std::size_t offset, const std::function<void(int,int,float)>& draw_question) const;
    bool RenderGenderList(unsigned frame, bool closing, unsigned cursor,
                          std::vector<std::uint8_t>& vram, std::size_t offset,
                          const std::function<void(unsigned,int,int)>& draw_row) const;
    bool RenderSoftkey(unsigned clip, unsigned frame, const std::vector<UiClipFrame>& controls,
                       const std::vector<UiNodeOverride>& overrides,
                       const std::function<void(unsigned,int,int)>& draw_node,
                       std::vector<std::uint8_t>& vram, std::size_t offset) const;
    bool Render(unsigned panel, unsigned frame, unsigned menu_variant,
                std::vector<std::uint8_t>& vram, std::size_t offset,
                bool foreground = false, std::array<unsigned,2> arrow_frames = {1,1}) const;
    bool RenderGuide(unsigned guide, unsigned frame, std::vector<std::uint8_t>& vram,
                     std::size_t offset) const;
    bool RenderProfile(unsigned active_row, unsigned present_mask, bool foreground,
                       std::vector<std::uint8_t>& vram, std::size_t offset,
                       const std::function<void(unsigned,int,int)>& draw_field) const;

private:
    using Scene = Cb2aSceneData;
    std::shared_ptr<const Scene> scene_;
    std::shared_ptr<const Scene> softkey_;
    std::shared_ptr<const Scene> select_;
    std::shared_ptr<const Scene> trc_;
    enum class Resource { title, softkey, select, trc };
    bool LoadResource(const std::filesystem::path&, Resource, std::string&);
};

} // namespace dokodemo::gameplay
