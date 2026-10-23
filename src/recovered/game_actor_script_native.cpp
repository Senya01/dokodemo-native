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
bool SameRegistration(const Value& a,const Value& b){
    return a.kind==Value::Kind::Reference&&b.kind==a.kind&&a.word==b.word&&a.table==b.table&&a.index==b.index
        &&!a.frame_owner.owner_before(b.frame_owner)&&!b.frame_owner.owner_before(a.frame_owner)
        &&!a.context_owner.owner_before(b.context_owner)&&!b.context_owner.owner_before(a.context_owner);
}
Value False() {Value v;v.kind=Value::Kind::Scalar;v.word=0x10000000;return v;}
Value Nil() {Value v;v.kind=Value::Kind::Scalar;return v;}
script::ValueWitness PassiveValue(const Value& v){
    script::ValueWitness w;w.kind=v.kind;w.word=v.word;w.table=uint32_t(v.table);w.index=v.index;
    w.cp932_bytes=v.cp932.size();w.cp932_sha256=core::sha256(reinterpret_cast<const uint8_t*>(v.cp932.data()),v.cp932.size());return w;
}
std::array<script::ValueWitness,4> Flow5Guards(const script::Vm& vm){
    return {PassiveValue(vm.read_slot(script::Table::Global,0x9A76)),PassiveValue(vm.read_slot(script::Table::Ctx28,0x2869)),
            PassiveValue(vm.read_slot(script::Table::Ctx28,0x2866)),PassiveValue(vm.read_slot(script::Table::Global,0x6105))};
}
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
bool GameMenuCallbackBoundaryAccepted(const script::Event& event,bool restored32,bool disabled)noexcept{
    return event.status==script::Status::HostYield||(event.status==script::Status::Yield&&event.opcode==0x32
        &&event.pc==0x8DD8B&&restored32&&disabled);
}
bool GameFlow5EventRestored(const GameFlow5EventSnapshot& c,const script::FrameTransition& t)noexcept{
    const auto& s=c.suspended;const auto& x=t.before;const auto& y=t.after;
    const auto same=[](const script::ValueWitness& a,const script::ValueWitness& b){return a.kind==b.kind&&a.word==b.word&&a.table==b.table&&a.index==b.index&&a.cp932_bytes==b.cp932_bytes&&a.cp932_sha256==b.cp932_sha256;};
    if(!c.accepted||c.queued||!c.receipt||t.callback_receipt!=c.receipt||(t.opcode!=0x32&&t.opcode!=0x1C)
       ||!s.valid||!x.valid||!y.valid||!s.context_registered||!x.context_registered||!y.context_registered
       ||s.stopped||x.stopped||y.stopped||s.event_envelope_present||s.queued_callbacks
       ||s.frame_cells!=s.frame_captured||s.operand_cells!=s.operands_captured
       ||s.frame_captured>s.frame_values.size()||s.operands_captured>s.operands.size()
       ||!c.entered.valid||!c.entered.context_registered||c.entered.stopped||!c.entered.event_envelope_present
       ||c.entered.run_id!=s.run_id||c.entered.context_word!=s.context_word||c.entered.code_sha256!=s.code_sha256
       ||c.entered.code_entry!=0x8DA28||c.entered.frame_id!=c.callback_frame||c.entered.frame_captured!=2
       ||c.entered.frame_values[0].kind!=Value::Kind::Scalar||c.entered.frame_values[0].word!=0x2000000C
       ||c.entered.frame_values[1].kind!=Value::Kind::Scalar||c.entered.frame_values[1].word!=0x20000005
       ||x.frame_id!=c.callback_frame||x.code_entry!=0x8DA28||!x.event_envelope_present||y.event_envelope_present
       ||x.run_id!=s.run_id||y.run_id!=s.run_id||x.context_word!=s.context_word||y.context_word!=s.context_word
       ||x.code_sha256!=s.code_sha256||y.code_sha256!=s.code_sha256||y.frame_id!=s.frame_id||y.code_entry!=s.code_entry
       ||y.current_pc!=s.current_pc||y.caller_entry!=s.caller_entry||y.caller_call_pc!=s.caller_call_pc
       ||y.caller_return_pc!=s.caller_return_pc||y.caller_frame_id!=s.caller_frame_id||y.caller_stack_base!=s.caller_stack_base
       ||y.call_depth!=s.call_depth||y.active_blocks!=s.active_blocks||y.event_scope_depth!=s.event_scope_depth+1
       ||y.queued_callbacks!=s.queued_callbacks||y.frame_cells!=s.frame_cells||y.operand_cells!=s.operand_cells
       ||y.frame_captured!=s.frame_captured||y.operands_captured!=s.operands_captured||!same(s.result,y.result)
       ||y.executed!=x.executed+1||y.clock_tick!=x.clock_tick
       ||y.deadline_tick!=(s.relative_deadline23>0?y.clock_tick+uint32_t(s.relative_deadline23):0))return false;
    for(uint32_t i=0;i<s.frame_captured;++i)if(!same(s.frame_values[i],y.frame_values[i]))return false;
    for(uint32_t i=0;i<s.operands_captured;++i)if(!same(s.operands[i],y.operands[i]))return false;
    return true;
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
GameInventoryRowsObservation GameObserveInventoryRows(const script::Vm& vm,uint64_t generation,const Value& registered,std::vector<GameInventoryItem>& items,GameInventoryRowsImage* image){
    const auto current=vm.current_witness();
    Need(current.valid&&current.run_id&&registered.kind==Value::Kind::Reference
         &&registered.table==script::Table::Global&&registered.index==0x4FAD,"Inventory actual GAME35/Global4FAD registration required");
    GameInventoryRowsObservation out;out.generation=generation;out.run_id=current.run_id;out.code_sha256=current.code_sha256;
    out.table=uint32_t(registered.table);out.index=registered.index;
    std::vector<uint8_t> bytes;std::vector<GameInventoryItem> next;GameInventoryRowsImage captured;
    const auto append=[&](uint32_t n){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(n>>(8*i)));};
    const auto capture=[&](const Value& v){Need(v.kind==Value::Kind::String||v.cp932.empty(),"Inventory non-string value owns malformed text");append(uint32_t(v.kind));append(v.word);append(uint32_t(v.table));append(v.index);
        append(uint32_t(v.kind==Value::Kind::String?v.cp932.size():0));if(v.kind==Value::Kind::String)bytes.insert(bytes.end(),v.cp932.begin(),v.cp932.end());};
    const auto at=[&](Value root,uint32_t n){const auto index=uint32_t(root.index)+1+n;Need(index<=UINT16_MAX,"Inventory typed row extent");root.index=uint16_t(index);return vm.peek_reference(root);};
    const auto header=vm.peek_reference(registered);Need(header.kind==Value::Kind::ArrayHeader&&(header.word&0xFFF)==170,"Inventory actual170-row table");
    out.header=header.word;out.rows=170;capture(registered);capture(header);
    for(uint32_t i=0;i<170;++i){
        const auto row=at(registered,i);Need(row.kind==Value::Kind::Reference&&(row.table==script::Table::Global||row.table==script::Table::Local||row.table==script::Table::Ctx28),"Inventory stable typed row owner");
        const auto row_header=vm.peek_reference(row);const auto count=row_header.word&0xFFF;
        Need(row_header.kind==Value::Kind::ArrayHeader&&count>=10&&count<=64,"Inventory finite actual row header");
        capture(row);capture(row_header);std::array<Value,10> first;
        auto& shape=captured.rows[i];shape.table=uint32_t(row.table);shape.index=row.index;shape.header=row_header.word;
        shape.cell_offsets.resize(count);
        for(uint32_t n=0;n<count;++n){const auto value=at(row,n);
            Need(bytes.size()<=UINT32_MAX,"Inventory canonical image extent");
            shape.cell_offsets[n]=uint32_t(bytes.size());
            if(n==5)shape.flag_offset=uint32_t(bytes.size());if(n==6)shape.model_offset=uint32_t(bytes.size());
            capture(value);if(n<10)first[n]=value;}
        out.cells+=count;GameInventoryItem item;item.row=i;
        if(first[0].word>>28){item.id=Number(first[0]);item.valid=item.id>=0;
            if(item.valid){Need(first[1].kind==Value::Kind::String&&first[2].kind==Value::Kind::String,"Inventory owned display strings");
                item.name=first[1].cp932;item.description=first[2].cp932;item.date_word=first[4].word&0x0FFFFFFF;
                item.category=Number(first[5])==1?2:((first[7].word>>28)==0?1:0);
                for(uint32_t n=0;n<3;++n){item.source_words[n]=first[5+n].word;item.source_kinds[n]=uint8_t(first[5+n].kind);
                    const auto k=n==0?3u:n+7;item.control_words[n]=first[k].word;item.control_kinds[n]=uint8_t(first[k].kind);}
                ++out.valid_items;}}
        next.push_back(std::move(item));
    }
    out.sha256=core::sha256(bytes.data(),bytes.size());out.valid=true;
    if(image){captured.observation=out;captured.bytes=std::move(bytes);*image=std::move(captured);}
    items=std::move(next);return out;
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
        vm_->access_observer([this](const Value& ref,bool write){ObserveNotebookStore(ref,write);});
        owned_conversation_=GameOwnedConversation{};
        vm_->frame_observer([this](const script::FrameTransition& t){owned_conversation_.Observe(t,*vm_);ObserveInventoryAction(t);ObserveWordSelection(t);ObserveMenuCallback(t);ObserveFlow5Event(t);});
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
        const bool placement=state_.inventory_action.pending&&state_.inventory_action.placement;
        const auto before=runtime_->Snapshot();if(placement)menu_placement_.BeforeModel(*vm_,31,arg,before);
        const auto field=[&](uint32_t index){return Number(vm_->load_reference(vm_->indexed_reference(arg,index)));};
        const auto resource=field(0),category=field(3),motion=field(8),x=field(9);
        int32_t slot=-1;const auto status=runtime_->CreateModel(resource,category,motion,float(x),slot);
        const auto result=Value::integer(slot);
        RecordInventoryModelService(31,slot,resource,category,motion,x,status?result.word:0,bool(status));
        if(status&&placement)menu_placement_.AfterModel(*vm_,31,arg,result,before,runtime_->Snapshot());
        Need(bool(status),status.detail.c_str());return result;
    }
    case 32: {
        Need(runtime_!=nullptr,"GAME32 requires live model slot owner");const auto slot=Number(arg);
        const bool placement=state_.inventory_action.pending&&state_.inventory_action.placement;
        const auto before=runtime_->Snapshot();if(placement)menu_placement_.BeforeModel(*vm_,32,arg,before);
        const auto status=runtime_->RetireModel(slot);
        RecordInventoryModelService(32,slot,-1,0,0,0,status?False().word:0,bool(status));
        if(status&&placement)menu_placement_.AfterModel(*vm_,32,arg,False(),before,runtime_->Snapshot());
        Need(bool(status),status.detail.c_str());return False();
    }
    case 33: {
        Need(runtime_!=nullptr,"GAME33 requires live model slot owner");
        const bool placement=state_.inventory_action.pending&&state_.inventory_action.placement;
        const auto before=runtime_->Snapshot();if(placement)menu_placement_.BeforeModel(*vm_,33,arg,before);
        const auto field=[&](uint32_t index){return Number(vm_->load_reference(vm_->indexed_reference(arg,index)));};
        const auto slot=field(6),selector=field(8),x=field(9);
        const auto status=runtime_->UpdateModel(slot,selector,float(x));
        RecordInventoryModelService(33,slot,field(0),field(3),selector,x,status?False().word:0,bool(status));
        if(status&&placement)menu_placement_.AfterModel(*vm_,33,arg,False(),before,runtime_->Snapshot());
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
        (void)vm_->load_reference(arg);bindings_[uint32_t(id)]=arg;
        if(id==11)++state_.notebook_registrations;
        if(id==10){++state_.wordmemo_registrations;state_.wordmemo_registration_source=vm_->current_witness();state_.wordmemo_import_alias=SameRegistration(arg,dictionary_import_target_);}
        return False();
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
        dictionary_import_target_=arg;++state_.wordmemo_imports;state_.wordmemo_import_source=vm_->current_witness();
        state_.wordmemo_import_cells=dictionary_.Snapshot().copied_values;
        if(const auto registered=bindings_.find(10);registered!=bindings_.end()){
            state_.wordmemo_import_alias=SameRegistration(registered->second,dictionary_import_target_);
        }
        // Register after import's internal validation. These counts describe
        // subsequent executed game queries/mutations,not a parser self-check.
        vm_->access_observer([this](const Value& ref,bool write){
            ObserveNotebookStore(ref,write);
            ObserveInventoryStore(ref,write);
            if(dictionary_.OwnsCell(ref)){if(write)++state_.dictionary_game_writes;else ++state_.dictionary_game_reads;}
            // Passive proof of the reached DBC descending row-clear consumer.
            // Peek never invokes this callback or supplies VM values/results.
            if(write&&vm_->pc()==0xDDF){
                try{
                    const auto current=vm_->current_witness();
                    const auto row=vm_->read_slot(script::Table::Frame,0),record=vm_->read_slot(script::Table::Frame,1);
                    const auto index=vm_->read_slot(script::Table::Frame,2);
                    if(current.code_entry==0xDBC&&row.kind==Value::Kind::Scalar&&row.word>>28==2
                       &&index.kind==Value::Kind::Scalar&&index.word>>28==2&&(index.word&0x0FFFFFFF)<19){
                        const auto observed=ObserveWordRow(row.word&0x0FFFFFFF);
                        if(observed.valid&&record.kind==Value::Kind::Reference&&uint32_t(record.table)==observed.table&&record.index==observed.index
                           &&ref.table==record.table&&uint32_t(ref.index)==uint32_t(record.index)+1+(index.word&0x0FFFFFFF)){
                            ++state_.word_clear_writes;state_.word_clear_row=observed;
                        }
                    }
                }catch(...){/* Evidence failure does not change execution. */}
            }
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
        const bool placement=state_.inventory_action.pending&&state_.inventory_action.placement;
        if(placement&&id==0x60)menu_placement_.BeforeFilter(*vm_,args);
        const auto result=id==0x60?GameValueArray96(*vm_,args):id==0x61?GameValueArray97(*vm_,args):id==0x62?GameValueArray98(*vm_,args):GameValueArray99(*vm_,args);
        if(placement&&id==0x60)menu_placement_.AfterFilter();
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
        CompleteInventoryActionReadback();
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
void GameActorScript::CompleteInventoryActionReadback(){
        auto& action=state_.inventory_action;
        Need(!action.pending||!action.failed,"Inventory callback receipt/restoration failed");
        if(action.pending&&action.returned){
            Need(!action.failed&&!vm_->context_enabled()&&vm_->callback_depth()==action.suspended.event_scope_depth,"Inventory action failed to restore suspended disabled context/scope");
            const auto registered=bindings_.at(35);const auto rowref=vm_->load_reference(vm_->indexed_reference(registered,action.row));
            Need(rowref.kind==Value::Kind::Reference&&uint32_t(rowref.table)==action.row_table&&rowref.index==action.row_index
                 &&vm_->load_reference(rowref).word==action.row_header,"Inventory action source row replaced");
            GameInventoryRowsImage approved;
            if(action.menu_origin){
                (void)GameObserveInventoryRows(*vm_,state_.generation,bindings_.at(35),state_.inventory_items,&approved);
                if(!action.placement)Need(action.model_service_count==0,"NIL6 mark callback reached an unsupported model service");
            }else CaptureInventoryItems();
            const auto& item=state_.inventory_items.at(action.row);
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
            if(action.menu_origin){
                if(action.placement){
                    menu_placement_.Commit(*vm_,approved,menu_mark_);
                    auto& cert=action.certificate;cert.after_display=GameInventoryItemsHash(state_.inventory_items);cert.approved_image=approved.observation.sha256;
                    for(const auto& store:menu_placement_.Stores())cert.mutable_fields[store.row]|=1u<<store.field;
                    cert.valid=true;
                }else{
                Need(action.nil6_returns==1&&action.flag_returns==1&&action.protected_writes==0
                     &&action.nil6_return_executed<action.flag_store_executed&&action.flag_store_executed<action.flag_return_executed,
                     "Menu mark helper return/store ordering or protected write proof failed");
                menu_mark_.Commit(approved,action.event,action.row,action.receipt,action.flag_store_pc,action.flag_store_count);
                }
                state_.menu_inventory_verified=approved.observation;state_.menu_mark=menu_mark_.Snapshot();
            }
            action.pending=false;action.completed=true;++action.completions;
        }
}
GameFsStatus GameActorScript::PostMenuEvent(const GameOwnerEventTarget& expected,uint32_t event,uint64_t generation,int32_t& queue){
    queue=-1;
    try{
        Need(state_.live&&vm_&&generation&&(event==10||event==11||event==18)&&!state_.menu_callback.pending,"Menu callback owner/event unavailable");
        Need(!state_.blocked&&!state_.awaiting_message&&!state_.awaiting_choice&&!state_.awaiting_input
             &&!vm_->callback_depth()&&!vm_->queued_callbacks(),"Menu callback requires its idle owning VM");
        const auto target=OwnerEventTarget();
        Need(expected.generation==target.generation&&expected.context.word==target.context.word
             &&!expected.context.context_owner.expired()&&expected.context.context_owner.lock()==target.context.context_owner.lock()
             &&expected.callback.kind==target.callback.kind&&expected.callback.word==target.callback.word
             &&target.callback.word==0x7008DA28u,"Menu callback registry/callback replaced");
        const auto saved=vm_->current_witness();
        Need(saved.valid&&!saved.event_envelope_present&&vm_->context_enabled(),"Menu callback needs its live enabled suspended caller");
        const auto flag=vm_->read_slot(script::Table::Global,0x9A78);
        queue=vm_->post_owner_nil_event(target.callback,int32_t(event));
        state_.owner_event_callback_word=target.callback.word;state_.owner_event_result=queue;++state_.owner_event_calls;
        if(queue==0){
            auto& c=state_.menu_callback;c={};c.pending=true;c.event=event;c.menu_generation=generation;
            c.receipt=vm_->last_callback_receipt();c.callback_frame=vm_->current_witness().frame_id;c.suspended=saved;
            c.flag_before=flag.word;c.kind_before=uint8_t(flag.kind);c.enabled_before=vm_->context_enabled();
        }
        return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
void GameActorScript::ObserveMenuCallback(const script::FrameTransition& t)noexcept{
    auto& c=state_.menu_callback;if(!c.pending||c.returned||c.failed||t.opcode!=0x32)return;
    try{
        const auto& x=t.before;const auto& y=t.after;const auto& s=c.suspended;
        const auto same=[](const script::ValueWitness& a,const script::ValueWitness& b){return a.kind==b.kind&&a.word==b.word&&a.table==b.table&&a.index==b.index&&a.cp932_bytes==b.cp932_bytes&&a.cp932_sha256==b.cp932_sha256;};
        bool valid=t.callback_receipt==c.receipt&&x.frame_id==c.callback_frame&&x.code_entry==0x8DA28&&x.current_pc==0x8DD8B
            &&x.event_envelope_present&&!y.event_envelope_present&&y.run_id==s.run_id&&y.code_sha256==s.code_sha256
            &&y.context_word==s.context_word&&y.frame_id==s.frame_id&&y.code_entry==s.code_entry&&y.current_pc==s.current_pc
            &&y.call_depth==s.call_depth&&y.active_blocks==s.active_blocks&&y.event_scope_depth==s.event_scope_depth+1
            &&y.operand_cells==s.operand_cells&&y.frame_cells==s.frame_cells&&same(y.result,s.result)
            &&y.deadline_tick==(s.relative_deadline23>0?y.clock_tick+uint32_t(s.relative_deadline23):0);
        for(uint32_t i=0;i<s.frame_captured;++i)valid=valid&&same(s.frame_values[i],y.frame_values[i]);
        for(uint32_t i=0;i<s.operands_captured;++i)valid=valid&&same(s.operands[i],y.operands[i]);
        const auto flag=vm_->read_slot(script::Table::Global,0x9A78);c.flag_after=flag.word;c.kind_after=uint8_t(flag.kind);
        c.enabled_after=vm_->context_enabled();
        valid=valid&&flag.kind==Value::Kind::Scalar&&flag.word==(c.event==10?0x10000001u:0x10000000u)
            &&c.enabled_after==(c.event!=18);
        c.return_pc=x.current_pc;c.return_opcode=t.opcode;
        c.restored=y;c.returned=valid;c.failed=!valid;
    }catch(...){c.failed=true;}
}
GameFsStatus GameActorScript::TickMenuOwner(){
    try{
        Need(state_.live&&vm_&&!state_.blocked,"Menu modal VM retired or blocked");
        const auto step_callback=[&]{
            const auto e=vm_->run(1);
            const auto& c=state_.menu_callback;const auto& a=state_.inventory_action;
            const auto w=vm_->current_witness();
            const bool restored=!w.event_envelope_present&&!vm_->callback_depth()&&((c.pending&&c.returned&&!c.failed)
                ||(a.pending&&a.menu_origin&&a.returned&&!a.failed));
            if(!GameMenuCallbackBoundaryAccepted(e,restored,!vm_->context_enabled())){
                std::ostringstream why;why<<"Menu callback reached unsupported VM boundary status="<<int(e.status)
                    <<" pc="<<std::hex<<e.pc<<" opcode="<<e.opcode<<" next="<<e.next_pc<<" "<<e.diagnostic.text();
                state_.pc=e.pc;state_.last_opcode=uint32_t(e.opcode);state_.message=e.message_cp932;
                throw std::runtime_error(why.str());
            }
        };
        vm_->clock_tick(++state_.native_clock_ticks);
        if(runtime_){const auto status=runtime_->AdvanceActorFrame();Need(bool(status),status.detail.c_str());}
        auto& c=state_.menu_callback;
        if(c.pending){
            for(uint32_t i=0;i<256&&!c.returned&&!c.failed;++i){
                step_callback();
            }
            Need(!c.failed,"Menu callback did not restore its source envelope/typed flag");
            Need(c.returned,"Menu callback exceeded finite instruction domain");
            const auto after=vm_->current_witness();
            Need(after.event_scope_depth==c.suspended.event_scope_depth&&!after.event_envelope_present,"Menu callback public scope did not restore");
            c.pending=false;
            if(c.event==10)state_.menu_open_return=c;else if(c.event==11)state_.menu_close_return=c;else state_.menu_pause_return=c;
        }
        auto& action=state_.inventory_action;
        if(action.pending&&action.menu_origin){
            for(uint32_t i=0;i<256&&!action.returned&&!action.failed;++i){
                step_callback();
            }
            Need(!action.failed,action.proof_error.empty()?"Menu item callback source/owner proof failed":action.proof_error.c_str());
            if(action.returned)CompleteInventoryActionReadback(); // same receipt continues next tick; no ordinary VM slice
        }
        return {};
    }catch(const std::exception& e){state_.blocked=true;state_.error=e.what();return {GameFsCode::Unavailable,0,e.what()};}
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
    return PostInventoryActionImpl(generation,event,row,false);
}
GameInventoryActionAdmission GameActorScript::PostMenuInventoryAction(const GameOwnerEventTarget& expected,uint64_t generation,uint32_t event,int32_t row){
    GameInventoryActionAdmission out;
    bool prepared=false;
    try{
        Need(!state_.inventory_action.pending&&!menu_placement_.Pending(),"Menu item action already pending");
        Need(state_.menu_inventory_live&&!state_.inventory_requested&&generation==state_.inventory_gui_generation,
             "Menu item request origin/generation replaced");
        const auto current=OwnerEventTarget();const auto& pause=state_.menu_pause_return;
        Need(expected.generation==current.generation&&expected.callback.kind==current.callback.kind&&expected.callback.word==current.callback.word
             &&expected.context.word==current.context.word&&!expected.context.context_owner.expired()
             &&expected.context.context_owner.lock()==current.context.context_owner.lock()
             &&pause.returned&&!pause.failed&&pause.event==18&&pause.menu_generation==state_.menu_inventory_parent_generation,
             "Menu item callback origin/registered owner changed");
        Need((event==13||event==14||event==15)&&row>=0&&row<170,"Menu inventory event domain");
        GameInventoryRowsImage before;std::vector<GameInventoryItem> items;
        (void)GameObserveInventoryRows(*vm_,state_.generation,bindings_.at(35),items,&before);
        const auto& item=items.at(size_t(row));
        const bool placement=event==13||(event==14&&item.source_words[1]!=0);
        if(placement){
            Need(MenuPlacementAvailable(),"Menu placement requires actual home and source gate");menu_mark_.CheckPlacementBefore(before,event,uint32_t(row));
            menu_placement_.Begin(*vm_,before,event,uint32_t(row),runtime_->Snapshot());
            prepared=true;
        }else menu_mark_.CheckBefore(before,event,uint32_t(row));
        Need(item.valid&&item.source_kinds[1]==uint8_t(Value::Kind::Scalar),"Menu item typed model field");
        const auto w=vm_->current_witness();
        Need(w.frame_cells==w.frame_captured&&w.operand_cells==w.operands_captured,
             "Menu item caller exceeds complete witness extent");
        out=PostInventoryActionImpl(generation,event,row,true);
        if(placement){if(out.status&&out.queue_result==0)menu_placement_.Receipt(out.receipt,state_.inventory_action.callback_frame);else menu_placement_.Abort();}
        return out;
    }catch(const std::exception& e){if(prepared)menu_placement_.Abort();out.status={GameFsCode::Unavailable,0,e.what()};return out;}
}
GameInventoryActionAdmission GameActorScript::PostInventoryActionImpl(uint64_t generation,uint32_t event,int32_t row,bool menu){
    GameInventoryActionAdmission out;
    try{
        Need(state_.live&&vm_&&(menu?state_.menu_inventory_live:state_.inventory_requested)
             &&generation==state_.inventory_gui_generation&&!state_.inventory_action.pending,
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
            state_.inventory_action={};auto& a=state_.inventory_action;a.pending=true;a.completions=count;a.menu_origin=menu;
            a.placement=menu&&(event==13||(event==14&&item.source_words[1]!=0));
            a.generation=generation;a.receipt=out.receipt=vm_->last_callback_receipt();a.callback_frame=entered.frame_id;
            a.event=event;a.row=uint32_t(row);a.item_id=item.id;a.before_words=item.source_words;a.before_kinds=item.source_kinds;
            a.before_controls=item.control_words;a.before_control_kinds=item.control_kinds;a.gate_word=gate_word;
            if(event==16){a.row_cells=10;for(uint32_t i=0;i<a.row_cells;++i){const auto v=vm_->load_reference(vm_->indexed_reference(rowref,i));a.before_row_words[i]=v.word;a.before_row_kinds[i]=uint8_t(v.kind);}}
            if(event==13){a.definition_table=uint32_t(definition.table);a.definition_index=definition.index;
                a.definition_header=vm_->load_reference(definition).word;a.definition_mask=definition_mask;a.definition_width=definition_width;}
            a.row_table=uint32_t(rowref.table);a.row_index=rowref.index;a.row_header=header.word;a.suspended=before;
            if(a.placement){auto& cert=a.certificate;cert.generation=generation;cert.receipt=a.receipt;cert.event=event;cert.row=uint32_t(row);cert.before_display=GameInventoryItemsHash(state_.inventory_items);}
        }
    }catch(const std::exception& e){out.status={GameFsCode::Unavailable,0,e.what()};}
    return out;
}
void GameActorScript::ObserveInventoryStore(const Value& ref,bool write)noexcept{
    auto& a=state_.inventory_action;if(!write||!a.pending||!a.menu_origin)return;
    try{
        if(a.placement){
            menu_placement_.ObserveStore(*vm_,ref,menu_mark_);
            if(uint32_t(ref.table)==a.row_table&&ref.index==a.row_index+6){const auto w=vm_->current_witness();++a.flag_store_count;a.flag_store_pc=w.current_pc;a.flag_store_executed=w.executed;a.flag_store_word=vm_->peek_reference(ref).word;}
            return;
        }
        const auto w=vm_->current_witness();
        Need(!menu_placement_.OwnsAuxiliaryCell(ref),"NIL mark callback wrote protected placement dependency");
        if(uint32_t(ref.table)==a.row_table&&uint32_t(ref.index)==a.row_index+6){
            ++a.flag_store_count;a.flag_store_pc=w.current_pc;a.flag_store_executed=w.executed;
            const auto value=vm_->peek_reference(ref);a.flag_store_word=value.word;
            if(w.current_pc!=0x2FDA||w.code_entry!=0x2FBC||!w.event_envelope_present||w.run_id!=a.suspended.run_id
               ||value.kind!=Value::Kind::Scalar||value.word!=(a.event==15?0x10000001u:0x10000000u))a.failed=true;
        }else if(menu_mark_.OwnsCell(uint32_t(ref.table),ref.index)){++a.protected_writes;a.failed=true;}
    }catch(const std::exception& e){a.failed=true;a.proof_error=e.what();}catch(...){a.failed=true;a.proof_error="Unknown placement store failure";}
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
    if(a.placement){try{menu_placement_.ObserveReturn(t);}catch(const std::exception& e){a.failed=true;a.proof_error=e.what();return;}catch(...){a.failed=true;return;}}
    if(a.menu_origin&&t.opcode==0x31){
        if(x.code_entry==0x38DA){
            if(x.current_pc!=(a.placement?0x393Bu:0x38F2u)||y.frame_id!=a.callback_frame||y.code_entry!=0x8DA28)a.failed=true;
            else{++a.nil6_returns;a.nil6_return_executed=y.executed;}
        }
        if(x.code_entry==0x2FBC){
            if(x.current_pc!=0x2FED||y.frame_id!=a.callback_frame||y.code_entry!=0x8DA28)a.failed=true;
            else{++a.flag_returns;a.flag_return_executed=y.executed;}
        }
    }
    if(t.opcode!=0x32)return;
    a.restored=y;
    const auto same=[](const script::ValueWitness& l,const script::ValueWitness& r){return l.kind==r.kind&&l.word==r.word&&l.table==r.table&&l.index==r.index&&l.cp932_bytes==r.cp932_bytes&&l.cp932_sha256==r.cp932_sha256;};
    bool valid=x.frame_id==a.callback_frame&&x.code_entry==0x8DA28&&x.current_pc==0x8DD8B&&x.event_envelope_present&&!y.event_envelope_present
        &&y.frame_id==s.frame_id&&y.code_entry==s.code_entry&&y.current_pc==s.current_pc&&y.call_depth==s.call_depth
        &&y.active_blocks==s.active_blocks&&y.event_scope_depth==s.event_scope_depth+1&&y.operand_cells==s.operand_cells&&y.frame_cells==s.frame_cells
        &&same(y.result,s.result)&&y.deadline_tick==(s.relative_deadline23>0?y.clock_tick+uint32_t(s.relative_deadline23):0);
    for(uint32_t i=0;i<s.frame_captured;++i)valid=valid&&same(s.frame_values[i],y.frame_values[i]);
    for(uint32_t i=0;i<s.operands_captured;++i)valid=valid&&same(s.operands[i],y.operands[i]);
    if(a.menu_origin)valid=valid&&y.context_registered==s.context_registered&&y.queued_callbacks==s.queued_callbacks
        &&y.caller_entry==s.caller_entry&&y.caller_call_pc==s.caller_call_pc&&y.caller_return_pc==s.caller_return_pc
        &&y.caller_frame_id==s.caller_frame_id&&y.caller_stack_base==s.caller_stack_base&&!vm_->context_enabled();
    a.failed=!valid;a.returned=valid;
}
bool GameActorScript::CanResumeInventory()const{
    return state_.inventory_requested&&vm_&&!vm_->context_enabled()&&vm_->callbacks_returned()>0;
}
GameFsStatus GameActorScript::CompleteInventory(const GameInventorySnapshot& gui){
    try{
        Need(state_.live&&CanResumeInventory()&&gui.generation==state_.inventory_gui_generation&&gui.origin==GameInventoryOrigin::Script41,"Inventory stale or unready context resume");
        Need(gui.phase==7&&gui.retired_generation==gui.generation&&!gui.live&&!gui.owner480&&!gui.request14&&gui.stateB2C==3&&gui.loader468==1&&gui.loader498==0&&gui.retirements>0,
             "Native modal resume requires genuine close,poll completion and deferred retirement");
        vm_->resume_modal_context();state_.inventory_requested=false;++state_.inventory_completions;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::BindScriptInventory(uint64_t generation,uint32_t request){
    if(!state_.live||!vm_||!state_.inventory_requested||state_.menu_inventory_live
       ||request!=state_.inventory_requests||!generation||generation<=state_.inventory_gui_generation)
        return {GameFsCode::Unavailable,0,"Script inventory request/GUI binding refused"};
    state_.inventory_gui_generation=generation;return {};
}
GameFsStatus GameActorScript::ReadMenuInventory(const GameOwnerEventTarget& expected,std::vector<GameInventoryItem>& items){
    try{
        Need(state_.live&&vm_&&!state_.blocked&&!vm_->callback_depth()&&!vm_->queued_callbacks(),"Menu inventory read needs its idle VM owner");
        const auto target=OwnerEventTarget();
        Need(expected.generation==target.generation&&expected.context.word==target.context.word
             &&!expected.context.context_owner.expired()&&expected.context.context_owner.lock()==target.context.context_owner.lock()
             &&expected.callback.kind==target.callback.kind&&expected.callback.word==target.callback.word&&target.callback.word==0x7008DA28u,
             "Menu inventory registered context/callback changed");
        (void)GameObserveInventoryRows(*vm_,state_.generation,bindings_.at(35),items);return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::BindMenuInventory(const GameOwnerEventTarget& expected,uint64_t generation,uint64_t menu_generation){
    if(!state_.live||!vm_||!generation||!menu_generation||generation<=state_.inventory_gui_generation||state_.inventory_requested
       ||state_.menu_inventory_live||!vm_->context_enabled())
        return {GameFsCode::Unavailable,0,"Direct menu inventory origin/generation unavailable"};
    std::vector<GameInventoryItem> items;const auto status=ReadMenuInventory(expected,items);if(!status)return status;
    try{
        GameInventoryRowsImage image;(void)GameObserveInventoryRows(*vm_,state_.generation,bindings_.at(35),items,&image);
        GameInventoryMarkChain mark;mark.Begin(image);
        Need(runtime_!=nullptr,"Menu inventory runtime owner missing");GameInventoryPlacementProof placement;placement.BindModels(runtime_->Snapshot());
        menu_mark_=std::move(mark);menu_placement_=std::move(placement);state_.menu_inventory_before=state_.menu_inventory_verified=image.observation;
        state_.menu_mark=menu_mark_.Snapshot();state_.inventory_items=std::move(items);
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
    state_.inventory_gui_generation=generation;state_.menu_inventory_parent_generation=menu_generation;
    state_.menu_inventory_live=true;++state_.menu_inventory_requests;return {};
}
GameFsStatus GameActorScript::CompleteMenuInventory(const GameOwnerEventTarget& expected,const GameInventorySnapshot& gui){
    try{
        Need(state_.live&&vm_&&state_.menu_inventory_live&&!state_.inventory_requested&&!state_.inventory_action.pending
             &&gui.generation==state_.inventory_gui_generation&&gui.origin==GameInventoryOrigin::MainMenu5,"Menu inventory completion origin replaced");
        const auto target=OwnerEventTarget();
        Need(expected.generation==target.generation&&expected.context.word==target.context.word
             &&!expected.context.context_owner.expired()&&expected.context.context_owner.lock()==target.context.context_owner.lock()
             &&expected.callback.kind==target.callback.kind&&expected.callback.word==target.callback.word,"Menu inventory callback owner replaced");
        const auto& pause=state_.menu_pause_return;
        Need(pause.returned&&!pause.failed&&pause.event==18&&pause.menu_generation==state_.menu_inventory_parent_generation
             &&!vm_->context_enabled()&&!vm_->callback_depth()&&!vm_->queued_callbacks(),"Menu inventory needs actual source18 disabled owner");
        Need(gui.phase==7&&gui.retired_generation==gui.generation&&!gui.live&&!gui.owner480&&!gui.request14&&!gui.retirement_pending&&gui.stateB2C==3
             &&gui.loader468==1&&gui.loader498==0&&gui.retirements>0&&gui.retirement_requests>0
             &&gui.close_requests>0&&gui.close_completions>0&&gui.completed_polls>0,"Menu inventory needs real close/poll/deferred retirement");
        std::vector<GameInventoryItem> items;
        GameInventoryRowsImage after;
        state_.menu_inventory_after=GameObserveInventoryRows(*vm_,state_.generation,bindings_.at(35),items,&after);
        Need(menu_mark_.Matches(after),"Menu inventory close changed receipt-approved registered typed RAM");
        Need(menu_placement_.MatchesAuxiliary(*vm_),"Menu inventory close changed protected placement dependencies");
        Need(runtime_&&menu_placement_.RetirementsComplete(runtime_->Snapshot()),"Menu inventory model retirement has not naturally destroyed this publication");
        vm_->resume_modal_context();menu_mark_.End();state_.menu_mark=menu_mark_.Snapshot();
        menu_placement_.End();
        state_.menu_inventory_live=false;++state_.menu_inventory_completions;state_.inventory_items=std::move(items);return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::ReadMenuNotebook(const GameOwnerEventTarget& expected,GameNotebookRows& out)const{
    try{
        Need(state_.live&&vm_&&!state_.blocked&&!vm_->callback_depth()&&!vm_->queued_callbacks(),"NOTE read needs idle owning VM");
        const auto target=OwnerEventTarget();
        Need(expected.generation==target.generation&&expected.context.kind==target.context.kind&&expected.context.word==target.context.word
             &&!expected.context.context_owner.expired()&&expected.context.context_owner.lock()==target.context.context_owner.lock()
             &&expected.callback.kind==target.callback.kind&&expected.callback.word==target.callback.word&&target.callback.word==0x7008DA28u,
             "NOTE actual registered context/Local0 replaced");
        out=GameObserveNotebookRows(*vm_,state_.generation,bindings_.at(11));return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::BindMenuNotebook(const GameOwnerEventTarget& target,uint64_t gui,uint64_t parent,const GameNotebookRows& rows){
    if(!gui||gui<=state_.menu_notebook_generation||!parent||state_.menu_notebook_live||state_.menu_inventory_live
       ||state_.inventory_requested||!vm_||!vm_->context_enabled())return {GameFsCode::Unavailable,0,"NOTE origin/generation unavailable"};
    GameNotebookRows current;auto status=ReadMenuNotebook(target,current);if(!status)return status;
    if(!rows.valid||current.sha!=rows.sha||current.run_id!=rows.run_id||current.code_sha!=rows.code_sha)
        return {GameFsCode::Unavailable,0,"NOTE registration changed before binding"};
    GameNotebookSelfImage self;
    try{self=GameObserveNotebookSelf(*vm_,state_.generation,bindings_.at(11));}catch(...){/* Optional index0 must not narrow legacy empty NOTE admission. */}
    notebook_self_before_=std::move(self);notebook_self_registration_=state_.notebook_registrations;
    state_.notebook_self_before=notebook_self_before_.owner;state_.notebook_self_after={};
    state_.notebook_rows=current;state_.menu_notebook_generation=gui;state_.menu_notebook_parent=parent;
    state_.menu_notebook_live=true;++state_.menu_notebook_requests;return {};
}
GameFsStatus GameActorScript::ReadMenuNotebookSelf(const GameOwnerEventTarget& target,uint64_t gui,uint64_t parent,GameNotebookSelfImage& out,bool for_preview)const{
    try{
        Need(state_.menu_notebook_live&&gui==state_.menu_notebook_generation&&parent==state_.menu_notebook_parent,
             "NOTE self-details GUI/parent owner replaced");
        const auto& pause=state_.menu_pause_return;
        Need(vm_&&pause.returned&&!pause.failed&&pause.event==18&&pause.menu_generation==parent
             &&!vm_->context_enabled()&&!vm_->callback_depth()&&!vm_->queued_callbacks(),"NOTE self-details needs actual disabled18/32 owner");
        GameNotebookRows rows;const auto status=ReadMenuNotebook(target,rows);Need(bool(status),status.detail.c_str());
        Need(rows.sha==state_.notebook_rows.sha&&rows.run_id==state_.notebook_rows.run_id&&rows.code_sha==state_.notebook_rows.code_sha,
             "NOTE self-details GAME11 registration/legacy image changed");
        if(!notebook_self_before_.owner.valid&&!for_preview){out={};return {};}
        Need(state_.notebook_registrations==notebook_self_registration_,"NOTE self-details GAME11 registration replaced");
        auto image=GameObserveNotebookSelf(*vm_,state_.generation,bindings_.at(11));
        if(for_preview)Need(profile_.live&&image.fields.size()>1&&image.fields[1].kind==Value::Kind::String
             &&image.fields[1].cp932==profile_.player,"NOTE self-details does not match current GAME20(0) player owner");
        const auto& before=notebook_self_before_;
        Need(before.owner.valid&&image.owner.sha==before.owner.sha&&image.code_sha==before.code_sha
             &&image.owner.run_id==before.owner.run_id&&image.owner.script_generation==before.owner.script_generation
             &&image.owner.row_table==before.owner.row_table&&image.owner.row_index==before.owner.row_index
             &&image.owner.row_header==before.owner.row_header,"NOTE self-details immutable index0 changed");
        out=std::move(image);return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::CompleteMenuNotebook(const GameOwnerEventTarget& target,const GameNotebookSnapshot& gui){
    try{
        Need(state_.live&&vm_&&state_.menu_notebook_live&&gui.generation==state_.menu_notebook_generation
             &&gui.lease.parent_generation==state_.menu_notebook_parent,"NOTE completion origin replaced");
        const auto& pause=state_.menu_pause_return;
        Need(pause.returned&&!pause.failed&&pause.event==18&&pause.menu_generation==state_.menu_notebook_parent
             &&!vm_->context_enabled()&&!vm_->callback_depth()&&!vm_->queued_callbacks(),"NOTE needs actual disabled18/32 owner");
        Need(gui.phase==7&&gui.retired_generation==gui.generation&&!gui.live&&!gui.owner46C&&!gui.request9&&!gui.retirement_pending
             &&gui.state23C==3&&gui.loader498==0&&gui.loader469==0&&gui.close_requests&&gui.close_completions&&gui.polls
             &&gui.retirement_requests,"NOTE needs actual current open/close/poll/deferred drain");
        GameNotebookRows current;const auto status=ReadMenuNotebook(target,current);Need(bool(status),status.detail.c_str());
        const auto& before=state_.notebook_rows;
        Need(current.sha==before.sha&&current.run_id==before.run_id&&current.code_sha==before.code_sha
             &&current.script_generation==before.script_generation&&current.root_table==before.root_table
             &&current.root_index==before.root_index&&current.root_header==before.root_header,"NOTE registered typed data changed during modal");
        if(gui.preview_opens){
            Need(gui.state26C==1&&gui.ordinary270==3&&gui.preview_opens==gui.preview_returns,"NOTE nested preview not returned to live parent");
        }
        if(notebook_self_before_.owner.valid){
            GameNotebookSelfImage self;const auto checked=ReadMenuNotebookSelf(target,gui.generation,gui.lease.parent_generation,self,gui.preview_opens!=0);
            Need(bool(checked),checked.detail.c_str());
            if(gui.preview_opens)Need(gui.preview_record.sha==self.owner.sha,"NOTE preview record certificate replaced");
            state_.notebook_self_after=self.owner;
        }
        // NativeActualClosedNotebookWake: genuine child retirement is the only
        // wake. The original post32 enable producer remains an explicit cut.
        state_.notebook_after=current;
        vm_->resume_modal_context();notebook_self_before_={};state_.menu_notebook_live=false;++state_.menu_notebook_completions;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::BindMenuOption(const GameOwnerEventTarget& expected,uint64_t gui,uint64_t parent,GameOptionSettings& settings,GameNotebookLease lease,const GameSystemOptionFrontier& gui_owner){
    try{
        Need(state_.live&&!state_.blocked&&vm_&&vm_->context_enabled()&&!vm_->callback_depth()&&!vm_->queued_callbacks()
             &&gui&&gui>state_.menu_option_generation&&parent&&!state_.menu_option_live&&!state_.menu_diary_live&&!state_.menu_wordmemo_live&&!state_.menu_notebook_live&&!state_.menu_inventory_live,
             "Options request current owner/generation unavailable");
        const auto target=OwnerEventTarget();
        Need(expected.generation==target.generation&&expected.context.kind==target.context.kind&&expected.context.word==target.context.word
             &&!expected.context.context_owner.expired()&&expected.context.context_owner.lock()==target.context.context_owner.lock()
             &&expected.callback.kind==target.callback.kind&&expected.callback.word==target.callback.word&&target.callback.word==0x7008DA28u,
             "Options current registered context/Local0 changed");
        Need(state_.menu_open_return.returned&&!state_.menu_open_return.failed&&state_.menu_open_return.event==10&&state_.menu_open_return.menu_generation==parent,
             "Options genuine opening10 return missing");
        const auto current=settings.OptionSettings();Need(current.live&&current.owner&&current.generation&&current.words[0]==2,"Options current source settings owner unavailable");
        const auto sound=settings.OptionSoundOwner();
        Need(!settings.OptionRightWritable()||GameSourceSoundNegativeReady(sound),"Options actual initialized source sound owner unavailable");
        Need(lease.script_generation==state_.generation&&lease.run_id==vm_->current_witness().run_id&&lease.parent_generation==parent
             &&lease.context&&lease.available_owner&&lease.available_generation&&lease.input_epoch,"Options sampled child lease unavailable");
        Need(gui_owner.Snapshot().generation==gui&&gui_owner.Snapshot().phase==1,"Options actual requested frontier owner unavailable");
        option_gui_owner_=&gui_owner;option_lease_=lease;
        option_current_epoch_=lease.input_epoch;option_boundary_issued_=option_prompt_stage_=0;
        option_yes_issued_={};option_yes_nonce_={};option_yes_publication_={};option_yes_words_={};option_yes_root_={};
        option_yes_armed_=option_yes_published_=option_pending_yes_=false;option_yes_sequence_=0;option_terminal_kind_=0;
        state_.option_yes_writes=state_.option_yes_publication_receipt=state_.option_completion_kind=0;
        state_.option_row_writes=state_.option_row_selection_receipt=0;state_.option_row_prefix_verified=false;
        option_row_issued_={};option_row_pending_receipt_={};option_row_pending_=option_down_armed_=option_left_armed_=false;option_down_sequence_=option_left_sequence_=0;
        option_settings_=&settings;state_.option_before=current;state_.option_after={};state_.menu_option_generation=gui;state_.menu_option_parent=parent;
        option_expected_=current;option_issued_={};option_restore_issued_={};option_pending_={};option_write_pending_=option_right_armed_=false;
        option_feedback_pending_=option_feedback_committed_=false;option_feedback_issued_={};state_.option_feedback_prefix_verified=false;
        option_voice_service_pending_=option_voice_service_committed_=false;option_voice_service_issued_={};state_.option_voice_prefix_verified=state_.option_sequence_prefix_verified=false;
        option_pending_restore_=option_gain_pending_=option_tick_pending_=false;option_gain_issued_={};option_sound_expected_=sound;
        state_.option_sound_before=sound;state_.option_sound_after={};
        option_right_sequence_=option_cancel_sequence_=option_input_sequence_=0;state_.option_right_writes=state_.option_right_after_key_before=0;
        state_.option_restore_writes=state_.option_gain_returns=state_.option_tick_receipts=state_.option_after_key_cursor=0;state_.option_prefix_verified=false;
        state_.option_lifecycle_phase=2;state_.option_poll_receipt=state_.option_drain_receipt=0;
        state_.menu_option_live=true;++state_.menu_option_requests;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::CheckMenuOption(const GameOwnerEventTarget& expected,const GameSystemOptionSnapshot& gui)const{
    return CheckMenuOptionOwner(expected,gui,false);
}
bool GameActorScript::OptionRowCursor(const GameSystemOptionSnapshot& gui)const{
    if(gui.phase!=3||gui.state44!=1||!gui.live||!gui.visible||!gui.owner474||!gui.request10||!gui.loader469
       ||!gui.template_owner||!gui.clone_owner||!gui.message.live||gui.message.references!=1
       ||gui.row_selections!=1||gui.right_steps||gui.right_writes||gui.row_writes!=state_.option_row_writes||gui.row_writes>7
       ||gui.selection56!=1||gui.substate48||gui.row_before!=state_.option_before.words||gui.passive_settings!=state_.option_before.words)return false;
    if(!state_.option_row_selection_receipt&&(!option_tick_pending_||!option_down_sequence_||option_down_sequence_>option_input_sequence_))return false;
    if(!gui.row_left_steps)return gui.current_words==state_.option_before.words&&gui.previous50==1;
    auto words=state_.option_before.words;if(!words[1]||words[1]>INT32_MAX)return false;--words[1];
    if(gui.row_left_steps!=1||gui.row_writes<3||gui.current_words!=words)return false;
    if(gui.row_writes<=4&&gui.previous50==UINT32_MAX)return option_tick_pending_&&option_left_sequence_&&option_left_sequence_<=option_input_sequence_&&gui.after_key_updates==option_tick_before_;
    return option_feedback_committed_&&GameSourceVoiceFeedbackValid(option_feedback_issued_)&&gui.row_writes>=4&&gui.previous50==1
        &&gui.source_ui_sound_intents==3&&gui.after_key_updates==(option_tick_pending_&&gui.row_writes<7?option_tick_before_+1:state_.option_after_key_cursor)
        ||(option_feedback_committed_&&GameSourceVoiceFeedbackValid(option_feedback_issued_)&&option_tick_pending_&&gui.previous50==1&&gui.row_writes>=4
            &&gui.source_ui_sound_intents==3&&gui.after_key_updates==option_tick_before_+1);
}
GameFsStatus GameActorScript::CheckMenuOptionOwner(const GameOwnerEventTarget& expected,const GameSystemOptionSnapshot& gui,bool failed_prefix)const{
    try{
        Need(state_.live&&!state_.blocked&&vm_&&state_.menu_option_live&&option_settings_&&(!gui.failed||failed_prefix)
             &&gui.generation==state_.menu_option_generation&&gui.child_lease.parent_generation==state_.menu_option_parent
             &&gui.child_lease.script_generation==state_.generation&&gui.child_lease.run_id==vm_->current_witness().run_id,"Options GUI/parent/run owner changed");
        Need(option_gui_owner_!=nullptr,"Options actual frontier owner retired");
        const auto actual=option_gui_owner_->Snapshot();
        Need(gui.input_epoch==option_current_epoch_,"Options unissued current input epoch");
        Need(gui.prompt_bound_owner==actual.prompt_bound_owner&&gui.prompt_bound_generation==actual.prompt_bound_generation
             &&(!gui.prompt_bound_owner||(gui.prompt.live&&!gui.prompt.failed&&gui.prompt.resource_live&&gui.prompt.font_live
                &&gui.prompt.owner==gui.prompt_bound_owner&&gui.prompt.generation==gui.prompt_bound_generation)),"Options frozen global TRC borrow replaced/retired");
        Need(std::tie(gui.input_boundaries,gui.boundary_cause,gui.prompt_opens,gui.prompt_returns,
              gui.prompt.owner,gui.prompt.generation,gui.prompt.parent,gui.prompt.invocation,gui.prompt.input_epoch,gui.prompt.last_sequence,
              gui.prompt.live,gui.prompt.active,gui.prompt.failed,gui.prompt.resource_live,gui.prompt.font_live,gui.prompt.state864,gui.prompt.result86C,gui.prompt.selection,gui.prompt.opens,gui.prompt.returns,gui.prompt.no_choices,gui.prompt.yes_refusals,gui.prompt.yes_choices,gui.prompt.result_sequence,gui.prompt.result_epoch,gui.prompt.completed_invocation,gui.prompt.publication,gui.prompt.result_update)
          ==std::tie(actual.input_boundaries,actual.boundary_cause,actual.prompt_opens,actual.prompt_returns,
              actual.prompt.owner,actual.prompt.generation,actual.prompt.parent,actual.prompt.invocation,actual.prompt.input_epoch,actual.prompt.last_sequence,
              actual.prompt.live,actual.prompt.active,actual.prompt.failed,actual.prompt.resource_live,actual.prompt.font_live,actual.prompt.state864,actual.prompt.result86C,actual.prompt.selection,actual.prompt.opens,actual.prompt.returns,actual.prompt.no_choices,actual.prompt.yes_refusals,actual.prompt.yes_choices,actual.prompt.result_sequence,actual.prompt.result_epoch,actual.prompt.completed_invocation,actual.prompt.publication,actual.prompt.result_update),
              "Options copied prompt differs from actual bound global borrow");
        Need(std::tie(gui.generation,gui.phase,gui.state44,gui.live,gui.visible,gui.owner474,gui.request10,gui.loader469,
              gui.poll_completed,gui.retired,gui.retired_generation,gui.retirement_pending,gui.template_owner,gui.clone_owner,
              gui.opens,gui.close_requests,gui.close_completions,gui.polls,gui.retirement_requests,gui.retirements,
              gui.message.live,gui.message.template_owner,gui.message.clone_owner,gui.message.text_owner,gui.message.registered,gui.message.references)
          ==std::tie(actual.generation,actual.phase,actual.state44,actual.live,actual.visible,actual.owner474,actual.request10,actual.loader469,
              actual.poll_completed,actual.retired,actual.retired_generation,actual.retirement_pending,actual.template_owner,actual.clone_owner,
              actual.opens,actual.close_requests,actual.close_completions,actual.polls,actual.retirement_requests,actual.retirements,
              actual.message.live,actual.message.template_owner,actual.message.clone_owner,actual.message.text_owner,actual.message.registered,actual.message.references),
              "Options copied lifecycle differs from actual bound frontier owner");
        Need(std::tie(gui.failed,gui.source_consumer,gui.after_key_updates,gui.previous50,gui.selection56,gui.substate48,gui.current_words,gui.passive_settings,
              gui.yes_writes,gui.yes_consumed,gui.close_kind,gui.right_steps,gui.right_writes,gui.restore_writes,gui.gain_queries,gui.gain_value,gui.gain_returned,gui.right_message_continued,
              gui.source_ui_sound_intents,gui.input_epoch,gui.last_sequence,gui.message.state64,gui.message.complete68,gui.message.updates,gui.message.rate_setting)
          ==std::tie(actual.failed,actual.source_consumer,actual.after_key_updates,actual.previous50,actual.selection56,actual.substate48,actual.current_words,actual.passive_settings,
              actual.yes_writes,actual.yes_consumed,actual.close_kind,actual.right_steps,actual.right_writes,actual.restore_writes,actual.gain_queries,actual.gain_value,actual.gain_returned,actual.right_message_continued,
              actual.source_ui_sound_intents,actual.input_epoch,actual.last_sequence,actual.message.state64,actual.message.complete68,actual.message.updates,actual.message.rate_setting),
              "Options copied source cursor differs from actual bound frontier owner");
        Need(std::tie(gui.row_selections,gui.row_left_steps,gui.row_writes,gui.row_feedback_attempts,gui.row_before,gui.marker_frame,
              gui.row_feedback.attempted,gui.row_feedback.bank_attached,gui.row_feedback.bank_handle_known,gui.row_feedback.read_returned)
          ==std::tie(actual.row_selections,actual.row_left_steps,actual.row_writes,actual.row_feedback_attempts,actual.row_before,actual.marker_frame,
              actual.row_feedback.attempted,actual.row_feedback.bank_attached,actual.row_feedback.bank_handle_known,actual.row_feedback.read_returned),"Options copied selected cursor changed");
        const auto& lease=gui.child_lease;Need(lease.context==option_lease_.context&&lease.available_owner==option_lease_.available_owner
             &&lease.available_generation==option_lease_.available_generation&&lease.input_epoch==option_lease_.input_epoch
             &&gui.lease.script_generation==lease.script_generation&&gui.lease.run_id==lease.run_id&&gui.lease.available_owner==lease.available_owner
             &&gui.lease.available_generation==lease.available_generation&&gui.lease.message_context==lease.context&&gui.lease.input_epoch+1==lease.input_epoch,
             "Options child sampled context/available/opening epoch replaced");
        const auto target=OwnerEventTarget();
        Need(expected.generation==target.generation&&expected.context.kind==target.context.kind&&expected.context.word==target.context.word
             &&!expected.context.context_owner.expired()&&expected.context.context_owner.lock()==target.context.context_owner.lock()
             &&expected.callback.kind==target.callback.kind&&expected.callback.word==target.callback.word&&target.callback.word==0x7008DA28u,"Options registered context/Local0 changed");
        const auto& pause=state_.menu_pause_return;
        Need(pause.returned&&!pause.failed&&pause.event==18&&pause.menu_generation==state_.menu_option_parent&&!vm_->context_enabled()
             &&!vm_->callback_depth()&&!vm_->queued_callbacks()&&GameWordMemoPausedOwnerExact(pause.restored,vm_->current_witness()),"Options exact paused18 caller/frame/operands/deadline changed");
        const auto current=option_settings_->OptionSettings();const auto& before=state_.option_before;
        Need(gui.passive_settings==before.words,"Options immutable backup changed");
        Need(gui.phase==state_.option_lifecycle_phase||option_tick_pending_,"Options unissued lifecycle phase changed");
        const bool early_right=option_tick_pending_&&gui.right_steps==1&&state_.option_right_writes<=1&&!state_.option_restore_writes
            &&!state_.option_gain_returns&&gui.previous50==UINT32_MAX&&gui.after_key_updates==option_tick_before_;
        const bool nested=gui.substate48==3&&gui.prompt.live&&gui.prompt.active&&gui.prompt.resource_live&&gui.prompt.font_live
            &&!gui.prompt.failed&&gui.prompt.parent==gui.generation&&gui.prompt_opens==gui.prompt_returns+1;
        const bool row=OptionRowCursor(gui);
        Need(row||(!gui.row_selections&&!gui.row_writes&&!gui.row_left_steps&&!gui.selection56&&(!gui.substate48||nested||OptionYesReturnReady(gui))&&(!gui.previous50||early_right)),"Options finite selected/common cursor changed");
        for(uint32_t i=0;i<state_.option_row_writes;++i)Need(GameOptionRowReceiptExact(gui.row_receipts[i],option_row_issued_[i]),"Options selected source receipt replaced");
        Need(gui.draw_frames==actual.draw_frames&&gui.row_left_draw_frames_before==actual.row_left_draw_frames_before&&gui.bar_frames==actual.bar_frames,
             "Options copied offered-frame proof differs from actual bound frontier");
        Need(gui.voice_service_attempts==actual.voice_service_attempts&&GameSourceFeedbackExact(gui.row_voice_service,actual.row_voice_service),"Options copied actual voice service changed");
        if(option_feedback_committed_)Need(GameSourceFeedbackExact(gui.row_feedback,option_feedback_issued_),"Options returned feedback receipt replaced");
        if(option_voice_service_committed_)Need(GameSourceFeedbackExact(gui.row_voice_service,option_voice_service_issued_),"Options issued callback receipt replaced");
        if(option_voice_service_pending_&&(GameSourceVoiceServiceValid(option_voice_service_issued_)||GameSourceSequencePrefixValid(option_voice_service_issued_))){
            Need(GameSourceFeedbackExact(gui.row_voice_service,option_voice_service_issued_)
               &&GameSourceSoundSnapshotExact(option_voice_service_issued_.owner,option_sound_expected_)
               &&GameSourceSoundSnapshotExact(gui.sound_owner,option_voice_service_issued_.after)
               &&GameSourceSoundSnapshotExact(option_settings_->OptionSoundOwner(),option_voice_service_issued_.after),"Options actual issued voice service delta changed");
        }else if(option_feedback_pending_&&(GameSourceFeedbackReachedValid(option_feedback_issued_)||GameSourceVoiceFeedbackValid(option_feedback_issued_))){
            Need(GameSourceFeedbackExact(gui.row_feedback,option_feedback_issued_)
                &&GameSourceSoundSnapshotExact(option_feedback_issued_.owner,option_sound_expected_)
                &&GameSourceSoundSnapshotExact(gui.sound_owner,option_feedback_issued_.after)
                &&GameSourceSoundSnapshotExact(option_settings_->OptionSoundOwner(),option_feedback_issued_.after),"Options pending feedback actual issued delta changed");
        }else if(option_sound_expected_.live){const auto sound=option_settings_->OptionSoundOwner();
            Need(GameSourceSoundOwnerStable(sound,option_sound_expected_),"Options bound source sound owner/lastwriter changed");
            Need(GameSourceSoundSnapshotExact(gui.sound_owner,option_sound_expected_),"Options GUI source owner receipt changed");
            if(option_gain_pending_)Need(sound.queries==option_sound_expected_.queries||(option_sound_expected_.queries!=UINT64_MAX&&sound.queries==option_sound_expected_.queries+1),"Options pending source query counter changed");
            else Need(GameSourceSoundSnapshotExact(sound,option_sound_expected_),"Options source query readback changed");
        }
        if(option_tick_pending_)Need(gui.after_key_updates==option_tick_before_||(option_tick_before_!=UINT32_MAX&&gui.after_key_updates==option_tick_before_+1),"Options source tick cursor changed");
        else Need(gui.after_key_updates==state_.option_after_key_cursor,"Options unissued source tick changed");
        Need(GameOptionSnapshotExact(current,option_expected_)&&GameOptionSnapshotExact(gui.settings,option_expected_),"Options privately expected scalar image changed");
        Need(gui.restore_writes==state_.option_restore_writes,"Options backup setter count changed");
        Need(gui.yes_writes==state_.option_yes_writes&&gui.yes_consumed<=1,"Options current Yes setter count changed");
        for(uint32_t i=0;i<state_.option_yes_writes;++i)Need(GameOptionReceiptExact(gui.yes_receipts[i],option_yes_issued_[i]),"Options current Yes receipt replaced");
        if(!option_tick_pending_)Need(gui.close_kind==option_terminal_kind_,"Options unissued terminal kind changed");
        for(uint32_t i=0;i<state_.option_restore_writes;++i)Need(GameOptionReceiptExact(gui.restore_receipts[i],option_restore_issued_[i]),"Options backup receipt replaced");
        if(state_.option_right_writes){
            Need(gui.right_steps==1&&gui.right_writes==state_.option_right_writes
                 &&gui.current_words==std::array<uint32_t,3>{{3,3,3}},"Options issued Right prefix/current words changed");
            for(uint32_t i=0;i<state_.option_right_writes;++i)Need(GameOptionReceiptExact(gui.right_receipts[i],option_issued_[i]),"Options issued receipt replaced/reordered");
        }else if(!row)Need((!gui.right_steps&&!gui.right_writes&&gui.current_words==before.words)
             ||(early_right&&option_right_sequence_&&!gui.right_writes&&gui.source_consumer==0x351CC&&gui.current_words==std::array<uint32_t,3>{{3,3,3}}),"Options unissued Right/local words changed");
        const bool cancel_close=gui.close_kind==1&&state_.option_restore_writes==3&&!state_.option_yes_writes&&!gui.substate48;
        const bool yes_close=gui.close_kind==2&&state_.option_yes_writes==3&&!state_.option_restore_writes&&gui.yes_consumed==1&&OptionYesReturnReady(gui);
        Need(!gui.close_requests||(cancel_close||yes_close),"Options Close lacks distinct typed writer chain");
        if(state_.option_gain_returns)Need((gui.gain_returned||(gui.row_writes==7&&option_feedback_committed_&&gui.source_consumer==0x4AFC8))&&gui.gain_queries==state_.option_gain_returns+((gui.row_writes==7&&!gui.gain_returned&&gui.source_consumer==0x4AFC8)?1u:0u)&&((gui.row_writes==7&&!gui.gain_returned&&gui.source_consumer==0x4AFC8)||GameSourceSoundGainExact(gui.gain.source,option_gain_issued_)),"Options genuine gain return evidence changed");
        else Need(!gui.gain_returned,"Options unissued gain return");return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
bool GameActorScript::OptionYesReturnReady(const GameSystemOptionSnapshot& gui)const{
    const auto& p=gui.prompt;const auto& issued=option_yes_publication_;
    return option_yes_published_&&state_.option_yes_publication_receipt&&gui.substate48==3
        &&p.live&&!p.failed&&p.resource_live&&p.font_live&&!p.active&&p.state864==3&&p.result86C==1&&!p.selection
        &&p.owner==issued.owner&&p.generation==issued.generation&&p.parent==gui.generation&&p.invocation==issued.invocation
        &&p.result_sequence==issued.result_sequence&&p.result_epoch==issued.result_epoch&&p.publication==issued.publication&&p.result_update==issued.result_update
        &&p.completed_invocation==p.invocation&&issued.returns!=UINT32_MAX&&p.returns==issued.returns+1
        &&p.updates>issued.result_update&&gui.current_words==option_yes_words_&&gui.passive_settings==state_.option_before.words;
}
GameFsStatus GameActorScript::ObserveMenuOptionInput(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui,const GameInputFrame& f){
    const auto checked=CheckMenuOption(target,gui);if(!checked)return checked;
    if(!f.focused||f.overflow_resync){DisarmMenuOptionInput();return {};}
    if(gui.phase!=3||gui.state44!=1||f.sequence<=option_input_sequence_||f.sequence!=gui.last_sequence
       ||f.main_menu_epoch!=gui.input_epoch||f.main_menu_epoch!=option_current_epoch_
       ||f.message_owner_generation!=option_lease_.script_generation||f.message_context!=option_lease_.context
       ||f.returned_owner!=option_lease_.available_owner||f.returned_generation!=option_lease_.available_generation)
        return {GameFsCode::Unavailable,0,"Options writer sampled input identity unavailable"};
    option_input_sequence_=f.sequence;
    if(gui.substate48){
        option_right_armed_=false;option_right_sequence_=option_cancel_sequence_=0;
        const auto& p=gui.prompt;
        if(option_prompt_stage_!=2||p.state864!=1||!p.active||p.result86C||p.input_epoch!=option_current_epoch_)
            return {GameFsCode::Unavailable,0,"Options nested sampled input owner unavailable"};
        // Mirror Circle's per-button release gate, not unrelated raw buttons.
        // This is a candidate edge only; the actual source producer decides
        // navigation priority/selection before private publication validation.
        if(!(f.buttons&Circle))option_yes_armed_=true;
        else{if(option_yes_armed_){option_yes_sequence_=f.sequence;option_yes_nonce_=p;}
             option_yes_armed_=false;}
        return {};
    }
    if(!(f.buttons&Down))option_down_armed_=true;
    else{if(option_down_armed_&&!gui.row_selections)option_down_sequence_=f.sequence;option_down_armed_=false;}
    if(!(f.buttons&Left))option_left_armed_=true;
    else{if(option_left_armed_&&gui.row_selections==1)option_left_sequence_=f.sequence;option_left_armed_=false;}
    // Observe only the existing route's accepted samples. A valid pending
    // Right edge survives later held/release samples until Tick consumes it,
    // matching GameMenuInput's pending mask. Focus/overflow explicitly revoke.
    if(!f.buttons)option_right_armed_=true;
    else{if(option_right_armed_&&(f.buttons&Right)&&!(f.buttons&(Down|Up)))option_right_sequence_=f.sequence;
         if(option_right_armed_&&(f.buttons&Cross)&&!(f.buttons&(Down|Up|Right|Left|Circle)))option_cancel_sequence_=f.sequence;
         option_right_armed_=false;}
    return {};
}
GameFsStatus GameActorScript::PrepareMenuOptionRowWrite(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui,GameOptionRowCause cause,uint32_t stage,uint32_t value,GameOptionRowWriteReceipt& out){
    const auto checked=CheckMenuOption(target,gui);if(!checked)return checked;
    try{
        const bool down=cause==GameOptionRowCause::Down,common=cause==GameOptionRowCause::LeftCommon;
        Need(option_tick_pending_&&!option_write_pending_&&!option_row_pending_&&!option_gain_pending_&&gui.phase==3&&gui.state44==1&&!gui.close_requests
          &&gui.last_sequence==option_input_sequence_&&gui.input_epoch==option_current_epoch_&&OptionRowCursor(gui),"Options selected writer owner unavailable");
        Need(common?(stage<3&&state_.option_row_writes==4+stage&&option_feedback_committed_&&GameSourceVoiceFeedbackValid(option_feedback_issued_)
              &&gui.previous50==1&&gui.source_ui_sound_intents==3&&gui.after_key_updates==option_tick_before_+1&&state_.option_gain_returns==1)
          :down?(stage<3&&stage==state_.option_row_writes&&!gui.row_left_steps&&option_down_sequence_&&option_down_sequence_<=option_input_sequence_
              &&gui.source_ui_sound_intents==2&&gui.previous50==1&&gui.after_key_updates==option_tick_before_+1&&!state_.option_gain_returns)
          :(!stage&&state_.option_row_writes==3&&gui.row_left_steps==1&&option_left_sequence_&&option_left_sequence_<=option_input_sequence_
              &&gui.source_ui_sound_intents==2&&gui.previous50==UINT32_MAX&&gui.after_key_updates==option_tick_before_&&state_.option_gain_returns==1),"Options exact Down/Left source stage unavailable");
        const uint32_t field=down||common?stage:1,pc=down||common?0x34CCC+stage*8:0x351CC;
        Need(gui.source_consumer==pc&&value==gui.current_words[field],"Options selected source JAL/value changed");
        out={};out.cause=cause;out.selection_before=down?0:1;out.local_before=common?option_expected_.words:state_.option_before.words;out.local_after=gui.current_words;
        out.write={gui.generation,option_input_sequence_,gui.input_epoch,stage,field,pc,0x24B1C+field*0x18,value,option_expected_,option_expected_};
        Need(out.write.after.writes!=UINT64_MAX,"Options selected write ordinal exhausted");out.write.after.words[field]=value;++out.write.after.writes;
        Need(GameOptionRowReceiptValid(out),"Options selected source receipt shape refused");
        option_row_pending_receipt_=out;option_row_pending_=true;
        if(down&&!stage)state_.option_row_selection_receipt=state_.option_tick_receipts+1;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::CommitMenuOptionRowWrite(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui,const GameOptionRowWriteReceipt& row){
    try{
        const auto& receipt=row.write;
        Need(state_.live&&!state_.blocked&&state_.menu_option_live&&vm_&&option_settings_&&option_tick_pending_&&option_row_pending_
          &&!option_write_pending_&&!option_gain_pending_&&GameOptionRowReceiptExact(row,option_row_pending_receipt_)
          &&GameOptionRowReceiptValid(row)&&state_.option_row_writes<7&&GameOptionSnapshotExact(option_settings_->OptionSettings(),receipt.after),"Options selected full32 readback refused");
        const auto current=OwnerEventTarget();Need(target.generation==current.generation&&target.context.kind==current.context.kind&&target.context.word==current.context.word
             &&!target.context.context_owner.expired()&&target.context.context_owner.lock()==current.context.context_owner.lock()
             &&target.callback.kind==current.callback.kind&&target.callback.word==current.callback.word&&current.callback.word==0x7008DA28u
             &&gui.generation==state_.menu_option_generation&&gui.child_lease.parent_generation==state_.menu_option_parent
             &&gui.child_lease.script_generation==state_.generation&&gui.child_lease.run_id==vm_->current_witness().run_id
             &&gui.child_lease.context==option_lease_.context&&gui.child_lease.available_owner==option_lease_.available_owner
             &&gui.child_lease.available_generation==option_lease_.available_generation&&gui.child_lease.input_epoch==option_lease_.input_epoch
             &&!vm_->context_enabled()&&!vm_->callback_depth()&&!vm_->queued_callbacks()&&state_.menu_pause_return.returned&&!state_.menu_pause_return.failed
             &&GameWordMemoPausedOwnerExact(state_.menu_pause_return.restored,vm_->current_witness()),"Options paused writer owner changed");
        Need(!option_sound_expected_.live||GameSourceSoundSnapshotExact(option_settings_->OptionSoundOwner(),option_sound_expected_),"Options setter sound owner changed");

        const auto actual=option_gui_owner_->Snapshot();
        Need(OptionRowCursor(actual)&&actual.row_writes==state_.option_row_writes&&actual.source_consumer==receipt.call_pc&&gui.source_consumer==receipt.call_pc
          &&actual.last_sequence==option_input_sequence_&&actual.input_epoch==option_current_epoch_&&actual.current_words==row.local_after
          &&actual.previous50==gui.previous50&&actual.after_key_updates==gui.after_key_updates&&!actual.failed,"Options selected actual frontier/JAL changed");
        option_expected_=receipt.after;option_row_issued_[state_.option_row_writes++]=row;option_row_pending_=false;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::PrepareMenuOptionFeedback(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui){
    const auto checked=CheckMenuOption(target,gui);if(!checked)return checked;
    if(option_feedback_pending_||option_feedback_committed_||!option_tick_pending_||!OptionRowCursor(gui)
       ||gui.source_consumer!=0x4AC84||gui.row_writes!=4||state_.option_row_writes!=4||!state_.option_row_selection_receipt
       ||gui.row_left_steps!=1||gui.row_feedback_attempts!=1||gui.after_key_updates!=option_tick_before_||gui.source_ui_sound_intents!=2
       ||state_.option_gain_returns!=1||gui.gain_queries!=1||!option_sound_expected_.registration.live
       ||!GameSourceSoundNegativeReady(option_sound_expected_)||option_expected_.words!=std::array<uint32_t,3>{{2,2,3}})
        return {GameFsCode::Unavailable,0,"Options exact issued SE5 feedback prefix unavailable"};
    option_feedback_pending_=true;return {};
}
GameFsStatus GameActorScript::CommitMenuOptionFeedback(const GameOwnerEventTarget& target,const GameSourceFeedbackRead& receipt){
    if(!option_feedback_pending_||option_feedback_committed_||!option_gui_owner_||(!GameSourceFeedbackReachedValid(receipt)&&!GameSourceVoiceFeedbackValid(receipt))
       ||!GameSourceSoundSnapshotExact(receipt.owner,option_sound_expected_)
       ||!GameSourceSoundSnapshotExact(receipt.after,option_settings_->OptionSoundOwner()))
        return {GameFsCode::Unavailable,0,"Options SE5 actual outcome/owner unavailable"};
    option_feedback_issued_=receipt;
    const auto checked=CheckMenuOption(target,option_gui_owner_->Snapshot());if(!checked)return checked;
    option_sound_expected_=receipt.after;state_.option_sound_after=receipt.after;option_feedback_pending_=false;option_feedback_committed_=true;return {};
}
GameFsStatus GameActorScript::PrepareMenuOptionVoiceService(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui){
 const auto checked=CheckMenuOption(target,gui);if(!checked)return checked;
 if(!option_tick_pending_||option_voice_service_pending_||option_voice_service_committed_||!option_feedback_committed_||!GameSourceVoiceFeedbackValid(option_feedback_issued_)
    ||gui.row_writes!=7||state_.option_row_writes!=7||gui.gain_queries!=2||state_.option_gain_returns!=2||gui.previous50!=1||gui.selection56!=1||gui.substate48
    ||gui.after_key_updates!=option_tick_before_||gui.source_ui_sound_intents!=3||gui.voice_service_attempts!=1
    ||gui.draw_frames<=gui.row_left_draw_frames_before||gui.bar_frames!=std::array<uint32_t,3>{{3,8,4}}||gui.sound_owner.voice.services
    ||gui.close_requests||gui.polls||gui.retirements||gui.restore_writes||gui.yes_writes)return {GameFsCode::Unavailable,0,"Options next actual voice service owner unavailable"};
 option_voice_service_pending_=true;return {};
}
GameFsStatus GameActorScript::CommitMenuOptionVoiceService(const GameOwnerEventTarget& target,const GameSourceFeedbackRead& receipt){
 if(!option_voice_service_pending_||option_voice_service_committed_||(!GameSourceVoiceServiceValid(receipt)&&!GameSourceSequencePrefixValid(receipt))
    ||!GameSourceSoundSnapshotExact(receipt.owner,option_sound_expected_)||!GameSourceSoundSnapshotExact(receipt.after,option_settings_->OptionSoundOwner()))return {GameFsCode::Unavailable,0,"Options actual voice service receipt unavailable"};
 option_voice_service_issued_=receipt;const auto checked=CheckMenuOption(target,option_gui_owner_->Snapshot());if(!checked)return checked;
 option_sound_expected_=receipt.after;state_.option_sound_after=receipt.after;option_voice_service_pending_=false;option_voice_service_committed_=true;return {};
}
GameFsStatus GameActorScript::VerifyMenuOptionVoiceServicePrefix(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui){
 const auto checked=CheckMenuOptionOwner(target,gui,true);if(!checked)return checked;
 if(!option_voice_service_committed_||!gui.failed||gui.failure.code!=GameFsCode::Unavailable||gui.source_consumer!=0x57CF0||!option_tick_pending_
    ||!GameSourceFeedbackExact(gui.row_voice_service,option_voice_service_issued_)||!GameSourceVoiceServiceValid(gui.row_voice_service)
    ||gui.row_writes!=7||state_.option_row_writes!=7||gui.gain_queries!=2||state_.option_gain_returns!=2||gui.previous50!=1||gui.selection56!=1||gui.substate48
    ||gui.after_key_updates!=option_tick_before_||gui.voice_service_attempts!=1||gui.source_ui_sound_intents!=3||gui.close_requests||gui.polls||gui.retirements||gui.restore_writes||gui.yes_writes)
 return {GameFsCode::Unavailable,0,"Options selected callback failed-only prefix incomplete"};
 state_.option_voice_prefix_verified=true;state_.option_after=option_expected_;return {};
}
GameFsStatus GameActorScript::VerifyMenuOptionSequencePrefix(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui){
 const auto checked=CheckMenuOptionOwner(target,gui,true);if(!checked)return checked;
 if(!option_voice_service_committed_||!gui.failed||gui.failure.code!=GameFsCode::Unavailable||gui.source_consumer!=0x5A5AC||!option_tick_pending_
    ||!GameSourceFeedbackExact(gui.row_voice_service,option_voice_service_issued_)||!GameSourceSequencePrefixValid(gui.row_voice_service)
    ||gui.row_writes!=7||state_.option_row_writes!=7||gui.gain_queries!=2||state_.option_gain_returns!=2||gui.previous50!=1||gui.selection56!=1||gui.substate48
    ||gui.after_key_updates!=option_tick_before_||gui.voice_service_attempts!=1||gui.source_ui_sound_intents!=3||gui.close_requests||gui.polls||gui.retirements||gui.restore_writes||gui.yes_writes)
 return {GameFsCode::Unavailable,0,"Options selected sequence port failed-only prefix incomplete"};
 state_.option_sequence_prefix_verified=true;state_.option_after=option_expected_;return {};
}
GameFsStatus GameActorScript::VerifyMenuOptionFeedbackPrefix(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui){
    const auto checked=CheckMenuOptionOwner(target,gui,true);if(!checked)return checked;
    if(!option_feedback_committed_||!gui.failed||gui.failure.code!=GameFsCode::Unavailable||gui.source_consumer!=0x56598
       ||!GameSourceFeedbackExact(gui.row_feedback,option_feedback_issued_)||!GameSourceFeedbackReachedValid(gui.row_feedback)
       ||gui.row_writes!=4||state_.option_row_writes!=4||gui.selection56!=1||gui.previous50!=UINT32_MAX||gui.substate48
       ||gui.after_key_updates!=option_tick_before_||gui.source_ui_sound_intents!=2||gui.gain_queries!=1||state_.option_gain_returns!=1
       ||gui.close_requests||gui.polls||gui.retirements||gui.restore_writes||gui.yes_writes||!option_tick_pending_)
        return {GameFsCode::Unavailable,0,"Options reached SE5 voice issuer prefix incomplete"};
    state_.option_feedback_prefix_verified=true;state_.option_after=option_expected_;return {};
}
GameFsStatus GameActorScript::VerifyMenuOptionRowPrefix(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui){
    const auto checked=CheckMenuOptionOwner(target,gui,true);if(!checked)return checked;
    const auto& observation=gui.row_feedback;
    if(!gui.failed||gui.failure.code!=GameFsCode::Unavailable||gui.source_consumer!=0x4AC84||gui.row_writes!=4||state_.option_row_writes!=4
      ||!state_.option_row_selection_receipt||gui.row_left_steps!=1||gui.selection56!=1||gui.previous50!=UINT32_MAX||gui.substate48
      ||gui.after_key_updates!=option_tick_before_||gui.source_ui_sound_intents!=2||gui.row_feedback_attempts!=1
      ||!observation.attempted||observation.context_index!=5||observation.read_pc!=0x4AC84||!observation.bank_attached||observation.bank_handle_known||observation.read_returned
      ||!GameSourceSoundSnapshotExact(observation.owner,option_sound_expected_)||gui.gain_queries!=1||state_.option_gain_returns!=1
      ||gui.close_requests||gui.polls||gui.retirements||gui.restore_writes||gui.yes_writes||!option_tick_pending_)
        return {GameFsCode::Unavailable,0,"Options selected failed feedback prefix incomplete"};
    state_.option_row_prefix_verified=true;state_.option_after=option_expected_;return {};
}
GameFsStatus GameActorScript::PrepareMenuOptionWrite(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui,uint32_t stage,uint32_t value,GameOptionWriteReceipt& out){
    const auto checked=CheckMenuOption(target,gui);if(!checked)return checked;
    try{
        static constexpr uint32_t fields[]={1,0,1,2},calls[]={0x351CC,0x34CCC,0x34CD4,0x34CDC},setters[]={0x24B34,0x24B1C,0x24B34,0x24B4C};
        Need(option_tick_pending_&&!option_write_pending_&&stage<4&&stage==state_.option_right_writes&&value==3&&option_right_sequence_
             &&gui.last_sequence==option_input_sequence_&&option_right_sequence_<=option_input_sequence_
             &&gui.input_epoch==option_current_epoch_&&gui.right_steps==1
             &&gui.phase==3&&gui.state44==1&&!gui.close_requests&&gui.selection56==0&&gui.substate48==0
             &&gui.previous50==(stage?0u:UINT32_MAX)&&gui.current_words==std::array<uint32_t,3>{{3,3,3}}
             &&state_.option_before.words==std::array<uint32_t,3>{{2,3,3}}&&gui.passive_settings==state_.option_before.words,
             "Options exact selected2-to3 source writer step unavailable");
        Need(gui.source_ui_sound_intents==(stage?2u:1u),"Options direct setter/feedback source order changed");
        if(!stage){Need(gui.after_key_updates!=UINT32_MAX,"Options source update ordinal exhausted");state_.option_right_after_key_before=gui.after_key_updates;}
        else Need(gui.after_key_updates==state_.option_right_after_key_before+1,"Options common update cursor changed");
        out={gui.generation,option_input_sequence_,gui.input_epoch,stage,fields[stage],calls[stage],setters[stage],value,option_expected_,option_expected_};
        Need(out.after.writes!=UINT64_MAX,"Options writer ordinal exhausted");out.after.words[out.field]=value;++out.after.writes;
        Need(GameOptionRightReceiptValid(out),"Options source writer receipt shape refused");
        option_pending_=out;option_pending_restore_=option_pending_yes_=false;option_write_pending_=true;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::CommitMenuOptionWrite(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui,const GameOptionWriteReceipt& receipt){
    try{
        Need(state_.live&&!state_.blocked&&state_.menu_option_live&&vm_&&option_settings_&&option_write_pending_
             &&option_tick_pending_&&!option_gain_pending_&&GameOptionReceiptExact(receipt,option_pending_)
             &&receipt.stage==(option_pending_yes_?state_.option_yes_writes:option_pending_restore_?state_.option_restore_writes:state_.option_right_writes)
             &&(option_pending_yes_?GameOptionYesReceiptValid(receipt):option_pending_restore_?GameOptionBackupReceiptValid(receipt):GameOptionRightReceiptValid(receipt))
             &&GameOptionSnapshotExact(option_settings_->OptionSettings(),receipt.after),"Options actual full-word writer/readback refused");
        // Validate the original exact paused18 owner independently of the new
        // scalar value before advancing the privately issued prefix.
        const auto current=OwnerEventTarget();Need(target.generation==current.generation&&target.context.kind==current.context.kind&&target.context.word==current.context.word
             &&!target.context.context_owner.expired()&&target.context.context_owner.lock()==current.context.context_owner.lock()
             &&target.callback.kind==current.callback.kind&&target.callback.word==current.callback.word&&current.callback.word==0x7008DA28u
             &&gui.generation==state_.menu_option_generation&&gui.child_lease.parent_generation==state_.menu_option_parent
             &&gui.child_lease.script_generation==state_.generation&&gui.child_lease.run_id==vm_->current_witness().run_id
             &&gui.child_lease.context==option_lease_.context&&gui.child_lease.available_owner==option_lease_.available_owner
             &&gui.child_lease.available_generation==option_lease_.available_generation&&gui.child_lease.input_epoch==option_lease_.input_epoch
             &&!vm_->context_enabled()&&!vm_->callback_depth()&&!vm_->queued_callbacks()&&state_.menu_pause_return.returned&&!state_.menu_pause_return.failed
             &&GameWordMemoPausedOwnerExact(state_.menu_pause_return.restored,vm_->current_witness()),"Options paused writer owner changed");
        Need(!option_sound_expected_.live||GameSourceSoundSnapshotExact(option_settings_->OptionSoundOwner(),option_sound_expected_),"Options setter sound owner changed");
        if(option_pending_yes_){
            Need(option_gui_owner_&&OptionYesReturnReady(gui),"Options Yes publication retired before readback");
            const auto actual=option_gui_owner_->Snapshot();
            Need(actual.phase==3&&actual.state44==1&&!actual.close_requests&&actual.yes_writes==state_.option_yes_writes
                 &&actual.substate48==3&&actual.after_key_updates==option_tick_before_&&actual.input_epoch==option_current_epoch_
                 &&actual.source_consumer==receipt.call_pc&&gui.source_consumer==receipt.call_pc
                 &&actual.prompt.publication==option_yes_publication_.publication&&OptionYesReturnReady(actual),"Options Yes actual writer cursor replaced");
        }
        option_expected_=receipt.after;
        if(option_pending_yes_){option_yes_issued_[receipt.stage]=receipt;++state_.option_yes_writes;}
        else if(option_pending_restore_){option_restore_issued_[receipt.stage]=receipt;++state_.option_restore_writes;if(state_.option_restore_writes==3)option_cancel_sequence_=0;}
        else{option_issued_[receipt.stage]=receipt;++state_.option_right_writes;if(state_.option_right_writes==4)option_right_sequence_=0;}
        option_write_pending_=false;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::PrepareMenuOptionBackup(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui,uint32_t stage,uint32_t value,GameOptionWriteReceipt& out){
    const auto checked=CheckMenuOption(target,gui);if(!checked)return checked;
    try{
        Need(option_tick_pending_&&!option_write_pending_&&!option_gain_pending_&&stage<3&&stage==state_.option_restore_writes
             &&option_cancel_sequence_&&option_cancel_sequence_<=option_input_sequence_&&gui.last_sequence==option_input_sequence_
             &&gui.input_epoch==option_current_epoch_&&gui.phase==3&&gui.state44==1&&!gui.close_requests
             &&!gui.selection56&&!gui.previous50&&!gui.substate48&&value==state_.option_before.words[stage]
             &&gui.after_key_updates==option_tick_before_&&option_boundary_issued_>=1&&!option_prompt_stage_
             &&(option_boundary_issued_-1)%3==0&&gui.prompt_opens==(option_boundary_issued_-1)/3
             &&gui.prompt_returns==gui.prompt_opens
             &&gui.source_ui_sound_intents==1+(state_.option_right_writes?1u:0u)+(option_boundary_issued_-1)/3
             &&(!state_.option_right_writes||(state_.option_right_writes==4&&state_.option_gain_returns==1&&gui.right_message_continued)),"Options genuine fresh Cancel backup step unavailable");
        out={gui.generation,option_input_sequence_,gui.input_epoch,stage,stage,0x3526C+stage*8,0x24B1C+stage*0x18,value,option_expected_,option_expected_};
        Need(out.after.writes!=UINT64_MAX,"Options backup writer ordinal exhausted");out.after.words[stage]=value;++out.after.writes;
        Need(GameOptionBackupReceiptValid(out),"Options backup receipt shape refused");
        option_pending_=out;option_pending_yes_=false;option_pending_restore_=option_write_pending_=true;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::PrepareMenuOptionYes(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui,uint32_t stage,uint32_t value,GameOptionWriteReceipt& out){
    const auto checked=CheckMenuOption(target,gui);if(!checked)return checked;
    try{
        Need(option_tick_pending_&&option_tick_phase_==3&&!option_write_pending_&&!option_gain_pending_&&OptionYesReturnReady(gui)
             &&option_prompt_stage_==2&&!option_terminal_kind_&&option_boundary_issued_>=3&&option_boundary_issued_%3==0
             &&state_.option_yes_publication_receipt<=state_.option_tick_receipts
             &&stage<3&&stage==state_.option_yes_writes&&!state_.option_restore_writes&&!gui.restore_writes
             &&gui.source_consumer==0x3534C+stage*8
             &&gui.phase==3&&gui.state44==1&&!gui.close_requests&&!gui.yes_consumed&&!gui.close_kind
             &&gui.after_key_updates==option_tick_before_&&!gui.selection56&&!gui.previous50
             &&value==option_yes_words_[stage]&&gui.input_epoch==option_yes_publication_.result_epoch
             &&gui.last_sequence==option_yes_publication_.result_sequence
             &&gui.source_ui_sound_intents==1+(state_.option_right_writes?1u:0u)+(option_boundary_issued_-1)/3+1,
             "Options actual natural Yes current writer unavailable");
        out={gui.generation,option_yes_publication_.result_sequence,option_yes_publication_.result_epoch,stage,stage,0x3534C+stage*8,0x24B1C+stage*0x18,value,option_expected_,option_expected_};
        Need(out.after.writes!=UINT64_MAX,"Options Yes writer ordinal exhausted");out.after.words[stage]=value;++out.after.writes;
        Need(GameOptionYesReceiptValid(out),"Options current Yes receipt shape refused");
        option_pending_=out;option_pending_restore_=false;option_pending_yes_=option_write_pending_=true;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::PrepareMenuOptionGain(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui){
    const auto checked=CheckMenuOption(target,gui);if(!checked)return checked;
    if(gui.row_selections){
        const bool left=option_feedback_committed_&&GameSourceVoiceFeedbackValid(option_feedback_issued_)&&state_.option_row_writes==7&&gui.row_writes==7&&gui.row_left_steps==1;
        if(!option_tick_pending_||option_write_pending_||option_row_pending_||option_gain_pending_||state_.option_gain_returns!=(left?1u:0u)||state_.option_restore_writes
          ||(!left&&(state_.option_row_writes!=3||gui.row_writes!=3||gui.row_left_steps))||gui.gain_queries!=(left?2u:1u)||gui.gain_value!=105||gui.gain_returned
          ||gui.source_consumer!=0x4AFC8||gui.previous50!=1||gui.selection56!=1||gui.substate48||gui.source_ui_sound_intents!=(left?3u:2u)
          ||gui.after_key_updates!=option_tick_before_+1||!GameSourceSoundNegativeReady(option_sound_expected_))return {GameFsCode::Unavailable,0,"Options Down source gain owner unavailable"};
        option_gain_pending_=true;return {};
    }
    if(!option_tick_pending_||option_write_pending_||option_gain_pending_||state_.option_gain_returns||state_.option_restore_writes
       ||state_.option_right_writes!=4||gui.right_writes!=4||gui.gain_queries!=1||gui.gain_value!=105||gui.gain_returned
       ||gui.source_consumer!=0x4AFC8||gui.previous50||gui.selection56||gui.substate48||gui.source_ui_sound_intents!=2
       ||gui.after_key_updates!=state_.option_right_after_key_before+1||!GameSourceSoundNegativeReady(option_sound_expected_))
        return {GameFsCode::Unavailable,0,"Options selected query source owner unavailable"};
    option_gain_pending_=true;return {};
}
GameFsStatus GameActorScript::CommitMenuOptionGain(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui,const GameSourceSoundGainReceipt& receipt){
    const auto checked=CheckMenuOption(target,gui);if(!checked)return checked;
    if(!option_gain_pending_||state_.option_gain_returns>(gui.row_writes==7?1u:0u)||!GameSourceSoundGainValid(receipt)||receipt.value!=105
       ||!GameSourceSoundSnapshotExact(receipt.before,option_sound_expected_)
       ||!GameSourceSoundSnapshotExact(receipt.after,option_settings_->OptionSoundOwner()))
        return {GameFsCode::Unavailable,0,"Options actual source query return/readback refused"};
    option_gain_issued_=receipt;option_sound_expected_=receipt.after;state_.option_sound_after=receipt.after;
    option_gain_pending_=false;++state_.option_gain_returns;return {};
}
GameFsStatus GameActorScript::PrepareMenuOptionTick(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui){
    const auto checked=CheckMenuOption(target,gui);if(!checked)return checked;
    if(option_tick_pending_||option_write_pending_||option_row_pending_||option_gain_pending_||gui.failed)
        return {GameFsCode::Unavailable,0,"Options worker source tick already pending"};
    option_tick_before_=gui.after_key_updates;option_tick_phase_=gui.phase;option_tick_pending_=true;return {};
}
GameFsStatus GameActorScript::CommitMenuOptionTick(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui){
    const auto checked=CheckMenuOption(target,gui);if(!checked)return checked;
    const auto delta=option_tick_phase_==3&&!(gui.row_writes==7&&option_feedback_committed_&&gui.after_key_updates==option_tick_before_)?1u:0u;
    if(!option_tick_pending_||option_write_pending_||option_gain_pending_||gui.failed||option_tick_before_>UINT32_MAX-delta
       ||gui.after_key_updates!=option_tick_before_+delta)
        return {GameFsCode::Unavailable,0,"Options actual worker source cursor refused"};
    if(!option_yes_published_&&gui.prompt.state864==2&&gui.prompt.result86C==1){
        const auto& p=gui.prompt;const auto& nonce=option_yes_nonce_;
        if(option_prompt_stage_!=2||!option_yes_sequence_||option_yes_sequence_>option_input_sequence_
           ||p.result_sequence!=option_input_sequence_||p.result_sequence!=gui.last_sequence||p.result_epoch!=option_current_epoch_
           ||p.owner!=nonce.owner||p.generation!=nonce.generation||p.invocation!=nonce.invocation||p.input_epoch!=nonce.input_epoch
           ||p.parent!=gui.generation||p.selection||!p.active||p.completed_invocation==p.invocation||!p.result_update
           ||nonce.publication==UINT32_MAX||p.publication!=nonce.publication+1||nonce.yes_choices==UINT32_MAX||p.yes_choices!=nonce.yes_choices+1
           ||gui.phase!=3||gui.state44!=1||gui.substate48!=3||gui.yes_writes||gui.close_requests||gui.restore_writes)
            return {GameFsCode::Unavailable,0,"Options genuine sampled Yes publication unavailable"};
        option_yes_publication_=p;option_yes_words_=gui.current_words;option_yes_root_=option_expected_;
        option_yes_published_=true;state_.option_yes_publication_receipt=state_.option_tick_receipts+1;option_yes_sequence_=0;option_yes_armed_=false;
    }
    const auto from=state_.option_lifecycle_phase;
    bool valid=false;
    if(from==2)valid=(gui.phase==2||gui.phase==3)&&!gui.close_requests&&!gui.polls&&!gui.retirements&&gui.live&&gui.owner474&&gui.request10;
    if(from==3)valid=(gui.phase==3&&!gui.close_requests||gui.phase==4&&gui.close_requests==1
        &&((gui.close_kind==1&&state_.option_restore_writes==3&&!state_.option_yes_writes&&!gui.substate48)
           ||(gui.close_kind==2&&state_.option_yes_writes==3&&!state_.option_restore_writes&&gui.yes_consumed==1&&OptionYesReturnReady(gui))))
        &&!gui.polls&&!gui.retirements&&gui.live&&gui.owner474&&gui.request10;
    if(from==4)valid=(gui.phase==4&&!gui.polls&&!gui.retirement_requests&&!gui.retirement_pending&&gui.state44==2
        ||gui.phase==6&&gui.polls==1&&gui.retirement_requests==1&&gui.retirement_pending&&gui.state44==3&&!gui.visible&&!gui.request10&&!gui.loader469)
        &&gui.close_requests==1&&gui.live&&gui.owner474&&gui.template_owner&&gui.clone_owner&&gui.message.live&&gui.message.text_owner&&gui.message.references==1&&!gui.retirements&&!gui.retired_generation;
    if(from==6)valid=gui.phase==7&&state_.option_poll_receipt&&!state_.option_drain_receipt&&gui.polls==1&&gui.retirement_requests==1&&gui.retirements==1
        &&gui.retired_generation==gui.generation&&!gui.live&&!gui.owner474&&!gui.request10&&!gui.retirement_pending&&!gui.template_owner&&!gui.clone_owner
        &&!gui.message.live&&!gui.message.text_owner&&!gui.message.references;
    if(!valid)return {GameFsCode::Unavailable,0,"Options source lifecycle transition/refcount receipt refused"};
    if(from==3&&gui.phase==4){option_terminal_kind_=gui.close_kind;state_.option_completion_kind=option_terminal_kind_;
        if(option_terminal_kind_==2)option_prompt_stage_=3;}
    if(from==4&&gui.phase==6)state_.option_poll_receipt=state_.option_tick_receipts+1;
    if(from==6&&gui.phase==7)state_.option_drain_receipt=state_.option_tick_receipts+1;
    if(gui.row_selections){option_down_sequence_=0;if(!gui.row_left_steps)option_left_sequence_=0;}
    state_.option_lifecycle_phase=gui.phase;state_.option_after_key_cursor=gui.after_key_updates;option_tick_pending_=false;++state_.option_tick_receipts;return {};
}
GameFsStatus GameActorScript::AdvanceMenuOptionInputEpoch(const GameOwnerEventTarget& target,GameSystemOptionFrontier& owner){
    if(&owner!=option_gui_owner_)return {GameFsCode::Unavailable,0,"Options input boundary actual owner replaced"};
    const auto gui=owner.Snapshot();const auto checked=CheckMenuOption(target,gui);if(!checked)return checked;
    const auto& p=gui.prompt;bool valid=false;uint32_t next=option_prompt_stage_;
    if(gui.boundary_cause==1)valid=!option_boundary_issued_&&!gui.substate48&&!gui.prompt_opens;
    if(gui.boundary_cause==2){valid=option_boundary_issued_&&option_prompt_stage_==0&&gui.substate48==3&&p.state864==-1&&p.active&&p.parent==gui.generation&&gui.prompt_opens==gui.prompt_returns+1;next=1;}
    if(gui.boundary_cause==3){valid=option_prompt_stage_==1&&gui.substate48==3&&p.state864==1&&p.active&&!p.result86C;next=2;}
    if(gui.boundary_cause==4){valid=option_prompt_stage_==2&&!gui.substate48&&p.state864==3&&!p.active&&p.result86C==2&&p.completed_invocation==p.invocation&&gui.prompt_opens==gui.prompt_returns;next=0;}
    if(option_tick_pending_||!valid||gui.phase!=3||gui.state44!=1||option_boundary_issued_==UINT32_MAX
       ||gui.input_boundaries!=option_boundary_issued_+1||option_current_epoch_==UINT64_MAX)
        return {GameFsCode::Unavailable,0,"Options source input boundary/cause unavailable"};
    if(!owner.SetInputEpoch(option_current_epoch_+1))return {GameFsCode::Unavailable,0,"Options current epoch attach refused"};
    ++option_current_epoch_;++option_boundary_issued_;option_prompt_stage_=next;DisarmMenuOptionInput();
    return CheckMenuOption(target,owner.Snapshot());
}
GameFsStatus GameActorScript::VerifyMenuOptionPrefix(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui){
    state_.option_prefix_verified=false;
    if(!gui.failed||gui.source_consumer!=0x4AFC8||gui.phase!=3||gui.previous50||gui.selection56||gui.substate48
       ||gui.source_ui_sound_intents!=2||gui.after_key_updates!=state_.option_right_after_key_before+1
       ||gui.right_writes!=4||gui.gain_queries!=1||gui.gain_value!=105
       ||gui.gain_returned||gui.right_message_continued||gui.close_requests||gui.polls||gui.retirements||gui.gain.source_id_known||gui.gain.source_dispatched)
        return {GameFsCode::Unavailable,0,"Options stopped source prefix incomplete"};
    const auto checked=CheckMenuOptionOwner(target,gui,true);if(!checked)return checked;
    state_.option_prefix_verified=true;state_.option_after=option_expected_;return {};
}
GameFsStatus GameActorScript::CompleteMenuOption(const GameOwnerEventTarget& target,const GameSystemOptionSnapshot& gui){
    const auto checked=CheckMenuOption(target,gui);if(!checked)return checked;
    if(state_.option_lifecycle_phase!=7||!state_.option_poll_receipt||state_.option_drain_receipt!=state_.option_poll_receipt+1
       ||option_tick_pending_||option_gain_pending_||option_write_pending_||!option_terminal_kind_
       ||(state_.option_right_writes&&(state_.option_right_writes!=4||state_.option_gain_returns!=1||!gui.gain_returned||!gui.right_message_continued))
       ||gui.phase!=7||gui.retired_generation!=gui.generation||gui.live||gui.owner474||gui.request10||gui.retirement_pending||gui.template_owner||gui.clone_owner
       ||gui.message.live||gui.message.text_owner||gui.message.registered||gui.message.references||gui.state44!=3||gui.loader469
       ||gui.opens!=1||gui.close_requests!=1||gui.close_completions!=1||gui.polls!=1||gui.retirement_requests!=1||gui.retirements!=1)
        return {GameFsCode::Unavailable,0,"Options current source poll/deferred deleting owner incomplete"};
    const bool cancel=option_terminal_kind_==1&&gui.close_kind==1&&state_.option_restore_writes==3&&!state_.option_yes_writes&&!gui.substate48
        &&option_expected_.words==state_.option_before.words&&option_expected_.writes==state_.option_before.writes+3+state_.option_right_writes;
    const bool yes=option_terminal_kind_==2&&gui.close_kind==2&&option_prompt_stage_==3&&state_.option_yes_writes==3&&!state_.option_restore_writes
        &&gui.yes_consumed==1&&OptionYesReturnReady(gui)&&option_expected_.words==option_yes_words_
        &&option_expected_.writes==option_yes_root_.writes+3&&option_yes_root_.words==option_yes_words_;
    if(!cancel&&!yes)return {GameFsCode::Unavailable,0,"Options distinct current Yes/backup Cancel final readback incomplete"};
    state_.option_after=option_settings_->OptionSettings();state_.option_sound_after=option_settings_->OptionSoundOwner();
    vm_->resume_modal_context();state_.menu_option_live=false;++state_.menu_option_completions;return {};
}
GameFsStatus GameActorScript::ReadMenuDiary(const GameOwnerEventTarget& expected,int32_t manager58,uint32_t mode,GameRecordImage& image,GameDiaryOpenInputs& in)const{
    try{
        Need(state_.live&&vm_&&!state_.blocked&&records_&&records_->Live()&&clock_&&!vm_->callback_depth()&&!vm_->queued_callbacks(),"Diary requires current GAME71 bank/context owner");
        const auto target=OwnerEventTarget();
        Need(expected.generation==target.generation&&expected.context.kind==target.context.kind&&expected.context.word==target.context.word
             &&!expected.context.context_owner.expired()&&expected.context.context_owner.lock()==target.context.context_owner.lock()
             &&expected.callback.kind==target.callback.kind&&expected.callback.word==target.callback.word&&target.callback.word==0x7008DA28u,
             "Diary current registered context/Local0 replaced");
        Need(profile_.live&&profile_.actor<=4&&manager58==int32_t(profile_.actor),"Diary actual confirmed TITLE/LoadMan58 owner mismatch");
        image=records_->Image();in.profile=profile_;in.manager58=manager58;in.mode=mode;
        in.script_generation=state_.generation;in.run_id=vm_->current_witness().run_id;in.clock_word=clock_();
        in.date=GameRecordDateFromClockWord(in.clock_word);in.hour=((in.clock_word&0x0FFFFFFF)%1440)/60;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::PrepareMenuDiary(const GameOwnerEventTarget& target,uint64_t gui,uint64_t parent,const GameRecordImage& before,const GameDiaryOpenInputs& inputs)const{
    try{
        Need(gui&&gui>state_.menu_diary_generation&&parent&&!state_.menu_diary_live&&!state_.menu_wordmemo_live
             &&!state_.menu_notebook_live&&!state_.menu_inventory_live&&!state_.inventory_requested&&vm_&&vm_->context_enabled(),"Diary origin/generation already owned");
        GameRecordImage current;GameDiaryOpenInputs actual;const auto read=ReadMenuDiary(target,inputs.manager58,inputs.mode,current,actual);Need(bool(read),read.detail.c_str());
        Need(current.sha==before.sha&&inputs.script_generation==state_.generation&&inputs.run_id==actual.run_id
             &&inputs.profile.source_nickname==profile_.source_nickname&&inputs.profile.source_player==profile_.source_player
             &&inputs.profile.actor==profile_.actor,"Diary full bank/profile changed before request");
        GameDiarySelection selected;(void)GameProjectDiaryOpen(before,inputs,selected);
        Need(state_.menu_open_return.returned&&!state_.menu_open_return.failed&&state_.menu_open_return.menu_generation==parent,"Diary genuine opening callback missing");return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::BindMenuDiary(const GameOwnerEventTarget& target,uint64_t gui,uint64_t parent,const GameRecordImage& before,const GameDiaryOpenInputs& inputs,GameDiaryIo& io){
    const auto checked=PrepareMenuDiary(target,gui,parent,before,inputs);if(!checked)return checked;
    const auto manager=io.DiaryIo();if(!manager.live||!manager.owner||!manager.generation)return {GameFsCode::Unavailable,0,"Diary real IO owner missing at binding"};
    GameDiarySelection selected;const auto approved=GameProjectDiaryOpen(before,inputs,selected);
    diary_io_=&io;diary_io_at_bind_=manager;diary_inputs_=inputs;state_.diary_before=before.sha;state_.diary_approved=approved.sha;state_.diary_after={};
    state_.menu_diary_generation=gui;state_.menu_diary_parent=parent;state_.menu_diary_live=true;++state_.menu_diary_requests;return {};
}
GameFsStatus GameActorScript::CheckMenuDiary(const GameOwnerEventTarget& expected,const GameDiarySnapshot& gui)const{
    try{
        Need(state_.live&&!state_.blocked&&vm_&&records_&&records_->Live()&&state_.menu_diary_live
             &&gui.generation==state_.menu_diary_generation&&gui.lease.parent_generation==state_.menu_diary_parent
             &&gui.lease.script_generation==state_.generation&&gui.lease.run_id==vm_->current_witness().run_id,"Diary current GUI/parent/script/bank identity mismatch");
        const auto target=OwnerEventTarget();
        Need(expected.generation==target.generation&&expected.context.kind==target.context.kind&&expected.context.word==target.context.word
             &&!expected.context.context_owner.expired()&&expected.context.context_owner.lock()==target.context.context_owner.lock()
             &&expected.callback.kind==target.callback.kind&&expected.callback.word==target.callback.word&&target.callback.word==0x7008DA28u,"Diary registered context/Local0 replaced");
        const auto& pause=state_.menu_pause_return;
        Need(pause.returned&&!pause.failed&&pause.event==18&&pause.menu_generation==state_.menu_diary_parent&&!vm_->context_enabled()
             &&!vm_->callback_depth()&&!vm_->queued_callbacks()&&GameWordMemoPausedOwnerExact(pause.restored,vm_->current_witness()),"Diary exact suspended18 caller/frame/deadline changed");
        const auto io=diary_io_?diary_io_->DiaryIo():GameDiaryIoSnapshot{};
        Need(io.live&&io.owner==diary_io_at_bind_.owner&&io.generation==diary_io_at_bind_.generation,"Diary bound IO/filesystem owner retired/replaced");
        Need(!gui.failed&&(gui.opened_generation==0||gui.opened_generation==gui.generation),"Diary terminal failure or foreign Open generation");
        Need(gui.before_sha==state_.diary_before&&gui.approved_sha==state_.diary_approved
             &&records_->Image().sha==(gui.opened_generation==gui.generation?state_.diary_approved:state_.diary_before),"Diary source-approved whole typed bank changed");
        Need(gui.inputs.profile.source_player==profile_.source_player&&gui.inputs.profile.source_nickname==profile_.source_nickname
             &&gui.inputs.manager58==diary_inputs_.manager58&&gui.inputs.clock_word==diary_inputs_.clock_word&&gui.inputs.mode==diary_inputs_.mode,
             "Diary source input/provenance replaced");return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::CompleteMenuDiary(const GameOwnerEventTarget& target,const GameDiarySnapshot& gui){
    const auto checked=CheckMenuDiary(target,gui);if(!checked)return checked;
    if(gui.opened_generation!=gui.generation||gui.phase!=7||gui.retired_generation!=gui.generation||gui.live||gui.owner47C||gui.request12||gui.retirement_pending
       ||gui.text_owner||gui.nested_owner||gui.state154!=3||gui.loader469||!gui.opens||!gui.close_requests||!gui.close_completions||!gui.polls||!gui.retirement_requests||!gui.retirements)
        return {GameFsCode::Unavailable,0,"Diary current-generation real close/poll/deferred+4 drain incomplete"};
    state_.diary_after=records_->Image().sha;
    vm_->resume_modal_context();state_.menu_diary_live=false;++state_.menu_diary_completions;return {};
}
GameFsStatus GameActorScript::ReadMenuWordMemo(const GameOwnerEventTarget& expected,GameWordMemoRows& out)const{
    try{
        Need(state_.live&&vm_&&!state_.blocked&&!vm_->callback_depth()&&!vm_->queued_callbacks(),"Wordmemo read needs idle owning VM");
        const auto target=OwnerEventTarget();
        Need(expected.generation==target.generation&&expected.context.kind==target.context.kind&&expected.context.word==target.context.word
             &&!expected.context.context_owner.expired()&&expected.context.context_owner.lock()==target.context.context_owner.lock()
             &&expected.callback.kind==target.callback.kind&&expected.callback.word==target.callback.word&&target.callback.word==0x7008DA28u,
             "Wordmemo actual registered context/Local0 replaced");
        out=GameObserveWordMemoRows(*vm_,state_.generation,bindings_.at(10));return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::BindMenuWordMemo(const GameOwnerEventTarget& target,uint64_t gui,uint64_t parent,const GameWordMemoRows& rows){
    const auto status=PrepareMenuWordMemo(target,gui,parent,rows);if(!status)return status;
    state_.wordmemo_before=rows;state_.wordmemo_after={};wordmemo_registration_at_bind_=state_.wordmemo_registrations;
    state_.menu_wordmemo_generation=gui;state_.menu_wordmemo_parent=parent;
    state_.menu_wordmemo_live=true;++state_.menu_wordmemo_requests;return {};
}
GameFsStatus GameActorScript::PrepareMenuWordMemo(const GameOwnerEventTarget& target,uint64_t gui,uint64_t parent,const GameWordMemoRows& rows)const{
    if(!gui||gui<=state_.menu_wordmemo_generation||!parent||state_.menu_diary_live||state_.menu_wordmemo_live||state_.menu_notebook_live
       ||state_.menu_inventory_live||state_.inventory_requested||!vm_||!vm_->context_enabled())
        return {GameFsCode::Unavailable,0,"Wordmemo origin/generation unavailable"};
    GameWordMemoRows current;const auto status=ReadMenuWordMemo(target,current);if(!status)return status;
    if(!rows.valid||rows.sha!=current.sha||rows.run_id!=current.run_id||rows.code_sha!=current.code_sha
       ||rows.records!=current.records||rows.cells!=current.cells||rows.eligible_records!=current.eligible_records||current.eligible_records
       ||!state_.wordmemo_imports||!dictionary_.ImportedRecordCount()
       ||dictionary_.ImportedRecordsInRoot(*vm_,bindings_.at(10))!=dictionary_.ImportedRecordCount()
       ||!state_.menu_open_return.returned||state_.menu_open_return.failed||state_.menu_open_return.menu_generation!=parent)
        return {GameFsCode::Unavailable,0,"Wordmemo registration changed before binding"};
    return {};
}
GameFsStatus GameActorScript::CheckMenuWordMemo(const GameOwnerEventTarget& target,uint64_t gui,uint64_t parent)const{
    try{
        Need(state_.menu_wordmemo_live&&gui==state_.menu_wordmemo_generation&&parent==state_.menu_wordmemo_parent
             &&wordmemo_registration_at_bind_==state_.wordmemo_registrations&&state_.wordmemo_imports
             &&dictionary_.ImportedRecordCount()&&dictionary_.ImportedRecordsInRoot(*vm_,bindings_.at(10))==dictionary_.ImportedRecordCount(),
             "Wordmemo GUI/registration/imported-record owner replaced");
        const auto& pause=state_.menu_pause_return;
        Need(vm_&&pause.returned&&!pause.failed&&pause.event==18&&pause.menu_generation==parent
             &&!vm_->context_enabled()&&!vm_->callback_depth()&&!vm_->queued_callbacks(),"Wordmemo needs actual disabled18/32 owner");
        Need(GameWordMemoPausedOwnerExact(pause.restored,vm_->current_witness()),"Wordmemo suspended18 caller/frame/deadline changed");
        GameWordMemoRows current;const auto status=ReadMenuWordMemo(target,current);Need(bool(status),status.detail.c_str());
        const auto& before=state_.wordmemo_before;
        Need(before.valid&&current.sha==before.sha&&current.run_id==before.run_id&&current.code_sha==before.code_sha
             &&current.script_generation==before.script_generation&&current.root_table==before.root_table
             &&current.root_index==before.root_index&&current.root_header==before.root_header,
             "Wordmemo immutable registered typed image changed");return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameActorScript::CompleteMenuWordMemo(const GameOwnerEventTarget& target,const GameWordMemoSnapshot& gui){
    try{
        const auto checked=CheckMenuWordMemo(target,gui.generation,gui.lease.parent_generation);Need(bool(checked),checked.detail.c_str());
        Need(gui.phase==7&&gui.retired_generation==gui.generation&&!gui.live&&!gui.owner470&&!gui.request8
             &&!gui.retirement_pending&&gui.state9B0==3&&gui.loader469==0&&gui.close_requests&&gui.close_completions
             &&gui.polls&&gui.retirement_requests&&gui.retirements,"Wordmemo needs real current-generation close/poll/deferred drain");
        GameWordMemoRows after;const auto status=ReadMenuWordMemo(target,after);Need(bool(status),status.detail.c_str());
        state_.wordmemo_after=after;
        // NativeActualClosedWordMemoWake, separate from original post32 producer.
        vm_->resume_modal_context();state_.menu_wordmemo_live=false;++state_.menu_wordmemo_completions;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameOwnerEventTarget GameActorScript::OwnerEventTarget()const{
    Need(state_.live&&vm_&&program_.header().local_count>0,"Owner event target requires live context/local owner");
    return {state_.generation,vm_->context_value(),vm_->read_slot(script::Table::Local,0)};
}
bool GameActorScript::MenuPlacementAvailable()const{
    try{
        if(!state_.live||!vm_||!runtime_||!runtime_->OwnersLive()||runtime_->Snapshot().manager_scene14!=0)return false;
        const auto header=vm_->read_slot(script::Table::Ctx28,0x243F);if(header.kind!=Value::Kind::ArrayHeader||(header.word&0xFFF)<1)return false;
        const auto gate=vm_->peek_reference(script::Value{Value::Kind::Reference,0,script::Table::Ctx28,uint16_t(0x2440)});
        return gate.kind==Value::Kind::Scalar&&gate.word==0x20000000u&&gate.cp932.empty();
    }catch(...){return false;}
}
bool GameActorScript::ValidateMenuInventoryCertificate(const GameOwnerEventTarget& expected,const GameInventoryActionCertificate& c)const{
    try{
        if(!state_.live||!vm_||!state_.menu_inventory_live||state_.inventory_requested||vm_->context_enabled()||vm_->callback_depth()||vm_->queued_callbacks())return false;
        const auto target=OwnerEventTarget();const auto& a=state_.inventory_action;const auto& actual=a.certificate;const auto proof=menu_placement_.Snapshot();
        return expected.generation==target.generation&&expected.callback.kind==target.callback.kind&&expected.callback.word==target.callback.word
            &&expected.context.word==target.context.word&&!expected.context.context_owner.expired()&&expected.context.context_owner.lock()==target.context.context_owner.lock()
            &&a.placement&&a.menu_origin&&a.completed&&a.returned&&!a.failed&&!a.pending&&c.valid&&actual.valid&&proof.callback_returned&&!proof.pending
            &&c.generation==state_.inventory_gui_generation&&c.generation==actual.generation&&c.receipt==actual.receipt&&c.receipt==proof.receipt
            &&c.event==actual.event&&c.row==actual.row&&c.before_display==actual.before_display&&c.after_display==actual.after_display
            &&c.after_display==GameInventoryItemsHash(state_.inventory_items)&&c.approved_image==actual.approved_image
            &&c.approved_image==state_.menu_inventory_verified.sha256&&c.mutable_fields==actual.mutable_fields;
    }catch(...){return false;}
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
        const bool flow5=event_id==12&&argument==5;
        GameFlow5EventSnapshot observation;
        if(flow5){
            observation.attempts=state_.flow5_event.attempts+1;observation.unobserved=state_.flow5_event.unobserved;
            if(state_.flow5_event.accepted&&!state_.flow5_event.returned)++observation.unobserved;
            observation.generation=state_.generation;observation.callback_word=code.word;
            observation.suspended=vm_->current_witness();observation.enabled_before=vm_->context_enabled();
            observation.manager9_before=runtime_&&runtime_->Snapshot().manager_request9;
            try{observation.guards_before=Flow5Guards(*vm_);observation.guards_valid=true;}catch(...){}
        }
        state_.owner_event_callback_word=code.word;
        state_.owner_event_result=vm_->post_owner_event(code,int32_t(event_id),argument);
        ++state_.owner_event_calls;
        if(flow5){
            observation.queue_result=state_.owner_event_result;observation.accepted=observation.queue_result==0;
            observation.receipt=observation.accepted?vm_->last_callback_receipt():0;
            observation.entered=vm_->current_witness();observation.callback_frame=observation.entered.frame_id;
            observation.queued=observation.accepted&&(observation.suspended.event_envelope_present||observation.suspended.event_scope_depth);
            state_.flow5_event=std::move(observation);
        }
        if(state_.owner_event_result!=0){std::ostringstream why;why<<"Owner event rejected event="<<event_id<<" argument="<<argument<<" queue_result="<<state_.owner_event_result;
            return {GameFsCode::Unavailable,0,why.str()};}
        return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
void GameActorScript::ObserveFlow5Event(const script::FrameTransition& t)noexcept{
    auto& c=state_.flow5_event;if(!c.accepted||c.returned||t.callback_receipt!=c.receipt)return;
    if(t.before.code_entry==0x8DA28&&t.before.frame_id==c.callback_frame&&t.opcode>=0x20&&t.opcode<=0x23){
        if(c.calls<c.call_pcs.size()){c.call_pcs[c.calls]=t.before.current_pc;c.call_entries[c.calls]=t.after.code_entry;++c.calls;}
        else ++c.unobserved;
    }
    if((t.opcode!=0x32&&t.opcode!=0x1C)||!t.before.event_envelope_present||t.after.event_envelope_present)return;
    c.returned=true;c.return_pc=t.before.current_pc;c.return_opcode=t.opcode;c.restored=t.after;c.enabled_after=vm_->context_enabled();
    c.restored_exact=GameFlow5EventRestored(c,t)&&c.enabled_before==c.enabled_after;
    c.manager9_after=runtime_&&runtime_->Snapshot().manager_request9;
    try{c.guards_after=Flow5Guards(*vm_);}catch(...){c.guards_valid=false;}
}
void GameActorScript::ObserveNotebookStore(const Value& ref,bool write)noexcept{
    if(!write||!vm_||ref.kind!=Value::Kind::Reference)return;
    try{
        const auto found=bindings_.find(11);if(found==bindings_.end())return;
        const auto& root=found->second;
        if(root.kind!=Value::Kind::Reference||!vm_->stable_value_owner(root)||root.index==UINT16_MAX)return;
        const auto h=vm_->peek_reference(root);if(h.kind!=Value::Kind::ArrayHeader||!(h.word&0xFFF))return;
        auto slot=root;++slot.index;const auto row=vm_->peek_reference(slot);
        if(row.kind!=Value::Kind::Reference||!vm_->stable_value_owner(row)||row.table!=ref.table)return;
        const auto header=vm_->peek_reference(row);const auto cells=header.word&0xFFF;
        if(header.kind!=Value::Kind::ArrayHeader||!cells||cells>64||ref.index<=row.index||uint32_t(ref.index)>uint32_t(row.index)+cells)return;
        if(state_.notebook_slot0_stores.size()==64){++state_.notebook_slot0_stores_dropped;return;}
        const auto w=vm_->current_witness();const auto* cmd=program_.command(vm_->pc());
        GameNotebookStoreWitness s;s.field=uint32_t(ref.index)-row.index-1;s.pc=vm_->pc();s.entry=w.code_entry;
        s.opcode=cmd?uint32_t(cmd->opcode):UINT32_MAX;s.table=uint32_t(ref.table);s.index=ref.index;
        s.row_index=row.index;s.row_header=header.word;s.registered_table=uint32_t(root.table);s.registered_index=root.index;
        s.registration=state_.notebook_registrations;s.run_id=w.run_id;s.generation=state_.generation;
        s.executed=w.executed+1;s.frame=w.frame_id;s.value=PassiveValue(vm_->peek_reference(ref));
        state_.notebook_slot0_stores.push_back(std::move(s));
    }catch(...){++state_.notebook_slot0_stores_dropped;}
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
void GameActorScript::ObserveWordSelection(const script::FrameTransition& t)noexcept{
    if(!state_.live||!vm_||t.opcode!=0x31||!t.before.valid||!t.after.valid||t.before.code_entry!=0x89B6
       ||t.before.run_id!=t.after.run_id||t.before.code_sha256!=t.after.code_sha256)return;
    const auto sequence=++state_.word_selection_returns;
    state_.word_selection_return={true,sequence,t.before.current_pc,t.before,t.after};
    // Success is the actual final31/id result, separate from Input32 completion
    // and from refusals. The row capture executes no VM read/write operation.
    const auto& result=t.after.result;
    if(t.before.current_pc!=0x8A56||!t.before.frame_captured||result.kind!=Value::Kind::Scalar||result.word>>28!=2
       ||t.before.frame_values[0].kind!=Value::Kind::Scalar||t.before.frame_values[0].word!=result.word)return;
    const auto row=result.word&0x0FFFFFFF;if(row>=600)return;
    state_.word_accepted_return=state_.word_selection_return;
    state_.word_accepted_row=ObserveWordRow(row);
}
GameActorScriptSnapshot GameActorScript::Snapshot() const {
    auto out=state_;out.menu_placement=menu_placement_.Snapshot();out.menu_model_effects=menu_placement_.Effects();out.menu_source_stores=menu_placement_.Stores();out.dictionary=dictionary_.Snapshot();if(records_)out.records=records_->Snapshot();if(vm_){out.owned_conversation=owned_conversation_.Snapshot(*vm_);out.instructions=vm_->executed();out.call_depth=vm_->call_depth();out.random_selections=vm_->random_selections();out.selected_value_word=vm_->last_random_value().word;out.root_functions_returned=vm_->root_functions_returned();
        out.wordmemo_import_records=dictionary_.ImportedRecordCount();
        if(const auto it=bindings_.find(10);it!=bindings_.end())out.wordmemo_import_rows=dictionary_.ImportedRecordsInRoot(*vm_,it->second);
        out.b4_calls=vm_->b4_calls();out.b4_operand_word=vm_->b4_operand_word();out.b4_result_word=vm_->b4_result_word();
        out.e2_calls=vm_->e2_calls();out.e2_left_word=vm_->e2_left_word();out.e2_right_word=vm_->e2_right_word();out.e2_result_word=vm_->e2_result_word();
        out.d0_calls=vm_->d0_calls();out.d0_left_word=vm_->d0_left_word();out.d0_right_word=vm_->d0_right_word();out.d0_result_word=vm_->d0_result_word();
        out.d5_calls=vm_->d5_calls();out.d5_first_pc=vm_->d5_first_pc();out.d5_first_left_word=vm_->d5_first_left_word();out.d5_first_right_word=vm_->d5_first_right_word();out.d5_first_result_word=vm_->d5_first_result_word();
        out.op55_calls=vm_->op55_calls();out.op55_first_pc=vm_->op55_first_pc();out.op55_first_before=vm_->op55_first_before();out.op55_first_after=vm_->op55_first_after();out.op55_first_table=vm_->op55_first_table();out.op55_first_index=vm_->op55_first_index();
        out.ctx_reference_branches=vm_->ctx_reference_branches();out.ctx_reference_first_pc=vm_->ctx_reference_first_pc();out.ctx_reference_first_index=vm_->ctx_reference_first_index();
        out.native_reserved_local_nil=vm_->native_reserved_local_nil();
        auto& controls=out.ordinary_controls;controls.generation=state_.generation;
        try{
            controls.run_id=vm_->current_witness().run_id;controls.error_code=1;
            const auto capture=[](const Value& v){script::ValueWitness w;w.kind=v.kind;w.word=v.word;w.table=uint32_t(v.table);w.index=v.index;return w;};
            const auto header=vm_->read_slot(script::Table::Global,9);controls.values[0]=capture(header);
            Need(header.kind==Value::Kind::ArrayHeader&&(header.word&0xFFF)>8,"Ordinary Global9 array extent");
            controls.values[1]=capture(vm_->read_slot(script::Table::Global,18));
            controls.values[2]=capture(vm_->read_slot(script::Table::Global,0x9A76));
            controls.values[3]=capture(vm_->read_slot(script::Table::Ctx28,0x3DF7));
            controls.values[4]=capture(vm_->read_slot(script::Table::Ctx28,0x2871));
            controls.valid=true;controls.error_code=0;
        }catch(...){/* Observation failure never changes input eligibility. */}
        out.vm_current=vm_->current_witness();out.normal_return=vm_->last_normal_return();out.farewell_return=vm_->last_normal_return(1);
        try{out.flow5_guards=Flow5Guards(*vm_);out.flow5_guards_valid=true;}catch(...){}
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
    dictionary_import_target_={};
    wordmemo_registration_at_bind_=0;
    notebook_self_before_={};notebook_self_registration_=0;
    menu_mark_.End();
    menu_placement_.End();
    owned_conversation_.Close();
    ascii_class70_.Close();state_.ascii_class70_live=false;state_.ascii_class70_generation=0;
    state_.ascii_class70_current_word=ascii_class70_.CurrentWord();state_.ascii_class70_other_word=ascii_class70_.OtherWord();
    state_=Snapshot();
    word_consumer_row_index_=UINT32_MAX;
    diary_io_=nullptr;diary_io_at_bind_={};diary_inputs_={};
    dictionary_.Close();bindings_.clear();vm_.reset();program_={};script_image_.Reset();profile_={};clock_={};rng_=nullptr;runtime_=nullptr;records_=nullptr;sound_=nullptr;
    state_.live=false;state_.awaiting_message=state_.awaiting_choice=state_.awaiting_input=false;
    state_.source_main_input_suppressed48=false;
}
GameWordRowObservation GameActorScript::ObserveWordRow(uint32_t row)const noexcept{
    if(state_.live&&vm_)return GameObserveWordRow(*vm_,state_.generation,row);
    GameWordRowObservation out;out.generation=state_.generation;out.row=row;out.error_code=1;return out;
}
} // namespace dokodemo::recovered
