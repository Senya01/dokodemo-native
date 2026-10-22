#include "game_actor_script_native.h"
#include "calendar_native.h"
#include "game_value_array_native.h"
#include "service39_native.h"
#include "core_hash.h"
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
uint8_t KanaPrefix(uint8_t trail) {
    if(trail==0xC1)return 0;
    if(trail<=0xA8)return uint8_t((trail+1u)&~1u);
    switch(trail){
    case 0xE1:case 0xE3:case 0xE5:case 0xEC:return uint8_t(trail+1);
    case 0xEE:return 0xA2;
    case 0xEF:return 0xA6;
    case 0xF0:return 0xA8;
    default:return trail;
    }
}
}
GameWordRowObservation GameObserveWordRow(const script::Vm& vm,uint64_t generation,uint32_t row) noexcept {
    GameWordRowObservation out;out.generation=generation;out.row=row;
    try {
        const auto current=vm.current_witness();
        out.error_code=1;Need(current.valid&&current.run_id>0,"Word row needs an owning VM run");
        out.run_id=current.run_id;out.code_sha256=current.code_sha256;
        Value root;root.kind=Value::Kind::Reference;root.table=script::Table::Global;root.index=0x3DD2;
        const auto header=vm.peek_reference(root);out.global_header_word=header.word;
        out.error_code=2;Need(header.kind==Value::Kind::ArrayHeader&&header.word>>28==9&&row<(header.word&0xFFF),"Word row global header/index");
        const auto at=uint64_t(root.index)+1+row;Need(at<=UINT16_MAX,"Word row global index extent");root.index=uint16_t(at);
        const auto record=vm.peek_reference(root);
        out.error_code=3;Need(record.kind==Value::Kind::Reference&&(record.table==script::Table::Global||record.table==script::Table::Local||record.table==script::Table::Ctx28),"Word row needs a stable VM table owner");
        out.table=uint32_t(record.table);out.index=record.index;
        const auto record_header=vm.peek_reference(record);out.record_header_word=record_header.word;
        out.error_code=4;Need(record_header.kind==Value::Kind::ArrayHeader&&record_header.word>>28==9&&(record_header.word&0xFFF)==19,"Word row record header/count");
        Need(uint64_t(record.index)+19<=UINT16_MAX,"Word row record extent");
        out.error_code=5;for(uint32_t i=0;i<19;++i){
            auto ref=record;ref.index=uint16_t(uint32_t(record.index)+1+i);const auto value=vm.peek_reference(ref);auto& field=out.fields[i];
            field.kind=value.kind;field.word=value.word;field.table=uint32_t(value.table);field.index=value.index;
            if(value.kind==Value::Kind::String){field.cp932_bytes=value.cp932.size();field.cp932_sha256=core::sha256(reinterpret_cast<const uint8_t*>(value.cp932.data()),value.cp932.size());}
        }
        out.valid=true;out.error_code=0;
    }catch(...){/* Fixed evidence remains invalid; no owner or VM mutation. */}
    return out;
}
script::Value GameNilBytePair0D(const std::vector<script::Value>& args){
    Need(args.size()==1,"Builtin0D argc");const auto& value=args[0];
    if(value.kind==Value::Kind::String&&value.word>>28==6&&value.cp932.size()==2){
        const auto lead=uint8_t(value.cp932[0]),trail=uint8_t(value.cp932[1]);
        Need(lead==0x82&&trail>=0x9F&&trail<=0xF1&&KanaPrefix(trail)==trail,"Builtin0D outside finite prefix text domain");
        return Value::integer(int32_t(lead)|(int32_t(trail)<<8));
    }
    Need(value.kind==Value::Kind::Scalar&&value.word==0,"Builtin0D conversion outside finite canonical NIL/prefix domain");
    //757D8 selects static E0760 for tag0. 69404/6940C LBU and69418 SLL8
    //pack the first two bytes83,73;686A4/685C0 publish tagged integer.
    return Value::integer(0x7383);
}
void GameAsciiClass70Owner::Begin(uint64_t generation) noexcept {
    generation_=generation;other_=False();other_.word=0x10000001;
    current_=False();live_=generation!=0;
}
void GameAsciiClass70Owner::Close() noexcept {
    live_=false;generation_=0;current_={};other_={};
}
script::Value GameAsciiClass70Owner::Query(const std::vector<script::Value>& args)const {
    Need(live_&&generation_!=0,"Builtin70 requires its current constructor-owned character flags");
    Need(args.size()==1&&args[0].kind==Value::Kind::String&&args[0].word>>28==6,"Builtin70 outside finite owned-string domain");
    const auto& text=args[0].cp932;Need(GameInput32TextAllowed(text),"Builtin70 character map outside finite Latin/kana domain");
    Need(current_.kind==Value::Kind::Scalar&&(current_.word==0x10000000||current_.word==0x10000001),"Builtin70 current flag outside finite constructor-owned domain");
    Need(other_.kind==Value::Kind::Scalar&&(other_.word==0x10000000||other_.word==0x10000001),"Builtin70 other flag outside finite constructor-owned domain");
    uint32_t mapped=0;bool scanned=true;
    for(size_t at=0;at<text.size();at+=2){
        const auto lead=uint8_t(text[at]);
        if(lead==0x81)continue; // admitted815B only
        if(lead!=0x82){scanned=false;break;}
        if(KanaPrefix(uint8_t(text[at+1])))++mapped;
    }
    //7C27C/280 includes the pointer one byte beyond the last decoded unit.
    //The two values are constructor-owned flags, not constant branch answers.
    const auto& flag=scanned&&mapped&&text.size()+1<=31?other_:current_;
    auto result=False();result.word|=uint32_t((flag.word&0x0FFFFFFF)!=0);return result;
}
script::Value GameAsciiPrefix71(const std::vector<script::Value>& args){
    Need(args.size()==1,"Builtin71 requires one string argument");const auto& value=args[0];
    Need(value.kind==Value::Kind::String&&value.word>>28==6,"Builtin71 outside finite owned-string domain");
    Need(GameInput32TextAllowed(value.cp932),"Builtin71 character map outside finite Latin/kana domain");
    for(size_t at=0;at<value.cp932.size();at+=2){
        const auto lead=uint8_t(value.cp932[at]);
        if(lead==0x81)continue;
        if(lead!=0x82)break;
        const auto trail=KanaPrefix(uint8_t(value.cp932[at+1]));
        if(trail){
            //WindowsPrefixOwnedText: deep-copy the proved static text.
            //Original tag5/static PSP pointer is not a KSC-pool address here.
            std::string prefix;prefix.push_back('\x82');prefix.push_back(char(trail));
            return Value::string(std::move(prefix));
        }
    }
    return Nil();
}
script::Value GameStringLength53(const std::vector<script::Value>& args){
    Need(args.size()==1,"Builtin53 character count requires one argument");const auto& value=args[0];
    if(value.kind==Value::Kind::Scalar&&value.word==0)return Value::integer(0);
    Need(value.kind==Value::Kind::String&&value.word>>28==6,"Builtin53 conversion outside finite owned-string domain");
    uint32_t count=0;
    // All256 E07CC bit4 decisions:81..9F/E0..FE advance two bytes.
    // Checked native string boundary rejects embedded NUL/truncated units;
    // this byte counter does not expand the Input32 admission domain.
    for(size_t at=0;at<value.cp932.size();++count){
        const auto lead=uint8_t(value.cp932[at]);Need(lead!=0,"Builtin53 embedded NUL");
        const size_t width=(lead>=0x81&&lead<=0x9F)||(lead>=0xE0&&lead<=0xFE)?2u:1u;
        Need(width<=value.cp932.size()-at&&(width==1||value.cp932[at+1]!=0),"Builtin53 truncated flagged unit");at+=width;
    }
    return Value::integer(int32_t(count&0x0FFFFFFF));
}
script::Value GameKanaSuffix72(const std::vector<script::Value>& args){
    Need(args.size()==1,"Builtin72 requires one string argument");const auto& value=args[0];
    Need(value.kind==Value::Kind::String&&value.word>>28==6,"Builtin72 outside finite owned-string domain");
    Need(GameInput32TextAllowed(value.cp932),"Builtin72 character map outside finite Latin/kana domain");
    uint8_t last=0;
    for(size_t at=0;at<value.cp932.size();at+=2){
        const auto lead=uint8_t(value.cp932[at]);
        if(lead==0x81)continue;
        if(lead!=0x82)return Nil();
        const auto trail=KanaPrefix(uint8_t(value.cp932[at+1]));if(trail)last=trail;
    }
    if(!last)return Nil();
    std::string suffix;suffix.push_back('\x82');suffix.push_back(char(last));
    return Value::string(std::move(suffix));
}
script::Value GameStringReplace51(const std::vector<script::Value>& args) {
    Need(args.size()==3,"Builtin51 requires source,needle,replacement");
    const auto text=[](const Value& arg)->std::string{
        if(arg.kind==Value::Kind::String)return arg.cp932;
        //757C0/757DC tags0/1 return PH0 E0760. Relocation-bound retained
        //bytes8373815B00 are original CP932 "ピー",not an empty fallback.
        if(arg.kind==Value::Kind::Scalar&&(arg.word>>28)<2)return "\x83\x73\x81\x5B";
        throw std::runtime_error("Builtin51 unimplemented75778 conversion domain");
    };
    const auto source=text(args[0]),needle=text(args[1]),replacement=text(args[2]);
    Need(!needle.empty(),"Builtin51 empty needle: original7BF78 traps");
    constexpr size_t budget=1024*1024;
    Need(source.size()<=budget&&replacement.size()<=budget,"Builtin51 native owned-string budget");
    std::string result;size_t at=0;
    const auto append=[&](std::string_view part){Need(part.size()<=budget-result.size(),"Builtin51 result budget");result.append(part);};
    for(;;){const auto match=source.find(needle,at);
        if(match==std::string::npos){append(std::string_view(source).substr(at));break;}
        append(std::string_view(source).substr(at,match-at));append(replacement);at=match+needle.size();
    }
    return Value::string(std::move(result));
}
GameFsStatus GameActorScript::Initialize(PackageView dictionary,PackageView image,std::string path,
                                        const GameProfileRecord& profile,ClockWord clock,RngNativeOwner* rng,GameActorRuntime* runtime,GameRecordStore* records,GameSoundBank* sound) {
    Close();state_={};
    state_.generation=++owner_generation_;
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
        script_image_=std::move(image);profile_=profile;clock_=std::move(clock);rng_=rng;runtime_=runtime;records_=records;sound_=sound;state_.path=std::move(path);
        vm_=std::make_unique<script::Vm>(program_);
        owned_conversation_=GameOwnedConversation{};
        vm_->frame_observer([this](const script::FrameTransition& t){owned_conversation_.Observe(t,*vm_);ObserveInventoryAction(t);});
        ascii_class70_.Begin(state_.generation);
        state_.ascii_class70_live=ascii_class70_.Live();state_.ascii_class70_generation=ascii_class70_.Generation();
        state_.ascii_class70_current_word=ascii_class70_.CurrentWord();state_.ascii_class70_other_word=ascii_class70_.OtherWord();
        vm_->native_reserved_local_nil(true);
        if(profile.actor==0){ // finite passive NEKO return witnesses, independently retained
            vm_->trace_normal_return_at(0x85438,0,0x2B483);vm_->trace_normal_return_at(0x7D628,1);
        }
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
    state_.last_service_argument_word=args[1].word;
    state_.last_service_returned=false;
    const auto result=DispatchGameService(args);
    state_.last_service_result_word=result.word;state_.last_service_returned=true;
    return result;
}
Value GameActorScript::DispatchGameService(const std::vector<Value>& args) {
    Need(args.size()==2,"F0 expects actual service id and argument");
    const auto id=Number(args[0]);Need(id>=0&&id<=80,"F0 game service domain");
    ++state_.game_service_calls;state_.last_service=uint32_t(id);state_.service_ids.push_back(uint32_t(id));
    const auto& arg=args[1];
    switch(id) {
    case 73: {
        Need(sound_&&arg.kind==Value::Kind::Reference,"GAME73 needs registered SE_SYS bank and actual argument array");
        const auto header=vm_->load_reference(arg);Need(header.kind==Value::Kind::ArrayHeader&&(header.word&0xFFF)==3,"GAME73 reached three scalar fields");
        const auto field=[&](uint32_t n){return Number(vm_->load_reference(vm_->indexed_reference(arg,n)));};
        const auto status=sound_->Request73(field(0),field(1),field(2));Need(bool(status),status.detail.c_str());
        ++state_.sound73_requests;state_.sound73_playback_result=sound_->Snapshot().last_playback_result;
        state_.sound73_result_word=False().word;return False(); //1D064 ignores playback return
    }
    case 3: {
        Need(runtime_!=nullptr,"GAME3 needs live game manager owner");
        state_.game3_execution=vm_->current_witness();
        const auto status=runtime_->RequestManager9();Need(bool(status),status.detail.c_str());
        owned_conversation_.Game3(state_.game3_execution);
        ++state_.game3_calls;state_.game3_result_word=False().word;return False();
    }
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
    case 27:
        Need(runtime_&&runtime_->Snapshot().live,"GAME27 needs live actor5C owner");
        return Value::integer(runtime_->Snapshot().source_field5C);
    case 19: {
        Need(runtime_!=nullptr,"GAME19 needs actual actor and region owners");
        bool inside=false;const auto status=runtime_->QueryActorPosition(inside);Need(bool(status),status.detail.c_str());
        auto value=False();if(inside)value.word|=1;state_.position_result_word=value.word;return value;
    }
    case 28: {
        Need(runtime_&&runtime_->Snapshot().live,"GAME28 needs live source game-state word2A97C");
        auto value=False();if(runtime_->Snapshot().source_flag2A97C)value.word|=1;
        ++state_.game28_queries;state_.game28_result_word=value.word;return value;
    }
    case 31: {
        Need(runtime_!=nullptr,"Render31 needs actual model slot owner");
        const auto field=[&](uint32_t index){return Number(vm_->load_reference(vm_->indexed_reference(arg,index)));};
        const auto resource=field(0),category=field(3),motion=field(8),x=field(9);
        int32_t slot=-1;const auto status=runtime_->CreateModel(resource,category,motion,float(x),slot);
        const auto result=Value::integer(slot);
        RecordInventoryModelService(31,slot,resource,category,motion,x,status?result.word:0,bool(status));
        Need(bool(status),status.detail.c_str());return result;
    }
    case 32: {
        Need(runtime_!=nullptr,"GAME32 requires live model slot owner");const auto slot=Number(arg);
        const auto status=runtime_->RetireModel(slot);
        RecordInventoryModelService(32,slot,-1,0,0,0,status?False().word:0,bool(status));
        Need(bool(status),status.detail.c_str());return False();
    }
    case 33: {
        Need(runtime_!=nullptr,"GAME33 requires live model slot owner");
        const auto field=[&](uint32_t index){return Number(vm_->load_reference(vm_->indexed_reference(arg,index)));};
        const auto slot=field(6),selector=field(8),x=field(9);
        const auto status=runtime_->UpdateModel(slot,selector,float(x));
        RecordInventoryModelService(33,slot,field(0),field(3),selector,x,status?False().word:0,bool(status));
        Need(bool(status),status.detail.c_str());return False();
    }
    case 36: {
        Need(runtime_&&bool(clock_),"GAME36 needs live scene owner and native civil clock");
        const auto clock=clock_();Need(clock>>28==3,"Scene clock provider must return actual minute timestamp");
        const auto minute=(clock&0x0FFFFFFF)%1440;int32_t period=-1;
        const auto status=runtime_->QueryScenePeriod(minute/60,minute%60,period);Need(bool(status),status.detail.c_str());
        ++state_.scene_period_queries;state_.scene_period_clock_word=clock;state_.scene_period=period;state_.scene_period_result_word=Value::integer(period).word;
        return Value::integer(period);
    }
    case 40: {
        Need(runtime_&&runtime_->Snapshot().live,"GAME40 requires live game-state word2A980");
        state_.source_flag2A980=runtime_->Snapshot().source_flag2A980!=0;
        ++state_.scene_flag_queries;auto value=False();if(state_.source_flag2A980)value.word|=1;return value;
    }
    case 71: {
        Need(records_&&records_->Live()&&bool(clock_),"GAME71 needs live profile record-bank and civil clock owners");
        state_.record_clock_word=clock_();const auto date=GameRecordDateFromClockWord(state_.record_clock_word);
        const auto row=records_->Candidate71(date);GameRecordInput input;const GameRecordInput* source=nullptr;
        Value first_ref,first;
        //Negative-index protection precedes registration queries in39D64.
        if(row>=0&&records_->At(0,uint32_t(row)).flag!=1){
            const auto found=bindings_.find(29);
            if(found!=bindings_.end()){
                const auto header=vm_->load_reference(found->second);
                Need(header.kind==Value::Kind::ArrayHeader,"GAME29 record registration needs typed array");
                if((header.word&0xFFF)>=3){
                    first_ref=vm_->indexed_reference(found->second,0);first=vm_->load_reference(first_ref);
                    const auto second=vm_->load_reference(vm_->indexed_reference(found->second,1));
                    const auto text=vm_->load_reference(vm_->indexed_reference(found->second,2));
                    if(text.word>>28!=0){
                        Need(first.kind==Value::Kind::Scalar&&first.word>>28<=3,"GAME71 cell0 byte store finite scalar domain");
                        Need(text.kind==Value::Kind::String,"GAME71 reached owned CP932 record text domain");
                        input.value=Number(second);input.text=text.cp932;source=&input;
                    }
                }
            }
        }
        const auto captured=records_->Capture71(date,source);Need(bool(captured),captured.detail.c_str());
        if(records_->Snapshot().last_copied){
            state_.record_cell0_before=first.word;first.word&=0xFFFFFF00u;state_.record_cell0_after=first.word;
            vm_->store_reference(first_ref,first);++state_.record_cell0_writes;state_.record_input_text=input.text;
        }
        ++state_.game71_calls;state_.game71_result_word=False().word;return False();
    }
    case 41: {
        Need(Number(arg)==0,"Reached GAME41 inventory mode0 only");
        state_.inventory_mode=0;state_.inventory_requested=true;++state_.inventory_requests;
        state_.inventory_setter_result_word=0x10000000;
        CaptureInventoryItems();
        return False(); //1C834 queues resource14;not a synchronous GUI result
    }
    case 43: {
        for(uint16_t i=0;i<3;++i)state_.intro_caller_words[i]=vm_->read_slot(script::Table::Frame,i).word;
        if(Number(arg)==0)state_.intro_clock_expired=state_.last_clock_word;
        Need(runtime_!=nullptr,"GAME43 needs live CGuiIntro owner");
        const auto status=runtime_->ShowIntro(Number(arg));Need(bool(status),status.detail.c_str());return False();
    }
    case 44: {
        Need(runtime_!=nullptr,"GAME44 needs live camera manager owner");
        const auto status=runtime_->RequestCamera(Number(arg));Need(bool(status),status.detail.c_str());
        ++state_.camera_setter_returns;state_.camera_setter_result_word=0x10000000;
        return False(); // dispatcher default remainsBooleanFALSE;no camera wait
    }
    case 45: {
        Need(runtime_&&runtime_->Snapshot().live,"GAME45 needs live actor command owner");
        ++state_.actor_flag_queries;auto value=False();if(runtime_->Snapshot().command_flag25A)value.word|=1;return value;
    }
    case 24:
        Need(runtime_&&runtime_->Snapshot().live,"GAME24 needs live actor field58 producer");
        ++state_.actor_flag_queries;return Value::integer(runtime_->Snapshot().source_field58);
    case 23:
        Need(runtime_&&runtime_->Snapshot().live,"GAME23 needs live actor50 owner");
        return Value::integer(runtime_->Snapshot().source_field50);
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
    default: {
        std::ostringstream detail;detail<<"Unimplemented reached native game service id="<<id
            <<" argKind="<<int(arg.kind)<<" raw="<<std::hex<<arg.word;
        if(arg.kind==Value::Kind::Reference) {
            const auto value=vm_->peek_reference(arg);
            detail<<" table="<<int(arg.table)<<" index="<<arg.index<<" value="<<value.word;
            if(value.kind==Value::Kind::ArrayHeader) {
                const auto count=value.word&0xFFF;detail<<" fields=";
                for(uint32_t i=0;i<count&&i<20;++i) {
                    auto ref=arg;ref.index=uint16_t(uint32_t(arg.index)+1+i);
                    const auto field=vm_->peek_reference(ref);detail<<field.word<<',';
                }
            }
        }
        throw std::runtime_error(detail.str());
    }
    }
}
Value GameActorScript::Service(uint8_t id,const std::vector<Value>& args) {
    ++state_.builtin_calls;
    if(id==0xF0)return GameService(args);
    if(id==0x27){Need(args.size()==4,"Builtin27 actual context,callback,two values ABI");
        return Value::integer(vm_->post_callback(args[0],args[1],args[2],args[3]));}
    if(id==0x51){
        try{auto value=GameStringReplace51(args);++state_.string_replace_calls;state_.last_replacement=value.cp932;state_.replacement_results.push_back(value.cp932);return value;}
        catch(const std::exception& e){std::ostringstream detail;detail<<e.what()<<" actual=";
            for(const auto& a:args){detail<<std::hex<<a.word<<':';for(unsigned char b:a.cp932)detail<<"0123456789abcdef"[b>>4]<<"0123456789abcdef"[b&15];detail<<',';}
            throw std::runtime_error(detail.str());}
    }
    if(id==0x53){
        auto value=GameStringLength53(args);
        if(args[0].kind==Value::Kind::String&&!GameInput32TextAllowed(args[0].cp932)){
            if(!state_.string_length53_extended_calls){state_.string_length53_extended_pc=vm_->pc();state_.string_length53_extended_input=args[0].cp932;state_.string_length53_extended_result=value.word;}
            ++state_.string_length53_extended_calls;
        }
        if(vm_->pc()==0x1584){ // actual display-length producer; earlier scenario counters are unrelated
            const auto current=vm_->current_witness();
            if(current.frame_captured&&current.frame_values[0].kind==Value::Kind::Scalar&&current.frame_values[0].word>>28==2){
                const auto row=current.frame_values[0].word&0x0FFFFFFF;
                const auto observed=ObserveWordRow(row);
                const auto input_hash=args[0].kind==Value::Kind::String?core::sha256(reinterpret_cast<const uint8_t*>(args[0].cp932.data()),args[0].cp932.size()):std::array<uint8_t,32>{};
                if(row<600&&observed.valid&&args[0].kind==Value::Kind::String&&observed.fields[1].kind==Value::Kind::String
                   &&observed.fields[1].cp932_bytes==args[0].cp932.size()&&observed.fields[1].cp932_sha256==input_hash){word_consumer_row_index_=row;state_.word_consumer_source_pc=current.current_pc;
                    state_.word_consumer_source_entry=current.code_entry;state_.word_consumer_source_frame_id=current.frame_id;}
            }
        }
        ++state_.string_length53_calls;state_.string_length53_pc=vm_->pc();
        state_.string_length53_input=args[0].kind==Value::Kind::String?args[0].cp932:std::string{};
        state_.string_length53_result_word=value.word;return value;
    }
    if(id==0x71){
        auto value=GameAsciiPrefix71(args);++state_.string_prefix71_calls;state_.string_prefix71_pc=vm_->pc();
        state_.string_prefix71_input=args[0].cp932;state_.string_prefix71_result_word=value.word;state_.string_prefix71_result_kind=uint32_t(value.kind);
        state_.string_prefix71_result_text=value.cp932;return value;
    }
    if(id==0x72){
        auto value=GameKanaSuffix72(args);++state_.string_suffix72_calls;state_.string_suffix72_pc=vm_->pc();
        state_.string_suffix72_input=args[0].cp932;state_.string_suffix72_result_word=value.word;state_.string_suffix72_result_kind=uint32_t(value.kind);
        state_.string_suffix72_result_text=value.cp932;return value;
    }
    if(id==0x0D){
        auto value=GameNilBytePair0D(args);++state_.nil_code0d_calls;state_.nil_code0d_pc=vm_->pc();state_.nil_code0d_result_word=value.word;return value;
    }
    if(id==0x70){
        auto value=ascii_class70_.Query(args);++state_.ascii_class70_calls;state_.ascii_class70_pc=vm_->pc();
        state_.ascii_class70_input=args[0].cp932;state_.ascii_class70_result_word=value.word;return value;
    }
    if(id==1){Need(args.size()==1&&args[0].kind!=Value::Kind::Unknown,"Builtin01 type query argc/value");return Value::integer(args[0].kind==Value::Kind::Reference?11:int32_t(args[0].word>>28));}
    if(id==0x22){
        Need(args.size()==2,"Builtin22 event request argc");
        Need(runtime_!=nullptr,"Builtin22 needs actual actor command owner");
        state_.actor22_pc=vm_->pc();state_.actor22_command_word=args[0].word;state_.actor22_parameter_word=args[1].word;
        const auto begun=runtime_->RequestActorEvent(Number(args[0]),Number(args[1]));Need(bool(begun),begun.detail.c_str());
        ++state_.actor22_calls;
        vm_->clear_event_signal();return Value::integer(0);
    }
    if(id==0x23){Need(args.empty(),"Builtin23 wait argc");vm_->wait_for_event();return Nil();}
    if(id==0x35){
        Need(args.size()==5&&program_.header().local_count>=7,"Builtin35 actual local configuration extent/argc");
        for(const auto& arg:args)Need((arg.kind==Value::Kind::Scalar||arg.kind==Value::Kind::Timestamp)&&arg.word>>28<=3,
                                     "Builtin35 reached finite scalar configuration domain");
        //69EE0 moves five owned arguments into ctx24[2..6]. The VM host
        //dispatcher owns/releases args;native assignments release prior cells.
        //These are the SAME locals used by KSC instructions/string expansion.
        for(uint16_t i=0;i<5;++i){Value ref;ref.kind=Value::Kind::Reference;ref.table=script::Table::Local;ref.index=i+2;
            vm_->store_reference(ref,args[i]);state_.message_local_words[i]=args[i].word;}
        ++state_.local_config_writes;return Nil();
    }
    if(id==4) {Need(args.size()==1,"Builtin04 argc");return Value::integer(Number(args[0]));}
    if(id==17){
        Need(args.size()==1&&rng_&&rng_->Available(),"Builtin17 real RNG/argc");
        const auto divisor=Number(args[0]);Need(divisor>0,"Builtin17 positive divisor domain");
        const auto raw=rng_->NextC3234();const auto result=Value::integer(int32_t(raw)%divisor);
        ++state_.rng17_calls;state_.rng17_pc=vm_->pc();state_.rng17_bound=uint32_t(divisor);state_.rng17_raw=raw;state_.rng17_result_word=result.word;
        return result;
    }
    if(id==9) {
        Value value;value.kind=Value::Kind::Timestamp;
        if(args.empty()) {Need(bool(clock_),"Live native clock provider unavailable");value.word=clock_();
            ++state_.clock_calls;state_.last_clock_word=value.word;
            if(runtime_&&runtime_->Snapshot().intro_requests&&state_.intro_clock_origin==0)state_.intro_clock_origin=value.word;
            return value;}
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
    if(id==0x60||id==0x61||id==0x62||id==0x63) {
        const auto result=id==0x60?GameValueArray96(*vm_,args):id==0x61?GameValueArray97(*vm_,args):id==0x62?GameValueArray98(*vm_,args):GameValueArray99(*vm_,args);
        if(id!=0x61){++state_.array_query_calls;state_.array_query_matches+=uint32_t(result.selected);state_.last_query_indices=result.indices;}
        state_.array_actions+=result.writes;
        if(id==0x63){auto value=False();value.word|=uint32_t(result.selected);return value;}
        return Value::integer(result.selected);
    }
    std::ostringstream unimplemented;unimplemented<<"Unimplemented reached KSC builtin id="<<unsigned(id)<<" argc="<<args.size()<<" args=";
    for(const auto& arg:args){unimplemented<<int(arg.kind)<<':'<<std::hex<<arg.word;
        if(arg.kind==Value::Kind::String){unimplemented<<" cp932=";for(unsigned char b:arg.cp932)unimplemented<<"0123456789abcdef"[b>>4]<<"0123456789abcdef"[b&15];}
        unimplemented<<',';
    }
    throw std::runtime_error(unimplemented.str());
}
void GameActorScript::CaptureInventoryItems(){
    const auto found=bindings_.find(35);Need(found!=bindings_.end(),"GAME41 needs actual GAME35 item registration");
    const auto header=vm_->load_reference(found->second);Need(header.kind==Value::Kind::ArrayHeader&&(header.word&0xFFF)==170,"Inventory actual170 item rows");
    state_.inventory_items.clear();
    for(uint32_t i=0;i<170;++i){
        const auto row=vm_->load_reference(vm_->indexed_reference(found->second,i));Need(row.kind==Value::Kind::Reference,"Inventory row reference");
        const auto field=[&](uint32_t n){return vm_->load_reference(vm_->indexed_reference(row,n));};
        const auto item_id=field(0);GameInventoryItem item;item.row=i;
        if(item_id.word>>28!=0){
        item.id=Number(item_id);item.valid=item.id>=0;
        if(item.valid){const auto name=field(1),description=field(2);
            Need(name.kind==Value::Kind::String&&description.kind==Value::Kind::String,"Inventory real CP932 name and description");
            item.name=name.cp932;item.description=description.cp932;item.date_word=field(4).word&0x0FFFFFFF;
            item.category=Number(field(5))==1?2:((field(7).word>>28)==0?1:0);
            for(uint32_t n=0;n<3;++n){const auto value=field(5+n);item.source_words[n]=value.word;item.source_kinds[n]=uint8_t(value.kind);}
            for(uint32_t n=0;n<3;++n){const auto value=field(n==0?3:n+7);item.control_words[n]=value.word;item.control_kinds[n]=uint8_t(value.kind);}
        }
        }
        state_.inventory_items.push_back(std::move(item));
    }
}
GameFsStatus GameActorScript::Tick() {
    if(!state_.live||!vm_)return {GameFsCode::Unavailable,0,"KSC owner not live"};
    //The source global clock advances independently of VM interpreter entries.
    //Named native one-clock-unit per owner phase,including event/text waits.
    vm_->clock_tick(++state_.native_clock_ticks);
    // 1E788 checks readiness before actor+8 and only then copies394->ext20.
    // A flag produced by this actor phase is eligible on a later phase,not
    // an immediate same-phase acknowledgement of a newly reached8031.
    const bool event_eligible=vm_->waiting_for_event()&&runtime_&&runtime_->WaitReady();
    const bool repeat_eligible=event_eligible&&runtime_->Snapshot().command_repeat_ready258;
    if(runtime_){const auto phase=runtime_->AdvanceActorFrame();if(!phase)return phase;}
    if(state_.blocked)return {GameFsCode::Unavailable,0,state_.error};
    if(state_.completed||state_.awaiting_message||state_.awaiting_choice||state_.awaiting_input)return {};
    try {
        (void)script_image_.Describe(); // live backing required throughout use
        if(vm_->waiting_for_event()) {
            if(!event_eligible)return {};
            vm_->signal_event();++state_.event_signals;if(repeat_eligible)++state_.event_repeat_signals;
        }
        ++state_.ticks;
        const auto event=vm_->run(200000);
        dictionary_.Capture(*vm_);
        auto& action=state_.inventory_action;
        Need(!action.pending||!action.failed,"Inventory callback receipt/restoration failed");
        if(action.pending&&action.returned){
            Need(!action.failed&&!vm_->context_enabled()&&vm_->callback_depth()==action.suspended.event_scope_depth,"Inventory action failed to restore suspended disabled context/scope");
            const auto registered=bindings_.at(35);const auto rowref=vm_->load_reference(vm_->indexed_reference(registered,action.row));
            Need(rowref.kind==Value::Kind::Reference&&uint32_t(rowref.table)==action.row_table&&rowref.index==action.row_index
                 &&vm_->load_reference(rowref).word==action.row_header,"Inventory action source row replaced");
            CaptureInventoryItems();const auto& item=state_.inventory_items.at(action.row);
            if(action.event==16){
                Need(!item.valid&&action.row_cells==10,"Discard row remained valid or changed declared shape");
                for(uint32_t i=0;i<action.row_cells;++i){const auto v=vm_->load_reference(vm_->indexed_reference(rowref,i));
                    action.after_row_words[i]=v.word;action.after_row_kinds[i]=uint8_t(v.kind);
                    Need(v.kind==Value::Kind::Scalar&&v.word==0,"Discard did not clear every actual row cell to canonical NIL");}
            }else Need(item.valid&&item.id==action.item_id,"Inventory action changed item identity");
            action.after_words=item.source_words;action.after_kinds=item.source_kinds;
            action.after_controls=item.control_words;action.after_control_kinds=item.control_kinds;
            if(action.event==16){
                for(uint32_t i=0;i<3;++i){action.after_words[i]=action.after_row_words[i+5];action.after_kinds[i]=action.after_row_kinds[i+5];
                    const auto n=i==0?3u:i+7;action.after_controls[i]=action.after_row_words[n];action.after_control_kinds[i]=action.after_row_kinds[n];}
            }else{
            Need(item.source_kinds[0]==uint8_t(Value::Kind::Scalar)&&item.source_words[0]==(action.event==15?0x10000001u:0x10000000u),"Inventory action source flag write mismatch");
            if(action.event==13){
                Need(item.source_kinds[1]==uint8_t(Value::Kind::Scalar)&&(item.source_words[1]>>28)==2
                     &&item.source_kinds[2]==uint8_t(Value::Kind::Scalar)&&(item.source_words[2]>>28)==2,"Decoration did not produce a typed model/placement");
                const auto slot=Value{Value::Kind::Scalar,item.source_words[1]}.signed_integer();
                const auto candidate=Value{Value::Kind::Scalar,item.source_words[2]}.signed_integer();
                Value layouts;layouts.kind=Value::Kind::Reference;layouts.table=script::Table::Global;layouts.index=0x4EAE;
                const auto layout=vm_->load_reference(vm_->indexed_reference(layouts,uint32_t(candidate)));
                Need(layout.kind==Value::Kind::Reference,"Decoration layout source reference");
                action.layout_table=uint32_t(layout.table);action.layout_index=layout.index;action.layout_header=vm_->load_reference(layout).word;
                for(uint32_t i=0;i<2;++i)action.layout_words[i]=vm_->load_reference(vm_->indexed_reference(layout,i)).word;
                Need(item.control_words[1]==action.layout_words[0],"Decoration did not retain actual layout selector");
                const auto room=runtime_->Snapshot();Need(slot>=0&&size_t(slot)<room.room_models.size(),"Decoration source model allocation unavailable");
                const auto& owner=room.room_models[size_t(slot)];
                // Match GAME31/33's scalar conversion: the source layout may
                // retain canonical NIL as numeric selector0. Do not retag it.
                const auto selector=Number(vm_->load_reference(vm_->indexed_reference(rowref,8)));
                const auto x=Number(vm_->load_reference(vm_->indexed_reference(rowref,9)));
                Need(owner.active&&!owner.retirement&&owner.resource==item.id&&owner.category==0&&owner.selector==selector
                     &&owner.source_x==float(x),"Decoration returned a foreign/unsupported model owner");
            }else if(action.before_words[1]>>28){
                Need(item.source_kinds[1]==uint8_t(Value::Kind::Scalar)&&item.source_kinds[2]==uint8_t(Value::Kind::Scalar)
                     &&item.source_words[1]==0&&item.source_words[2]==0&&item.control_kinds[2]==uint8_t(Value::Kind::Scalar)
                     &&item.control_words[2]==0x20000000u,"Putaway did not clear typed source placement");
                for(uint32_t i=0;i<2;++i)Need(item.control_words[i]==action.before_controls[i]&&item.control_kinds[i]==action.before_control_kinds[i],
                     "Putaway changed source category or retained selector");
            }else{
                Need(item.source_words[1]==action.before_words[1]&&item.source_words[2]==action.before_words[2]
                     &&item.source_kinds[1]==action.before_kinds[1]&&item.source_kinds[2]==action.before_kinds[2],"Inventory flag action changed placement");
            }
            }
            action.pending=false;action.completed=true;++action.completions;
        }
        state_.pc=event.pc;state_.last_opcode=uint32_t(event.opcode);
        if(event.status==script::Status::Stopped)throw std::runtime_error(event.diagnostic.text());
        if(event.status==script::Status::Yield)++state_.yields;
        if(event.status==script::Status::HostYield)++state_.native_budget_yields;
        if(event.status==script::Status::Message) {
            ++state_.messages;state_.message=event.message_cp932;state_.message_history.push_back(state_.message);
            state_.message_pcs.push_back(event.pc);
            state_.message_actor_commands.push_back(runtime_?runtime_->Snapshot().requested_command:UINT32_MAX);
            state_.awaiting_message=true;
        }
        if(event.status==script::Status::Choice){
            state_.source_main_input_suppressed48=true; //1E620 source latch,not generic busy
            auto values=vm_->take_choice_arguments();state_.choice_options.clear();
            for(const auto& value:values){
                Need(value.kind==Value::Kind::String,"SELECT33 reached text conversion domain not implemented");
                Need(value.cp932.size()<=1023,"SELECT33 native checked1023-byte text extent");
                state_.choice_options.push_back(value.cp932);
            }
            ++state_.choice_requests;state_.awaiting_choice=true;
        }
        if(event.status==script::Status::Input){
            state_.source_main_input_suppressed48=true; //1E154 same source latch
            const auto args=vm_->input_arguments();
            Need(args.size()==2&&args[1].kind==Value::Kind::String,"Input32 arguments unavailable");
            const auto mode=args[0].signed_integer();
            Need(mode==0,"WindowsInput32 finite mode0 adapter;other modes not implemented");
            state_.input_mode=uint32_t(mode);state_.input_initial=args[1].cp932;
            state_.input_text.clear();state_.input_auxiliary.clear();state_.input_timed_out=false;
            ++state_.input_requests;state_.awaiting_input=true;
        }
        if(event.status==script::Status::Complete)state_.completed=true;
        return {};
    } catch(const std::exception& e) {state_.blocked=true;state_.error=e.what();return {GameFsCode::Unavailable,0,e.what()};}
}
GameMessageTarget GameActorScript::MessageTarget()const{
    Need(state_.live&&state_.awaiting_message&&vm_,"Message target needs live awaiting context");
    return {state_.generation,state_.messages,state_.pc,vm_->context_value()};
}
bool GameActorScript::AcknowledgeMessage(const GameMessageTarget* expected) {
    if(!state_.live||!state_.awaiting_message)return false;
    if(expected){
        const auto owner=expected->context.context_owner.lock();
        const auto current=vm_->context_value();
        if(!owner||owner!=current.context_owner.lock()||expected->context.word!=current.word
           ||expected->generation!=state_.generation||expected->message!=state_.messages||expected->pc!=state_.pc)return false;
    }
    state_.awaiting_message=false;++state_.message_acknowledgements;return true;
}
GameChoiceTarget GameActorScript::ChoiceTarget()const{
    Need(state_.live&&state_.awaiting_choice&&vm_&&vm_->choice_waiting(),"Choice target needs live state9 context");
    return {state_.generation,state_.choice_requests,state_.pc,vm_->context_value()};
}
GameFsStatus GameActorScript::CompleteChoice(const GameChoiceTarget& expected,uint32_t result){
    try{
        const auto current=ChoiceTarget();const auto owner=expected.context.context_owner.lock();
        Need(owner&&owner==current.context.context_owner.lock()&&expected.context.word==current.context.word
             &&expected.generation==current.generation&&expected.request==current.request&&expected.pc==current.pc,
             "SELECT completion target generation/registry/request changed");
        Need(result<=state_.choice_options.size()&&vm_->complete_choice(result),"SELECT result outside actual option range");
        //complete_choice may drain a queued event and temporarily replace
        //VMresult with that callback's result. Retain the published value,
        //not the result of a different currently executing context phase.
        state_.awaiting_choice=false;++state_.choice_completions;state_.choice_result_word=script::Value::integer(int32_t(result)).word;
        //1E508 publishes/drains first;only raw zero then emits owner event20.
        //Positive user choice never emits this timeout/cleanup event.
        if(result==0){const auto posted=PostMainFlowEvent(20,0);state_.source_main_input_suppressed48=false;return posted;}
        state_.source_main_input_suppressed48=false;
        return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameChoiceTarget GameActorScript::InputTarget()const{
    Need(state_.live&&state_.awaiting_input&&vm_&&vm_->input_waiting(),"Input32 target needs live state8 context");
    return {state_.generation,state_.input_requests,state_.pc,vm_->context_value()};
}
GameFsStatus GameActorScript::CompleteInput(const GameChoiceTarget& expected,std::string text,std::string auxiliary,bool timed_out){
    try{
        const auto current=InputTarget();const auto owner=expected.context.context_owner.lock();
        Need(owner&&owner==current.context.context_owner.lock()&&expected.context.word==current.context.word
             &&expected.generation==current.generation&&expected.request==current.request&&expected.pc==current.pc,
             "Input32 completion target generation/registry/request changed");
        Need(!timed_out||text.empty(),"Expired Input32 must publish nil pair");
        Need(vm_->complete_input(text,auxiliary),"Input32 completion refused");
        state_.input_text=std::move(text);state_.input_auxiliary=std::move(auxiliary);
        state_.input_timed_out=timed_out;state_.awaiting_input=false;++state_.input_completions;
        //682B8 publishes/drains before timeout event20;cancel/accepted empty
        //with remaining time does not emit it. Suppression clears last.
        GameFsStatus status;if(timed_out)status=PostMainFlowEvent(20,0);
        state_.source_main_input_suppressed48=false;return status;
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameInventoryActionAdmission GameActorScript::PostInventoryAction(uint64_t generation,uint32_t event,int32_t row){
    GameInventoryActionAdmission out;
    try{
        Need(state_.live&&vm_&&state_.inventory_requested&&generation==state_.inventory_requests&&!state_.inventory_action.pending,
             "Inventory action owner/generation unavailable");
        Need((event==13||event==14||event==15||event==16)&&row>=0&&uint32_t(row)<state_.inventory_items.size(),"Inventory action event/row domain");
        Need(!state_.blocked&&!state_.awaiting_message&&!state_.awaiting_choice&&!state_.awaiting_input&&!vm_->context_enabled()
             &&!vm_->callback_depth()&&!vm_->queued_callbacks(),"Inventory action requires its suspended nonmodal callback owner");
        CaptureInventoryItems();const auto item=state_.inventory_items.at(size_t(row));
        Need(item.valid&&item.source_kinds[1]==uint8_t(Value::Kind::Scalar),"Inventory source model field domain");
        uint32_t gate_word=0;
        if(event==13||item.source_words[1]>>28){
            Need(runtime_&&runtime_->OwnersLive(),"Inventory model operation requires its live runtime owner");
            const auto gate=vm_->load_reference(vm_->indexed_reference(script::Value{Value::Kind::Reference,0,script::Table::Ctx28,0x243F},0));
            Need(gate.kind==Value::Kind::Scalar&&gate.word==0x20000000u,"Inventory model operation requires actual source integer-zero owner gate");gate_word=gate.word;
            if(item.source_words[1]>>28){
                const auto slot=Value{Value::Kind::Scalar,item.source_words[1]}.signed_integer();const auto room=runtime_->Snapshot();
                Need(slot>=0&&size_t(slot)<room.room_models.size()&&room.room_models[size_t(slot)].active
                     &&!room.room_models[size_t(slot)].retirement&&room.room_models[size_t(slot)].resource==item.id,
                     "Inventory placed model owner no longer belongs to this row");
            }else Need(item.source_words[1]==0,"Decoration requires canonical NIL model field");
        }else Need(item.source_words[1]==0,"Inventory model field must be canonical NIL");
        const auto registered=bindings_.at(35);
        Need(registered.kind==Value::Kind::Reference&&registered.table==script::Table::Global&&registered.index==0x4FAD,"GAME35 registration differs from source Global4FAD owner");
        const auto rowref=vm_->load_reference(vm_->indexed_reference(registered,uint32_t(row)));
        const auto header=vm_->load_reference(rowref);const auto before=vm_->current_witness();const auto target=OwnerEventTarget();
        Need(rowref.kind==Value::Kind::Reference&&header.kind==Value::Kind::ArrayHeader&&before.valid&&!before.event_envelope_present
             &&target.callback.word==0x7008DA28u,"Inventory source row/callback identity");
        if(event==16)Need((header.word&0xFFF)==10,"Discard finite actual ten-cell row domain");
        Value definition;uint32_t definition_mask=0,definition_width=0;
        if(event==13){
            Value definitions;definitions.kind=Value::Kind::Reference;definitions.table=script::Table::Global;definitions.index=0x44ED;
            definition=vm_->load_reference(vm_->indexed_reference(definitions,uint32_t(item.id)));
            Need(definition.kind==Value::Kind::Reference,"Decoration actual item definition reference");
            const auto mask=vm_->load_reference(vm_->indexed_reference(definition,7)),width=vm_->load_reference(vm_->indexed_reference(definition,8));
            Need(mask.kind==Value::Kind::Scalar&&mask.word>>28==2&&width.kind==Value::Kind::Scalar&&width.word>>28==2,"Decoration reached integer definition domain");
            definition_mask=mask.word;definition_width=width.word;
        }
        out.status=PostMainFlowEvent(event,row,&target);out.queue_result=state_.owner_event_result;
        if(out.status&&out.queue_result==0){
            const auto entered=vm_->current_witness();const auto count=state_.inventory_action.completions;
            state_.inventory_action={};auto& a=state_.inventory_action;a.pending=true;a.completions=count;
            a.generation=generation;a.receipt=out.receipt=vm_->last_callback_receipt();a.callback_frame=entered.frame_id;
            a.event=event;a.row=uint32_t(row);a.item_id=item.id;a.before_words=item.source_words;a.before_kinds=item.source_kinds;
            a.before_controls=item.control_words;a.before_control_kinds=item.control_kinds;a.gate_word=gate_word;
            if(event==16){a.row_cells=10;for(uint32_t i=0;i<a.row_cells;++i){const auto v=vm_->load_reference(vm_->indexed_reference(rowref,i));a.before_row_words[i]=v.word;a.before_row_kinds[i]=uint8_t(v.kind);}}
            if(event==13){a.definition_table=uint32_t(definition.table);a.definition_index=definition.index;
                a.definition_header=vm_->load_reference(definition).word;a.definition_mask=definition_mask;a.definition_width=definition_width;}
            a.row_table=uint32_t(rowref.table);a.row_index=rowref.index;a.row_header=header.word;a.suspended=before;
        }
    }catch(const std::exception& e){out.status={GameFsCode::Unavailable,0,e.what()};}
    return out;
}
void GameActorScript::RecordInventoryModelService(uint32_t id,int32_t slot,int32_t resource,int32_t category,int32_t selector,int32_t x,uint32_t result,bool returned){
    auto& a=state_.inventory_action;if(!a.pending)return;
    Need(a.model_service_count<a.model_services.size(),"Inventory bounded model operation evidence overflow");
    const auto w=vm_->current_witness();Need(w.event_envelope_present,"Inventory model operation lost callback owner");
    a.model_services[a.model_service_count++]={returned,id,w.current_pc,w.code_entry,result,a.receipt,w.frame_id,slot,resource,category,selector,x};
}
void GameActorScript::ObserveInventoryAction(const script::FrameTransition& t)noexcept{
    auto& a=state_.inventory_action;if(!a.pending||a.returned||a.failed)return;
    const auto& x=t.before;const auto& y=t.after;const auto& s=a.suspended;
    if(t.callback_receipt!=a.receipt||x.run_id!=s.run_id||y.run_id!=s.run_id||x.code_sha256!=s.code_sha256||y.context_word!=s.context_word){a.failed=true;return;}
    if(t.opcode!=0x32)return;
    a.restored=y;
    const auto same=[](const script::ValueWitness& l,const script::ValueWitness& r){return l.kind==r.kind&&l.word==r.word&&l.table==r.table&&l.index==r.index&&l.cp932_bytes==r.cp932_bytes&&l.cp932_sha256==r.cp932_sha256;};
    bool valid=x.frame_id==a.callback_frame&&x.code_entry==0x8DA28&&x.current_pc==0x8DD8B&&x.event_envelope_present&&!y.event_envelope_present
        &&y.frame_id==s.frame_id&&y.code_entry==s.code_entry&&y.current_pc==s.current_pc&&y.call_depth==s.call_depth
        &&y.active_blocks==s.active_blocks&&y.event_scope_depth==s.event_scope_depth+1&&y.operand_cells==s.operand_cells&&y.frame_cells==s.frame_cells
        &&same(y.result,s.result)&&y.deadline_tick==(s.relative_deadline23>0?y.clock_tick+uint32_t(s.relative_deadline23):0);
    for(uint32_t i=0;i<s.frame_captured;++i)valid=valid&&same(s.frame_values[i],y.frame_values[i]);
    for(uint32_t i=0;i<s.operands_captured;++i)valid=valid&&same(s.operands[i],y.operands[i]);
    a.failed=!valid;a.returned=valid;
}
bool GameActorScript::CanResumeInventory()const{
    return state_.inventory_requested&&vm_&&!vm_->context_enabled()&&vm_->callbacks_returned()>0;
}
GameFsStatus GameActorScript::CompleteInventory(const GameInventorySnapshot& gui){
    try{
        Need(state_.live&&CanResumeInventory()&&gui.generation==state_.inventory_requests,"Inventory stale or unready context resume");
        Need(gui.phase==7&&!gui.live&&!gui.owner480&&!gui.request14&&gui.stateB2C==3&&gui.loader468==1&&gui.loader498==0&&gui.retirements>0,
             "Native modal resume requires genuine close,poll completion and deferred retirement");
        vm_->resume_modal_context();state_.inventory_requested=false;++state_.inventory_completions;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameOwnerEventTarget GameActorScript::OwnerEventTarget()const{
    Need(state_.live&&vm_&&program_.header().local_count>0,"Owner event target requires live context/local owner");
    return {state_.generation,vm_->context_value(),vm_->read_slot(script::Table::Local,0)};
}
GameFsStatus GameActorScript::PostMainFlowEvent(uint32_t event_id,int32_t argument,const GameOwnerEventTarget* expected){
    try{
        Need(state_.live&&vm_&&program_.header().local_count>0,"Main-flow event requires live head context/local owner");
        //Current finite manager list has this one real context. ctx24 points
        //to Local[0],not a callback table or a new synthetic VM.
        const auto target=OwnerEventTarget();const auto& code=target.callback;
        if(expected){
            const auto old_owner=expected->context.context_owner.lock(),current_owner=target.context.context_owner.lock();
            Need(expected->generation==target.generation&&old_owner&&old_owner==current_owner
                 &&expected->context.word==target.context.word&&expected->callback.kind==code.kind&&expected->callback.word==code.word,
                 "Native interaction target registry/generation/callback changed or retired");
        }
        state_.owner_event_callback_word=code.word;
        state_.owner_event_result=vm_->post_owner_event(code,int32_t(event_id),argument);
        ++state_.owner_event_calls;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::PostConversationInteraction(const GameOwnerEventTarget& target,uint64_t origin,bool owned_dispatch,int32_t& result){
    if(!vm_||!origin)return {GameFsCode::Unavailable,0,"Conversation transaction needs its live source activation"};
    const auto before=vm_->current_witness();const auto previous=owned_conversation_.Snapshot(*vm_);
    const auto main=before.code_entry==0x8DDB5?before.frame_id:previous.main_frame;
    if(!vm_->owns_active_frame(main,0x8DDB5)
       ||(owned_dispatch?(!previous.accepted||!previous.completed||!previous.dispatch_ready)
                         :(before.code_entry!=0x8DDB5||before.call_depth!=1)))
        return {GameFsCode::Unavailable,0,"Conversation transaction has no current owning main frame"};
    const auto status=PostMainFlowEvent(0,0,&target);result=state_.owner_event_result;
    if(status&&result==0){
        owned_conversation_.SupersedePostModal();
        if(!owned_conversation_.Begin(before,state_.generation,origin,vm_->last_callback_receipt(),main))
            return {GameFsCode::Unavailable,0,"Accepted conversation event could not bind its source receipt"};
    }
    return status;
}
GameFsStatus GameActorScript::PostPostModalInteraction(const GameOwnerEventTarget& target,uint64_t post,uint64_t inventory,
                                                     uint64_t sequence,int32_t& result){
    result=-1;
    if(!vm_||!post||!inventory||!sequence||owned_conversation_.State().accepted)
        return {GameFsCode::Unavailable,0,"Post-modal transaction needs its unclaimed owning input"};
    const auto before=vm_->current_witness();const auto main=vm_->unique_active_frame(0x8DDB5);
    const bool observe=main&&!before.event_envelope_present&&!before.queued_callbacks
        &&target.callback.kind==script::Value::Kind::Code&&target.callback.word==0x7008DA28u;
    const auto status=PostMainFlowEvent(0,0,&target);result=state_.owner_event_result;
    if(status&&result==0&&observe&&!owned_conversation_.BeginPostModal(before,state_.generation,post,inventory,sequence,vm_->last_callback_receipt(),main)){
        ++state_.post_modal_observation_refusals;owned_conversation_.SupersedePostModal();
    }
    return status;
}
GameActorScriptSnapshot GameActorScript::Snapshot() const {
    auto out=state_;out.dictionary=dictionary_.Snapshot();if(records_)out.records=records_->Snapshot();if(vm_){out.owned_conversation=owned_conversation_.Snapshot(*vm_);out.instructions=vm_->executed();out.call_depth=vm_->call_depth();out.random_selections=vm_->random_selections();out.selected_value_word=vm_->last_random_value().word;out.root_functions_returned=vm_->root_functions_returned();
        out.b4_calls=vm_->b4_calls();out.b4_operand_word=vm_->b4_operand_word();out.b4_result_word=vm_->b4_result_word();
        out.e2_calls=vm_->e2_calls();out.e2_left_word=vm_->e2_left_word();out.e2_right_word=vm_->e2_right_word();out.e2_result_word=vm_->e2_result_word();
        out.d0_calls=vm_->d0_calls();out.d0_left_word=vm_->d0_left_word();out.d0_right_word=vm_->d0_right_word();out.d0_result_word=vm_->d0_result_word();
        out.d5_calls=vm_->d5_calls();out.d5_first_pc=vm_->d5_first_pc();out.d5_first_left_word=vm_->d5_first_left_word();out.d5_first_right_word=vm_->d5_first_right_word();out.d5_first_result_word=vm_->d5_first_result_word();
        out.ctx_reference_branches=vm_->ctx_reference_branches();out.ctx_reference_first_pc=vm_->ctx_reference_first_pc();out.ctx_reference_first_index=vm_->ctx_reference_first_index();
        out.native_reserved_local_nil=vm_->native_reserved_local_nil();
        out.vm_current=vm_->current_witness();out.normal_return=vm_->last_normal_return();out.farewell_return=vm_->last_normal_return(1);
        if(word_consumer_row_index_!=UINT32_MAX)out.word_consumer_row=ObserveWordRow(word_consumer_row_index_);
        if(out.vm_current.frame_captured&&out.vm_current.frame_values[0].kind==Value::Kind::Scalar&&out.vm_current.frame_values[0].word>>28==2){
            const auto index=out.vm_current.frame_values[0].word&0x0FFFFFFF;
            if(index<600)out.candidate_word_row=ObserveWordRow(index);
        }
        out.b3_calls=vm_->b3_calls();out.b3_pc=vm_->b3_pc();out.b3_operand_kind=vm_->b3_operand_kind();out.b3_operand_word=vm_->b3_operand_word();out.b3_result_word=vm_->b3_result_word();
        out.context_enabled=vm_->context_enabled();out.callbacks_posted=vm_->callbacks_posted();out.callbacks_returned=vm_->callbacks_returned();
        out.context_word=vm_->context_value().word;out.callback_depth=vm_->callback_depth();out.queued_callbacks=vm_->queued_callbacks();out.operand_count=uint32_t(vm_->stack().size());out.deadline_tick=vm_->deadline_tick();
        if(program_.header().local_count)out.owner_event_callback_word=vm_->read_slot(script::Table::Local,0).word;
        out.control_blocks_entered=vm_->control_blocks_entered();out.control_blocks_closed=vm_->control_blocks_closed();
        out.active_control_blocks=vm_->active_control_blocks();out.switches=vm_->switches_executed();
        out.waiting_for_event=vm_->waiting_for_event();out.waiting_deadline=vm_->deadline_waiting();
        out.source_timestamp_writes=vm_->timestamp_writes();out.source_deadline_writes=vm_->deadline_writes();
    }if(runtime_){out.runtime=runtime_->Snapshot();out.source_flag2A980=out.runtime.source_flag2A980!=0;}return out;
}
void GameActorScript::Close() {
    owned_conversation_.Close();
    ascii_class70_.Close();state_.ascii_class70_live=false;state_.ascii_class70_generation=0;
    state_.ascii_class70_current_word=ascii_class70_.CurrentWord();state_.ascii_class70_other_word=ascii_class70_.OtherWord();
    state_=Snapshot();
    word_consumer_row_index_=UINT32_MAX;
    dictionary_.Close();bindings_.clear();vm_.reset();program_={};script_image_.Reset();profile_={};clock_={};rng_=nullptr;runtime_=nullptr;records_=nullptr;sound_=nullptr;
    state_.live=false;state_.awaiting_message=state_.awaiting_choice=state_.awaiting_input=false;
    state_.source_main_input_suppressed48=false;
}
GameWordRowObservation GameActorScript::ObserveWordRow(uint32_t row)const noexcept{
    if(state_.live&&vm_)return GameObserveWordRow(*vm_,state_.generation,row);
    GameWordRowObservation out;out.generation=state_.generation;out.row=row;out.error_code=1;return out;
}
} // namespace dokodemo::recovered
