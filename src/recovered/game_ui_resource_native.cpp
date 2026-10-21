#include "game_ui_resource_native.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void Need(bool yes, const char *text) {
    if (!yes)
        throw std::runtime_error(text);
}
uint32_t Count(uint32_t word, uint32_t cap) {
    const auto signed_word = int32_t(word);
    Need(signed_word != INT32_MIN, "B2A count overflow");
    const auto value = uint32_t(signed_word < 0 ? -signed_word : signed_word);
    Need(value <= cap, "B2A native count cap");
    return value;
}
std::string Text(const PackageView &image, uint32_t at) {
    const auto size = image.Describe().size;
    std::string out;
    for (uint64_t i = at; i < size && out.size() < 512; ++i) {
        const auto c = image.ReadU8(i);
        if (!c)
            return out;
        Need(c < 128, "B2A resource name must be ASCII");
        out.push_back(char(c));
    }
    throw std::runtime_error("B2A resource name unterminated");
}
} // namespace
GameFsStatus GameResourceSource::TextureResource(const std::string &path, std::shared_ptr<GameImage> &out) {
    std::string key = path;
    for (auto &c : key)
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
    const auto found = texture_cache_.find(key);
    if (found != texture_cache_.end()) {
        out = found->second;
        return {};
    }
    if (texture_cache_.size() >= 512)
        return {GameFsCode::Bounds, 0, "native texture cache cap512"};
    PackageView image;
    auto status = OpenResource(path, image);
    if (!status)
        return status;
    auto decoded = std::make_shared<GameImage>();
    status = decoded->DecodeGim(std::move(image));
    if (!status)
        return status;
    if (uint64_t(TextureCachePixels()) + uint64_t(decoded->Width()) * decoded->Height() > 16 * 1024 * 1024)
        return {GameFsCode::Bounds, 0, "native decoded texture budget64MiB"};
    texture_cache_.emplace(std::move(key), decoded);
    out = std::move(decoded);
    return {};
}
GameFsStatus GameUiResource::Load(GameResourceSource &source, const std::string &path,
                                  const std::string &prefix) {
    Close();
    try {
        PackageView image;
        auto status = source.OpenResource(path, image);
        if (!status)
            return status;
        Need(image.Describe().size >= 32 && image.ReadLE32(0) == 0x30413242, "expected B2A0 UI resource");
        const auto textures = Count(image.ReadLE32(4), 4096), texture_at = image.ReadLE32(8);
        const auto nodes = Count(image.ReadLE32(12), 4096), node_at = image.ReadLE32(16);
        const auto clips = Count(image.ReadLE32(20), 4096), clip_at = image.ReadLE32(24);
        const auto texture_view = image.Subview(texture_at, uint64_t(textures) * 8);
        const auto node_view = image.Subview(node_at, uint64_t(nodes) * 8);
        const auto clip_view = image.Subview(clip_at, uint64_t(clips) * 52);
        std::vector<GameUiTexture> native_textures;
        // Source order: all texture acquisitions precede clone construction.
        for (uint32_t i = 0; i < textures; ++i) {
            auto name = Text(image, texture_view.ReadLE32(i * 8 + 4));
            const auto dot = name.find_last_of('.');
            if (dot != std::string::npos)
                name.resize(dot);
            Need(!name.empty(), "B2A empty texture stem");
            std::shared_ptr<GameImage> decoded;
            status = source.TextureResource(prefix + name + ".GIM", decoded);
            if (!status)
                return status;
            native_textures.push_back({int16_t(texture_view.ReadLE16(i * 8)),
                                       int16_t(texture_view.ReadLE16(i * 8 + 2)), name, std::move(decoded)});
        }
        std::vector<std::array<uint16_t, 4>> native_nodes(nodes);
        for (uint32_t i = 0; i < nodes; ++i)
            for (uint32_t j = 0; j < 4; ++j)
                native_nodes[i][j] = node_view.ReadLE16(i * 8 + j * 2);
        std::vector<GameUiClip> native_clips(clips);
        for (uint32_t i = 0; i < clips; ++i) {
            const auto at = i * 52;
            for (uint32_t c = 0; c < 6; ++c) {
                const auto raw = c == 5 ? uint32_t(int32_t(int16_t(clip_view.ReadLE16(at + 42))))
                                        : clip_view.ReadLE32(at + c * 8);
                const auto count = Count(raw, 65536), offset = clip_view.ReadLE32(at + c * 8 + 4);
                auto keys = image.Subview(offset, uint64_t(count) * 8);
                for (uint32_t k = 0; k < count; ++k) {
                    const auto values = Count(uint32_t(int32_t(int16_t(keys.ReadLE16(k * 8 + 2)))), 65535);
                    const auto pointer = keys.ReadLE32(k * 8 + 4);
                    // 95BCC..95C90 normalizes these pointers. Element semantics
                    // and lengths belong to each actual interpolation consumer.
                    if (values)
                        Need(pointer < image.Describe().size, "B2A key values outside image");
                }
                native_clips[i].channels[c] = {count, std::move(keys)};
            }
            native_clips[i].field30 = clip_view.ReadLE32(at + 48);
        }
        roots_ = image.ReadLE32(28);
        Need(roots_ <= nodes, "B2A root count exceeds nodes");
        image_ = std::move(image);
        textures_ = std::move(native_textures);
        nodes_ = std::move(native_nodes);
        clips_ = std::move(native_clips);
        return {};
    } catch (const std::exception &error) {
        return {GameFsCode::MalformedDirectory, 0, error.what()};
    }
}
void GameUiResource::Close() {
    roots_ = 0;
    clips_.clear();
    nodes_.clear();
    textures_.clear();
    image_.Reset();
}
bool GameUiInstance::Clone(std::shared_ptr<const GameUiResource> resource) {
    if (resource_ || !resource)
        return false;
    std::vector<GameUiNode> nodes(resource->InitialNodes().size());
    for (size_t i = 0; i < nodes.size(); ++i)
        nodes[i].encoded_initial = resource->InitialNodes()[i];
    nodes_ = std::move(nodes);
    resource_ = std::move(resource);
    return true;
}
namespace {
float Float(const PackageView &image, uint64_t at) {
    const auto word = image.ReadLE32(at);
    float value = 0;
    std::memcpy(&value, &word, 4);
    Need(std::isfinite(value), "B2A nonfinite keyframe");
    return value;
}
// Native interpolation of the recovered integer-time step/linear channels.
// The first key is the base value; subsequent keys supply relative animation.
std::array<float, 4> Sample(const PackageView &image, const GameUiChannel &channel, uint32_t record,
                            uint32_t frame, uint32_t kind, bool base) {
    const uint32_t stride = (kind == 1 || kind == 3) ? 16 : 12;
    const auto count = Count(uint32_t(int32_t(int16_t(channel.keys.ReadLE16(record * 8 + 2)))), 65535);
    const auto at = channel.keys.ReadLE32(record * 8 + 4);
    auto data = image.Subview(at, uint64_t(count) * stride);
    std::array<float, 4> result{};
    if (kind == 1)
        result[0] = result[1] = 1;
    if (kind == 4)
        result.fill(128);
    Need(count > 0, "B2A track has no initial key");
    const auto value = [&](uint32_t index) {
        auto v = result;
        if (kind == 0)
            v[0] = float(int32_t(data.ReadLE32(uint64_t(index) * stride)));
        else if (kind == 4) {
            const auto color = data.ReadLE32(uint64_t(index) * stride);
            for (unsigned j = 0; j < 4; ++j)
                v[j] = float((color >> (8 * j)) & 255);
        } else {
            v[0] = Float(data, uint64_t(index) * stride);
            if (kind == 1 || kind == 3)
                v[1] = Float(data, uint64_t(index) * stride + 4);
        }
        return v;
    };
    if (kind == 0) {
        uint32_t key = 0;
        while (key + 1 < count && int32_t(data.ReadLE32(uint64_t(key + 1) * stride + 8)) <= int32_t(frame))
            ++key;
        return value(key);
    }
    const auto finish = [&](std::array<float, 4> relative) {
        if (!base)
            return relative;
        const auto initial = value(0);
        if (kind == 1)
            for (unsigned j = 0; j < 2; ++j)
                relative[j] *= initial[j];
        else if (kind == 4)
            for (unsigned j = 0; j < 4; ++j)
                relative[j] = initial[j] * relative[j] / 128;
        else
            for (unsigned j = 0; j < 2; ++j)
                relative[j] += initial[j];
        return relative;
    };
    if (count <= 1)
        return finish(result);
    const auto time_at = stride - 4, mode_at = stride - 8;
    if (int32_t(frame) < int32_t(data.ReadLE32(stride + time_at)))
        return finish(result);
    uint32_t key = 1;
    while (key + 1 < count && int32_t(data.ReadLE32(uint64_t(key + 1) * stride + time_at)) <= int32_t(frame))
        ++key;
    const auto a = value(key);
    const auto mode = data.ReadLE32(uint64_t(key) * stride + mode_at);
    Need(mode == 1 || mode == 2, "B2A unsupported interpolation mode");
    if (mode == 1 || key + 1 == count || kind == 0)
        return finish(a);
    const auto begin = int32_t(data.ReadLE32(uint64_t(key) * stride + time_at));
    const auto end = int32_t(data.ReadLE32(uint64_t(key + 1) * stride + time_at));
    Need(end > begin, "B2A non-increasing key times");
    const auto b = value(key + 1);
    const auto t = float(int64_t(frame) - begin) / float(int64_t(end) - begin);
    for (unsigned j = 0; j < 4; ++j)
        result[j] = a[j] + (b[j] - a[j]) * t;
    return finish(result);
}
void Apply(std::vector<GameUiNode> &nodes, const GameUiResource &resource, uint32_t clip, uint32_t frame,
           bool base) {
    const auto &channels = resource.Clips().at(clip).channels;
    for (uint32_t c = 0; c < 5; ++c)
        for (uint32_t k = 0; k < channels[c].count; ++k) {
            const auto target = channels[c].keys.ReadLE16(k * 8);
            Need(target < nodes.size(), "B2A channel targets missing node");
            const auto value = Sample(resource.Image(), channels[c], k, frame, c, base);
            auto &n = nodes[target];
            if (c == 0)
                n.texture = int32_t(value[0]);
            if (c == 1)
                for (unsigned j = 0; j < 2; ++j)
                    (base ? n.base_scale[j] : n.scale[j]) = base ? value[j] : n.scale[j] * value[j];
            if (c == 2)
                (base ? n.base_angle : n.angle) = base ? value[0] : n.angle + value[0];
            if (c == 3)
                for (unsigned j = 0; j < 2; ++j)
                    (base ? n.base_position[j] : n.position[j]) = base ? value[j] : n.position[j] + value[j];
            if (c == 4) {
                uint32_t color = 0;
                for (unsigned j = 0; j < 4; ++j) {
                    const auto previous = (n.color >> (8 * j)) & 255;
                    const auto v = base ? uint32_t(value[j]) : (previous * uint32_t(value[j])) >> 7;
                    color |= (std::min)(255u, v) << (8 * j);
                }
                (base ? n.base_color : n.color) = color;
            }
        }
}
} // namespace
bool GameUiInstance::Bind(uint32_t root, uint32_t clip, uint32_t frame) {
    if (!resource_ || root >= nodes_.size() || clip >= resource_->Clips().size())
        return false;
    try {
        Apply(nodes_, *resource_, clip, frame, true);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}
GameUiControl *GameUiInstance::AddControl(uint32_t clip) {
    if (!resource_ || clip >= resource_->Clips().size() || controls_.size() >= 256)
        return nullptr;
    controls_.push_back({0, clip, 0, 0});
    return &controls_.back();
}
GameUiControl *GameUiInstance::Control(uint32_t index) {
    return index < controls_.size() ? &controls_[index] : nullptr;
}
bool GameUiInstance::Update(std::string &error) {
    if (!resource_) {
        error = "UI clone not live";
        return false;
    }
    try {
        for (auto &n : nodes_) {
            n.position = n.base_position;
            n.scale = n.base_scale;
            n.angle = n.base_angle;
            n.color = n.base_color;
        }
        for (auto &c : controls_)
            if (c.active) {
                Need(!resource_->Clips()[c.clip].channels[5].count,
                     "B2A nonempty time mapping is not implemented");
                if (c.mode == 1) {
                    const auto end=resource_->Clips()[c.clip].field30;
                    if(end&&c.frame<end-1)
                        ++c.frame;
                    else if(end&&c.frame==end-1){
                        c.frame=end;c.active=1;
                        c.mode = 2;
                    }
                }
                Need(c.mode == 1 || c.mode == 2, "UI control mode unsupported");
                Apply(nodes_, *resource_, c.clip, c.frame, false);
            }
        error.clear();
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
bool GameUiInstance::ShowRoot(uint32_t index, bool visible) {
    if (!resource_ || index >= resource_->RootCount())
        return false;
    nodes_[index].visible = visible;
    return true;
}
uint32_t GameUiInstance::VisibleRoots() const {
    uint32_t count = 0;
    if (resource_)
        for (uint32_t i = 0; i < resource_->RootCount(); ++i)
            if (nodes_[i].visible)
                ++count;
    return count;
}
GameUiNode *GameUiInstance::Node(uint32_t index) { return index < nodes_.size() ? &nodes_[index] : nullptr; }
bool GameUiInstance::SetSourceCallback(uint32_t node, uint32_t pc) {
    if (node >= nodes_.size())
        return false;
    nodes_[node].source_callback_pc = pc;
    return true;
}
bool GameUiInstance::Draw(GameGraphics &graphics, GameUiDrawStats &stats, std::string &error,
                          GameUiCallbacks *callbacks, GameUiMatrix base, uint32_t first_group,
                          uint32_t last_group,int32_t explicit_root) const {
    stats = {};
    try {
        Need(resource_ && resource_->Image().Describe().readable, "UI draw source lease retired");
        const auto config = graphics.Snapshot().config;
        std::vector<bool> path(nodes_.size(), false);
        uint32_t calls = 0;
        const auto multiply = [](uint32_t left, uint32_t right) {
            uint32_t color = 0;
            for (unsigned j = 0; j < 4; ++j)
                color |= (std::min)(255u, (((left >> (8 * j)) & 255) * ((right >> (8 * j)) & 255)) >> 7)
                         << (8 * j);
            return color;
        };
        const auto native = [](uint32_t psp) {
            uint32_t out = 0;
            for (unsigned j = 0; j < 4; ++j)
                out |= (uint32_t(float((psp >> (8 * j)) & 255) * 1.9921875f) & 255) << (8 * j);
            return NativeColorFromPspAbgr(out);
        };
        std::function<void(uint32_t, const GameUiMatrix &, uint32_t, uint32_t)> draw;
        draw = [&](uint32_t index, const GameUiMatrix &parent, uint32_t parent_color,
                   uint32_t inherited_group) {
            Need(index < nodes_.size() && !path[index] && ++calls <= 32768,
                 "UI reference cycle/index/call cap");
            path[index] = true;
            ++stats.nodes;
            const auto &node = nodes_[index];
            const auto group = node.draw_group == UINT32_MAX ? inherited_group : node.draw_group;
            const bool render = group >= first_group && group <= last_group;
            const auto flags = uint32_t(node.encoded_initial[2]) | uint32_t(node.encoded_initial[3]) << 16;
            auto world = parent;
            if (!(flags & 4)) {
                world.tx = world.ty = 0;
            }
            if (!(flags & 2)) {
                world.a = base.a;
                world.b = base.b;
                world.c = base.c;
                world.d = base.d;
            }
            const auto angle = -node.angle, cosine = std::cos(angle), sine = std::sin(angle);
            GameUiMatrix result;
            result.tx = world.a * node.position[0] + world.c * node.position[1] + world.tx;
            result.ty = world.b * node.position[0] + world.d * node.position[1] + world.ty;
            result.a = (world.a * cosine + world.c * sine) * node.scale[0];
            result.b = (world.b * cosine + world.d * sine) * node.scale[0];
            result.c = (-world.a * sine + world.c * cosine) * node.scale[1];
            result.d = (-world.b * sine + world.d * cosine) * node.scale[1];
            for (auto v : {result.a, result.b, result.c, result.d, result.tx, result.ty})
                Need(std::isfinite(v) && std::abs(v) <= 1e7f, "UI world transform bounds");
            const auto color = multiply(flags & 8 ? parent_color : 0x80808080u, node.color);
            if (node.texture > 0 && render) {
                Need(size_t(node.texture) <= resource_->Textures().size(),
                     "UI one-based texture outside resource");
                const auto &texture = resource_->Textures()[size_t(node.texture - 1)];
                Need(texture.image && !texture.image->Pixels().empty(), "UI texture not live");
                const float x = float(texture.origin_x), y = float(texture.origin_y);
                const float w = float(texture.image->Width()), h = float(texture.image->Height());
                std::array<GameMeshVertex, 4> vertices;
                for (unsigned i = 0; i < 4; ++i) {
                    const auto px = x + (i & 1 ? w : 0), py = y + (i & 2 ? h : 0);
                    const auto wx = result.a * px + result.c * py + result.tx,
                               wy = result.b * px + result.d * py + result.ty;
                    vertices[i].clip = {{wx * 2 / float(config.width), -wy * 2 / float(config.height), 0, 1}};
                    vertices[i].u = i & 1 ? 1.0f : 0.0f;
                    vertices[i].v = i & 2 ? 1.0f : 0.0f;
                    vertices[i].color = native(color);
                }
                GameTextureSpan image{texture.image->Pixels().data(), texture.image->Width(),
                                      texture.image->Height(), texture.image->Pixels().size()};
                GameRasterPolicy policy;
                policy.two_sided = true;
                policy.depth_test = policy.depth_write = false;
                policy.repeat = false;
                Need(graphics.DrawMeshTriangle({{vertices[0], vertices[1], vertices[2]}}, image, policy) &&
                         graphics.DrawMeshTriangle({{vertices[2], vertices[1], vertices[3]}}, image, policy),
                     "UI native quad draw failed");
                ++stats.quads;
            } else if (node.texture < 0)
                draw(~uint32_t(node.texture), result, color, group);
            if (node.source_callback_pc && render) {
                Need(callbacks, "UI reached callback has no native handler");
                if (!callbacks->DrawUiCallback(node.source_callback_pc, index, result, color, graphics,
                                               error))
                    throw std::runtime_error(error.empty() ? "UI callback unsupported" : error);
                ++stats.callbacks;
            }
            if (node.encoded_initial[1])
                draw(node.encoded_initial[1], result, color, group);
            if (node.encoded_initial[0])
                draw(node.encoded_initial[0], parent, parent_color, inherited_group);
            path[index] = false;
        };
        if(explicit_root>=0){
            Need(uint32_t(explicit_root)<resource_->RootCount(),"manual UI root outside resource");
            draw(uint32_t(explicit_root),base,0x80808080,0);
        }else for (uint32_t root = 0; root < resource_->RootCount(); ++root)
                if (nodes_[root].visible)draw(root, base, 0x80808080, 0);
        error.clear();
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}
} // namespace dokodemo::recovered
