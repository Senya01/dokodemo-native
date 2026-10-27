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
std::vector<uint8_t> Fixture(bool normals=false,const std::vector<std::array<int16_t,3>>* custom=nullptr,bool textured=false) {
    const std::vector<std::array<int16_t,3>> regular{{{-25000,-25000,0}},{{25000,-25000,0}},{{0,25000,0}}};
    const auto &positions=custom?*custom:regular;
    std::vector<uint8_t> b{'D', 'V', 'S', 0};
    b.resize(16);
    W(b, textured?24:6);
    W(b, textured?1:0);
    if(textured){
        const auto common=b.size();b.resize(common+36);b[common]=1;
        W(b,0);const auto name=b.size();b.resize(name+32);
        const std::string stem="CONTACT_TEST";std::copy(stem.begin(),stem.end(),b.begin()+name);
    }
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
    H(b, normals?0x4009:9);
    b.push_back(textured?0:255);
    b.push_back(0);
    H(b, int32_t(positions.size()));
    F(b, 1);
    for(const auto &p:positions)for(auto v:p)H(b,v);
    while (b.size() % 4)
        b.push_back(0);
    if(normals){
        for(size_t i=0;i<positions.size();++i)b.insert(b.end(),{0,0,126});
        while(b.size()%4)b.push_back(0);
    }
    b.insert(b.end(), {255, 255, 255, 128});
    if(textured)for(size_t i=0;i<positions.size();++i){F(b,.5f);F(b,.5f);}
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

struct BackgroundFileState {
    bool live=false,fail_read=false,fail_close=false;
    uint32_t reads=0,closes=0;
    std::vector<std::shared_ptr<Lease>> buffers;
};
class BackgroundFile final:public GameFileSource {
    std::vector<uint8_t> bytes_;
    std::shared_ptr<BackgroundFileState> state_;
  public:
    BackgroundFile(std::vector<uint8_t> bytes,std::shared_ptr<BackgroundFileState> state)
        :bytes_(std::move(bytes)),state_(std::move(state)){}
    ~BackgroundFile()override{(void)Close();}
    GameFsStatus Open()override{state_->live=true;return {};}
    GameFsStatus Read(uint64_t at,uint32_t count,PackageView& out)override{
        if(!state_->live||at>bytes_.size()||count>bytes_.size()-at||state_->fail_read)
            return {GameFsCode::IoFailure,0,"synthetic read failure"};
        auto lease=std::make_shared<Lease>(std::vector<uint8_t>(bytes_.begin()+size_t(at),bytes_.begin()+size_t(at)+count));
        state_->buffers.push_back(lease);out=PackageStorage::Attach(lease,count);++state_->reads;return {};
    }
    GameFsStatus Close()override{
        if(state_->live&&state_->fail_close)return {GameFsCode::IoFailure,0,"synthetic close failure"};
        if(state_->live){for(auto& b:state_->buffers)b->live=false;state_->live=false;++state_->closes;}
        return {};
    }
    GameFileIoSnapshot Snapshot()const override{GameFileIoSnapshot s;s.file_live=state_->live;s.file_bytes=bytes_.size();s.read_calls=state_->reads;return s;}
};
std::vector<uint8_t> BackgroundArchive(const std::vector<std::pair<std::string,std::vector<uint8_t>>>& entries){
    size_t names=0;for(const auto& e:entries)names+=e.first.size()+1;
    const auto payload=(16+entries.size()*16+names+15)&~size_t(15);
    std::vector<uint8_t> out(payload);Word(out,0,1);Word(out,4,uint32_t(entries.size()));Word(out,8,uint32_t(names));Word(out,12,uint32_t(payload));
    size_t name=16+entries.size()*16;
    for(size_t i=0;i<entries.size();++i){
        const auto& e=entries[i];const auto at=16+i*16;
        Word(out,at,uint32_t(name-16-entries.size()*16));Word(out,at+4,uint32_t(out.size()-payload));Word(out,at+8,uint32_t(e.second.size()));Word(out,at+12,i+1<entries.size()?uint32_t(i+1):UINT32_MAX);
        std::copy(e.first.begin(),e.first.end(),out.begin()+name);name+=e.first.size()+1;
        out.insert(out.end(),e.second.begin(),e.second.end());out.resize((out.size()+15)&~size_t(15));
    }
    return out;
}
void BackgroundPreparationTests(){
    // Arbitrary synthetic names/geometry; no extracted registry/asset fixture.
    auto records=std::make_shared<Lease>(std::vector<uint8_t>(60));
    std::vector<std::pair<std::string,std::vector<uint8_t>>> packages;
    for(unsigned i=0;i<3;++i){const auto name=std::string("SYN_")+char('A'+i);
        std::copy(name.begin(),name.end(),records->b.begin()+i*20);
        packages.push_back({name+".BPM",BackgroundArchive({{name+"\\model01.DVS",Fixture()}})});
    }
    const auto file=BackgroundArchive(packages);
    auto owner=std::make_shared<int>(11),foreign=std::make_shared<int>(12);
    GameBackgroundPreparation job;GameGraphics graphics;Check(graphics.Initialize({}),"background offscreen renderer");
    const auto view=[&]{return PackageStorage::Attach(records,records->b.size());};
    const std::array<uint32_t,6> variants{{0,1,1,2,0,0}};
    for(uint32_t period=0;period<6;++period){
        auto io=std::make_shared<BackgroundFileState>();
        Check(bool(job.Begin(owner,period+1,{21,12,period,period+1},view(),std::make_unique<BackgroundFile>(file,io))),"real initialized pending background owner");
        auto pending=job.Snapshot();Check(pending.pending&&!pending.staged&&!pending.source_committed&&pending.resource==66+variants[period]&&pending.candidate==21,"live period lineage, not scene publication");
        auto foreign_control=std::shared_ptr<const void>(owner.get(),[](const void*){});
        Check(!job.Load(foreign_control,period+1)&&!job.Load(foreign,period+1)&&!job.Load(owner,period+2)&&io->reads==2,"foreign/stale epoch refused before member load");
        Check(bool(job.Load(owner,period+1)),"actual nested package/DVS clone load");
        auto ready=job.Snapshot();Check(ready.live&&ready.staged&&!ready.pending&&!ready.failed&&!ready.source_committed&&ready.model.vertices==3&&ready.variant==variants[period],"actual staged model only");
        Check(!job.Load(owner,period+1),"completed request cannot replay load");
        Check(graphics.Clear(0xFF000000)&&bool(job.Draw(owner,period+1,graphics,ModelIdentity()))&&graphics.Snapshot().mesh_pixels>0,"production staged clone render");
        Check(!job.Draw(foreign,period+1,graphics,ModelIdentity()),"foreign draw refused");
        Check(!job.Begin(owner,99,{1,12,0,99},view(),std::make_unique<BackgroundFile>(file,std::make_shared<BackgroundFileState>())),"pending/current cannot be overwritten");
        Check(bool(job.Close())&&io->closes==1&&!job.Snapshot().live,"clone/template retire before real file close");
        for(const auto& b:io->buffers)Check(!b->live,"every staged allocation retired");
        Check(!job.Draw(owner,period+1,graphics,ModelIdentity())&&bool(job.Close()),"retired draw and idempotent close");
    }
    auto io=std::make_shared<BackgroundFileState>();
    Check(!job.Begin(owner,0,{1,12,0,1},view(),std::make_unique<BackgroundFile>(file,io))&&!io->reads,"uninitialized epoch no I/O");
    Check(!job.Begin(owner,7,{1,11,0,7},view(),std::make_unique<BackgroundFile>(file,io)),"other selector unimplemented");
    Check(!job.Begin(owner,7,{1,12,6,7},view(),std::make_unique<BackgroundFile>(file,io)),"invalid period not default variant");
    records->b[0]='/';Check(!job.Begin(owner,7,{1,12,0,7},view(),std::make_unique<BackgroundFile>(file,io)),"unsafe registration no load");records->b[0]='S';
    Check(bool(job.Begin(owner,7,{1,12,0,7},view(),std::make_unique<BackgroundFile>(file,io))),"fresh owner after retirement");
    const auto reads=io->reads;records->b[1]='X';Check(!job.Load(owner,7)&&io->reads==reads&&!job.Snapshot().live,"current registration drift refuses before I/O");records->b[1]='Y';
    Check(bool(job.Close()),"drifted owner can retire");
    Check(!job.Begin(owner,7,{1,12,0,7},view(),std::make_unique<BackgroundFile>(file,io)),"retired epoch/request cannot rearm");
    io=std::make_shared<BackgroundFileState>();Check(bool(job.Begin(owner,8,{1,12,0,8},view(),std::make_unique<BackgroundFile>(file,io))),"failure pending");io->fail_read=true;
    Check(!job.Load(owner,8)&&job.Snapshot().failed&&!job.Snapshot().staged&&!job.Snapshot().source_committed,"failed load never becomes scene success");Check(bool(job.Close()),"failed load cleanup");
    io=std::make_shared<BackgroundFileState>();Check(bool(job.Begin(owner,9,{1,12,0,9},view(),std::make_unique<BackgroundFile>(file,io))),"weak owner pending");owner.reset();
    Check(!job.Snapshot().live&&!job.Load(foreign,9)&&bool(job.Close()),"owner expiration refuses, explicit file retirement");

    auto bad_model=Fixture();const auto parsed=dokodemo::model::parseDvs(bad_model);const auto node=parsed.nodes[0].source.offset;
    bad_model[node+4]=13;bad_model[node+5]=0;bad_model[node+6+74+6]=255;bad_model[node+6+74+7]=0;
    const auto bad_file=BackgroundArchive({{"SYN_A.BPM",BackgroundArchive({{"SYN_A\\model01.DVS",bad_model}})}});
    io=std::make_shared<BackgroundFileState>();Check(bool(job.Begin(foreign,10,{1,12,0,10},view(),std::make_unique<BackgroundFile>(bad_file,io))),"unadmitted marker tail pending");
    const auto unsupported=job.Load(foreign,10);const auto cut=job.Snapshot();
    Check(!unsupported&&unsupported.code==GameFsCode::Unavailable&&cut.failed&&cut.package_read&&!cut.staged&&!cut.source_committed&&cut.package_owner,"unadmitted marker tail refuses after package read, not ready");
    io->fail_close=true;Check(!job.Close()&&io->live&&!job.Snapshot().staged,"close failure retains backing for retry");
    Check(!job.Begin(foreign,11,{1,12,0,11},view(),std::make_unique<BackgroundFile>(file,std::make_shared<BackgroundFileState>())),"failed retirement cannot restart");
    io->fail_close=false;Check(bool(job.Close())&&!io->live,"explicit retirement retry succeeds");
    graphics.Close();
}

std::vector<uint8_t> AreaMarkerFixture(uint16_t flags=3,bool material=true){
    auto b=Fixture();auto parsed=dokodemo::model::parseDvs(b);
    if(material){
        const auto old=parsed.nodes[0].source.offset;
        b.insert(b.begin()+24,36,0);Word(b,16,15);Word(b,20,1);Word(b,old+36,uint32_t(b.size()/4));
        parsed=dokodemo::model::parseDvs(b);
    }
    const auto node=parsed.nodes[0].source.offset;std::vector<uint8_t> marker;
    W(marker,0);H(marker,13);H(marker,flags);H(marker,-1);H(marker,-1);H(marker,255);H(marker,0);
    // Synthetic pose and typed payload, not the extracted156B target nodes.
    for(const auto& v:std::array<GameVector4,4>{{{{5,8,11,1}},{{0,0,0,1}},{{0,0,.3826834324f,.9238795325f}},{{2,4,3,1}}}})
        for(auto x:v)F(marker,x);
    if(flags&1){auto optional=ModelIdentity();optional[12]=2;for(auto x:optional)F(marker,x);}
    H(marker,9);H(marker,7);H(marker,13);H(marker,material?0:255);W(marker,0x11223344);
    b.insert(b.begin()+node,marker.begin(),marker.end());Word(b,node,uint32_t((node+marker.size())/4));
    const auto mesh=node+marker.size();Word(b,mesh,uint32_t(b.size()/4));
    b[mesh+8]=b[mesh+9]=0; // geometry child really inherits marker0
    uint32_t one;const float f=1;std::memcpy(&one,&f,4);Word(b,mesh+16,one);
    b[parsed.bounds.source.offset+parsed.bounds.source.bytes]=2; // node count
    return b;
}
void AreaMarkerTests(){
    constexpr auto windows_basis=GameModelTransformPolicy::WindowsAreaMarkerCanonicalBasis;
    const auto near=[](float a,float b){return std::abs(a-b)<.0003f;};
    auto bytes=AreaMarkerFixture();const auto parsed=dokodemo::model::parseDvs(bytes);
    Check(parsed.nodes.size()==2&&parsed.nodes[0].areaMarker.field208==9&&parsed.nodes[0].areaMarker.field20C==7&&parsed.nodes[0].areaMarker.field214==13&&parsed.nodes[0].areaMarker.word210==0x11223344&&parsed.nodes[0].areaMarker.material==0,"source typed12B marker parse");
    auto lease=std::make_shared<Lease>(bytes);auto asset=std::make_shared<GameModelAsset>();
    const auto strict=asset->Decode(PackageStorage::Attach(lease,lease->b.size()));
    Check(!strict&&strict.code==GameFsCode::Unavailable&&!asset->Live(),"unknown PSP flag2 basis is refused by default");
    Check(bool(asset->Decode(PackageStorage::Attach(lease,lease->b.size()),nullptr,{},windows_basis)),"finite area-marker model load");
    Check(asset->Statistics().area_markers==1&&asset->Statistics().meshes==1&&asset->Statistics().vertices==3,"marker is typed metadata, no manufactured geometry");
    GameModelClone clone,second;Check(bool(clone.Initialize(asset))&&bool(second.Initialize(asset)),"independent marker clones share live immutable template");
    auto root=ModelIdentity();root[12]=100;root[13]=200;root[14]=300;
    Check(bool(clone.Update(root)),"marker oriented-scale hierarchy update");std::vector<GameAreaMarkerState> markers;
    Check(bool(clone.AreaMarkers(*asset,markers))&&markers.size()==1,"current owned marker-list consumer");const auto m=markers[0];
    Check(m.node==0&&m.parent==-1&&m.material==0&&m.parameters.word210==0x11223344&&near(m.world[0],3)&&near(m.world[1],1)&&near(m.world[4],1)&&near(m.world[5],3)&&near(m.world[10],3),"R O S inverseO order under named native canonical-basis policy");
    Check(near(m.world[12],105)&&near(m.world[13],208)&&near(m.final_matrix[12],111)&&near(m.final_matrix[13],210),"optional final matrix follows world, not a second parent");
    const auto& pose=static_cast<const GameModelClone&>(clone).Pose();Check(near(pose[1].world[12],108)&&near(pose[1].world[13],209),"child inherits marker world without optional correction twice");
    Check(bool(second.AreaMarkers(*asset,markers))&&near(markers[0].world[12],5),"clone transforms do not alias");
    GameModelAsset foreign;markers[0].node=999;Check(!clone.AreaMarkers(foreign,markers)&&markers[0].node==999,"foreign template cannot replace current copied receipt");
    auto nonfinite_root=root;nonfinite_root[12]=std::numeric_limits<float>::infinity();
    Check(!clone.Update(nonfinite_root)&&!clone.AreaMarkers(*asset,markers),"nonfinite composed marker matrix refuses receipt");
    Check(bool(clone.Update(root)),"valid explicit root restores pose");
    auto& retained_pose=clone.Pose();Check(bool(clone.Update(root)),"retained alias baseline update");
    retained_pose[0].position[0]=99;Check(!clone.AreaMarkers(*asset,markers),"retained input alias cannot reuse a stale marker receipt");
    retained_pose[0].position[0]=5;Check(bool(clone.Update(root)),"explicit producer restores consistent pose");
    retained_pose[0].world[12]+=1;Check(!clone.AreaMarkers(*asset,markers),"retained world alias drift is checked directly");
    Check(bool(clone.Update(root)),"world drift repaired only by explicit update");
    clone.Pose()[0].position[0]=6;Check(!clone.AreaMarkers(*asset,markers)&&bool(clone.Update(root))&&bool(clone.AreaMarkers(*asset,markers))&&near(markers[0].world[12],106),"mutable pose invalidates receipt until real update");
    clone.Pose()[0].material=-1;Check(bool(clone.Update(root))&&!clone.AreaMarkers(*asset,markers),"unclosed marker material writer refuses");clone.Pose()[0].material=0;
    clone.Pose()[0].scale_orientation={{0,0,0,0}};Check(!clone.Update(root)&&!clone.AreaMarkers(*asset,markers),"invalid quaternion never publishes marker matrix");clone.Pose()[0].scale_orientation={{0,0,.3826834324f,.9238795325f}};
    Check(bool(clone.Update(ModelIdentity())),"recover only after valid explicit pose update");
    GameGraphics graphics;Check(graphics.Initialize({})&&graphics.Clear(0xFF000000),"marker child raster");auto camera=ModelIdentity();camera[0]=camera[5]=camera[10]=.1f;camera[12]=-.6f;camera[13]=-.8f;camera[14]=-1.1f;
    const auto pixels=graphics.Snapshot().mesh_pixels;Check(bool(clone.Draw(graphics,camera))&&graphics.Snapshot().mesh_pixels>pixels,"geometry child renders through actual transformed marker parent");graphics.Close();
    for(uint16_t flags:{uint16_t(0),uint16_t(1),uint16_t(2)}){
        auto l=std::make_shared<Lease>(AreaMarkerFixture(flags,false));auto a=std::make_shared<GameModelAsset>();GameModelClone c;
        Check(bool(a->Decode(PackageStorage::Attach(l,l->b.size()),nullptr,{},flags&2?windows_basis:GameModelTransformPolicy::SourceCommon))&&bool(c.Initialize(a))&&bool(c.AreaMarkers(*a,markers)),"closed flags0..3 and material255 null binding");
        Check(markers[0].material==-1&&near(markers[0].world[0],flags&2?3.f:1.f),"source flag2 controls scale consumption");
        c.Close();Check(!c.AreaMarkers(*a,markers),"closed marker owner refuses");
    }
    for(unsigned kind=0;kind<4;++kind){
        auto b=AreaMarkerFixture();const auto p=dokodemo::model::parseDvs(b);const auto n=p.nodes[0].source.offset;const auto end=n+p.nodes[0].source.bytes;
        if(kind==0){b[n+6]=7;} // unknown common flag4
        if(kind==1){b[end-6]=3;} // foreign material index
        if(kind==2){Word(b,n,uint32_t((end-4)/4));} // truncated payload
        if(kind==3){b[n+8]=1;b[n+9]=0;} // actual parent cycle through geometry child
        auto l=std::make_shared<Lease>(b);auto a=std::make_shared<GameModelAsset>();auto status=a->Decode(PackageStorage::Attach(l,l->b.size()),nullptr,{},windows_basis);GameModelClone c;
        Check(!status||!c.Initialize(a),"malformed marker bounds/flags/material/cycle refuse");
    }
    lease->live=false;Check(!clone.AreaMarkers(*asset,markers)&&!clone.Update(root)&&!second.AreaMarkers(*asset,markers),"retired source template refuses every marker clone");clone.Close();second.Close();
}
struct ContactTextureSource final:GameResourceSource {
    uint8_t alpha=0;
    GameFsStatus OpenResource(const std::string& path,PackageView& out) override {
        if(path!="CONTACT_TEST.GIM")return {GameFsCode::NotFound,0,"synthetic contact texture path"};
        // Tiny synthetic 2x2 white RGBA image; no original texture pixels.
        std::vector<uint8_t> b(144,0);const char magic[]="MIG.00.1PSP";
        std::copy(std::begin(magic),std::end(magic),b.begin());
        const auto half=[&](size_t at,uint16_t v){b.at(at)=uint8_t(v);b.at(at+1)=uint8_t(v>>8);};
        for(size_t at:{16u,32u}){half(at,at==16?2:3);half(at+2,16);Word(b,at+4,uint32_t(b.size()-at));Word(b,at+8,16);Word(b,at+12,16);}
        half(48,4);half(50,16);Word(b,52,96);Word(b,56,96);Word(b,60,16);
        half(64,48);half(68,3);half(72,2);half(74,2);half(76,32);half(78,4);half(80,1);
        Word(b,88,48);Word(b,92,64);Word(b,96,80);half(104,1);half(106,1);half(108,3);half(110,1);Word(b,112,64);
        for(size_t at=128;at<144;at+=4)Word(b,at,0x00FFFFFFu|uint32_t(alpha)<<24);
        auto lease=std::make_shared<Lease>(std::move(b));out=PackageStorage::Attach(lease,lease->b.size());return {};
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
void TexturedColorApproximation(){
    GameGraphics graphics;GameGraphicsConfig config;
    config.width=config.height=config.stride=16;
    config.scissor_right=config.scissor_bottom=16;
    config.cull_face=false;
    Check(graphics.Initialize(config),"color adapter raster initializes");
    const auto sample=[&](uint32_t vertex,uint32_t texel,int scale,bool textured=true){
        Check(graphics.Clear(0),"color adapter clear");
        std::array<GameMeshVertex,3> vertices{{
            {{{-1,-1,0,1}},0,0,vertex},{{{1,-1,0,1}},0,0,vertex},{{{0,1,0,1}},0,0,vertex}}};
        GameRasterPolicy policy;policy.two_sided=true;policy.blend=false;
        if(scale>=0)policy.windows_dvs_textured_modulation2x=scale!=0;
        const GameTextureSpan texture=textured?GameTextureSpan{&texel,1,1,1}:GameTextureSpan{};
        Check(graphics.DrawMeshTriangle(vertices,texture,policy)&&graphics.SwapBuffers(),"color adapter actual draw");
        GameGraphicsFrame frame;Check(graphics.CopyFront(frame),"color adapter actual readback");
        return frame.pixels[8*frame.stride+8];
    };
    Check(sample(0xFF7F7C6D,0xFFFDFDFD,false)==0xFF7E7B6C,"default/UI path retains1x modulation");
    Check(sample(0xFF808080,0xFFE0D9C9,-1)==0xFF706D65,"untouched default raster/UI policy remains1x");
    Check(sample(0xFF808080,0xFFE0D9C9,true)==0xFFE1DACA,"room neutral128 preserves actual fridge texture tone");
    Check(sample(0xFF808080,0xFF463329,true)==0xFF463329,"room neutral128 preserves actual floor texture tone");
    Check(sample(0xFF565656,0xFFE0D9C9,true)==0xFF979288,"room baked darker vertex colour remains darker under2x");
    Check(sample(0xFF7F7C6D,0xFFFDFDFD,true)==0xFFFCF6D8,"Toro real neutral texel reaches body within1 RGB");
    Check(sample(0xFFFDF7D9,0xFFFFFFFF,true,false)==0xFFFDF7D9,"untextured body stays source RGB");
    Check(sample(0x80C8C8C8,0x80404040,true)==0x40646464,"post-product2x preserves dark texels and alpha product");
    Check(sample(0xFFC8C8C8,0xFFFFFFFF,true)==0xFFFFFFFF,"post-product saturation clamps RGB");
}
void NormalLighting(){
    auto scale=ModelIdentity();scale[0]=2;scale[10]=3;
    scale[12]=99;scale[13]=-71;
    const auto normal=ModelTransformNormal(scale,{{1,1,0,0}});
    Check(Near(normal[0],.5f)&&Near(normal[1],1)&&normal[3]==0,"normal inverse transpose ignores translation");
    const auto tangent=ModelTransform(scale,{{1,-1,0,0}});
    Check(Near(normal[0]*tangent[0]+normal[1]*tangent[1],0),"nonuniform normal stays perpendicular to transformed tangent");
    auto rotate=ModelIdentity();rotate[0]=rotate[5]=0;rotate[1]=1;rotate[4]=-1;
    const auto turned=ModelTransformNormal(rotate,{{1,0,0,0}});
    Check(Near(turned[0],0)&&Near(turned[1],1),"normal follows bone rotation");
    auto shear=ModelIdentity();shear[4]=2;
    const auto sheared=ModelTransformNormal(shear,{{1,0,0,0}});
    Check(Near(sheared[0],1)&&Near(sheared[1],-2),"normal cofactor orientation handles shear");
    auto reflection=ModelIdentity();reflection[0]=-1;
    const auto reflected=ModelTransformNormal(reflection,{{1,0,0,0}});
    Check(Near(reflected[0],-1),"normal reflection preserves negative determinant sign");
    auto singular=ModelIdentity();singular[0]=0;bool refused=false;
    try{(void)ModelTransformNormal(singular,{{1,0,0,0}});}catch(const std::exception&){refused=true;}
    Check(refused,"singular normal matrix refuses");
    dokodemo::model::Geometry g;g.positions.push_back({0,0,0});g.normals.push_back({126,126,0});
    g.weightRecords.assign(24,0);Word(g.weightRecords,0,0x00010000);Word(g.weightRecords,4,0xFFFFFFFF);
    Word(g.weightRecords,8,0x3F000000);Word(g.weightRecords,12,0x3F000000);
    std::vector<GameMatrix> palette{scale,ModelIdentity()};
    const auto blended=GameModelClone::SkinNormal(g,0,palette);
    Check(Near(blended[0],.6f)&&Near(blended[1],.8f),"skin blends unnormalized contributions before final normalization");
    Word(g.weightRecords,0,0xFFFF0001);Word(g.weightRecords,8,0x7FC00000);
    const auto rigid=GameModelClone::SkinNormal(g,0,palette);
    Check(Near(rigid[0],.70710678f)&&Near(rigid[1],.70710678f),"rigid skin normal ignores unused NaN weights");
    Word(g.weightRecords,0,0xFFFF0002);refused=false;
    try{(void)GameModelClone::SkinNormal(g,0,palette);}catch(const std::exception&){refused=true;}
    Check(refused,"normal palette bounds refuse");
    const GameVector4 key{{-.3f,-.7f,.65f,0}},away{{.3f,.7f,-.65f,0}};
    Check(ModelWindowsActorSoftDiffuse(0x80FFFFFF,key)==0x80FFFFFF&&
          ModelWindowsActorSoftDiffuse(0x80FFFFFF,away)==0x80B8B8B8,"soft key/ambient preserve alpha without overbrightening");
    auto lease=std::make_shared<Lease>(Fixture(true));auto asset=std::make_shared<GameModelAsset>();
    Check(bool(asset->Decode(PackageStorage::Attach(lease,lease->b.size()))),"actual normal-bearing DVS admitted");
    GameModelClone clone;Check(bool(clone.Initialize(asset)),"normal-bearing clone initialized");
    GameGraphics graphics;GameGraphicsConfig config;config.width=config.height=config.stride=32;
    config.scissor_right=config.scissor_bottom=32;config.cull_face=false;
    Check(graphics.Initialize(config),"normal draw surface initialized");
    const auto sample=[&](GameModelLightingPolicy policy){
        Check(graphics.Clear(0)&&bool(clone.Draw(graphics,ModelIdentity(),GameModelColorPolicy::SourceRgb,policy))&&graphics.SwapBuffers(),"normal policy actual model draw");
        GameGraphicsFrame frame;Check(graphics.CopyFront(frame),"normal actual framebuffer readback");
        return frame.pixels[16*frame.stride+16];
    };
    Check(sample(GameModelLightingPolicy::Unlit)==0xFFFFFFFF,"normal-bearing default model stays unlit");
    const auto lit=sample(GameModelLightingPolicy::WindowsActorSoftDiffuse);
    Check((lit>>24)==255&&(lit&255)>184&&(lit&255)<255,"actor opt-in shades actual source-normal triangle");
    lease->live=false;
    Check(!clone.Draw(graphics,ModelIdentity(),GameModelColorPolicy::SourceRgb,GameModelLightingPolicy::WindowsActorSoftDiffuse),"lighting cannot draw a retired source lease");
    auto plain_lease=std::make_shared<Lease>(Fixture());auto plain_asset=std::make_shared<GameModelAsset>();
    Check(bool(plain_asset->Decode(PackageStorage::Attach(plain_lease,plain_lease->b.size()))),"normal-free fixture admitted");
    GameModelClone plain;Check(bool(plain.Initialize(plain_asset)),"normal-free clone initialized");
    Check(graphics.Clear(0)&&bool(plain.Draw(graphics,ModelIdentity()))&&graphics.SwapBuffers(),"actual default lighting argument");
    GameGraphicsFrame original;Check(graphics.CopyFront(original),"default model frame readback");
    Check(graphics.Clear(0)&&bool(plain.Draw(graphics,ModelIdentity(),GameModelColorPolicy::SourceRgb,
        GameModelLightingPolicy::WindowsActorSoftDiffuse))&&graphics.SwapBuffers(),"normal-free actor policy draw");
    GameGraphicsFrame unchanged;Check(graphics.CopyFront(unchanged)&&original.pixels==unchanged.pixels,
        "absent source normals stay original without fabricated replacement normals");
}
void ContactQueriesAndDecal(){
    auto floor_lease=std::make_shared<Lease>(Fixture());auto floor_asset=std::make_shared<GameModelAsset>();
    Check(bool(floor_asset->Decode(PackageStorage::Attach(floor_lease,floor_lease->b.size()))),"contact floor fixture admitted");
    GameModelClone floor;Check(bool(floor.Initialize(floor_asset)),"contact floor clone");
    auto root=ModelIdentity();root[14]=2.5f;Check(bool(floor.Update(root)),"floor has actual nonzero support height");
    bool found=false;float height=0;
    Check(bool(floor.HorizontalSurfaceBelow(0,0,3,2,found,height))&&found&&Near(height,2.5f),"find actual posed floor rather than assume Z0");
    Check(bool(floor.HorizontalSurfaceBelow(3,0,3,2,found,height))&&!found,"outside floor has no support");
    Check(bool(floor.HorizontalSurfaceBelow(0,0,2,2,found,height))&&!found,"ceiling rejects surface above actor");
    floor.Pose()[0].opacity=.5f;Check(bool(floor.Update(root)),"translucent floor pose");
    Check(bool(floor.HorizontalSurfaceBelow(0,0,3,2,found,height))&&!found,"translucent receiver is not opaque ground");
    floor.Pose()[0].opacity=1;root=ModelIdentity();root[5]=root[10]=0;root[6]=1;root[9]=-1;
    Check(bool(floor.Update(root))&&bool(floor.HorizontalSurfaceBelow(0,0,3,4,found,height))&&!found,"vertical wall is not horizontal support");
    const std::vector<std::array<int16_t,3>> body{{{-20000,-20000,0}},{{20000,-20000,0}},{{0,20000,30000}},{{0,0,-32000}}};
    auto actor_lease=std::make_shared<Lease>(Fixture(false,&body));auto actor_asset=std::make_shared<GameModelAsset>();
    Check(bool(actor_asset->Decode(PackageStorage::Attach(actor_lease,actor_lease->b.size()))),"posed contact fixture includes unused low vertex");
    GameModelClone actor;Check(bool(actor.Initialize(actor_asset)),"contact actor clone");root=ModelIdentity();root[12]=4;root[13]=5;root[14]=6;
    Check(bool(actor.Update(root)),"contact actor translated pose");GameContactFootprint footprint;
    Check(bool(actor.ContactFootprint(footprint))&&footprint.valid&&Near(footprint.foot_z,6)&&Near(footprint.body_height,30000.f/32767),
          "bottom uses referenced posed triangles, ignores unused low vertex and works without normals");
    Check(Near(footprint.x,4)&&footprint.y<5&&footprint.radius_x>0&&footprint.radius_y>0,"footprint follows actual lower-envelope XY");
    for(uint8_t alpha:{uint8_t(0),uint8_t(128),uint8_t(255)}){
        ContactTextureSource source;source.alpha=alpha;
        auto image=std::make_shared<Lease>(Fixture(false,&body,true));auto textured=std::make_shared<GameModelAsset>();
        Check(bool(textured->Decode(PackageStorage::Attach(image,image->b.size()),&source)),"synthetic alpha-bound contact model");
        GameModelClone caster;Check(bool(caster.Initialize(textured)),"textured caster clone");GameContactFootprint patch;
        Check(bool(caster.ContactFootprint(patch))&&patch.valid==(alpha==255),"only fully opaque diffuse texture is eligible as contact caster");
        GameGraphics raster;GameGraphicsConfig c;c.width=c.height=c.stride=16;c.scissor_right=c.scissor_bottom=16;c.cull_face=false;
        Check(raster.Initialize(c)&&raster.Clear(0)&&bool(caster.Draw(raster,ModelIdentity())),"alpha caster actual model draw");
        Check((raster.Snapshot().mesh_pixels>0)==(alpha!=0),"zero-alpha diffuse texture draws no pixels and casts no contact");
    }
    actor_lease->live=false;Check(!actor.ContactFootprint(footprint),"retired actor cannot produce contact");
    floor_lease->live=false;Check(!floor.HorizontalSurfaceBelow(0,0,3,4,found,height),"retired floor cannot produce support");
    GameGraphics graphics;GameGraphicsConfig config;config.width=config.height=config.stride=16;
    config.scissor_right=config.scissor_bottom=16;config.cull_face=false;
    Check(graphics.Initialize(config)&&graphics.Clear(0xFF808080),"contact raster surface");
    const auto camera=ModelIdentity();
    Check(graphics.DrawContactEllipse(camera,0,0,0,.8f,.8f,.5f)&&graphics.SwapBuffers(),"empty-depth decal call");
    GameGraphicsFrame frame;Check(graphics.CopyFront(frame)&&frame.pixels[7*16+7]==0xFF808080,"no rendered receiver means no shadow");
    const auto triangle=[&](std::array<GameVector4,3> p,uint32_t color){std::array<GameMeshVertex,3> v{};
        for(unsigned i=0;i<3;++i){v[i].clip=p[i];v[i].color=color;}GameRasterPolicy policy;policy.two_sided=true;
        Check(graphics.DrawMeshTriangle(v,{},policy),"contact receiver/occluder triangle");};
    Check(graphics.Clear(0xFF000000),"contact receiver clear");
    triangle({{{{-1,-1,0,1}},{{1,-1,0,1}},{{1,1,0,1}}}},0xFF808080);
    triangle({{{{-1,-1,0,1}},{{1,1,0,1}},{{-1,1,0,1}}}},0xFF808080);
    triangle({{{{.1f,-.4f,-.5f,1}},{{.9f,-.4f,-.5f,1}},{{.5f,.5f,-.5f,1}}}},0xFFFF0000);
    Check(graphics.DrawContactEllipse(camera,0,0,.00002f,.8f,.8f,.5f),"single-coverage depth-matched procedural decal");
    triangle({{{{-1,-1,.00002f,1}},{{1,-1,.00002f,1}},{{0,1,.00002f,1}}}},0xFF00FF00);
    Check(graphics.SwapBuffers()&&graphics.CopyFront(frame),"contact raster actual readback");
    const auto centre=frame.pixels[7*16+7];
    Check((centre&255)<128&&(centre&255)>50,"soft contact darkens rendered floor exactly once");
    Check(frame.pixels[8*16+12]==0xFFFF0000,"nearer occluder is untouched by floor decal");
    Check(frame.pixels[8*16+7]!=0xFF00FF00,"contact did not overwrite receiver depth with slightly farther plane");
    Check(!graphics.DrawContactEllipse(camera,0,0,0,-1,1,.5f),"invalid contact extent refused");
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

void BackgroundModelMotionTests(){
    auto owner=std::make_shared<int>(1),foreign=std::make_shared<int>(2);
    auto records=std::make_shared<Lease>(std::vector<uint8_t>(60));
    for(unsigned i=0;i<3;++i){const auto name=std::string("MOTION_")+char('A'+i);std::copy(name.begin(),name.end(),records->b.begin()+i*20);}
    std::vector<std::pair<std::string,std::vector<uint8_t>>> entries{{"MOTION_A\\model01.DVS",Fixture()}};
    for(unsigned i=1;i<=4;++i)entries.push_back({"MOTION_A\\model0"+std::to_string(i)+".DAK",MotionFixture(i==4)});
    const auto file=BackgroundArchive({{"MOTION_A.BPM",BackgroundArchive(entries)}});
    for(int count:{0,2,4,-1,5}){
        GameBackgroundPreparation job;auto io=std::make_shared<BackgroundFileState>();
        auto counts=std::make_shared<Lease>(std::vector<uint8_t>{9,uint8_t(count),8,0,7,4});
        Check(bool(job.Begin(owner,1,{27,12,0,1},PackageStorage::Attach(records,60),std::make_unique<BackgroundFile>(file,io)))&&bool(job.Load(owner,1)),"model-motion real package stage");
        Check(!job.LoadModelMotions(foreign,1,PackageStorage::Attach(counts,6))&&!job.Snapshot().model_motions_staged,"foreign model-motion owner refuses");
        const auto status=job.LoadModelMotions(owner,1,PackageStorage::Attach(counts,6));const auto q=job.Snapshot();
        if(count>4)Check(!status&&q.failed&&!q.staged&&!q.model_motions_staged&&!q.source_committed,"count outside four source slots fails before template publication");
        else{
            Check(bool(status)&&q.model_motions_staged&&q.model_motions==uint32_t((std::max)(0,count))&&!q.source_committed,"source signed count, actual templates, no playback/scene ready");
            Check(q.model_motion_deferred==(count==4?1u:0u),"inactive deferred tracks preserved, never played by staging");
            Check(!job.LoadModelMotions(owner,1,PackageStorage::Attach(counts,6)),"template stage cannot replay");
            counts->live=false;std::vector<GameAreaMarkerState> markers;
            Check(!job.Snapshot().staged&&!job.AreaMarkers(owner,1,markers),"retired recipe no longer supplies current factory lineage");
        }
        Check(bool(job.Close())&&io->closes==1,"templates retire before backing file");
    }
    GameBackgroundPreparation job;auto io=std::make_shared<BackgroundFileState>();
    Check(bool(job.Begin(owner,1,{27,12,0,1},PackageStorage::Attach(records,60),std::make_unique<BackgroundFile>(file,io)))&&bool(job.Load(owner,1)),"missing recipe model staged");
    Check(!job.LoadModelMotions(owner,1,{})&&!job.Snapshot().model_motions_staged&&bool(job.Close()),"unknown recipe is not empty/default controls");
}
void BackgroundAuxiliaryTests(){
    // Entirely generated ASCII recipes/models/motions, not source table bytes.
    auto records=std::make_shared<Lease>(std::vector<uint8_t>(60));
    for(unsigned i=0;i<3;++i){const std::string n="AUX_"+std::to_string(i);std::copy(n.begin(),n.end(),records->b.begin()+i*20);}
    const auto view=[](const std::shared_ptr<Lease>& l){return PackageStorage::Attach(l,l->b.size());};
    const auto chars=[](std::string s){std::vector<uint8_t> b(s.begin(),s.end());b.push_back(0);return std::make_shared<Lease>(std::move(b));};
    auto format=chars("%s\\%s\\SYN_CHILD.DVS");
    std::array<std::shared_ptr<Lease>,3> texts{{chars("walk.dak"),chars("idle.dak"),chars("scene.dak")}};
    const GameAuxiliaryNameResolver resolve=[&](uint32_t token,PackageView& out)->GameFsStatus{
        if(token<101||token>103)return {GameFsCode::Unavailable,0,"synthetic unknown token"};
        out=view(texts[token-101]);return {};
    };
    auto recipe=std::make_shared<Lease>(std::vector<uint8_t>(36));
    Word(recipe->b,0,101);Word(recipe->b,4,102);Word(recipe->b,20,103); // row1 slot2 only
    const std::vector<std::array<int16_t,3>> quad{{{-20000,-20000,0}},{{20000,-20000,0}},{{20000,20000,0}},{{-20000,20000,0}}};
    const auto nested=BackgroundArchive({{"AUX_0\\model01.DVS",Fixture(false,&quad)},
        {"AUX_0\\SYN_CHILD.DVS",Fixture()},{"AUX_0\\walk.dak",MotionFixture()},
        {"AUX_0\\idle.dak",MotionFixture()},{"AUX_0\\scene.dak",MotionFixture()}});
    const auto file=BackgroundArchive({{"AUX_0.BPM",nested}});
    auto owner=std::make_shared<int>(31),foreign=std::make_shared<int>(32);
    GameBackgroundPreparation job;auto io=std::make_shared<BackgroundFileState>();uint64_t generation=0;
    const auto begin=[&](const std::vector<uint8_t>& bytes){++generation;io=std::make_shared<BackgroundFileState>();
        Check(bool(job.Begin(owner,generation,{47,12,0,generation},view(records),std::make_unique<BackgroundFile>(bytes,io)))&&bool(job.Load(owner,generation)),"auxiliary initialized real main owner");};
    begin(file);const auto reads=io->reads;
    std::vector<GameAuxiliaryBindingState> bindings;
    Check(!job.LoadAuxiliary(foreign,generation,view(recipe),view(format),resolve)
          &&!job.LoadAuxiliary(owner,generation+1,view(recipe),view(format),resolve)&&io->reads==reads,"foreign/stale auxiliary request no load");
    Check(bool(job.LoadAuxiliary(owner,generation,view(recipe),view(format),resolve)),"actual triplet/model/DAK typed owner construction");
    auto state=job.Snapshot();Check(state.auxiliary_staged&&state.auxiliary_entries==2&&state.auxiliary_children==1
        &&state.auxiliary_templates==3&&state.auxiliary_bindings==4&&!state.source_committed,"slot2 nonzero continues5070; distinct real bindings not ready");
    Check(bool(job.Auxiliary(owner,generation,bindings))&&bindings.size()==4,"current copied binding readback");
    for(const auto& b:bindings){Check(b.playback.state==(b.target==GameAuxiliaryTarget::MainTemplate?1u:3u)&&b.playback.time==0&&b.playback.samples==0
        &&b.playback.speed==4800&&b.playback.stop_limit==-1&&b.playback.duration==160,"88728/89954 initialization without sampling/selection");
        Check(b.model.vertices==(b.target==GameAuxiliaryTarget::Child?3u:4u),"DAK owns correct child/template/visible-clone target");}
    Check(bindings[2].target==GameAuxiliaryTarget::MainTemplate&&bindings[3].target==GameAuxiliaryTarget::MainClone,"independent template and visible clone controls");
    GameGraphics graphics;Check(graphics.Initialize({})&&graphics.Clear(0xFF000000)
        &&bool(job.DrawAuxiliary(owner,generation,0,graphics,ModelIdentity()))&&graphics.Snapshot().mesh_pixels>0,"actual auxiliary child offscreen draw");
    Check(!job.DrawAuxiliary(owner,generation,1,graphics,ModelIdentity())&&!job.LoadAuxiliary(owner,generation,view(recipe),view(format),resolve),"no fabricated child and no load replay");
    Check(bool(job.Auxiliary(owner,generation,bindings))&&bindings[0].playback.samples==0,"preview leaves controls/selected state unchanged");
    texts[0]->b[0]='W';bindings[0].entry=99;
    Check(!job.Auxiliary(owner,generation,bindings)&&bindings[0].entry==99&&!job.Snapshot().auxiliary_staged
        &&!job.Draw(owner,generation,graphics,ModelIdentity()),"name/recipe drift rejects current bindings without rebasing output");texts[0]->b[0]='w';
    io->fail_close=true;Check(!job.Close()&&io->live&&!job.Snapshot().auxiliary_staged,"failed backing retirement keeps owner for retry");
    Check(!job.Begin(owner,generation+1,{47,12,0,generation+1},view(records),std::make_unique<BackgroundFile>(file,io)),"failed close prevents fresh reuse");
    io->fail_close=false;Check(bool(job.Close())&&!io->live,"auxiliary controls/templates retire before file close retry");
    Check(!job.Auxiliary(owner,generation,bindings),"retired owner cannot read prior controls");
    // Each malformed/unknown input is a fresh real request, never an empty list.
    for(unsigned failure=0;failure<8;++failure){
        begin(file);auto bad=std::make_shared<Lease>(recipe->b);PackageView r=view(bad),f=view(format);
        GameAuxiliaryNameResolver resolver=resolve;
        if(failure==0)r={};
        if(failure==1){bad->b.resize(24);bad->known.resize(24);r=view(bad);} // missing terminator
        if(failure==2){bad->b.resize(48);bad->known.resize(48,1);r=view(bad);} // bytes after sentinel
        if(failure==3)resolver={};
        if(failure==4){bad->known[4]=0;}
        if(failure==5){Word(bad->b,0,0);} // slot1 has no child producer
        if(failure==6){texts[0]->b[0]='/';}
        if(failure==7)f={};
        Check(!job.LoadAuxiliary(owner,generation,r,f,resolver)&&job.Snapshot().failed
              &&!job.Snapshot().auxiliary_staged&&!job.Snapshot().source_committed,"malformed/unknown auxiliary recipe never publishes");
        texts[0]->b[0]='w';Check(bool(job.Close()),"bad recipe teardown");
    }
    const auto missing=BackgroundArchive({{"AUX_0.BPM",BackgroundArchive({{"AUX_0\\model01.DVS",Fixture()}})}});
    begin(missing);Check(!job.LoadAuxiliary(owner,generation,view(recipe),view(format),resolve)
        &&job.Snapshot().failed&&!job.Snapshot().staged,"missing mandatory child model not optional success");Check(bool(job.Close()),"failed-load cleanup");
    begin(file);Check(bool(job.LoadAuxiliary(owner,generation,view(recipe),view(format),resolve)),"fresh epoch after failed load retries real construction");
    recipe->live=false;Check(!job.Snapshot().staged&&!job.Auxiliary(owner,generation,bindings),"retired recipe prevents live readback");
    recipe->live=true;owner.reset();Check(!job.Auxiliary(foreign,generation,bindings)&&!job.Snapshot().live,"expired owning control block refuses");
    Check(bool(job.Close()),"final auxiliary teardown");
    owner=std::make_shared<int>(33);begin(file);auto empty=std::make_shared<Lease>(std::vector<uint8_t>(12));
    const auto before=io->reads;Check(bool(job.LoadAuxiliary(owner,generation,view(empty),{},{}))
        &&job.Snapshot().auxiliary_staged&&!job.Snapshot().auxiliary_entries&&io->reads==before,"actual all-zero terminator is the only empty source recipe branch");
    Check(bool(job.Close()),"empty recipe owner retires");graphics.Close();
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
        AreaMarkerTests();
        BackgroundModelMotionTests();
        BackgroundAuxiliaryTests();
        BackgroundPreparationTests();
        MathAndSkin();
        GeometryAndRaster();
        TexturedColorApproximation();
        NormalLighting();
        ContactQueriesAndDecal();
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
