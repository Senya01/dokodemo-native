#include "game_sound_control_native.h"
#include <atomic>
#include <tuple>

namespace dokodemo::recovered {
namespace {
bool BankSame(const PackageViewDescription& a,const PackageViewDescription& b)noexcept{
    return std::tie(a.owner,a.generation,a.serial,a.allocation_bytes,a.host_address,a.offset,a.size,a.source_address,a.allocation_source_base,a.readable,a.writable)
        ==std::tie(b.owner,b.generation,b.serial,b.allocation_bytes,b.host_address,b.offset,b.size,b.source_address,b.allocation_source_base,b.readable,b.writable);
}
bool ColdRecord(const GameSourceSoundRecord& r,uint32_t vtable)noexcept{
    return !r.name&&!r.context54&&!r.context58&&!r.resource&&!r.resource_state&&r.vtable==vtable&&r.bank_handle==-1&&r.bank_index==-1&&r.handle==-1
        &&r.gain==128&&!r.state18&&!r.state1C&&!r.state20&&r.step==4&&!r.position&&!r.request&&!r.flags&&r.rate==1.0f
        &&!r.bank_attached&&r.bank_handle_known&&r.name_known;
}
}
bool GameSourceSoundOwnerStable(const GameSourceSoundSnapshot& a,const GameSourceSoundSnapshot& b)noexcept{
    return std::tie(a.live,a.cold_initialized,a.bank_live,a.failed,a.owner,a.generation,a.cold_receipt,a.id_writes,a.bank_bindings,a.bgm_attempts,
        a.constructor,a.last_id_writer,a.se_clean_mask,a.bgm_clean_mask,a.automatic_phase,a.current_id,a.previous_id,a.pending_id,a.automatic)
        ==std::tie(b.live,b.cold_initialized,b.bank_live,b.failed,b.owner,b.generation,b.cold_receipt,b.id_writes,b.bank_bindings,b.bgm_attempts,
        b.constructor,b.last_id_writer,b.se_clean_mask,b.bgm_clean_mask,b.automatic_phase,b.current_id,b.previous_id,b.pending_id,b.automatic)&&BankSame(a.bank,b.bank);
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
    failure_={};bank_.Reset();return {};
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
    out.bank_live=false;
    if(out.live&&out.bank_bindings)try{out.bank_live=BankSame(bank_.Describe(),out.bank);}catch(const std::exception&){}
    return out;
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
GameFsStatus GameSourceSoundControl::RequestBgm(int32_t){
    if(state_.failed)return failure_;
    ++state_.bgm_attempts;state_.failed=true;
    failure_={GameFsCode::Unavailable,0,"Unsupported source BGM request4AD4C before owner mutation"};return failure_;
}
void GameSourceSoundControl::Close()noexcept{bank_.Reset();state_.live=state_.bank_live=false;}
}
