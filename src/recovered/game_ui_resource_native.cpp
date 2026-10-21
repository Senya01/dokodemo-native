#include "game_ui_resource_native.h"
#include <algorithm>
#include <cmath>
#include <cstring>
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
    if(uint64_t(TextureCachePixels())+uint64_t(decoded->Width())*decoded->Height()>16*1024*1024)
        return {GameFsCode::Bounds,0,"native decoded texture budget64MiB"};
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
                    if (c.frame < resource_->Clips()[c.clip].field30)
                        ++c.frame;
                    else
                        c.mode = 2;
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
GameUiNode *GameUiInstance::Node(uint32_t index) {
    return index < nodes_.size() ? &nodes_[index] : nullptr;
}
} // namespace dokodemo::recovered
