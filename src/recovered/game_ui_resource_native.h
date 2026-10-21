#pragma once
#include "game_image_native.h"
#include <array>
#include <memory>
#include <map>
#include <string>
#include <vector>

namespace dokodemo::recovered {
class GameResourceSource {
  public:
    virtual ~GameResourceSource() = default;
    virtual GameFsStatus OpenResource(const std::string &path, PackageView &out) = 0;
    GameFsStatus TextureResource(const std::string &path, std::shared_ptr<GameImage> &out);
    void ClearTextureCache() {
        texture_cache_.clear();
    }
    uint32_t TextureCacheSize() const {
        return uint32_t(texture_cache_.size());
    }
    uint32_t TextureCachePixels() const {
        uint32_t pixels = 0;
        for (const auto &item : texture_cache_)
            pixels += item.second->Width() * item.second->Height();
        return pixels;
    }

  private:
    std::map<std::string, std::shared_ptr<GameImage>> texture_cache_;
};
struct GameUiTexture {
    int16_t origin_x = 0, origin_y = 0;
    std::string stem;
    std::shared_ptr<GameImage> image;
};
struct GameUiChannel {
    uint32_t count = 0;
    PackageView keys;
};
struct GameUiClip {
    std::array<GameUiChannel, 6> channels;
    uint32_t field30 = 0;
};
// 95950 resource model. Encoded signed counts/relative offsets are kept as
//  typed native views; no PSP pointer fixups are written into the file image.
class GameUiResource final {
  public:
    GameFsStatus Load(GameResourceSource &source, const std::string &path, const std::string &prefix);
    const std::vector<GameUiTexture> &Textures() const {
        return textures_;
    }
    const std::vector<std::array<uint16_t, 4>> &InitialNodes() const {
        return nodes_;
    }
    const std::vector<GameUiClip> &Clips() const {
        return clips_;
    }
    const PackageView &Image() const {
        return image_;
    }
    uint32_t RootCount() const {
        return roots_;
    }
    void Close();

  private:
    PackageView image_;
    std::vector<GameUiTexture> textures_;
    std::vector<std::array<uint16_t, 4>> nodes_;
    std::vector<GameUiClip> clips_;
    uint32_t roots_ = 0;
};
struct GameUiNode {
    std::array<uint16_t, 4> encoded_initial{};
    int32_t texture = 0;
    bool visible = false;
    std::array<float, 2> base_position{}, position{}, base_scale{{1, 1}}, scale{{1, 1}};
    float base_angle = 0, angle = 0;
    uint32_t base_color = 0x80808080, color = 0x80808080;
};
struct GameUiControl {
    uint32_t frame = 0, clip = 0;
    uint8_t mode = 0, active = 0;
};
// 9575C/96130 native clone: independent mutable node state, shared immutable
//  resource/texture views. Detailed animation/draw consumers are separate APIs.
class GameUiInstance final {
  public:
    bool Clone(std::shared_ptr<const GameUiResource> resource);
    // Cold96C48 base-pose subset. Full subtree reset/world traversal belongs to
    // the still unsupported general UI renderer. Fourth arg is a frame, not loop.
    bool Bind(uint32_t root, uint32_t clip, uint32_t initial_frame);
    // 96514 creates a separate8-byte animation control, not a node record.
    GameUiControl *AddControl(uint32_t clip);
    GameUiControl *Control(uint32_t index);
    bool Update(std::string &error);
    bool ShowRoot(uint32_t index, bool visible = true);
    uint32_t VisibleRoots() const;
    GameUiNode *Node(uint32_t index);
    const std::vector<GameUiNode> &Nodes() const {
        return nodes_;
    }
    void Close() {
        controls_.clear();
        nodes_.clear();
        resource_.reset();
    }

  private:
    std::shared_ptr<const GameUiResource> resource_;
    std::vector<GameUiNode> nodes_;
    std::vector<GameUiControl> controls_;
};
} // namespace dokodemo::recovered
