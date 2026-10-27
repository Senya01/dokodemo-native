#include "game_model_native.h"
#include "core_hash.h"
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
GameVector4 UnitNormal(GameVector4 normal) {
    const auto length2=normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2];
    Need(std::isfinite(length2)&&length2>1e-12f,"invalid model normal");
    const auto scale=1/std::sqrt(length2);
    for(unsigned i=0;i<3;++i)normal[i]*=scale;
    normal[3]=0;
    return normal;
}
} // namespace

namespace {
std::string BackgroundRegistration(const PackageView& records,uint32_t variant) {
    Need(records.Describe().size==60 && variant<3,"background registration extent/variant");
    std::string result;
    for(uint32_t i=0;i<20;++i){
        const auto c=records.ReadU8(variant*20+i);
        if(!c){
            Need(!result.empty(),"empty background registration");
            for(++i;i<20;++i)Need(records.ReadU8(variant*20+i)==0,"background registration padding");
            return result;
        }
        Need((c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_',"unsafe background registration");
        result+=char(c);
    }
    throw std::runtime_error("unterminated background registration");
}
class BackgroundPackageSource final:public GameResourceSource {
  public:
    BackgroundPackageSource(PackageView package,std::string mount)
        :directory_(BpDirectory::Parse(package,package.Describe().size)),mount_(std::move(mount)+"\\"){}
    GameFsStatus OpenResource(const std::string& path,PackageView& out)override {
        try{
            Need(path.compare(0,mount_.size(),mount_)==0,"background mount mismatch");
            out=directory_.Open(path.substr(mount_.size()));return {};
        }catch(const std::exception& e){return Error(e);}
    }
  private:
    BpDirectory directory_;
    std::string mount_;
};
}
bool GameBackgroundPreparation::Current(const std::shared_ptr<const void>& owner,uint64_t epoch)const {
    const auto current=owner_.lock();
    return owner&&current&&state_.live&&epoch&&epoch==state_.epoch&&current==owner
        &&!owner.owner_before(current)&&!current.owner_before(owner);
}
bool GameBackgroundPreparation::RegistrationsCurrent()const {
    try{return registrations_.Describe().size==60&&registrations_.InitializedDigest()==registrations_digest_;}
    catch(...){return false;}
}
bool GameBackgroundPreparation::MotionCountsCurrent()const {
    if(!state_.model_motions_staged)return true;
    try{return model_motion_counts_.Describe().size==6&&model_motion_counts_.InitializedDigest()==model_motion_counts_digest_;}
    catch(...){return false;}
}
bool GameBackgroundPreparation::AuxiliaryCurrent()const {
    if(!state_.auxiliary_staged)return true;
    try{
        for(const auto& input:auxiliary_inputs_)
            if(input.view.InitializedDigest()!=input.digest)return false;
        if(!auxiliary_entries_.empty()&&(!auxiliary_asset_||!auxiliary_asset_->Live()))return false;
        for(const auto& entry:auxiliary_entries_)for(const auto& motion:entry.motions)
            if(motion&&!motion->Live())return false;
        return true;
    }catch(...){return false;}
}
GameFsStatus GameBackgroundPreparation::Begin(std::shared_ptr<const void> owner,uint64_t epoch,
        const GameBackgroundCandidate& candidate,PackageView records,std::unique_ptr<GameFileSource> source,GameModelTransformPolicy policy) {
    if(state_.live||state_.failed||!owner||epoch<=last_epoch_||candidate.request<=last_request_
       ||candidate.selector!=12||candidate.period>=6||!source
       ||(policy!=GameModelTransformPolicy::SourceCommon&&policy!=GameModelTransformPolicy::WindowsAreaMarkerCanonicalBasis))
        return {GameFsCode::Unavailable,0,"background preparation owner/request/selected domain"};
    try{
        // 4848: source period->variant; no date-derived/default variant.
        constexpr uint32_t variants[]{0,1,1,2,0,0};
        const auto variant=variants[candidate.period];
        const auto name=BackgroundRegistration(records,variant);
        const auto digest=records.InitializedDigest();
        // A restarted epoch must follow explicit retirement; this object owns
        // both file and buffers even if Mount fails part way through.
        state_={};state_.live=true;state_.pending=true;state_.epoch=epoch;
        state_.request=candidate.request;state_.candidate=candidate.id;
        state_.transform_policy=policy;
        state_.selector=candidate.selector;state_.period=candidate.period;
        state_.variant=variant;state_.resource=30+3*candidate.selector+variant;
        state_.registration=name;owner_=owner;registrations_=std::move(records);registrations_digest_=digest;
        last_epoch_=epoch;last_request_=candidate.request;
        const auto status=filesystem_.Mount(std::move(source));
        if(!status){state_.failed=true;state_.pending=false;state_.status=status;return status;}
        return {};
    }catch(const std::exception& e){if(state_.live){state_.failed=true;state_.pending=false;state_.status=Error(e);}return Error(e);}
}
GameFsStatus GameBackgroundPreparation::Load(const std::shared_ptr<const void>& owner,uint64_t epoch) {
    if(!Current(owner,epoch)||!state_.pending||state_.staged||state_.failed)
        return {GameFsCode::Unavailable,0,"background pending owner/epoch/registration changed"};
    if(!RegistrationsCurrent()){
        state_.failed=true;state_.pending=false;
        state_.status={GameFsCode::Unavailable,0,"background registration retired/changed"};return state_.status;
    }
    try{
        // This is an actual separately owned file read, not request-bit ready.
        auto status=filesystem_.LoadMember(state_.registration+".BPM",package_);
        if(!status){state_.failed=true;state_.pending=false;state_.status=status;return status;}
        const auto span=package_.Describe();Need(span.readable,"background package readback");
        state_.package_read=true;state_.package_owner=span.owner;
        state_.package_generation=span.generation;state_.package_serial=span.serial;
        BackgroundPackageSource resources(package_,state_.registration);
        auto asset=std::make_shared<GameModelAsset>();
        const auto prefix=state_.registration+"\\"+state_.registration+"\\";
        status=asset->Load(resources,prefix+"model01.DVS",prefix+"gim\\",state_.transform_policy);
        if(!status){state_.failed=true;state_.pending=false;state_.status=status;return status;}
        GameModelClone clone;status=clone.Initialize(asset);
        if(!status){state_.failed=true;state_.pending=false;state_.status=status;return status;}
        status=clone.Update(ModelIdentity());
        if(!status){state_.failed=true;state_.pending=false;state_.status=status;return status;}
        std::vector<GameAreaMarkerState> markers;
        status=clone.AreaMarkers(*asset,markers);
        if(!status){state_.failed=true;state_.pending=false;state_.status=status;return status;}
        Need(markers.size()==asset->Statistics().area_markers,"background marker-list readback");
        Need(Current(owner,epoch)&&RegistrationsCurrent()&&asset->Live(),"background load owner drift");
        // Only native staging is published. No source4DC,4A8,manager14 or
        // actor/context effects are admitted by successful model construction.
        state_.model=asset->Statistics();asset_=std::move(asset);clone_=std::move(clone);
        state_.pending=false;state_.staged=true;return {};
    }catch(const std::exception& e){state_.failed=true;state_.pending=false;state_.status=Error(e);return state_.status;}
}
GameFsStatus GameBackgroundPreparation::Draw(const std::shared_ptr<const void>& owner,uint64_t epoch,
        GameGraphics& graphics,const GameMatrix& camera)const {
    if(!Current(owner,epoch)||!state_.staged||state_.failed||!RegistrationsCurrent()||!MotionCountsCurrent()||!AuxiliaryCurrent()||!asset_||!asset_->Live())
        return {GameFsCode::Unavailable,0,"background staged draw owner retired/changed"};
    // Explicit component/offline view, never an ordinary game scene publish.
    return clone_.Draw(graphics,camera,GameModelColorPolicy::WindowsDvsTexturedModulation2x);
}
GameFsStatus GameBackgroundPreparation::AreaMarkers(const std::shared_ptr<const void>& owner,uint64_t epoch,
        std::vector<GameAreaMarkerState>& out)const {
    if(!Current(owner,epoch)||!state_.staged||state_.failed||!RegistrationsCurrent()||!MotionCountsCurrent()||!AuxiliaryCurrent()||!asset_)
        return {GameFsCode::Unavailable,0,"background marker-list owner unavailable"};
    return clone_.AreaMarkers(*asset_,out);
}
GameFsStatus GameBackgroundPreparation::LoadModelMotions(const std::shared_ptr<const void>& owner,uint64_t epoch,
        PackageView counts) {
    if(!Current(owner,epoch)||!state_.staged||state_.failed||state_.model_motions_staged||!RegistrationsCurrent()||!asset_||!asset_->Live())
        return {GameFsCode::Unavailable,0,"background model-motion owner unavailable"};
    try{
        Need(counts.Describe().size==6,"background model-motion count extent");
        const auto digest=counts.InitializedDigest();const auto raw=counts.ReadU8(state_.variant*2+1);
        const auto count=raw<128?int(raw):int(raw)-256;
        // Source allocates four handle/template slots. Refuse larger values
        // before loading instead of writing past that selected finite domain.
        Need(count<=4,"unimplemented background model-motion count");
        BackgroundPackageSource resources(package_,state_.registration);
        std::vector<GameModelMotion> motions;uint32_t deferred=0;
        for(int i=0;i<count;++i){
            GameModelMotion motion;
            const auto path=state_.registration+"\\"+state_.registration+"\\model0"+char('1'+i)+".DAK";
            const auto status=motion.Load(resources,path,*asset_);
            if(!status){state_.failed=true;state_.staged=false;state_.status=status;return status;}
            deferred+=motion.Statistics().deferred_sections;motions.push_back(std::move(motion));
        }
        Need(Current(owner,epoch)&&RegistrationsCurrent()&&counts.InitializedDigest()==digest,"background model-motion recipe drift");
        model_motion_counts_=std::move(counts);model_motion_counts_digest_=digest;
        model_motions_=std::move(motions);state_.model_motions=uint32_t(model_motions_.size());
        state_.model_motion_deferred=deferred;state_.model_motions_staged=true;return {};
    }catch(const std::exception& e){state_.failed=true;state_.staged=false;state_.status=Error(e);return state_.status;}
}
GameFsStatus GameBackgroundPreparation::LoadAuxiliary(const std::shared_ptr<const void>& owner,uint64_t epoch,
        PackageView triplets,PackageView format,const GameAuxiliaryNameResolver& resolve) {
    if(!Current(owner,epoch)||!state_.staged||state_.failed||state_.auxiliary_staged
       ||!RegistrationsCurrent()||!MotionCountsCurrent()||!asset_||!asset_->Live())
        return {GameFsCode::Unavailable,0,"background auxiliary owner unavailable"};
    try{
        std::vector<AuxiliaryInput> inputs;
        const auto remember=[&](const PackageView& view){inputs.push_back({view,view.InitializedDigest()});};
        const auto text=[&](const PackageView& view){
            const auto size=view.Describe().size;Need(size>1&&size<=96,"auxiliary text extent");
            remember(view);std::string out;
            for(uint64_t i=0;i<size;++i){const auto c=view.ReadU8(i);
                if(!c){Need(i+1==size,"auxiliary text trailing bytes");return out;}
                Need(c>=32&&c<127,"auxiliary text encoding");out+=char(c);
            }
            throw std::runtime_error("unterminated auxiliary text");
        };
        const auto filename=[](const std::string& name){
            Need(!name.empty()&&name.size()<=64&&name!="."&&name!="..","auxiliary filename extent");
            for(auto c:name)Need((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')
                                ||c=='_'||c=='-'||c=='.',"unsafe auxiliary filename");
        };
        const auto bytes=triplets.Describe().size;
        // Native bounded capacity, not a recovered PSP capacity.
        Need(bytes>=12&&bytes<=33*12&&bytes%12==0,"auxiliary triplet extent");remember(triplets);
        std::vector<AuxiliaryEntry> entries;
        std::vector<std::array<std::string,3>> names;
        bool terminated=false;
        for(uint64_t at=0;at<bytes;at+=12){
            AuxiliaryEntry entry;
            for(unsigned slot=0;slot<3;++slot)entry.words[slot]=triplets.ReadLE32(at+slot*4);
            if(entry.words==std::array<uint32_t,3>{}){
                Need(at+12==bytes,"auxiliary entries after terminator");terminated=true;break;
            }
            Need(bool(resolve),"auxiliary name resolver unavailable");
            std::array<std::string,3> row;
            for(unsigned slot=0;slot<3;++slot)if(entry.words[slot]){
                PackageView view;const auto status=resolve(entry.words[slot],view);
                if(!status){state_.failed=true;state_.staged=false;state_.status=status;return status;}
                row[slot]=text(view);filename(row[slot]);
            }
            Need(!entry.words[1]||entry.words[0],"unimplemented auxiliary slot1 without child producer");
            entries.push_back(std::move(entry));names.push_back(std::move(row));
        }
        Need(terminated,"auxiliary triplet terminator missing");
        std::shared_ptr<GameModelAsset> auxiliary;
        GameModelClone main_template;
        std::vector<AuxiliaryBinding> bindings;
        uint32_t templates=0,children=0;
        if(!entries.empty()){
            const auto model_format=text(format);
            const std::string prefix_format="%s\\%s\\";
            Need(model_format.compare(0,prefix_format.size(),prefix_format)==0,"auxiliary model path format");
            const auto model_name=model_format.substr(prefix_format.size());filename(model_name);
            BackgroundPackageSource resources(package_,state_.registration);
            const auto prefix=state_.registration+"\\"+state_.registration+"\\";
            auxiliary=std::make_shared<GameModelAsset>();
            auto status=auxiliary->Load(resources,prefix+model_name,prefix+"gim\\",state_.transform_policy);
            if(!status){state_.failed=true;state_.staged=false;state_.status=status;return status;}
            for(size_t i=0;i<entries.size();++i){auto& entry=entries[i];
                for(unsigned slot=0;slot<3;++slot)if(entry.words[slot]){
                    auto motion=std::make_unique<GameModelMotion>();
                    // 4A0C parses templates; Windows validates the later635C
                    // target eagerly. Source random choice/5BFC is not issued.
                    status=motion->Load(resources,prefix+names[i][slot],slot==2?*asset_:*auxiliary);
                    if(!status){state_.failed=true;state_.staged=false;state_.status=status;return status;}
                    entry.motions[slot]=std::move(motion);++templates;
                }
                if(entry.words[0]){
                    entry.child=std::make_unique<GameModelClone>();status=entry.child->Initialize(auxiliary);
                    if(status)status=entry.child->Update(ModelIdentity());
                    if(!status){state_.failed=true;state_.staged=false;state_.status=status;return status;}++children;
                }
                for(unsigned slot=0;slot<3;++slot)if(entry.motions[slot]){
                    const auto bind=[&](GameAuxiliaryTarget target){
                        AuxiliaryBinding b;b.entry=uint32_t(i);b.slot=slot;b.target=target;
                        const auto stats=entry.motions[slot]->Statistics();b.playback.Configure(stats.duration,stats.loop);
                        // 88728/89954 leaves state1. Only635C's child/visible
                        // controls receive explicit state3; template stays1.
                        if(target!=GameAuxiliaryTarget::MainTemplate)b.playback.Start(0);
                        bindings.push_back(std::move(b));
                    };
                    if(slot<2)bind(GameAuxiliaryTarget::Child);
                    else{
                        if(!main_template.Asset()){
                            status=main_template.Initialize(asset_);if(status)status=main_template.Update(ModelIdentity());
                            if(!status){state_.failed=true;state_.staged=false;state_.status=status;return status;}
                        }
                        bind(GameAuxiliaryTarget::MainTemplate);bind(GameAuxiliaryTarget::MainClone);
                    }
                }
            }
        }
        Need(Current(owner,epoch)&&RegistrationsCurrent()&&MotionCountsCurrent(),"auxiliary owner drift");
        for(const auto& input:inputs)Need(input.view.InitializedDigest()==input.digest,"auxiliary recipe/name drift");
        auxiliary_inputs_=std::move(inputs);auxiliary_entries_=std::move(entries);
        auxiliary_bindings_=std::move(bindings);auxiliary_asset_=std::move(auxiliary);main_template_clone_=std::move(main_template);
        state_.auxiliary_entries=uint32_t(auxiliary_entries_.size());state_.auxiliary_children=children;
        state_.auxiliary_templates=templates;state_.auxiliary_bindings=uint32_t(auxiliary_bindings_.size());
        if(auxiliary_asset_)state_.auxiliary_model=auxiliary_asset_->Statistics();
        state_.auxiliary_staged=true;return {};
    }catch(const std::exception& e){state_.failed=true;state_.staged=false;state_.status=Error(e);return state_.status;}
}
GameFsStatus GameBackgroundPreparation::Auxiliary(const std::shared_ptr<const void>& owner,uint64_t epoch,
        std::vector<GameAuxiliaryBindingState>& out)const {
    if(!Current(owner,epoch)||state_.failed||!state_.staged||!state_.auxiliary_staged
       ||!RegistrationsCurrent()||!MotionCountsCurrent()||!AuxiliaryCurrent()||!asset_||!asset_->Live())
        return {GameFsCode::Unavailable,0,"background auxiliary readback unavailable"};
    std::vector<GameAuxiliaryBindingState> result;
    for(const auto& b:auxiliary_bindings_){
        const auto& entry=auxiliary_entries_.at(b.entry);
        const auto* target=b.target==GameAuxiliaryTarget::Child?entry.child.get():
                           b.target==GameAuxiliaryTarget::MainTemplate?&main_template_clone_:&clone_;
        if(!target||!target->Asset()||!target->Asset()->Live())
            return {GameFsCode::Unavailable,0,"background auxiliary target retired"};
        result.push_back({b.entry,b.slot,entry.words[b.slot],b.target,b.playback.Snapshot(),
                          entry.motions[b.slot]->Statistics(),target->Asset()->Statistics()});
    }
    out=std::move(result);return {};
}
GameFsStatus GameBackgroundPreparation::DrawAuxiliary(const std::shared_ptr<const void>& owner,uint64_t epoch,
        uint32_t entry,GameGraphics& graphics,const GameMatrix& camera)const {
    std::vector<GameAuxiliaryBindingState> readback;
    const auto status=Auxiliary(owner,epoch,readback);if(!status)return status;
    if(entry>=auxiliary_entries_.size()||!auxiliary_entries_[entry].child)
        return {GameFsCode::Unavailable,0,"background auxiliary child not present"};
    return auxiliary_entries_[entry].child->Draw(graphics,camera,GameModelColorPolicy::WindowsDvsTexturedModulation2x);
}
GameBackgroundPreparationSnapshot GameBackgroundPreparation::Snapshot()const {
    auto out=state_;
    if(owner_.expired()||!RegistrationsCurrent()||!MotionCountsCurrent()||!AuxiliaryCurrent()||(state_.staged&&(!asset_||!asset_->Live())))
        out.live=out.pending=out.staged=out.model_motions_staged=out.auxiliary_staged=false;
    return out;
}
GameFsStatus GameBackgroundPreparation::Close() {
    // Clone/template/material views die before their backing/file is revoked.
    auxiliary_bindings_.clear();
    for(auto& entry:auxiliary_entries_)if(entry.child)entry.child->Close();
    main_template_clone_.Close();auxiliary_entries_.clear();auxiliary_asset_.reset();auxiliary_inputs_.clear();
    state_.auxiliary_staged=false;
    model_motions_.clear();model_motion_counts_.Reset();
    clone_.Close();asset_.reset();package_.Reset();
    auto status=filesystem_.Close();
    state_.live=state_.pending=state_.staged=false;
    if(!status){state_.failed=true;state_.status=status;return status;}
    registrations_.Reset();owner_.reset();state_={};return {};
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
GameVector4 ModelTransformNormal(const GameMatrix &m,const GameVector4 &n) {
    const auto a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    const auto c00=e*i-f*h,c01=f*g-d*i,c02=d*h-e*g;
    const auto determinant=a*c00+b*c01+c*c02;
    Need(std::isfinite(determinant)&&std::abs(determinant)>1e-12f,"singular model normal transform");
    const GameVector4 out{{(c00*n[0]+c01*n[1]+c02*n[2])/determinant,
        ((c*h-b*i)*n[0]+(a*i-c*g)*n[1]+(b*g-a*h)*n[2])/determinant,
        ((b*f-c*e)*n[0]+(c*d-a*f)*n[1]+(a*e-b*d)*n[2])/determinant,0}};
    Need(std::isfinite(out[0])&&std::isfinite(out[1])&&std::isfinite(out[2]),"nonfinite model normal transform");
    return out;
}
uint32_t ModelWindowsActorSoftDiffuse(uint32_t color,const GameVector4 &world_normal) {
    const auto n=UnitNormal(world_normal);
    // Explicit functional Windows approximation, Z-up world, front/left/up
    // key and soft ambient. Source type5 light semantics are not inferred.
    const auto key=UnitNormal({{-.3f,-.7f,.65f,0}});
    const auto cosine=(std::clamp)(n[0]*key[0]+n[1]*key[1]+n[2]*key[2],0.0f,1.0f);
    const auto shade=.72f+.28f*cosine;
    uint32_t out=color&0xFF000000;
    for(unsigned shift=0;shift<24;shift+=8)
        out|=uint32_t(std::lround(float((color>>shift)&255)*shade))<<shift;
    return out;
}
GameFsStatus GameModelAsset::Load(GameResourceSource &source, const std::string &path,
                                  const std::string &prefix,GameModelTransformPolicy policy) {
    PackageView image;
    auto status = source.OpenResource(path, image);
    return status ? Decode(std::move(image), &source, prefix,policy) : status;
}
GameFsStatus GameModelAsset::Decode(PackageView image, GameResourceSource *source,
                                    const std::string &prefix,GameModelTransformPolicy policy) {
    if (Live())
        return {GameFsCode::Busy, 0, "model asset already live"};
    try {
        Need(policy==GameModelTransformPolicy::SourceCommon||policy==GameModelTransformPolicy::WindowsAreaMarkerCanonicalBasis,
             "unimplemented model transform policy");
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
                     (n.type == 0 || n.type == 1 || n.type == 3 || n.type == 4 || n.type == 5 || n.type==12 || n.type==13),
                 "unimplemented model node type");
            if(n.type==13){
                Need(n.hasCommon&&n.typePayload.size()==12,"area-marker descriptor extent/common");
                Need(n.uninterpreted.bytes==0,"unimplemented area-marker trailing payload");
                Need(!(n.commonFlags&~3u),"unimplemented area-marker common flags");
                Need(!(n.commonFlags&2)||policy==GameModelTransformPolicy::WindowsAreaMarkerCanonicalBasis,
                     "unimplemented source area-marker flag2 basis initializer");
                ++stats.area_markers;
            }
            if(n.type==12){
                Need(n.typePayload.size()==12,"type12 region descriptor extent");
                ++stats.particle_emitters;
                //7F0B8/80C14:owned four-field descriptor+common transform.
                //8A394 updates hierarchy;836F8/26B80 now supplies the reached
                //region selector/shape consumer. No emission or own triangles.
            }
            if (n.hasCommon) {
                Need(n.commonShorts[1] >= -1 && n.commonShorts[1] < int32_t(stats.nodes),
                     "model parent index");
                Need(n.type==13||!(n.commonFlags & 2), "unimplemented oriented-scale common transform");
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
                for(const auto& bone:g.bones){
                    auto parent=int32_t(bone.idWord&65535);size_t depth=0;
                    while(parent>=0){
                        Need(size_t(parent)<parsed.nodes.size()&&++depth<=parsed.nodes.size(),"skin parent hierarchy");
                        const auto& ancestor=parsed.nodes[size_t(parent)];
                        Need(ancestor.type!=13,"unimplemented skin binding through area marker");
                        parent=ancestor.hasCommon?ancestor.commonShorts[1]:-1;
                    }
                }
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
        transform_policy_=policy;
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
        p.material = node.hasGeometry ? node.geometry.materialIndex :
            node.type==13&&node.areaMarker.material!=255?node.areaMarker.material:-1;
        if(node.type==13){p.scale=node.commonVectors[3];p.scale_orientation=node.commonVectors[2];}
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
    marker_pose_current_=false;
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
            auto local = ModelTRS(pose.position, pose.base_rotation, pose.rotation, pose.scale);
            if(node.type==13){
                // 8C684 multiply order; opted Windows policy supplies native
                // canonical axes. PSP NOBITS basis/current values UNKNOWN.
                // Existing native quaternion normalization remains explicit.
                local=ModelTRS(pose.position,pose.base_rotation,pose.rotation,{{1,1,1,1}});
                if(node.commonFlags&2){
                    const auto orientation=ModelQuaternion(pose.scale_orientation);
                    auto scale=ModelIdentity();
                    for(unsigned axis=0;axis<3;++axis){
                        Need(std::isfinite(pose.scale[axis]),"nonfinite area-marker scale");
                        scale[axis*5]=pose.scale[axis];
                    }
                    local=ModelMultiply(local,ModelMultiply(orientation,ModelMultiply(scale,ModelInverseRigid(orientation))));
                }
            }
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
            if(node.type==13)
                for(const auto value:pose.world)Need(std::isfinite(value),"nonfinite area-marker world matrix");
            visited[i] = 2;
        };
        for (size_t i = 0; i < pose_.size(); ++i)
            update(i);
        if(asset_->Statistics().area_markers)marker_pose_digest_=MarkerPoseDigest();
        marker_pose_current_=true;
        return {};
    } catch (const std::exception &e) {
        return Error(e);
    }
}
std::array<uint8_t,32> GameModelClone::MarkerPoseDigest()const {
    core::Sha256 hash;
    const auto bytes=[&](const void* p,size_t n){hash.update(static_cast<const uint8_t*>(p),n);};
    for(const auto& p:pose_){
        for(const auto* v:{&p.position,&p.base_rotation,&p.rotation,&p.scale,&p.scale_orientation})
            bytes(v->data(),v->size()*sizeof(float));
        bytes(p.world.data(),p.world.size()*sizeof(float));
        bytes(&p.material,sizeof(p.material));bytes(&p.opacity,sizeof(p.opacity));bytes(&p.world_opacity,sizeof(p.world_opacity));
    }
    return hash.digest();
}
GameFsStatus GameModelClone::AreaMarkers(const GameModelAsset& expected,std::vector<GameAreaMarkerState>& out)const {
    if(!asset_||asset_.get()!=&expected||!asset_->Live()||!marker_pose_current_)
        return {GameFsCode::Unavailable,0,"area-marker template/lease unavailable"};
    try{
        Need(pose_.size()==asset_->Data().nodes.size(),"area-marker clone table extent");
        if(asset_->Statistics().area_markers)
            Need(MarkerPoseDigest()==marker_pose_digest_,"area-marker pose changed after update");
        std::vector<GameAreaMarkerState> current;
        for(size_t i=0;i<pose_.size();++i){
            const auto& node=asset_->Data().nodes[i];if(node.type!=13)continue;
            const auto& p=pose_[i];
            Need(p.material==(node.areaMarker.material==255?-1:node.areaMarker.material),
                 "unimplemented area-marker material rebinding");
            GameAreaMarkerState marker;marker.node=uint32_t(i);marker.parent=node.commonShorts[1];
            marker.material=p.material;marker.parameters=node.areaMarker;
            marker.world=p.world;marker.final_matrix=p.world;marker.world_opacity=p.world_opacity;
            marker.transform_policy=asset_->TransformPolicy();
            if(!node.optionalMatrix.empty()){
                Need(node.optionalMatrix.size()==16,"area-marker optional matrix extent");
                GameMatrix optional;std::copy(node.optionalMatrix.begin(),node.optionalMatrix.end(),optional.begin());
                marker.final_matrix=ModelMultiply(p.world,optional);
            }
            for(const auto value:marker.final_matrix)Need(std::isfinite(value),"nonfinite area-marker final matrix");
            Need(std::isfinite(marker.world_opacity),"nonfinite area-marker inherited opacity");
            current.push_back(marker);
        }
        out=std::move(current);return {};
    }catch(const std::exception& e){return Error(e);}
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
GameVector4 GameModelClone::SkinNormal(const model::Geometry &g,uint32_t vertex,
                                      const std::vector<GameMatrix> &palette) {
    Need(vertex<g.normals.size()&&g.normals.size()==g.positions.size()&&
         g.weightRecords.size()==g.positions.size()*24,"skin normal bounds");
    const auto at=size_t(vertex)*24;
    const auto index=[&](unsigned slot){const auto p=at+slot*2;
        return int16_t(uint16_t(g.weightRecords[p])|uint16_t(g.weightRecords[p+1])<<8);};
    const auto &raw=g.normals[vertex];
    const GameVector4 normal{{float(raw[0]),float(raw[1]),float(raw[2]),0}};
    const auto rigid=index(1)<0;
    GameVector4 out{};
    for(unsigned slot=0;slot<(rigid?1u:4u);++slot){
        const auto bone=index(slot);if(bone<0)break;
        Need(size_t(bone)<palette.size(),"skin normal palette index");
        const auto weight=rigid?1.0f:FloatWord(Word(g.weightRecords,at+8+slot*4));
        Need(weight>=0&&weight<=1.0001f,"skin normal weight domain");
        const auto value=ModelTransformNormal(palette[size_t(bone)],normal);
        for(unsigned j=0;j<3;++j)out[j]+=value[j]*weight;
    }
    Need(index(0)>=0,"skin normal first palette index");
    return UnitNormal(out);
}
GameModelClone::GeometrySpace GameModelClone::GeometryTransforms(uint32_t node) const {
    Need(asset_&&asset_->Live()&&node<pose_.size(),"world geometry source lease/index");
    const auto &n=asset_->Data().nodes.at(node);const auto &g=n.geometry;
    GeometrySpace space{pose_[node].world,{}};
    for(const auto &b:g.bones){const auto bone=b.idWord&65535;
        Need(bone<pose_.size()&&bone<inverse_bind_.size(),"world geometry palette index");
        space.palette.push_back(ModelMultiply(pose_[bone].world,inverse_bind_[bone]));}
    if(n.optionalMatrix.size()==16){GameMatrix optional{};
        std::copy(n.optionalMatrix.begin(),n.optionalMatrix.end(),optional.begin());
        space.rigid=ModelMultiply(space.rigid,optional);}
    return space;
}
std::vector<GameVector4> GameModelClone::WorldVertices(uint32_t node) const {
    const auto space=GeometryTransforms(node);const auto &g=asset_->Data().nodes.at(node).geometry;
    Need(g.positions.size()==g.vertexCount,"world geometry positions extent");
    std::vector<GameVector4> result;result.reserve(g.vertexCount);
    for(uint32_t i=0;i<g.vertexCount;++i){const auto &p=g.positions[i];
        auto world=g.weightRecords.empty()?ModelTransform(space.rigid,{{p.x,p.y,p.z,1}}):Skin(g,i,space.palette);
        for(float v:world)Need(std::isfinite(v)&&std::abs(v)<1e12f,"invalid world geometry position");
        Need(world[3]>1e-8f,"world geometry homogeneous position");
        for(unsigned j=0;j<3;++j)world[j]/=world[3];world[3]=1;
        result.push_back(world);}
    return result;
}
GameFsStatus GameModelClone::ContactFootprint(GameContactFootprint &out) const {
    out={};
    if(!asset_||!asset_->Live())return {GameFsCode::Unavailable,0,"contact source lease retired"};
    try{
        struct Triangle {std::array<GameVector4,3> p;float opacity;};std::vector<Triangle> triangles;
        float low=1e30f,high=-1e30f;
        for(uint32_t node=0;node<pose_.size();++node){const auto &g=asset_->Data().nodes[node].geometry;
            Need(std::isfinite(pose_[node].world_opacity)&&pose_[node].world_opacity>=0,"contact opacity domain");
            if(!g.vertexCount||pose_[node].world_opacity<=0)continue;
            const auto material=pose_[node].material;
            Need(material>=-1&&material<int32_t(asset_->Materials().size()),"contact material index");
            if(material>=0)if(const auto &image=asset_->Materials()[size_t(material)].texture){
                Need(image->Width()&&image->Height()&&image->Pixels().size()==size_t(image->Width())*image->Height(),"contact texture extent");
                // Conservative caster scope: no textured cutout/translucent
                // mesh contributes bounds. Current Toro diffuse maps are opaque.
                if(!std::all_of(image->Pixels().begin(),image->Pixels().end(),[](uint32_t pixel){return (pixel>>24)==255;}))continue;
            }
            const auto points=WorldVertices(node);
            for(const auto &t:model::triangles(g)){
                float alpha=0;
                for(auto index:t){const auto a=g.rgba.empty()?255u:(std::min)(255u,2u*g.rgba.at(g.rgba.size()==1?0:index)[3]);
                    alpha=(std::max)(alpha,float(a)/255*(std::min)(1.0f,pose_[node].world_opacity));}
                if(alpha<=0)continue;
                Triangle tri{{{points.at(t[0]),points.at(t[1]),points.at(t[2])}},alpha};
                for(const auto &p:tri.p){low=(std::min)(low,p[2]);high=(std::max)(high,p[2]);}
                triangles.push_back(tri);
            }
        }
        if(triangles.empty()||high-low<=1e-4f)return {};
        const auto body_height=high-low,cut=low+body_height*.035f;
        float min_x=1e30f,max_x=-1e30f,min_y=1e30f,max_y=-1e30f,opacity=0;
        const auto include=[&](const GameVector4 &p,float alpha){min_x=(std::min)(min_x,p[0]);max_x=(std::max)(max_x,p[0]);
            min_y=(std::min)(min_y,p[1]);max_y=(std::max)(max_y,p[1]);opacity=(std::max)(opacity,alpha);};
        for(const auto &t:triangles)for(unsigned i=0;i<3;++i){const auto &a=t.p[i],&b=t.p[(i+1)%3];
            if(a[2]<=cut)include(a,t.opacity);
            if((a[2]<cut)!=(b[2]<cut)){const auto f=(cut-a[2])/(b[2]-a[2]);
                include({{a[0]+f*(b[0]-a[0]),a[1]+f*(b[1]-a[1]),cut,1}},t.opacity);}}
        if(max_x-min_x<=1e-6f||max_y-min_y<=1e-6f)return {};
        const auto pad=body_height*.02f;
        out={true,(min_x+max_x)*.5f,(min_y+max_y)*.5f,low,
            (std::min)((max_x-min_x)*.5f+pad,body_height*.2f),
            (std::min)((max_y-min_y)*.5f+pad,body_height*.2f),body_height,opacity};
        return {};
    }catch(const std::exception &e){return Error(e);}
}
GameFsStatus GameModelClone::HorizontalSurfaceBelow(float x,float y,float ceiling,float max_drop,
                                                   bool &found,float &height) const {
    found=false;height=0;
    if(!asset_||!asset_->Live())return {GameFsCode::Unavailable,0,"support source lease retired"};
    try{
        Need(std::isfinite(x)&&std::isfinite(y)&&std::isfinite(ceiling)&&std::isfinite(max_drop)&&max_drop>0,"support query domain");
        for(uint32_t node=0;node<pose_.size();++node){const auto &g=asset_->Data().nodes[node].geometry;
            Need(std::isfinite(pose_[node].world_opacity)&&pose_[node].world_opacity>=0,"support opacity domain");
            if(!g.vertexCount||pose_[node].world_opacity<.999f)continue;
            const auto points=WorldVertices(node);
            const auto *material=pose_[node].material<0?nullptr:&asset_->Materials().at(size_t(pose_[node].material));
            for(const auto &t:model::triangles(g)){
                bool opaque=true;for(auto i:t)if(!g.rgba.empty()&&g.rgba.at(g.rgba.size()==1?0:i)[3]<128)opaque=false;
                if(!opaque)continue;
                const auto &a=points.at(t[0]),&b=points.at(t[1]),&c=points.at(t[2]);
                const double ax=double(b[0])-a[0],ay=double(b[1])-a[1],bx=double(c[0])-a[0],by=double(c[1])-a[1];
                const auto area=ax*by-ay*bx,span=(std::max)({std::abs(ax),std::abs(ay),std::abs(bx),std::abs(by),1e-6});
                if(std::abs(area)<span*span*1e-10)continue;
                const auto vertical=(std::max)({std::abs(double(a[2])-b[2]),std::abs(double(a[2])-c[2])});
                if(vertical>(std::max)(1e-5,span*.001))continue; // horizontal-only native contact policy
                const auto u=((double(x)-a[0])*by-(double(y)-a[1])*bx)/area;
                const auto v=(ax*(double(y)-a[1])-ay*(double(x)-a[0]))/area,w=1-u-v;
                if(u<-1e-6||v<-1e-6||w<-1e-6)continue;
                const auto z=float(w*a[2]+u*b[2]+v*c[2]);
                if(z>ceiling||z<ceiling-max_drop||(found&&z<=height))continue;
                if(material&&material->texture){
                    Need(g.uv.size()==g.vertexCount,"support UV extent");
                    auto tx=w*g.uv[t[0]].x+u*g.uv[t[1]].x+v*g.uv[t[2]].x;
                    auto ty=w*g.uv[t[0]].y+u*g.uv[t[1]].y+v*g.uv[t[2]].y;
                    tx-=std::floor(tx);ty-=std::floor(ty);
                    const auto &image=*material->texture;
                    Need(image.Width()&&image.Height()&&image.Pixels().size()==size_t(image.Width())*image.Height(),"support texture extent");
                    const auto col=(std::min)(image.Width()-1,uint32_t(tx*image.Width()));
                    const auto row=(std::min)(image.Height()-1,uint32_t(ty*image.Height()));
                    if((image.Pixels().at(size_t(row)*image.Width()+col)>>24)!=255)continue;
                }
                found=true;height=z;
            }
        }
        return {};
    }catch(const std::exception &e){return Error(e);}
}
GameFsStatus DrawWindowsContactShadow(GameGraphics &graphics,const GameMatrix &camera,
                                      const GameContactFootprint &footprint,float height) {
    if(!footprint.valid)return {};
    const auto max_drop=footprint.body_height*.25f;
    if(!std::isfinite(height)||max_drop<=0)return {GameFsCode::Unavailable,0,"contact height domain"};
    const auto separation=(std::max)(0.0f,footprint.foot_z-height);
    const auto fade=(std::clamp)(1-separation/max_drop,0.0f,1.0f);
    if(!graphics.DrawContactEllipse(camera,footprint.x,footprint.y,height,footprint.radius_x,
        footprint.radius_y,.28f*footprint.opacity*fade*fade))return {GameFsCode::Unavailable,0,"native contact decal rejected"};
    return {};
}
GameFsStatus GameModelClone::Draw(GameGraphics &graphics, const GameMatrix &view_projection,
                                 GameModelColorPolicy color_policy,GameModelLightingPolicy lighting_policy) const {
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
            const auto space=GeometryTransforms(uint32_t(node));
            const auto &palette=space.palette;
            const auto &rigid=space.rigid;
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
                if(lighting_policy==GameModelLightingPolicy::WindowsActorSoftDiffuse&&!g.normals.empty()) {
                    Need(g.normals.size()==g.vertexCount,"model normal extent");
                    const auto &raw=g.normals[i];
                    const auto normal=g.weightRecords.empty()?
                        ModelTransformNormal(rigid,{{float(raw[0]),float(raw[1]),float(raw[2]),0}}):
                        SkinNormal(g,i,palette);
                    v.color=ModelWindowsActorSoftDiffuse(v.color,normal);
                }
                vertices.push_back(v);
            }
            GameRasterPolicy policy;
            policy.two_sided = m && (m->flags & 128);
            policy.windows_dvs_textured_modulation2x = texture.pixels && !g.rgba.empty() &&
                color_policy==GameModelColorPolicy::WindowsDvsTexturedModulation2x;
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
