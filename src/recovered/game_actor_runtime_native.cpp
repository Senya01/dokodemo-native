#include "game_actor_runtime_native.h"
#include "game_model_resources_native.h"
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
PackageView AscRecord(const PackageView& image,uint32_t index){
    const auto size=image.Describe().size;
    if(size<32||image.ReadLE32(0)!=0x00435341||image.ReadLE32(4)!=16||index>4095)
        throw std::runtime_error("Invalid/capped actual ASC header/index");
    uint64_t at=16;
    for(uint32_t i=0;i<=index;++i){
        if(at>size||size-at<16||image.ReadLE32(at+4)!=16)
            throw std::runtime_error("ASC ordinal header outside live resource");
        const auto count=image.ReadLE32(at+8);at+=16;
        if(!count||count>size-at)throw std::runtime_error("ASC ordinal payload outside live resource");
        if(i==index)return image.Subview(at,count);
        at+=count;
    }
    throw std::runtime_error("ASC ordinal not found");
}
}
GameFsStatus GameActorRuntime::Initialize(uint32_t actor,PackageView selection,GameTransition& transition,GameResourceSource* resources) {
    Close();state_={};
    try {
        if(actor>=5||!selection.Describe().readable||!transition.Snapshot().live)
            throw std::runtime_error("Actor control owners need live selected package and transition");
        selection_=std::move(selection);transition_=&transition;resources_=resources;state_.actor=actor;
        // Source9480: actor40=(0,0,600,1);source23FD0:258=1,25E=0,248=-1.
        // The camera7650 vectors and these explicit constructor stores are
        // typed owned values,not borrowed PSP/BSS coordinates or fake models.
        state_.live=true;return {};
    } catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
bool GameActorRuntime::Live() const {
    if(!state_.live||!transition_||!transition_->Snapshot().live)return false;
    try{return selection_.Describe().readable;}catch(const std::exception&){return false;}
}
GameFsStatus GameActorRuntime::BindCamera(int32_t mode) {
    if(!Live())return {GameFsCode::Unavailable,0,"Camera72 target owner expired"};
    // 1CF44/25098: retain raw scalar and link camera90 to LIVE actor40.
    // The zero branch is a symbolic static131D0 target,never a null vector.
    state_.camera_mode=mode;state_.camera_follows_actor=mode!=0;
    state_.pose_dirty=mode!=0;++state_.camera_bindings;return {};
}
GameFsStatus GameActorRuntime::StartTransition() {
    if(!Live())return {GameFsCode::Unavailable,0,"Game transition owner expired"};
    state_.loader_interval=2000;
    // Actual source17B0->15F8 produced the zero point;not code bytes10D80.
    if(!transition_->StartGameOpen())return {GameFsCode::Unavailable,0,"Game transition rejected live request"};
    ++state_.transition_requests;return {};
}
bool GameActorRuntime::WaitReady() const {
    if(!Live())return false;
    // BCFC checks actual produced flags. Future active commands need their
    // own completion producer;there is no generic one-frame auto-completion.
    return !state_.actor_request_pending&&!state_.selected_extension_pending
        &&(state_.actor_special_ready||state_.command_complete||state_.command_ready);
}
GameFsStatus GameActorRuntime::SetField54(int32_t value) {
    if(!Live())return {GameFsCode::Unavailable,0,"Actor field54 owner expired"};
    state_.source_field54=value;return {};
}
GameFsStatus GameActorRuntime::RequestManager() {
    if(!Live())return {GameFsCode::Unavailable,0,"Game manager0 owner expired"};
    state_.manager_request8=true;return {}; //1C210 sets8;not manager-update completion
}
GameFsStatus GameActorRuntime::PrepareActor(){
    if(state_.actor_model_live)return {};
    try{
        if(!Live()||!resources_||state_.actor!=0)throw std::runtime_error("Actor assets need admitted NEKO source binding");
        // 8E64/8FC0:actual actor0 name and mount21 are relocation-bound NEKO.
        // Native lazy resource ownership replaces eager207-DAK construction.
        PackageView asc;auto status=resources_->OpenResource("NEKO\\NEKO\\NEKO.ASC",asc);if(!status)return status;
        const auto names=AscRecord(asc,0);const auto count=asc.ReadLE32(8);
        if(!count||count>4096)throw std::runtime_error("ASC motion-name count cap");
        std::vector<std::string> parsed;uint64_t cursor=0;
        for(uint32_t i=0;i<count;++i){
            std::string name;bool ended=false;
            while(cursor<names.Describe().size){const auto c=names.ReadU8(cursor++);if(!c){ended=true;break;}
                if(c<32||c>126||name.size()>=127||c=='\\'||c=='/'||c==':')throw std::runtime_error("ASC motion component outside native safe domain");
                name.push_back(char(c));}
            if(!ended||name.empty())throw std::runtime_error("ASC motion name missing/empty");parsed.push_back(std::move(name));
        }
        // 98F0/F4's distinct body-name table0D58[0] is R32-bound TORO.
        auto asset=std::make_shared<GameModelAsset>();status=asset->Load(*resources_,"NEKO\\NEKO\\TORO.DVS","NEKO\\NEKO\\gim\\");if(!status)return status;
        GameModelClone clone;status=clone.Initialize(asset);if(!status)return status;
        const auto stats=asset->Statistics();actor_asc_=std::move(asc);actor_motion_names_=std::move(parsed);
        actor_asset_=std::move(asset);actor_clone_=std::move(clone);state_.actor_model_live=true;
        state_.actor_model_nodes=stats.nodes;state_.actor_model_vertices=stats.vertices;state_.actor_model_triangles=stats.triangles;
        return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorRuntime::RequestActorEvent(int32_t first,int32_t second){
    const auto prepared=PrepareActor();if(!prepared)return prepared;
    try{
        if(first<0||second!=0)throw std::runtime_error("Actor event outside reached positive-command/zero-followup domain");
        if(state_.actor_request_pending||state_.selected_extension_pending)throw std::runtime_error("Actor event already pending");
        auto command=AscRecord(actor_asc_,uint32_t(first));command_image_=std::move(command);state_.requested_command=first;
        state_.actor_request_pending=state_.selected_extension_pending=true;++state_.event_requests;
        return {}; //B1D4/1DF00 queue request;no fabricated completion signal
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorRuntime::AdvanceActorFrame(){
    if(!Live())return {GameFsCode::Unavailable,0,"Actor phase owner expired"};
    if(state_.requested_command<0||state_.command_blocked)return {};
    try{
        ++state_.actor_phases;
        if(state_.actor_request_pending){
            if(state_.requested_command!=176)throw std::runtime_error("Unimplemented reached ASC command identity");
            // B774 calls24700 before394 clears;1E824 then copies394 toext20.
            state_.active_command=176;state_.command_complete=state_.command_ready=false;
            state_.actor_request_pending=state_.selected_extension_pending=false;
        }
        const auto size=command_image_.Describe().size;const uint64_t at=state_.command_pc;
        if(at>size||size-at<4)throw std::runtime_error("ASC instruction outside live ordinal payload");
        const auto op=command_image_.ReadLE32(at);
        if(op==0x8040){
            if(size-at<8)throw std::runtime_error("ASC8040 operand truncated");
            state_.command_flag25A=command_image_.ReadLE32(at+4)!=0;state_.command_pc+=8;++state_.command_effects;return {};
        }
        if(op!=0x8030||size-at<12)throw std::runtime_error("Unimplemented/truncated reached ASC opcode");
        const auto parameter=command_image_.ReadLE32(at+4);const auto value=int32_t(command_image_.ReadLE32(at+8));
        if(parameter==3){
            if(value<0)throw std::runtime_error("Negative ASC coordinate index");
            if(!coordinate_loaded_){
                const auto status=resources_->OpenResource("DATA\\SEQ\\sequence_coord.bin",coordinate_image_);
                if(!status)throw std::runtime_error(status.detail);
                coordinate_loaded_=true;
            }
            const uint64_t coordinate=uint64_t(uint32_t(value))*8;
            if(coordinate_image_.Describe().size<coordinate+8)throw std::runtime_error("ASC coordinate pair extent");
            state_.logical_xy={{float(int32_t(coordinate_image_.ReadLE32(coordinate))),float(int32_t(coordinate_image_.ReadLE32(coordinate+4)))}};
            state_.logical_heading=3.14159265358979323846f;state_.heading_known=true;state_.actor_math_mode=1;
            // B720 applies the mode5 point then B744 queues logical9.
            // Do not skip its real motion8/channel handoff to later ASC ops.
            state_.pending_logical_motion=9;state_.command_pc+=12;++state_.command_effects;
            state_.command_frontier=0xB790;
            throw std::runtime_error("ASC176 coordinate mode5 queued logical9: actual motion8/model channel handoff not implemented");
        }else throw std::runtime_error("Unimplemented reached ASC8030 parameter");
    }catch(const std::exception& e){
        state_.command_blocked=true;state_.command_error=e.what();
        if(!state_.command_frontier)state_.command_frontier=0x24098;
        return {}; // Live host frame,NOT successful ASC command completion.
    }
}
GameFsStatus GameActorRuntime::CreateModel(int32_t resource,int32_t category,int32_t motion,float x,int32_t& out) {
    out=-1;
    if(!Live()||!resources_)return {GameFsCode::Unavailable,0,"Render31 needs live native resource owner"};
    if(resource<0||uint32_t(resource)>=GameModelResources.size())
        return {GameFsCode::Unavailable,0,"Render31 resource outside finite208 table"};
    if(category!=0||motion!=0||x!=0)return {GameFsCode::Unavailable,0,"Render31 reached category/transform/motion domain not yet recovered"};
    if(state_.model_slots>=models_.size())return {}; // source returns -1 on full pool
    size_t index=0;while(index<models_.size()&&models_[index].active)++index;
    if(index==models_.size())return {};
    auto asset=std::make_shared<GameModelAsset>();
    const auto& entry=GameModelResources[size_t(resource)];
    const std::string base=std::string("BG_ROOM\\")+entry.name+"\\";
    const auto path=base+"model01.DVS";
    auto status=asset->Load(*resources_,path,base+"gim\\");
    if(!status)return status;
    std::vector<GameModelMotion> motions;
    for(uint32_t i=0;i<entry.motions;++i){
        GameModelMotion data;status=data.Load(*resources_,base+"model0"+std::to_string(i+1)+".DAK",*asset);
        if(!status)return status;motions.push_back(std::move(data));
    }
    GameModelClone clone;status=clone.Initialize(asset);if(!status)return status;
    status=clone.Update(ModelIdentity());if(!status)return status;
    auto& slot=models_[index];slot.resource=resource;slot.category=category;
    slot.asset=std::move(asset);slot.clone=std::move(clone);slot.motions=std::move(motions);slot.active=true;
    const auto statistics=slot.asset->Statistics();
    ++state_.model_slots;state_.model_nodes+=statistics.nodes;state_.model_vertices+=statistics.vertices;
    state_.model_triangles+=statistics.triangles;state_.last_model_slot=int32_t(index);state_.last_model_path=path;
    state_.motion_templates+=entry.motions;
    out=int32_t(index);return {};
}
GameFsStatus GameActorRuntime::Draw(GameGraphics& graphics) {
    if(!Live())return {GameFsCode::Unavailable,0,"Actor render owners expired"};
    if(!state_.model_slots)return {};
    if(!state_.camera_bindings||!state_.camera_follows_actor)return {GameFsCode::Unavailable,0,"Room render needs actual following-camera binding;static131D0 producer unrecovered"};
    // Source493C0 uses an unrecovered world basis. Named functional policy:
    // canonical native identity basis,authored geometry,NO fit/recenter/scale.
    // Cold7650/7E28/8140 eye(0,-4400,980),look+Y,FOV30,near100/far10400.
    auto view=ModelIdentity();view[5]=0;view[9]=1;view[6]=-1;view[10]=0;
    view[13]=-980;view[14]=-4400;
    const auto camera=ModelMultiply(ModelPerspective(0.5235987756f,480.0f/272.0f,100,10400),view);
    if(!graphics.Clear(0xFF000000,0))return {GameFsCode::Unavailable,0,"Room clear failed"};
    const auto prior_pixels=graphics.Snapshot().mesh_pixels;
    for(const auto& slot:models_)if(slot.active){const auto status=slot.clone.Draw(graphics,camera);if(!status)return status;}
    if(!transition_->Draw(graphics)||!graphics.SwapBuffers())return {GameFsCode::Unavailable,0,"Room frame presentation failed"};
    state_.model_pixel_writes+=graphics.Snapshot().mesh_pixels-prior_pixels;
    GameGraphicsFrame frame;
    if(!graphics.CopyFront(frame))return {GameFsCode::Unavailable,0,"Room front-buffer ownership failed"};
    state_.visible_pixels=0;
    for(uint32_t y=0;y<frame.height;++y)for(uint32_t x=0;x<frame.width;++x)
        if((frame.pixels[size_t(y)*frame.stride+x]&0xFFFFFF)!=0)++state_.visible_pixels;
    ++state_.model_frames;return {};
}
void GameActorRuntime::Close() {
    actor_clone_.Close();actor_asset_.reset();actor_asc_.Reset();command_image_.Reset();coordinate_image_.Reset();actor_motion_names_.clear();state_.actor_model_live=false;
    state_.actor_request_pending=state_.selected_extension_pending=false;
    coordinate_loaded_=false;state_.active_command=-1;
    state_.pending_logical_motion=-1;state_.manager_request8=false;state_.command_flag25A=false;
    for(auto& slot:models_){slot.clone.Close();slot.asset.reset();slot.motions.clear();slot.active=false;}
    state_.model_slots=0;selection_.Reset();transition_=nullptr;resources_=nullptr;state_.live=false;state_.camera_follows_actor=false;
}
} // namespace dokodemo::recovered
