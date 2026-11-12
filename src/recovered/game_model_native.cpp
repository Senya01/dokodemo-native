#include "game_model_native.h"
#include "core_hash.h"
#include "../script_vm.h"
#include "game_actor_runtime_native.h" // existing709C arithmetic, not a second table
#include "game_system_option_native.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace dokodemo::recovered {
bool GameMainSceneData::Current()const{
    try{if(!live_||!file_||!file_->Snapshot().file_live||!image_.Describe().readable)return false;
        for(const auto& part:selected_inputs_)if(part.first.InitializedDigest()!=part.second)return false;return true;
    }catch(...){return false;}
}
GameFsStatus GameMainSceneData::Load(std::unique_ptr<GameFileSource> file,const std::array<uint8_t,32>& expected){
    if(file_||!file)return {GameFsCode::Unavailable,0,"fresh main DATA file owner required"};file_=std::move(file);
    try{
        auto status=file_->Open();if(!status)return status;
        if(file_->Snapshot().file_bytes!=1341110)throw std::runtime_error("prepared main DATA extent mismatch");
        status=file_->Read(0,1341110,image_);if(!status)return status;
        if(image_.Describe().size!=1341110||image_.InitializedDigest()!=expected)throw std::runtime_error("prepared main DATA identity mismatch");
        digest_=expected;
        if(image_.ReadLE32(0)!=0x464C457F||image_.ReadU8(4)!=1||image_.ReadU8(5)!=1||image_.ReadLE16(16)!=0xFFA0||image_.ReadLE16(18)!=8)
            throw std::runtime_error("prepared main DATA ELF32 LE MIPS PRX required");
        const auto ph=image_.ReadLE32(28),sh=image_.ReadLE32(32);const auto count=image_.ReadLE16(48);
        if(image_.ReadLE16(42)!=32||image_.ReadLE16(44)<2||image_.ReadLE16(46)!=40||!count||count>128||ph>1341110-64||sh>1341110-uint32_t(count)*40)
            throw std::runtime_error("prepared main DATA table layout");
        std::array<uint32_t,2> starts{},sizes{};
        for(unsigned k=0;k<2;++k){starts[k]=image_.ReadLE32(ph+32*k+4);sizes[k]=image_.ReadLE32(ph+32*k+16);
            if(image_.ReadLE32(ph+32*k)!=1||!sizes[k]||starts[k]>1341110||sizes[k]>1341110-starts[k])throw std::runtime_error("prepared main DATA segment extent");}
        const auto part=[&](unsigned segment,uint32_t at,uint32_t n){if(segment>=2||at>sizes[segment]||n>sizes[segment]-at)throw std::runtime_error("selected main DATA unbacked range");return image_.Subview(starts[segment]+at,n);};
        std::array<int,25> name_segments;name_segments.fill(-1);int position_segment=-1,heading_segment=-1;unsigned selected=0;
        for(uint32_t k=0;k<count;++k){const auto row=sh+40*k;if(image_.ReadLE32(row+4)!=0x700000A0)continue;
            const auto at=image_.ReadLE32(row+16),n=image_.ReadLE32(row+20);if(n%8||at>1341110||n>1341110-at)throw std::runtime_error("prepared main DATA relocation bounds");
            for(uint32_t j=0;j<n;j+=8){const auto off=image_.ReadLE32(at+j),info=image_.ReadLE32(at+j+4);const auto origin=(info>>8)&255,refer=(info>>16)&255,type=info&255;
                if(origin==1&&off>=0x6808&&off<0x6808+25*4){if(type!=2||off%4||refer>=2)throw std::runtime_error("BGM name relocation kind");auto& dst=name_segments[(off-0x6808)/4];if(dst!=-1)throw std::runtime_error("duplicate BGM name relocation");dst=int(refer);++selected;}
                if(origin==1&&((off>=0x4ACC&&off<0x4B14)||(off>=0x4B74&&off<0x4CF4)
                    ||(off>=0x488&&off<0x4D0)||off==0x9B0)){
                    if(type!=2||off%4||refer>=2||!outing_relocations_.emplace(off,uint8_t(refer)).second)
                        throw std::runtime_error("selected outing R32 relocation kind/duplicate");
                }
                if(origin==0&&(off==0xD2D0||off==0xD2D8||off==0xBD44||off==0xBD54)){
                    const auto want=(off==0xD2D0||off==0xBD44)?5u:6u;if(type!=want||refer>=2)throw std::runtime_error("placement relocation kind");
                    auto& dst=(off==0xD2D0||off==0xD2D8)?position_segment:heading_segment;
                    if(dst!=-1&&dst!=int(refer))throw std::runtime_error("placement relocation pair mismatch");dst=int(refer);++selected;
                }
            }
        }
        if(selected!=29||position_segment<0||heading_segment<0)throw std::runtime_error("selected main DATA relocation set missing");
        lists_=part(0,0xD8CC8,76);gains_=part(0,0xD85B8,40);
        const auto token=[&](unsigned segment,uint32_t at){const auto raw=part(segment,at,80);uint32_t n=0;while(n<80&&raw.ReadU8(n)){if(raw.ReadU8(n)<33||raw.ReadU8(n)>126)throw std::runtime_error("selected DATA text outside ASCII token");++n;}if(!n||n==80)throw std::runtime_error("selected DATA text terminator");return raw.Subview(0,n+1);};
        for(unsigned k=0;k<25;++k)names_[k]=token(unsigned(name_segments[k]),part(1,0x6808+4*k,4).ReadLE32(0));
        root_=token(0,905276); // exact selected4A9B8 root relocation, held DATA not code string
        position_file_=starts[unsigned(position_segment)]+0x4180;heading_file_=starts[unsigned(heading_segment)]+0xD7EE0;
        (void)part(unsigned(position_segment),0x4180,13*16);(void)part(unsigned(heading_segment),0xD7EE0,4);
        const auto hold=[&](PackageView view){selected_inputs_.emplace_back(view,view.InitializedDigest());};
        hold(lists_);hold(gains_);hold(root_);for(const auto& name:names_)hold(name);
        hold(image_.Subview(position_file_,12));hold(image_.Subview(position_file_+12*16,12));hold(image_.Subview(heading_file_,4));
        segment_files_=starts;segment_sizes_=sizes;lifetime_=std::make_shared<uint8_t>(0);live_=true;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
PackageView GameMainSceneData::Lists()const{if(!Current())throw std::runtime_error("main DATA owner retired/drifted");return lists_;}
PackageView GameMainSceneData::Gains()const{if(!Current())throw std::runtime_error("main DATA owner retired/drifted");return gains_;}
PackageView GameMainSceneData::Root()const{if(!Current())throw std::runtime_error("main DATA owner retired/drifted");return root_;}
PackageView GameMainSceneData::Name(uint32_t index)const{if(!Current()||index>=25)throw std::runtime_error("main DATA name owner/index");return names_[index];}
PackageView GameMainSceneData::PositionRow(uint32_t scene)const{if(!Current()||(scene!=0&&scene!=12))throw std::runtime_error("main DATA selected placement/current owner");return image_.Subview(position_file_+16*scene,12);}
PackageView GameMainSceneData::PlacementHeading()const{if(!Current())throw std::runtime_error("main DATA heading owner retired");return image_.Subview(heading_file_,4);}
GameFsStatus GameMainSceneData::Placement(uint32_t scene,std::array<float,4>& out)const{
    try{if(!Current()||(scene!=0&&scene!=12))throw std::runtime_error("placement current selected DATA/scene0 or12 required");std::array<float,4> values{};
        for(unsigned k=0;k<4;++k){const auto word=image_.ReadLE32(k==3?heading_file_:position_file_+scene*16+k*4);std::memcpy(&values[k],&word,4);if(!std::isfinite(values[k]))throw std::runtime_error("placement nonfinite field");}out=values;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameMainSceneDataSnapshot GameMainSceneData::Snapshot()const{GameMainSceneDataSnapshot out;out.live=Current();out.identity=digest_;if(out.live){const auto d=image_.Describe();out.owner=d.owner;out.generation=d.generation;out.serial=d.serial;out.names=25;}return out;}
GameFsStatus GameMainSceneData::Close(){live_=false;lifetime_.reset();outing_relocations_.clear();selected_inputs_.clear();lists_={};gains_={};root_={};names_={};image_={};if(file_){auto status=file_->Close();if(!status)return status;file_.reset();}return {};}
namespace {
struct OutingTextLease final:PackageStorageLease {
    std::vector<uint8_t> bytes,known;
    bool Span(PackageStorageSpan& out,std::string&)const override{
        out={const_cast<uint8_t*>(bytes.data()),const_cast<uint8_t*>(known.data()),bytes.size(),
            uint64_t(reinterpret_cast<uintptr_t>(bytes.data())),uint64_t(reinterpret_cast<uintptr_t>(this)),1,1,0,true,false};return true;
    }
};
PackageView OutingText(const std::string& text){
    auto p=std::make_shared<OutingTextLease>();p->bytes.assign(text.begin(),text.end());p->bytes.push_back(0);p->known.assign(p->bytes.size(),1);
    return PackageStorage::Attach(p,p->bytes.size());
}
std::string OutingTextBytes(const PackageView& v){
    std::string out;for(uint64_t i=0;i<v.Describe().size;++i){const auto c=v.ReadU8(i);if(!c){if(i+1!=v.Describe().size)throw std::runtime_error("outing text trailing extent");return out;}out+=char(c);}
    throw std::runtime_error("outing text terminator");
}
std::string OutingStringFormat(const PackageView& format,const std::vector<std::string>& args){
    const auto text=OutingTextBytes(format);std::string out;size_t arg=0;
    for(size_t i=0;i<text.size();++i){if(text[i]!='%')out+=text[i];else{
        if(++i==text.size()||text[i]!='s'||arg==args.size())throw std::runtime_error("selected outing string format");out+=args[arg++];}
        if(out.size()>255)throw std::runtime_error("selected outing path extent");}
    if(arg!=args.size()||out.empty()||out.find("..")!=out.npos||out.find(':')!=out.npos||out[0]=='/'||out[0]=='\\')throw std::runtime_error("selected outing path domain");return out;
}
}
bool GameOutingData::Current()const{
    try{if(lifetime.expired()||pins.empty())return false;for(const auto& p:pins)if(p.first.InitializedDigest()!=p.second)return false;return true;}catch(...){return false;}
}
GameFsStatus GameMainSceneData::Outing12(std::shared_ptr<const GameOutingData>& out)const{
    if(!Current())return {GameFsCode::Unavailable,0,"current main DATA outing packet required"};
    try{
        auto data=std::shared_ptr<GameOutingData>(new GameOutingData);data->lifetime=lifetime_;
        const auto part=[&](unsigned seg,uint32_t at,uint32_t n){if(seg>=2||at>segment_sizes_[seg]||n>segment_sizes_[seg]-at)throw std::runtime_error("selected outing DATA extent");return image_.Subview(segment_files_[seg]+at,n);};
        const auto text=[&](unsigned seg,uint32_t at,uint32_t cap){auto v=part(seg,at,cap);uint32_t n=0;while(n<cap&&v.ReadU8(n))++n;if(n==cap)throw std::runtime_error("selected outing DATA string bound");return v.Subview(0,n+1);};
        const auto pointer=[&](uint32_t site,uint32_t cap){const auto r=outing_relocations_.find(site);if(r==outing_relocations_.end())throw std::runtime_error("selected outing R32 missing");return text(r->second,part(1,site,4).ReadLE32(0),cap);};
        data->origin=image_.Subview(0,64);data->registrations=part(1,0x43C4+66*20,60);data->motion_counts=part(1,0xA18,6);
        data->position_row=PositionRow(12);data->placement_heading=PlacementHeading();
        const auto row=outing_relocations_.find(0x9B0);
        if(row==outing_relocations_.end()||row->second!=1||part(1,0x9B0,4).ReadLE32(0)!=0x488)throw std::runtime_error("selected50AC row lineage");
        data->triplets=part(1,0x488,84);bool sentinel=false;
        for(unsigned i=0;i<7;++i){std::array<uint32_t,3> words{};for(unsigned j=0;j<3;++j)words[j]=data->triplets.ReadLE32(i*12+j*4);
            if(!words[0]&&!words[1]&&!words[2]){if(i!=6)throw std::runtime_error("selected50AC recipe domain");sentinel=true;break;}
            if(i==6)throw std::runtime_error("selected50AC terminator");
            for(unsigned j=0;j<3;++j)if(words[j]){auto v=pointer(0x488+i*12+j*4,32);const auto prior=data->auxiliary_names.find(words[j]);
                if(prior!=data->auxiliary_names.end()&&OutingTextBytes(prior->second)!=OutingTextBytes(v))throw std::runtime_error("selected auxiliary token alias");data->auxiliary_names[words[j]]=v;}}
        if(!sentinel)throw std::runtime_error("selected50AC sentinel");
        data->model_format=text(0,0xD3058,32);data->light_format=text(0,0xD307C,32);data->glyph_format=text(0,0xD9C50,64);
        data->text_map.keys=part(1,0x4B14,96);for(unsigned i=0;i<96;++i)data->text_map.expansions[i]=pointer(0x4B74+i*4,64);
        data->text_map.fallback=text(0,0xE0760,64);data->text_map.integer_format=text(0,0xE075C,64);
        const auto registry=part(1,0x43C4+29*20,20);uint32_t n=0;while(n<20&&registry.ReadU8(n))++n;if(!n||n==20)throw std::runtime_error("source29 registration");
        const auto directory_view=text(0,0xD9C68,32);
        const auto mount=OutingTextBytes(registry.Subview(0,n+1)),directory=OutingTextBytes(directory_view);
        const auto models=text(0,0xD9C74,64),textures=text(0,0xD9C80,64);
        for(unsigned i=0;i<18;++i)data->support_paths.models[i]=OutingText(OutingStringFormat(models,{mount,directory,OutingTextBytes(pointer(0x4ACC+i*4,64))}));
        data->support_paths.textures=OutingText(OutingStringFormat(textures,{mount,directory}));
        const auto hold=[&](PackageView v){data->pins.emplace_back(v,v.InitializedDigest());};
        hold(data->origin);hold(data->registrations);hold(data->motion_counts);hold(data->triplets);hold(data->model_format);hold(data->light_format);hold(data->glyph_format);
        hold(data->position_row);hold(data->placement_heading);
        hold(data->text_map.keys);hold(data->text_map.fallback);hold(data->text_map.integer_format);hold(registry);hold(models);hold(textures);hold(directory_view);
        hold(part(1,0x9B0,4));hold(part(1,0x4ACC,18*4));hold(part(1,0x4B74,96*4));
        for(const auto& v:data->text_map.expansions)hold(v);for(const auto& v:data->support_paths.models)hold(v);hold(data->support_paths.textures);
        for(const auto& v:data->auxiliary_names)hold(v.second);
        if(!data->Current()||!Current())throw std::runtime_error("outing packet drift");out=std::move(data);return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus ReadBgmGain14834(const GameOptionSettingsSnapshot& settings,PackageView table,uint32_t& out){
    try{if(!settings.live||!settings.owner||!settings.generation||table.Describe().size!=40)throw std::runtime_error("14834 current typed settings/ten copied gain words required");
        const auto digest=table.InitializedDigest();uint32_t at=settings.words[2]+5u;if(at>=11)at=0;
        if(at>=10)throw std::runtime_error("14834 index10 is not one of ten initialized copied gain words");
        const auto value=table.ReadLE32(at*4);if(table.InitializedDigest()!=digest)throw std::runtime_error("14834 gain table drift");out=value;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
bool GameBackgroundRuntime::SettingsCurrent(const std::shared_ptr<GameBackgroundRuntime>& current,const GameOptionSettings& settings)const{
    const auto parent=settings_runtime_.lock();const auto life=settings_life_.lock();
    if(!Live()||!parent||parent!=current||parent.owner_before(current)||current.owner_before(parent)||settings_!=&settings||!life)return false;
    const auto now=settings.OptionSettings();return now.live&&now.owner==settings_owner_&&now.generation==settings_generation_;
}
GameFsStatus GameBackgroundRuntime::BindSceneSettings(const std::shared_ptr<GameBackgroundRuntime>& parent,GameOptionSettings& settings){
    if(settings_)return SettingsCurrent(parent,settings)?GameFsStatus{}:GameFsStatus{GameFsCode::Unavailable,0,"BGM settings cannot replace current constructor binding"};
    const auto life=settings.OptionSettingsLifetime().lock();const auto current=settings.OptionSettings();
    if(!parent||parent.get()!=this||!Live()||!life||!current.live||!current.owner||!current.generation)
        return {GameFsCode::Unavailable,0,"BGM settings current content constructor required"};
    settings_runtime_=parent;settings_=&settings;settings_life_=life;settings_owner_=current.owner;settings_generation_=current.generation;return {};
}
struct GameSceneBgm::Data {
    std::shared_ptr<const void> lifetime=std::make_shared<uint8_t>(0);
    uint64_t last_job_request=0,last_cache_consumed=0;
    std::weak_ptr<GameBackgroundRuntime> runtime;
    std::weak_ptr<GameManagerSwitchControl> manager;
    std::weak_ptr<int32_t> manager_selection;
    bool selection_bound=false;
    GameOptionSettings* settings=nullptr;
    std::weak_ptr<const void> settings_lifetime;
    uint64_t settings_owner=0,settings_generation=0;
    std::array<PackageView,3> inputs;
    std::array<std::array<uint8_t,32>,3> hashes;
    std::function<PackageView(uint32_t)> names;
    GameBgmFileQueue files;
    GameSceneBgmSnapshot state;
    bool closing=false;
    bool Current()const{
        const auto r=runtime.lock();const auto life=settings_lifetime.lock();
        if(closing||!state.initialized||!r||!r->Live()||!life||!settings||!r->SettingsCurrent(r,*settings))return false;
        if(selection_bound){const auto m=manager.lock();const auto cell=manager_selection.lock();
            if(!m||!cell||!m->CurrentBorrow(m)||m->Snapshot().epoch!=state.actor_epoch||*cell!=state.stored4BC)return false;}
        const auto now=settings->OptionSettings();if(!now.live||now.owner!=settings_owner||now.generation!=settings_generation)return false;
        try{for(unsigned i=0;i<3;++i)if(inputs[i].InitializedDigest()!=hashes[i])return false;return true;}catch(...){return false;}
    }
    bool Bound(const std::shared_ptr<GameBackgroundRuntime>& current)const{
        const auto old=runtime.lock();return Current()&&current&&old==current&&!old.owner_before(current)&&!current.owner_before(old);
    }
};
GameFsStatus GameSceneBgm::BindSelectionCell(const std::shared_ptr<GameManagerSwitchControl>& manager,uint64_t epoch){
    if(!data_||!data_->Current()||data_->selection_bound||!manager||!manager->CurrentBorrow(manager)
        ||manager->state_.epoch!=epoch||data_->state.actor_epoch!=epoch||!data_->state.selected||!manager->stored_bgm4BC_)
        return {GameFsCode::Unavailable,0,"actual initial BGM/current LoadMan selection cell required"};
    data_->manager=manager;data_->manager_selection=manager->stored_bgm4BC_;
    *manager->stored_bgm4BC_=data_->state.stored4BC;data_->selection_bound=true;return {};
}
GameSceneBgm::GameSceneBgm()=default;
GameSceneBgm::~GameSceneBgm(){(void)Close();}
GameFsStatus GameSceneBgm::Initialize(std::shared_ptr<GameBackgroundRuntime> runtime,uint64_t epoch,GameOptionSettings& settings,
    PackageView lists,PackageView gains,PackageView root,std::function<PackageView(uint32_t)> names,GameBgmFileQueue::Factory factory){
    if(data_||!runtime||runtime->bgm_constructor_issued_||!runtime->SettingsCurrent(runtime,settings)||!epoch||!names||!factory)return {GameFsCode::Unavailable,0,"scene BGM fresh owner/current runtime/settings/inputs required"};
    try{
        const auto lifetime=settings.OptionSettingsLifetime().lock();if(!lifetime)throw std::runtime_error("BGM settings actual lifetime required");
        const auto snapshot=settings.OptionSettings();if(!snapshot.live||!snapshot.owner||!snapshot.generation)throw std::runtime_error("BGM current initialized settings required");
        if(lists.Describe().size!=76||gains.Describe().size!=40)throw std::runtime_error("BGM exact held list/gain extents required");
        auto d=std::make_unique<Data>();d->runtime=runtime;d->settings=&settings;d->settings_lifetime=lifetime;d->settings_owner=snapshot.owner;d->settings_generation=snapshot.generation;
        d->inputs={{lists,gains,root}};for(unsigned i=0;i<3;++i)d->hashes[i]=d->inputs[i].InitializedDigest();
        d->names=std::move(names);auto status=d->files.Initialize(runtime,epoch,std::move(factory));if(!status)return status;
        d->state.initialized=true;d->state.epoch=epoch;runtime->bgm_constructor_issued_=true;data_=std::move(d);return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameSceneBgm::BeginInitial(const std::shared_ptr<GameBackgroundRuntime>& runtime,GameActorRuntime& actor){
    if(!data_||!data_->Bound(runtime)||data_->state.attempted||!actor.BackgroundModuleMatches(runtime)||actor.Snapshot().scene_clock_ready)
        return {GameFsCode::Unavailable,0,"initial BGM requires fresh actual scene clock/runtime owner"};
    auto& d=*data_;auto& s=d.state;
    auto status=runtime->Read(s.calendar);if(!status)return status;
    if(!d.Bound(runtime))return {GameFsCode::Unavailable,0,"BGM input changed during current clock read"};
    status=actor.InitializeScene0Clock(s.calendar);if(!status)return status;s.actor_epoch=actor.Snapshot().owner_epoch;
    int32_t period=-1;status=actor.ReadInitialScenePeriod7398(period);if(!status)return status;
    s.period4B4=uint32_t(period);s.attempted=true;
    status=SelectBgm19B6C(s.period4B4,s.stored4BC,d.inputs[0],*runtime->rng_,s.selection);if(!status)return status;
    s.stored4BC=int32_t(s.selection.index);s.selected=true;s.selection_writer=0x19E20; // BEFORE actual file request
    try{status=d.files.Request(runtime,s.epoch,s.selection.index,d.inputs[2],d.names(s.selection.index),s.file_result);}
    catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
    if(status&&!d.Bound(runtime))return {GameFsCode::Unavailable,0,"initial BGM owner changed during file creation"};
    if(status)s.requested=true;return status; //0/-1 wrapper outcomes are not readiness
}
GameFsStatus GameSceneBgm::RefreshCachedPeriod(const std::shared_ptr<GameBackgroundRuntime>& runtime,GameActorRuntime& actor){
    if(!data_||!data_->Bound(runtime)||!data_->state.requested||!actor.BackgroundModuleMatches(runtime)
       ||actor.Snapshot().owner_epoch!=data_->state.actor_epoch)
        return {GameFsCode::Unavailable,0,"1B114 current initialized4DC scene/module owner required"};
    auto& d=*data_;std::array<uint32_t,7> calendar{};auto status=runtime->Read(calendar);if(!status)return status;
    if(!d.Bound(runtime)||actor.Snapshot().owner_epoch!=d.state.actor_epoch)
        return {GameFsCode::Unavailable,0,"19AFC current clock/scene owner drift"};
    int32_t period=-1;status=actor.QueryScenePeriod(calendar[3],calendar[2],period);if(!status)return status;
    if(period<0||period>=6)return {GameFsCode::Unavailable,0,"19AFC period outside admitted current scene"};
    constexpr uint32_t variants[]{0,1,1,2,0,0}; // existing4848 mapping
    d.state.calendar=calendar;d.state.period4B4=uint32_t(period); //19B50 before19B58
    d.state.variant4AC=variants[period];d.state.cache_refreshed=true;++d.state.cache_revision;return {};
}
GameFsStatus GameSceneBgm::RequestControl(const std::shared_ptr<GameBackgroundRuntime>& runtime,uint32_t argument){
    if(!data_||!data_->Bound(runtime)||!data_->state.requested)return {GameFsCode::Unavailable,0,"4AD4C current selected request/settings owner required"};
    try{auto& d=*data_;const auto before=d.settings->OptionSettings();
    const auto file=d.files.Read(uint32_t(d.state.stored4BC));if(!file.current)return {GameFsCode::Unavailable,0,"4AD4C actual selected file record unavailable"};
    GameBgmControlRecord control;const auto source=d.settings->OptionSoundOwner();
    auto status=d.settings->RequestBgmControlPrefix(before,source,d.inputs[1],uint32_t(d.state.stored4BC),argument,file.request,control);if(!status)return status;
    if(!d.Bound(runtime)||!GameOptionSnapshotExact(before,d.settings->OptionSettings()))return {GameFsCode::Unavailable,0,"14834 actual settings/input drift after prefix"};
    status=d.files.BindControl(runtime,d.state.epoch,uint32_t(d.state.stored4BC),control);if(status)d.state.control_return=control.wrapper_return;return status;
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameSceneBgm::RequestCleanupControl(const std::shared_ptr<GameBackgroundRuntime>& runtime,bool loader469,uint32_t ui){
    if(!data_||!data_->Bound(runtime))return {GameFsCode::Unavailable,0,"188BC current scene/settings/record owner required"};
    if(!loader469)return RequestControl(runtime,0);
    //18898 unsigned(ui-16)<5 selects5, otherwise2. Not a BGM/file-ready flag.
    const uint32_t index=uint32_t(ui-16)<5?5u:2u;
    auto& d=*data_;const auto before=d.settings->OptionSettings();const auto sound=d.settings->OptionSoundOwner();GameBgmControlRecord out;
    const auto status=d.settings->RequestBgmRecordControlPrefix(before,sound,d.inputs[1],index,0,out);if(!status)return status;
    if(!d.Bound(runtime)||!GameOptionSnapshotExact(before,d.settings->OptionSettings())||!out.bound||out.record_index!=index||out.file_request_bound||out.linked_request)
        return {GameFsCode::Unavailable,0,"188BC same initialized record/control readback drift"};
    d.state.control_return=out.wrapper_return;d.state.cleanup_control=out;return {};
}
GameFsStatus GameSceneBgm::Pump(const std::shared_ptr<GameBackgroundRuntime>& runtime){
    if(!data_||!data_->Bound(runtime))return {GameFsCode::Unavailable,0,"BGM current file worker owner required"};
    return data_->files.Pump(runtime,data_->state.epoch);
}
GameFsStatus GameSceneBgm::DepartureControlCurrent(const std::shared_ptr<GameBackgroundRuntime>& runtime)const{
    if(!data_||!data_->Bound(runtime))return {GameFsCode::Unavailable,0,"17FC0 same current sound/settings/file owner required"};
    const auto sound=data_->settings->OptionSoundOwner();
    if(!sound.live||!sound.cold_initialized||!sound.owner||!sound.generation||!sound.id_writes)
        return {GameFsCode::Unavailable,0,"17FC0 actual initialized source sound control unavailable"};
    // 4B530 and4B400 both read CURRENT11EC. A genuine negative value takes
    // their local no-voice branches. Neither pending file I/O nor a failed
    // bank registration supplies that value. Positive voice consumers remain
    // outside this finite file/scene implementation (deep audio stays paused).
    if(sound.current_id>=0)return {GameFsCode::Unavailable,0,"17FC0 current voice requires unimplemented4B62C; no false pending/result"};
    return {};
}
GameFsStatus GameSceneBgm::RetireSceneRecord(const std::shared_ptr<GameBackgroundRuntime>& runtime){
    if(!data_||!data_->Bound(runtime)||!data_->state.selected||!data_->state.requested||data_->state.source_scene_retired)
        return {GameFsCode::Unavailable,0,"19F88 actual current scene BGM record required"};
    auto& d=*data_;const auto file=d.files.Read(uint32_t(d.state.stored4BC));
    if(!file.current)return {GameFsCode::Unavailable,0,"4ABC8 current selected FILE-RECORD unknown"};
    if(file.bank5C>=0)return {GameFsCode::Unavailable,0,"4ABF8 registered FILE-RECORD release5560C not admitted"};
    // Actual4ABF0 negative registrar branch does NOT clear/free the record.
    // Its native pending/read allocation stays in the SAME module file pool.
    // 19F94 subsequently clears the scene selection, regardless of I/O phase.
    const auto cell=d.manager_selection.lock();if(!cell)return {GameFsCode::Unavailable,0,"19F94 current LoadMan cell retired"};
    d.state.stored4BC=0;*cell=0;d.state.source_scene_retired=true;d.state.selection_writer=0x19F94;return {};
}
bool GameSceneBgm::SharesDataBacking(PackageView view)const{return data_&&data_->Current()&&data_->inputs[0].SharesBacking(view);}
bool GameSceneBgm::CurrentChoice(int32_t& out)const{
    if(!data_||!data_->Current()||!data_->state.selected)return false;out=data_->state.stored4BC;return true;
}
GameSceneBgmSnapshot GameSceneBgm::Snapshot()const{
    if(!data_)return {};auto s=data_->state;s.current=data_->Current();if(s.selected&&!s.source_scene_retired){s.file=data_->files.Read(uint32_t(s.stored4BC));
        if(s.file.previous_request)s.previous_file=data_->files.ReadRequest(uint32_t(s.stored4BC),s.file.previous_request);}return s;
}
std::weak_ptr<const void> GameSceneBgm::Lifetime()const{return data_?data_->lifetime:std::weak_ptr<const void>{};}
GameFsStatus GameSceneBgm::Close(){
    if(!data_)return {};data_->closing=true;auto status=data_->files.Close();if(!status)return status;data_.reset();return {};
}
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
namespace {
std::string SupportText(const PackageView& view){
    const auto n=view.Describe().size;Need(n&&n<=256,"support text input extent");std::string s;
    for(uint64_t i=0;i<n;++i){const auto c=view.ReadU8(i);if(!c){Need(i+1==n,"support text trailing bytes");return s;}s+=char(c);}
    throw std::runtime_error("support text missing terminator");
}
std::vector<std::string> SupportUnits(const std::string& text){
    std::vector<std::string> out;
    for(size_t i=0;i<text.size();){const auto c=uint8_t(text[i]);Need(c,"support embedded NUL");
        if(c>=128){Need(i+1<text.size()&&text[i+1],"support truncated source byte pair");out.push_back(text.substr(i,2));i+=2;}
        else{if(c>32)out.push_back(text.substr(i,1));++i;}}
    return out;
}
std::string SupportGlyphPath(const std::string& format,GameSupportGlyph glyph){
    std::string path;unsigned slot=0;
    for(size_t i=0;i<format.size();){
        if(format[i]!='%'){const auto c=uint8_t(format[i++]);Need(c>=33&&c<127&&c!=':',"support glyph path literal");path+=char(c);continue;}
        Need(slot<3&&i+4<=format.size()&&format[i+1]=='0'&&(format[i+2]=='3'||format[i+2]=='5')&&format[i+3]=='d',"unimplemented glyph path format");
        const auto width=size_t(format[i+2]-'0');const int value=slot<2?int(glyph.style):int(glyph.code);++slot;i+=4;
        auto digits=std::to_string(value<0?-value:value);const size_t sign=value<0?1:0;
        if(digits.size()+sign<width)digits.insert(0,width-digits.size()-sign,'0');if(sign)path+='-';path+=digits;
    }
    Need(slot==3&&!path.empty()&&path.size()<256&&path[0]!='/'&&uint8_t(path[0])!=92&&path.find("..")==path.npos,"support glyph path extent/domain");return path;
}
void SupportGlyphs(std::vector<GameSupportGlyph>& queue,bool& capped,const std::string& text,int32_t style){
    // 28AF0 stores a halfword;298A4 loads it signed. Preserve
    // styles>=256 (whole-text mode2), rather than aliasing their low byte.
    const auto low=uint32_t(style)&65535;const auto signed_style=int16_t(low<32768?int(low):int(low)-65536);
    for(size_t i=0;i<text.size();){const auto c=uint8_t(text[i++]);uint16_t code=c;
        if(c>=128){Need(i<text.size()&&text[i],"support glyph truncated byte pair");code=uint16_t((uint16_t(c)<<8)|uint8_t(text[i++]));}
        bool found=false;for(const auto& q:queue)found|=q.code==code&&q.style==signed_style;
        if(found)continue;if(queue.size()==64){capped=true;break;}queue.push_back({code,signed_style});
    }
}
}
struct GameSupportRegistration::State {
    std::weak_ptr<const void> manager;
    std::weak_ptr<script::NativeScriptContext> context;
    std::weak_ptr<std::vector<script::Value>> frame_owner;
    script::Table table{};uint16_t index=0;GameSupportRegistrationSnapshot record;
    script::Value Reference(unsigned offset=0)const{
        Need(uint32_t(index)+offset<=UINT16_MAX,"support registered reference extent");script::Value ref;ref.kind=script::Value::Kind::Reference;
        ref.table=table;ref.index=uint16_t(index+offset);ref.frame_owner=frame_owner;ref.context_owner=context;return ref;
    }
    bool Current(script::Vm& vm)const{
        try{const auto w=vm.current_witness();if(manager.expired()||!w.valid||w.stopped||w.run_id!=record.run||w.code_sha256!=record.module||!w.context_registered||context.expired())return false;
            if(vm.context_value().context_owner.lock()!=context.lock())return false;
            if(table==script::Table::Frame&&!vm.reference_belongs_to_frame(Reference(),record.frame)&&!vm.reference_belongs_to_event_caller(Reference(),record.frame))return false;
            (void)vm.peek_reference(Reference());return true;
        }catch(...){return false;}
    }
};
GameFsStatus GameSupportRegistration::Initialize(std::shared_ptr<const void> manager,uint64_t epoch){
    if(!manager||!epoch||!manager_.expired()||binding_)return {GameFsCode::Unavailable,0,"support manager constructor/epoch"};manager_=manager;epoch_=epoch;return {};
}
GameFsStatus GameSupportRegistration::Register16(script::Vm& vm,const script::Value& ref){
    try{using V=script::Value;Need(!manager_.expired()&&epoch_&&ref.kind==V::Kind::Reference,"support actual GAME16 manager/reference required");
        Need(ref.table==script::Table::Global||ref.table==script::Table::Local||ref.table==script::Table::Ctx28||ref.table==script::Table::Frame,"unimplemented support registration table");
        const auto before=vm.current_witness();Need(before.valid&&before.run_id&&!before.stopped,"support actual current module/run");
        const auto ctx=vm.context_value();const auto w=vm.current_witness();Need(vm.stable_value_owner(ref)||ref.table==script::Table::Frame,"support foreign context reference");
        const std::weak_ptr<script::NativeScriptContext> empty;
        if(ref.context_owner.owner_before(empty)||empty.owner_before(ref.context_owner))
            Need(ref.context_owner.lock()==ctx.context_owner.lock(),"support foreign/retired reference owner");
        uint64_t frame=0;if(ref.table==script::Table::Frame){if(vm.reference_belongs_to_frame(ref,w.frame_id))frame=w.frame_id;
            else if(vm.reference_belongs_to_frame(ref,w.caller_frame_id))frame=w.caller_frame_id;Need(frame,"unimplemented foreign/noncurrent frame support registration");}
        (void)vm.peek_reference(ref);auto b=std::make_shared<State>();b->manager=manager_;b->context=ctx.context_owner;b->frame_owner=ref.frame_owner;b->table=ref.table;b->index=ref.index;
        b->record={true,true,epoch_,++serial_,w.run_id,frame,uint32_t(ref.table),ref.index,w.current_pc,w.code_sha256};binding_=std::move(b);return {};
    }catch(const std::exception& e){return Error(e);}
}
bool GameSupportRegistration::Current(script::Vm& vm)const{return binding_&&binding_->Current(vm);}
GameSupportRegistrationSnapshot GameSupportRegistration::Snapshot()const{
    auto out=binding_?binding_->record:GameSupportRegistrationSnapshot{};out.live=!manager_.expired();out.manager_epoch=epoch_;out.registered=out.registered&&out.live;return out;
}
void GameSupportRegistration::Close(){binding_.reset();manager_.reset();epoch_=0;}
script::Value GameSupportTextOwner::Cell(script::Vm& vm,unsigned offset)const{
    if(state_.source_current_bound){const auto b=registration_.lock();Need(b&&b->Current(vm),"support registration replaced/retired");return vm.peek_reference(b->Reference(offset));}
    return vm.read_slot(table_,uint16_t(index_+offset));
}
GameFsStatus GameSupportTextOwner::Initialize(std::shared_ptr<const void> owner,uint64_t epoch,script::Vm& vm,const script::Value& context,script::Table table,uint16_t index,PackageView module,const GameSupportTextMap& map){
    if(state_.live||state_.failed)return {GameFsCode::Unavailable,0,"support owner already live/failed"};registration_.reset();return InitializeImpl(std::move(owner),epoch,vm,context,table,index,std::move(module),map);
}
GameFsStatus GameSupportTextOwner::InitializeRegistered(std::shared_ptr<const void> owner,uint64_t epoch,script::Vm& vm,const GameSupportRegistration& input,PackageView module,const GameSupportTextMap& map){
    if(state_.live||state_.failed||!input.Current(vm))return {GameFsCode::Unavailable,0,"support current GAME16 binding required"};
    registration_=input.binding_;const auto b=input.binding_;return InitializeImpl(std::move(owner),epoch,vm,vm.context_value(),b->table,b->index,std::move(module),map);
}
GameSupportTextOwner::Atom GameSupportTextOwner::Capture(script::Vm& vm,const script::Value& v)const{
    Atom a;a.kind=uint32_t(v.kind);a.word=v.word;
    if(v.kind==script::Value::Kind::String){a.text=v.cp932;if(v.owned_string.bound()){
        const auto r=vm.owned_string_receipt(v);Need(r.live,"support owned string retired");a.owner=r.owner;a.epoch=r.epoch;a.serial=r.serial;}}
    return a;
}
bool GameSupportTextOwner::InputsCurrent()const{
    try{for(const auto& r:inputs_)if(r.view.InitializedDigest()!=r.digest)return false;
        for(const auto& a:templates_)if(!a->SourceCurrent())return false;
        for(const auto& image:glyph_images_)if(!image->SourceCurrent())return false;return true;}catch(...){return false;}
}
bool GameSupportTextOwner::Current(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm)const{
    try{const auto held=owner_.lock();const auto w=vm.current_witness();
        if(!state_.live||state_.failed||state_.converted!=8||!held||held!=owner||held.owner_before(owner)||owner.owner_before(held)
           ||epoch!=state_.epoch||!w.valid||!w.context_registered||w.run_id!=state_.run||w.code_sha256!=module_sha_
           ||(!state_.source_current_bound&&table_==script::Table::Frame&&w.frame_id!=frame_)||!InputsCurrent())return false;
        const auto expected=context_.lock();if(!expected||vm.context_value().context_owner.lock()!=expected)return false;
        for(unsigned i=0;i<9;++i)if(!(Capture(vm,Cell(vm,i))==atoms_[i]))return false;
        return true;
    }catch(...){return false;}
}
GameFsStatus GameSupportTextOwner::InitializeImpl(std::shared_ptr<const void> owner,uint64_t epoch,script::Vm& vm,
        const script::Value& context,script::Table table,uint16_t index,PackageView module,const GameSupportTextMap& map){
    using V=script::Value;
    if(state_.live||state_.failed||!owner||epoch<=last_epoch_||index>UINT16_MAX-8)
        return {GameFsCode::Unavailable,0,"support consumer owner/epoch/extent"};
    try{const auto w=vm.current_witness();
        Need(w.valid&&w.context_registered&&w.run_id&&context.kind==V::Kind::Context,"support existing current context required");
        const auto ctx=context.context_owner.lock(),current=vm.context_value().context_owner.lock();
        Need(ctx&&ctx==current,"support foreign context");
        Need(table==script::Table::Global||table==script::Table::Local||table==script::Table::Ctx28||table==script::Table::Frame,"support current table domain");
        const auto sha=core::hex_digest(module.InitializedDigest());Need(sha==std::string(w.code_sha256.data()),"support module/VM identity mismatch");
        state_={};state_.live=true;state_.epoch=epoch;state_.run=w.run_id;last_epoch_=epoch;owner_=owner;context_=ctx;
        table_=table;index_=index;frame_=w.frame_id;module_sha_=w.code_sha256;
        if(const auto b=registration_.lock()){Need(b->Current(vm),"support registration expired");state_.source_current_bound=true;state_.source_registration=b->record.serial;state_.manager_epoch=b->record.manager_epoch;}
        const auto remember=[&](const PackageView& v){inputs_.push_back({v,v.InitializedDigest()});};remember(module);
        Need(map.keys.Describe().size==96,"support exact96-key map required");remember(map.keys);
        for(unsigned i=0;i<96;++i){keys_[i]=map.keys.ReadU8(i);remember(map.expansions[i]);expansions_[i]=SupportText(map.expansions[i]);}
        remember(map.fallback);fallback_=SupportText(map.fallback);remember(map.integer_format);integer_format_=SupportText(map.integer_format);
        const auto header=Cell(vm,0);atoms_[0]=Capture(vm,header);
        Need(header.kind==V::Kind::ArrayHeader&&header.word>>28==9&&(header.word&4095)>=8,"support current eight-cell header required");
        // Actual sequential conversion after256B clear. Failure retains only
        // completed cells in diagnostic Progress; it never publishes success.
        for(unsigned i=0;i<8;++i){const auto value=Cell(vm,i+1);atoms_[i+1]=Capture(vm,value);std::string text;
            if(value.kind==V::Kind::String&&(value.word>>28==5||value.word>>28==6))text=value.cp932;
            else if(value.kind==V::Kind::Scalar&&value.word>>28<2)text=fallback_;
            else if(value.kind==V::Kind::Scalar&&value.word>>28==2){Need(integer_format_=="%d","unimplemented support numeric format");text=std::to_string(value.signed_integer());}
            else throw std::runtime_error("unimplemented support75778 value domain");
            Need(text.find('\0')==std::string::npos,"support value embedded NUL");std::string normalized;
            for(size_t at=0;at<text.size();){const auto c=uint8_t(text[at]);std::string append;
                // Source tests signed byte, NOT a generic CP932 lead table.
                if(c>=128){Need(at+1<text.size()&&text[at+1],"support truncated source byte pair");append=text.substr(at,2);at+=2;}
                else{for(unsigned k=0;k<96;++k)if(keys_[k]==c){append=expansions_[k];break;}++at;}
                Need(normalized.size()+append.size()<=31,"support Windows32-byte cell overflow");normalized+=append;
            }
            std::copy(normalized.begin(),normalized.end(),state_.cells[i].begin());++state_.converted;
        }
        Need(Current(owner,epoch,vm),"support input changed during conversion");return {};
    }catch(const std::exception& e){if(state_.live)state_.failed=true;return Error(e);}
}
GameFsStatus GameSupportTextOwner::LoadTemplates(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm,
        GameResourceSource& source,const GameSupportTemplatePaths& paths){
    if(!Current(owner,epoch,vm)||state_.templates_loaded)return {GameFsCode::Unavailable,0,"support template current owner/replay"};
    try{
        const auto input=[&](const PackageView& v){inputs_.push_back({v,v.InitializedDigest()});return SupportText(v);};
        const auto safe=[](const std::string& s){return !s.empty()&&s.find(':')==s.npos&&s[0]!='/'&&s[0]!='\\'&&s.find("..") ==s.npos;};
        const auto prefix=input(paths.textures);Need(safe(prefix),"support texture prefix");
        struct CaptureSource:GameResourceSource {
            GameResourceSource& source;std::vector<Input>& inputs;
            CaptureSource(GameResourceSource& s,std::vector<Input>& i):source(s),inputs(i){}
            GameFsStatus OpenResource(const std::string& p,PackageView& out)override{
                const auto status=source.OpenResource(p,out);if(status)inputs.push_back({out,out.InitializedDigest()});return status;
            }
        } capture(source,inputs_);
        for(unsigned i=0;i<18;++i){const auto path=input(paths.models[i]);Need(safe(path),"support model path");
            auto asset=std::make_shared<GameModelAsset>();const auto status=asset->Load(capture,path,prefix);
            if(!status){state_.failed=true;return status;}templates_.push_back(std::move(asset));++state_.loaded;
        }
        Need(Current(owner,epoch,vm),"support template input drift");state_.templates_loaded=true;return {};
    }catch(const std::exception& e){state_.failed=true;return Error(e);}
}
GameFsStatus GameSupportTextOwner::Attach(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm,
        std::shared_ptr<const GameModelClone> parent){
    if(!Current(owner,epoch,vm)||!state_.templates_loaded||state_.attached||!parent||!parent->Asset()||!parent->Asset()->SourceCurrent())
        return {GameFsCode::Unavailable,0,"support actual parent/templates unavailable"};
    try{std::vector<GameAreaMarkerState> markers;auto status=parent->AreaMarkers(*parent->Asset(),markers);if(!status)return status;
        Need(markers.size()<=64,"support marker source64 bound");parent_=parent;parent_generation_=parent->Generation();
        for(const auto& marker:markers){Need(marker.parameters.field208>=1&&marker.parameters.field208<=8,"support marker text-cell key");
            const uint32_t cell=marker.parameters.field208-1;const std::string text(reinterpret_cast<const char*>(state_.cells[cell].data()));
            const auto units=SupportUnits(text);const auto count=uint32_t((std::min)(units.size(),size_t(8)));if(!count)continue;
            const auto mode=marker.parameters.field20C;const uint32_t model=mode==1?count+1:mode==2?count+9:(mode>=3&&mode<=10?0:1);
            GameModelClone child;status=child.Initialize(templates_.at(model));if(status)status=child.Update(marker.world);
            if(!status){state_.failed=true;return status;}
            children_.push_back(std::move(child));state_.placements.push_back({marker.node,cell,count,model,mode,marker.world});
        }
        state_.attached=true;return Update(owner,epoch,vm);
    }catch(const std::exception& e){state_.failed=true;return Error(e);}
}
GameFsStatus GameSupportTextOwner::Update(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm){
    const auto parent=parent_.lock();
    if(!Current(owner,epoch,vm)||!state_.attached||!parent||parent->Generation()!=parent_generation_||!parent->Asset()||!parent->Asset()->SourceCurrent())
        return {GameFsCode::Unavailable,0,"support current parent/data retired"};
    try{std::vector<GameAreaMarkerState> markers;auto status=parent->AreaMarkers(*parent->Asset(),markers);if(!status)return status;
        for(size_t i=0;i<state_.placements.size();++i){auto& bind=state_.placements[i];
            const auto found=std::find_if(markers.begin(),markers.end(),[&](const GameAreaMarkerState& m){return m.node==bind.node;});
            Need(found!=markers.end()&&found->parameters.field208==bind.text_cell+1&&found->parameters.field20C==bind.mode,"support marker alias changed");
            status=children_[i].Update(found->world);if(!status){state_.failed=true;return status;}bind.parent_world=found->world;
            const std::string text(reinterpret_cast<const char*>(state_.cells[bind.text_cell].data()));const auto units=SupportUnits(text);
            std::string request;int32_t style=int32_t(found->parameters.field214)-1;
            if(bind.mode==1||bind.mode==2){request=text;if(bind.mode==2)style=int32_t(found->parameters.field214)+499;}
            else{const int32_t at=int32_t(bind.mode)-(bind.mode>=3&&bind.mode<=10?3:11);
                if(!(bind.mode>=3&&bind.mode<=10))style=int32_t(found->parameters.field214)+499;
                if(at>=0&&size_t(at)<units.size())request=units[size_t(at)];}
            SupportGlyphs(state_.glyph_requests,state_.glyph_capacity_reached,request,style);
        }
        Need(Current(owner,epoch,vm),"support data changed during update");return {};
    }catch(const std::exception& e){state_.failed=true;return Error(e);}
}
GameFsStatus GameSupportTextOwner::Read(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm,GameSupportSnapshot& out)const{
    if(!Current(owner,epoch,vm))return {GameFsCode::Unavailable,0,"support current readback unavailable"};
    if(state_.attached){const auto parent=parent_.lock();if(!parent||parent->Generation()!=parent_generation_||!parent->Asset()||!parent->Asset()->SourceCurrent())return {GameFsCode::Unavailable,0,"support parent retired"};
        std::vector<GameAreaMarkerState> markers;if(!parent->AreaMarkers(*parent->Asset(),markers))return {GameFsCode::Unavailable,0,"support parent pose stale"};
        for(const auto& b:state_.placements){const auto m=std::find_if(markers.begin(),markers.end(),[&](const GameAreaMarkerState& m){return m.node==b.node;});
            if(m==markers.end()||m->world!=b.parent_world)return {GameFsCode::Unavailable,0,"support child update required"};}}
    if(state_.glyph_pixels_ready){uint32_t count=0;for(const auto& child:children_){if(!child.DiffuseTexturesCurrent())return {GameFsCode::Unavailable,0,"support glyph binding retired"};count+=child.DiffuseTextureCount();}
        if(count!=state_.bound_texture_nodes)return {GameFsCode::Unavailable,0,"support glyph binding drift"};}
    out=state_;return {};
}
GameFsStatus GameSupportTextOwner::PrepareGlyphTextures(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm,GameResourceSource& source,PackageView format){
    GameSupportSnapshot check;
    if(!Read(owner,epoch,vm,check)||!state_.attached||state_.glyph_pixels_ready)
        return {GameFsCode::Unavailable,0,"support glyph current owner/replay"};
    try{
        const auto held_parent=parent_.lock();Need(held_parent&&held_parent->Asset(),"support glyph parent retired");
        inputs_.push_back({format,format.InitializedDigest()});const auto pattern=SupportText(format);(void)SupportGlyphPath(pattern,{});
        for(const auto glyph:state_.glyph_requests){
            const auto path=SupportGlyphPath(pattern,glyph);PackageView bytes;auto status=source.OpenResource(path,bytes);
            if(!status){state_.failed=true;return status;}
            Need(bytes.Describe().size<=8*1024*1024,"support glyph encoded budget");inputs_.push_back({bytes,bytes.InitializedDigest()});
            auto image=std::make_shared<GameImage>();status=image->DecodeGim(bytes);if(!status){state_.failed=true;return status;}
            Need(image->Width()<=512&&image->Height()<=512,"support native glyph texture budget");
            glyph_images_.push_back(image);++state_.glyph_decoded;
            state_.textures.push_back({glyph,image->Width(),image->Height(),image->Generation(),core::sha256(reinterpret_cast<const uint8_t*>(image->Pixels().data()),image->Pixels().size()*sizeof(uint32_t))});
        }
        // WindowsSynchronousGlyphResources: stage every decoded image and all
        // per-clone bindings before publication. No async poll/ready provider.
        const auto parent=parent_.lock();Need(parent&&bool(Read(owner,epoch,vm,check)),"support glyph parent retired before staging");
        auto staged=children_;std::vector<bool> visible(staged.size(),false);uint32_t bound=0;
        for(size_t i=0;i<staged.size();++i){
            const auto& bind=state_.placements[i];const auto& params=parent->Asset()->Data().nodes[bind.node].areaMarker;
            const std::string text(reinterpret_cast<const char*>(state_.cells[bind.text_cell].data()));const auto units=SupportUnits(text);
            int32_t style=int32_t(params.field214)-1;std::vector<std::string> selected;
            if(bind.mode==1||bind.mode==2){if(bind.mode==2)style=int32_t(params.field214)+499;
                selected.assign(units.begin(),units.begin()+(std::min)(units.size(),size_t(8)));}
            else{const bool first=bind.mode>=3&&bind.mode<=10;const auto at=int32_t(bind.mode)-(first?3:11);if(!first)style=int32_t(params.field214)+499;
                if(at>=0&&size_t(at)<units.size())selected.push_back(units[size_t(at)]);}
            // Source29444 disables a clone when the selected unit is absent.
            if(selected.empty())continue;
            std::vector<uint32_t> nodes;const auto* asset=staged[i].Asset();Need(asset,"support glyph clone retired");
            for(size_t n=0;n<asset->Data().nodes.size();++n){const auto& node=asset->Data().nodes[n];if(node.type==1||node.hasGeometry){
                Need(node.type==1&&node.hasGeometry&&node.geometry.vertexCount,"unimplemented support glyph geometry type/empty node");nodes.push_back(uint32_t(n));}}
            // Refuse extra/missing geometry instead of guessing a glyph slot,
            // background texture, UV layout or one-past source pointer domain.
            Need(nodes.size()==selected.size(),"unimplemented support glyph node/unit cardinality");
            std::vector<std::pair<uint32_t,std::shared_ptr<const GameImage>>> textures;
            for(size_t n=0;n<selected.size();++n){std::vector<GameSupportGlyph> key;bool capped=false;SupportGlyphs(key,capped,selected[n],style);
                Need(key.size()==1&&!capped,"support selected raw glyph unit");size_t at=0;
                for(;at<state_.glyph_requests.size();++at)if(state_.glyph_requests[at].code==key[0].code&&state_.glyph_requests[at].style==key[0].style)break;
                Need(at<glyph_images_.size(),"support selected glyph absent from decoded queue");textures.emplace_back(nodes[n],glyph_images_[at]);}
            const auto status=staged[i].BindGlyphTextures(staged[i].Generation(),parent,bind.node,textures);if(!status){state_.failed=true;return status;}
            bound+=uint32_t(textures.size());visible[i]=true;
        }
        Need(bool(Read(owner,epoch,vm,check)),"support glyph owner/parent/input changed before publication");
        children_=std::move(staged);glyph_visible_=std::move(visible);state_.bound_texture_nodes=bound;state_.glyph_pixels_ready=true;return {};
    }catch(const std::exception& e){state_.failed=true;return Error(e);}
}
GameFsStatus GameSupportTextOwner::DrawGlyphPreview(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm,GameGraphics& graphics,const GameMatrix& camera)const{
    if(!state_.glyph_pixels_ready)return {GameFsCode::Unavailable,0,"support glyph pixels not bound"};return DrawPreview(owner,epoch,vm,graphics,camera);
}
GameFsStatus GameSupportTextOwner::DrawPreview(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm,
        GameGraphics& graphics,const GameMatrix& camera)const{
    GameSupportSnapshot check;const auto valid=Read(owner,epoch,vm,check);if(!valid||!state_.attached)return {GameFsCode::Unavailable,0,"support preview unavailable"};
    for(size_t i=0;i<children_.size();++i){if(state_.glyph_pixels_ready&&!glyph_visible_[i])continue;const auto status=children_[i].Draw(graphics,camera);if(!status)return status;}return {};
}
void GameSupportTextOwner::Close(){
    for(auto& c:children_)c.Close();children_.clear();glyph_images_.clear();glyph_visible_.clear();parent_.reset();templates_.clear();inputs_.clear();registration_.reset();owner_.reset();context_.reset();state_={};
}
GameFsStatus GameBackgroundRuntime::Initialize(Clock clock,RngNativeOwner& rng){
    if(identity_||!clock||!rng.Available()||!rng.Lineage())
        return {GameFsCode::Unavailable,0,"background live runtime producer unavailable/replaced"};
    const auto seed=rng.Snapshot();
    if(!seed.scenario_identity||seed.provenance.empty()||seed.origin==RngSeedOrigin::InitialImageModel)
        return {GameFsCode::Unavailable,0,"background requires current initialized RNG lineage"};
    static std::atomic<uint64_t> next{0};identity_=++next;clock_=std::move(clock);rng_=&rng;rng_lifetime_=rng.Lifetime();lineage_=rng.Lineage();return {};
}
bool GameBackgroundRuntime::Live()const noexcept {
    return identity_&&clock_&&rng_&&!rng_lifetime_.expired()&&rng_->Available()&&rng_->Lineage()==lineage_;
}
GameFsStatus GameBackgroundRuntime::Read(std::array<uint32_t,7>& out){
    if(!Live())return {GameFsCode::Unavailable,0,"background runtime retired/reseeded"};
    try{const auto read=clock_;const auto value=read();
        Need(Live(),"background producer changed during read");
        Need(value[5]>=1&&value[5]<=12&&value[4]>=1&&value[4]<=31
             &&value[3]<24&&value[2]<60&&value[1]<60,"background current civil date/time unavailable");
        out=value;++reads_;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
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
bool GameBackgroundPreparation::EffectsCurrent()const {
    if(!RuntimeCurrent())return false;
    if(!state_.effects_prepared)return true;
    try{
        if(!light_motions_.empty()&&light_format_.InitializedDigest()!=light_format_digest_)return false;
        for(const auto& motion:light_motions_)if(!motion.Live())return false;
        return light_motions_.size()==light_playbacks_.size();
    }catch(...){return false;}
}
bool GameBackgroundPreparation::RuntimeCurrent()const {
    if(!BgmCurrent())return false;
    if(!activated_)return true;
    const auto runtime=runtime_.lock();return runtime&&runtime->Live()&&runtime->Identity()==applied_.runtime;
}
bool GameBackgroundPreparation::BgmCurrent()const {
    if(!state_.bgm_attempted)return true;
    const auto runtime=bgm_runtime_.lock();const auto owner=owner_.lock();
    const std::shared_ptr<const void> parent=runtime;
    if(!runtime||!runtime->Live()||!owner||parent!=owner||parent.owner_before(owner)||owner.owner_before(parent))return false;
    if(scene_bgm_){
        if(scene_bgm_lifetime_.expired()||!scene_bgm_->data_||!scene_bgm_->data_->Bound(runtime))return false;
        const auto& data=*scene_bgm_->data_;return data.last_job_request==state_.request&&state_.bgm_requested
            &&data.state.cache_revision==scene_bgm_cache_revision_&&data.state.period4B4==state_.period&&data.state.variant4AC==state_.variant
            &&data.state.stored4BC==state_.bgm_choice&&data.files.Read(uint32_t(state_.bgm_choice)).current;
    }
    try{for(unsigned i=0;i<2;++i)if(bgm_inputs_[i].InitializedDigest()!=bgm_hashes_[i])return false;
        return state_.bgm_requested&&bgm_files_.Read(uint32_t(state_.bgm_choice)).current;
    }catch(...){return false;}
}
GameFsStatus GameBackgroundPreparation::RequestBgm(const std::shared_ptr<const void>& owner,uint64_t epoch,
    const std::shared_ptr<GameBackgroundRuntime>& runtime,PackageView previous,PackageView lists,
    PackageView root,const std::function<PackageView(uint32_t)>& names,GameBgmFileQueue::Factory factory){
    const std::shared_ptr<const void> parent=runtime;
    if(!Current(owner,epoch)||!state_.pending||state_.failed||state_.bgm_attempted||!RegistrationsCurrent()
       ||!runtime||!runtime->Live()||parent!=owner||parent.owner_before(owner)||owner.owner_before(parent)||!names||!factory)
        return {GameFsCode::Unavailable,0,"BGM must use current pending job/clock/RNG before5F58"};
    try{
        Need(previous.Describe().size==4,"BGM requires current old4BC typed word, not an implicit default");
        bgm_inputs_={{previous,lists}};for(unsigned i=0;i<2;++i)bgm_hashes_[i]=bgm_inputs_[i].InitializedDigest();
        auto status=bgm_files_.Initialize(owner,epoch,std::move(factory));if(!status)return status;
        bgm_runtime_=runtime;
        state_.bgm_attempted=true;
        status=SelectBgm19B6C(state_.period,int32_t(previous.ReadLE32(0)),lists,*runtime->rng_,state_.bgm_selection);
        if(!status)return status;
        state_.bgm_choice=int32_t(state_.bgm_selection.index); // source19E24 store, BEFORE file request
        status=bgm_files_.Request(owner,epoch,uint32_t(state_.bgm_choice),root,names(uint32_t(state_.bgm_choice)),state_.bgm_result);
        if(!status)return status; // unknown provider is not sourceI/O failure
        state_.bgm_requested=true;
        //19DD4 ignores wrapper_return, including genuine queue refusal. Do
        // not gate background initialization on all-audio-success/registration.
        return {};
    }catch(const std::exception& e){return Error(e);}
}
GameFsStatus GameBackgroundPreparation::PumpBgm(const std::shared_ptr<const void>& owner,uint64_t epoch){
    if(!Current(owner,epoch)||!BgmCurrent())return {GameFsCode::Unavailable,0,"BGM current job/input owner required"};
    if(scene_bgm_)return scene_bgm_->Pump(bgm_runtime_.lock());
    return bgm_files_.Pump(owner,epoch);
}
GameBgmFileRecord GameBackgroundPreparation::BgmFile()const {
    if(state_.bgm_choice<0)return {};
    if(scene_bgm_){if(scene_bgm_lifetime_.expired()||!BgmCurrent())return {};return scene_bgm_->data_->files.Read(uint32_t(state_.bgm_choice));}
    auto value=bgm_files_.Read(uint32_t(state_.bgm_choice));value.current=value.current&&state_.live&&BgmCurrent();return value;
}
GameFsStatus GameBackgroundPreparation::RequestBgm(const std::shared_ptr<const void>& owner,uint64_t epoch,
    const std::shared_ptr<GameBackgroundRuntime>& runtime,GameSceneBgm& current){
    const std::shared_ptr<const void> parent=runtime;
    if(!Current(owner,epoch)||!state_.pending||state_.failed||state_.bgm_attempted||!RegistrationsCurrent()
       ||!runtime||parent!=owner||parent.owner_before(owner)||owner.owner_before(parent)||!current.data_||!current.data_->Bound(runtime))
        return {GameFsCode::Unavailable,0,"target BGM requires SAME initialized module/scene owner"};
    auto& data=*current.data_;auto& stored=data.state;
    if(!stored.requested||!stored.selected||!stored.cache_refreshed||stored.cache_revision<=data.last_cache_consumed
       ||stored.period4B4!=state_.period||stored.variant4AC!=state_.variant||data.last_job_request>=state_.request)
        return {GameFsCode::Unavailable,0,"target BGM current stored4B4/4BC/request lineage unavailable"};
    bgm_runtime_=runtime;scene_bgm_=&current;scene_bgm_lifetime_=data.lifetime;scene_bgm_cache_revision_=stored.cache_revision;
    state_.bgm_attempted=true;data.last_job_request=state_.request;data.last_cache_consumed=stored.cache_revision;
    auto status=SelectBgm19B6C(stored.period4B4,stored.stored4BC,data.inputs[0],*runtime->rng_,state_.bgm_selection);
    if(!status)return status;
    if(!data.Bound(runtime))return {GameFsCode::Unavailable,0,"current BGM/LoadMan retired during selection; RNG effects retained"};
    const auto cell=data.manager_selection.lock();
    if(data.selection_bound&&!cell)return {GameFsCode::Unavailable,0,"current LoadMan selection cell retired after RNG"};
    stored.selection=state_.bgm_selection;stored.stored4BC=int32_t(stored.selection.index);
    stored.source_scene_retired=false;stored.selection_writer=0x19E20;
    if(cell)*cell=stored.stored4BC; // SAME owned manager field, at source store BEFORE file/provider
    stored.requested=false;state_.bgm_choice=stored.stored4BC;
    try{status=data.files.Request(runtime,stored.epoch,stored.selection.index,data.inputs[2],data.names(stored.selection.index),state_.bgm_result);}
    catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
    if(status&&!data.Bound(runtime))return {GameFsCode::Unavailable,0,"target BGM producer changed during request"};
    if(status){stored.file_result=state_.bgm_result;stored.requested=true;state_.bgm_requested=true;}return status;
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
    if(!Current(owner,epoch)||!state_.pending||state_.staged||state_.failed||!BgmCurrent())
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
        auto clone=std::make_shared<GameModelClone>();status=clone->Initialize(asset);
        if(!status){state_.failed=true;state_.pending=false;state_.status=status;return status;}
        status=clone->Update(ModelIdentity());
        if(!status){state_.failed=true;state_.pending=false;state_.status=status;return status;}
        std::vector<GameAreaMarkerState> markers;
        status=clone->AreaMarkers(*asset,markers);
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
    if(!Current(owner,epoch)||!state_.staged||state_.failed||!RegistrationsCurrent()||!MotionCountsCurrent()||!AuxiliaryCurrent()||!EffectsCurrent()||!asset_||!asset_->Live())
        return {GameFsCode::Unavailable,0,"background staged draw owner retired/changed"};
    // Explicit component/offline view, never an ordinary game scene publish.
    if(support_prepared_)return {GameFsCode::Unavailable,0,"current VM support consumer required; use DrawWithSupport"};
    return clone_->Draw(graphics,camera,GameModelColorPolicy::WindowsDvsTexturedModulation2x);
}
GameFsStatus GameBackgroundPreparation::AreaMarkers(const std::shared_ptr<const void>& owner,uint64_t epoch,
        std::vector<GameAreaMarkerState>& out)const {
    if(!Current(owner,epoch)||!state_.staged||state_.failed||!RegistrationsCurrent()||!MotionCountsCurrent()||!AuxiliaryCurrent()||!EffectsCurrent()||!asset_)
        return {GameFsCode::Unavailable,0,"background marker-list owner unavailable"};
    return clone_->AreaMarkers(*asset_,out);
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
       ||!RegistrationsCurrent()||!MotionCountsCurrent()||!AuxiliaryCurrent()||!EffectsCurrent()||!asset_||!asset_->Live())
        return {GameFsCode::Unavailable,0,"background auxiliary readback unavailable"};
    std::vector<GameAuxiliaryBindingState> result;
    for(const auto& b:auxiliary_bindings_){
        const auto& entry=auxiliary_entries_.at(b.entry);
        const auto* target=b.target==GameAuxiliaryTarget::Child?entry.child.get():
                           b.target==GameAuxiliaryTarget::MainTemplate?&main_template_clone_:clone_.get();
        if(!target||!target->Asset()||!target->Asset()->Live())
            return {GameFsCode::Unavailable,0,"background auxiliary target retired"};
        result.push_back({b.entry,b.slot,entry.words[b.slot],b.target,b.playback.Snapshot(),
                          entry.motions[b.slot]->Statistics(),target->Asset()->Statistics()});
    }
    out=std::move(result);return {};
}
GameFsStatus GameBackgroundPreparation::PrepareEffects(const std::shared_ptr<const void>& owner,uint64_t epoch,
        GameBackgroundEffectsPolicy policy,PackageView format) {
    if(!Current(owner,epoch)||state_.failed||!state_.staged||state_.effects_prepared
       ||!state_.model_motions_staged||!state_.auxiliary_staged||!RegistrationsCurrent()
       ||!MotionCountsCurrent()||!AuxiliaryCurrent()||!asset_||!asset_->Live())
        return {GameFsCode::Unavailable,0,"background effects owner/prerequisites unavailable"};
    // No recipe/count is invented for PH1 row2D8->1316C. The strict source
    // path refuses before I/O or mutations. The Windows policy omits only
    // this model-motion layer, not actor/script/job/scene completion effects.
    if(policy!=GameBackgroundEffectsPolicy::WindowsReducedBackgroundEffects)
        return {GameFsCode::Unavailable,0,"source5448 current recipe unbound"};
    try{
        std::vector<GameModelMotion> motions;
        std::vector<GameModelPlayback> controls;
        std::array<uint8_t,32> digest{};
        // 5610 tests signed byte for zero, not for positivity. The SAME owned
        // count image was consumed by52AC; no independently supplied flag.
        if(model_motion_counts_.ReadU8(state_.variant*2)!=0){
            const auto bytes=format.Describe().size;Need(bytes>1&&bytes<=96,"light path format extent");
            digest=format.InitializedDigest();std::string text;
            for(uint64_t i=0;i<bytes;++i){const auto c=format.ReadU8(i);
                if(!c){Need(i+1==bytes,"light format trailing bytes");break;}
                Need(c>=32&&c<127&&i+1<bytes,"light format encoding/termination");text+=char(c);
            }
            const std::string prefix_format="%s\\%s\\";
            Need(text.compare(0,prefix_format.size(),prefix_format)==0,"light path prefix format");
            const auto name=text.substr(prefix_format.size());const auto marker=name.find("%02d");
            Need(marker!=std::string::npos&&name.find('%',marker+1)==std::string::npos
                 &&name.find('%')==marker,"light path number format");
            const auto safe=[](const std::string& s){for(auto c:s)
                if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
                return true;};
            const auto before=name.substr(0,marker),after=name.substr(marker+4);
            Need(safe(before)&&safe(after),"unsafe light path filename");
            BackgroundPackageSource resources(package_,state_.registration);
            const auto prefix=state_.registration+"\\"+state_.registration+"\\";
            for(unsigned i=0;i<7;++i){
                GameModelMotion motion;
                const auto status=motion.Load(resources,prefix+before+"0"+char('1'+i)+after,*asset_);
                // Unlike source2E7C's null branch, a failed native file/parse
                // operation is not absence authority. Retain failure/cleanup.
                if(!status){state_.failed=true;state_.staged=false;state_.status=status;return status;}
                const auto stats=motion.Statistics();GameModelPlayback control;
                control.Configure(stats.duration,stats.loop); // 88918 state1, no72B8 activation
                motions.push_back(std::move(motion));controls.push_back(std::move(control));
            }
            Need(format.InitializedDigest()==digest,"light format drift");
        }
        Need(Current(owner,epoch)&&RegistrationsCurrent()&&MotionCountsCurrent()&&AuxiliaryCurrent()
             &&asset_->Live(),"background effects owner drift");
        light_format_=motions.empty()?PackageView{}:std::move(format);light_format_digest_=digest;
        light_motions_=std::move(motions);light_playbacks_=std::move(controls);
        state_.light_templates=uint32_t(light_motions_.size());state_.light_bindings=uint32_t(light_playbacks_.size());
        for(const auto& motion:light_motions_)state_.light_deferred+=motion.Statistics().deferred_sections;
        state_.secondary_motions_omitted=true;
        // 573C returns immediately for this component's selected selector12.
        state_.special_motion_not_applicable=true;state_.effects_prepared=true;return {};
    }catch(const std::exception& e){state_.failed=true;state_.staged=false;state_.status=Error(e);return state_.status;}
}
GameFsStatus GameBackgroundPreparation::LightBindings(const std::shared_ptr<const void>& owner,uint64_t epoch,
        std::vector<GameBackgroundLightBinding>& out)const {
    if(!Current(owner,epoch)||state_.failed||!state_.staged||!state_.effects_prepared
       ||!RegistrationsCurrent()||!MotionCountsCurrent()||!AuxiliaryCurrent()||!EffectsCurrent()||!RuntimeCurrent()||!asset_||!asset_->Live())
        return {GameFsCode::Unavailable,0,"background light binding owner retired/changed"};
    std::vector<GameBackgroundLightBinding> result;
    for(size_t i=0;i<light_motions_.size();++i)
        result.push_back({uint32_t(i),light_playbacks_[i].Snapshot(),light_motions_[i].Statistics()});
    out=std::move(result);return {};
}
GameFsStatus GameBackgroundPreparation::ActivateEffects(const std::shared_ptr<const void>& owner,uint64_t epoch,
        std::weak_ptr<GameBackgroundRuntime> input){
    std::vector<GameBackgroundLightBinding> check;
    const auto runtime=input.lock();
    const std::shared_ptr<const void> parent=runtime;
    if(activated_||!runtime||!runtime->Live()||parent!=owner||parent.owner_before(owner)||owner.owner_before(parent)
       ||!LightBindings(owner,epoch,check))
        return {GameFsCode::Unavailable,0,"background activation owner/runtime/replay refused"};
    // 61E4 has eight physical candidate/output slots. Preparation's larger
    // Windows storage bound does not widen this source selection domain.
    if(auxiliary_entries_.size()>8)return {GameFsCode::Unavailable,0,"unimplemented61E4 candidate extent"};
    std::array<uint32_t,7> calendar;auto status=runtime->Read(calendar);if(!status)return status;
    if(!Current(owner,epoch)||!RegistrationsCurrent()||!MotionCountsCurrent()||!AuxiliaryCurrent()||!EffectsCurrent())
        return {GameFsCode::Unavailable,0,"background input drift during current clock read"};
    try{
        GameBackgroundAppliedState result;result.runtime=runtime->Identity();result.clock_reads=runtime->reads_;result.calendar=calendar;
        result.light_slot=SceneSeason709C(calendar[5],calendar[4]);
        auto& rng=*runtime->rng_;result.rng_before=rng.CurrentSeed64();
        const auto draw=[&](){++result.rng_calls;return rng.NextC3234();};
        const auto count=uint32_t(auxiliary_entries_.size());const auto n=draw()%(count>=3?4:count+1);
        std::vector<uint32_t> remaining;for(uint32_t i=0;i<count;++i)remaining.push_back(i);
        for(uint32_t i=0;i<n;++i){const auto at=draw()%uint32_t(remaining.size());
            result.selected_entries.push_back(remaining[at]);remaining.erase(remaining.begin()+at);}
        // Source still consumes61E4's draw before this special overwrite.
        if(n==1&&count==1)result.selected_entries[0]=0;
        for(auto& binding:auxiliary_bindings_)binding.playback.Stop();
        for(const auto selected:result.selected_entries){
            auto& entry=auxiliary_entries_.at(selected);bool present=false;
            for(const auto& motion:entry.motions)present|=bool(motion); // exact5BFC any-template presence
            int32_t phase=-1;
            if(present){
                for(unsigned slot=0;slot<3;++slot){
                    for(auto& binding:auxiliary_bindings_)if(binding.entry==selected&&binding.slot==slot){
                        Need(!entry.motions[slot]->Statistics().deferred_sections,"unimplemented selected auxiliary motion");
                        if(binding.target!=GameAuxiliaryTarget::MainTemplate)binding.playback.Start(0);
                    }
                    if(slot==0&&entry.child)phase=int32_t(draw()%6); // source662C, before slots1/2
                }
            }
            result.child_phases.push_back(phase);
        }
        for(auto& control:light_playbacks_)control.Stop();
        if(!light_playbacks_.empty()){
            Need(result.light_slot<light_playbacks_.size(),"background seasonal slot outside bindings");
            Need(!light_motions_[result.light_slot].Statistics().deferred_sections,"unimplemented selected light motion");
            auto& control=light_playbacks_[result.light_slot];control.SetSpeed(30);
            control.Start(calendar[3]*216000+calendar[2]*3600+calendar[1]*60);
        }
        Need(runtime->Live()&&Current(owner,epoch),"background producer retired during activation");
        result.rng_after=rng.CurrentSeed64();runtime_=runtime;applied_=std::move(result);activated_=true;
        return {};
    }catch(const std::exception& e){state_.failed=true;state_.staged=false;state_.status=Error(e);return state_.status;}
}
GameFsStatus GameBackgroundPreparation::SampleEffects(const std::shared_ptr<const void>& owner,uint64_t epoch,uint32_t delta){
    std::vector<GameBackgroundLightBinding> check;
    if(!activated_||!LightBindings(owner,epoch,check))
        return {GameFsCode::Unavailable,0,"background sample current owner unavailable"};
    try{
        // Windows scheduling boundary: sample the admitted controls in source
        // construction order, then recompute actual world/marker transforms.
        for(auto& binding:auxiliary_bindings_){
            auto& entry=auxiliary_entries_[binding.entry];
            auto* target=binding.target==GameAuxiliaryTarget::Child?entry.child.get():
                binding.target==GameAuxiliaryTarget::MainTemplate?&main_template_clone_:clone_.get();
            const auto status=binding.playback.Apply(*entry.motions[binding.slot],*target,delta);
            if(!status)throw std::runtime_error(status.detail);
        }
        for(size_t i=0;i<light_playbacks_.size();++i){const auto status=light_playbacks_[i].Apply(light_motions_[i],*clone_,delta);
            if(!status)throw std::runtime_error(status.detail);}
        for(auto& entry:auxiliary_entries_)if(entry.child){const auto status=entry.child->Update(ModelIdentity());if(!status)throw std::runtime_error(status.detail);}
        auto status=clone_->Update(ModelIdentity());if(!status)throw std::runtime_error(status.detail);
        ++applied_.samples;return {};
    }catch(const std::exception& e){state_.failed=true;state_.staged=false;state_.status=Error(e);return state_.status;}
}
GameFsStatus GameBackgroundPreparation::AppliedEffects(const std::shared_ptr<const void>& owner,uint64_t epoch,
        GameBackgroundAppliedState& out)const {
    std::vector<GameBackgroundLightBinding> check;
    if(!activated_||!LightBindings(owner,epoch,check))return {GameFsCode::Unavailable,0,"background applied readback unavailable"};
    auto result=applied_;result.main_pose=static_cast<const GameModelClone&>(*clone_).Pose();result.lights=clone_->Lights();
    for(auto i:result.selected_entries){const auto& child=auxiliary_entries_[i].child;
        result.child_poses.push_back(child?static_cast<const GameModelClone&>(*child).Pose():std::vector<GameModelPose>{});}
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
    if(owner_.expired()||!RegistrationsCurrent()||!MotionCountsCurrent()||!AuxiliaryCurrent()||!EffectsCurrent()||(state_.staged&&(!asset_||!asset_->Live())))
        out.live=out.pending=out.staged=out.model_motions_staged=out.auxiliary_staged=out.effects_prepared=false;
    return out;
}
bool GameBackgroundPreparation::SupportJobCurrent(const std::shared_ptr<const void>& owner,uint64_t epoch)const{
    return Current(owner,epoch)&&state_.staged&&!state_.failed&&clone_&&asset_&&asset_->SourceCurrent()
        &&RegistrationsCurrent()&&MotionCountsCurrent()&&AuxiliaryCurrent()&&EffectsCurrent();
}
GameFsStatus GameBackgroundPreparation::PrepareSupport(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm,
        const GameSupportRegistration& registration,PackageView module,const GameSupportTextMap& map,GameResourceSource& source,
        const GameSupportTemplatePaths& paths,PackageView format){
    if(!SupportJobCurrent(owner,epoch)||support_prepared_||support_.Progress().live||!registration.Current(vm))
        return {GameFsCode::Unavailable,0,"support requires current staged job and actual GAME16 producer"};
    auto status=support_.InitializeRegistered(owner,epoch,vm,registration,std::move(module),map);
    if(status)status=support_.LoadTemplates(owner,epoch,vm,source,paths);
    if(status)status=support_.Attach(owner,epoch,vm,clone_);
    if(status)status=support_.PrepareGlyphTextures(owner,epoch,vm,source,std::move(format));
    if(status&&!SupportJobCurrent(owner,epoch))status={GameFsCode::Unavailable,0,"support background owner changed during preparation"};
    if(!status){state_.failed=true;state_.staged=false;state_.status=status;return status;}
    support_prepared_=true;return {};
}
GameFsStatus GameBackgroundPreparation::UpdateSupport(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm){
    if(!SupportJobCurrent(owner,epoch)||!support_prepared_)return {GameFsCode::Unavailable,0,"support job not current"};return support_.Update(owner,epoch,vm);
}
GameFsStatus GameBackgroundPreparation::ReadSupport(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm,GameSupportSnapshot& out)const{
    if(!SupportJobCurrent(owner,epoch)||!support_prepared_)return {GameFsCode::Unavailable,0,"support job not current"};return support_.Read(owner,epoch,vm,out);
}
GameFsStatus GameBackgroundPreparation::DrawWithSupport(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm,GameGraphics& graphics,const GameMatrix& camera)const{
    GameSupportSnapshot support;const auto checked=ReadSupport(owner,epoch,vm,support);if(!checked)return checked;
    auto status=clone_->Draw(graphics,camera,GameModelColorPolicy::WindowsDvsTexturedModulation2x);if(status)status=support_.DrawGlyphPreview(owner,epoch,vm,graphics,camera);return status;
}
namespace {
// Own parsed nested packages in this job; shared templates never receive a
// mutable material or a borrow from a separately retired scene.
class OutingPackageResources final:public GameResourceSource {
public:
    explicit OutingPackageResources(GameFilesystem& files):files_(files){}
    GameFsStatus OpenResource(const std::string& path,PackageView& out)override{
        try{
            const auto slash=path.find('\\');
            if(slash==std::string::npos||!slash||slash>64||path.find("..")!=path.npos||path.find(':')!=path.npos)
                throw std::runtime_error("outing nested resource path");
            const auto mount=path.substr(0,slash);auto found=packages_.find(mount);
            if(found==packages_.end()){
                if(packages_.size()>=64)throw std::runtime_error("outing nested package capacity");
                PackageView bytes;auto status=files_.LoadMember(mount+".BPM",bytes);if(!status)return status;
                found=packages_.emplace(mount,BpDirectory::Parse(bytes,bytes.Describe().size)).first;
            }
            out=found->second.Open(path.substr(slash+1));return {};
        }catch(const std::exception& e){return Error(e);}
    }
private:
    GameFilesystem& files_;std::map<std::string,BpDirectory> packages_;
};
}
GameFsStatus GameBackgroundPreparation::PrepareOwnedSupport(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm,
        const GameSupportRegistration& registration,PackageView module,const GameOutingData& packet){
    if(!packet.Current()||support_resources_)return {GameFsCode::Unavailable,0,"current outing support DATA/fresh resources required"};
    support_resources_=std::make_shared<OutingPackageResources>(filesystem_);
    auto status=PrepareSupport(owner,epoch,vm,registration,std::move(module),packet.text_map,*support_resources_,packet.support_paths,packet.glyph_format);
    if(status&&!packet.Current())return {GameFsCode::Unavailable,0,"outing support DATA changed during consumption"};
    return status;
}
GameFsStatus GameBackgroundPreparation::DrawSelected(const std::shared_ptr<const void>& owner,uint64_t epoch,script::Vm& vm,
        GameGraphics& graphics,const GameMatrix& camera)const{
    if(!RuntimeCurrent()||!activated_)return {GameFsCode::Unavailable,0,"selected current auxiliary activation required"};
    for(const auto index:applied_.selected_entries){
        if(index>=auxiliary_entries_.size())return {GameFsCode::Unavailable,0,"selected auxiliary index outside current recipe"};
        const auto& entry=auxiliary_entries_[index];
        if(entry.words[0]){
            if(!entry.motions[0]||!entry.child||!entry.child->Asset()||!entry.child->Asset()->SourceCurrent())
                return {GameFsCode::Unavailable,0,"selected required auxiliary child owner missing/retired"};
        }else if(entry.child||entry.words[1]||!entry.words[2]||!entry.motions[2])
            return {GameFsCode::Unavailable,0,"selected absent-child row is not an admitted main-only producer"};
    }
    auto status=DrawWithSupport(owner,epoch,vm,graphics,camera);if(!status)return status;
    for(const auto index:applied_.selected_entries){
        const auto& entry=auxiliary_entries_[index];
        if(!entry.child)continue; // legitimate {0,0,slot2}: main motion already sampled
        status=entry.child->Draw(graphics,camera,GameModelColorPolicy::WindowsDvsTexturedModulation2x);
        if(!status)return status;
    }
    return {};
}
GameFsStatus GameBackgroundPreparation::Close() {
    scene_bgm_=nullptr;scene_bgm_lifetime_.reset(); // manager BGM pool outlives this background job
    // Clone/template/material views die before their backing/file is revoked.
    runtime_.reset();activated_=false;applied_={};
    light_playbacks_.clear();light_motions_.clear();light_format_.Reset();state_.effects_prepared=false;
    auxiliary_bindings_.clear();
    for(auto& entry:auxiliary_entries_)if(entry.child)entry.child->Close();
    main_template_clone_.Close();auxiliary_entries_.clear();auxiliary_asset_.reset();auxiliary_inputs_.clear();
    state_.auxiliary_staged=false;
    model_motions_.clear();model_motion_counts_.Reset();
    support_.Close();support_prepared_=false;support_resources_.reset();
    if(clone_)clone_->Close();clone_.reset();asset_.reset();package_.Reset();
    const auto bgm_closed=bgm_files_.Close();
    if(!bgm_closed){state_.live=state_.pending=state_.staged=false;state_.failed=true;state_.status=bgm_closed;return bgm_closed;}
    bgm_runtime_.reset();bgm_inputs_={};
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
        const auto decoded_digest=core::sha256(bytes.data(),bytes.size());
        Need(image.InitializedDigest()==decoded_digest,"model source changed during decode");
        source_digest_=decoded_digest;
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
    static std::atomic<uint64_t> next_generation{0};generation_=++next_generation;
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
bool GameModelClone::DiffuseTexturesCurrent()const noexcept{
    try{if(!asset_||!asset_->SourceCurrent())return false;
        if(glyph_material_){const auto& b=*glyph_material_;const auto parent=b.parent.lock();
            if(!parent||parent->Generation()!=b.generation||!parent->Asset()||!parent->Asset()->SourceCurrent())return false;
            std::vector<GameAreaMarkerState> markers;if(!parent->AreaMarkers(*parent->Asset(),markers))return false;
            const auto m=std::find_if(markers.begin(),markers.end(),[&](const auto& m){return m.node==b.marker;});
            if(m==markers.end()||m->material!=b.material||b.child_materials.size()!=pose_.size())return false;
            for(size_t i=0;i<pose_.size();++i)if(pose_[i].material!=b.child_materials[i])return false;}
        for(const auto& b:diffuse_)
        if(!b.image||!b.image->SourceCurrent()||b.image->Generation()!=b.generation)return false;return true;}catch(...){return false;}
}
GameFsStatus GameModelClone::BindDiffuseTextures(uint64_t generation,const std::vector<std::pair<uint32_t,std::shared_ptr<const GameImage>>>& input){
    if(!generation||generation!=generation_||!asset_||!asset_->SourceCurrent())return {GameFsCode::Unavailable,0,"clone texture generation/source"};
    try{std::vector<DiffuseBinding> result;
        for(const auto& b:input){Need(b.first<asset_->Data().nodes.size()&&b.second&&b.second->SourceCurrent(),"clone texture node/image");
            const auto& n=asset_->Data().nodes[b.first];Need(n.type==1&&n.hasGeometry&&n.geometry.vertexCount&&n.geometry.uv.size()==n.geometry.vertexCount,"clone texture geometry/UV");
            for(const auto& uv:n.geometry.uv)Need(std::isfinite(uv.x)&&std::isfinite(uv.y),"nonfinite clone texture UV");
            for(const auto& old:result)Need(old.node!=b.first,"duplicate clone texture node");result.push_back({b.first,b.second,b.second->Generation()});}
        diffuse_=std::move(result);glyph_material_.reset();return {};
    }catch(const std::exception& e){return Error(e);}
}
GameFsStatus GameModelClone::BindGlyphTextures(uint64_t generation,const std::shared_ptr<const GameModelClone>& parent,uint32_t marker,
        const std::vector<std::pair<uint32_t,std::shared_ptr<const GameImage>>>& input){
    try{
        Need(parent&&parent.get()!=this&&parent->Asset()&&parent->Asset()->SourceCurrent(),"glyph descriptor parent/source");
        std::vector<GameAreaMarkerState> markers;Need(bool(parent->AreaMarkers(*parent->Asset(),markers)),"glyph descriptor parent pose");
        const auto m=std::find_if(markers.begin(),markers.end(),[&](const auto& m){return m.node==marker;});
        Need(m!=markers.end(),"glyph descriptor marker");
        GlyphMaterialBinding binding;binding.parent=parent;binding.generation=parent->Generation();binding.marker=marker;binding.material=m->material;
        if(m->material>=0){Need(size_t(m->material)<parent->Asset()->Data().materials.size(),"glyph current material index");
            const auto& original=parent->Asset()->Data().materials[size_t(m->material)];
            binding.descriptor.colorWords=original.colorWords;binding.descriptor.field30=original.field30;binding.descriptor.field34=original.field34;
            // Finite WindowsGlyphAmbient policy for the reached diffuse-only
            // renderer. Preserve all copied words/+30. Nonzero +34 and distinct
            // diffuse/ambient or emissive terms need their own lighting consumer.
            const auto& d=binding.descriptor;
            Need(d.colorWords[0]==d.colorWords[1]&&!(d.colorWords[3]&0xFFFFFF)&&!d.field34,
                 "unimplemented glyph material lighting terms");
        }else Need(m->parameters.material==255,"glyph descriptor null domain");
        for(const auto& p:pose_)binding.child_materials.push_back(p.material);
        auto staged=*this;const auto status=staged.BindDiffuseTextures(generation,input);if(!status)return status;
        // Null255 retains the prior finite glyph material path. Nonnull copies
        // colors and parameters, not parent textures, flags, or UV transforms.
        staged.glyph_material_=std::move(binding);
        *this=std::move(staged);return {};
    }catch(const std::exception& e){return Error(e);}
}
GameFsStatus GameModelClone::Draw(GameGraphics &graphics, const GameMatrix &view_projection,
                                 GameModelColorPolicy color_policy,GameModelLightingPolicy lighting_policy) const {
    if (!asset_ || !asset_->Live())
        return {GameFsCode::Unavailable, 0, "draw source lease retired"};
    try {
        Need(diffuse_.empty()||DiffuseTexturesCurrent(),"clone glyph texture retired/drifted");
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
            const GameImage* image=m&&m->texture?m->texture.get():nullptr;
            bool glyph_descriptor=false;
            for(const auto& binding:diffuse_)if(binding.node==node){image=binding.image.get();glyph_descriptor=glyph_material_&&glyph_material_->material>=0;break;}
            if(image)texture={image->Pixels().data(),image->Width(),image->Height(),image->Pixels().size()};
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
                if(glyph_descriptor){
                    // 28DD4 ->8DBE0 receives descriptor+8 (NOT marker210).
                    // WindowsGlyphAmbient uses that copied node RGB in the
                    // existing unlit diffuse raster. UV/geometry alpha remain
                    // authored; 8DC34's GE lighting commands are not emulated.
                    const auto rgb=glyph_material_->descriptor.colorWords[1];
                    v.color=(v.color&0xFF000000)|(NativeColorFromPspAbgr(rgb)&0xFFFFFF);
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
            policy.two_sided = !glyph_descriptor && m && (m->flags & 128);
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
