#include "game_actor_script_native.h"
#include "calendar_native.h"
#include "game_value_array_native.h"
#include "service39_native.h"
#include <stdexcept>
#include <sstream>

namespace dokodemo::recovered {
namespace {
using script::Value;
void Need(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
Value False() {Value v;v.kind=Value::Kind::Scalar;v.word=0x10000000;return v;}
Value Nil() {Value v;v.kind=Value::Kind::Scalar;return v;}
int32_t Number(const Value& value) {
    if(value.kind==Value::Kind::ArrayHeader)return int32_t(value.word&0xFFF);
    Need((value.kind==Value::Kind::Scalar||value.kind==Value::Kind::Timestamp)&&value.word>>28<=3,"Native KSC service needs scalar value");
    const auto p=value.word&0x0FFFFFFF;return int32_t(p&0x08000000?p|0xF0000000:p);
}
}
GameFsStatus GameActorScript::Initialize(PackageView dictionary,PackageView image,std::string path,
                                        const GameProfileRecord& profile,ClockWord clock,RngNativeOwner* rng,GameActorRuntime* runtime) {
    Close();state_={};
    try {
        Need(profile.live&&profile.actor<5,"KSC requires the actually confirmed native profile");
        const auto size=image.Describe().size;
        Need(size>=28&&size<=16*1024*1024,"Actual selected KSC extent");
        std::vector<uint8_t> bytes(size_t(size),0);
        for(size_t i=0;i<bytes.size();++i)bytes[i]=image.ReadU8(i);
        script::Diagnostic error;
        Need(program_.load_original_bytes(std::move(bytes),path,error),error.text().c_str());
        const auto attached=dictionary_.Attach(std::move(dictionary));
        Need(bool(attached),attached.detail.c_str());
        script_image_=std::move(image);profile_=profile;clock_=std::move(clock);rng_=rng;runtime_=runtime;state_.path=std::move(path);
        vm_=std::make_unique<script::Vm>(program_);
        vm_->host_service([this](uint8_t id,const std::vector<Value>& args){return Service(id,args);});
        vm_->random_source([this]{Need(rng_&&rng_->Available(),"Current game RNG not live");return rng_->NextC3234();});
        // Source67FD4 has argc2: (LoadMan4C8==0),0. This is the actual new-game
        // route,not the old no-argument probe or an isolated message function.
        const auto begun=vm_->start({Value::integer(1),Value::integer(0)});
        Need(begun.status==script::Status::Running,begun.diagnostic.text().c_str());
        state_.live=state_.started=true;state_.bytes=uint32_t(size);
        state_.entry_pc=state_.pc=program_.header().entrypoint;state_.sha256=program_.source().sha256;
        return {};
    } catch(const std::exception& e) {state_.blocked=true;state_.error=e.what();return {GameFsCode::Unavailable,0,e.what()};}
}
Value GameActorScript::GameService(const std::vector<Value>& args) {
    Need(args.size()==2,"F0 expects actual service id and argument");
    const auto id=Number(args[0]);Need(id>=0&&id<=80,"F0 game service domain");
    ++state_.game_service_calls;state_.last_service=uint32_t(id);state_.service_ids.push_back(uint32_t(id));
    const auto& arg=args[1];
    switch(id) {
    case 0: {
        Need(runtime_!=nullptr,"Game0 requires live zero-initialized manager owner");
        const auto requested=runtime_->RequestManager();Need(bool(requested),requested.detail.c_str());return False();
    }
    case 21:
        Need(runtime_&&runtime_->Snapshot().live,"Game21 requires actor owner");
        {const auto status=runtime_->SetField54(Number(arg));Need(bool(status),status.detail.c_str());}return False();
    case 22:
        Need(runtime_&&runtime_->Snapshot().live,"Game22 requires actor owner");
        return Value::integer(runtime_->Snapshot().source_field54);
    case 31: {
        Need(runtime_!=nullptr,"Render31 needs actual model slot owner");
        const auto field=[&](uint32_t index){return Number(vm_->load_reference(vm_->indexed_reference(arg,index)));};
        const auto resource=field(0),category=field(3),motion=field(8),x=field(9);
        int32_t slot=-1;const auto status=runtime_->CreateModel(resource,category,motion,float(x),slot);
        Need(bool(status),status.detail.c_str());return Value::integer(slot);
    }
    case 72: {
        Need(runtime_!=nullptr,"Camera72 needs live actor/camera owners");
        const auto bound=runtime_->BindCamera(Number(arg));Need(bool(bound),bound.detail.c_str());return False();
    }
    case 6: {
        Need(runtime_!=nullptr,"Game6 needs live transition owner");
        const auto begun=runtime_->StartTransition();Need(bool(begun),begun.detail.c_str());return False();
    }
    case 10:case 11:case 16:case 29:case 35:
        Need(arg.kind==Value::Kind::Reference,"Game registration requires typed live table reference");
        (void)vm_->load_reference(arg);bindings_[uint32_t(id)]=arg;return False();
    case 47:
        Need(arg.kind==Value::Kind::Reference,"Service47 destination reference");
        (void)vm_->load_reference(arg);
        // This new-game route's real native AUTOLOAD was missing. There is
        // no PSP utility buffer/signature to import. Source1C984 resets the
        // storage control and clears manager50/54;it never marks a loaded save.
        bindings_.erase(47);state_.autoload_imported=false;return False();
    case 48: {
        auto copied=dictionary_.Import(*vm_,arg);
        Need(bool(copied),copied.detail.c_str());
        // Register after import's internal validation. These counts describe
        // subsequent executed game queries/mutations,not a parser self-check.
        vm_->access_observer([this](const Value& ref,bool write){
            if(dictionary_.OwnsCell(ref)){if(write)++state_.dictionary_game_writes;else ++state_.dictionary_game_reads;}
        });
        return False();
    }
    case 39: {
        const auto value=vm_->load_reference(arg);
        Need(value.kind==Value::Kind::String,"Service39 needs current CP932 string reference");
        std::vector<uint8_t> bytes(value.cp932.begin(),value.cp932.end());bytes.push_back(0);
        const auto normalized=Service39Normalize(bytes,value.word>>28==6);
        state_.last_normalized.assign(normalized.bytes.begin(),normalized.bytes.end()-1);
        return Value::string(state_.last_normalized);
    }
    case 78:state_.manager_flag68=Number(arg)!=0;return False();
    case 20: {
        switch(Number(arg)) {
        case 0:return Value::string(profile_.player);
        case 1:return Value::string(profile_.nickname);
        case 2:return Value::integer(int32_t(profile_.date));
        case 4:return Value::integer(int32_t(profile_.gender));
        case 5:return Value::string(profile_.comment); // source label 電話番号/type3
        case 6:return Value::integer(int32_t(profile_.blood));
        case 10:return False(); // native confirmed profile != loaded PSP save
        case 3: {
            int64_t y=profile_.calendar[6],m=profile_.calendar[5];
            const auto day=profile_.calendar[4],hour=profile_.calendar[3],minute=profile_.calendar[2];
            Need(y>=1900&&m>=1&&m<=12&&day>=1&&day<=31&&hour<24&&minute<60,
                 "Confirmed calendar outside source1D354 domain");
            if(m<3){--y;m+=12;}
            const auto days=365*y+y/4-y/100+y/400+30*m+(3*m+3)/5+day-33;
            const auto minutes=days*1440+hour*60+minute-1051898400;
            Value value;value.kind=Value::Kind::Timestamp;value.word=0x30000000u|(uint32_t(minutes)&0x0FFFFFFF);return value;
        }
        case 7:case 8:case 9:
            // Explicit functional new-profile policy: these optional native
            // slots are absent. No original BSS value,field name,save import
            // or arbitrary placeholder string is inferred.
            return Nil();
        default:return False();
        }
    }
    default:throw std::runtime_error("Unimplemented reached native game service id="+std::to_string(id));
    }
}
Value GameActorScript::Service(uint8_t id,const std::vector<Value>& args) {
    ++state_.builtin_calls;
    if(id==0xF0)return GameService(args);
    if(id==1){Need(args.size()==1&&args[0].kind!=Value::Kind::Unknown,"Builtin01 type query argc/value");return Value::integer(args[0].kind==Value::Kind::Reference?11:int32_t(args[0].word>>28));}
    if(id==0x22){
        Need(args.size()==2,"Builtin22 event request argc");
        Need(runtime_!=nullptr,"Builtin22 needs actual actor command owner");
        const auto begun=runtime_->RequestActorEvent(Number(args[0]),Number(args[1]));Need(bool(begun),begun.detail.c_str());
        vm_->clear_event_signal();return Value::integer(0);
    }
    if(id==0x23){Need(args.empty(),"Builtin23 wait argc");vm_->wait_for_event();return Nil();}
    if(id==4) {Need(args.size()==1,"Builtin04 argc");return Value::integer(Number(args[0]));}
    if(id==17){Need(args.size()==1&&rng_&&rng_->Available(),"Builtin17 real RNG/argc");const auto divisor=Number(args[0]);Need(divisor>0,"Builtin17 positive divisor domain");return Value::integer(int32_t(rng_->NextC3234())%divisor);}
    if(id==9) {
        Value value;value.kind=Value::Kind::Timestamp;
        if(args.empty()) {Need(bool(clock_),"Live native clock provider unavailable");value.word=clock_();return value;}
        Need(args.size()==5,"Builtin09 calendar argc");
        std::array<int32_t,5> fields{};for(size_t i=0;i<5;++i)fields[i]=Number(args[i]);
        value.word=0x30000000u|(CalendarScalar7C660(CalendarRecord689B8(fields))&0x0FFFFFFF);return value;
    }
    if(id==10||id==11||id==12) {
        Need(args.size()==1,"Calendar decomposition argc");const auto minutes=Number(args[0]);
        Need(minutes>=0,"Pre2000 minute timestamp inverse is not recovered");
        const auto packed=[](uint32_t n){return ((n/10)<<4)|(n%10);};
        if(id==11)return Value::integer(int32_t((packed((uint32_t(minutes)/60)%24)<<8)|packed(uint32_t(minutes)%60)));
        if(id==12)return Value::integer((minutes/1440+5)%7);
        int32_t days=minutes/1440,year=2000,month=1;
        while(days>=365+(GameProfile::Days(year,2)==29)){days-=365+(GameProfile::Days(year,2)==29);++year;}
        while(days>=GameProfile::Days(year,month)){days-=GameProfile::Days(year,month);++month;}
        return Value::integer(int32_t((packed(uint32_t(year-2000))<<16)|(packed(uint32_t(month))<<8)|packed(uint32_t(days+1))));
    }
    if(id==0x60||id==0x61||id==0x62) {
        const auto result=id==0x60?GameValueArray96(*vm_,args):id==0x61?GameValueArray97(*vm_,args):GameValueArray98(*vm_,args);
        if(id!=0x61){++state_.array_query_calls;state_.array_query_matches+=uint32_t(result.selected);state_.last_query_indices=result.indices;}
        state_.array_actions+=result.writes;
        return Value::integer(result.selected);
    }
    throw std::runtime_error("Unimplemented reached KSC builtin id="+std::to_string(id));
}
GameFsStatus GameActorScript::Tick() {
    if(!state_.live||!vm_)return {GameFsCode::Unavailable,0,"KSC owner not live"};
    if(state_.blocked)return {GameFsCode::Unavailable,0,state_.error};
    if(state_.completed||state_.awaiting_message)return {};
    try {
        (void)script_image_.Describe(); // live backing required throughout use
        if(runtime_){const auto phase=runtime_->AdvanceActorFrame();Need(bool(phase),phase.detail.c_str());}
        if(vm_->waiting_for_event()) {
            if(!runtime_||!runtime_->WaitReady())return {};
            vm_->signal_event();++state_.event_signals;
        }
        ++state_.ticks;vm_->clock_tick(state_.ticks);
        const auto event=vm_->run(200000);
        dictionary_.Capture(*vm_);
        state_.pc=event.pc;state_.last_opcode=uint32_t(event.opcode);
        if(event.status==script::Status::Stopped)throw std::runtime_error(event.diagnostic.text());
        if(event.status==script::Status::Yield)++state_.yields;
        if(event.status==script::Status::HostYield)++state_.native_budget_yields;
        if(event.status==script::Status::Message) {++state_.messages;state_.message=event.message_cp932;state_.awaiting_message=true;}
        if(event.status==script::Status::Complete)state_.completed=true;
        return {};
    } catch(const std::exception& e) {state_.blocked=true;state_.error=e.what();return {GameFsCode::Unavailable,0,e.what()};}
}
bool GameActorScript::AcknowledgeMessage() {
    if(!state_.live||!state_.awaiting_message)return false;
    state_.awaiting_message=false;return true;
}
GameActorScriptSnapshot GameActorScript::Snapshot() const {
    auto out=state_;out.dictionary=dictionary_.Snapshot();if(vm_){out.instructions=vm_->executed();out.random_selections=vm_->random_selections();out.selected_value_word=vm_->last_random_value().word;out.root_functions_returned=vm_->root_functions_returned();
        out.control_blocks_entered=vm_->control_blocks_entered();out.control_blocks_closed=vm_->control_blocks_closed();
        out.active_control_blocks=vm_->active_control_blocks();out.switches=vm_->switches_executed();
        out.waiting_for_event=vm_->waiting_for_event();out.waiting_deadline=vm_->deadline_waiting();
        out.source_timestamp_writes=vm_->timestamp_writes();out.source_deadline_writes=vm_->deadline_writes();
    }if(runtime_)out.runtime=runtime_->Snapshot();return out;
}
void GameActorScript::Close() {
    state_=Snapshot();
    dictionary_.Close();bindings_.clear();vm_.reset();program_={};script_image_.Reset();profile_={};clock_={};rng_=nullptr;runtime_=nullptr;
    state_.live=false;state_.awaiting_message=false;
}
} // namespace dokodemo::recovered
