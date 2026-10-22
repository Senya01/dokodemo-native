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
void Type12RegionDescriptor(){
    auto bytes=Fixture();Word(bytes,92,2);const auto node=bytes.size();W(bytes,0);H(bytes,12);
    for(auto v:{0,0,-1,255,0})H(bytes,v);
    for(const auto& row:std::array<GameVector4,4>{{{{3,4,5,1}},{{0,0,0,1}},{{0,0,0,1}},{{1,1,1,1}}}})
        for(auto v:row)F(bytes,v);
    H(bytes,1);H(bytes,1);F(bytes,1.25f);F(bytes,2.5f);Word(bytes,node,uint32_t(bytes.size()/4));
    auto lease=std::make_shared<Lease>(std::move(bytes));auto asset=std::make_shared<GameModelAsset>();
    Check(bool(asset->Decode(PackageStorage::Attach(lease,lease->b.size())))&&asset->Statistics().particle_emitters==1,
          "type12 source region payload admission");
    GameModelClone a,b;Check(bool(a.Initialize(asset))&&bool(b.Initialize(asset)),"emitter fields copied to separate native clones");
    Check(a.Pose()[1].emitter.first==1&&a.Pose()[1].emitter.second==1&&Near(a.Pose()[1].emitter.scalar0,1.25f),"exact12B selector/shape/dimension payload");
    a.Pose()[1].emitter.first=99;Check(b.Pose()[1].emitter.first==1,"type12 descriptor clone ownership");
    a.Pose()[0].position[0]=10;Check(bool(a.Update(ModelIdentity()))&&Near(a.Pose()[1].world[12],13),"emitter common parent transform propagated");
    Check(asset->Statistics().triangles==1,"particle descriptor creates no fabricated triangle/emission");
    GamePositionRegions regions;Check(bool(regions.Build(b))&&regions.Lists()[1].size()==1,"compile actual cloned type12 selection");
    Check(bool(regions.Build(a))&&regions.Lists()[1].empty(),"classifier uses current cloned field,not immutable source payload");
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
void RoomAnchorLocalX(){
    //Independent authored-pose literals: a quarter-turn sends local X into Y.
    std::vector<GameModelPose> pose(142);for(auto& p:pose)p.world=ModelIdentity();
    GameMatrix root=ModelIdentity();int32_t anchor=-9;
    const std::array<int32_t,9> nodes{{54,55,56,141,61,57,58,59,60}};
    for(size_t i=0;i<nodes.size();++i){pose[size_t(nodes[i])].world[12]=float(nodes[i]);
        Check(bool(RoomModelRootNativeSceneAnchorLocalX(int32_t(i+1),0,pose,root,anchor))&&anchor==nodes[i]&&root[12]==float(nodes[i]),"source selector map uses actual authored node");}
    auto& m=pose[57].world;m=ModelIdentity();m[0]=0;m[1]=1;m[4]=-1;m[5]=0;m[12]=10;m[13]=20;m[14]=30;
    Check(bool(RoomModelRootNativeSceneAnchorLocalX(6,2,pose,root,anchor))&&anchor==57&&root[12]==10&&root[13]==22&&root[14]==30,
          "NativeSceneAnchorLocalX rotates local spacing before world translation");
    m[13]=40;Check(bool(RoomModelRootNativeSceneAnchorLocalX(6,-3,pose,root,anchor))&&root[12]==10&&root[13]==37,
          "fresh authored pose replaces old transform without accumulating spacing");
    const auto prior=root;const auto prior_anchor=anchor;
    Check(!RoomModelRootNativeSceneAnchorLocalX(10,0,pose,root,anchor)&&root==prior&&anchor==prior_anchor,"out-of-range selector preserves output");
    Check(!RoomModelRootNativeSceneAnchorLocalX(6,std::numeric_limits<float>::infinity(),pose,root,anchor)&&root==prior,"nonfinite source spacing refusal");
    auto short_pose=pose;short_pose.resize(57);
    Check(!RoomModelRootNativeSceneAnchorLocalX(6,0,short_pose,root,anchor)&&root==prior,"missing selected authored node refusal");
    m[0]=std::numeric_limits<float>::quiet_NaN();
    Check(!RoomModelRootNativeSceneAnchorLocalX(6,0,pose,root,anchor)&&root==prior,"invalid authored transform refusal");
    Check(bool(RoomModelRootNativeSceneAnchorLocalX(0,-5,{},root,anchor))&&anchor==-1&&root[12]==-5&&root[13]==0,
          "native unparented local-X policy has no fabricated scene owner");
}
void RoomModelLifecycle(){
    //Synthetic DVS through the actual resource/asset/clone owner,not a fake slot.
    struct RoomSource final:GameResourceSource{
        bool missing=false;uint32_t calls=0;std::vector<std::weak_ptr<Lease>> leases;
        GameFsStatus OpenResource(const std::string& path,PackageView& out)override{
            ++calls;if(missing)return {GameFsCode::NotFound,0,"selected room fixture absent"};
            Check(path=="BG_ROOM\\Wallpaper0\\model01.DVS","source-selected static resource path");
            auto lease=std::make_shared<Lease>(Fixture());leases.push_back(lease);out=PackageStorage::Attach(lease,lease->b.size());return {};
        }
    } resources;
    auto selection=std::make_shared<Lease>(Fixture());GameTransition transition;
    Check(transition.Initialize({{0,0}}),"room slot owner transition");GameActorRuntime room;
    Check(bool(room.Initialize(0,PackageStorage::Attach(selection,selection->b.size()),transition,&resources)),"room slot source owner");
    {GameGraphics boot;uint32_t overlays=0;
        Check(bool(room.Draw(boot,[&](GameGraphics&){++overlays;return GameFsStatus{};}))&&overlays==0&&room.Snapshot().model_frames==0,
              "bootstrap before first room/camera has no admitted frame to publish");}
    int32_t a=-1,b=-1;
    Check(bool(room.CreateModel(170,0,0,-5,a))&&a==0&&bool(room.CreateModel(170,0,0,1,b))&&b==1,"real two-clone slot admission with stored spacing");
    auto s=room.Snapshot();Check(s.model_slots==2&&s.model_publications==2&&s.room_models[0].source_x==-5&&s.room_models[0].native_root[12]==-5,"native unparented controller retains actual source spacing");
    const auto requests=resources.calls;
    int32_t absent=-1;Check(!room.CreateModel(170,0,6,0,absent)&&absent==-1&&resources.calls==requests,"missing scene owner refuses before asset acquisition");
    Check(bool(room.UpdateModel(a,0,7))&&resources.calls==requests&&room.Snapshot().model_updates==1
          &&room.Snapshot().model_slots==2&&room.Snapshot().room_models[0].resource==170,"GAME33 updates only controller fields without reload/count change");
    Check(!room.UpdateModel(a,1,0)&&room.Snapshot().room_models[0].source_x==7,"missing authored scene owner transactional refusal");
    Check(bool(room.RetireModel(a))&&room.Snapshot().room_models[0].retirement==1&&!resources.leases[0].expired(),"retire request retains clone and borrowed source");
    {const auto r=room.Snapshot();const auto& w=r.model_retirement_history.back();Check(w.request==1&&w.slot==a&&w.resource==170&&w.generation==r.room_models[0].generation
       &&w.requested_phase==r.room_owner_phases&&!w.disabled_phase&&!w.destroyed_phase&&!w.asset_owner_expired,"request witness cannot precredit disable/destruction");}
    Check(bool(room.AdvanceActorFrame())&&room.Snapshot().room_models[0].retirement==2&&room.Snapshot().model_slots==2
          &&!resources.leases[0].expired(),"first source owner phase disables but retains");
    {const auto r=room.Snapshot();const auto& w=r.model_retirement_history.back();Check(w.disabled_phase==w.requested_phase+1&&!w.destroyed_phase&&w.assets_retained_at_disable
       &&w.clone_retained_at_disable&&w.nodes_at_disable>0&&!w.asset_owner_expired&&!resources.leases[0].expired(),"natural disable retains actual clone asset and borrowed lease");}
    Check(bool(room.RetireModel(a))&&room.Snapshot().room_models[0].retirement==1,"repeated source request restarts retirement");
    Check(bool(room.AdvanceActorFrame())&&!resources.leases[0].expired()&&bool(room.AdvanceActorFrame()),"two natural phases after repeated request");
    s=room.Snapshot();Check(!s.room_models[0].active&&s.model_slots==1&&s.model_retirements==1&&resources.leases[0].expired()
          &&!resources.leases[1].expired(),"only selected model source retired after deferred destruction");
    {const auto& w=s.model_retirement_history.back();const auto& first=s.model_retirement_history.front();Check(w.request==2&&w.requested_phase==1&&w.disabled_phase==2&&w.destroyed_phase==3
       &&w.asset_owner_expired&&w.clone_owner_cleared&&!first.destroyed_phase,"repeated request has distinct history;only newest request destroys the exact asset owner");}
    const auto old=s.room_models[0].generation;int32_t reused=-1;
    resources.missing=true;Check(!room.CreateModel(170,0,0,2,reused)&&reused==-1&&room.Snapshot().model_slots==1,"missing resource never publishes recycled slot");
    resources.missing=false;Check(bool(room.CreateModel(170,0,0,2,reused))&&reused==0&&room.Snapshot().room_models[0].generation>old,"first inactive slot has fresh native generation");
    for(unsigned i=0;i<20;++i)Check(bool(room.RetireModel(b)),"bounded passive history repeated source request");
    {const auto r=room.Snapshot();Check(r.model_retirement_history.size()==16&&r.model_retirement_history_dropped==6&&r.model_slots==2&&!resources.leases[1].expired(),"history overflow drops evidence only,never retires a source owner");}
    Check(bool(room.RetireModel(b))&&bool(room.RetireModel(reused))&&bool(room.AdvanceActorFrame())&&bool(room.AdvanceActorFrame())
          &&room.Snapshot().model_slots==0,"final room slots retire naturally");
    GameGraphics graphics;GameGraphicsConfig config;config.width=config.height=config.stride=32;config.scissor_right=config.scissor_bottom=32;
    Check(graphics.Initialize(config)&&bool(room.BindCamera(1)),"empty room still has live renderer/camera");uint32_t overlays=0;
    Check(bool(room.Draw(graphics,[&](GameGraphics& g){++overlays;return g.Clear(0xFF123456)?GameFsStatus{}:GameFsStatus{GameFsCode::Unavailable,0,"overlay clear"};})),"zero room slots still present owned overlay");
    GameGraphicsFrame frame;Check(overlays==1&&graphics.CopyFront(frame)&&frame.pixels[0]==0xFF123456&&room.Snapshot().model_frames==1,"empty-room frame is cleared/swapped,not stale");
    graphics.Close();
    room.Close();Check(resources.leases[1].expired()&&resources.leases[2].expired()&&!room.UpdateModel(0,0,0)&&!room.RetireModel(0),"close releases every clone/source and refuses late operation");
    transition.Retire();
}
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
void ActorPendingReplacement(){
    // Synthetic ASC/model owners; tests the source single pending slot,
    // not connected gameplay or additional opcode/effect coverage.
    auto lease=std::make_shared<Lease>(std::vector<uint8_t>(16));GameTransition fade;
    Check(fade.Initialize({{0,0}}),"pending actor fade");ActorSource source;GameActorRuntime actor;
    Check(bool(actor.Initialize(0,PackageStorage::Attach(lease,16),fade,&source)),"pending actor owner");
    Check(bool(actor.RequestActorEvent(176,0))&&bool(actor.RequestActorEvent(153,0)),"second positive request overwrites pending slot");
    const auto pending=actor.Snapshot();
    Check(pending.active_command==-1&&pending.requested_command==153&&pending.actor_request_pending
          &&pending.selected_extension_pending&&pending.event_requests==2&&pending.event_pending_replacements==1
          &&pending.command_pc==0&&pending.command_effects==0&&!actor.WaitReady(),"no active command or fake ready before natural owner pass");
    Check(!actor.RequestActorEvent(9999,0)&&!actor.RequestActorEvent(-1,0)&&!actor.RequestActorEvent(0,0)&&!actor.RequestActorEvent(176,1)
          &&actor.Snapshot().requested_command==153&&actor.Snapshot().event_requests==2,"bad replacement preserves accepted pending owner");
    Check(bool(actor.AdvanceActorFrame()),"natural owner consumes latest153");const auto active=actor.Snapshot();
    Check(active.active_command==153&&!active.actor_request_pending&&active.command_pc==12&&active.command_effects==1
          &&active.command_completions==0,"only latest pending command starts on natural phase");
    Check(bool(actor.RequestActorEvent(176,0))&&bool(actor.RequestActorEvent(157,0)),"replace again while old command active");
    const auto replaced=actor.Snapshot();
    Check(replaced.requested_command==157&&replaced.active_command==active.active_command
          &&replaced.command_pc==active.command_pc&&replaced.command_effects==active.command_effects
          &&replaced.effect_order==active.effect_order&&replaced.command_ready==active.command_ready
          &&replaced.command_repeat_ready258==active.command_repeat_ready258&&replaced.channel_admissions==active.channel_admissions
          &&replaced.command_completions==0&&replaced.event_pending_replacements==2&&!actor.WaitReady(),"pending writes retain active cursor flags motion and completion");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().active_command==157
          &&actor.Snapshot().command_pc==12&&actor.Snapshot().command_effects==1
          &&actor.Snapshot().command_completions==0,"natural next phase resets cursor for157 without completing153");
    Check(bool(actor.RequestActorEvent(176,0))&&bool(actor.AdvanceActorFrame())&&actor.Snapshot().command_flag25A,
          "176 first8040 produces true25A");
    Check(bool(actor.RequestActorEvent(157,0))&&actor.Snapshot().command_flag25A,
          "pending request retains active25A");
    Check(bool(actor.AdvanceActorFrame())&&actor.Snapshot().active_command==157&&!actor.Snapshot().command_flag25A,
          "natural24700 clears old25A before157 first8030");
    Check(bool(actor.RequestActorEvent(176,0)),"pending owner before close");actor.Close();
    Check(!actor.RequestActorEvent(153,0)&&!actor.WaitReady()&&!actor.Snapshot().actor_request_pending,"close retires pending and active owners");
    GameActorRuntime moving;
    Check(bool(moving.Initialize(0,PackageStorage::Attach(lease,16),fade,&source))&&bool(moving.RequestActorEvent(153,0))
          &&bool(moving.AdvanceActorFrame())&&bool(moving.AdvanceActorFrame()),"nontrivial old movement fixture");
    const auto old=moving.Snapshot();
    Check(old.movement_pending&&old.pending_logical_motion==9&&!moving.RequestActorEvent(176,0)
          &&moving.Snapshot().active_command==old.active_command&&moving.Snapshot().command_pc==old.command_pc
          &&moving.Snapshot().event_requests==old.event_requests&&!moving.Snapshot().actor_request_pending,
          "unrecovered physical-mode interruption refuses transactionally; no new command waits behind old mode");
    moving.Close();
    fade.Retire();
}
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
    bool position=false;Check(bool(scene.QueryActorPosition(position))&&position
          &&scene.Snapshot().position3B0==scene.Snapshot().actor_root&&scene.Snapshot().position_queries==1,
          "C0A8 copies actual root into owned query point without changing actor40");
    int32_t slot=-1;Check(bool(scene.CreateModel(170,0,0,0,slot))&&bool(scene.BindCamera(1)),"synthetic scene render owners");
    GameGraphics graphics;Check(graphics.Initialize({})&&bool(scene.Draw(graphics)),"native following-camera frame");
    Check(scene.Snapshot().camera_position==GameVector4{{600,-4700,980,1}}
          &&scene.Snapshot().actor_position==GameVector4{{0,0,600,1}},"explicit native root follow never overwrites unproved source actor40");
    graphics.Close();
    scene.Close();Check(!scene.Snapshot().scene_model_live&&!scene.Snapshot().scene_controls&&!scene.WaitReady(),"scene controls retired with actor");
    position=false;Check(!scene.QueryActorPosition(position)&&!position&&!scene.Snapshot().region_owners_live,
          "retired scene cannot fabricate GAME19 return");
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
void PositionRegionTests(){
    GamePositionRegions owner;bool inside=false;GameVector4 point{{0,0,7,1}},copy=point;
    Check(!owner.ContainsSecond(point,inside)&&!inside,"unbuilt region owner refuses without result publication");
    Check(bool(owner.Initialize({}))&&bool(owner.ContainsSecond(point,inside))&&inside,"source empty second list means true");
    Check(bool(owner.FallbackSecond(point,copy))&&copy==point,"empty fallback preserves original value includingZ");
    GameRegionInput a;a.node=74;a.selector=1;a.shape=1;a.first=10;a.second=20;
    a.world[12]=30;a.world[13]=-5;
    GameRegionInput b=a;b.node=75;b.world[12]=70;
    Check(bool(owner.Initialize({a,b})),"two source-ordered owned quads");
    a.world[12]=999;
    Check(owner.Lists()[1][0].center[0]==30,"compiled regions own matrix-derived values");
    for(const auto p:{GameVector4{{30,-5,0,1}},GameVector4{{40,0,900,1}},GameVector4{{20,-10,-900,1}}}){
        inside=false;Check(bool(owner.ContainsSecond(p,inside))&&inside,"inclusive affine quad boundary ignores sourceZ");}
    inside=true;Check(bool(owner.ContainsSecond({{40.01f,0,0,1}},inside))&&!inside,"outside all source quads is false");
    Check(bool(owner.FallbackSecond({{50,-5,0,1}},copy))&&copy==GameVector4{{30,-5,0,1}},"equal center distance keeps first candidate,not nearest boundary");
    auto blocker=b;blocker.selector=2;blocker.first=100;blocker.second=100;blocker.world[12]=30;
    Check(bool(owner.Initialize({b,blocker}))&&bool(owner.FallbackSecond(point,copy))&&copy==GameVector4{{0,0,1,1}},"fully contained candidate is excluded from fallback");
    // Partial overlap does not fully contain a candidate.
    a.world[12]=30;blocker.first=10;blocker.second=10;
    Check(bool(owner.Initialize({a,blocker}))&&bool(owner.FallbackSecond({{50,-5,0,1}},copy))&&copy==GameVector4{{30,-5,0,1}},"partial overlap cannot remove fallback candidate");
    blocker.second=20;
    Check(bool(owner.Initialize({a,blocker}))&&bool(owner.FallbackSecond(point,copy))&&copy==GameVector4{{0,0,1,1}},"no eligible fallback returns ownedP with affineZ1");
    GameRegionInput circle;circle.node=1;circle.selector=1;circle.shape=2;circle.first=5;
    Check(bool(owner.Initialize({circle})),"circle region");
    inside=false;Check(bool(owner.ContainsSecond({{3,4,19,1}},inside))&&inside,"inclusive XY circle radius");
    inside=true;Check(bool(owner.ContainsSecond({{3,4.01f,0,1}},inside))&&!inside,"outside circle refuses membership");
    auto circular_blocker=circle;circular_blocker.selector=2;circular_blocker.first=10;
    Check(bool(owner.Initialize({circle,circular_blocker}))&&bool(owner.FallbackSecond(point,copy))&&copy==GameVector4{{0,0,1,1}},"fully contained circle excluded");
    circular_blocker.world[12]=12;
    Check(bool(owner.Initialize({circle,circular_blocker}))&&bool(owner.FallbackSecond(point,copy))&&copy==GameVector4{{0,0,0,1}},"partial circle overlap is not containment");
    circular_blocker.world=ModelIdentity();circular_blocker.first=5;
    Check(bool(owner.Initialize({circle,circular_blocker}))&&bool(owner.FallbackSecond(point,copy))&&copy==GameVector4{{0,0,0,1}},"circle contact is not strict full containment");
    GameRegionInput rotated=a;rotated.world=ModelIdentity();rotated.world[0]=0;rotated.world[1]=1;rotated.world[4]=-1;rotated.world[5]=0;
    Check(bool(owner.Initialize({rotated})),"quad uses actual transformed basis");
    inside=false;Check(bool(owner.ContainsSecond({{5,10,0,1}},inside))&&inside,"rotated source corner included");
    auto bad=rotated;bad.shape=3;Check(!owner.Initialize({rotated,bad})&&!owner.Live(),"unsupported shape cannot publish partial owner");
    bad=rotated;bad.world[0]=bad.world[1]=0;Check(!owner.Initialize({bad}),"degenerate compiled edge refuses");
    std::vector<GameRegionInput> full(257,circle);Check(!owner.Initialize(full),"original256 region cap enforced");
    Check(bool(owner.Initialize({circle})),"fresh owner after failure");owner.Close();
    inside=true;Check(!owner.ContainsSecond(point,inside)&&inside,"retirement refuses without fabricated Boolean");
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
        RoomAnchorLocalX();RoomModelLifecycle();
        Type12RegionDescriptor();
        Motion();
        ActorChannelOrder();
        ActorPendingReplacement();
        PositionRegionTests();
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
