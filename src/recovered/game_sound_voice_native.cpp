#include "game_sound_voice_native.h"
#include <algorithm>
#include <atomic>
#include <tuple>
#include <cmath>
#include <limits>
namespace dokodemo::recovered {
bool GameSourcePortExact(const GameSourcePortSnapshot& a,const GameSourcePortSnapshot& b)noexcept{
 return std::tie(a.live,a.active,a.returned,a.free_unlinked,a.active_linked,a.owner,a.generation,a.allocation,a.known_halfwords,a.storage,a.id,a.flags,a.initialize_pc,a.return_pc,a.sentinel_ids,a.sentinel_flags,a.sentinel_self,a.parameters,a.zero70)==std::tie(b.live,b.active,b.returned,b.free_unlinked,b.active_linked,b.owner,b.generation,b.allocation,b.known_halfwords,b.storage,b.id,b.flags,b.initialize_pc,b.return_pc,b.sentinel_ids,b.sentinel_flags,b.sentinel_self,b.parameters,b.zero70);
}
bool GameSourceTrackExact(const GameSourceTrackSnapshot& a,const GameSourceTrackSnapshot& b)noexcept{
 return std::tie(a.live,a.initialized,a.free_unlinked,a.child_linked,a.lane_initialized,a.returned,a.storage,a.context_storage,a.allocation_index,a.subindex,a.bank,a.record_offset,a.body_offset,a.end_offset,a.word20,a.wordCC,a.wordE4,a.wordE6,a.byte28,a.byte29,a.allocated_ports,a.lanes,a.retain_pc,a.refs_before,a.refs_after,a.return_pc,a.factor,a.ports,a.lane_controls)==std::tie(b.live,b.initialized,b.free_unlinked,b.child_linked,b.lane_initialized,b.returned,b.storage,b.context_storage,b.allocation_index,b.subindex,b.bank,b.record_offset,b.body_offset,b.end_offset,b.word20,b.wordCC,b.wordE4,b.wordE6,b.byte28,b.byte29,b.allocated_ports,b.lanes,b.retain_pc,b.refs_before,b.refs_after,b.return_pc,b.factor,b.ports,b.lane_controls);
}
bool GameSourceSequenceExact(const GameSourceSequenceSnapshot& a,const GameSourceSequenceSnapshot& b)noexcept{
 return std::tie(a.live,a.active,a.allocation_returned,a.lookup_returned,a.clear_returned,a.command_entered,a.bank_retained,a.record_lookup_returned,a.track_reserved,a.track_source_initialized,a.track_activated,a.port_attempted,a.port_result_known,a.handler_returned,a.owner,a.generation,a.allocation,a.voice_owner,a.voice_generation,a.ids_storage,a.output_storage,a.id,a.bank,a.sentinel_id,a.track_id,a.port_id,a.flags,a.sentinel_flags,a.allocation_pc,a.return_pc,a.result_store_pc,a.lookup_pc,a.clear_pc,a.command,a.subindex,a.group,a.event,a.command_pc,a.retain_writer,a.refs_before,a.refs_after,a.record_offset,a.record_kind,a.record_divisor,a.record_bytes,a.track_pool_stride,a.port_call_pc,a.frontier,a.initial_factor,a.selected_factor,a.sentinel_self,a.child_head_same,a.output_unchanged,a.bank_ids,a.output_before,a.output_after,a.admission_returned,a.command_released,a.update_entered,a.processor_attempted,a.current_refs,a.release_pc,a.release_before,a.release_after,a.update_pc,a.processor_pc,a.native_elapsed_ns,a.worker_tick_serial,a.worker_previous_ns,a.worker_current_ns,a.native_delta_seconds,a.update_quantum,a.processor_quantum,a.processor_context)
 ==std::tie(b.live,b.active,b.allocation_returned,b.lookup_returned,b.clear_returned,b.command_entered,b.bank_retained,b.record_lookup_returned,b.track_reserved,b.track_source_initialized,b.track_activated,b.port_attempted,b.port_result_known,b.handler_returned,b.owner,b.generation,b.allocation,b.voice_owner,b.voice_generation,b.ids_storage,b.output_storage,b.id,b.bank,b.sentinel_id,b.track_id,b.port_id,b.flags,b.sentinel_flags,b.allocation_pc,b.return_pc,b.result_store_pc,b.lookup_pc,b.clear_pc,b.command,b.subindex,b.group,b.event,b.command_pc,b.retain_writer,b.refs_before,b.refs_after,b.record_offset,b.record_kind,b.record_divisor,b.record_bytes,b.track_pool_stride,b.port_call_pc,b.frontier,b.initial_factor,b.selected_factor,b.sentinel_self,b.child_head_same,b.output_unchanged,b.bank_ids,b.output_before,b.output_after,b.admission_returned,b.command_released,b.update_entered,b.processor_attempted,b.current_refs,b.release_pc,b.release_before,b.release_after,b.update_pc,b.processor_pc,b.native_elapsed_ns,b.worker_tick_serial,b.worker_previous_ns,b.worker_current_ns,b.native_delta_seconds,b.update_quantum,b.processor_quantum,b.processor_context)&&GameSourcePortExact(a.port,b.port)&&GameSourceTrackExact(a.track,b.track);
}
GameSourceSequencePool::GameSourceSequencePool(){Close();}
void GameSourceSequencePool::Close()noexcept{
 const auto head=[](Link& h){h={};h.next=h.previous=&h;};
 head(free_);head(active_);head(track_free_);head(port_free_);head(port_active_);
 const auto add=[](Link& h,Link& n){n.next=h.next;n.previous=&h;h.next->previous=&n;h.next=&n;};
 for(size_t i=nodes_.size();i-->0;){auto& n=nodes_[i];n={};n.link.id=int16_t(i);add(free_,n.link);
  auto& t=tracks_[i];t={};t.allocation_index=int16_t(i);t.link.id=int16_t(i);add(track_free_,t.link);
  auto& p=ports_[i];p={};p.link.id=int16_t(i);add(port_free_,p.link);}
 current_=nullptr;current_port_=nullptr;current_track_=nullptr;state_.live=state_.active=false;state_.port.live=state_.port.active=false;state_.track.live=false;
}
GameSourceSequenceSnapshot GameSourceSequencePool::Snapshot()const{
 auto out=state_;if(!current_)return out;
 const auto& n=*current_;out.live=state_.live;out.active=(n.link.flags&1)!=0;
 out.id=n.link.id;out.flags=n.link.flags;out.sentinel_id=n.child.id;out.sentinel_flags=n.child.flags;
 out.sentinel_self=n.child.previous==&n.child&&n.child.next==&n.child;out.child_head_same=n.child_head==&n.child;
 out.ids_storage=reinterpret_cast<uintptr_t>(n.ids);out.output_storage=reinterpret_cast<uintptr_t>(n.output);
 if(n.ids)out.bank_ids=*n.ids;if(n.output)out.output_after=*n.output;
 out.output_unchanged=out.output_before==out.output_after;
 if(current_track_&&current_track_->state.initialized){out.track=current_track_->state;out.track.storage=reinterpret_cast<uintptr_t>(current_track_);out.track.context_storage=reinterpret_cast<uintptr_t>(current_);out.track.subindex=current_track_->link.id;
  out.track.child_linked=n.child.next==&current_track_->link&&n.child.previous==&current_track_->link&&current_track_->link.next==&n.child&&current_track_->link.previous==&n.child;}
 if(current_port_){const auto& p=*current_port_;out.port.storage=reinterpret_cast<uintptr_t>(&p);out.port.id=p.link.id;out.port.flags=p.link.flags;out.port.active=(p.link.flags&1)!=0;
  out.port.parameters=p.parameters;out.port.known_halfwords=p.known;out.port.zero70=p.zero70;
  out.port.sentinel_ids={p.child0.id,p.child1.id};out.port.sentinel_flags={p.child0.flags,p.child1.flags};
  out.port.sentinel_self={p.child0.next==&p.child0&&p.child0.previous==&p.child0,p.child1.next==&p.child1&&p.child1.previous==&p.child1};}
 return out;
}
GameFsStatus GameSourceSequencePool::Allocate(const std::array<int32_t,4>& ids,std::array<uint32_t,8>& output,uint64_t voice_owner,uint64_t voice_generation){
 if(current_||!voice_owner||!voice_generation)return {GameFsCode::Unavailable,0,"57CF0 current sequence owner unavailable"};
 auto* chosen=free_.next;if(chosen==&free_||chosen->id<0)return {GameFsCode::Busy,0,"NativeIndexedSequenceContextPool exhausted"};
 const auto id=chosen->id;if(size_t(id)>=nodes_.size()||&nodes_[size_t(id)].link!=chosen)return {GameFsCode::Unavailable,0,"57CF0 free context lookup"};
 auto& n=nodes_[size_t(id)];static std::atomic<uint64_t> next{0};const auto generation=++generation_;state_={};
 state_.owner=++next;state_.generation=generation;state_.allocation=1;state_.voice_owner=voice_owner;state_.voice_generation=voice_generation;
 n.child={};n.child.id=-1;n.child.previous=n.child.next=&n.child;n.child_head=&n.child;n.link.flags=1;n.ids=&ids;n.output=&output;n.word18=n.word1A=0;
 state_.output_before=output;
 chosen->next->previous=chosen->previous;chosen->previous->next=chosen->next;
 chosen->next=active_.next;chosen->previous=&active_;active_.next->previous=chosen;active_.next=chosen;
 current_=&n;state_.live=state_.active=true;state_.allocation_pc=0x57CF0;state_.return_pc=0x57D80;
 if(n.link.id!=id||!(n.link.flags&1)||n.ids!=&ids||n.output!=&output)return {GameFsCode::Unavailable,0,"57D74 actual context ID/argument readback"};
 state_.id=n.link.id;state_.allocation_returned=true;return {}; //actual57D74 readback retained for subsequent58C04 lookup
}
bool GameSourceSequencePool::Lookup(int32_t id){
 if(!current_||!state_.live||id<0||size_t(id)>=nodes_.size())return false;
 const auto& n=nodes_[size_t(id)];if(&n!=current_||n.link.id!=id||!(n.link.flags&1))return false;
 state_.lookup_returned=true;state_.lookup_pc=0x57A90;return true;
}
bool GameSourceSequencePool::Clear58540(){
 if(!current_||!state_.lookup_returned||current_->child_head!=&current_->child||current_->child.id>=0||current_->child.previous!=&current_->child)return false;
 state_.clear_returned=true;state_.clear_pc=0x58540;return true; //actual negative embedded sentinel, no5842C call
}
GameFsStatus GameSourceSequencePool::Start580D4(uint32_t offset,uint16_t kind,uint16_t divisor,uint32_t bytes,bool advance_port,const std::function<GameFsStatus()>& retain){
 if(!current_||!state_.record_lookup_returned||!state_.bank_retained||!state_.clear_returned||kind!=0||!divisor)
 return {GameFsCode::Unavailable,0,"580D4 selected live record unavailable"};
 Track* candidate=nullptr;for(auto& t:tracks_)if(t.free&&!t.reserved){candidate=&t;break;}
 if(!candidate)return {GameFsCode::Busy,0,"NativeIndexedSequenceTrackPool exhausted"};
 //Host reservation preserves the source free-list membership. All source
 //track stores58190+ and activation58220+ follow the authentic port result.
 candidate->reserved=true;state_.track_reserved=true;state_.track_id=candidate->allocation_index;state_.track_pool_stride=0; //original track stride not recovered
 state_.record_offset=offset;state_.record_kind=kind;state_.record_divisor=divisor;state_.record_bytes=bytes;
 state_.initial_factor=1000.0f;state_.selected_factor=1000000.0f/float(divisor); //PH0:DEEB4/DEEB8
 current_track_=candidate;
 if(!advance_port){state_.port_attempted=true;state_.port_call_pc=0x582D8;state_.frontier=0x5A5AC;
  return {GameFsCode::Unavailable,0,"Unsupported selected sequence port allocator5A5AC before source track initialization"};}
 const auto port=AllocatePort5A5AC();if(!port)return port;
 return CompleteTrack580D4(retain);
}
GameFsStatus GameSourceSequencePool::AllocatePort5A5AC(){
 state_.port_attempted=true;state_.port_call_pc=0x582D8;state_.frontier=0x5A5AC;
 auto* chosen=port_free_.next;
 if(chosen==&port_free_||chosen->id<0)return {GameFsCode::Busy,0,"NativeIndexedPortPool16 actual free-head empty"};
 const auto id=chosen->id;if(size_t(id)>=ports_.size()||&ports_[size_t(id)].link!=chosen)return {GameFsCode::Unavailable,0,"5A5AC actual free node coherence"};
 auto& p=ports_[size_t(id)];p.child0={};p.child1={};p.child0.id=p.child1.id=-1;
 p.child0.next=p.child0.previous=&p.child0;p.child1.next=p.child1.previous=&p.child1;p.link.flags=1;
 //5A490 initializes only these halfwords; the remaining words are UNKNOWN
 //source values even though Windows-owned backing storage is initialized.
 p.known=0;const auto write=[&](uint32_t index,uint16_t value){p.parameters[index]=value;p.known|=uint64_t(1)<<index;};
 write(0,0);write(1,0);for(uint32_t i=12;i<36;++i)write(i,0);
 for(uint32_t k=0;k<4;++k){write(16+6*k,4096);write(17+6*k,1000);}
 p.zero70.fill(0); //actual aligned54878(node+70,16) four-word effect
 chosen->next->previous=chosen->previous;chosen->previous->next=chosen->next;
 chosen->next=port_active_.next;chosen->previous=&port_active_;port_active_.next->previous=chosen;port_active_.next=chosen;
 static std::atomic<uint64_t> next{0};auto& r=state_.port;r={};r.owner=++next;r.generation=state_.generation;r.allocation=1;
 current_port_=&p;r.live=r.active=r.free_unlinked=r.active_linked=true;r.initialize_pc=0x5A490;r.return_pc=0x5A660;
 if(p.link.id!=id||p.link.flags!=1||p.link.previous!=&port_active_||port_active_.next!=&p.link)return {GameFsCode::Unavailable,0,"5A64C actual port ID readback"};
 r.id=p.link.id;r.returned=true;state_.port_result_known=true;state_.port_id=r.id;return {};
}
GameFsStatus GameSourceSequencePool::CompleteTrack580D4(const std::function<GameFsStatus()>& retain){
 if(!current_||!current_track_||!state_.port_result_known||!state_.port.returned||state_.record_bytes>UINT32_MAX-state_.record_offset-20)
 return {GameFsCode::Unavailable,0,"580D4 actual port/record domain unavailable"};
 auto& t=*current_track_;auto& r=t.state;r={};r.ports.fill(-1);
 r.live=r.initialized=true;r.allocation_index=t.allocation_index;t.link.id=int16_t(state_.subindex);r.subindex=t.link.id;r.bank=state_.bank;
 r.record_offset=state_.record_offset;r.body_offset=r.record_offset+20;r.end_offset=r.body_offset+state_.record_bytes;r.factor=state_.selected_factor;
 r.word20=0;r.byte28=r.byte29=0;r.allocated_ports=r.lanes=1;r.ports[0]=int16_t(state_.port_id);r.wordCC=UINT32_MAX;r.wordE4=r.wordE6=0;
 //58060(track,lane0,0): no57EB8/57F58 path is selected.
 r.lane_controls={127,64,0,0,0,127,64};r.lane_initialized=true;
 state_.track_source_initialized=true;
 const auto status=retain();if(!status)return status;
 r.retain_pc=0x55A2C;r.refs_before=2;r.refs_after=3;
 auto* link=&t.link;link->next->previous=link->previous;link->previous->next=link->next;t.free=false;
 link->next=current_->child.next;link->previous=&current_->child;current_->child.next->previous=link;current_->child.next=link;current_->child_head=link;
 current_->link.flags|=2;r.free_unlinked=r.child_linked=true;r.returned=true;r.return_pc=0x58290;
 state_.track_activated=true;state_.frontier=0;return {};
}
GameFsStatus GameSourceSequencePool::Update5A06C(uint64_t elapsed){
 if(!current_||!state_.handler_returned||!state_.command_released||!current_track_||!current_track_->state.returned)
 return {GameFsCode::Unavailable,0,"5A06C actual returned context/track unavailable"};
 state_.update_entered=true;state_.update_pc=0x54FE8;state_.native_elapsed_ns=elapsed;
 //NativeWorkerSequenceDeltaSeconds: real consecutive worker timestamps;
 //this is a Windows timing adaptation, not a recovered PSP clock scale.
 state_.native_delta_seconds=float(double(elapsed)/1000000000.0);
 const float scaled=state_.native_delta_seconds*1000000.0f;
 if(!std::isfinite(scaled)||scaled<0||double(scaled)>double(INT32_MAX)){state_.frontier=0x5A06C;return {GameFsCode::Unavailable,0,"5A06C native delta outside finite trunc.w.s domain"};}
 state_.update_quantum=int32_t(scaled);
 if(current_->link.flags!=3||current_->word1A)return {GameFsCode::Unavailable,0,"5A06C selected current context flags changed"};
 state_.processor_pc=0x5A120;return ProcessContext59DD4(*current_,state_.update_quantum);
}
GameFsStatus GameSourceSequencePool::ProcessContext59DD4(Node& context,int32_t quantum){
 state_.processor_attempted=true;state_.frontier=0x59DD4;state_.processor_context=reinterpret_cast<uintptr_t>(&context);state_.processor_quantum=quantum;
 if(&context!=current_||quantum!=state_.update_quantum||!current_track_||context.child_head!=&current_track_->link||context.child.next!=&current_track_->link||context.child.previous!=&current_track_->link)
  return {GameFsCode::Unavailable,0,"59DD4 actual context/quantum/attached-child ABI unavailable"};
 //Genuine typed consumer entry. No grammar, source body result,5A128
 //context clear or later54FD4 update consumers are implemented here.
 return {GameFsCode::Unavailable,0,"Unsupported selected59DD4(actual live context, actual worker delta quantum) before event6 grammar"};
}
bool GameSourceVoiceExact(const GameSourceVoiceSnapshot& a,const GameSourceVoiceSnapshot& b)noexcept{
 return std::tie(a.live,a.active,a.returned,a.auxiliary_null,a.callback_entered,a.callback_returned,a.sequence_result_known,a.owner,a.generation,a.allocation,a.services,a.id,a.bank,a.sequence_slot,a.flags,a.mode,a.group,a.event,a.extra,a.descriptor,a.bank_refs_before,a.bank_refs_after,a.retain_writer,a.initial_left,a.initial_right,a.initial_dirty_mask,a.initial_port_writes,a.initialized_mask,a.port_writes,a.return_pc,a.callback_pc,a.frontier,a.initializer_store_pc,a.words74,a.words80,a.wordsA0,a.initial_lane_controls,a.initial_lane_flags,a.bank_ids,a.working,a.published,a.consumer,a.dirty,a.persistent,a.published_dirty,a.published_persistent,a.consumer_dirty,a.consumer_persistent,a.master_left,a.master_right)
 ==std::tie(b.live,b.active,b.returned,b.auxiliary_null,b.callback_entered,b.callback_returned,b.sequence_result_known,b.owner,b.generation,b.allocation,b.services,b.id,b.bank,b.sequence_slot,b.flags,b.mode,b.group,b.event,b.extra,b.descriptor,b.bank_refs_before,b.bank_refs_after,b.retain_writer,b.initial_left,b.initial_right,b.initial_dirty_mask,b.initial_port_writes,b.initialized_mask,b.port_writes,b.return_pc,b.callback_pc,b.frontier,b.initializer_store_pc,b.words74,b.words80,b.wordsA0,b.initial_lane_controls,b.initial_lane_flags,b.bank_ids,b.working,b.published,b.consumer,b.dirty,b.persistent,b.published_dirty,b.published_persistent,b.consumer_dirty,b.consumer_persistent,b.master_left,b.master_right)&&GameSourceSequenceExact(a.sequence,b.sequence);
}
bool GameSourceVoicePool::Parameter(uint32_t param,int32_t value){
 if(!state_.live||param<2||param>=12||value<INT16_MIN||value>INT16_MAX)return false;
 const auto bit=uint16_t(1u<<param);if((state_.working[1]&bit)&&int16_t(state_.working[param])==value)return true;
 state_.working[param]=uint16_t(value);state_.working[0]|=bit;state_.working[1]|=bit;++state_.port_writes;return true; //54C38
}
bool GameSourceVoicePool::Gain(int32_t id,int32_t left,int32_t right){
 if(!state_.live||!state_.active||id!=state_.id||!allocated_.at(size_t(id))||(state_.persistent&2))return false;
 left=std::clamp(left,-128,128);right=std::clamp(right,-128,128);
 const auto l=(left*int32_t(state_.master_left))>>7,r=(right*int32_t(state_.master_right))>>7;
 return Parameter(2,std::clamp(l,-128,128)*32)&&Parameter(3,std::clamp(r,-128,128)*32); //56E74/56D34
}
bool GameSourceVoicePool::Rate(int32_t id,float rate){
 if(!state_.live||id!=state_.id||rate<0.01f||rate>2.0f)return false;
 return Parameter(9,int32_t(rate*4096.0f)); //57278 trunc.w.s, actual source operand
}
GameFsStatus GameSourceVoicePool::Allocate(int32_t bank,const std::array<int32_t,4>& ids,uint32_t before,uint32_t after,const std::function<GameFsStatus()>& auxiliary){
 if(state_.live||bank<0||before==UINT32_MAX||after!=before+1)return {GameFsCode::Unavailable,0,"56598 bank retain/voice admission"};
 if(sequences_.Snapshot().live)return {GameFsCode::Unavailable,0,"56598 previous sequence owner still live"};
 sequences_.state_={}; //new owning voice detaches retired observation; pool generation remains monotonic
 int32_t id=-1;for(size_t i=0;i<allocated_.size();++i)if(!allocated_[i]){id=int32_t(i);break;}
 if(id<0)return {GameFsCode::Busy,0,"NativeIndexedVoicePool exhausted"};
 static std::atomic<uint64_t> next{0};const auto generation=state_.generation+1;state_={};state_.owner=++next;state_.generation=generation;state_.allocation=1;
 allocated_[size_t(id)]=true;state_.live=true;state_.id=id;state_.bank=bank;state_.bank_refs_before=before;state_.bank_refs_after=after;state_.retain_writer=0x55A2C;
 state_.flags=3;state_.mode=2;state_.group=0;state_.event=6;state_.descriptor=0x7210;
 const auto lookup=auxiliary();if(!lookup)return lookup; //after owned node initial writes, before mappings/clears
 state_.bank_ids=ids;
 //55BA0 selected group0/event6 record+4==0. NULL is auxiliary+7C.
 state_.words74.fill(0);state_.words80.fill(0);state_.wordsA0.fill(0);state_.initial_lane_controls.fill(0);state_.initial_lane_flags.fill(0);
 state_.auxiliary_null=true;
 const auto zero=[](const auto& a){return std::all_of(a.begin(),a.end(),[](auto x){return x==0;});};
 state_.initialized_mask=(zero(state_.initial_lane_controls)&&zero(state_.initial_lane_flags)?1u:0u)|(zero(state_.words74)?2u:0u)
    |(zero(state_.words80)?4u:0u)|(zero(state_.wordsA0)?8u:0u)|(state_.bank_ids==ids?16u:0u)|(state_.auxiliary_null?32u:0u)|(state_.flags==3?64u:0u); //controls/74/80/A0 cleared, four IDs copied
 state_.dirty=state_.persistent=1;state_.active=true; //actual active ownership before56E74
 if(!Gain(id,128,128))return {GameFsCode::Unavailable,0,"56598 mandatory56E74 port readback"};
 state_.initial_left=state_.working[2];state_.initial_right=state_.working[3];state_.initial_dirty_mask=state_.working[0];state_.initial_port_writes=state_.port_writes;
 //56794 rereads actual node ID after port effects; no callback is invoked here.
 if(!allocated_[size_t(state_.id)])return {GameFsCode::Unavailable,0,"56794 owned node ID readback"};
 state_.returned=true;state_.return_pc=0x567BC;return {};
}
GameFsStatus GameSourceVoicePool::Service(bool advance_sequence,const std::function<GameFsStatus()>& command){
 if(!state_.live||!state_.returned||!state_.active||state_.services)return {GameFsCode::Unavailable,0,"NativeVoiceService retired/replayed"};
 ++state_.services;
 const auto copy=[](auto& dst,const auto& src){const auto mask=src[0];for(uint32_t i=2;i<12;++i)if(mask&(1u<<i))dst[i]=src[i];dst[0]|=mask;dst[1]|=mask;};
 //Named NativeWorkerVoiceService: the source56254 publication precedes562D4.
 copy(state_.published,state_.working);state_.published_dirty=state_.dirty;state_.published_persistent=state_.persistent;state_.working[0]=0;state_.dirty=0;
 copy(state_.consumer,state_.published);state_.consumer_dirty=state_.published_dirty;state_.consumer_persistent=state_.published_persistent;state_.published[0]=0;state_.published_dirty=0;
 if(!(state_.consumer_dirty&1)||!(state_.consumer_persistent&1)||state_.descriptor!=0x7210)return {GameFsCode::Unavailable,0,"562D4 actual first callback guard"};
 return Callback578CC(advance_sequence,command);
}
GameFsStatus GameSourceVoicePool::Callback578CC(bool advance_sequence,const std::function<GameFsStatus()>& command){
 state_.callback_entered=true;state_.callback_pc=0x578CC;
 state_.sequence_slot=-1;state_.words80[0]=UINT32_MAX;state_.initializer_store_pc=0x578D4; //real store BEFORE57CF0, no returned ID
 if(!advance_sequence)return AllocateSequence57CF0(state_.bank_ids,state_.wordsA0); //historical explicit unsupported domain
 const auto allocated=sequences_.Allocate(state_.bank_ids,state_.wordsA0,state_.owner,state_.generation);if(!allocated)return allocated;
 const auto context=sequences_.Snapshot();state_.sequence_slot=context.id;state_.words80[0]=uint32_t(context.id);state_.sequence_result_known=true;
 sequences_.state_.result_store_pc=0x57908;
 const auto status=command();state_.frontier=sequences_.Snapshot().frontier;
 if(!status)return status;
 state_.callback_returned=true;return {};
}
GameFsStatus GameSourceVoicePool::AllocateSequence57CF0(const std::array<int32_t,4>& ids,std::array<uint32_t,8>& output){
 if(&ids!=&state_.bank_ids||&output!=&state_.wordsA0||!state_.live||!state_.active||!state_.callback_entered)
 return {GameFsCode::Unavailable,0,"57CF0 actual voice-owned argument storage unavailable"};
 state_.frontier=0x57CF0;
 //No source result is supplied; neither57908 nor58C04 may execute.
 return {GameFsCode::Unavailable,0,"Unsupported selected57CF0(four actual bank IDs, voice+A0 output)"};
}
void GameSourceVoicePool::Close()noexcept{sequences_.Close();allocated_.fill(false);state_.live=state_.active=false;}
}
