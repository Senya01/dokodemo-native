#include "game_sound_voice_native.h"
#include <algorithm>
#include <atomic>
#include <tuple>
#include <cmath>
#include <limits>
#include <new>
namespace dokodemo::recovered {
bool GameSourcePortExact(const GameSourcePortSnapshot& a,const GameSourcePortSnapshot& b)noexcept{
 return std::tie(a.live,a.active,a.returned,a.free_unlinked,a.active_linked,a.owner,a.generation,a.allocation,a.known_halfwords,a.storage,a.id,a.flags,a.initialize_pc,a.return_pc,a.sentinel_ids,a.sentinel_flags,a.sentinel_self,a.parameters,a.zero70)==std::tie(b.live,b.active,b.returned,b.free_unlinked,b.active_linked,b.owner,b.generation,b.allocation,b.known_halfwords,b.storage,b.id,b.flags,b.initialize_pc,b.return_pc,b.sentinel_ids,b.sentinel_flags,b.sentinel_self,b.parameters,b.zero70);
}
bool GameSourceTrackExact(const GameSourceTrackSnapshot& a,const GameSourceTrackSnapshot& b)noexcept{
 return std::tie(a.live,a.initialized,a.free_unlinked,a.child_linked,a.lane_initialized,a.returned,a.storage,a.context_storage,a.allocation_index,a.subindex,a.bank,a.record_offset,a.body_offset,a.end_offset,a.cursor_offset,a.word20,a.wordCC,a.wordE4,a.wordE6,a.byte28,a.byte29,a.allocated_ports,a.lanes,a.retain_pc,a.refs_before,a.refs_after,a.return_pc,a.factor,a.ports,a.lane_controls)==std::tie(b.live,b.initialized,b.free_unlinked,b.child_linked,b.lane_initialized,b.returned,b.storage,b.context_storage,b.allocation_index,b.subindex,b.bank,b.record_offset,b.body_offset,b.end_offset,b.cursor_offset,b.word20,b.wordCC,b.wordE4,b.wordE6,b.byte28,b.byte29,b.allocated_ports,b.lanes,b.retain_pc,b.refs_before,b.refs_after,b.return_pc,b.factor,b.ports,b.lane_controls);
}
bool GameSourceProcessorExact(const GameSourceProcessorSnapshot& a,const GameSourceProcessorSnapshot& b)noexcept{
 return std::tie(a.entered,a.sentinel_terminated,a.controls_read,a.kind_dispatched,a.opcode_read,a.operands_returned,a.control_attempted,a.control_returned,a.kind_returned,a.context_returned,a.context_storage,a.track_storage,a.command_storage,a.sentinel_pc,a.dispatch_pc,a.kind_target,a.opcode_pc,a.opcode,a.cursor_before,a.cursor_after,a.body_end,a.effective_flags,a.context_word18,a.track_e4,a.track_e6,a.byte29,a.countdown,a.operand_count,a.control_pc,a.frontier,a.quantum,a.port_id,a.operands,a.operand_begin,a.operand_end,a.operand_call,a.bytes,a.command)
 ==std::tie(b.entered,b.sentinel_terminated,b.controls_read,b.kind_dispatched,b.opcode_read,b.operands_returned,b.control_attempted,b.control_returned,b.kind_returned,b.context_returned,b.context_storage,b.track_storage,b.command_storage,b.sentinel_pc,b.dispatch_pc,b.kind_target,b.opcode_pc,b.opcode,b.cursor_before,b.cursor_after,b.body_end,b.effective_flags,b.context_word18,b.track_e4,b.track_e6,b.byte29,b.countdown,b.operand_count,b.control_pc,b.frontier,b.quantum,b.port_id,b.operands,b.operand_begin,b.operand_end,b.operand_call,b.bytes,b.command);
}
bool GameSourceControlExact(const GameSourceControlSnapshot& a,const GameSourceControlSnapshot& b)noexcept{
 return std::tie(a.entered,a.lookup_returned,a.command_dispatched,a.prepare_entered,a.prepare_returned,a.bank_retained,a.region_returned,a.region_selected,a.child_entered,a.wave_attempted,a.wave_output_known,a.wave_result_known,a.wave_returned,a.child_returned,a.handler_returned,a.control_returned,a.port_storage,a.command_storage,a.wave_output_storage,a.port_owner,a.port_generation,a.port_id,a.note,a.bank,a.program,a.lookup_pc,a.source_stride,a.pool_count,a.lookup_flags,a.dispatch_pc,a.handler_target,a.prepare_pc,a.prepare_return_pc,a.retain_pc,a.retain_writer,a.refs_before,a.refs_after,a.rgnd_base,a.group_count,a.region_count,a.region_first,a.region_offset,a.walk_count,a.stride_reads,a.selected_flags,a.selected_byteC,a.selected_byteD,a.selected_wave,a.child_call_pc,a.child_target,a.wave_call_pc,a.frontier,a.offsets,a.strides,a.lows,a.highs,a.child_args)
 ==std::tie(b.entered,b.lookup_returned,b.command_dispatched,b.prepare_entered,b.prepare_returned,b.bank_retained,b.region_returned,b.region_selected,b.child_entered,b.wave_attempted,b.wave_output_known,b.wave_result_known,b.wave_returned,b.child_returned,b.handler_returned,b.control_returned,b.port_storage,b.command_storage,b.wave_output_storage,b.port_owner,b.port_generation,b.port_id,b.note,b.bank,b.program,b.lookup_pc,b.source_stride,b.pool_count,b.lookup_flags,b.dispatch_pc,b.handler_target,b.prepare_pc,b.prepare_return_pc,b.retain_pc,b.retain_writer,b.refs_before,b.refs_after,b.rgnd_base,b.group_count,b.region_count,b.region_first,b.region_offset,b.walk_count,b.stride_reads,b.selected_flags,b.selected_byteC,b.selected_byteD,b.selected_wave,b.child_call_pc,b.child_target,b.wave_call_pc,b.frontier,b.offsets,b.strides,b.lows,b.highs,b.child_args);
}
bool GameSourceWaveExact(const GameSourceWaveSnapshot& a,const GameSourceWaveSnapshot& b)noexcept{
 return std::tie(a.entered,a.bank_lookup_returned,a.wave_chunk_known,a.count_read,a.mode_read,a.output_written,a.output_readback,a.result_known,a.result_null,a.returned,a.borrow_live,a.child_continued,a.port_word24_attempted,a.port_word24_known,a.port_word24_returned,a.output_storage,a.control_slot_storage,a.output_value,a.result_pointer,a.port_storage,a.registry,a.registry_generation,a.registry_serial,a.image_owner,a.image_generation,a.image_serial,a.image_offset,a.image_host_address,a.bank,a.wave,a.port_id,a.byte8,a.byte9,a.channels,a.lookup_pc,a.wave_load_pc,a.count_load_pc,a.mode_load_pc,a.output_store_pc,a.return_pc,a.return_delay_pc,a.wave_payload,a.wave_size,a.wave_count,a.stride,a.record_offset,a.control_mode,a.record_flags,a.record_read_mask,a.word10,a.word14,a.word1C,a.stack_known,a.stack_writes,a.port_read_pc,a.frontier,a.parameters,a.store_pc,a.store_index)==std::tie(b.entered,b.bank_lookup_returned,b.wave_chunk_known,b.count_read,b.mode_read,b.output_written,b.output_readback,b.result_known,b.result_null,b.returned,b.borrow_live,b.child_continued,b.port_word24_attempted,b.port_word24_known,b.port_word24_returned,b.output_storage,b.control_slot_storage,b.output_value,b.result_pointer,b.port_storage,b.registry,b.registry_generation,b.registry_serial,b.image_owner,b.image_generation,b.image_serial,b.image_offset,b.image_host_address,b.bank,b.wave,b.port_id,b.byte8,b.byte9,b.channels,b.lookup_pc,b.wave_load_pc,b.count_load_pc,b.mode_load_pc,b.output_store_pc,b.return_pc,b.return_delay_pc,b.wave_payload,b.wave_size,b.wave_count,b.stride,b.record_offset,b.control_mode,b.record_flags,b.record_read_mask,b.word10,b.word14,b.word1C,b.stack_known,b.stack_writes,b.port_read_pc,b.frontier,b.parameters,b.store_pc,b.store_index);
}
bool GameSourcePortInputExact(const GameSourcePortInputSnapshot& a,const GameSourcePortInputSnapshot& b)noexcept{
 return std::tie(a.entered,a.allocation_live,a.zeroed,a.bound,a.read_known,a.read_returned,a.packet_complete,a.child_entered,a.child_returned,a.block_owner,a.block_generation,a.block_serial,a.sound_owner,a.sound_generation,a.port_owner,a.port_generation,a.block_base,a.node_address,a.port_storage,a.packet_storage,a.mode_slot,a.bytes,a.count,a.stride,a.zero_writes,a.zero_readbacks,a.index,a.flags,a.read_pc,a.read_offset,a.value,a.stack_known,a.stack_writes,a.child_pc,a.child_delay_pc,a.frontier,a.port_id,a.note,a.bank,a.channels,a.words,a.store_index,a.store_pc)==std::tie(b.entered,b.allocation_live,b.zeroed,b.bound,b.read_known,b.read_returned,b.packet_complete,b.child_entered,b.child_returned,b.block_owner,b.block_generation,b.block_serial,b.sound_owner,b.sound_generation,b.port_owner,b.port_generation,b.block_base,b.node_address,b.port_storage,b.packet_storage,b.mode_slot,b.bytes,b.count,b.stride,b.zero_writes,b.zero_readbacks,b.index,b.flags,b.read_pc,b.read_offset,b.value,b.stack_known,b.stack_writes,b.child_pc,b.child_delay_pc,b.frontier,b.port_id,b.note,b.bank,b.channels,b.words,b.store_index,b.store_pc);
}
bool GameSourceSequenceExact(const GameSourceSequenceSnapshot& a,const GameSourceSequenceSnapshot& b)noexcept{
 return std::tie(a.live,a.active,a.allocation_returned,a.lookup_returned,a.clear_returned,a.command_entered,a.bank_retained,a.record_lookup_returned,a.track_reserved,a.track_source_initialized,a.track_activated,a.port_attempted,a.port_result_known,a.handler_returned,a.owner,a.generation,a.allocation,a.voice_owner,a.voice_generation,a.ids_storage,a.output_storage,a.id,a.bank,a.sentinel_id,a.track_id,a.port_id,a.flags,a.sentinel_flags,a.allocation_pc,a.return_pc,a.result_store_pc,a.lookup_pc,a.clear_pc,a.command,a.subindex,a.group,a.event,a.command_pc,a.retain_writer,a.refs_before,a.refs_after,a.record_offset,a.record_kind,a.record_divisor,a.record_bytes,a.track_pool_stride,a.port_call_pc,a.frontier,a.initial_factor,a.selected_factor,a.sentinel_self,a.child_head_same,a.output_unchanged,a.bank_ids,a.output_before,a.output_after,a.admission_returned,a.command_released,a.update_entered,a.processor_attempted,a.current_refs,a.release_pc,a.release_before,a.release_after,a.update_pc,a.processor_pc,a.native_elapsed_ns,a.worker_tick_serial,a.worker_previous_ns,a.worker_current_ns,a.native_delta_seconds,a.update_quantum,a.processor_quantum,a.processor_context)
 ==std::tie(b.live,b.active,b.allocation_returned,b.lookup_returned,b.clear_returned,b.command_entered,b.bank_retained,b.record_lookup_returned,b.track_reserved,b.track_source_initialized,b.track_activated,b.port_attempted,b.port_result_known,b.handler_returned,b.owner,b.generation,b.allocation,b.voice_owner,b.voice_generation,b.ids_storage,b.output_storage,b.id,b.bank,b.sentinel_id,b.track_id,b.port_id,b.flags,b.sentinel_flags,b.allocation_pc,b.return_pc,b.result_store_pc,b.lookup_pc,b.clear_pc,b.command,b.subindex,b.group,b.event,b.command_pc,b.retain_writer,b.refs_before,b.refs_after,b.record_offset,b.record_kind,b.record_divisor,b.record_bytes,b.track_pool_stride,b.port_call_pc,b.frontier,b.initial_factor,b.selected_factor,b.sentinel_self,b.child_head_same,b.output_unchanged,b.bank_ids,b.output_before,b.output_after,b.admission_returned,b.command_released,b.update_entered,b.processor_attempted,b.current_refs,b.release_pc,b.release_before,b.release_after,b.update_pc,b.processor_pc,b.native_elapsed_ns,b.worker_tick_serial,b.worker_previous_ns,b.worker_current_ns,b.native_delta_seconds,b.update_quantum,b.processor_quantum,b.processor_context)&&GameSourcePortExact(a.port,b.port)&&GameSourceTrackExact(a.track,b.track)&&GameSourceProcessorExact(a.processor,b.processor)&&GameSourceControlExact(a.control,b.control)&&GameSourceWaveExact(a.wave,b.wave)&&GameSourcePortInputExact(a.port_input,b.port_input);
}
GameSourceSequencePool::GameSourceSequencePool(){Close();}
void GameSourceSequencePool::Close()noexcept{
 //Retire the issuing binding before semantic nodes or allocation disappear.
 port_source_binding_=nullptr;port_binding_owner_=port_binding_generation_=0;port_zero_writes_=port_zero_readbacks_=0;
 port_source_owner_=port_sound_owner_=port_sound_generation_=0;port_source_.reset();child_packet_={};
 state_.port_input.allocation_live=state_.port_input.bound=false;
 const auto head=[](Link& h){h={};h.next=h.previous=&h;};
 head(free_);head(active_);head(track_free_);head(port_free_);head(port_active_);
 const auto add=[](Link& h,Link& n){n.next=h.next;n.previous=&h;h.next->previous=&n;h.next=&n;};
 for(size_t i=nodes_.size();i-->0;){auto& n=nodes_[i];n={};n.link.id=int16_t(i);add(free_,n.link);
  auto& t=tracks_[i];t={};t.allocation_index=int16_t(i);t.link.id=int16_t(i);add(track_free_,t.link);
  auto& p=ports_[i];p={};p.link.id=int16_t(i);add(port_free_,p.link);}
 wave_view_.Reset();state_.wave.borrow_live=false;
 current_=nullptr;current_port_=nullptr;current_track_=nullptr;state_.live=state_.active=false;state_.port.live=state_.port.active=false;state_.track.live=false;
}
GameFsStatus GameSourceSequencePool::InitializeNativePortComponent(uint64_t owner,uint64_t generation){
 if(port_source_||current_port_||!owner||!generation)return {GameFsCode::Unavailable,0,"NativeOwnedZeroedPortPool16 cold owner unavailable"};
 //Windows owning allocator replaces the opaque C21D0/C244C heap contract.
 //Original PSP configured capacity/heap/alignment are not claimed.
 std::unique_ptr<uint32_t[]> block(new(std::nothrow) uint32_t[512]);
 if(!block)return {GameFsCode::Unavailable,0,"NativeOwnedZeroedPortPool16 real allocation failed"};
 //5478C clear=1 -> aligned54878: explicit writes, not value-initialization.
 port_zero_writes_=port_zero_readbacks_=0;
 for(size_t i=0;i<512;++i){block[i]=0;++port_zero_writes_;}
 for(size_t i=0;i<512;++i){if(block[i]!=0)return {GameFsCode::Unavailable,0,"NativeOwnedZeroedPortPool16 zero readback"};++port_zero_readbacks_;}
 static std::atomic<uint64_t> next{0};port_source_owner_=++next;++port_source_generation_;++port_source_serial_;
 port_sound_owner_=owner;port_sound_generation_=generation;port_source_=std::move(block);return {};
}
GameSourceSequenceSnapshot GameSourceSequencePool::Snapshot()const{
 auto out=state_;
 if(out.wave.entered){out.wave.borrow_live=false;try{const auto d=wave_view_.Describe();out.wave.borrow_live=d.owner==out.wave.image_owner&&d.generation==out.wave.image_generation&&d.serial==out.wave.image_serial&&d.host_address+d.offset==out.wave.result_pointer;}catch(const std::exception&){} }
 if(!current_)return out;
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
 r.id=p.link.id;r.returned=true;state_.port_result_known=true;state_.port_id=r.id;
 if(port_source_){port_source_binding_=&p;port_binding_owner_=r.owner;port_binding_generation_=r.generation;port_binding_index_=uint32_t(id);}
 return {};
}
GameFsStatus GameSourceSequencePool::CompleteTrack580D4(const std::function<GameFsStatus()>& retain){
 if(!current_||!current_track_||!state_.port_result_known||!state_.port.returned||state_.record_bytes>UINT32_MAX-state_.record_offset-20)
 return {GameFsCode::Unavailable,0,"580D4 actual port/record domain unavailable"};
 auto& t=*current_track_;auto& r=t.state;r={};r.ports.fill(-1);
 r.live=r.initialized=true;r.allocation_index=t.allocation_index;t.link.id=int16_t(state_.subindex);r.subindex=t.link.id;r.bank=state_.bank;
 r.record_offset=state_.record_offset;r.body_offset=r.record_offset+20;r.end_offset=r.body_offset+state_.record_bytes;r.cursor_offset=r.body_offset;r.factor=state_.selected_factor;
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
GameFsStatus GameSourceSequencePool::Update5A06C(uint64_t elapsed,const ReadByte& read,const ControlCall& control){
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
 state_.processor_pc=0x5A120;return ProcessContext59DD4(*current_,state_.update_quantum,read,control);
}
GameFsStatus GameSourceSequencePool::ProcessContext59DD4(Node& context,int32_t quantum,const ReadByte& read,const ControlCall& control){
 state_.processor_attempted=true;state_.frontier=0x59DD4;state_.processor_context=reinterpret_cast<uintptr_t>(&context);state_.processor_quantum=quantum;
 if(&context!=current_||quantum!=state_.update_quantum||!current_track_||context.child_head!=&current_track_->link||context.child.next!=&current_track_->link||context.child.previous!=&current_track_->link)
  return {GameFsCode::Unavailable,0,"59DD4 actual context/quantum/attached-child ABI unavailable"};
 if(read)return ProcessKind59168(context,*current_track_,quantum,read,control);
 //Historical59DD4 entry-only domain remains separate from processor advance.
 return {GameFsCode::Unavailable,0,"Unsupported selected59DD4(actual live context, actual worker delta quantum) before event6 grammar"};
}
GameFsStatus GameSourceSequencePool::ProcessKind59168(Node& context,Track& track,int32_t quantum,const ReadByte& read,const ControlCall& control){
 auto& p=state_.processor;auto& t=track.state;p.entered=true;p.context_storage=reinterpret_cast<uintptr_t>(&context);p.track_storage=reinterpret_cast<uintptr_t>(&track);p.quantum=quantum;
 p.cursor_before=t.cursor_offset;p.cursor_after=t.cursor_offset;p.body_end=t.end_offset;
 if(track.link.id<0||track.link.previous!=&context.child||track.link.next!=&context.child||context.child.id>=0||context.child.flags||context.child.previous!=&track.link)
  return {GameFsCode::Unavailable,0,"59DD4 actual child/sentinel topology changed"};
 p.sentinel_terminated=true;p.sentinel_pc=0x5A05C;
 p.context_word18=context.word18;p.track_e4=t.wordE4;p.track_e6=t.wordE6;p.effective_flags=uint16_t(context.word18|t.wordE4);p.controls_read=true;
 if(p.effective_flags||p.track_e6)return {GameFsCode::Unavailable,0,"59DD4 unimplemented current control flags"};
 try{
  const auto kind=int16_t(uint16_t(read(t.record_offset+8))|(uint16_t(read(t.record_offset+9))<<8));
  if(kind!=0||uint16_t(kind)!=state_.record_kind||t.record_offset!=state_.record_offset)
   return {GameFsCode::Unavailable,0,"59F38 current owning record kind changed"};
 }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
 p.kind_dispatched=true;p.dispatch_pc=0x59F50;p.kind_target=0x59168;p.byte29=t.byte29;p.countdown=t.word20;
 if(t.byte29||int32_t(t.word20)!=0)return {GameFsCode::Unavailable,0,"59168 selected first child branch changed"};
 const auto byte=[&](uint32_t at){if(at<t.body_offset||at>=t.end_offset)throw std::runtime_error("59168 owning body bounds");return read(at);};
 try{
  //5923C/59250: the real command byte is consumed before its dispatch.
  p.opcode=byte(t.cursor_offset);p.opcode_pc=0x5923C;p.opcode_read=true;p.bytes[0]=uint8_t(p.opcode);++t.cursor_offset;p.cursor_after=t.cursor_offset;
  if(p.opcode!=0xD0)return {GameFsCode::Unavailable,0,"Unsupported selected59168 opcode before D0 effect"};
  const uint32_t calls[]={0x5935C,0x5936C,0x5937C,0x5938C};
  for(uint32_t i=0;i<4;++i){
   p.operand_begin[i]=t.cursor_offset;p.operand_call[i]=calls[i];
   const auto tag=byte(t.cursor_offset);if((tag&0xF0)!=0)return {GameFsCode::Unavailable,0,"Unsupported58DA4 operand encoding"};
   const uint32_t count=tag&15u;if(count>4||t.cursor_offset-t.body_offset+1+count>p.bytes.size())return {GameFsCode::Unavailable,0,"58D4C selected operand extent"};
   p.bytes[t.cursor_offset-t.body_offset]=tag;++t.cursor_offset;p.cursor_after=t.cursor_offset; //58DD8 tag store
   //58D4C advances a LOCAL pointer; a failed payload read cannot publish it.
   auto cursor=t.cursor_offset;uint32_t value=0;
   for(uint32_t j=0;j<count;++j){const auto v=byte(cursor);p.bytes[cursor-t.body_offset]=v;value=value*100u+10u*(v>>4)+(v&15u);++cursor;}
   t.cursor_offset=cursor;p.operands[i]=int32_t(value);p.operand_end[i]=cursor;p.cursor_after=cursor;++p.operand_count; //58E64 only after helper return
  }
  p.operands_returned=true;return Control5900C(context,track,control);
 }catch(const std::exception& e){p.cursor_after=t.cursor_offset;return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameSourceSequencePool::Control5900C(Node& context,Track& track,const ControlCall& control){
 auto& p=state_.processor;
 if(&context!=current_||&track!=current_track_||!context.ids)
  return {GameFsCode::Unavailable,0,"5900C current context ID storage unavailable"};
 command_={0,uint32_t(p.operands[0]),uint32_t((*context.ids)[0]),uint32_t(p.operands[1]),uint32_t(p.operands[2]),uint32_t(p.operands[3])};
 p.command=command_;p.command_storage=reinterpret_cast<uintptr_t>(&command_);p.port_id=int16_t(track.state.ports[0]);p.control_pc=0x5903C;
 if(!current_port_||p.port_id<0||size_t(p.port_id)>=ports_.size()||&ports_[size_t(p.port_id)]!=current_port_||current_port_->link.id!=p.port_id||!(current_port_->link.flags&1))
  return {GameFsCode::Unavailable,0,"5900C actual signed port owner unavailable"};
 return Submit5B9E4(*current_port_,command_,control);
}
GameFsStatus GameSourceSequencePool::Submit5B9E4(Port& port,const std::array<uint32_t,6>& command,const ControlCall& control){
 auto& p=state_.processor;p.control_attempted=true;p.frontier=state_.frontier=0x5B9E4;
 if(&port!=current_port_||&command!=&command_||command!=p.command||p.command_storage!=reinterpret_cast<uintptr_t>(&command))
  return {GameFsCode::Unavailable,0,"5B9E4 actual port/command ABI changed"};
 if(control)return control();
 return {GameFsCode::Unavailable,0,"Unsupported selected5B9E4(actual owned port, current D0 command) before control body"};
}
GameSourceSequencePool::Port* GameSourceSequencePool::LookupPort5A1B4(int32_t id){
 auto& r=state_.control;r.lookup_pc=0x5A1B4;r.source_stride=0x80;r.pool_count=uint32_t(ports_.size());r.port_id=id;
 if(id<0||size_t(id)>=ports_.size())return nullptr;
 auto& port=ports_[size_t(id)];r.lookup_flags=port.link.flags;
 //Native pool index/ID coherence is an owning initialization invariant;
 //source lookup additionally requires only the active bit of flags+A.
 if(&port!=current_port_||port.link.id!=id||!(port.link.flags&1))return nullptr;
 r.lookup_returned=true;r.port_storage=reinterpret_cast<uintptr_t>(&port);r.port_owner=state_.port.owner;r.port_generation=state_.port.generation;return &port;
}
GameFsStatus GameSourceSequencePool::Dispatch5B9E4(uint32_t rgnd,uint32_t size,const ReadByte& read,const ControlCall& retain,const WaveCall& wave,const ControlCall& port_input){
 auto& r=state_.control;r.entered=true;r.command_storage=reinterpret_cast<uintptr_t>(&command_);r.frontier=state_.frontier=0x5B9E4;
 auto* port=LookupPort5A1B4(int16_t(current_track_->state.ports[0]));
 if(!port||command_[0]>=9)return {GameFsCode::Unavailable,0,"5B9E4 current owned port/command lookup unavailable"};
 if(command_[0]!=0)return {GameFsCode::Unavailable,0,"Unsupported5B9E4 command before selected handler"};
 r.command_dispatched=true;r.dispatch_pc=0x5BA44;r.handler_target=0x5B2F4;
 return Command5B2F4(*port,rgnd,size,read,retain,wave,port_input);
}
GameFsStatus GameSourceSequencePool::Prepare5B040(Port& port,int32_t note){
 auto& r=state_.control;r.prepare_entered=true;r.prepare_pc=0x5B33C;r.note=note;
 auto* head=port.child0.previous;
 if(&port!=current_port_||head!=&port.child0||head->id!=-1||head->flags||head->previous!=head||head->next!=head)
  return {GameFsCode::Unavailable,0,"5B040 selected fresh owned child0 sentinel unavailable"};
 //5B0BC zeroes a LOCAL pointer. No source port link/parameter is written.
 r.prepare_returned=true;r.prepare_return_pc=0x5B09C;return {};
}
GameFsStatus GameSourceSequencePool::Command5B2F4(Port& port,uint32_t rgnd,uint32_t size,const ReadByte& read,const ControlCall& retain,const WaveCall& wave,const ControlCall& port_input){
 auto& r=state_.control;r.bank=int32_t(command_[2]);r.program=int32_t(command_[3]);
 auto status=Prepare5B040(port,int32_t(command_[1]));if(!status)return status;
 status=retain();if(!status)return status; //actual2->3 readback precedes region helper
 const auto end=uint64_t(rgnd)+size;
 const auto u8=[&](uint32_t at){if(at<rgnd||uint64_t(at)>=end)throw std::runtime_error("55850 owning RGND bounds");return read(at);};
 const auto u32=[&](uint32_t at){uint32_t v=0;for(uint32_t i=0;i<4;++i)v|=uint32_t(u8(at+i))<<(8*i);return v;};
 try{
  r.rgnd_base=rgnd;r.group_count=u32(rgnd+4);
  if(r.program<0||uint32_t(r.program)>=r.group_count)return {GameFsCode::Unavailable,0,"55850 actual RGND program bounds"};
  const uint64_t directory=uint64_t(rgnd)+8+8*uint32_t(r.program);if(directory+8>end)return {GameFsCode::Unavailable,0,"55850 RGND directory extent"};
  r.region_count=u32(uint32_t(directory));r.region_first=u32(uint32_t(directory+4));
  if(!r.region_count||int32_t(r.region_count)<=0||!r.region_first||int32_t(r.region_first)<=0)return {GameFsCode::Unavailable,0,"55850 selected positive region unavailable"};
  //Returned pointer is represented as its current owning bank-relative offset.
  (void)u8(r.region_first);r.region_returned=true;uint32_t at=r.region_first;
  for(uint32_t i=0;i<r.region_count;++i){
   if(i>=r.offsets.size())return {GameFsCode::Unavailable,0,"5B2F4 selected finite RGND walk extent"};
   r.offsets[i]=at;r.lows[i]=int8_t(u8(at+24));r.highs[i]=int8_t(u8(at+25));++r.walk_count;
   if(int32_t(command_[4])>=r.lows[i]&&int32_t(command_[4])<=r.highs[i]){
    r.region_selected=true;r.region_offset=at;r.selected_byteD=u8(at+13);r.selected_flags=u32(at);r.selected_byteC=u8(at+12);r.selected_wave=u32(at+52);
    if(int8_t(r.selected_byteD)>0||(r.selected_flags&4))return {GameFsCode::Unavailable,0,"5B2F4 unimplemented selected packed/override field"};
    r.child_call_pc=0x5B494;r.child_target=0x5B130;
    return Child5B130(port,r.note,r.bank,r.selected_wave,int8_t(r.selected_byteC),0,-1,wave,port_input);
   }
   const auto stride=u32(at+8);r.strides[i]=stride;++r.stride_reads;
   if(!stride||stride>UINT32_MAX-at)return {GameFsCode::Unavailable,0,"5B2F4 RGND stride overflow"};at+=stride;
  }
  return {GameFsCode::Unavailable,0,"5B2F4 selected region match unavailable before source release"};
 }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameSourceSequencePool::Child5B130(Port& port,int32_t note,int32_t bank,uint32_t wave,int32_t byteC,int32_t packed,int32_t extra,const WaveCall& lookup,const ControlCall& port_input){
 auto& r=state_.control;r.child_entered=true;r.child_args={port.link.id,note,bank,int32_t(wave),byteC,packed,extra};
 if(&port!=current_port_||bank!=state_.bank)return {GameFsCode::Unavailable,0,"5B130 actual port/bank argument owner changed"};
 r.wave_call_pc=0x5B178;const auto status=Wave557B8(bank,wave,wave_output_,lookup);
 if(!status)return status;return ContinueChild5B180(port,byteC,extra,port_input);
}
GameFsStatus GameSourceSequencePool::Wave557B8(int32_t bank,uint32_t wave,uintptr_t& output,const WaveCall& lookup){
 auto& r=state_.control;r.wave_attempted=true;r.wave_output_storage=reinterpret_cast<uintptr_t>(&output);r.frontier=state_.frontier=0x557B8;
 if(&output!=&wave_output_||bank!=state_.bank||wave!=r.selected_wave)return {GameFsCode::Unavailable,0,"557B8 actual bank/wave/output ABI changed"};
 if(lookup)return lookup(bank,wave,output);
 //Output backing exists, but its source pointee remains UNKNOWN until real writes.
 return {GameFsCode::Unavailable,0,"Unsupported selected557B8(actual bank, wave30, owned output) before WAVE lookup result"};
}
GameFsStatus GameSourceSequencePool::ContinueChild5B180(Port& port,int32_t byteC,int32_t extra,const ControlCall& port_input){
 auto& w=state_.wave;auto& c=state_.control;w.child_continued=true;w.port_storage=reinterpret_cast<uintptr_t>(&port);w.port_id=port.link.id;
 if(w.result_null||!w.result_pointer){w.frontier=c.frontier=state_.frontier=0x5B184;return {GameFsCode::Unavailable,0,"Unsupported Child5B130 null-result return path after genuine557B8 return"};}
 try{
  const auto d=wave_view_.Describe();if(d.host_address+d.offset!=w.result_pointer||&port!=current_port_)throw std::runtime_error("5B180 actual wave/port borrow changed");
  w.record_flags=wave_view_.ReadLE32(0);w.record_read_mask|=1;
  w.byte9=int8_t(wave_view_.ReadU8(9));w.record_read_mask|=2;
  const int32_t s0=(w.record_flags&1)?w.byte9:1,v1=(w.record_flags&1)?1:w.byte9;
  const auto store=[&](uint32_t index,int32_t value,uint32_t pc){child_packet_.words[index]=uint32_t(value);w.parameters[index]=value;w.stack_known|=1u<<index;w.store_index.at(w.stack_writes)=index;w.store_pc.at(w.stack_writes)=pc;++w.stack_writes;};
  store(1,byteC,0x5B1A4);store(0,c.child_args[5],0x5B1B0);
  int32_t format=2,t2=extra;
  if(extra<0){w.byte8=int8_t(wave_view_.ReadU8(8));w.record_read_mask|=4;format=w.byte8;}
  store(2,format,0x5B1B8);store(3,v1,0x5B1C0);
  if(extra<0){w.word10=wave_view_.ReadLE32(0x10);w.record_read_mask|=8;t2=int32_t(w.word10);}
  store(4,t2,0x5B1C4);w.word14=wave_view_.ReadLE32(0x14);w.record_read_mask|=16;store(6,0,0x5B1D4);store(5,int32_t(w.word14),0x5B1E0);
  w.word1C=wave_view_.ReadLE32(0x1C);w.record_read_mask|=32;
  w.channels=s0; //actual a3 register, not a child allocation result
  if(port_input)return port_input();
  return ReadPortWord24(port);
 }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameSourceSequencePool::ReadPortWord24(Port& port){
 auto& w=state_.wave;w.port_word24_attempted=true;w.port_read_pc=w.frontier=state_.control.frontier=state_.frontier=0x5B1EC;
 if(&port!=current_port_||port.link.id!=state_.port.id||!(port.link.flags&1))return {GameFsCode::Unavailable,0,"5B1EC actual port owner changed"};
 //No source writer for this node gap is proven. Do not read host backing0.
 return {GameFsCode::Unavailable,0,"Unsupported source PORT+0x24 read5B1EC: writer/value UNKNOWN after genuine WAVE30 return"};
}
GameFsStatus GameSourceSequencePool::ReadProducedPort24(){
 auto& a=state_.port_input;auto& w=state_.wave;a.entered=true;a.read_pc=0x5B1EC;
 w.port_word24_attempted=true;w.port_read_pc=w.frontier=state_.control.frontier=state_.frontier=0x5B1EC;
 auto* p=current_port_;
 if(!p||!port_source_||!port_source_owner_||!port_sound_owner_||!port_sound_generation_||port_source_binding_!=p
    ||port_binding_index_>=ports_.size()||&ports_[port_binding_index_]!=p||p->link.id!=int32_t(port_binding_index_)
    ||p->link.id!=state_.port.id||p->link.flags!=1||port_binding_owner_!=state_.port.owner||port_binding_generation_!=state_.port.generation
    ||p->link.previous!=&port_active_||port_active_.next!=&p->link||!w.borrow_live||!w.output_readback||wave_output_!=w.control_slot_storage)
  return {GameFsCode::Unavailable,0,"5B1EC private produced PORT24 binding unavailable"};
 a.block_owner=port_source_owner_;a.block_generation=port_source_generation_;a.block_serial=port_source_serial_;
 a.sound_owner=port_sound_owner_;a.sound_generation=port_sound_generation_;a.port_owner=port_binding_owner_;a.port_generation=port_binding_generation_;
 a.block_base=reinterpret_cast<uintptr_t>(port_source_.get());a.bytes=2048;a.count=16;a.stride=128;a.zero_writes=port_zero_writes_;a.zero_readbacks=port_zero_readbacks_;
 a.index=port_binding_index_;a.read_offset=a.index*128+0x24;a.node_address=a.block_base+a.index*128;
 a.port_storage=reinterpret_cast<uintptr_t>(p);a.port_id=p->link.id;a.flags=p->link.flags;a.allocation_live=a.zeroed=a.bound=true;
 //Only this source word is read. The sidecar is not a byte-matching C++ Port.
 a.value=port_source_[a.read_offset/4];
 if(port_source_binding_!=p||port_binding_owner_!=state_.port.owner||port_binding_generation_!=state_.port.generation||p->link.id!=a.port_id||p->link.flags!=a.flags
    ||port_source_[a.read_offset/4]!=a.value)return {GameFsCode::Unavailable,0,"5B1EC actual produced word readback changed"};
 a.read_known=a.read_returned=true;w.port_word24_known=w.port_word24_returned=true;
 for(size_t i=0;i<7;++i)if(child_packet_.words[i]!=uint32_t(w.parameters[i]))return {GameFsCode::Unavailable,0,"5B1EC actual source packet prefix readback changed"};
 const auto store=[&](uint32_t index,uint32_t value,uint32_t pc){child_packet_.words[index]=value;a.store_index.at(a.stack_writes)=index;a.store_pc.at(a.stack_writes)=pc;++a.stack_writes;};
 store(7,w.word1C,0x5B1F0);store(8,wave_view_.ReadLE32(0x20),0x5B1F8);store(9,wave_view_.ReadLE32(0x24),0x5B200);
 const auto word28=wave_view_.ReadLE32(0x28); //5B204 load precedes5B208 store
 store(11,0,0x5B208);store(10,word28,0x5B20C);
 const auto mode_slot=wave_output_;const auto word2C=wave_view_.ReadLE32(0x2C); //5B210/214 loads before5B218 store
 child_packet_.port_word24=a.value;a.store_index[a.stack_writes]=14;a.store_pc[a.stack_writes++]=0x5B218;
 child_packet_.mode_slot=mode_slot;a.store_index[a.stack_writes]=13;a.store_pc[a.stack_writes++]=0x5B21C;
 store(12,word2C,0x5B224); //jal5B220 delay store precedes entry
 a.words=child_packet_.words;a.mode_slot=child_packet_.mode_slot;a.packet_storage=reinterpret_cast<uintptr_t>(&child_packet_);a.stack_known=0x7FFF;a.packet_complete=true;
 return AllocateChild5ABF0(*p,state_.control.note,state_.control.bank,w.channels,child_packet_);
}
GameFsStatus GameSourceSequencePool::AllocateChild5ABF0(Port& port,int32_t note,int32_t bank,int32_t channels,const ChildPacket& packet){
 auto& a=state_.port_input;a.child_entered=true;a.child_pc=0x5B220;a.child_delay_pc=0x5B224;a.note=note;a.bank=bank;a.channels=channels;
 a.frontier=state_.wave.frontier=state_.control.frontier=state_.frontier=0x5ABF0;
 if(&port!=current_port_||&packet!=&child_packet_||a.packet_storage!=reinterpret_cast<uintptr_t>(&packet)||a.words!=packet.words||a.mode_slot!=packet.mode_slot||packet.mode_slot!=wave_output_||a.value!=packet.port_word24
    ||note!=state_.control.note||bank!=state_.bank||channels!=state_.wave.channels)
  return {GameFsCode::Unavailable,0,"5ABF0 actual owned packet/port ABI changed"};
 return {GameFsCode::Unavailable,0,"Unsupported selected5ABF0(actual port, note, bank, channels, owned host-width packet) before body/result"};
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
