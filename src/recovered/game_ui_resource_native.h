#pragma once
#include "game_graphics_native.h"
#include "game_image_native.h"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace dokodemo::recovered {
class GameResourceSource {
  public:
    virtual ~GameResourceSource() = default;
    virtual GameFsStatus OpenResource(const std::string &path, PackageView &out) = 0;
    GameFsStatus TextureResource(const std::string &path, std::shared_ptr<GameImage> &out);
    void ClearTextureCache() { texture_cache_.clear(); }
    uint32_t TextureCacheSize() const { return uint32_t(texture_cache_.size()); }
    uint64_t TextureCacheEvictions() const { return texture_cache_evictions_; }
    uint32_t TextureCachePixels() const {
        uint32_t pixels = 0;
        for (const auto &item : texture_cache_)
            pixels += item.second->Width() * item.second->Height();
        return pixels;
    }

  private:
    void TrimUnusedTextures();
    std::map<std::string, std::shared_ptr<GameImage>> texture_cache_;
    uint64_t texture_cache_evictions_=0;
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
    const std::vector<GameUiTexture> &Textures() const { return textures_; }
    const std::vector<std::array<uint16_t, 4>> &InitialNodes() const { return nodes_; }
    const std::vector<GameUiClip> &Clips() const { return clips_; }
    const PackageView &Image() const { return image_; }
    uint32_t RootCount() const { return roots_; }
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
    uint32_t source_callback_pc = 0;
    uint32_t draw_group = UINT32_MAX; // unset nodes inherit current group
};
// Native column-vector affine2D: x'=a*x+c*y+tx,y'=b*x+d*y+ty.
struct GameUiMatrix {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
};
struct GameUiDrawStats {
    uint32_t nodes = 0, quads = 0, callbacks = 0;
};
class GameUiCallbacks {
  public:
    virtual ~GameUiCallbacks() = default;
    virtual bool DrawUiCallback(uint32_t source_pc, uint32_t node, const GameUiMatrix &, uint32_t psp_color,
                                GameGraphics &, std::string &error) = 0;
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
    const GameUiControl *Control(uint32_t index)const;
    bool Update(std::string &error);
    // 96870/96F20/97FC0. Both matrices at a root are the same instance base;
    // children inherit current parent world, not previous-frame cached state.
    bool Draw(GameGraphics &, GameUiDrawStats &, std::string &error, GameUiCallbacks *callbacks = nullptr,
              GameUiMatrix instance_base = {}, uint32_t first_group = 0,
              uint32_t last_group = UINT32_MAX,int32_t explicit_root=-1) const;
    bool SetSourceCallback(uint32_t node, uint32_t source_pc);
    // Passive native Draw traversal transform; no callback, raster or mutation.
    bool WorldMatrix(uint32_t node,GameUiMatrix& out)const;
    bool ShowRoot(uint32_t index, bool visible = true);
    uint32_t VisibleRoots() const;
    GameUiNode *Node(uint32_t index);
    const std::vector<GameUiNode> &Nodes() const { return nodes_; }
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
