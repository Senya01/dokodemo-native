#include "recovered/game_boot_native.h"
#include "recovered/game_model_native.h"
#include "recovered/game_title_native.h"
#include "recovered/game_actor_runtime_native.h"
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
bool Near(float a, float b) { return std::abs(a - b) < .0001f; }
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
void ParticleDescriptor(){
    auto bytes=Fixture();Word(bytes,92,2);const auto node=bytes.size();W(bytes,0);H(bytes,12);
    for(auto v:{0,0,-1,255,0})H(bytes,v);
    for(const auto& row:std::array<GameVector4,4>{{{{3,4,5,1}},{{0,0,0,1}},{{0,0,0,1}},{{1,1,1,1}}}})
        for(auto v:row)F(bytes,v);
    H(bytes,7);H(bytes,8);F(bytes,1.25f);F(bytes,2.5f);Word(bytes,node,uint32_t(bytes.size()/4));
    auto lease=std::make_shared<Lease>(std::move(bytes));auto asset=std::make_shared<GameModelAsset>();
    Check(bool(asset->Decode(PackageStorage::Attach(lease,lease->b.size())))&&asset->Statistics().particle_emitters==1,
          "type12 particle descriptor admission,not a guessed collider");
    GameModelClone a,b;Check(bool(a.Initialize(asset))&&bool(b.Initialize(asset)),"emitter fields copied to separate native clones");
    Check(a.Pose()[1].emitter.first==7&&a.Pose()[1].emitter.second==8&&Near(a.Pose()[1].emitter.scalar0,1.25f),"exact12B emitter payload");
    a.Pose()[1].emitter.first=99;Check(b.Pose()[1].emitter.first==7,"emitter descriptor clone ownership");
    a.Pose()[0].position[0]=10;Check(bool(a.Update(ModelIdentity()))&&Near(a.Pose()[1].world[12],13),"emitter common parent transform propagated");
    Check(asset->Statistics().triangles==1,"particle descriptor creates no fabricated triangle/emission");
    lease->live=false;Check(!a.Update(ModelIdentity()),"retired emitter source refuses transform update");
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
    H(b, unknown ? 1 : 3);
    for (const auto &row :
         std::array<GameVector4, 4>{{{{0, 0, 0, 1}}, {{0, 0, 0, 1}}, {{0, 0, 0, 1}}, {{1, 1, 1, 1}}}})
        for (auto v : row)
            F(b, v);
    if(unknown){
        b.push_back(0x50);b.push_back(0);H(b,1);
        for(uint32_t v:{0u,1u,2u,3u})W(b,v);
        Word(b,object,uint32_t(b.size()/4));return b;
    }
    b.push_back(1);
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
    GameModelPlayback channel;
    channel.Configure(motion.Statistics().duration,false);channel.Start();
    Check(!channel.Complete()&&bool(channel.Apply(motion,clone,160))&&channel.Snapshot().sampled_time==0
          &&channel.Snapshot().time==160&&!channel.Complete(),"admission/first actual pose is not channel completion");
    Check(bool(channel.Apply(motion,clone,160))&&channel.Complete()&&channel.Snapshot().sampled_time==160
          &&Near(clone.Pose()[0].position[1],22),"nonloop completes only after applying actual end pose");
    clone.Pose()[0].position[1]=-123;
    Check(bool(channel.Apply(motion,clone,160))&&channel.Complete()&&Near(clone.Pose()[0].position[1],22)
          &&channel.Snapshot().time==160,"finished2 reapplies final pose without advancing");
    channel.Start();Check(!channel.Complete()&&channel.Snapshot().time==0&&channel.Snapshot().loops==0,
          "89954 restart clears elapsed time and loop count");
    auto looping=MotionFixture();Word(looping,12,1);
    auto looping_keys=std::make_shared<Lease>(std::move(looping));GameModelMotion loop;
    Check(bool(loop.Decode(PackageStorage::Attach(looping_keys,looping_keys->b.size()),*asset)),"source file loop flag admitted");
    GameModelPlayback loop_channel;loop_channel.Configure(160,true);loop_channel.Start();
    for(unsigned i=0;i<7;++i)Check(bool(loop_channel.Apply(loop,clone,160))&&!loop_channel.Complete(),"loop duration never signals command ready");
    Check(loop_channel.Snapshot().loops==6&&loop_channel.Snapshot().state==3,"source loop reset and continue increment");
    loop_channel.Stop();const auto stopped=loop_channel.Snapshot().samples;
    Check(bool(loop_channel.Apply(loop,clone,160))&&loop_channel.Snapshot().samples==stopped&&!loop_channel.Complete(),
          "scene stop state1 is inactive,not finished2");
    std::vector<uint8_t> opacity{'D','A','K',0};W(opacity,2004072701);W(opacity,1);W(opacity,0);W(opacity,160);
    const auto opacity_object=opacity.size();W(opacity,0);H(opacity,0);H(opacity,1);
    for(const auto& row:std::array<GameVector4,4>{{{{0,0,0,1}},{{0,0,0,1}},{{0,0,0,1}},{{1,1,1,1}}}})
        for(auto value:row)F(opacity,value);
    opacity.push_back(0x50);opacity.push_back(15);H(opacity,2);
    W(opacity,0);F(opacity,0);F(opacity,0);W(opacity,0x7FC00000);
    W(opacity,160);F(opacity,1);W(opacity,0x7FC00000);F(opacity,0);
    Word(opacity,opacity_object,uint32_t(opacity.size()/4));
    auto alpha_keys=std::make_shared<Lease>(opacity);GameModelMotion alpha;
    Check(bool(alpha.Decode(PackageStorage::Attach(alpha_keys,alpha_keys->b.size()),*asset))
          &&bool(alpha.Apply(clone,80))&&clone.Pose()[0].opacity==0,
          "exact88E0C outgoing quiet-NaN tangent holds left opacity,not a ramp");
    Check(bool(alpha.Apply(clone,160))&&clone.Pose()[0].opacity==1,"opacity reaches next actual key");
    Word(opacity,opacity_object+88,0); // first OUT tangent finite;next IN remains NaN
    auto incoming_keys=std::make_shared<Lease>(opacity);GameModelMotion incoming;
    Check(bool(incoming.Decode(PackageStorage::Attach(incoming_keys,incoming_keys->b.size()),*asset))
          &&!incoming.Apply(clone,80),"unclosed incoming-only NaN does not become a fabricated scalar");
    keys->live = false;
    Check(!motion.Apply(clone, 0), "retired motion lease refused");
    channel.Start();Check(!channel.Apply(motion,clone,160)&&!channel.Complete(),"expired motion cannot manufacture finished state");
    auto invalid = std::make_shared<Lease>(MotionFixture(true));
    GameModelMotion bad;
    Check(bool(bad.Decode(PackageStorage::Attach(invalid, invalid->b.size()), *asset))&&bad.Statistics().deferred_sections==1,
          "inactive unknown track retained,not executed at template admission");
    const auto prior=clone.Pose()[0].position;
    Check(!bad.Apply(clone,0)&&clone.Pose()[0].position==prior,"active unknown track refused before partial pose mutation");
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
struct ActorSource final:GameResourceSource {
    bool fail_motion=false;
    bool scene_assets=false,season_missing=false;
    std::vector<std::string> requests;
    GameFsStatus OpenResource(const std::string& path,PackageView& out) override {
        requests.push_back(path);std::vector<uint8_t> bytes;
        if(season_missing&&path.find("BG_ROOM\\BG_ROOM\\light")==0)return {GameFsCode::NotFound,0,"Explicit missing optional season fixture"};
        if(path.find(".ASC")!=std::string::npos){
            W(bytes,0x00435341);W(bytes,16);W(bytes,124);W(bytes,0);
            for(unsigned i=0;i<=176;++i){
                std::vector<uint8_t> payload;
                if(i==0){for(unsigned n=0;n<124;++n){const auto name="m"+std::to_string(n);payload.insert(payload.end(),name.begin(),name.end());payload.push_back(0);}}
                else if(i==153){for(uint32_t word:{0x8030u,5u,0u,0x8030u,11u,15u,0x8030u,2u,90u,
                    0x8040u,1u,0x8010u,119u,8u,0x8030u,8u,1u,0x8010u,120u,8u,0x8040u,0u,0x8031u,0x8000u})W(payload,word);}
                else if(i==157){for(uint32_t word:{0x8030u,5u,0u,0x8030u,0u,1u,0x8030u,4u,16u,0x8031u,0x8000u})W(payload,word);}
                else if(i==176){for(uint32_t word:{0x8040u,1u,0x8030u,3u,9u,0x8030u,5u,0u,0x8030u,0u,1u,
                    0x8030u,2u,90u,0x8030u,10u,3u,0x8010u,123u,4u,0x8031u,0x8000u})W(payload,word);}
                else {W(payload,0x8010);W(payload,8);W(payload,8);}
                W(bytes,0);W(bytes,16);W(bytes,uint32_t(payload.size()));W(bytes,0);bytes.insert(bytes.end(),payload.begin(),payload.end());
            }
        }else if(path.find(".DVS")!=std::string::npos)bytes=Fixture();
        else if(path=="DATA\\SEQ\\sequence_coord.bin"){bytes.resize(136);Word(bytes,72,1830);Word(bytes,76,uint32_t(-27));Word(bytes,120,1820);Word(bytes,124,uint32_t(-627));Word(bytes,128,600);Word(bytes,132,uint32_t(-300));}
        else if(scene_assets&&path.find("BG_ROOM\\BG_ROOM\\") == 0){bytes=MotionFixture();}
        else if(path.find("m8.DAK")!=std::string::npos){
            if(fail_motion)return {GameFsCode::MissingFile,0,"synthetic actual motion8 failure"};
            bytes=MotionFixture();Word(bytes,12,1);
        }else if(path.find("m123.DAK")!=std::string::npos||path.find("m119.DAK")!=std::string::npos||path.find("m120.DAK")!=std::string::npos){bytes=MotionFixture();
        }else return {GameFsCode::NotFound,0,"Explicit missing synthetic scene owner"};
        auto lease=std::make_shared<Lease>(std::move(bytes));out=PackageStorage::Attach(lease,lease->b.size());return {};
    }
};
void ActorChannelOrder(){
    auto lease=std::make_shared<Lease>(std::vector<uint8_t>(16));GameTransition fade;Check(fade.Initialize({{0,0}}),"actor synthetic fade");
    ActorSource source;GameActorRuntime actor;
    Check(bool(actor.Initialize(0,PackageStorage::Attach(lease,16),fade,&source))&&bool(actor.RequestActorEvent(176,0)),"live ASC actor request");
    Check(!actor.WaitReady(),"request/installation cannot signal VM54");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().command_effects==1&&!actor.WaitReady(),"first8040 only,not completion");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().pending_logical_motion==9&&actor.Snapshot().channel_admissions==0,
          "coordinate effect precedes actual logical9 handoff");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().first_channel_motion==8&&actor.Snapshot().channel_admissions==2
          &&actor.Snapshot().command_pc==32&&!actor.WaitReady(),"both real channels8 admitted before next ASC parameter");
    for(unsigned i=0;i<35;++i)Check(bool(actor.AdvanceActorFrame()),"actor phase with looping motion and source angular gate");
    auto state=actor.Snapshot();
    Check(state.command_blocked&&state.command_frontier==0x6D6C&&state.command_pc==56&&state.command_effects==5,
          "later motion cannot jump absent actual scene pair consumer");
    Check(state.channels[0].loop&&state.channels[0].loops>0&&!state.actor_control_ready&&!actor.WaitReady()
          &&state.angular_completions==1&&state.motion_samples>0,"loops/pose/rotation alone never command completion");
    Check(state.effect_order[0]==0x8040&&state.effect_order[1]==0x8030&&state.effect_order[4]==0x8030,
          "checked ASC operand effect order retained");
    actor.Close();Check(!actor.WaitReady()&&actor.Snapshot().channel_motion[0]==-1,"channels retired before resources");
    GameActorRuntime scene;source.scene_assets=true;
    Check(bool(scene.Initialize(0,PackageStorage::Attach(lease,16),fade,&source))&&bool(scene.InitializeScene0({{0,0,15,12,21,10,2026}})),"actual14-control scene typed owner");
    int32_t period=-1;Check(bool(scene.QueryScenePeriod(15,49,period))&&period==2
          &&scene.Snapshot().scene_period_table52==2&&scene.Snapshot().season_controls==7,
          "actual optional presence admits month/day row2,not constructor row0");
    Check(bool(scene.QueryScenePeriod(15,50,period))&&period==3,"scene-selected row2 endpoint used by GAME36");
    Check(scene.Snapshot().scene_controls==14&&scene.Snapshot().scene_pair_calls==2
          &&scene.Snapshot().scene_playback[10].state==3&&scene.Snapshot().scene_playback[13].state==3
          &&scene.Snapshot().scene_playback[8].state==1&&scene.Snapshot().scene_playback[11].state==1,
          "source startup pairs10/13 stop8/11 before start");
    Check(bool(scene.RequestActorEvent(176,0)),"paired actual command owner");
    for(unsigned i=0;i<55;++i)Check(bool(scene.AdvanceActorFrame()),"scene pair and nonloop command progression");
    const auto completed=scene.Snapshot();
    Check(completed.scene_pair3_calls==1&&completed.scene_last_pair==std::array<int32_t,2>{{3,2}}
          &&completed.scene_playback[2].state==1&&completed.scene_playback[3].state==2,
          "real pair3 stops2 and starts3,retained final pose");
    Check(completed.asc176_ready&&completed.asc176_complete&&completed.command_completions==1&&scene.WaitReady()
          &&completed.command_effects==9,"ready only after actual nonloop motion,end marker and root unwind");
    Check(bool(scene.RequestActorEvent(153,0))&&!scene.WaitReady(),"new switch request cannot inherit previous command readiness");
    bool moved_without_ready=false;
    for(unsigned i=0;i<70&&scene.Snapshot().command_completions<2;++i){
        Check(bool(scene.AdvanceActorFrame()),"checked switch actor phase");
        const auto moving=scene.Snapshot();if(moving.movement_pending&&moving.movement_updates){
            moved_without_ready=true;Check(!scene.WaitReady()&&moving.actor_root[1]!=-627,"intermediate live movement is not command completion");}
    }
    Check(moved_without_ready&&scene.Snapshot().last_completed_command==153
          &&scene.Snapshot().source_flag2A980==1&&scene.Snapshot().light_toggle_calls==1
          &&scene.Snapshot().light_on_playback.samples>0&&scene.Snapshot().movement_completions==1,
          "real motion end and parameter8 control/flag effects precede switch completion");
    Check(bool(scene.RequestActorEvent(157,0))&&!scene.WaitReady(),"mode3 target request remains pending");
    for(unsigned i=0;i<40&&scene.Snapshot().command_completions<3;++i)Check(bool(scene.AdvanceActorFrame()),"checked mode3 phase");
    Check(scene.Snapshot().last_completed_command==157&&scene.Snapshot().movement_completions==2
          &&scene.Snapshot().logical_xy==std::array<float,2>{{600,-300}}&&scene.WaitReady(),
          "mode3 completes only at the produced live target");
    int32_t slot=-1;Check(bool(scene.CreateModel(170,0,0,0,slot))&&bool(scene.BindCamera(1)),"synthetic scene render owners");
    GameGraphics graphics;Check(graphics.Initialize({})&&bool(scene.Draw(graphics)),"native following-camera frame");
    Check(scene.Snapshot().camera_position==GameVector4{{600,-4700,980,1}}
          &&scene.Snapshot().actor_position==GameVector4{{0,0,600,1}},"explicit native root follow never overwrites unproved source actor40");
    graphics.Close();
    scene.Close();Check(!scene.Snapshot().scene_model_live&&!scene.Snapshot().scene_controls&&!scene.WaitReady(),"scene controls retired with actor");
    period=-7;Check(!scene.QueryScenePeriod(12,0,period)&&period==-7,"retired scene clock cannot return successful period");
    GameActorRuntime no_season;source.season_missing=true;
    Check(bool(no_season.Initialize(0,PackageStorage::Attach(lease,16),fade,&source))
          &&bool(no_season.InitializeScene0({{0,0,0,12,21,10,2026}})),"explicit NotFound optional controls retain real constructor row0");
    period=-1;Check(no_season.Snapshot().season_controls==0&&no_season.Snapshot().scene_period_table52==0
          &&bool(no_season.QueryScenePeriod(15,40,period))&&period==3,"absent template cannot fabricate seasonal row2");
    no_season.Close();source.season_missing=false;
    source.fail_motion=true;GameActorRuntime failed;Check(bool(failed.Initialize(0,PackageStorage::Attach(lease,16),fade,&source))
          &&bool(failed.RequestActorEvent(176,0)),"motion failure request owner");
    for(unsigned i=0;i<3;++i)Check(bool(failed.AdvanceActorFrame()),"checked motion error phase");
    Check(failed.Snapshot().command_blocked&&failed.Snapshot().channel_admissions==0&&!failed.WaitReady(),
          "missing DAK has no published control or ready signal");
    failed.Close();fade.Retire();
}
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
    for (unsigned i = 0; i < 300; ++i)
        Check(bool(scene.TickAndDraw(graphics, fade)), "running through real arrivals");
    Check(scene.Snapshot().primary_arrivals >= 2 && scene.Snapshot().primary_restarts >= 2 &&
              !scene.Snapshot().arrival_branch_pending && scene.Snapshot().arrival_countdown == 3,
          "phase11 source arrival restarts cycle;only index0 decrements countdown");
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
void ArrivalBranchTests() {
    GameTitleSnapshot state;
    state.state = 1;
    state.layout = 0;
    for (unsigned i = 0; i < 10; ++i) {
        auto &actor = state.actors[i];
        actor.type = i < 5 ? i : 5;
        actor.x = 2500;
        actor.y = 2000 + 500.0f * i;
        actor.target_x = -4000;
        actor.target_y = actor.y;
        actor.target_produced = i < 5;
        actor.moving = i < 5;
        actor.visible = actor.desired_visible = i < 5;
    }
    state.actors[1].x = -4000;
    state.actors[1].moving = false;
    TitleArrivalCycle(state, nullptr);
    Check(state.arrival_countdown == 4 && state.primary_restarts == 1 && state.rng_calls == 0 &&
              state.actors[1].x == 5700 && state.actors[1].target_y == 4000,
          "Suzuki arrival restarts source offset position without random/motion "
          "restart");
    state.actors[0].x = -4000;
    state.actors[0].moving = false;
    TitleArrivalCycle(state, nullptr);
    Check(state.arrival_countdown == 3 && state.actors[0].x == 2500, "only Toro decrements140");
    state.actors[0].x = -4000;
    state.selection = 0;
    TitleArrivalCycle(state, nullptr);
    Check(!state.actors[0].desired_visible && state.primary_restarts == 2,
          "selection suppresses attract restart");
    state.selection = -1;
    state.media_state = 1;
    TitleArrivalCycle(state, nullptr);
    Check(state.primary_restarts == 2, "media transition suppresses restart");
    state.media_state = 0;
    state.arrival_countdown = 0;
    RngNativeOwner rng;
    uint64_t seed = 0;
    for (;; ++seed) {
        RngNativeOwner candidate;
        candidate.ImportCurrentState64(seed, 1, "explicit deterministic fixture");
        if (candidate.NextC3234() % 64 == 0)
            break;
    }
    rng.ImportCurrentState64(seed, 1, "explicit deterministic fixture");
    TitleArrivalCycle(state, &rng);
    Check(state.kuro_selections == 1 && state.rng_calls == 1 && state.actors[5].moving &&
              state.actors[5].visible && !state.actors[0].visible,
          "zero countdown seeded64-way branch creates real kuro target");
    state.actors[5].x = state.actors[5].target_x;
    state.actors[5].y = state.actors[5].target_y;
    TitleArrivalCycle(state, &rng);
    Check(!state.actors[5].visible && state.rng_calls == 1,
          "paired visible kuro arrival clears only itself that invocation");
}
void TitleMenuLifecycle() {
    // Synthetic checked assets isolate source state/input/fade/ownership. The
    // connected window run separately validates actual CB2A/GIM/model bodies.
    struct MenuSource : GameResourceSource {
        TitleSource models;
        GameFsStatus OpenResource(const std::string &path, PackageView &out) override {
            if (path != "fixture.CB2A")
                return models.OpenResource(path, out);
            std::vector<uint8_t> b(32 + 404 * 8 + 30 * 52, 0);
            Word(b, 0, 0x30413242);
            Word(b, 8, 32);
            Word(b, 12, 404);
            Word(b, 16, 32);
            Word(b, 20, 30);
            Word(b, 24, 32 + 404 * 8);
            Word(b, 28, 30);
            auto lease = std::make_shared<Lease>(std::move(b));
            out = PackageStorage::Attach(lease, lease->b.size());
            return {};
        }
    } source;
    std::vector<uint8_t> memory(4 * 1024 * 1024);
    GameArena arena;
    Check(arena.Initialize(memory.data(), uint32_t(memory.size())), "menu arena");
    GameVmBoot manager;
    Check(manager.Initialize74EF4(arena) && manager.InitializeManager93404(), "menu manager");
    const auto baseline = arena.Used();
    auto ui = std::make_shared<GameUiResource>();
    Check(bool(ui->Load(source, "fixture.CB2A", "")), "synthetic UI table");
    GameUiInstance clone;
    Check(clone.Clone(ui), "independent menu clone");
    std::vector<uint8_t> f(96, 0);
    Word(f, 0, 0x31544658);
    Word(f, 4, 0x00100010);
    Word(f, 24, 1);
    Word(f, 32, 4);
    Word(f, 36, 4);
    Word(f, 40, 16);
    Word(f, 44, 64);
    Word(f, 52, 8);
    Word(f, 56, 80);
    auto lease = std::make_shared<Lease>(std::move(f));
    GameFontResource font;
    Check(bool(font.Initialize(PackageStorage::Attach(lease, lease->b.size()))), "live font prerequisite");
    RngNativeOwner rng;
    rng.ApplyInitializerTimeT32(1234, 1, "explicit menu test seed");
    GameTitleScene scene;
    Check(bool(scene.Initialize(source, arena, manager, NativeTitleLayout::CharacterEntrance, 0)) &&
              bool(scene.AttachMenu(clone, font, rng)),
          "title owns tasks and borrows actual-lifetime UI/font/RNG");
    GameGraphics graphics;
    Check(graphics.Initialize({}), "menu graphics");
    GameTransition fade;
    Check(fade.Initialize({{0, 0}}), "menu fade");
    const auto tick = [&](unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            Check(bool(scene.TickAndDraw(graphics, fade)), "menu phase state");
    };
    const auto await_root=[&](uint32_t root){
        for(unsigned i=0;i<160&&(scene.Snapshot().layout!=root||scene.Snapshot().state!=1);++i)tick(1);
        Check(scene.Snapshot().layout==root&&scene.Snapshot().state==1,"source fade must reach requested root within bounded test");
    };
    tick(40);
    scene.Input({Circle, true, true, 1});
    tick(1);
    Check(scene.Snapshot().confirmations == 0, "held prior-dialog Circle cannot choose New Game");
    scene.Input({0, true, true, 2});
    scene.Input({Down, true, true, 3});
    tick(1);
    Check(scene.Snapshot().menu_row == 1, "cold124=-1 prevents invented Continue enablement");
    scene.Input({0, true, true, 4});
    scene.Input({Circle, true, true, 5});
    tick(1);
    Check(scene.Snapshot().state == 2 && scene.Snapshot().layout == 0,
          "confirm begins genuine contracting fade");
    await_root(2);
    Check(scene.Snapshot().layout == 2 && scene.Snapshot().state == 1 && scene.Snapshot().confirmations == 1,
          "root2 waits actual close/open,held Circle not inherited");
    scene.Input({0, true, true, 6});
    scene.Input({Right, true, true, 7});
    tick(1);
    Check(scene.Snapshot().selected_actor == 1 && scene.Snapshot().selection_moving,
          "Right starts real carousel target");
    scene.Input({0, true, true, 8});
    tick(11);
    Check(!scene.Snapshot().selection_moving && scene.Snapshot().actors[1].x == 0 &&
              scene.Snapshot().actors[1].z == 0,
          "selected target clamps by200,root2 placement has Z0");
    scene.Input({Cross, true, true, 9});
    tick(1);
    await_root(0);
    Check(scene.Snapshot().layout == 0 && scene.Snapshot().state == 1 && !scene.Snapshot().scene3_completed,
          "Cancel returns through fade instead of scene completion");
    scene.Input({0, true, true, 10});
    scene.Input({Circle, true, true, 11});
    tick(1);
    await_root(2);
    scene.Input({0, true, true, 12});
    scene.Input({Circle, true, true, 13});
    tick(1);
    Check(scene.Snapshot().pending_root == -1 && !scene.Snapshot().selection_motion_complete,
          "motion3 starts without immediate result");
    GameFsStatus status;
    for (unsigned i = 0; i < 100 && status; ++i)
        status = scene.TickAndDraw(graphics, fade);
    Check(!status && status.code == GameFsCode::Unavailable && scene.Snapshot().frontier_pc == 0x1FD80 &&
              scene.Snapshot().selection_motion_complete && scene.Snapshot().state == 1 &&
              scene.Snapshot().layout == 3 && scene.Snapshot().actors[1].x == -600 &&
              !scene.Snapshot().scene3_completed,
          "actual nonloop motion/fade/root3 reach explicit unsupported SOFTKEY,never fake CC3");
    Check(!manager.Close() && scene.Close() && arena.Used() == baseline,
          "menu owner survives until joined cleanup");
    clone.Close();
    ui->Close();
    font.Close();
    graphics.Close();
    Check(manager.Close() && !arena.Used(), "all borrowed and owned state retires");
}
} // namespace
int main() {
    try {
        MathAndSkin();
        GeometryAndRaster();
        ParticleDescriptor();
        Motion();
        ActorChannelOrder();
        TitleLifetimeAndFade();
        ArrivalBranchTests();
        TitleMenuLifecycle();
        std::cout << "model clone/geometry,skin/source quaternion,clip/texture "
                     "pixels,errors/retirement PASS\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
