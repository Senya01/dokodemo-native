#include "game_sound_control_native.h"
#include <atomic>
#include <tuple>
#include <chrono>
#include <cmath>

namespace dokodemo::recovered {
namespace {
bool BankSame(const PackageViewDescription& a,const PackageViewDescription& b)noexcept{
    return std::tie(a.owner,a.generation,a.serial,a.allocation_bytes,a.host_address,a.offset,a.size,a.source_address,a.allocation_source_base,a.readable,a.writable)
        ==std::tie(b.owner,b.generation,b.serial,b.allocation_bytes,b.host_address,b.offset,b.size,b.source_address,b.allocation_source_base,b.readable,b.writable);
}
bool RecordSame(const GameSourceSoundRecord& a,const GameSourceSoundRecord& b)noexcept{
    return std::tie(a.name,a.context54,a.context58,a.resource,a.resource_state,a.vtable,a.bank_handle,a.bank_index,a.handle,a.gain,a.state18,a.state1C,a.state20,a.step,a.position,a.request,a.flags,a.rate,a.bank_attached,a.bank_handle_known,a.name_known,a.context_owner,a.context_generation)
        ==std::tie(b.name,b.context54,b.context58,b.resource,b.resource_state,b.vtable,b.bank_handle,b.bank_index,b.handle,b.gain,b.state18,b.state1C,b.state20,b.step,b.position,b.request,b.flags,b.rate,b.bank_attached,b.bank_handle_known,b.name_known,b.context_owner,b.context_generation);
}
bool ColdRecord(const GameSourceSoundRecord& r,uint32_t vtable)noexcept{
    return !r.name&&!r.context54&&!r.context58&&!r.resource&&!r.resource_state&&r.vtable==vtable&&r.bank_handle==-1&&r.bank_index==-1&&r.handle==-1
        &&r.gain==128&&!r.state18&&!r.state1C&&!r.state20&&r.step==4&&!r.position&&!r.request&&!r.flags&&r.rate==1.0f
        &&!r.bank_attached&&r.bank_handle_known&&r.name_known&&!r.context_owner&&!r.context_generation;
}
}
bool GameSourceBankRegistrationExact(const GameSourceBankRegistration& a,const GameSourceBankRegistration& b)noexcept{
    return std::tie(a.live,a.attached,a.coherent,a.registry,a.generation,a.serial,a.id,a.active,a.mode,a.state,a.writer,a.word0C,a.word30,a.initial_zero24_mask,a.ids,a.chunks,a.sizes,a.digest)
        ==std::tie(b.live,b.attached,b.coherent,b.registry,b.generation,b.serial,b.id,b.active,b.mode,b.state,b.writer,b.word0C,b.word30,b.initial_zero24_mask,b.ids,b.chunks,b.sizes,b.digest)
        &&BankSame(a.image,b.image)&&BankSame(a.tail,b.tail);
}
GameFsStatus GameSourceBankRegistry::Allocate(PackageView image,int32_t& result){
    try{
        const auto d=image.Describe();
        if(!d.readable||d.size<16||d.size>UINT32_MAX||image.ReadLE32(0)!=0x44584753)
            return {GameFsCode::MalformedDirectory,0,"5536C live SGXD image unavailable"};
        const uint32_t end=image.ReadLE32(8);if(end<16||end>d.size)return {GameFsCode::MalformedDirectory,0,"5536C chunk boundary"};
        GameSourceBankRegistration e;e.image=d;
        for(uint64_t at=16;at<end;){
            if(end-at<8)return {GameFsCode::MalformedDirectory,0,"55208 truncated chunk"};
            const auto tag=image.ReadLE32(at),n=image.ReadLE32(at+4);if(n>end-at-8)return {GameFsCode::MalformedDirectory,0,"55208 chunk outside image"};
            const uint32_t tags[]={0x444E4752,0x45564157,0x44514553,0x454D414E};
            for(size_t i=0;i<4;++i)if(tag==tags[i]){if(e.chunks[i])return {GameFsCode::MalformedDirectory,0,"55208 duplicate selected chunk"};e.chunks[i]=uint32_t(at+8);e.sizes[i]=n;}
            at+=8+uint64_t(n);
        }
        // Validate actual initialized payload, not just a saved descriptor.
        e.digest=image.InitializedDigest();
        PackageView tail;
        if(image.ReadLE32(12)&0x80000000u){
            if(end==d.size)return {GameFsCode::MalformedDirectory,0,"55F2C negative header without wave tail"};
            for(auto chunk:e.chunks)if(!chunk)return {GameFsCode::MalformedDirectory,0,"55560 selected bank chunk absent"};
            tail=image.Subview(end,d.size-end);e.tail=tail.Describe();e.attached=true;e.mode=1;e.state=0;
        }
        int32_t id=-1;for(int32_t i=63;i>=0;--i)if(!entries_[size_t(i)].live){id=i;break;}
        if(id<0)return {GameFsCode::Busy,0,"NativeIndexedBankPool exhausted"};
        static std::atomic<uint64_t> next{0};if(!owner_){owner_=++next;++generation_;}
        e.live=true;e.registry=owner_;e.generation=generation_;e.serial=++serial_;e.id=id;e.active=1;
        e.word0C=e.word30=0;e.initial_zero24_mask=7;e.ids.fill(id);
        // Typed node initialization is the Windows pool layout adaptation.
        // CPU-coherent immutable views replace SDK DcacheWritebackInvalidateAll.
        images_[size_t(id)]=std::move(image);tails_[size_t(id)]=std::move(tail);
        std::atomic_thread_fence(std::memory_order_release);e.coherent=true;entries_[size_t(id)]=e;
        GameSourceBankRegistration read;if(!Readback(id,read)||!GameSourceBankRegistrationExact(e,read)){
            entries_[size_t(id)]={};images_[size_t(id)].Reset();tails_[size_t(id)].Reset();
            return {GameFsCode::Unavailable,0,"55144 allocated node readback failed"};
        }
        result=read.id;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
bool GameSourceBankRegistry::Readback(int32_t id,GameSourceBankRegistration& out)const{
    if(id<0||id>=64)return false;const auto& e=entries_[size_t(id)];
    if(!e.live||e.id!=id||e.active!=1||e.registry!=owner_||e.generation!=generation_||!e.coherent||e.word0C||e.word30!=references_[size_t(id)]||e.initial_zero24_mask!=7)return false;
    for(auto value:e.ids)if(value!=id)return false;
    try{if(!BankSame(e.image,images_[size_t(id)].Describe())||images_[size_t(id)].InitializedDigest()!=e.digest)return false;
        if(e.attached&&(!BankSame(e.tail,tails_[size_t(id)].Describe())||e.mode!=1||e.state))return false;
    }catch(const std::exception&){return false;}
    out=e;return true;
}
bool GameSourceBankRegistry::Retain(int32_t id,uint32_t& before,uint32_t& after){
 GameSourceBankRegistration e;if(!Readback(id,e)||e.word0C||e.word30==UINT32_MAX)return false;
 before=e.word30;after=before+1;entries_[size_t(id)].word30=after;references_[size_t(id)]=after;
 GameSourceBankRegistration read;return Readback(id,read)&&read.word30==after; //559E8, private source-owned increment
}
bool GameSourceBankRegistry::Release(int32_t id,uint32_t& before,uint32_t& after){
 GameSourceBankRegistration e;if(!Readback(id,e)||!e.word30)return false;
 before=e.word30;after=before-1;entries_[size_t(id)].word30=after;references_[size_t(id)]=after;
 GameSourceBankRegistration read;return Readback(id,read)&&read.word30==after; //55A7C, actual owning decrement
}
bool GameSourceBankRegistry::BorrowWaveSource(int32_t id,PackageView& image,uintptr_t& slot)const{
 GameSourceBankRegistration e;if(!Readback(id,e))return false;
 image=images_[size_t(id)];slot=reinterpret_cast<uintptr_t>(&entries_[size_t(id)].mode);return true;
}
bool GameSourceBankRegistry::ReadModeSlot(int32_t id,uintptr_t slot,uint32_t& value)const{
 GameSourceBankRegistration e;if(!Readback(id,e)||slot!=reinterpret_cast<uintptr_t>(&entries_[size_t(id)].mode))return false;
 value=entries_[size_t(id)].mode;return true;
}
void GameSourceBankRegistry::Close()noexcept{
    references_.fill(0);
    for(size_t i=0;i<64;++i){entries_[i]={};tails_[i].Reset();images_[i].Reset();}++generation_;
}
bool GameSourceSoundOwnerStable(const GameSourceSoundSnapshot& a,const GameSourceSoundSnapshot& b)noexcept{
    return std::tie(a.live,a.cold_initialized,a.bank_live,a.failed,a.owner,a.generation,a.cold_receipt,a.id_writes,a.bank_bindings,a.bgm_attempts,
        a.constructor,a.last_id_writer,a.se_clean_mask,a.bgm_clean_mask,a.automatic_phase,a.current_id,a.previous_id,a.pending_id,a.automatic)
        ==std::tie(b.live,b.cold_initialized,b.bank_live,b.failed,b.owner,b.generation,b.cold_receipt,b.id_writes,b.bank_bindings,b.bgm_attempts,
        b.constructor,b.last_id_writer,b.se_clean_mask,b.bgm_clean_mask,b.automatic_phase,b.current_id,b.previous_id,b.pending_id,b.automatic)&&BankSame(a.bank,b.bank)&&GameSourceBankRegistrationExact(a.registration,b.registration)
        &&GameSourceVoiceExact(a.voice,b.voice)&&RecordSame(a.se5,b.se5)&&std::tie(a.feedback_attempts,a.feedback_writes,a.feedback_frontier,a.feedback_gain,a.feedback_failed)
        ==std::tie(b.feedback_attempts,b.feedback_writes,b.feedback_frontier,b.feedback_gain,b.feedback_failed);
}
bool GameSourceSoundSnapshotExact(const GameSourceSoundSnapshot& a,const GameSourceSoundSnapshot& b)noexcept{
    return GameSourceSoundOwnerStable(a,b)&&a.queries==b.queries&&a.last_query_gain==b.last_query_gain;
}
bool GameSourceSoundNegativeReady(const GameSourceSoundSnapshot& b)noexcept{
    return b.live&&b.cold_initialized&&b.bank_live&&!b.failed&&b.owner&&b.generation&&b.cold_receipt==1&&b.id_writes==1&&b.bank_bindings==1
        &&b.constructor==0x17F0&&b.last_id_writer==0x18E4&&b.current_id==-1&&b.previous_id==-1&&b.pending_id==-1
        &&b.se_clean_mask==0x1F&&b.bgm_clean_mask==0x1FFFFFF&&!b.automatic&&!b.automatic_phase&&!b.bgm_attempts
        &&b.bank.owner&&b.bank.generation&&b.bank.serial&&b.bank.readable;
}
bool GameSourceSoundGainValid(const GameSourceSoundGainReceipt& r)noexcept{
    const auto& b=r.before;const auto& a=r.after;
    return r.returned&&r.negative_guard&&!r.dispatched&&r.read_pc==0x4AFC8&&r.return_pc==0x4AFE0&&r.id==b.current_id&&r.id<0
        &&GameSourceSoundNegativeReady(b)&&b.queries!=UINT64_MAX
        &&GameSourceSoundOwnerStable(b,a)&&a.queries==b.queries+1&&a.last_query_gain==r.value;
}
bool GameSourceSoundGainExact(const GameSourceSoundGainReceipt& a,const GameSourceSoundGainReceipt& b)noexcept{
    return GameSourceSoundSnapshotExact(a.before,b.before)&&GameSourceSoundSnapshotExact(a.after,b.after)
        &&std::tie(a.read_pc,a.return_pc,a.id,a.value,a.returned,a.negative_guard,a.dispatched)
         ==std::tie(b.read_pc,b.return_pc,b.id,b.value,b.returned,b.negative_guard,b.dispatched);
}
GameFsStatus GameSourceSoundControl::Initialize17F0(){
    if(state_.live)return {GameFsCode::Busy,0,"17F0 sound owner already initialized"};
    static std::atomic<uint64_t> next_owner{0};const auto generation=state_.generation+1;const auto owner=++next_owner;
    const auto port_init=voices_.sequences_.InitializeNativePortComponent(owner,generation);if(!port_init)return port_init;
    const auto child_init=voices_.sequences_.InitializeNativeChildComponent(owner,generation);
    if(!child_init){voices_.sequences_.Close();return child_init;}
    const auto slot_init=voices_.sequences_.InitializeNativeSlotComponent(owner,generation);
    if(!slot_init){voices_.sequences_.Close();return slot_init;}
    state_={};
    state_.live=state_.cold_initialized=true;state_.owner=owner;state_.generation=generation;
    state_.constructor=0x17F0;state_.cold_receipt=state_.id_writes=1;state_.last_id_writer=0x18E4;
    // Explicit source loops:6 SE records,25 BGM records; sourceD2A04=1.0f.
    for(uint32_t i=0;i<31;++i){records_[i]={};records_[i].vtable=i<6?0xDD0A0:0xDD0B0;}
    //18E0/E4/E8/EC/F4, not an inference from zeroed BSS or SGX failure.
    state_.pending_id=state_.current_id=state_.previous_id=-1;state_.automatic=0;state_.automatic_phase=0;
    failure_={};feedback_={};feedback_failure_={};registry_=nullptr;owned_bank_refs_=0;worker_tick_serial_=worker_previous_ns_=worker_current_ns_=0;bank_.Reset();return {};
}
GameFsStatus GameSourceSoundControl::BindSe5(PackageView image){
    if(!state_.live||state_.failed||!state_.cold_initialized||state_.bank_bindings)
        return {GameFsCode::Unavailable,0,"18E8C source SE5 owner unavailable"};
    try{
        const auto d=image.Describe();if(!d.readable||!d.owner||!d.generation||!d.serial||d.size<16||image.ReadLE32(0)!=0x44584753)
            return {GameFsCode::Unavailable,0,"18E8C source SE5 bank lease unavailable"};
        //4A91C/4AA5C SE registration is represented by this owned native bank
        // borrow. No SGX handle/metadata slot is stored into current_id.
        bank_=std::move(image);state_.bank=d;state_.bank_bindings=1;
        // SE5 is no longer a cold record. The actual bank borrow and index are
        // known; the PSP name pointer/SGX bank handle are deliberately unknown.
        records_[5].bank_index=5;records_[5].bank_attached=true;
        records_[5].bank_handle_known=records_[5].name_known=false;return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameSourceSoundSnapshot GameSourceSoundControl::Snapshot()const{
    auto out=state_;out.voice=voices_.Snapshot();out.se_clean_mask=out.bgm_clean_mask=0;
    for(uint32_t i=0;i<31;++i)if(ColdRecord(records_[i],i<6?0xDD0A0:0xDD0B0)){
        if(i<6)out.se_clean_mask|=1u<<i;else out.bgm_clean_mask|=1u<<(i-6);
    }
    out.se5=records_[5];out.bank_live=false;
    if(out.live&&out.bank_bindings)try{out.bank_live=BankSame(bank_.Describe(),out.bank);}catch(const std::exception&){}
    if(registry_){GameSourceBankRegistration entry;if(!registry_->Readback(records_[5].bank_handle,entry)
        ||([&]{auto expected=state_.registration;if(out.voice.live)expected.word30=owned_bank_refs_;return !GameSourceBankRegistrationExact(entry,expected);}()))out.bank_live=false;}
    return out;
}
GameFsStatus GameSourceSoundControl::StoreRegisteredSe5(GameSourceBankRegistry& registry,int32_t id){
    GameSourceBankRegistration entry;
    if(!state_.live||state_.registration.live||!state_.bank_bindings||!registry.Readback(id,entry)||!BankSame(entry.image,state_.bank))
        return {GameFsCode::Unavailable,0,"4AB58 actual allocated SE5 registrar result unavailable"};
    records_[5].bank_handle=id;records_[5].bank_handle_known=true;registry_=&registry;
    registry.entries_[size_t(id)].writer=0x4AB58;
    if(!registry.Readback(id,entry))return {GameFsCode::Unavailable,0,"4AB58 registry post-store readback"};
    state_.registration=entry; // immutable issued registrar result
    // writer is the source context field writer, separate from node metadata.
    if(records_[5].bank_handle!=entry.id)return {GameFsCode::Unavailable,0,"4AB58 full32 bank field readback"};
    return {};
}
GameFsStatus GameSourceSoundControl::Gain4AFBC(const GameSourceSoundSnapshot& expected,int32_t gain,GameSourceSoundGainReceipt& out){
    if(state_.failed)return failure_;
    const auto now=Snapshot();
    if(!GameSourceSoundSnapshotExact(expected,now)||!now.live||!now.bank_live)
        return {GameFsCode::Unavailable,0,"4AFBC current source sound owner/lease changed"};
    GameSourceSoundGainReceipt r;r.before=now;r.after=now;r.read_pc=0x4AFC8;r.return_pc=0x4AFE0;r.id=now.current_id;r.value=gain;
    if(r.id>=0)return {GameFsCode::Unavailable,0,"Unsupported source4B22C current record handle"};
    r.returned=r.negative_guard=true;++r.after.queries;r.after.last_query_gain=gain;
    if(!GameSourceSoundGainValid(r))return {GameFsCode::Unavailable,0,"4AFC8 negative ID producer receipt unavailable"};
    ++state_.queries;state_.last_query_gain=gain;out=r;return {};
}
GameFsStatus GameSourceSoundControl::Feedback4AC38(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){
    const auto now=Snapshot();
    if(!GameSourceSoundSnapshotExact(now,expected)||!now.live||!now.bank_live)
        return {GameFsCode::Unavailable,0,"Options feedback source owner/SE5 lease changed"};
    const auto& record=records_[5];out={now,5,0x4AC84,true,record.bank_attached,record.bank_handle_known,false};
    if(!record.bank_handle_known)return {GameFsCode::Unavailable,0,"Unsupported source SE5 bank handle read4AC84: registered handle producer unknown"};
    return {GameFsCode::Unavailable,0,record.bank_handle<0?"Unsupported feedback branch4AD08":"Unsupported feedback continuation4ACA0"};
}
bool GameSourceFeedbackReachedValid(const GameSourceFeedbackRead& r)noexcept{
    if(r.outcome!=0||!r.attempted||!r.bank_attached||!r.bank_handle_known||!r.read_returned||!r.getter_returned||r.start_returned||r.handle_stored||r.feedback_returned
       ||r.context_index!=5||r.read_pc!=0x4AC84||r.frontier!=0x56598||r.selector||r.gain!=45||r.params_written!=6
       ||r.virtual_target!=0x4AF54||r.wrapper!=0x56930||r.issuer!=0x56598||r.bank_handle<0||r.group||r.event!=6||r.extra||r.mode!=2||r.rate!=1.0f)return false;
    const auto& b=r.owner;const auto& a=r.after;
    if(!GameSourceSoundNegativeReady(b)||!b.registration.live||!b.registration.coherent||!b.registration.attached
       ||b.registration.writer!=0x4AB58||b.registration.id!=r.bank_handle||b.feedback_attempts||b.feedback_writes||b.feedback_failed
       ||r.context_owner!=b.owner||r.context_generation!=b.generation)return false;
    auto expected=b;expected.feedback_attempts=1;expected.feedback_writes=6;expected.feedback_frontier=0x56598;expected.feedback_gain=45;expected.feedback_failed=true;
    expected.se5.context_owner=b.owner;expected.se5.context_generation=b.generation;
    expected.se5.gain=45;expected.se5.rate=1.0f;expected.se5.request=6;expected.se5.position=0;expected.se5.resource_state=0;
    return GameSourceSoundSnapshotExact(expected,a);
}
bool GameSourceFeedbackExact(const GameSourceFeedbackRead& a,const GameSourceFeedbackRead& b)noexcept{
    return GameSourceSoundSnapshotExact(a.owner,b.owner)&&GameSourceSoundSnapshotExact(a.after,b.after)
      &&std::tie(a.context_index,a.read_pc,a.attempted,a.bank_attached,a.bank_handle_known,a.read_returned,a.frontier,a.selector,a.params_written,a.virtual_target,a.wrapper,a.issuer,a.bank_handle,a.gain,a.group,a.event,a.extra,a.mode,a.rate,a.context_owner,a.context_generation,a.outcome,a.getter_returned,a.start_returned,a.handle_stored,a.feedback_returned)
      ==std::tie(b.context_index,b.read_pc,b.attempted,b.bank_attached,b.bank_handle_known,b.read_returned,b.frontier,b.selector,b.params_written,b.virtual_target,b.wrapper,b.issuer,b.bank_handle,b.gain,b.group,b.event,b.extra,b.mode,b.rate,b.context_owner,b.context_generation,b.outcome,b.getter_returned,b.start_returned,b.handle_stored,b.feedback_returned);
}
GameFsStatus GameSourceSoundControl::Feedback4AC38(const GameSourceSoundSnapshot& expected,uint32_t root14,GameSourceFeedbackRead& out){
    if(state_.feedback_failed){
        out={};const auto current=Snapshot();
        if(!current.live||!current.bank_live||!GameSourceSoundSnapshotExact(current,feedback_.after)
            ||(!GameSourceSoundSnapshotExact(expected,feedback_.owner)&&!GameSourceSoundSnapshotExact(expected,current)))
            return {GameFsCode::Unavailable,0,"4AC38 retired/changed feedback owner"};
        out=feedback_;return feedback_failure_;
    }
    out={};const auto now=Snapshot();
    if(!GameSourceSoundSnapshotExact(now,expected)||!now.bank_live||!now.registration.live||root14!=2)
        return {GameFsCode::Unavailable,0,"4AC38 actual SE5 registration/root14 selected domain unavailable"};
    GameSourceBankRegistration entry;if(!registry_||!registry_->Readback(records_[5].bank_handle,entry)||!GameSourceBankRegistrationExact(entry,now.registration))
        return {GameFsCode::Unavailable,0,"4AC84 current allocated bank readback changed"};
    GameSourceFeedbackRead r;r.owner=now;r.context_index=5;r.attempted=r.bank_attached=r.bank_handle_known=r.read_returned=true;
    r.bank_handle=records_[5].bank_handle;
    if(r.bank_handle<0)return {GameFsCode::Unavailable,0,"4AC38 positive registered bank required"};
    //14834(0): actual root+14=2 -> exact retained table index2 ->45.
    r.getter_returned=true;r.selector=0;r.gain=45;r.rate=1.0f;r.group=0;r.event=6;r.extra=0;
    auto& record=records_[5];record.context_owner=state_.owner;record.context_generation=state_.generation;
    record.gain=45;record.rate=1.0f;record.request=6;record.position=0;record.resource_state=0;
    r.context_owner=state_.owner;r.context_generation=state_.generation;r.params_written=6;
    // Actual portable wrapper calls; only the unimplemented issuer refuses.
    const auto status=Start4AF54(r);
    state_.feedback_attempts=1;state_.feedback_writes=6;state_.feedback_frontier=r.frontier;state_.feedback_gain=45;state_.feedback_failed=!bool(status);
    r.after=Snapshot();feedback_=r;out=r;feedback_failure_=status;return status;
}
GameFsStatus GameSourceSoundControl::Start4AF54(GameSourceFeedbackRead& r){
    r.virtual_target=0x4AF54;
    const auto status=Start56930(r,records_[5].bank_handle,int32_t(records_[5].position),int32_t(records_[5].request),int32_t(records_[5].resource_state));
    if(!status)return status;
    const auto voice=voices_.Snapshot();records_[5].handle=voice.id;r.start_returned=r.handle_stored=true; //4AF7C
    if(!voices_.Gain(voice.id,r.gain,r.gain)||!voices_.Rate(voice.id,r.rate))return {GameFsCode::Unavailable,0,"4AC38 actual voice gain/rate failed"};
    r.feedback_returned=true;r.outcome=1;r.frontier=0x4AD4C;return {};
}
GameFsStatus GameSourceSoundControl::Start56930(GameSourceFeedbackRead& r,int32_t bank,int32_t group,int32_t event,int32_t extra){
    r.wrapper=0x56930;
    return Issue56598(r,2,bank,group,event,extra,0);
}
GameFsStatus GameSourceSoundControl::Issue56598(GameSourceFeedbackRead& r,int32_t mode,int32_t bank,int32_t group,int32_t event,int32_t extra,int32_t last){
    r.issuer=r.frontier=0x56598;r.mode=mode;
    if(mode!=2||bank!=r.bank_handle||group||event!=6||extra||last)
        return {GameFsCode::Unavailable,0,"56598 selected wrapper argument mismatch"};
    if(voice_admission_){
        GameSourceBankRegistration entry;uint32_t before=0,after=0;
        if(!registry_||!registry_->Retain(bank,before,after)||!registry_->Readback(bank,entry))return {GameFsCode::Unavailable,0,"559E8 owning bank retain refused"};
        owned_bank_refs_=after;
        //Actual selected SEQD read, not saved metadata as a live producer.
        const auto auxiliary=[&]()->GameFsStatus{try{const auto image=bank_;const auto payload=entry.chunks[2];
            if(!payload||image.ReadLE32(payload+4)<=uint32_t(group))return {GameFsCode::Unavailable,0,"558E4 current SEQD group"};
            const auto g=image.ReadLE32(payload+8+4*uint32_t(group));
            if(image.ReadLE32(g+4)<=uint32_t(event))return {GameFsCode::Unavailable,0,"558E4 current SEQD event"};
            const auto record=image.ReadLE32(g+8+4*uint32_t(event));
            if(!record||image.ReadLE32(record+4)!=0)return {GameFsCode::Unavailable,0,"55BA0 unsupported nonnull auxiliary"};
        }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}return {};};
        return voices_.Allocate(bank,entry.ids,before,after,auxiliary);
    }
    // HOST Unavailable has no source v0. In particular no4AF7C playback handle
    // write or post-start gain/rate dispatch is performed without a real voice.
    return {GameFsCode::Unavailable,0,"Unsupported source voice issuer56598(mode2, registered bank, group0, event6, 0, 0)"};
}

namespace {
bool VoiceEnvelope(const GameSourceFeedbackRead& r)noexcept{
 return r.start_returned&&r.handle_stored&&r.feedback_returned&&r.issuer==0x56598&&r.virtual_target==0x4AF54&&r.wrapper==0x56930
    &&r.attempted&&r.read_returned&&r.getter_returned&&r.bank_handle_known&&r.bank_attached&&r.read_pc==0x4AC84&&r.context_index==5&&!r.selector&&r.params_written==6
    &&r.mode==2&&!r.group&&r.event==6&&!r.extra&&r.gain==45&&r.rate==1.0f&&r.bank_handle==r.owner.registration.id&&r.bank_handle==r.owner.se5.bank_handle
    &&r.context_owner==r.owner.owner&&r.context_generation==r.owner.generation;
}
bool ReturnedVoice(const GameSourceVoiceSnapshot& v,int32_t bank)noexcept{
 if(!v.owner||!v.generation||v.id<0||v.id>=16)return false;
 GameSourceVoiceSnapshot expected;expected.live=expected.active=expected.returned=expected.auxiliary_null=true;
 expected.owner=v.owner;expected.generation=v.generation;expected.allocation=1;expected.id=v.id;expected.bank=bank;expected.bank_ids.fill(bank);
 expected.retain_writer=0x55A2C;expected.initial_left=expected.initial_right=4096;expected.initial_dirty_mask=0xC;expected.initial_port_writes=2;
 expected.flags=3;expected.mode=2;expected.event=6;expected.descriptor=0x7210;expected.bank_refs_after=1;expected.initialized_mask=0x7F;expected.port_writes=5;expected.return_pc=0x567BC;
 expected.working[0]=expected.working[1]=0x20C;expected.working[2]=expected.working[3]=1440;expected.working[9]=4096;expected.dirty=expected.persistent=1;
 return GameSourceVoiceExact(v,expected);
}
}
bool GameSourceVoiceFeedbackValid(const GameSourceFeedbackRead& r)noexcept{
 if(!VoiceEnvelope(r)||r.outcome!=1||!r.start_returned||!r.handle_stored||!r.feedback_returned||r.frontier!=0x4AD4C||r.issuer!=0x56598||r.virtual_target!=0x4AF54||r.wrapper!=0x56930
    ||!r.attempted||!r.read_returned||!r.getter_returned||!r.bank_handle_known||!r.bank_attached||r.read_pc!=0x4AC84||r.context_index!=5||r.selector||r.params_written!=6||r.mode!=2||r.group||r.event!=6||r.extra||r.gain!=45||r.rate!=1.0f)return false;
 const auto& v=r.after.voice;
 if(!ReturnedVoice(v,r.bank_handle))return false;
 if(!v.live||!v.active||!v.returned||!v.auxiliary_null||!v.owner||!v.generation||v.allocation!=1||v.id<0||v.id>=16||v.bank!=r.bank_handle||v.services||v.callback_entered||v.sequence_slot
    ||v.bank_refs_before||v.bank_refs_after!=1||v.flags!=3||v.mode!=2||v.group||v.event!=6||v.extra||v.descriptor!=0x7210||v.initialized_mask!=0x7F||v.port_writes!=5
    ||v.return_pc!=0x567BC||v.working[0]!=0x20C||v.working[1]!=0x20C||v.working[2]!=1440||v.working[3]!=1440||v.working[9]!=4096||v.dirty!=1||v.persistent!=1)return false;
 for(auto id:v.bank_ids)if(id!=r.bank_handle)return false;
 auto expected=r.owner;expected.voice=v;expected.feedback_attempts=1;expected.feedback_writes=6;expected.feedback_frontier=r.frontier;expected.feedback_gain=45;expected.feedback_failed=false;
 expected.se5.context_owner=r.owner.owner;expected.se5.context_generation=r.owner.generation;expected.se5.gain=45;expected.se5.rate=1;expected.se5.request=6;expected.se5.position=0;expected.se5.resource_state=0;expected.se5.handle=v.id;
 expected.se_clean_mask&=~(1u<<5);
 return !r.owner.voice.live&&r.owner.registration.live&&r.owner.registration.word30==0&&r.owner.feedback_attempts==0
    &&r.context_owner==r.owner.owner&&r.context_generation==r.owner.generation&&r.after.bank_live&&GameSourceSoundSnapshotExact(expected,r.after);
}
bool GameSourceVoiceServiceValid(const GameSourceFeedbackRead& r)noexcept{
 if(!VoiceEnvelope(r)||!ReturnedVoice(r.owner.voice,r.bank_handle)||r.outcome!=2||r.frontier!=0x57CF0||!r.start_returned||!r.feedback_returned||!r.handle_stored)return false;
 const auto& v=r.after.voice;
 //Service owner is the complete post-query current sound snapshot; fields
 //from the original feedback remain passive, not authority for currentness.
 if(!r.owner.voice.live||!r.owner.voice.returned||r.owner.voice.services||v.services!=1||!v.callback_entered||v.callback_returned||v.sequence_result_known
    ||v.callback_pc!=0x578CC||v.frontier!=0x57CF0||v.initializer_store_pc!=0x578D4||v.sequence_slot!=-1)return false;
 auto expected=r.owner;auto ev=expected.voice;
 ev.services=1;ev.callback_entered=true;ev.callback_pc=0x578CC;ev.frontier=0x57CF0;ev.initializer_store_pc=0x578D4;ev.sequence_slot=-1;ev.words80[0]=UINT32_MAX;
 ev.published=ev.working;ev.consumer=ev.working;ev.published[0]=0;ev.working[0]=0;ev.published_dirty=ev.consumer_dirty=ev.dirty;ev.published_persistent=ev.consumer_persistent=ev.persistent;ev.dirty=ev.published_dirty=0;
 expected.voice=ev;return GameSourceSoundSnapshotExact(expected,r.after);
}
GameFsStatus GameSourceSoundControl::VoiceFeedback4AC38(const GameSourceSoundSnapshot& expected,uint32_t root14,GameSourceFeedbackRead& out){
 if(state_.feedback_failed)return Feedback4AC38(expected,root14,out); //first failure/current lease, no reissue
 if(voices_.Snapshot().live||state_.feedback_attempts){out={};return {GameFsCode::Unavailable,0,"NativeVoice feedback duplicate"};}
 voice_admission_=true;const auto status=Feedback4AC38(expected,root14,out);voice_admission_=false;
 if(status&&!GameSourceVoiceFeedbackValid(out))return {GameFsCode::Unavailable,0,"NativeVoice returned outcome readback"};return status;
}
GameFsStatus GameSourceSoundControl::ServiceVoice(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){
 out={};const auto current=Snapshot();if(!GameSourceSoundSnapshotExact(current,expected)||!current.live||!current.bank_live||!voices_.Snapshot().returned||voices_.Snapshot().services)
 return {GameFsCode::Unavailable,0,"NativeVoice current service owner unavailable"};
 auto r=feedback_;r.owner=current;r.outcome=2;const auto status=voices_.Service();r.frontier=voices_.Snapshot().frontier;r.after=Snapshot();out=r;return status;
}
bool GameSourceSequencePrefixValid(const GameSourceFeedbackRead& r)noexcept{
 if(r.outcome!=3||r.frontier!=0x5A5AC)return false;
 //Preserve the historical failed57CF0 validator unchanged. The same real
 //port publication precedes this disjoint returned-context continuation.
 auto legacy=r;legacy.outcome=2;legacy.frontier=0x57CF0;
 legacy.after.voice.sequence={};legacy.after.voice.sequence_result_known=false;legacy.after.voice.sequence_slot=-1;
 legacy.after.voice.words80[0]=UINT32_MAX;legacy.after.voice.frontier=0x57CF0;
 if(!GameSourceVoiceServiceValid(legacy))return false;
 const auto& v=r.after.voice;const auto& q=v.sequence;
 if(v.callback_returned||!v.sequence_result_known||v.frontier!=r.frontier||v.sequence_slot!=q.id||v.words80[0]!=uint32_t(q.id)
    ||!q.owner||!q.generation||q.id<0||q.id>=16||!q.ids_storage||!q.output_storage||q.ids_storage==q.output_storage)return false;
 GameSourceSequenceSnapshot e;e.live=e.active=e.allocation_returned=e.lookup_returned=e.clear_returned=e.command_entered=true;
 e.bank_retained=e.record_lookup_returned=e.track_reserved=e.port_attempted=true;
 e.owner=q.owner;e.generation=q.generation;e.allocation=1;e.voice_owner=v.owner;e.voice_generation=v.generation;
 e.ids_storage=q.ids_storage;e.output_storage=q.output_storage;e.id=q.id;e.bank=r.bank_handle;e.sentinel_id=-1;e.track_id=0;e.flags=1;
 e.allocation_pc=0x57CF0;e.return_pc=0x57D80;e.result_store_pc=0x57908;e.lookup_pc=0x57A90;e.clear_pc=0x58540;
 e.event=6;e.command_pc=0x58768;e.retain_writer=0x55A2C;e.refs_before=1;e.refs_after=2;
 e.record_offset=0xB28;e.record_divisor=1000;e.record_bytes=23;e.port_call_pc=0x582D8;e.frontier=0x5A5AC;
 e.initial_factor=1000.0f;e.selected_factor=1000.0f;e.sentinel_self=e.child_head_same=e.output_unchanged=true;e.bank_ids.fill(r.bank_handle);
 return GameSourceSequenceExact(q,e)&&v.wordsA0==q.output_after;
}
GameFsStatus GameSourceSoundControl::AdmitSequence58C04(bool advance_port){
 auto& pool=voices_.sequences_;auto& q=pool.state_;const auto voice=voices_.Snapshot();
 //Actual command args[0..4]={0,0,bank,group,event}; lookup precedes command.
 if(!voice.sequence_result_known||voice.sequence_slot!=q.id||!pool.Lookup(voice.sequence_slot))return {GameFsCode::Unavailable,0,"58C04 current context lookup57A90 failed"};
 q.command_entered=true;q.command_pc=0x58768;q.command=q.subindex=q.group=0;q.event=6;q.bank=voice.bank;
 if(!pool.Clear58540())return {GameFsCode::Unavailable,0,"58540 current context embedded sentinel unavailable"};
 uint32_t before=0,after=0;GameSourceBankRegistration entry;
 if(!registry_||!registry_->Retain(q.bank,before,after)||!registry_->Readback(q.bank,entry))return {GameFsCode::Unavailable,0,"587A4 actual command bank retain559E8"};
 owned_bank_refs_=after;q.bank_retained=true;q.retain_writer=0x55A2C;q.refs_before=before;q.refs_after=after;
 //No55A4C release is executed until580D4 genuinely returns.
 try{const auto payload=entry.chunks[2];if(!payload||bank_.ReadLE32(payload+4)<=q.group)return {GameFsCode::Unavailable,0,"558E4 command group bounds"};
 const auto group=bank_.ReadLE32(payload+8+4*q.group);if(!group||bank_.ReadLE32(group+4)<=q.event)return {GameFsCode::Unavailable,0,"558E4 command event bounds"};
 const auto record=bank_.ReadLE32(group+8+4*q.event);if(!record)return {GameFsCode::Unavailable,0,"558E4 command record absent"};
 const auto kind=bank_.ReadLE16(record+8),divisor=bank_.ReadLE16(record+10);const auto bytes=bank_.ReadLE32(record+16);
 q.record_lookup_returned=true;
 const auto status=pool.Start580D4(record,kind,divisor,bytes,advance_port,[&]{
  uint32_t b=0,a=0;GameSourceBankRegistration read;
  if(!registry_->Retain(q.bank,b,a)||!registry_->Readback(q.bank,read)||b!=2||a!=3||read.word30!=a)
   return GameFsStatus{GameFsCode::Unavailable,0,"58218 actual track retain/readback"};
  owned_bank_refs_=a;q.current_refs=a;return GameFsStatus{};
 });
 if(!status)return status; //no fabricated source result permits command release
 uint32_t b=0,a=0;GameSourceBankRegistration read;
 if(!registry_->Release(q.bank,b,a)||!registry_->Readback(q.bank,read)||b!=3||a!=2||read.word30!=a)
  return {GameFsCode::Unavailable,0,"5880C actual command release55A4C/readback"};
 owned_bank_refs_=a;q.current_refs=a;q.command_released=true;q.release_pc=0x55A7C;q.release_before=b;q.release_after=a;
 q.handler_returned=q.admission_returned=true;return GameFsStatus{};
 }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameSourceSoundControl::ServiceSequence(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){
 out={};const auto current=Snapshot();if(!GameSourceSoundSnapshotExact(current,expected)||!current.live||!current.bank_live||!current.voice.returned||current.voice.services)
 return {GameFsCode::Unavailable,0,"NativeSequence current service owner unavailable"};
 auto r=feedback_;r.owner=current;r.outcome=3;
 const auto status=voices_.Service(true,[&]{return AdmitSequence58C04();});r.frontier=voices_.Snapshot().frontier;r.after=Snapshot();out=r;return status;
}
void GameSourceSoundControl::ObserveWorkerTick()noexcept{
 const auto now=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
 worker_previous_ns_=worker_current_ns_;worker_current_ns_=now;++worker_tick_serial_;
}
GameFsStatus GameSourceSoundControl::ServicePort(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){
 out={};const auto current=Snapshot();
 if(!GameSourceSoundSnapshotExact(current,expected)||!current.live||!current.bank_live||!current.voice.returned||current.voice.services)
  return {GameFsCode::Unavailable,0,"NativePort current service owner unavailable"};
 const auto tick_serial=worker_tick_serial_,previous_ns=worker_previous_ns_,current_ns=worker_current_ns_; //freeze f12 provenance before562D4 callback
 auto r=feedback_;r.owner=current;r.outcome=4;
 auto status=voices_.Service(true,[&]{return AdmitSequence58C04(true);});
 if(status){auto& q=voices_.sequences_.state_;q.worker_tick_serial=tick_serial;q.worker_previous_ns=previous_ns;q.worker_current_ns=current_ns;
  if(tick_serial<2||!previous_ns||current_ns<previous_ns){q.frontier=0x5A06C;status={GameFsCode::Unavailable,0,"5A06C missing actual consecutive worker timestamps"};}
  else status=voices_.sequences_.Update5A06C(current_ns-previous_ns);
  voices_.state_.frontier=q.frontier;
 }
 r.frontier=voices_.Snapshot().frontier;r.after=Snapshot();out=r;return status;
}
GameFsStatus GameSourceSoundControl::ServiceProcessor(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){
 out={};const auto current=Snapshot();
 if(!GameSourceSoundSnapshotExact(current,expected)||!current.live||!current.bank_live||!current.voice.returned||current.voice.services)
  return {GameFsCode::Unavailable,0,"NativePort current service owner unavailable"};
 const auto tick_serial=worker_tick_serial_,previous_ns=worker_previous_ns_,current_ns=worker_current_ns_; //freeze f12 provenance before562D4 callback
 auto r=feedback_;r.owner=current;r.outcome=5;
 auto status=voices_.Service(true,[&]{return AdmitSequence58C04(true);});
 if(status){auto& q=voices_.sequences_.state_;q.worker_tick_serial=tick_serial;q.worker_previous_ns=previous_ns;q.worker_current_ns=current_ns;
  if(tick_serial<2||!previous_ns||current_ns<previous_ns){q.frontier=0x5A06C;status={GameFsCode::Unavailable,0,"5A06C missing actual consecutive worker timestamps"};}
  else status=voices_.sequences_.Update5A06C(current_ns-previous_ns,[&](uint32_t at){return bank_.ReadU8(at);});
  voices_.state_.frontier=q.frontier;
 }
 r.frontier=voices_.Snapshot().frontier;r.after=Snapshot();out=r;return status;
}
GameFsStatus GameSourceSoundControl::ServiceControl(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){
 out={};const auto current=Snapshot();
 if(!GameSourceSoundSnapshotExact(current,expected)||!current.live||!current.bank_live||!current.voice.returned||current.voice.services)
  return {GameFsCode::Unavailable,0,"NativePort current service owner unavailable"};
 const auto tick_serial=worker_tick_serial_,previous_ns=worker_previous_ns_,current_ns=worker_current_ns_; //freeze f12 provenance before562D4 callback
 auto r=feedback_;r.owner=current;r.outcome=6;
 auto status=voices_.Service(true,[&]{return AdmitSequence58C04(true);});
 if(status){auto& q=voices_.sequences_.state_;q.worker_tick_serial=tick_serial;q.worker_previous_ns=previous_ns;q.worker_current_ns=current_ns;
  if(tick_serial<2||!previous_ns||current_ns<previous_ns){q.frontier=0x5A06C;status={GameFsCode::Unavailable,0,"5A06C missing actual consecutive worker timestamps"};}
  else status=voices_.sequences_.Update5A06C(current_ns-previous_ns,[&](uint32_t at){return bank_.ReadU8(at);},[&]{return DispatchControl5B9E4();});
  voices_.state_.frontier=q.frontier;
 }
 r.frontier=voices_.Snapshot().frontier;r.after=Snapshot();out=r;return status;
}
GameFsStatus GameSourceSoundControl::ServiceWave(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){
 out={};const auto current=Snapshot();
 if(!GameSourceSoundSnapshotExact(current,expected)||!current.live||!current.bank_live||!current.voice.returned||current.voice.services)
  return {GameFsCode::Unavailable,0,"NativePort current service owner unavailable"};
 const auto tick_serial=worker_tick_serial_,previous_ns=worker_previous_ns_,current_ns=worker_current_ns_; //freeze f12 provenance before562D4 callback
 auto r=feedback_;r.owner=current;r.outcome=7;
 auto status=voices_.Service(true,[&]{return AdmitSequence58C04(true);});
 if(status){auto& q=voices_.sequences_.state_;q.worker_tick_serial=tick_serial;q.worker_previous_ns=previous_ns;q.worker_current_ns=current_ns;
  if(tick_serial<2||!previous_ns||current_ns<previous_ns){q.frontier=0x5A06C;status={GameFsCode::Unavailable,0,"5A06C missing actual consecutive worker timestamps"};}
  else status=voices_.sequences_.Update5A06C(current_ns-previous_ns,[&](uint32_t at){return bank_.ReadU8(at);},[&]{return DispatchControl5B9E4(true);});
  voices_.state_.frontier=q.frontier;
 }
 r.frontier=voices_.Snapshot().frontier;r.after=Snapshot();out=r;return status;
}
GameFsStatus GameSourceSoundControl::ServicePort24(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){
 out={};const auto current=Snapshot();
 if(!GameSourceSoundSnapshotExact(current,expected)||!current.live||!current.bank_live||!current.voice.returned||current.voice.services)
  return {GameFsCode::Unavailable,0,"NativePort current service owner unavailable"};
 const auto tick_serial=worker_tick_serial_,previous_ns=worker_previous_ns_,current_ns=worker_current_ns_; //freeze f12 provenance before562D4 callback
 auto r=feedback_;r.owner=current;r.outcome=8;
 auto status=voices_.Service(true,[&]{return AdmitSequence58C04(true);});
 if(status){auto& q=voices_.sequences_.state_;q.worker_tick_serial=tick_serial;q.worker_previous_ns=previous_ns;q.worker_current_ns=current_ns;
  if(tick_serial<2||!previous_ns||current_ns<previous_ns){q.frontier=0x5A06C;status={GameFsCode::Unavailable,0,"5A06C missing actual consecutive worker timestamps"};}
  else status=voices_.sequences_.Update5A06C(current_ns-previous_ns,[&](uint32_t at){return bank_.ReadU8(at);},[&]{return DispatchControl5B9E4(true,true);});
  voices_.state_.frontier=q.frontier;
 }
 r.frontier=voices_.Snapshot().frontier;r.after=Snapshot();out=r;return status;
}
GameFsStatus GameSourceSoundControl::ServiceChild(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){
 GameOptionsChildCallScope scope;scope.active_=scope.component_=true;scope.source_=this;scope.expected_=expected;
 return ServiceChildScoped(expected,out,scope);
}
GameFsStatus GameSourceSoundControl::ServiceChildScoped(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out,GameOptionsChildCallScope& scope){
 out={};const auto current=Snapshot();
 if(!GameSourceSoundSnapshotExact(current,expected)||!current.live||!current.bank_live||!current.voice.returned||current.voice.services)
  return {GameFsCode::Unavailable,0,"NativePort current service owner unavailable"};
 if(scope.slot_||active_child_scope_||!scope.active_||scope.sealed_||!GameSourceSoundSnapshotExact(scope.expected_,current)
    ||(!scope.component_&&(!scope.actor_prepared_||!scope.gui_||!scope.serial_||!scope.gui_generation_))
    ||(scope.component_&&scope.source_!=this))return {GameFsCode::Unavailable,0,"Options child private synchronous callscope unavailable"};
 if(scope.component_)scope.serial_=++child_component_serial_;
 scope.source_=this;active_child_scope_=&scope;
 struct EndScope {GameOptionsChildCallScope*& current;~EndScope(){current=nullptr;}} end{active_child_scope_};
 const auto tick_serial=worker_tick_serial_,previous_ns=worker_previous_ns_,current_ns=worker_current_ns_; //freeze f12 provenance before562D4 callback
 auto r=feedback_;r.owner=current;r.outcome=9;
 auto status=voices_.Service(true,[&]{return AdmitSequence58C04(true);});
 if(status){auto& q=voices_.sequences_.state_;q.worker_tick_serial=tick_serial;q.worker_previous_ns=previous_ns;q.worker_current_ns=current_ns;
  if(tick_serial<2||!previous_ns||current_ns<previous_ns){q.frontier=0x5A06C;status={GameFsCode::Unavailable,0,"5A06C missing actual consecutive worker timestamps"};}
  else status=voices_.sequences_.Update5A06C(current_ns-previous_ns,[&](uint32_t at){return bank_.ReadU8(at);},[&]{return DispatchControl5B9E4(true,true,true);});
  voices_.state_.frontier=q.frontier;
 }
 r.frontier=voices_.Snapshot().frontier;r.after=Snapshot();out=r;
 if(GameSourceChildPrefixValid(r)&&GameSourceSoundSnapshotExact(r.after,Snapshot())){scope.issued_=r;scope.sealed_=true;}
 return status;
}
GameFsStatus GameSourceSoundControl::ServiceSlot(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){
 GameOptionsChildCallScope scope;scope.active_=scope.component_=scope.slot_=true;scope.source_=this;scope.expected_=expected;
 return ServiceSlotScoped(expected,out,scope);
}
GameFsStatus GameSourceSoundControl::ServiceSlotScoped(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out,GameOptionsChildCallScope& scope){
 out={};const auto current=Snapshot();
 if(!GameSourceSoundSnapshotExact(current,expected)||!current.live||!current.bank_live||!current.voice.returned||current.voice.services)
  return {GameFsCode::Unavailable,0,"NativePort current service owner unavailable"};
 if(!scope.slot_||active_child_scope_||!scope.active_||scope.sealed_||!GameSourceSoundSnapshotExact(scope.expected_,current)
    ||(!scope.component_&&(!scope.actor_prepared_||!scope.gui_||!scope.serial_||!scope.gui_generation_))
    ||(scope.component_&&scope.source_!=this))return {GameFsCode::Unavailable,0,"Options child private synchronous callscope unavailable"};
 if(scope.component_)scope.serial_=++child_component_serial_;
 scope.source_=this;active_child_scope_=&scope;
 struct EndScope {GameOptionsChildCallScope*& current;~EndScope(){current=nullptr;}} end{active_child_scope_};
 const auto tick_serial=worker_tick_serial_,previous_ns=worker_previous_ns_,current_ns=worker_current_ns_; //freeze f12 provenance before562D4 callback
 auto r=feedback_;r.owner=current;r.outcome=10;
 auto status=voices_.Service(true,[&]{return AdmitSequence58C04(true);});
 if(status){auto& q=voices_.sequences_.state_;q.worker_tick_serial=tick_serial;q.worker_previous_ns=previous_ns;q.worker_current_ns=current_ns;
  if(tick_serial<2||!previous_ns||current_ns<previous_ns){q.frontier=0x5A06C;status={GameFsCode::Unavailable,0,"5A06C missing actual consecutive worker timestamps"};}
  else status=voices_.sequences_.Update5A06C(current_ns-previous_ns,[&](uint32_t at){return bank_.ReadU8(at);},[&]{return DispatchControl5B9E4(true,true,true);});
  voices_.state_.frontier=q.frontier;
 }
 r.frontier=voices_.Snapshot().frontier;r.after=Snapshot();out=r;
 if(GameSourceSlotPrefixValid(r)&&GameSourceSoundSnapshotExact(r.after,Snapshot())){scope.issued_=r;scope.sealed_=true;}
 return status;
}
bool GameSourcePortPrefixValid(const GameSourceFeedbackRead& r)noexcept{
 if(r.outcome!=4||r.frontier!=0x59DD4||r.after.voice.frontier!=r.frontier||!r.after.voice.callback_returned)return false;
 auto legacy=r;legacy.outcome=3;legacy.frontier=0x5A5AC;
 auto& v=legacy.after.voice;auto& q=v.sequence;
 v.callback_returned=false;v.frontier=0x5A5AC;
 q.port={};q.track={};q.admission_returned=q.command_released=q.update_entered=q.processor_attempted=false;
 q.current_refs=q.release_pc=q.release_before=q.release_after=q.update_pc=q.processor_pc=0;
 q.native_elapsed_ns=q.worker_tick_serial=q.worker_previous_ns=q.worker_current_ns=0;q.native_delta_seconds=0;q.update_quantum=q.processor_quantum=0;q.processor_context=0;
 q.track_source_initialized=q.track_activated=q.port_result_known=q.handler_returned=false;q.port_id=-1;
 q.flags=1;q.sentinel_self=q.child_head_same=true;q.frontier=0x5A5AC;
 if(!GameSourceSequencePrefixValid(legacy))return false;
 const auto& actual=r.after.voice.sequence;const auto& p=actual.port;const auto& t=actual.track;
 if(!p.owner||!p.generation||!p.storage||p.id<0||p.id>=16||!t.storage||!t.context_storage||p.storage==t.storage||p.storage==t.context_storage||t.storage==t.context_storage
  ||actual.worker_tick_serial<2||!actual.worker_previous_ns||actual.worker_current_ns<actual.worker_previous_ns)return false;
 const auto elapsed=actual.worker_current_ns-actual.worker_previous_ns;
 const float delta=float(double(elapsed)/1000000000.0),scaled=delta*1000000.0f;
 if(!std::isfinite(scaled)||scaled<0||double(scaled)>double(INT32_MAX))return false;
 auto e=q;e.flags=3;e.sentinel_self=e.child_head_same=false;e.port_result_known=e.track_source_initialized=e.track_activated=e.handler_returned=true;e.port_id=p.id;
 e.admission_returned=e.command_released=e.update_entered=e.processor_attempted=true;e.current_refs=2;e.release_pc=0x55A7C;e.release_before=3;e.release_after=2;e.update_pc=0x54FE8;e.processor_pc=0x5A120;
 e.native_elapsed_ns=elapsed;e.worker_tick_serial=actual.worker_tick_serial;e.worker_previous_ns=actual.worker_previous_ns;e.worker_current_ns=actual.worker_current_ns;e.native_delta_seconds=delta;e.update_quantum=int32_t(scaled);e.processor_quantum=e.update_quantum;e.processor_context=t.context_storage;e.frontier=0x59DD4;
 auto& ep=e.port;ep.live=ep.active=ep.returned=ep.free_unlinked=ep.active_linked=true;ep.owner=p.owner;ep.generation=e.generation;ep.allocation=1;ep.storage=p.storage;ep.id=p.id;ep.flags=1;ep.initialize_pc=0x5A490;ep.return_pc=0x5A660;
 ep.sentinel_ids={-1,-1};ep.sentinel_self={true,true};
 ep.known_halfwords=3;for(uint32_t i=12;i<36;++i)ep.known_halfwords|=uint64_t(1)<<i;
 for(uint32_t k=0;k<4;++k){ep.parameters[16+6*k]=4096;ep.parameters[17+6*k]=1000;}
 auto& et=e.track;et.live=et.initialized=et.free_unlinked=et.child_linked=et.lane_initialized=et.returned=true;
 et.storage=t.storage;et.context_storage=t.context_storage;et.allocation_index=0;et.subindex=0;et.bank=r.bank_handle;et.record_offset=0xB28;et.body_offset=0xB3C;et.end_offset=0xB53;et.cursor_offset=0xB3C;et.wordCC=UINT32_MAX;et.allocated_ports=et.lanes=1;
 et.retain_pc=0x55A2C;et.refs_before=2;et.refs_after=3;et.return_pc=0x58290;et.factor=1000;et.ports.fill(-1);et.ports[0]=int16_t(p.id);et.lane_controls={127,64,0,0,0,127,64};
 return GameSourceSequenceExact(actual,e);
}
bool GameSourceProcessorPrefixValid(const GameSourceFeedbackRead& r)noexcept{
 const auto& q=r.after.voice.sequence;const auto& p=q.processor;
 if(r.outcome!=5||r.frontier!=0x5B9E4||r.after.voice.frontier!=r.frontier||q.frontier!=r.frontier||!r.after.bank_live)return false;
 auto legacy=r;legacy.outcome=4;legacy.frontier=0x59DD4;legacy.after.voice.frontier=0x59DD4;legacy.after.voice.sequence.frontier=0x59DD4;
 legacy.after.voice.sequence.processor={};legacy.after.voice.sequence.track.cursor_offset=0xB3C;
 if(!GameSourcePortPrefixValid(legacy))return false;
 if(!p.command_storage||p.command_storage==q.ids_storage||p.command_storage==q.output_storage||p.command_storage==q.track.storage||p.command_storage==q.port.storage||p.command_storage==q.track.context_storage)return false;
 GameSourceProcessorSnapshot e;e.entered=e.sentinel_terminated=e.controls_read=e.kind_dispatched=e.opcode_read=e.operands_returned=e.control_attempted=true;
 e.context_storage=q.track.context_storage;e.track_storage=q.track.storage;e.command_storage=p.command_storage;e.quantum=q.update_quantum;
 e.sentinel_pc=0x5A05C;e.dispatch_pc=0x59F50;e.kind_target=0x59168;e.opcode_pc=0x5923C;e.opcode=0xD0;e.cursor_before=0xB3C;e.cursor_after=0xB45;e.body_end=0xB53;
 e.operand_count=4;e.control_pc=0x5903C;e.frontier=0x5B9E4;e.port_id=q.port.id;
 e.operands={64,0,64,4096};e.operand_begin={0xB3D,0xB3F,0xB40,0xB42};e.operand_end={0xB3F,0xB40,0xB42,0xB45};e.operand_call={0x5935C,0x5936C,0x5937C,0x5938C};
 e.bytes={0xD0,1,0x64,0,1,0x64,2,0x40,0x96};e.command={0,64,uint32_t(q.bank_ids[0]),0,64,4096};
 return q.track.cursor_offset==0xB45&&GameSourceProcessorExact(p,e);
}
GameFsStatus GameSourceSoundControl::DispatchControl5B9E4(bool advance_wave,bool advance_port24,bool advance_child){
 auto& pool=voices_.sequences_;auto& q=pool.state_;GameSourceBankRegistration entry;
 if(!registry_||!registry_->Readback(q.bank,entry)||entry.word30!=2||owned_bank_refs_!=2||q.current_refs!=2||!q.command_released)
  return {GameFsCode::Unavailable,0,"5B9E4 private current bank references unavailable"};
 return pool.Dispatch5B9E4(entry.chunks[0],entry.sizes[0],[&](uint32_t at){return bank_.ReadU8(at);},[&]{
  uint32_t before=0,after=0;GameSourceBankRegistration actual;
  if(q.control.bank!=q.bank||!registry_->Retain(q.control.bank,before,after)||!registry_->Readback(q.control.bank,actual)||before!=2||after!=3||actual.word30!=after)
   return GameFsStatus{GameFsCode::Unavailable,0,"5B344 actual D0 control retain/readback unavailable"};
  owned_bank_refs_=after;q.current_refs=after;auto& c=q.control;c.bank_retained=true;c.retain_pc=0x5B344;c.retain_writer=0x55A2C;c.refs_before=before;c.refs_after=after;return GameFsStatus{};
 },advance_wave?GameSourceSequencePool::WaveCall([&](int32_t bank,uint32_t wave,uintptr_t& output){return LookupWave557B8(bank,wave,output);}):GameSourceSequencePool::WaveCall{},advance_port24?GameSourceSequencePool::ControlCall([&]{return ReadProducedPort24(advance_child);}):GameSourceSequencePool::ControlCall{});
}
GameFsStatus GameSourceSoundControl::ReadProducedPort24(bool advance_child){
 auto& pool=voices_.sequences_;GameSourceBankRegistration entry;auto expected=state_.registration;expected.word30=3;
 if(!registry_||owned_bank_refs_!=3||pool.state_.current_refs!=3||pool.port_sound_owner_!=state_.owner||pool.port_sound_generation_!=state_.generation
    ||!registry_->Readback(pool.state_.bank,entry)||!GameSourceBankRegistrationExact(entry,expected))
  return {GameFsCode::Unavailable,0,"5B1EC current issuing bank/sound owner unavailable"};
 const auto result=pool.ReadProducedPort24(advance_child?GameSourceSequencePool::ChildCall([&](const auto& packet){return BeginOwnedChild5ABF0(packet);}):GameSourceSequencePool::ChildCall{});GameSourceBankRegistration after;
 if(active_child_scope_&&active_child_scope_->slot_&&pool.state_.slot.retained)expected.word30=4;
 if(owned_bank_refs_!=expected.word30||!registry_->Readback(pool.state_.bank,after)||!GameSourceBankRegistrationExact(after,expected))return {GameFsCode::Unavailable,0,"5B1EC current bank after-read changed"};
 return result;
}
GameFsStatus GameSourceSoundControl::BeginOwnedChild5ABF0(const GameSourceSequencePool::ChildPacket& packet){
 auto& pool=voices_.sequences_;GameSourceBankRegistration current;auto expected=state_.registration;expected.word30=3;
 if(!active_child_scope_||!active_child_scope_->active_||active_child_scope_->source_!=this||active_child_scope_->expected_.owner!=state_.owner||active_child_scope_->expected_.generation!=state_.generation
    ||&packet!=&pool.child_packet_||!registry_||owned_bank_refs_!=3||pool.state_.current_refs!=3||pool.child_sound_owner_!=state_.owner||pool.child_sound_generation_!=state_.generation
    ||!registry_->Readback(pool.state_.bank,current)||!GameSourceBankRegistrationExact(current,expected))
  return {GameFsCode::Unavailable,0,"5ABF0 actual child callscope/bank owner unavailable"};
 pool.advance_slot_=active_child_scope_->slot_;
 struct EndSlot {bool& value;~EndSlot(){value=false;}} slot_scope{pool.advance_slot_};
 auto result=pool.BeginChild5ABF0(packet,[&](uintptr_t slot,uint32_t& value){
  GameSourceBankRegistration before,after;
  if(slot!=packet.mode_slot||slot!=pool.wave_output_||!registry_->Readback(pool.state_.bank,before)||!GameSourceBankRegistrationExact(before,expected)
     ||!registry_->ReadModeSlot(pool.state_.bank,slot,value)||!registry_->Readback(pool.state_.bank,after)||!GameSourceBankRegistrationExact(after,expected))
   return GameFsStatus{GameFsCode::Unavailable,0,"5F57C actual BANK mode-slot read/lease unavailable"};
  return GameFsStatus{};
 });
 if(result&&active_child_scope_->slot_){uint8_t* returned_node=nullptr;result=pool.ContinueSlotChild([&]{
  auto& s=pool.state_.slot;GameSourceBankRegistration before,after;uint32_t b=0,a=0;
  if(owned_bank_refs_!=3||pool.state_.current_refs!=3||!registry_->Readback(pool.state_.bank,before)||!GameSourceBankRegistrationExact(before,expected))return GameFsStatus{GameFsCode::Unavailable,0,"5AD6C actual child retain bank owner changed"};
  const bool returned=registry_->Retain(pool.state_.bank,b,a);s.retain_result=returned?1:0;
  if(!returned)return GameFsStatus{GameFsCode::Unavailable,0,"Native require-success child Acquire policy after genuine559E8 failure"};
  owned_bank_refs_=a;pool.state_.current_refs=a;auto retained=expected;retained.word30=a;
  if(b!=3||a!=4||!registry_->Readback(pool.state_.bank,after)||!GameSourceBankRegistrationExact(after,retained))return GameFsStatus{GameFsCode::Unavailable,0,"5AD6C actual child retain readback changed"};
  return GameFsStatus{};
 },returned_node);if(result)result=pool.ParentAfterChild5B228(returned_node);}
 if(active_child_scope_->slot_&&pool.state_.slot.retained)expected.word30=4;
 GameSourceBankRegistration after;if(owned_bank_refs_!=expected.word30||!registry_->Readback(pool.state_.bank,after)||!GameSourceBankRegistrationExact(after,expected))return {GameFsCode::Unavailable,0,"5ABF0 current bank after child prefix changed"};
 return result;
}
GameFsStatus GameSourceSoundControl::LookupWave557B8(int32_t bank,uint32_t wave,uintptr_t& output){
 auto& pool=voices_.sequences_;auto& q=pool.state_;auto& w=q.wave;auto& c=q.control;w.entered=true;w.bank=bank;w.wave=int32_t(wave);w.output_storage=reinterpret_cast<uintptr_t>(&output);w.lookup_pc=0x55144;
 if(&output!=&pool.wave_output_||bank!=q.bank||!registry_||owned_bank_refs_!=3||q.current_refs!=3)return {GameFsCode::Unavailable,0,"557B8 actual output/bank/ref owner changed"};
 PackageView image;uintptr_t slot=0;GameSourceBankRegistration entry;
 if(!registry_->BorrowWaveSource(bank,image,slot)||!registry_->Readback(bank,entry)||!BankSame(image.Describe(),bank_.Describe())||entry.word30!=3)
  return {GameFsCode::Unavailable,0,"55144 current WAVE bank allocation unavailable"};
 auto expected=state_.registration;expected.word30=owned_bank_refs_;
 if(!GameSourceBankRegistrationExact(entry,expected))return {GameFsCode::Unavailable,0,"557B8 current issued registration lease changed"};
 w.bank_lookup_returned=true;w.registry=entry.registry;w.registry_generation=entry.generation;w.registry_serial=entry.serial;
 const auto desc=image.Describe();w.image_owner=desc.owner;w.image_generation=desc.generation;w.image_serial=desc.serial;w.image_host_address=desc.host_address;w.image_offset=desc.offset;
 const auto returned=[&](bool is_null){w.result_known=true;w.result_null=is_null;w.returned=true;w.return_pc=0x55848;w.return_delay_pc=0x5584C;c.wave_result_known=c.wave_returned=true;return GameFsStatus{};};
 try{
  w.wave_load_pc=0x557E8;w.wave_payload=entry.chunks[1];w.wave_size=entry.sizes[1];
  if(!w.wave_payload)return returned(true);w.wave_chunk_known=true;
  if(w.wave<0)return returned(true);
  if(w.wave_size<8)return {GameFsCode::Unavailable,0,"557B8 current WAVE count extent"};
  w.wave_count=image.ReadLE32(w.wave_payload+4);w.count_read=true;w.count_load_pc=0x557FC;
  if(int32_t(w.wave_count-1)<w.wave)return returned(true);
  const uint64_t at=uint64_t(w.wave_payload)+8+uint64_t(uint32_t(w.wave))*56;w.stride=56;
  if(at>UINT32_MAX||at+56>uint64_t(w.wave_payload)+w.wave_size)return {GameFsCode::Unavailable,0,"557B8 current selected WAVE record extent"};
  w.record_offset=uint32_t(at);w.control_slot_storage=slot;w.mode_load_pc=0x55814;
  if(!registry_->ReadModeSlot(bank,slot,w.control_mode))return {GameFsCode::Unavailable,0,"55814 actual BANK mode slot readback"};w.mode_read=true;
  if(!w.control_mode)return returned(true);
  auto borrow=image.Subview(at,56);const auto rd=borrow.Describe();
  if(rd.host_address>UINTPTR_MAX-rd.offset)return {GameFsCode::Unavailable,0,"557B8 native WAVE address extent"};
  output=slot;w.output_written=true;w.output_store_pc=0x55830;w.output_value=output;c.wave_output_known=true;
  uint32_t mode=0;GameSourceBankRegistration after;w.output_readback=output==slot&&registry_->ReadModeSlot(bank,output,mode)&&mode==w.control_mode
   &&registry_->Readback(bank,after)&&GameSourceBankRegistrationExact(after,expected);
  if(!w.output_readback)return {GameFsCode::Unavailable,0,"55830 actual output/control slot readback"};
  pool.wave_view_=std::move(borrow);w.result_pointer=uintptr_t(rd.host_address+rd.offset);w.borrow_live=true;return returned(false);
 }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
bool GameSourceControlPrefixValid(const GameSourceFeedbackRead& r)noexcept{
 const auto& q=r.after.voice.sequence;const auto& c=q.control;
 if(r.outcome!=6||r.frontier!=0x557B8||r.after.voice.frontier!=r.frontier||q.frontier!=r.frontier||q.current_refs!=3)return false;
 auto legacy=r;legacy.outcome=5;legacy.frontier=0x5B9E4;legacy.after.voice.frontier=0x5B9E4;legacy.after.voice.sequence.frontier=0x5B9E4;legacy.after.voice.sequence.current_refs=2;legacy.after.voice.sequence.control={};
 if(!GameSourceProcessorPrefixValid(legacy)||!c.wave_output_storage)return false;
 const uintptr_t identities[]={q.ids_storage,q.output_storage,q.track.storage,q.track.context_storage,q.port.storage,q.processor.command_storage};
 for(auto address:identities)if(c.wave_output_storage==address)return false;
 GameSourceControlSnapshot e;e.entered=e.lookup_returned=e.command_dispatched=e.prepare_entered=e.prepare_returned=e.bank_retained=e.region_returned=e.region_selected=e.child_entered=e.wave_attempted=true;
 e.port_storage=q.port.storage;e.command_storage=q.processor.command_storage;e.wave_output_storage=c.wave_output_storage;e.port_owner=q.port.owner;e.port_generation=q.port.generation;e.port_id=q.port.id;
 e.note=64;e.bank=r.bank_handle;e.program=0;e.lookup_pc=0x5A1B4;e.source_stride=0x80;e.pool_count=16;e.lookup_flags=1;e.dispatch_pc=0x5BA44;e.handler_target=0x5B2F4;e.prepare_pc=0x5B33C;e.prepare_return_pc=0x5B09C;
 e.retain_pc=0x5B344;e.retain_writer=0x55A2C;e.refs_before=2;e.refs_after=3;e.rgnd_base=24;e.group_count=1;e.region_count=41;e.region_first=40;e.region_offset=0x728;e.walk_count=33;e.stride_reads=32;e.selected_flags=9;e.selected_byteC=10;e.selected_byteD=0;e.selected_wave=30;e.child_call_pc=0x5B494;e.child_target=0x5B130;e.wave_call_pc=0x5B178;e.frontier=0x557B8;
 for(uint32_t i=0;i<33;++i){e.offsets[i]=40+56*i;e.lows[i]=e.highs[i]=32+int32_t(i);if(i<32)e.strides[i]=56;}
 e.child_args={q.port.id,64,r.bank_handle,30,10,0,-1};return GameSourceControlExact(c,e);
}
bool GameSourceWavePrefixValid(const GameSourceFeedbackRead& r)noexcept{
 const auto& q=r.after.voice.sequence;const auto& w=q.wave;
 if(!w.result_pointer||r.after.bank.offset>UINTPTR_MAX||r.after.bank.host_address>UINTPTR_MAX-r.after.bank.offset
    ||r.after.bank.host_address+r.after.bank.offset>UINTPTR_MAX-6272)return false;
 if(r.outcome!=7||r.frontier!=0x5B1EC||q.frontier!=r.frontier||r.after.voice.frontier!=r.frontier||q.control.frontier!=r.frontier)return false;
 auto legacy=r;legacy.outcome=6;legacy.frontier=legacy.after.voice.frontier=legacy.after.voice.sequence.frontier=legacy.after.voice.sequence.control.frontier=0x557B8;
 legacy.after.voice.sequence.wave={};legacy.after.voice.sequence.control.wave_output_known=legacy.after.voice.sequence.control.wave_result_known=legacy.after.voice.sequence.control.wave_returned=false;
 if(!GameSourceControlPrefixValid(legacy)||!w.control_slot_storage)return false;
 const uintptr_t aliases[]={q.ids_storage,q.output_storage,q.track.storage,q.track.context_storage,q.port.storage,q.processor.command_storage,q.control.wave_output_storage,w.result_pointer};
 for(auto v:aliases)if(w.control_slot_storage==v)return false;
 GameSourceWaveSnapshot e;e.entered=e.bank_lookup_returned=e.wave_chunk_known=e.count_read=e.mode_read=e.output_written=e.output_readback=e.result_known=e.returned=e.borrow_live=e.child_continued=e.port_word24_attempted=true;
 e.output_storage=q.control.wave_output_storage;e.control_slot_storage=e.output_value=w.control_slot_storage;e.result_pointer=uintptr_t(r.after.bank.host_address+r.after.bank.offset+6272);e.port_storage=q.port.storage;
 e.registry=r.after.registration.registry;e.registry_generation=r.after.registration.generation;e.registry_serial=r.after.registration.serial;
 e.image_owner=r.after.bank.owner;e.image_generation=r.after.bank.generation;e.image_serial=r.after.bank.serial;e.image_offset=r.after.bank.offset;e.image_host_address=r.after.bank.host_address;
 e.bank=r.bank_handle;e.wave=30;e.port_id=q.port.id;e.byte8=3;e.byte9=e.channels=1;e.lookup_pc=0x55144;e.wave_load_pc=0x557E8;e.count_load_pc=0x557FC;e.mode_load_pc=0x55814;e.output_store_pc=0x55830;e.return_pc=0x55848;e.return_delay_pc=0x5584C;
 e.wave_payload=4584;e.wave_size=2136;e.wave_count=38;e.stride=56;e.record_offset=6272;e.control_mode=1;e.record_read_mask=63;e.stack_known=127;e.stack_writes=7;e.port_read_pc=e.frontier=0x5B1EC;
 e.parameters={0,10,3,1,0,0,0};e.store_pc={0x5B1A4,0x5B1B0,0x5B1B8,0x5B1C0,0x5B1C4,0x5B1D4,0x5B1E0};e.store_index={1,0,2,3,4,6,5};
 return GameSourceWaveExact(w,e)&&q.control.wave_output_known&&q.control.wave_result_known&&q.control.wave_returned;
}
bool GameSourcePort24PrefixValid(const GameSourceFeedbackRead& r)noexcept{
 const auto& q=r.after.voice.sequence;const auto& a=q.port_input;
 if(r.outcome!=8||r.frontier!=0x5ABF0||q.frontier!=r.frontier||r.after.voice.frontier!=r.frontier||q.control.frontier!=r.frontier||q.wave.frontier!=r.frontier)return false;
 auto old=r;old.outcome=7;old.frontier=old.after.voice.frontier=old.after.voice.sequence.frontier=old.after.voice.sequence.control.frontier=old.after.voice.sequence.wave.frontier=0x5B1EC;
 old.after.voice.sequence.wave.port_word24_known=old.after.voice.sequence.wave.port_word24_returned=false;old.after.voice.sequence.port_input={};
 if(!GameSourceWavePrefixValid(old)||!a.block_owner||!a.block_generation||!a.block_serial||!a.block_base||a.block_base%4||!a.packet_storage
    ||a.block_base>UINTPTR_MAX-2048||a.index>=16||a.index!=uint32_t(q.port.id))return false;
 const uintptr_t aliases[]={q.ids_storage,q.output_storage,q.track.storage,q.track.context_storage,q.port.storage,q.processor.command_storage,q.control.wave_output_storage,q.wave.control_slot_storage,q.wave.result_pointer};
 for(auto p:aliases)if(a.packet_storage==p||a.block_base==p||a.node_address==p)return false;
 if(a.packet_storage>=a.block_base&&a.packet_storage<a.block_base+2048)return false;
 GameSourcePortInputSnapshot e;e.entered=e.allocation_live=e.zeroed=e.bound=e.read_known=e.read_returned=e.packet_complete=e.child_entered=true;
 e.block_owner=a.block_owner;e.block_generation=a.block_generation;e.block_serial=a.block_serial;e.sound_owner=r.after.owner;e.sound_generation=r.after.generation;e.port_owner=q.port.owner;e.port_generation=q.port.generation;
 e.block_base=a.block_base;e.node_address=a.block_base+128*a.index;e.port_storage=q.port.storage;e.packet_storage=a.packet_storage;e.mode_slot=q.wave.control_slot_storage;
 e.bytes=2048;e.count=16;e.stride=128;e.zero_writes=e.zero_readbacks=512;e.index=a.index;e.flags=1;e.read_pc=0x5B1EC;e.read_offset=128*a.index+0x24;e.stack_known=0x7FFF;e.stack_writes=8;e.child_pc=0x5B220;e.child_delay_pc=0x5B224;e.frontier=0x5ABF0;
 e.port_id=q.port.id;e.note=64;e.bank=r.bank_handle;e.channels=1;e.words={0,10,3,1,0,0,0,0,0x73E04,UINT32_MAX,0x73E04,0,0x640};
 e.store_index={7,8,9,11,10,14,13,12};e.store_pc={0x5B1F0,0x5B1F8,0x5B200,0x5B208,0x5B20C,0x5B218,0x5B21C,0x5B224};
 return GameSourcePortInputExact(a,e)&&q.wave.port_word24_known&&q.wave.port_word24_returned;
}
bool GameSourceChildPrefixValid(const GameSourceFeedbackRead& r)noexcept{
 const auto& q=r.after.voice.sequence;const auto& a=q.child;
 if(r.outcome!=9||r.frontier!=0x5C8F4||r.after.voice.frontier!=r.frontier||q.frontier!=r.frontier||q.wave.frontier!=r.frontier||q.control.frontier!=r.frontier)return false;
 auto old=r;old.outcome=8;old.frontier=old.after.voice.frontier=old.after.voice.sequence.frontier=old.after.voice.sequence.wave.frontier=old.after.voice.sequence.control.frontier=0x5ABF0;old.after.voice.sequence.child={};
 if(!GameSourcePort24PrefixValid(old)||!a.pool_owner||!a.pool_generation||!a.pool_serial||!a.config_owner||!a.config_generation||!a.pool_base||a.pool_base%4||a.pool_base>UINTPTR_MAX-2176||a.index>=16||!a.node_storage||!a.head_storage)return false;
 const uintptr_t others[]={q.ids_storage,q.output_storage,q.track.storage,q.track.context_storage,q.port.storage,q.processor.command_storage,q.control.wave_output_storage,q.wave.control_slot_storage,q.wave.result_pointer,q.port_input.block_base,q.port_input.packet_storage};
 const uintptr_t fresh[]={a.pool_base,a.node_storage,a.head_storage,a.config_storage,a.key_storage,a.default_storage,a.output_storage,a.g_storage};
 for(size_t i=0;i<8;++i){if(!fresh[i])return false;for(auto p:others)if(fresh[i]==p)return false;for(size_t j=0;j<i;++j)if(fresh[i]==fresh[j])return false;}
 if(a.pool_base<q.port_input.block_base+2048&&q.port_input.block_base<a.pool_base+2176)return false;
 if(!a.candidate_links[1]||a.candidate_links[1]==a.node_storage)return false;
 GameSourceChildSnapshot e;e.entered=e.live=e.reserved=e.source_free_linked=e.header_readback=e.channel_written=e.bank_mode_read=e.format_dispatched=e.g_read=e.slot_entered=e.config_null=e.config_copy_returned=e.config_written=e.config_readback=true;
 e.pool_owner=a.pool_owner;e.pool_generation=a.pool_generation;e.pool_serial=a.pool_serial;e.sound_owner=r.after.owner;e.sound_generation=r.after.generation;e.config_owner=a.config_owner;e.config_generation=a.config_generation;
 e.pool_base=a.pool_base;e.node_address=a.pool_base+136*a.index;e.node_storage=a.node_storage;e.head_storage=a.head_storage;e.port_storage=q.port.storage;e.packet_storage=q.port_input.packet_storage;e.bank_mode_slot=q.wave.control_slot_storage;
 e.config_storage=a.config_storage;e.key_storage=a.key_storage;e.default_storage=a.default_storage;e.output_storage=a.output_storage;e.g_storage=a.g_storage;
 e.candidate_id=int32_t(a.index);e.parent_port_id=q.port.id;e.note=64;e.channels=1;e.bank=r.bank_handle;e.bank_mode=1;e.format=3;e.allocator_kind=1;e.allocator_argument=10;
 e.bytes=2176;e.count=16;e.stride=136;e.index=a.index;e.zero_writes=e.zero_readbacks=544;e.header_writes=32;e.channel_offset=136*a.index+0x2A;e.channel_after=255;e.channel_writes=1;e.channel_pc=0x5AC70;e.caller_pc=0x5AC80;e.mode_read_pc=0x5F57C;
 e.format_table_word=0xDF520;e.format_target=0x5F5F4;e.g_read_pc=0x5F5F8;e.g_reference_segment=1;e.g_source_addend=0xE47F4;e.config_copy_writes=1;e.g_writes=2;e.g_writer_pc=0x54B74;e.config_call_pc=0x5CCCC;e.allocator_pc=0x5F5B8;e.slot_pc=0x5CA8C;e.slot_delay_pc=0x5CA90;e.frontier=0x5C8F4;
 e.candidate_links={a.head_storage,a.candidate_links[1]};e.config_key={'s','g','x','-','p','s','p','-','s','s','e','-','u','s','e',0};
 return GameSourceChildExact(a,e);
}
bool GameSourceSlotPrefixValid(const GameSourceFeedbackRead& r)noexcept{
 const auto& q=r.after.voice.sequence;const auto& s=q.slot;const auto& entry=q.child;
 if(r.outcome!=10||r.frontier!=0x6016C||r.after.voice.frontier!=r.frontier||q.frontier!=r.frontier||q.wave.frontier!=r.frontier||q.control.frontier!=r.frontier
    ||q.current_refs!=4||q.port.flags!=3||q.port.sentinel_self[0]||!q.port.sentinel_self[1])return false;
 //Closed historical projection validates transport only. It never assigns an
 //expected owner, changes a live node, adopts a receipt or publishes authority.
 auto old=r;old.outcome=9;old.frontier=old.after.voice.frontier=old.after.voice.sequence.frontier=old.after.voice.sequence.wave.frontier=old.after.voice.sequence.control.frontier=0x5C8F4;
 old.after.voice.sequence.current_refs=3;old.after.voice.sequence.port.flags=1;old.after.voice.sequence.port.sentinel_self[0]=true;old.after.voice.sequence.slot={};
 if(!GameSourceChildPrefixValid(old)||!s.owner||!s.generation||!s.serial||!s.base||s.base%4||s.base>UINTPTR_MAX-512||s.selector_result<0||s.selector_result>=16
    ||s.child_links[0]!=s.child_links[1]||!s.child_links[0]||s.child_links[0]==entry.node_storage||!s.child_sentinel_storage||s.child_sentinel_storage==entry.node_storage)return false;
 const uintptr_t prior[]={entry.pool_base,q.port_input.block_base,q.port.storage,q.processor.command_storage,q.wave.control_slot_storage,q.wave.result_pointer,q.port_input.packet_storage};
 for(auto address:prior)if(s.base==address||s.child_sentinel_storage==address||s.child_links[0]==address)return false;
 if(s.base<entry.pool_base+2176&&entry.pool_base<s.base+512)return false;
 if(s.base<q.port_input.block_base+2048&&q.port_input.block_base<s.base+512)return false;
 GameSourceSlotSnapshot e;e.entered=e.live=e.header_read=e.selector_returned=e.allocator_returned=e.dispatcher_returned=e.channel_returned=e.first_registered=e.second_registered=e.constructed=e.parameters_initialized=e.scratch_zeroed=e.retain_attempted=e.retained=e.free_unlinked=e.parent_linked=e.child_returned=e.packet_written=e.consumer_entered=e.current_readback=true;
 e.owner=s.owner;e.generation=s.generation;e.serial=s.serial;e.sound_owner=r.after.owner;e.sound_generation=r.after.generation;
 e.base=s.base;e.storage=s.base+32*uint32_t(s.selector_result);e.child_pointer=entry.node_address;e.parent_storage=q.port.storage;e.packet_storage=q.port_input.packet_storage;e.mode_slot=q.wave.control_slot_storage;
 e.child_parent=q.port.storage;e.child_sentinel_storage=s.child_sentinel_storage;
 e.kind=1;e.argument=10;e.packed=0;e.low=0;e.high=15;e.cursor_before=0;e.cursor_after=1;
 e.selector_result=e.selected_position=e.allocator_result=e.stored_channel=e.callback_id=0;
 e.current_note=64;e.retain_result=1;e.count=16;e.bytes=512;e.zero_writes=e.zero_readbacks=128;e.id_writes=16;e.header_zero_writes=26;e.range_writes=3;
 e.free_reads=1;e.allocation_flags=1;e.slot_half6=10;e.slot_kind=1;e.current_parent_flags=3;e.phase=9;e.retain_before=3;e.retain_after=4;e.retain_pc=0x5AD6C;e.return_pc=0x5ADE4;e.return_delay_pc=0x5ADE8;
 e.wave_word30=s.wave_word30;e.packet_word6=s.wave_word30;
 e.consumer_pc=0x5B264;e.consumer_delay_pc=0x5B268;e.frontier=0x6016C;e.scratch_writes=4;e.parameter_writes=35;e.known_halfwords=UINT64_C(0xFFFFFF003);
 e.child_links=s.child_links;e.callback_contexts={entry.node_address+0x2A,entry.node_address};e.callback_codes={0x5AA90,0x5AAC4};e.callback_context_pc={0x5CB74,0x5CB74};e.callback_code_pc={0x5CB7C,0x5CB7C};
 e.child_sentinel_links={s.child_sentinel_storage,s.child_sentinel_storage};e.child_sentinel_ids={-1,0};
 //Unknown source parameter halfwords2..11 stay unknown. They are transported,
 //not read as permission or silently promoted to source-known zeros.
 for(size_t i=2;i<12;++i)e.parameters[i]=s.parameters[i];e.parameters[0]=UINT16_MAX;
 for(size_t k=0;k<4;++k){e.parameters[16+6*k]=4096;e.parameters[17+6*k]=1000;}
 return GameSourceSlotExact(s,e);
}
GameFsStatus GameSourceSoundControl::RequestBgm(int32_t){
    if(state_.failed)return failure_;
    ++state_.bgm_attempts;state_.failed=true;
    failure_={GameFsCode::Unavailable,0,"Unsupported source BGM request4AD4C before owner mutation"};return failure_;
}
void GameSourceSoundControl::Close()noexcept{active_child_scope_=nullptr;voices_.Close();registry_=nullptr;bank_.Reset();state_.registration.live=false;records_[5].bank_handle_known=false;state_.live=state_.bank_live=false;}
}
