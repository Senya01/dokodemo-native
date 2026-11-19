// GuiTitle model transforms, palette weights and DAK interpolation use the
// retained GameModelClone/GameModelMotion conventions, including normalized
// quaternion slerp and the Windows linear approximation for Hermite tracks.
#include "pokepi_models.h"
#include "post_profile_mesh.h"
#include "room_television.h"
#include "model_formats.hpp"
#include "pokepi_raster.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>

namespace dokodemo::gameplay {
namespace {
using GameMatrix = std::array<float, 16>;
using GameVector4 = std::array<float, 4>;
void Need(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
uint32_t Word(const model::Bytes &data, size_t at) {
    Need(at <= data.size() && data.size() - at >= 4, "model word extent");
    uint32_t v;
    std::memcpy(&v, data.data() + at, 4);
    return v;
}
float FloatWord(uint32_t word) {
    float v;
    std::memcpy(&v, &word, 4);
    Need(std::isfinite(v), "nonfinite title model value");
    return v;
}
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
GameVector4 Skin(const model::Geometry &g, uint32_t vertex, const std::vector<GameMatrix> &palette,
                 const GameVector4 &input) {
    Need(vertex < g.positions.size() && g.weightRecords.size() == g.positions.size() * 24,
         "skin vertex bounds");
    const auto at = size_t(vertex) * 24;
    const auto index = [&](unsigned slot) {
        const auto p = at + slot * 2;
        return int16_t(uint16_t(g.weightRecords[p]) | uint16_t(g.weightRecords[p + 1]) << 8);
    };
    GameVector4 out{};
    const auto rigid = index(1) < 0;
    for (unsigned slot = 0; slot < (rigid ? 1u : 4u); ++slot) {
        const auto bone = index(slot);
        if (bone < 0)
            break;
        Need(size_t(bone) < palette.size(), "skin palette index");
        const auto weight = rigid ? 1.0f : FloatWord(Word(g.weightRecords, at + 8 + slot * 4));
        Need(weight >= 0 && weight <= 1.0001f, "skin weight domain");
        const auto value = ModelTransform(palette[size_t(bone)], input);
        for (unsigned j = 0; j < 4; ++j)
            out[j] += value[j] * weight;
    }
    Need(index(0) >= 0, "skin first palette index");
    out[3] = input[3];
    return out;
}
struct Pose {
    GameVector4 position{{0, 0, 0, 1}}, rotation{{0, 0, 0, 1}};
    float opacity = 1.f;
    float texture_offset_v = 0;
    int material = -1;
    float light_level = 0;
    std::array<float,3> light_color{};
};
struct Motion {
    model::Bytes bytes;
    model::Dak tracks;
};
struct Actor {
    model::Dvs model;
    struct Material {
        std::array<float, 3> rgb;
        float exponent;
        unsigned repeat_axes;
    };
    std::vector<Material> materials;
    std::array<float,3> ambient{};
    struct LightNode {
        unsigned index;
        float spot_exponent = -1;
        float quadratic_attenuation = 0;
    };
    std::vector<LightNode> lights;
    struct ActorRegion {
        unsigned node, shape, selector;
        float first, second;
        GameVector4 center{};
        std::array<GameVector4,4> corners{};
        std::array<std::array<float,3>,4> planes{};
    };
    std::vector<ActorRegion> actor_regions;
    std::vector<Motion> clips;
    std::vector<Pose> bind;
    std::vector<GameMatrix> inverse;
    std::vector<std::vector<std::array<uint16_t, 3>>> faces;
};

using Vec3 = std::array<float, 3>;
using TitleLight = RoomLight;
float Dot(const Vec3 &a, const Vec3 &b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 Unit(Vec3 v) {
    const float length = std::sqrt(Dot(v, v));
    if (length > 0)
        for (auto &x : v)
            x /= length;
    return v;
}
Vec3 Channels(uint32_t rgba) {
    return {float(rgba & 255), float((rgba >> 8) & 255), float((rgba >> 16) & 255)};
}
struct TitleLighting {
    Vec3 ambient{};
    std::vector<TitleLight> lights;
    Vec3 viewer{0,0,1};
    explicit TitleLighting(const RoomLighting& room) : ambient(room.ambient), lights(room.points), viewer{0,-1,0} {}
    explicit TitleLighting(const model::Bytes &dvs) {
        ambient = Channels(Word(dvs, 12));
        for (const auto &node : model::parseDvs(dvs).nodes) {
            if (node.type != 5)
                continue;
            const auto at = node.commonSource.offset + node.commonSource.bytes;
            Need((Word(dvs, at) & 65535) == 1, "unsupported title light type");
            const auto &p = node.commonVectors[0];
            auto rgb = Channels(Word(dvs, at + 4));
            const float level = FloatWord(Word(dvs, at + 8));
            for (auto &c : rgb)
                c = std::floor(c * level);
            lights.push_back({{p[0], p[1], p[2]}, rgb});
        }
        Need(lights.size() == 2, "GuiTitle light rig extent");
    }
    void Shade(PokepiVertex &v, const GameVector4 &pos, const GameVector4 &normal,
               const Actor::Material &gloss) const {
        const auto n = Unit({normal[0], normal[1], normal[2]});
        Vec3 brightness = ambient;
        for (const auto &light : lights) {
            const Vec3 delta{light.position[0]-pos[0],light.position[1]-pos[1],light.position[2]-pos[2]};
            const auto l = Unit(delta);
            const float diffuse = Dot(n, l);
            if (diffuse <= 0)
                continue;
            const float weight = light.spot_exponent < 0 ? 1.f : light.SpotFactor(l,Dot(delta,delta));
            // The GE applies the light attenuation to both diffuse and separate specular.
            // The infinite viewer direction is expressed in the scene's world axes.
            const auto half = Unit({l[0]+viewer[0], l[1]+viewer[1], l[2]+viewer[2]});
            const float specular = std::pow((std::max)(0.f, Dot(n, half)), gloss.exponent);
            for (unsigned c = 0; c < 3; ++c) {
                brightness[c] += light.color[c] * diffuse * weight;
                v.specular[c] += light.color[c] * specular * weight * gloss.rgb[c] / 255.f;
            }
        }
        const auto base = Channels(v.color);
        v.color &= 0xff000000u;
        for (unsigned c = 0; c < 3; ++c)
            v.color |= uint32_t((std::min)(255.f, base[c] * brightness[c] / 255.f)) << (8 * c);
    }
};

std::vector<GameMatrix> WorldMatrices(const Actor &actor, const std::vector<Pose> &pose,
                                      const GameMatrix &root, std::vector<float>* opacity=nullptr) {
    std::vector<GameMatrix> matrices(pose.size());
    std::vector<unsigned char> visited(pose.size());
    std::function<void(size_t)> visit = [&](size_t i) {
        if (visited[i] == 2)
            return;
        Need(!visited[i], "cyclic DVS hierarchy");
        visited[i] = 1;
        const auto &node = actor.model.nodes[i];
        const int parent = node.hasCommon ? node.commonShorts[1] : -1;
        auto local = ModelTRS(pose[i].position, {{0, 0, 0, 1}}, pose[i].rotation, {{1, 1, 1, 1}});
        if (parent < 0)
            matrices[i] = ModelMultiply(root, local);
        else {
            Need(size_t(parent) < pose.size(), "model parent index");
            visit(size_t(parent));
            matrices[i] = ModelMultiply(matrices[parent], local);
        }
        if (opacity) (*opacity)[i] = pose[i].opacity * (parent < 0 ? 1.f : (*opacity)[parent]);
        visited[i] = 2;
    };
    for (size_t i = 0; i < pose.size(); ++i)
        visit(i);
    return matrices;
}

std::array<float,3> SampleCurve(const model::DakSection& track, size_t key, unsigned dims, uint32_t time) {
    const auto i = key * size_t(track.components);
    const auto next = i + size_t(track.components);
    const auto& words=track.keyWords;
    std::array<float,3> value{};
    for (unsigned c=0;c<dims;++c) value[c]=FloatWord(words.at(i+c+1));
    if (key+1<size_t(track.count) && time>words.at(i) && !(dims==1 && words.at(i+3)==0x7fc00000)) {
        const float t=float(time-words.at(i))/float(words.at(next)-words.at(i));
        for (unsigned c=0;c<dims;++c) value[c]+=(FloatWord(words.at(next+c+1))-value[c])*t;
    }
    return value;
}
void Animate(std::vector<Pose> &pose, const Motion &clip, uint32_t time, std::array<float,3>* ambient=nullptr) {
    for (const auto &obj : clip.tracks.objects) {
        if (obj.targetNodeIndex < 0 && !ambient) continue;
        Pose global;
        Pose &p = obj.targetNodeIndex < 0 ? global : pose.at(obj.targetNodeIndex);
        if (obj.targetNodeIndex >= 0) {
            std::copy_n(obj.transform.begin(), 4, p.position.begin());
            std::copy_n(obj.transform.begin() + 4, 4, p.rotation.begin());
        }
        for (const auto &track : obj.sections) {
            if (ambient && track.type == 0x60 && track.subtype == 11) {
                const auto at = track.proceduralTrailer.offset;
                p.light_level = SampleRoomLightNoise(time, Word(clip.bytes, at),
                    FloatWord(Word(clip.bytes, at + 4)), FloatWord(Word(clip.bytes, at + 8)));
                continue;
            }
            if (!track.count || track.type == 0x40)
                continue; // cold title has no motion event callback
            const auto stride = size_t(track.components), count = size_t(track.count);
            size_t key = 0;
            while (key + 1 < count && track.keyWords.at((key + 1) * stride) <= time)
                ++key;
            if (obj.targetNodeIndex < 0) {
                if (track.type == 0xf1) {
                    *ambient=SampleCurve(track,key,3,time);
                    for (auto& c : *ambient) c=std::floor(c*255.f);
                }
                continue;
            }
            if (track.type == 0x21) {
                // BOOT 8A054 -> 8AE40 -> 88E0C: the selected scalar curve
                // writes node+1F4. 8D4A0 emits this as the GE V offset.
                Need(track.subtype == 15 && stride == 4, "unsupported texture V curve");
                const auto at = key * stride;
                const auto& words = track.keyWords;
                const float start = FloatWord(words.at(at+1));
                p.texture_offset_v = start;
                if (key+1 < count && time > words[at] && words.at(at+3) != 0x7fc00000) {
                    const auto next = at + stride;
                    const float end = FloatWord(words.at(next+1));
                    const float out = FloatWord(words.at(at+3)), in = FloatWord(words.at(next+2));
                    const float t = float(time-words[at]) / float(words[next]-words[at]);
                    p.texture_offset_v += t * (out + t * (3.f*(end-start) - 2.f*out - in
                                                   + t * (2.f*(start-end) + out + in)));
                }
                continue;
            }
            if (track.type == 0x50) {
                p.opacity=std::clamp(SampleCurve(track,key,1,time)[0],0.f,1.f);
                continue;
            }
            if (track.type == 0x60) { p.light_level=SampleCurve(track,key,1,time)[0]; continue; }
            if (track.type == 0x61) { p.light_color=SampleCurve(track,key,3,time); continue; }
            if (track.type == 0x30) {
                auto index = track.keyWords.at(key * stride + 1);
                p.material = index == 255 ? -1 : int(index);
                continue;
            }
            Need(track.type == 1 || track.type == 2, "unsupported active DAK track");
            auto v = KeyValue(track, key);
            const auto start = track.keyWords.at(key * stride);
            if (key + 1 < count && time > start) {
                const auto next = KeyValue(track, key + 1);
                const auto end = track.keyWords.at((key + 1) * stride);
                const float t = float(time - start) / float(end - start);
                if (track.type == 2)
                    v = Slerp(v, next, t);
                else
                    for (unsigned j = 0; j < 3; ++j)
                        v[j] += (next[j] - v[j]) * t;
            }
            if (track.type == 2)
                p.rotation = NormalizeQuaternion(v);
            else {
                const auto offset = track.prefix.offset;
                const float scale = FloatWord(Word(clip.bytes, offset + 12));
                for (unsigned j = 0; j < 3; ++j)
                    p.position[j] = FloatWord(Word(clip.bytes, offset + 4 * j)) + scale * v[j];
            }
        }
    }
}

class Reader {
  public:
    explicit Reader(const std::filesystem::path &file) : stream_(file, std::ios::binary) {
        Need(bool(stream_), "cannot open title model bundle");
    }
    uint32_t U32() {
        uint32_t v = 0;
        Need(bool(stream_.read(reinterpret_cast<char *>(&v), 4)), "short title model header");
        return v;
    }
    model::Bytes Blob(size_t max) {
        auto n = U32();
        Need(n && n <= max, "title model blob extent");
        model::Bytes v(n);
        Need(bool(stream_.read(reinterpret_cast<char *>(v.data()), n)), "short title model blob");
        return v;
    }
    bool AtEnd() { return stream_.peek() == std::char_traits<char>::eof(); }

  private:
    std::ifstream stream_;
};

std::string Upper(std::string s) {
    for (char &c : s)
        if (c >= 'a' && c <= 'z')
            c = char(c - 'a' + 'A');
    return s;
}
void ReadMesh(Reader& reader, Actor& actor, unsigned clip_count, bool build_actor_regions = false) {
    const auto bytes = reader.Blob(4 << 20);
    actor.model = model::parseDvs(bytes);
    actor.ambient=Channels(Word(bytes,12));
    for (const auto &m : actor.model.materials) {
        auto rgb = Channels(Word(bytes, m.source.offset + 12));
        const uint32_t params = Word(bytes, m.source.offset + 20);
        for (float &c : rgb)
            c = std::floor(c * (params & 65535) / 32768.f);
        const float exponent = float(params >> 24);
        unsigned repeat_axes = 0;
        for (const auto &ref : m.textures)
            if (ref.roleBit == 1)
                repeat_axes = (Word(bytes, ref.source.offset) >> 24) & 3;
        actor.materials.push_back({rgb, exponent ? exponent : 10.f, repeat_axes});
    }
    for (const auto &n : actor.model.nodes) {
        Pose p;
        if (n.hasCommon) {
            p.position = n.commonVectors[0];
            p.rotation = n.commonVectors[1];
        }
        if (n.hasGeometry)
            p.material = n.geometry.materialIndex;
        if (build_actor_regions && n.type == 12) {
            const auto at = n.commonSource.offset+n.commonSource.bytes;
            const auto descriptor = Word(bytes,at);
            const auto selector = descriptor & 65535;
            if (selector == 1 || selector == 2) {
                const auto shape = descriptor >> 16;
                const float first = FloatWord(Word(bytes,at+4)), second = FloatWord(Word(bytes,at+8));
                Need((shape==1 || shape==2) && first>0 && (shape==2 || second>0), "invalid room actor region");
                actor.actor_regions.push_back({unsigned(actor.bind.size()),shape,selector,first,second});
            }
        }
        if (n.type==5) {
            const auto at=n.commonSource.offset+n.commonSource.bytes;
            const auto descriptor = Word(bytes,at);
            const bool television_spot = descriptor == 0x00200002;
            if ((descriptor & 65535) == 1 || television_spot) {
                Actor::LightNode light{static_cast<unsigned>(actor.bind.size())};
                if (television_spot) {
                    // Loader 7EDC8 and GE writer 8EC60. Bit 0x20 chooses
                    // quadratic falloff, and the DVS cone angle is full-width
                    // at half intensity. The GE spot cutoff is zero.
                    const float range = FloatWord(Word(bytes,at+12));
                    const float angle = FloatWord(Word(bytes,at+16));
                    Need(range > 0 && angle > 0 && angle < 3.14159265f, "invalid DVS spotlight");
                    light.quadratic_attenuation = 1.f / (range*range);
                    light.spot_exponent = -1.f / std::log2(std::cos(.5f*angle));
                }
                actor.lights.push_back(light);
                p.light_color=Channels(Word(bytes,at+4));
                for (auto& c:p.light_color) c/=255.f;
                p.light_level=FloatWord(Word(bytes,at+8));
            }
        }
        actor.bind.push_back(p);
        actor.faces.push_back(model::triangles(n.geometry));
    }
    for (const auto &m : WorldMatrices(actor, actor.bind, ModelIdentity()))
        actor.inverse.push_back(ModelInverseRigid(m));
    if (!actor.actor_regions.empty()) {
        const auto world = WorldMatrices(actor,actor.bind,ModelIdentity());
        for (auto &r : actor.actor_regions) {
            const auto &m = world.at(r.node);
            r.center = ModelTransform(m,{0,0,0,1});
            if (r.shape == 2) continue;
            const float x=r.second*.5f, y=r.first*.5f;
            const std::array<GameVector4,4> local{{{x,y,0,1},{x,-y,0,1},{-x,-y,0,1},{-x,y,0,1}}};
            for (unsigned j=0; j<4; ++j) {
                const auto a=ModelTransform(m,local[j]), b=ModelTransform(m,local[(j+1)%4]);
                r.corners[j]=a;
                const float nx=b[1]-a[1], ny=a[0]-b[0];
                r.planes[j]={nx,ny,-nx*a[0]-ny*a[1]};
                Need(nx*r.center[0]+ny*r.center[1]+r.planes[j][2]>0,"invalid room region winding");
            }
        }
    }
    for (unsigned j = 0; j < clip_count; ++j) {
        Motion m;
        m.bytes = reader.Blob(4 << 20);
        m.tracks = model::parseDak(m.bytes);
        model::validateBinding(actor.model, m.tracks);
        for (const auto &obj : m.tracks.objects)
            for (const auto &section : obj.sections)
                Need(section.supported || obj.targetNodeIndex < 0, "unsupported title DAK section");
        actor.clips.push_back(std::move(m));
    }
}
void ReadTextures(Reader& reader,std::map<std::string,PokepiTexture>& textures) {
    unsigned count = reader.U32();
    Need(count <= 256, "too many title textures");
    for (unsigned i = 0; i < count; ++i) {
        auto name = reader.Blob(32);
        auto width = reader.U32(), height = reader.U32();
        Need(width && width <= 1024 && height && height <= 1024, "title texture size");
        auto pixels = reader.Blob(4 * width * height);
        Need(pixels.size() == 4 * width * height, "title texture dimensions mismatch");
        Need(textures
                 .emplace(std::string(name.begin(), name.end()),
                          PokepiTexture{width, height, std::move(pixels)})
                 .second,
             "duplicate title texture");
    }
}
bool DrawMesh(const Actor& actor,const std::vector<Pose>& pose, const GameMatrix& root,
              const GameMatrix& camera,const std::map<std::string,PokepiTexture>& textures,
              const TitleLighting* lights, std::vector<uint8_t>& vram,size_t offset,
              const std::vector<PokepiTexture>* glyphs = nullptr, uint32_t diffuse_rgb = 0) {
    std::vector<float> opacity(pose.size());
    auto world = WorldMatrices(actor, pose, root, &opacity);
    for (size_t i = 0; i < pose.size(); ++i) {
        const auto &n = actor.model.nodes[i];
        const auto &g = n.geometry;
        if (opacity[i] <= 0 || actor.faces[i].empty())
            continue;
        const PokepiTexture *texture = nullptr;
        bool two_sided = false;
        unsigned repeat_axes = 0;
        if (pose[i].material >= 0) {
            const auto &m = actor.model.materials.at(pose[i].material);
            two_sided = (m.flags & 128) != 0;
            repeat_axes = actor.materials.at(pose[i].material).repeat_axes;
            if (glyphs) {
                texture = &glyphs->at(i);
            } else {
                for (const auto &ref : m.textures)
                    if (ref.roleBit == 1)
                        texture = &textures.at(Upper(ref.name));
            }
        }
        GameMatrix rigid = world[i];
        if (!n.optionalMatrix.empty()) {
            GameMatrix local;
            std::copy_n(n.optionalMatrix.begin(), 16, local.begin());
            rigid = ModelMultiply(rigid, local);
        }
        std::vector<GameMatrix> palette;
        for (const auto &b : g.bones) {
            auto bone = b.idWord & 65535;
            palette.push_back(ModelMultiply(world.at(bone), actor.inverse.at(bone)));
        }
        std::vector<PokepiVertex> vertices(g.vertexCount);
        for (size_t j = 0; j < vertices.size(); ++j) {
            auto p = g.positions.at(j);
            const auto transform = [&](const GameVector4 &vec) {
                return g.weightRecords.empty() ? ModelTransform(rigid, vec)
                                               : Skin(g, static_cast<uint32_t>(j), palette, vec);
            };
            auto pos = transform({p.x, p.y, p.z, 1});
            auto &v = vertices[j];
            v.clip = ModelTransform(camera, pos);
            if (texture) {
                auto uv = g.uv.at(j);
                v.u = uv.x;
                v.v = uv.y + pose[i].texture_offset_v;
            }
            if (!g.rgba.empty()) {
                auto color = g.rgba.at(g.rgba.size() == 1 ? 0 : j);
                if (texture)
                    for (unsigned c = 0; c < 3; ++c)
                        color[c] = uint8_t((std::min)(255u, uint32_t(color[c]) * 255u / 128u));
                v.color = uint32_t(color[0]) | uint32_t(color[1]) << 8 | uint32_t(color[2]) << 16 |
                          std::min(255u, 2u * color[3]) << 24;
            }
            if (glyphs) v.color = (v.color & 0xff000000u) | diffuse_rgb;
            if (opacity[i] < 1.f) v.color = (v.color & 0xffffffu) | uint32_t((v.color >> 24) * opacity[i]) << 24;
            if (lights && !g.normals.empty() && pose[i].material >= 0) {
                const auto &normal = g.normals.at(j);
                lights->Shade(
                    v, pos, transform({float(normal[0]), float(normal[1]), float(normal[2]), 0}),
                    actor.materials.at(pose[i].material));
            }
        }
        for (const auto &face : actor.faces[i]) {
            if (!DrawPokepiTriangle({{vertices.at(face[0]), vertices.at(face[1]), vertices.at(face[2])}},
                                    texture, repeat_axes, two_sided, vram, offset))
                return false;
        }
    }
    return true;
}
} // namespace

float RoomLight::SpotFactor(const std::array<float,3>& unit_to_light, float distance_squared) const {
    if (spot_exponent < 0)
        return 1;
    // PSP GE compares the direction register to the vertex-to-light vector.
    const float cosine = std::clamp(Dot(spot_direction,unit_to_light),0.f,1.f);
    const float denominator = distance_squared * quadratic_attenuation;
    const float attenuation = denominator > 0 ? std::min(1.f,1.f/denominator) : 0.f;
    return std::pow(cosine,spot_exponent) * attenuation;
}

struct PokepiModels::Data {
    explicit Data(const model::Bytes &dvs) : lighting(dvs) {}
    TitleLighting lighting;
    std::array<Actor, 6> actors;
    std::map<std::string, PokepiTexture> textures;
    GameMatrix camera;
};

bool PokepiModels::Load(const std::filesystem::path &directory, std::string &error) {
    try {
        Reader reader(directory / "pokepi-models.bin");
        Need(reader.U32() == 0x444d4b50 && reader.U32() == 2, "unexpected title model format");
        auto data = std::make_shared<Data>(reader.Blob(4096));
        for (size_t type = 0; type < data->actors.size(); ++type) {
            auto &actor = data->actors[type];
            ReadMesh(reader, actor, type == 5 ? 1u : 4u);
        }
        ReadTextures(reader, data->textures);
        Need(reader.AtEnd(), "trailing title model data");
        for (const auto &actor : data->actors)
            for (const auto &mat : actor.model.materials)
                for (const auto &ref : mat.textures)
                    if (ref.roleBit == 1)
                        Need(data->textures.count(Upper(ref.name)) != 0, "missing title material texture");
        // Title camera at (0,-4400,560) looks down +Y with +Z up, 20 degree
        // vertical FOV.
        GameMatrix view = ModelIdentity();
        view[5] = view[10] = 0;
        view[6] = -1;
        view[9] = 1;
        view[13] = -560;
        view[14] = -4400;
        data->camera = ModelMultiply(ModelPerspective(.34906587f, 480.f / 272.f, 100, 10400), view);
        data_ = std::move(data);
        return true;
    } catch (const std::exception &e) {
        error = e.what();
        return false;
    }
}

unsigned PokepiModels::Duration(unsigned type, unsigned motion) const {
    return data_ && type < data_->actors.size() && motion < data_->actors[type].clips.size()
               ? data_->actors[type].clips[motion].tracks.durationWord
               : 0;
}

bool PokepiModels::Draw(unsigned type, unsigned motion, uint32_t time, float x, float y, float z,
                        unsigned panel, std::vector<uint8_t> &vram, size_t offset) const {
    constexpr size_t surface = 512 * 272 * 4;
    if (!Duration(type, motion) || vram.size() < 2 * surface + surface / 2 ||
        (offset != 0 && offset != surface))
        return false;
    try {
        const auto &actor = data_->actors[type];
        auto pose = actor.bind;
        Animate(pose, actor.clips[motion], time);
        auto root = ModelIdentity();
        if (panel == 0) {
            root[0] = root[5] = 0;
            root[1] = -1;
            root[4] = 1;
        }
        if (panel == 3) {
            constexpr float angle = .0872664626f;
            root[0] = root[5] = std::cos(angle);
            root[1] = std::sin(angle);
            root[4] = -root[1];
        }
        root[12] = x;
        root[13] = y;
        root[14] = z;
        return DrawMesh(actor, pose, root, data_->camera, data_->textures, &data_->lighting, vram, offset);
    } catch (const std::exception &) {
        return false;
    }
}

struct RoomMesh::Data {
    Actor mesh;
    bool actor_regions_built = false;
    std::map<std::string,PokepiTexture> textures;
};
bool RoomMesh::Load(const std::filesystem::path& path,std::string& error, bool build_actor_regions) try {
    Reader reader(path);
    Need(reader.U32()==0x314d5352,"unexpected room mesh bundle");
    auto data=std::make_shared<Data>();
    const unsigned count=reader.U32();
    Need(count<=128,"room mesh motion count");
    ReadMesh(reader,data->mesh,count,build_actor_regions);
    data->actor_regions_built = build_actor_regions;
    ReadTextures(reader,data->textures);
    Need(reader.AtEnd(),"unexpected trailing room mesh data");
    data_=std::move(data);
    return true;
} catch(const std::exception& e) {error=e.what();return false;}
namespace {
using XY = std::array<float,2>;
XY CenterXY(const Actor::ActorRegion& r) { return {r.center[0], r.center[1]}; }
double RegionDistance(XY a, XY b) { return std::hypot(double(a[0])-b[0],double(a[1])-b[1]); }
bool RegionContains(const Actor::ActorRegion& r, XY point) {
    if (r.shape == 2) return RegionDistance(CenterXY(r),point) <= r.first;
    return std::all_of(r.planes.begin(),r.planes.end(),[&](const auto &edge){
        return double(edge[0])*point[0]+double(edge[1])*point[1]+edge[2]>=0;
    });
}
// 27648 skips only centers whose whole candidate region is inside a selector-2 blocker.
bool RegionCovered(const Actor::ActorRegion& candidate,const Actor::ActorRegion& blocker) {
    if (candidate.shape == 1) {
        return std::all_of(candidate.corners.begin(),candidate.corners.end(),[&](const auto& corner) {
            const XY p{corner[0],corner[1]};
            return blocker.shape == 2 ? RegionDistance(p,CenterXY(blocker)) < blocker.first : RegionContains(blocker,p);
        });
    }
    if (blocker.shape == 2)
        return RegionDistance(CenterXY(candidate),CenterXY(blocker)) + candidate.first < blocker.first;
    const auto c=CenterXY(candidate);
    if (!RegionContains(blocker,c)) return false;
    for (unsigned j=0;j<4;++j) {
        const auto& a=blocker.corners[j];
        const auto& b=blocker.corners[(j+1)%4];
        const double dx=double(b[0])-a[0], dy=double(b[1])-a[1];
        const double cx=double(c[0])-a[0], cy=double(c[1])-a[1];
        const double t=std::clamp((cx*dx+cy*dy)/(dx*dx+dy*dy),0.,1.);
        if (std::hypot(cx-t*dx,cy-t*dy) <= candidate.first) return false;
    }
    return true;
}
}
RoomActorRegion RoomMesh::QueryActorRegion(const std::array<float,2>& p) const {
    Need(data_ && data_->actor_regions_built,"room region owner unavailable");
    const auto &regions=data_->mesh.actor_regions;
    bool have_region=false;
    for (const auto& r:regions) if (r.selector == 1) {
        have_region=true;
        if (RegionContains(r,p)) return {true,p};
    }
    if (!have_region) return {true,p};
    RoomActorRegion result{false,p};
    double nearest=std::numeric_limits<double>::infinity();
    for (const auto& candidate:regions) {
        if (candidate.selector != 1) continue;
        const auto distance=RegionDistance(CenterXY(candidate),p);
        if (distance >= nearest) continue;
        if (std::any_of(regions.begin(),regions.end(),[&](const auto& b){
            return b.selector==2 && RegionCovered(candidate,b);
        })) continue;
        nearest=distance;
        result.target=CenterXY(candidate);
    }
    return result;
}
bool RoomMesh::ContainsActor(const std::array<float,2>& p) const {
    return QueryActorRegion(p).inside;
}
bool RoomMesh::DirectActorRoute(const XY& from,const XY& to) const {
    Need(data_ && data_->actor_regions_built,"room route owner unavailable");
    // 2732C and 25CF4 keep the two endpoints when 261F0(mode 1)
    // finds no crossing of a selector-2 obstacle.
    const double dx=double(to[0])-from[0], dy=double(to[1])-from[1];
    const double length2=dx*dx+dy*dy;
    if (!length2) return true;
    for (const auto& r:data_->mesh.actor_regions) {
        if (r.selector != 2) continue;
        if (r.shape == 2) {
            // 26950 requires acute angles at both endpoints before testing
            // perpendicular distance against the source radius plus D9978 (100).
            const double cx=double(r.center[0])-from[0], cy=double(r.center[1])-from[1];
            const double t=(cx*dx+cy*dy)/length2;
            if (t>0 && t<1 && std::hypot(cx-t*dx,cy-t*dy) <= r.first+100.f) return false;
            continue;
        }
        // 264C0..26574 pairs each edge's signed endpoint products
        // with its adjacent corners' signed distances to the travel line.
        const auto side=[&](const GameVector4& c){return (c[0]-from[0])*dy-(c[1]-from[1])*dx;};
        for (unsigned j=0;j<4;++j) {
            if (side(r.corners[j])*side(r.corners[(j+1)%4])>0) continue;
            const auto& edge=r.planes[j];
            const auto plane=[&](const XY& p){return double(edge[0])*p[0]+double(edge[1])*p[1]+edge[2];};
            if (plane(from)*plane(to)<=0) return false;
        }
    }
    return true;
}
uint32_t RoomMesh::Duration(unsigned clip) const {
    return data_ && clip<data_->mesh.clips.size() ? data_->mesh.clips[clip].tracks.durationWord : 0;
}
std::vector<std::pair<unsigned,uint32_t>> RoomMesh::SoundKeys(unsigned clip) const {
    std::vector<std::pair<unsigned,uint32_t>> keys;
    for (const auto &object : data_->mesh.clips.at(clip).tracks.objects) {
        for (const auto &track : object.sections) {
            if (track.type != 0x40 || track.components != 2)
                continue;
            for (size_t i = 0; i + 1 < track.keyWords.size(); i += 2) {
                const auto word = track.keyWords[i + 1];
                if ((word >> 24) == 6)
                    keys.emplace_back(track.keyWords[i], word);
            }
        }
    }
    return keys;
}
static void ApplyJointLayer(const Actor &mesh, std::vector<Pose> &pose, int clip, unsigned time,
                            const std::vector<unsigned> &nodes) {
    if (clip < 0) return;
    auto secondary = mesh.bind;
    Animate(secondary,mesh.clips.at(clip),time);
    for (const auto node : nodes)
        pose.at(node) = secondary.at(node);
}
static void BlendPose(const Actor &mesh, std::vector<Pose> &pose, const RoomPoseBlend *blend,
                      const RoomJointLayer *layer) {
    if (layer)
        ApplyJointLayer(mesh,pose,layer->clip,layer->time,layer->nodes);
    if (blend) {
        auto previous = mesh.bind;
        Animate(previous, mesh.clips.at(blend->clip), blend->time);
        if (layer)
            ApplyJointLayer(mesh,previous,blend->previous_secondary,blend->previous_secondary_time,layer->nodes);
        for (size_t i = 0; i < pose.size(); ++i) {
            auto &now = pose[i];
            const auto &old = previous[i];
            for (unsigned j = 0; j < 3; ++j)
                now.position[j] = old.position[j] + (now.position[j] - old.position[j]) * blend->weight;
            float dot = 0;
            for (unsigned j = 0; j < 4; ++j)
                dot += now.rotation[j] * old.rotation[j];
            const float signed_weight = dot < 0 ? -blend->weight : blend->weight;
            for (unsigned j = 0; j < 4; ++j)
                now.rotation[j] = old.rotation[j] * (1 - blend->weight) + now.rotation[j] * signed_weight;
            now.rotation = NormalizeQuaternion(now.rotation);
        }
    }
}
bool RoomMesh::NodeWorld(unsigned node,const std::vector<std::pair<unsigned,uint32_t>>& active,
                         GameMatrix& out, const RoomPoseBlend* blend, const RoomJointLayer* layer) const try {
    if (!data_ || node>=data_->mesh.bind.size()) return false;
    const auto& mesh=data_->mesh;
    auto pose=mesh.bind;
    for (const auto& clip : active) Animate(pose,mesh.clips.at(clip.first),clip.second);
    BlendPose(mesh,pose,blend,layer);
    out=WorldMatrices(mesh,pose,ModelIdentity()).at(node);
    return true;
} catch(const std::exception&) { return false; }
RoomLighting RoomMesh::Lighting(const std::vector<std::pair<unsigned, uint32_t>>& active) const {
    const auto& mesh=data_->mesh;
    auto pose=mesh.bind;
    RoomLighting out;
    out.ambient=mesh.ambient;
    for (const auto& clip:active) Animate(pose,mesh.clips.at(clip.first),clip.second,&out.ambient);
    const auto world=WorldMatrices(mesh,pose,ModelIdentity());
    for (const auto& descriptor : mesh.lights) {
        const auto n = descriptor.index;
        if (pose[n].light_level <= 0) continue;
        auto rgb=pose[n].light_color;
        for (auto& c:rgb) c=std::floor(c*255.f*pose[n].light_level);
        RoomLight light{{world[n][12],world[n][13],world[n][14]},rgb};
        if (descriptor.spot_exponent >= 0) {
            // 8C450 initializes +Z. 8ECD8 transforms this vector using
            // the light's three world-space basis vectors.
            light.spot_direction = Unit({world[n][8],world[n][9],world[n][10]});
            light.spot_exponent = descriptor.spot_exponent;
            light.quadratic_attenuation = descriptor.quadratic_attenuation;
        }
        out.points.push_back(light);
    }
    return out;
}
bool RoomMesh::Draw(const std::vector<std::pair<unsigned,uint32_t>>& active, const GameMatrix& root,
                    const GameMatrix& camera,std::vector<uint8_t>& vram,size_t offset,const RoomLighting* lights, const RoomPoseBlend* blend, const RoomJointLayer* layer,
                    const RoomMaterialVariant* variant) const try {
    if(!data_) return false;
    const auto& mesh=data_->mesh;
    auto pose=mesh.bind;
    for(const auto& clip:active) Animate(pose,mesh.clips.at(clip.first),clip.second);
    BlendPose(mesh,pose,blend,layer);
    if (variant && variant->phase) {
        // Frame callback 23DD8 -> 6CAC -> 7454. The selected KURO's material
        // track has already run. Only a match in this scene's table changes it.
        if (variant->phase >= 6 || variant->node < 0)
            return false;
        auto& material = pose.at(static_cast<std::size_t>(variant->node)).material;
        const auto& values = variant->source_materials;
        const auto found = std::find(values.begin(),values.end(),material);
        if (found != values.end()) {
            material += 4*static_cast<int>(variant->phase) + (found-values.begin()<2 ? 12 : 2);
            if (static_cast<std::size_t>(material) >= mesh.materials.size())
                return false;
        }
    }
    if (lights) {
        const TitleLighting rig(*lights);
        return DrawMesh(mesh,pose,root,camera,data_->textures,&rig,vram,offset);
    }
    return DrawMesh(mesh,pose,root,camera,data_->textures,nullptr,vram,offset);
} catch(const std::exception&) {return false;}
bool RoomMesh::SignWorld(unsigned marker, const std::vector<std::pair<unsigned,uint32_t>>& clips,
                         GameMatrix& out) const {
    if (!NodeWorld(marker, clips, out)) return false;
    const auto& node = data_->mesh.model.nodes[marker];
    if (node.type != 13 || node.commonFlags != 3) return false;
    // 8C684 uses R * O * scale * inverse(O). The Windows model uses canonical axes.
    const auto& q = node.commonVectors[2];
    const auto orientation = ModelQuaternion({q[0], q[1], q[2], q[3]});
    auto scale = ModelIdentity();
    for (unsigned axis = 0; axis != 3; ++axis) scale[axis*5] = node.commonVectors[3][axis];
    out = ModelMultiply(out, ModelMultiply(orientation, ModelMultiply(scale, ModelInverseRigid(orientation))));
    return true;
}
bool RoomMesh::DrawSign(const GameMatrix& root, const GameMatrix& camera,
                        const std::vector<PokepiTexture>& glyphs, uint32_t rgb,
                        std::vector<uint8_t>& vram, size_t offset) const try {
    if (!data_ || glyphs.size() != data_->mesh.model.nodes.size()) return false;
    return DrawMesh(data_->mesh, data_->mesh.bind, root, camera, data_->textures,
                    nullptr, vram, offset, &glyphs, rgb & 0xffffffu);
} catch (const std::exception&) { return false; }
} // namespace dokodemo::gameplay
