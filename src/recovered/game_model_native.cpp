#include "game_model_native.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void Need(bool value, const char *text) {
    if (!value)
        throw std::runtime_error(text);
}
GameFsStatus Error(const std::exception &error) {
    return {std::string(error.what()).find("unimplemented") != std::string::npos
                ? GameFsCode::Unavailable
                : GameFsCode::MalformedDirectory,
            0, error.what()};
}
float FloatWord(uint32_t word) {
    float v;
    std::memcpy(&v, &word, 4);
    Need(std::isfinite(v), "nonfinite model scalar");
    return v;
}
uint32_t Word(const model::Bytes &bytes, size_t at) {
    Need(at <= bytes.size() && bytes.size() - at >= 4, "model scalar bounds");
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i)
        v |= uint32_t(bytes[at + i]) << (8 * i);
    return v;
}
} // namespace
GameMatrix ModelIdentity() noexcept {
    GameMatrix m{};
    m[0] = m[5] = m[10] = m[15] = 1;
    return m;
}
GameMatrix ModelMultiply(const GameMatrix &a, const GameMatrix &b) noexcept {
    GameMatrix c{};
    for (unsigned col = 0; col < 4; ++col)
        for (unsigned row = 0; row < 4; ++row)
            for (unsigned k = 0; k < 4; ++k)
                c[col * 4 + row] += a[k * 4 + row] * b[col * 4 + k];
    return c;
}
GameVector4 ModelTransform(const GameMatrix &m, const GameVector4 &p) noexcept {
    GameVector4 out{};
    for (unsigned row = 0; row < 4; ++row)
        for (unsigned col = 0; col < 4; ++col)
            out[row] += m[col * 4 + row] * p[col];
    return out;
}
GameMatrix ModelQuaternion(GameVector4 q) {
    float norm = 0;
    for (auto v : q) {
        Need(std::isfinite(v), "nonfinite model quaternion");
        norm += v * v;
    }
    Need(norm > .000001f && std::isfinite(norm), "zero model quaternion");
    const auto inv = 1 / std::sqrt(norm);
    for (auto &v : q)
        v *= inv;
    // 8C618 uses the transpose of Hamilton's column-vector convention.
    // Conjugate source DVS/DAK quaternions before native matrix construction.
    q[0] = -q[0];
    q[1] = -q[1];
    q[2] = -q[2];
    const auto x = q[0], y = q[1], z = q[2], w = q[3];
    return {{1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0, 2 * (x * y - z * w),
             1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0, 2 * (x * z + y * w), 2 * (y * z - x * w),
             1 - 2 * (x * x + y * y), 0, 0, 0, 0, 1}};
}
GameMatrix ModelTRS(GameVector4 position, GameVector4 base, GameVector4 rotation, GameVector4 scale) {
    auto m = ModelMultiply(ModelQuaternion(base), ModelQuaternion(rotation));
    for (unsigned j = 0; j < 3; ++j) {
        Need(std::isfinite(position[j]) && std::isfinite(scale[j]), "nonfinite model TRS");
        for (unsigned row = 0; row < 3; ++row)
            m[j * 4 + row] *= scale[j];
        m[12 + j] = position[j];
    }
    return m;
}
GameMatrix ModelPerspective(float fov, float aspect, float near_plane, float far_plane) {
    Need(std::isfinite(fov) && std::isfinite(aspect) && std::isfinite(near_plane) &&
             std::isfinite(far_plane) && fov > 0 && fov < 3.13f && aspect > 0 && near_plane > 0 &&
             far_plane > near_plane,
         "model perspective domain");
    const auto tangent = std::tan(fov * .5f);
    GameMatrix m{};
    m[0] = 1 / (aspect * tangent);
    m[5] = 1 / tangent;
    m[10] = (far_plane + near_plane) / (near_plane - far_plane);
    m[11] = -1;
    m[14] = 2 * far_plane * near_plane / (near_plane - far_plane);
    return m;
}
GameMatrix ModelInverseRigid(const GameMatrix &m) {
    auto out = ModelIdentity();
    for (unsigned col = 0; col < 3; ++col)
        for (unsigned row = 0; row < 3; ++row)
            out[col * 4 + row] = m[row * 4 + col];
    const auto t = ModelTransform(out, {{-m[12], -m[13], -m[14], 0}});
    out[12] = t[0];
    out[13] = t[1];
    out[14] = t[2];
    return out;
}
GameFsStatus GameModelAsset::Load(GameResourceSource &source, const std::string &path,
                                  const std::string &prefix) {
    PackageView image;
    auto status = source.OpenResource(path, image);
    return status ? Decode(std::move(image), &source, prefix) : status;
}
GameFsStatus GameModelAsset::Decode(PackageView image, GameResourceSource *source,
                                    const std::string &prefix) {
    if (Live())
        return {GameFsCode::Busy, 0, "model asset already live"};
    try {
        const auto extent = image.Describe().size;
        Need(extent >= 24 && extent <= 16 * 1024 * 1024, "model source extent cap");
        model::Bytes bytes(size_t(extent), 0);
        for (size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = image.ReadU8(i);
        auto parsed = model::parseDvs(bytes);
        GameModelStatistics stats{};
        Need(parsed.nodes.size() <= 4096 && parsed.materials.size() <= 4096, "model native table caps");
        stats.nodes = uint32_t(parsed.nodes.size());
        stats.materials = uint32_t(parsed.materials.size());
        for (const auto &n : parsed.nodes) {
            Need(!n.unsupportedType &&
                     (n.type == 0 || n.type == 1 || n.type == 3 || n.type == 4 || n.type == 5 || n.type==12),
                 "unimplemented model node type");
            if(n.type==12){
                Need(n.typePayload.size()==12,"particle emitter descriptor extent");
                ++stats.particle_emitters;
                //7F0B8/80C14:owned four-field descriptor+common transform.
                //8A394 updates hierarchy. External particle emission remains
                //unimplemented; this node has no own triangle geometry.
            }
            if (n.hasCommon) {
                Need(n.commonShorts[1] >= -1 && n.commonShorts[1] < int32_t(stats.nodes),
                     "model parent index");
                Need(!(n.commonFlags & 2), "unimplemented oriented-scale common transform");
            }
            if (!n.hasGeometry || !n.geometry.vertexCount)
                continue;
            const auto &g = n.geometry;
            Need(g.mode == 3, "unimplemented nonempty model geometry mode");
            Need(g.positions.size() == g.vertexCount, "model positions missing");
            Need(!g.primitiveStreams.empty(), "model primitive stream missing");
            for (const auto &p : g.primitiveStreams[0].primitives)
                Need(p.topology == model::Topology::Triangles || p.topology == model::Topology::TriangleFan ||
                         p.topology == model::Topology::TriangleStrip,
                     "unimplemented line/point model consumer");
            if (!g.weightRecords.empty()) {
                Need(g.weightRecords.size() == g.vertexCount * 24, "skin record extent");
                for (const auto &b : g.bones)
                    Need(int16_t(b.idWord & 65535) >= 0 && (b.idWord & 65535) < stats.nodes,
                         "skin palette target index");
                stats.weighted_vertices += uint32_t(g.vertexCount);
            }
            stats.vertices += uint32_t(g.vertexCount);
            ++stats.meshes;
            stats.triangles += uint32_t(model::triangles(g).size());
            Need(stats.vertices <= 1024 * 1024 && stats.triangles <= 1024 * 1024,
                 "model native geometry budget");
        }
        std::vector<GameModelMaterial> materials;
        for (const auto &m : parsed.materials) {
            GameModelMaterial material;
            material.flags = m.flags;
            for (const auto &ref : m.textures) {
                Need(ref.roleBit == 1||ref.roleBit==2||ref.roleBit==4, "unimplemented model texture role");
                Need(source && !ref.name.empty() && ref.name.size() < 32, "model texture source/name");
                std::shared_ptr<GameImage> texture_image;
                const auto status = source->TextureResource(prefix + ref.name + ".GIM", texture_image);
                if (!status) {
                    auto failure = status;
                    failure.detail = prefix + ref.name + ".GIM: " + failure.detail;
                    return failure;
                }
                if(ref.roleBit==1)material.texture=std::move(texture_image);
                else {material.secondary_textures.emplace_back(ref.roleBit,std::move(texture_image));++stats.secondary_textures;}
                // Native diffuse-only lighting adapter. Secondary source
                // images are actually loaded/owned;their PSP shading roles
                // are retained but no reflective/normal effect is invented.
            }
            materials.push_back(std::move(material));
        }
        header_color_=image.ReadLE32(8);
        data_ = std::move(parsed);
        materials_ = std::move(materials);
        stats_ = stats;
        image_ = std::move(image);
        return {};
    } catch (const std::exception &e) {
        return Error(e);
    }
}
GameFsStatus GameModelClone::Initialize(std::shared_ptr<const GameModelAsset> asset) {
    if (asset_ || !asset || !asset->Live())
        return {GameFsCode::Unavailable, 0, "model clone owner not available"};
    asset_ = std::move(asset);
    pose_.resize(asset_->Data().nodes.size());
    for (size_t i = 0; i < pose_.size(); ++i) {
        const auto &node = asset_->Data().nodes[i];
        auto &p = pose_[i];
        if (node.hasCommon) {
            p.position = node.commonVectors[0];
            p.rotation = node.commonVectors[1];
        }
        p.material = node.hasGeometry ? node.geometry.materialIndex : -1;
        p.emitter=node.emitter;
        if(node.type==5)lights_.type5_nodes.push_back(uint32_t(i));
    }
    lights_.present=!lights_.type5_nodes.empty();
    // 80480/83024 sets template148.color14 from the actual DVS header;
    // 80E50..58 copies it to the new owned clone's light-selection record.
    if(lights_.present)lights_.packed_color14=asset_->HeaderColor();
    auto status = Update(ModelIdentity());
    if (status)
        for (const auto &p : pose_)
            inverse_bind_.push_back(ModelInverseRigid(p.world));
    else
        Close();
    return status;
}
GameFsStatus GameModelClone::Update(const GameMatrix &root) {
    if (!asset_ || !asset_->Live())
        return {GameFsCode::Unavailable, 0, "model clone source lease retired"};
    try {
        std::vector<uint8_t> visited(pose_.size(), 0);
        std::function<void(size_t)> update = [&](size_t i) {
            Need(visited[i] != 1, "model parent cycle");
            if (visited[i] == 2)
                return;
            visited[i] = 1;
            const auto &node = asset_->Data().nodes[i];
            auto &pose = pose_[i];
            const auto local = ModelTRS(pose.position, pose.base_rotation, pose.rotation, pose.scale);
            Need(std::isfinite(pose.opacity)&&pose.opacity>=0,"nonfinite/negative model opacity");
            pose.world_opacity=pose.opacity;
            const auto parent = node.hasCommon ? node.commonShorts[1] : -1;
            if (parent >= 0) {
                Need(size_t(parent) < pose_.size(), "model parent index");
                update(size_t(parent));
                pose.world = ModelMultiply(pose_[size_t(parent)].world, local);
                pose.world_opacity*=pose_[size_t(parent)].world_opacity;
            } else
                pose.world = ModelMultiply(root, local);
            visited[i] = 2;
        };
        for (size_t i = 0; i < pose_.size(); ++i)
            update(i);
        return {};
    } catch (const std::exception &e) {
        return Error(e);
    }
}
GameVector4 GameModelClone::Skin(const model::Geometry &g, uint32_t vertex,
                                 const std::vector<GameMatrix> &palette) {
    Need(vertex < g.positions.size() && g.weightRecords.size() == g.positions.size() * 24,
         "skin vertex bounds");
    const auto at = size_t(vertex) * 24;
    const auto index = [&](unsigned slot) {
        const auto p = at + slot * 2;
        return int16_t(uint16_t(g.weightRecords[p]) | uint16_t(g.weightRecords[p + 1]) << 8);
    };
    const auto &p = g.positions[vertex];
    const GameVector4 position{{p.x, p.y, p.z, 1}};
    GameVector4 out{};
    const auto rigid = index(1) < 0;
    for (unsigned slot = 0; slot < (rigid ? 1u : 4u); ++slot) {
        const auto bone = index(slot);
        if (bone < 0)
            break;
        Need(size_t(bone) < palette.size(), "skin palette index");
        const auto weight = rigid ? 1.0f : FloatWord(Word(g.weightRecords, at + 8 + slot * 4));
        Need(weight >= 0 && weight <= 1.0001f, "skin weight domain");
        const auto value = ModelTransform(palette[size_t(bone)], position);
        for (unsigned j = 0; j < 4; ++j)
            out[j] += value[j] * weight;
    }
    Need(index(0) >= 0, "skin first palette index");
    out[3] = 1;
    return out;
}
GameFsStatus GameModelClone::Draw(GameGraphics &graphics, const GameMatrix &view_projection) const {
    if (!asset_ || !asset_->Live())
        return {GameFsCode::Unavailable, 0, "draw source lease retired"};
    try {
        for (size_t node = 0; node < pose_.size(); ++node) {
            const auto &n = asset_->Data().nodes[node];
            if (!n.hasGeometry || !n.geometry.vertexCount)
                continue;
            if(pose_[node].world_opacity<=0)continue;
            const auto &g = n.geometry;
            const auto material = pose_[node].material;
            Need(material >= -1 && material < int32_t(asset_->Materials().size()), "clone material index");
            const auto *m = material >= 0 ? &asset_->Materials()[size_t(material)] : nullptr;
            GameTextureSpan texture;
            if (m && m->texture) {
                texture = {m->texture->Pixels().data(), m->texture->Width(), m->texture->Height(),
                           m->texture->Pixels().size()};
            }
            std::vector<GameMatrix> palette;
            // 8A660 uses cached per-node inverse bind, NOT file bone.matrix.
            for (const auto &b : g.bones)
                palette.push_back(
                    ModelMultiply(pose_[b.idWord & 65535].world, inverse_bind_[b.idWord & 65535]));
            auto rigid = pose_[node].world;
            if (n.optionalMatrix.size() == 16) {
                GameMatrix optional{};
                std::copy(n.optionalMatrix.begin(), n.optionalMatrix.end(), optional.begin());
                rigid = ModelMultiply(rigid, optional);
            }
            std::vector<GameMeshVertex> vertices;
            vertices.reserve(g.vertexCount);
            for (uint32_t i = 0; i < g.vertexCount; ++i) {
                const auto &p = g.positions[i];
                const auto world = g.weightRecords.empty() ? ModelTransform(rigid, {{p.x, p.y, p.z, 1}})
                                                           : Skin(g, i, palette);
                GameMeshVertex v;
                v.clip = ModelTransform(view_projection, world);
                if (texture.pixels) {
                    Need(g.uv.size() == g.vertexCount, "textured model UV extent");
                    v.u = g.uv[i].x;
                    v.v = g.uv[i].y;
                }
                if (!g.rgba.empty()) {
                    const auto &c = g.rgba[g.rgba.size() == 1 ? 0 : i];
                    // Explicit native PSP128-alpha -> opaque adaptation. RGB
                    // uses real geometry channels; no substitute textures/mesh.
                    v.color = uint32_t(c[0]) | uint32_t(c[1]) << 8 | uint32_t(c[2]) << 16 |
                              uint32_t(float((std::min)(255u,uint32_t(c[3])*2))*(std::min)(1.0f,pose_[node].world_opacity)) << 24;
                    v.color = NativeColorFromPspAbgr(v.color);
                }
                vertices.push_back(v);
            }
            GameRasterPolicy policy;
            policy.two_sided = m && (m->flags & 128);
            for (const auto &t : model::triangles(g))
                Need(graphics.DrawMeshTriangle({{vertices[t[0]], vertices[t[1]], vertices[t[2]]}}, texture,
                                               policy),
                     "native model triangle draw failed");
        }
        return {};
    } catch (const std::exception &e) {
        return Error(e);
    }
}
namespace {
GameVector4 NormalizeQuaternion(GameVector4 q) {
    float n = 0;
    for (auto v : q) {
        Need(std::isfinite(v), "motion quaternion nonfinite");
        n += v * v;
    }
    Need(n > 1e-8f, "motion quaternion zero");
    for (auto &v : q)
        v /= std::sqrt(n);
    return q;
}
GameVector4 Slerp(GameVector4 a, GameVector4 b, float t) {
    a = NormalizeQuaternion(a);
    b = NormalizeQuaternion(b);
    float dot = 0;
    for (unsigned j = 0; j < 4; ++j)
        dot += a[j] * b[j];
    if (dot < 0) {
        dot = -dot;
        for (auto &v : b)
            v = -v;
    }
    dot = (std::clamp)(dot, -1.0f, 1.0f);
    float x = 1 - t, y = t;
    if (dot < .9995f) {
        const auto angle = std::acos(dot), s = std::sin(angle);
        x = std::sin((1 - t) * angle) / s;
        y = std::sin(t * angle) / s;
    }
    for (unsigned j = 0; j < 4; ++j)
        a[j] = a[j] * x + b[j] * y;
    return NormalizeQuaternion(a);
}
uint32_t MotionWord(const PackageView &image, size_t at) {
    uint32_t word = 0;
    for (unsigned i = 0; i < 4; ++i)
        word |= uint32_t(image.ReadU8(at + i)) << (i * 8);
    return word;
}
GameVector4 KeyValue(const model::DakSection &s, size_t key) {
    const auto at = key * size_t(s.components);
    GameVector4 out{{0, 0, 0, 1}};
    if (s.type == 1 && s.subtype == 15) {
        for (unsigned j = 0; j < 3; ++j)
            out[j] = FloatWord(s.keyWords.at(at + 1 + j));
    } else if (s.type == 1) {
        for (unsigned j = 0; j < 3; ++j) {
            const auto word = s.keyWords.at(at + 1 + j / 2);
            out[j] = float(uint16_t(word >> (16 * (j % 2)))) / 65535.0f;
        }
    } else {
        for (unsigned j = 0; j < 4; ++j) {
            const auto word = s.keyWords.at(at + 1 + j / 2);
            out[j] = float(int16_t(word >> (16 * (j % 2)))) / 32767.0f;
        }
    }
    return out;
}
} // namespace
GameFsStatus GameModelMotion::Load(GameResourceSource &source, const std::string &path,
                                   const GameModelAsset &target) {
    PackageView image;
    auto status = source.OpenResource(path, image);
    return status ? Decode(std::move(image), target) : status;
}
GameFsStatus GameModelMotion::Decode(PackageView image, const GameModelAsset &target) {
    try {
        Need(!ready_, "motion already live");
        const auto extent = image.Describe().size;
        Need(extent >= 20 && extent <= 8 * 1024 * 1024, "motion extent cap");
        model::Bytes bytes(size_t(extent), 0);
        for (size_t i = 0; i < bytes.size(); ++i)
            bytes[i] = image.ReadU8(i);
        auto data = model::parseDak(bytes);
        model::validateBinding(target.Data(), data);
        Need((data.flags & ~1u) == 0, "unimplemented additive/unknown motion flags");
        GameMotionStatistics stats;
        stats.duration = data.durationWord;
        stats.loop = (data.flags & 1) != 0; //800B4 file1 -> control1000
        Need(stats.duration > 0, "motion zero duration");
        for (const auto &object : data.objects) {
            // Global F1 has its own real light-selection target148.
            // Other global sections stay deferred and refuse on activation.
            if (object.targetNodeIndex >= 0)
                Need(object.transform.size() == 16, "motion base transform");
            for (const auto &s : object.sections) {
                const bool translation = s.type == 1 && (s.subtype == 1 || s.subtype == 15);
                const bool quaternion = s.type == 2 && (s.subtype == 1 || s.subtype == 2);
                const bool material = s.type == 0x30 && s.subtype == 0;
                const bool callback = (s.type >= 0x40 && s.type <= 0x42);
                const bool opacity=s.type==0x50&&s.subtype==15;
                const bool light_scalar=s.type==0x60&&s.subtype==15;
                const bool light_vector=s.type==0x61&&s.subtype==15;
                const bool light_color=s.type==0xF1&&s.subtype==15&&object.targetNodeIndex<0;
                if(light_scalar||light_vector)Need(object.targetNodeIndex>=0&&
                    target.Data().nodes.at(size_t(object.targetNodeIndex)).type==5,"light track target must be actual type5");
                // Inactive scene controls own their original template without
                // executing its tracks. A future active unsupported section
                // is refused atomically in Apply,never a successful no-op.
                if((object.targetNodeIndex<0&&!light_color)||!(translation||quaternion||material||callback||opacity||light_scalar||light_vector||light_color)){++stats.deferred_sections;continue;}
                Need(s.count >= 0 && s.count <= 65535 && s.components > 0 &&
                         s.keyWords.size() == size_t(s.count) * s.components,
                     "motion key extent");
                uint32_t previous = 0;
                for (int i = 0; i < s.count; ++i) {
                    const auto at = size_t(i) * s.components;
                    const auto time = s.keyWords.at(at);
                    Need(i == 0 || time >= previous, "motion time order");
                    previous = time;
                    if (translation || quaternion)
                        (void)KeyValue(s, size_t(i));
                    if (translation && s.subtype == 15)
                        for (unsigned j = 1; j < 10; ++j)
                            (void)FloatWord(s.keyWords.at(at + j));
                    if (material)
                        Need(s.keyWords.at(at + 1) == 255 ||
                                 s.keyWords.at(at + 1) < target.Materials().size(),
                             "motion material index");
                    if(opacity||light_scalar||light_vector||light_color) {
                        const int dimensions=(light_vector||light_color)?3:1;
                        for(int j=1;j<s.components;++j){
                            const auto word=s.keyWords.at(at+size_t(j));
                            // Actual seasonal opacity keys retain quiet-NaN
                            // tangent sentinels. Values must still be finite.
                            if(dimensions==1&&j>dimensions&&word==0x7FC00000u)continue;
                            (void)FloatWord(word);
                        }
                    }
                }
                ++stats.tracks;
                stats.keys += uint32_t(s.count);
                if (s.subtype == 15 || (quaternion && s.subtype == 2))
                    ++stats.approximate_curves;
                if (callback)
                    ++stats.null_callback_tracks;
            }
        }
        data_ = std::move(data);
        stats_ = stats;
        image_ = std::move(image);
        ready_ = true;
        return {};
    } catch (const std::exception &e) {
        return Error(e);
    }
}
namespace {
GameVector4 NativeFloatCurve(const model::DakSection& s,uint32_t time,unsigned dimensions) {
    Need(s.count>0&&dimensions<=3,"Native float curve dimensions/keys");
    size_t key=0;
    while(key+1<size_t(s.count)&&s.keyWords[(key+1)*size_t(s.components)]<=time)++key;
    const auto at=key*size_t(s.components);GameVector4 value{{0,0,0,1}};
    for(unsigned j=0;j<dimensions;++j)value[j]=FloatWord(s.keyWords.at(at+1+j));
    if(key+1<size_t(s.count)&&time>s.keyWords[at]) {
        const auto next=(key+1)*size_t(s.components);
        // 88E0C..88E7C recognizes ONLY the current scalar OUT tangent's
        // exact quiet-NaN bits and returns the left value unchanged.
        const bool step=dimensions==1&&s.keyWords.at(at+3)==0x7FC00000u;
        if(dimensions==1&&!step)Need(s.keyWords.at(next+2)!=0x7FC00000u,"Unrecovered scalar incoming-only NaN tangent");
        const float t=step?0.0f:float(time-s.keyWords[at])/float(s.keyWords[next]-s.keyWords[at]);
        for(unsigned j=0;j<dimensions;++j)value[j]+=(FloatWord(s.keyWords.at(next+1+j))-value[j])*t;
    }
    // Named NativeCurveApproximation:actual time/value/tangents are retained;
    // this readable linear interpolation is not VFPU cubic matching.
    return value;
}
}
GameFsStatus GameModelMotion::Apply(GameModelClone &clone, uint32_t time) const {
    try {
        Need(image_.Describe().readable && clone.Asset() && clone.Asset()->Live(),
             "motion source lease retired");
        Need(stats_.deferred_sections==0,"unimplemented reached active motion section");
        for (const auto &object : data_.objects) {
            if (object.targetNodeIndex < 0) {
                for(const auto& s:object.sections)if(s.type==0xF1&&s.count&&clone.Lights().present){
                    const auto rgb=NativeFloatCurve(s,time,3);uint32_t packed=0xFF000000u;
                    for(unsigned j=0;j<3;++j){
                        // 8A284..308 writes low bytes after RGB*255 conversion.
                        const auto value=std::trunc(rgb[j]*255.0f);
                        Need(value>=float(INT32_MIN)&&double(value)<=double(INT32_MAX),"Global light color conversion bounds");
                        packed|=uint32_t(uint8_t(int32_t(value)))<<(j*8);
                    }
                    clone.SetLightColor14(packed);
                }
                continue;
            }
            Need(size_t(object.targetNodeIndex) < clone.Pose().size(), "motion clone binding");
            auto &pose = clone.Pose()[size_t(object.targetNodeIndex)];
            for (unsigned j = 0; j < 4; ++j) {
                pose.position[j] = object.transform[j];
                pose.rotation[j] = object.transform[4 + j];
            }
            pose.base_rotation = {{0, 0, 0, 1}};
            pose.scale = {{1, 1, 1, 1}};
            for (const auto &s : object.sections) {
                if (!s.count || (s.type >= 0x40&&s.type<=0x42))
                    continue; // source callback174/178 is null on this cold path
                if(s.type==0x50||s.type==0x60||s.type==0x61){
                    const auto value=NativeFloatCurve(s,time,s.type==0x61?3:1);
                    if(s.type==0x50)pose.opacity=(std::max)(0.0f,value[0]);
                    if(s.type==0x60){pose.source210=value[0];pose.source210_written=true;}
                    if(s.type==0x61){pose.source230=value;pose.source230_written=true;}
                    continue;
                }
                size_t key = 0;
                while (key + 1 < size_t(s.count) && s.keyWords[(key + 1) * size_t(s.components)] <= time)
                    ++key;
                if (s.type == 0x30) {
                    const auto value = s.keyWords[key * size_t(s.components) + 1];
                    pose.material = value == 255 ? -1 : int32_t(value);
                    continue;
                }
                auto value = KeyValue(s, key);
                if (key + 1 < size_t(s.count) && time > s.keyWords[key * size_t(s.components)]) {
                    const auto next = KeyValue(s, key + 1);
                    const auto a = s.keyWords[key * size_t(s.components)],
                               b = s.keyWords[(key + 1) * size_t(s.components)];
                    const float t = b > a ? float(time - a) / float(b - a) : 0;
                    if (s.type == 2)
                        value = Slerp(value, next, t);
                    else
                        for (unsigned j = 0; j < 3; ++j)
                            value[j] += (next[j] - value[j]) * t;
                }
                if (s.type == 1) {
                    GameVector4 prefix{};
                    for (unsigned j = 0; j < 4; ++j)
                        prefix[j] = FloatWord(MotionWord(image_, s.prefix.offset + j * 4));
                    for (unsigned j = 0; j < 3; ++j)
                        pose.position[j] = prefix[j] + prefix[3] * value[j];
                    pose.position[3] = 1;
                } else
                    pose.rotation = NormalizeQuaternion(value);
            }
        }
        return {};
    } catch (const std::exception &e) {
        return Error(e);
    }
}
void GameModelPlayback::Configure(uint32_t duration,bool loop) {
    if(!duration)throw std::invalid_argument("Playback needs nonzero actual duration");
    state_={};state_.duration=duration;state_.loop=loop;
}
void GameModelPlayback::Start(uint32_t time) {
    if(!state_.duration)throw std::logic_error("Playback template not admitted");
    state_.time=time;state_.loops=0;state_.state=3;
}
GameFsStatus GameModelPlayback::Apply(const GameModelMotion& motion,GameModelClone& clone,uint32_t delta) {
    if(state_.state==1)return {};
    if(motion.Statistics().duration!=state_.duration||motion.Statistics().loop!=state_.loop)
        return {GameFsCode::Unavailable,0,"Playback/template identity mismatch"};
    const auto status=motion.Apply(clone,state_.time);if(!status)return status;
    state_.sampled_time=state_.time;++state_.samples;
    if(state_.state==2)return {}; //89ACC final pose stays an active consumer
    if(state_.stop_limit>=0&&state_.time>=uint32_t(state_.stop_limit)){
        state_.stop_limit=-1;state_.state=2;return {};
    }
    if(state_.time>=state_.duration){
        if(!state_.loop){state_.state=2;return {};}
        state_.time=0;++state_.loops;
    }
    const uint64_t next=uint64_t(state_.time)+uint64_t(delta)*uint32_t(state_.speed)/4800;
    if(next>UINT32_MAX)return {GameFsCode::Unavailable,0,"Playback time overflow"};
    state_.time=uint32_t(next);return {};
}
} // namespace dokodemo::recovered
