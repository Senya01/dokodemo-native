#include "recovered/game_model_native.h"
#include "recovered/game_title_native.h"
#include "recovered/game_boot_native.h"
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace dokodemo::recovered;
namespace {
void Check(bool value, const char *text) {
    if (!value)
        throw std::runtime_error(text);
}
void W(std::vector<uint8_t> &b, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i)
        b.push_back(uint8_t(v >> (i * 8)));
}
void H(std::vector<uint8_t> &b, int32_t v) {
    b.push_back(uint8_t(v));
    b.push_back(uint8_t(v >> 8));
}
void F(std::vector<uint8_t> &b, float f) {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    W(b, v);
}
void Word(std::vector<uint8_t> &b, size_t at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        b.at(at + i) = uint8_t(value >> (i * 8));
}
void Bounds(std::vector<uint8_t> &b) {
    F(b, 1);
    for (unsigned i = 0; i < 24; ++i)
        H(b, 0);
    for (unsigned i = 0; i < 4; ++i)
        F(b, 0);
}
std::vector<uint8_t> Fixture() {
    std::vector<uint8_t> b{'D', 'V', 'S', 0};
    b.resize(16);
    W(b, 6);
    W(b, 0);
    Bounds(b);
    H(b, 1);
    H(b, 0);
    for (unsigned i = 0; i < 4; ++i)
        H(b, 0);
    const auto node = b.size();
    W(b, 0);
    H(b, 1);
    for (auto v : {0, -1, -1, 255, 0})
        H(b, v);
    for (const auto &row :
         std::array<GameVector4, 4>{{{{0, 0, 0, 1}}, {{0, 0, 0, 1}}, {{0, 0, 0, 1}}, {{1, 1, 1, 1}}}})
        for (auto v : row)
            F(b, v);
    b.push_back(3);
    b.push_back(0);
    H(b, 9);
    b.push_back(255);
    b.push_back(0);
    H(b, 3);
    F(b, 1);
    for (auto v : {-25000, -25000, 0, 25000, -25000, 0, 0, 25000, 0})
        H(b, v);
    while (b.size() % 4)
        b.push_back(0);
    b.insert(b.end(), {255, 255, 255, 128});
    Bounds(b);
    W(b, 0);
    H(b, 0);
    H(b, 0);
    W(b, 3);
    H(b, 1);
    H(b, 0x0203);
    H(b, 0);
    H(b, 1);
    H(b, 2);
    H(b, 0);
    Word(b, node, uint32_t(b.size() / 4));
    return b;
}
struct Lease : PackageStorageLease {
    std::vector<uint8_t> b, known;
    bool live = true;
    explicit Lease(std::vector<uint8_t> data) : b(std::move(data)), known(b.size(), 1) {}
    bool Span(PackageStorageSpan &s, std::string &error) const override {
        if (!live) {
            error = "fixture retired";
            return false;
        }
        s.bytes = const_cast<uint8_t *>(b.data());
        s.initialized = const_cast<uint8_t *>(known.data());
        s.size = b.size();
        s.owner = s.host_address = uint64_t(reinterpret_cast<uintptr_t>(b.data()));
        s.generation = s.serial = 1;
        s.readable = s.writable = true;
        return true;
    }
};
bool Near(float a, float b) {
    return std::abs(a - b) < .0001f;
}
void MathAndSkin() {
    const auto camera_rotation = ModelQuaternion({{-.70710678f, 0, 0, .70710678f}});
    const auto forward = ModelTransform(camera_rotation, {{0, 0, -1, 0}});
    Check(Near(forward[0], 0) && Near(forward[1], 1) && Near(forward[2], 0),
          "source conjugated quaternion maps camera forward+Y");
    auto a = ModelIdentity();
    a[12] = 3;
    a[13] = -7;
    a[14] = 11;
    const auto p = ModelTransform(ModelMultiply(a, ModelInverseRigid(a)), {{1, 2, 3, 1}});
    Check(Near(p[0], 1) && Near(p[1], 2) && Near(p[2], 3), "rigid bind inverse cancels translation");
    dokodemo::model::Geometry g;
    g.positions.push_back({1, 2, 3});
    g.weightRecords.assign(24, 0);
    Word(g.weightRecords, 0, 0xFFFF0001);
    Word(g.weightRecords, 4, 0xFFFFFFFF);
    Word(g.weightRecords, 8, 0x7FC00000);
    std::vector<GameMatrix> palette{ModelIdentity(), a};
    auto rigid = GameModelClone::Skin(g, 0, palette);
    Check(Near(rigid[0], 4) && Near(rigid[1], -5) && Near(rigid[2], 14),
          "index1negative ignores stored weight0 as actual CPU branch");
    Word(g.weightRecords, 0, 0x00010000);
    Word(g.weightRecords, 8, 0x3E800000);
    Word(g.weightRecords, 12, 0x3F400000);
    auto weighted = GameModelClone::Skin(g, 0, palette);
    Check(Near(weighted[0], 3.25f) && Near(weighted[1], -3.25f) && Near(weighted[2], 11.25f),
          "two palette weighted positions");
    Word(g.weightRecords, 0, 0xFFFF0002);
    bool refused = false;
    try {
        (void)GameModelClone::Skin(g, 0, palette);
    } catch (const std::exception &) {
        refused = true;
    }
    Check(refused, "out of palette does not read native memory");
}
void GeometryAndRaster() {
    auto lease = std::make_shared<Lease>(Fixture());
    auto asset = std::make_shared<GameModelAsset>();
    Check(bool(asset->Decode(PackageStorage::Attach(lease, lease->b.size()))),
          "checked DVS geometry consumer");
    Check(asset->Statistics().vertices == 3 && asset->Statistics().triangles == 1, "typed geometry arrays");
    GameModelClone a, b;
    Check(bool(a.Initialize(asset)) && bool(b.Initialize(asset)),
          "shared immutable asset,two actual mutable clones");
    a.Pose()[0].position[0] = .2f;
    Check(bool(a.Update(ModelIdentity())) && b.Pose()[0].position[0] == 0,
          "pose mutation never aliases another clone");
    GameGraphics graphics;
    GameGraphicsConfig config;
    config.width = config.height = config.stride = 32;
    config.scissor_right = config.scissor_bottom = 32;
    config.cull_face = false;
    Check(graphics.Initialize(config) && graphics.Clear(0xFF000000), "real raster surface");
    Check(bool(a.Draw(graphics, ModelIdentity())) && graphics.SwapBuffers(), "model Draw reaches renderer");
    Check(graphics.Snapshot().mesh_pixels > 0 && graphics.Snapshot().mesh_triangles == 1,
          "actual model triangle writes pixels");
    const uint32_t colors[]{0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF};
    GameTextureSpan texture{colors, 2, 2, 4};
    GameRasterPolicy policy;
    policy.two_sided = true;
    std::array<GameMeshVertex, 3> triangle{{{{{-.9f, -.9f, 0, 1}}, 0, 1, 0xFFFFFFFF},
                                            {{{.9f, -.9f, 0, 1}}, 1, 1, 0xFFFFFFFF},
                                            {{{0, .9f, -2, 1}}, .5f, 0, 0xFFFFFFFF}}};
    const auto before = graphics.Snapshot();
    Check(graphics.DrawMeshTriangle(triangle, texture, policy),
          "near-plane intersection clips rather than discards full triangle");
    Check(graphics.Snapshot().mesh_pixels > before.mesh_pixels && graphics.Snapshot().textured_triangles > 0,
          "clipped textured triangle writes actual samples");
    auto bad = triangle;
    bad[0].clip[0] = std::numeric_limits<float>::quiet_NaN();
    Check(!graphics.DrawMeshTriangle(bad, texture, policy), "nonfinite clip coordinate rejected");
    texture.count = 3;
    Check(!graphics.DrawMeshTriangle(triangle, texture, policy), "texture extent mismatch refused");
    lease->live = false;
    Check(!a.Draw(graphics, ModelIdentity()) && !b.Update(ModelIdentity()),
          "retired source lease cannot draw or animate");
    a.Close();
    b.Close();
    graphics.Close();
    Check(!graphics.Snapshot().live, "all clone/surface owners retire");
    auto malformed = Fixture();
    malformed[0] = 0;
    auto badlease = std::make_shared<Lease>(std::move(malformed));
    GameModelAsset badasset;
    Check(!badasset.Decode(PackageStorage::Attach(badlease, badlease->b.size())) && !badasset.Live(),
          "malformed source never publishes a live model");
}
std::vector<uint8_t> MotionFixture(bool unknown = false) {
    std::vector<uint8_t> b{'D', 'A', 'K', 0};
    W(b, 0);
    W(b, 1);
    W(b, 0);
    W(b, 160);
    const auto object = b.size();
    W(b, 0);
    H(b, 0);
    H(b, 3);
    for (const auto &row :
         std::array<GameVector4, 4>{{{{0, 0, 0, 1}}, {{0, 0, 0, 1}}, {{0, 0, 0, 1}}, {{1, 1, 1, 1}}}})
        for (auto v : row)
            F(b, v);
    b.push_back(unknown ? 0x61 : 1);
    b.push_back(1);
    H(b, 2);
    for (auto v : {10.0f, 20.0f, 30.0f, 2.0f})
        F(b, v);
    W(b, 0);
    W(b, 0);
    W(b, 0);
    W(b, 160);
    W(b, 0xFFFFFFFF);
    W(b, 65535);
    b.push_back(2);
    b.push_back(1);
    H(b, 2);
    W(b, 0);
    W(b, 0);
    W(b, 0x7FFF0000);
    W(b, 160);
    W(b, 0);
    W(b, 0x5A825A82);
    b.push_back(0x30);
    b.push_back(0);
    H(b, 1);
    W(b, 0);
    W(b, 255);
    Word(b, object, uint32_t(b.size() / 4));
    return b;
}
void Motion() {
    auto lease = std::make_shared<Lease>(Fixture());
    auto asset = std::make_shared<GameModelAsset>();
    Check(bool(asset->Decode(PackageStorage::Attach(lease, lease->b.size()))), "motion target asset");
    GameModelClone clone;
    Check(bool(clone.Initialize(asset)), "motion clone");
    auto keys = std::make_shared<Lease>(MotionFixture());
    GameModelMotion motion;
    Check(bool(motion.Decode(PackageStorage::Attach(keys, keys->b.size()), *asset)), "finite DAK consumer");
    Check(bool(motion.Apply(clone, 0)) && Near(clone.Pose()[0].position[0], 10), "actual first key at time0");
    Check(bool(motion.Apply(clone, 80)) && Near(clone.Pose()[0].position[0], 11) &&
              Near(clone.Pose()[0].position[2], 31),
          "unsigned packed translation,prefix and integer time");
    Check(clone.Pose()[0].material == -1, "stepped255 material null");
    Check(Near(clone.Pose()[0].rotation[2], .38268f), "native shortest quaternion slerp");
    Check(bool(motion.Apply(clone, 160)) && Near(clone.Pose()[0].position[1], 22),
          "last key not a float time");
    keys->live = false;
    Check(!motion.Apply(clone, 0), "retired motion lease refused");
    auto invalid = std::make_shared<Lease>(MotionFixture(true));
    GameModelMotion bad;
    Check(!bad.Decode(PackageStorage::Attach(invalid, invalid->b.size()), *asset),
          "unknown section not a success callback");
}
struct TitleSource final : GameResourceSource {
    bool fail = false;
    GameFsStatus OpenResource(const std::string &path, PackageView &out) override {
        if (fail)
            return {GameFsCode::NotFound, 0, "synthetic selected source failure"};
        auto lease =
            std::make_shared<Lease>(path.find(".DVS") != std::string::npos ? Fixture() : MotionFixture());
        out = PackageStorage::Attach(lease, lease->b.size());
        return {};
    }
};
void TitleLifetimeAndFade() {
    uint32_t segment = 0;
    Check(TitleMovementStep(2, 4, segment) == 0 && segment == 1 && TitleMovementStep(2, 5, segment) == 52,
          "Jun tuple step precedes next-segment publication");
    Check(TitleMovementStep(1, 99, segment) == 58 && TitleMovementStep(3, 99, segment) == 33,
          "signed integer distance*10/duration source steps");
    std::vector<uint8_t> memory(4 * 1024 * 1024);
    GameArena arena;
    Check(arena.Initialize(memory.data(), uint32_t(memory.size())), "TITLE synthetic arena");
    GameVmBoot manager;
    Check(manager.Initialize74EF4(arena) && manager.InitializeManager93404(), "TITLE manager");
    const auto baseline = arena.Used();
    TitleSource source;
    GameTitleScene scene;
    Check(bool(scene.Initialize(source, arena, manager, NativeTitleLayout::CharacterEntrance, 0)),
          "TITLE actual typed owner constructors over explicit synthetic assets");
    Check(scene.Snapshot().clones == 10 && scene.Snapshot().motions == 21 &&
              scene.Snapshot().visible_actors == 0,
          "all cloned owners initially hidden");
    GameGraphics graphics;
    Check(graphics.Initialize({}), "TITLE graphics");
    GameTransition fade;
    Check(fade.Initialize({{0, 0}}), "actual fade0 producer");
    Check(bool(scene.TickAndDraw(graphics, fade)) && scene.Snapshot().state == 0 &&
              scene.Snapshot().visible_actors == 0,
          "CC2 starts actual fade1,not forced visibility");
    for (unsigned i = 0; i < 40; ++i)
        Check(bool(scene.TickAndDraw(graphics, fade)), "finite title phase6/11");
    const auto state = scene.Snapshot();
    Check(state.state == 1 && fade.Snapshot().state == 2 && state.visible_actors == 5,
          "visibility only after natural fade2/CC1");
    Check(state.actors[0].x < 2500 && state.actors[5].x == 2500 && !state.actors[5].moving &&
              !state.actors[5].visible,
          "primary movement,cold kuro remains hidden without target");
    Check(!state.scene3_completed && !state.menu_input_implemented, "no fake scene completion/menu success");
    GameFsStatus boundary;
    for(unsigned i=0;i<100&&boundary;++i)boundary=scene.TickAndDraw(graphics,fade);
    Check(!boundary&&scene.Snapshot().arrival_branch_pending&&scene.Snapshot().frontier_pc==0x33B90,"unimplemented arrival random/input branch stops explicitly");
    Check(scene.Close() && arena.Used() == baseline, "TITLE releases text/task/clone ownership");
    GameTitleScene failed;
    source.fail = true;
    Check(!failed.Initialize(source, arena, manager, NativeTitleLayout::CharacterEntrance, 0) &&
              failed.Close() && arena.Used() == baseline,
          "partial constructor failure has reachable cleanup");
    GameTitleScene profile;
    Check(!profile.Initialize(source, arena, manager, NativeTitleLayout::CharacterEntrance, 1) &&
              profile.Close(),
          "nonzero342D8 profile refused");
    graphics.Close();
    Check(manager.Close() && arena.Used() == 0, "all native owners closed");
    arena.Reset();
}
} // namespace
int main() {
    try {
        MathAndSkin();
        GeometryAndRaster();
        Motion();
        TitleLifetimeAndFade();
        std::cout
            << "model clone/geometry,skin/source quaternion,clip/texture pixels,errors/retirement PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
