#include "game_sound_control_native.h"
#include <atomic>
#include <tuple>

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
    if(!e.live||e.id!=id||e.active!=1||e.registry!=owner_||e.generation!=generation_||!e.coherent||e.word0C||e.word30||e.initial_zero24_mask!=7)return false;
    for(auto value:e.ids)if(value!=id)return false;
    try{if(!BankSame(e.image,images_[size_t(id)].Describe())||images_[size_t(id)].InitializedDigest()!=e.digest)return false;
        if(e.attached&&(!BankSame(e.tail,tails_[size_t(id)].Describe())||e.mode!=1||e.state))return false;
    }catch(const std::exception&){return false;}
    out=e;return true;
}
void GameSourceBankRegistry::Close()noexcept{
    for(size_t i=0;i<64;++i){entries_[i]={};tails_[i].Reset();images_[i].Reset();}++generation_;
}
bool GameSourceSoundOwnerStable(const GameSourceSoundSnapshot& a,const GameSourceSoundSnapshot& b)noexcept{
    return std::tie(a.live,a.cold_initialized,a.bank_live,a.failed,a.owner,a.generation,a.cold_receipt,a.id_writes,a.bank_bindings,a.bgm_attempts,
        a.constructor,a.last_id_writer,a.se_clean_mask,a.bgm_clean_mask,a.automatic_phase,a.current_id,a.previous_id,a.pending_id,a.automatic)
        ==std::tie(b.live,b.cold_initialized,b.bank_live,b.failed,b.owner,b.generation,b.cold_receipt,b.id_writes,b.bank_bindings,b.bgm_attempts,
        b.constructor,b.last_id_writer,b.se_clean_mask,b.bgm_clean_mask,b.automatic_phase,b.current_id,b.previous_id,b.pending_id,b.automatic)&&BankSame(a.bank,b.bank)&&GameSourceBankRegistrationExact(a.registration,b.registration)
        &&RecordSame(a.se5,b.se5)&&std::tie(a.feedback_attempts,a.feedback_writes,a.feedback_frontier,a.feedback_gain,a.feedback_failed)
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
    static std::atomic<uint64_t> next_owner{0};const auto generation=state_.generation+1;state_={};
    state_.live=state_.cold_initialized=true;state_.owner=++next_owner;state_.generation=generation;
    state_.constructor=0x17F0;state_.cold_receipt=state_.id_writes=1;state_.last_id_writer=0x18E4;
    // Explicit source loops:6 SE records,25 BGM records; sourceD2A04=1.0f.
    for(uint32_t i=0;i<31;++i){records_[i]={};records_[i].vtable=i<6?0xDD0A0:0xDD0B0;}
    //18E0/E4/E8/EC/F4, not an inference from zeroed BSS or SGX failure.
    state_.pending_id=state_.current_id=state_.previous_id=-1;state_.automatic=0;state_.automatic_phase=0;
    failure_={};feedback_={};feedback_failure_={};registry_=nullptr;bank_.Reset();return {};
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
    auto out=state_;out.se_clean_mask=out.bgm_clean_mask=0;
    for(uint32_t i=0;i<31;++i)if(ColdRecord(records_[i],i<6?0xDD0A0:0xDD0B0)){
        if(i<6)out.se_clean_mask|=1u<<i;else out.bgm_clean_mask|=1u<<(i-6);
    }
    out.se5=records_[5];out.bank_live=false;
    if(out.live&&out.bank_bindings)try{out.bank_live=BankSame(bank_.Describe(),out.bank);}catch(const std::exception&){}
    if(registry_){GameSourceBankRegistration entry;if(!registry_->Readback(records_[5].bank_handle,entry)
        ||!GameSourceBankRegistrationExact(entry,state_.registration))out.bank_live=false;}
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
    if(!r.attempted||!r.bank_attached||!r.bank_handle_known||!r.read_returned||!r.getter_returned||r.start_returned||r.handle_stored||r.feedback_returned
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
      &&std::tie(a.context_index,a.read_pc,a.attempted,a.bank_attached,a.bank_handle_known,a.read_returned,a.frontier,a.selector,a.params_written,a.virtual_target,a.wrapper,a.issuer,a.bank_handle,a.gain,a.group,a.event,a.extra,a.mode,a.rate,a.context_owner,a.context_generation,a.getter_returned,a.start_returned,a.handle_stored,a.feedback_returned)
      ==std::tie(b.context_index,b.read_pc,b.attempted,b.bank_attached,b.bank_handle_known,b.read_returned,b.frontier,b.selector,b.params_written,b.virtual_target,b.wrapper,b.issuer,b.bank_handle,b.gain,b.group,b.event,b.extra,b.mode,b.rate,b.context_owner,b.context_generation,b.getter_returned,b.start_returned,b.handle_stored,b.feedback_returned);
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
    state_.feedback_attempts=1;state_.feedback_writes=6;state_.feedback_frontier=r.frontier;state_.feedback_gain=45;state_.feedback_failed=true;
    r.after=Snapshot();feedback_=r;out=r;feedback_failure_=status;return status;
}
GameFsStatus GameSourceSoundControl::Start4AF54(GameSourceFeedbackRead& r){
    r.virtual_target=0x4AF54;
    return Start56930(r,records_[5].bank_handle,int32_t(records_[5].position),int32_t(records_[5].request),int32_t(records_[5].resource_state));
}
GameFsStatus GameSourceSoundControl::Start56930(GameSourceFeedbackRead& r,int32_t bank,int32_t group,int32_t event,int32_t extra){
    r.wrapper=0x56930;
    return Issue56598(r,2,bank,group,event,extra,0);
}
GameFsStatus GameSourceSoundControl::Issue56598(GameSourceFeedbackRead& r,int32_t mode,int32_t bank,int32_t group,int32_t event,int32_t extra,int32_t last){
    r.issuer=r.frontier=0x56598;r.mode=mode;
    if(mode!=2||bank!=r.bank_handle||group||event!=6||extra||last)
        return {GameFsCode::Unavailable,0,"56598 selected wrapper argument mismatch"};
    // HOST Unavailable has no source v0. In particular no4AF7C playback handle
    // write or post-start gain/rate dispatch is performed without a real voice.
    return {GameFsCode::Unavailable,0,"Unsupported source voice issuer56598(mode2, registered bank, group0, event6, 0, 0)"};
}

GameFsStatus GameSourceSoundControl::RequestBgm(int32_t){
    if(state_.failed)return failure_;
    ++state_.bgm_attempts;state_.failed=true;
    failure_={GameFsCode::Unavailable,0,"Unsupported source BGM request4AD4C before owner mutation"};return failure_;
}
void GameSourceSoundControl::Close()noexcept{registry_=nullptr;bank_.Reset();state_.registration.live=false;records_[5].bank_handle_known=false;state_.live=state_.bank_live=false;}
}
