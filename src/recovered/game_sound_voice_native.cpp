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
bool GameSourceChildExact(const GameSourceChildSnapshot& a,const GameSourceChildSnapshot& b)noexcept{
 return std::tie(a.entered,a.live,a.reserved,a.source_free_linked,a.header_readback,a.channel_written,a.bank_mode_read,a.format_dispatched,a.g_read,a.slot_entered,a.slot_returned,a.child_returned,a.config_null,a.config_copy_returned,a.config_written,a.config_readback,a.pool_owner,a.pool_generation,a.pool_serial,a.sound_owner,a.sound_generation,a.config_owner,a.config_generation,a.pool_base,a.node_address,a.node_storage,a.head_storage,a.port_storage,a.packet_storage,a.bank_mode_slot,a.config_storage,a.key_storage,a.default_storage,a.output_storage,a.g_storage,a.candidate_id,a.parent_port_id,a.note,a.channels,a.bank,a.bank_mode,a.format,a.sse_use,a.allocator_kind,a.allocator_argument,a.packed,a.bytes,a.count,a.stride,a.index,a.flags,a.zero_writes,a.zero_readbacks,a.header_writes,a.channel_offset,a.channel_before,a.channel_after,a.channel_writes,a.channel_pc,a.caller_pc,a.mode_read_pc,a.format_table_word,a.format_target,a.g_read_pc,a.g_reference_segment,a.g_source_addend,a.config_default,a.config_out_byte,a.config_copy_writes,a.g_writes,a.g_writer_pc,a.config_call_pc,a.allocator_pc,a.slot_pc,a.slot_delay_pc,a.frontier,a.candidate_links,a.config_key)==std::tie(b.entered,b.live,b.reserved,b.source_free_linked,b.header_readback,b.channel_written,b.bank_mode_read,b.format_dispatched,b.g_read,b.slot_entered,b.slot_returned,b.child_returned,b.config_null,b.config_copy_returned,b.config_written,b.config_readback,b.pool_owner,b.pool_generation,b.pool_serial,b.sound_owner,b.sound_generation,b.config_owner,b.config_generation,b.pool_base,b.node_address,b.node_storage,b.head_storage,b.port_storage,b.packet_storage,b.bank_mode_slot,b.config_storage,b.key_storage,b.default_storage,b.output_storage,b.g_storage,b.candidate_id,b.parent_port_id,b.note,b.channels,b.bank,b.bank_mode,b.format,b.sse_use,b.allocator_kind,b.allocator_argument,b.packed,b.bytes,b.count,b.stride,b.index,b.flags,b.zero_writes,b.zero_readbacks,b.header_writes,b.channel_offset,b.channel_before,b.channel_after,b.channel_writes,b.channel_pc,b.caller_pc,b.mode_read_pc,b.format_table_word,b.format_target,b.g_read_pc,b.g_reference_segment,b.g_source_addend,b.config_default,b.config_out_byte,b.config_copy_writes,b.g_writes,b.g_writer_pc,b.config_call_pc,b.allocator_pc,b.slot_pc,b.slot_delay_pc,b.frontier,b.candidate_links,b.config_key);
}
bool GameSourceSlotExact(const GameSourceSlotSnapshot& a,const GameSourceSlotSnapshot& b)noexcept{
 return std::tie(a.entered,a.live,a.header_read,a.selector_returned,a.allocator_returned,a.dispatcher_returned,a.channel_returned,a.first_registered,a.second_registered,a.constructed,a.parameters_initialized,a.scratch_zeroed,a.retain_attempted,a.retained,a.free_unlinked,a.parent_linked,a.child_returned,a.packet_written,a.consumer_entered,a.consumer_returned,a.current_readback,a.owner,a.generation,a.serial,a.sound_owner,a.sound_generation,a.base,a.storage,a.child_pointer,a.parent_storage,a.packet_storage,a.mode_slot,a.child_parent,a.child_sentinel_storage,a.kind,a.argument,a.packed,a.low,a.high,a.cursor_before,a.cursor_after,a.selector_result,a.selected_position,a.allocator_result,a.stored_channel,a.callback_id,a.current_note,a.current_flags,a.retain_result,a.count,a.bytes,a.zero_writes,a.zero_readbacks,a.id_writes,a.header_zero_writes,a.range_writes,a.free_reads,a.fallback_reads,a.allocation_flags,a.slot_half6,a.slot_word8,a.slot_wordC,a.slot_kind,a.current_parent_flags,a.phase,a.retain_before,a.retain_after,a.retain_pc,a.return_pc,a.return_delay_pc,a.wave_word30,a.packet_word6,a.consumer_pc,a.consumer_delay_pc,a.frontier,a.scratch_writes,a.parameter_writes,a.known_halfwords,a.child_links,a.callback_contexts,a.child_sentinel_links,a.callback_codes,a.callback_context_pc,a.callback_code_pc,a.child_sentinel_ids,a.parameters,a.scratch78)==std::tie(b.entered,b.live,b.header_read,b.selector_returned,b.allocator_returned,b.dispatcher_returned,b.channel_returned,b.first_registered,b.second_registered,b.constructed,b.parameters_initialized,b.scratch_zeroed,b.retain_attempted,b.retained,b.free_unlinked,b.parent_linked,b.child_returned,b.packet_written,b.consumer_entered,b.consumer_returned,b.current_readback,b.owner,b.generation,b.serial,b.sound_owner,b.sound_generation,b.base,b.storage,b.child_pointer,b.parent_storage,b.packet_storage,b.mode_slot,b.child_parent,b.child_sentinel_storage,b.kind,b.argument,b.packed,b.low,b.high,b.cursor_before,b.cursor_after,b.selector_result,b.selected_position,b.allocator_result,b.stored_channel,b.callback_id,b.current_note,b.current_flags,b.retain_result,b.count,b.bytes,b.zero_writes,b.zero_readbacks,b.id_writes,b.header_zero_writes,b.range_writes,b.free_reads,b.fallback_reads,b.allocation_flags,b.slot_half6,b.slot_word8,b.slot_wordC,b.slot_kind,b.current_parent_flags,b.phase,b.retain_before,b.retain_after,b.retain_pc,b.return_pc,b.return_delay_pc,b.wave_word30,b.packet_word6,b.consumer_pc,b.consumer_delay_pc,b.frontier,b.scratch_writes,b.parameter_writes,b.known_halfwords,b.child_links,b.callback_contexts,b.child_sentinel_links,b.callback_codes,b.callback_context_pc,b.callback_code_pc,b.child_sentinel_ids,b.parameters,b.scratch78);
}
bool GameSourceSfxExact(const GameSourceSfxSnapshot& a,const GameSourceSfxSnapshot& b)noexcept{
 return std::tie(a.entered,a.live,a.platform_live,a.alias_read,a.index_read,a.tail_read,a.dirty_written,a.handler_returned,a.consumer_returned,a.parent_returned,a.port28_read,a.port2C_attempted,a.port2C_known,a.current_readback,a.owner,a.generation,a.sound_owner,a.sound_generation,a.tail_owner,a.tail_generation,a.tail_serial,a.tail_offset,a.tail_bytes,a.base,a.core,a.slot_base,a.slot_storage,a.record_storage,a.packet_storage,a.mode_slot,a.tail_base,a.data_pointer,a.child_wave,a.parent_result,a.count,a.bytes,a.zero_writes,a.zero_readbacks,a.grain,a.voices,a.output,a.rate,a.dry,a.wet,a.left,a.right,a.runtime_index,a.dirty_flags,a.dirty_word8,a.dirty_word10,a.dirty_word2C,a.dirty_word30,a.slot_flags_before,a.slot_flags_after,a.slot_byte5,a.wave_wordC,a.port_word28,a.writes,a.handler_pc,a.handler_return_pc,a.handler_return_delay_pc,a.consumer_return_pc,a.parent_return_pc,a.port_read_pc,a.frontier,a.slot_id,a.handler_result,a.pitch_before,a.pitch_product,a.pitch_result,a.pitch_after,a.write_pc,a.write_value)==std::tie(b.entered,b.live,b.platform_live,b.alias_read,b.index_read,b.tail_read,b.dirty_written,b.handler_returned,b.consumer_returned,b.parent_returned,b.port28_read,b.port2C_attempted,b.port2C_known,b.current_readback,b.owner,b.generation,b.sound_owner,b.sound_generation,b.tail_owner,b.tail_generation,b.tail_serial,b.tail_offset,b.tail_bytes,b.base,b.core,b.slot_base,b.slot_storage,b.record_storage,b.packet_storage,b.mode_slot,b.tail_base,b.data_pointer,b.child_wave,b.parent_result,b.count,b.bytes,b.zero_writes,b.zero_readbacks,b.grain,b.voices,b.output,b.rate,b.dry,b.wet,b.left,b.right,b.runtime_index,b.dirty_flags,b.dirty_word8,b.dirty_word10,b.dirty_word2C,b.dirty_word30,b.slot_flags_before,b.slot_flags_after,b.slot_byte5,b.wave_wordC,b.port_word28,b.writes,b.handler_pc,b.handler_return_pc,b.handler_return_delay_pc,b.consumer_return_pc,b.parent_return_pc,b.port_read_pc,b.frontier,b.slot_id,b.handler_result,b.pitch_before,b.pitch_product,b.pitch_result,b.pitch_after,b.write_pc,b.write_value);
}
bool GameSourceSequenceExact(const GameSourceSequenceSnapshot& a,const GameSourceSequenceSnapshot& b)noexcept{
 return std::tie(a.live,a.active,a.allocation_returned,a.lookup_returned,a.clear_returned,a.command_entered,a.bank_retained,a.record_lookup_returned,a.track_reserved,a.track_source_initialized,a.track_activated,a.port_attempted,a.port_result_known,a.handler_returned,a.owner,a.generation,a.allocation,a.voice_owner,a.voice_generation,a.ids_storage,a.output_storage,a.id,a.bank,a.sentinel_id,a.track_id,a.port_id,a.flags,a.sentinel_flags,a.allocation_pc,a.return_pc,a.result_store_pc,a.lookup_pc,a.clear_pc,a.command,a.subindex,a.group,a.event,a.command_pc,a.retain_writer,a.refs_before,a.refs_after,a.record_offset,a.record_kind,a.record_divisor,a.record_bytes,a.track_pool_stride,a.port_call_pc,a.frontier,a.initial_factor,a.selected_factor,a.sentinel_self,a.child_head_same,a.output_unchanged,a.bank_ids,a.output_before,a.output_after,a.admission_returned,a.command_released,a.update_entered,a.processor_attempted,a.current_refs,a.release_pc,a.release_before,a.release_after,a.update_pc,a.processor_pc,a.native_elapsed_ns,a.worker_tick_serial,a.worker_previous_ns,a.worker_current_ns,a.native_delta_seconds,a.update_quantum,a.processor_quantum,a.processor_context)
 ==std::tie(b.live,b.active,b.allocation_returned,b.lookup_returned,b.clear_returned,b.command_entered,b.bank_retained,b.record_lookup_returned,b.track_reserved,b.track_source_initialized,b.track_activated,b.port_attempted,b.port_result_known,b.handler_returned,b.owner,b.generation,b.allocation,b.voice_owner,b.voice_generation,b.ids_storage,b.output_storage,b.id,b.bank,b.sentinel_id,b.track_id,b.port_id,b.flags,b.sentinel_flags,b.allocation_pc,b.return_pc,b.result_store_pc,b.lookup_pc,b.clear_pc,b.command,b.subindex,b.group,b.event,b.command_pc,b.retain_writer,b.refs_before,b.refs_after,b.record_offset,b.record_kind,b.record_divisor,b.record_bytes,b.track_pool_stride,b.port_call_pc,b.frontier,b.initial_factor,b.selected_factor,b.sentinel_self,b.child_head_same,b.output_unchanged,b.bank_ids,b.output_before,b.output_after,b.admission_returned,b.command_released,b.update_entered,b.processor_attempted,b.current_refs,b.release_pc,b.release_before,b.release_after,b.update_pc,b.processor_pc,b.native_elapsed_ns,b.worker_tick_serial,b.worker_previous_ns,b.worker_current_ns,b.native_delta_seconds,b.update_quantum,b.processor_quantum,b.processor_context)&&GameSourcePortExact(a.port,b.port)&&GameSourceTrackExact(a.track,b.track)&&GameSourceProcessorExact(a.processor,b.processor)&&GameSourceControlExact(a.control,b.control)&&GameSourceWaveExact(a.wave,b.wave)&&GameSourcePortInputExact(a.port_input,b.port_input)&&GameSourceChildExact(a.child,b.child)&&GameSourceSlotExact(a.slot,b.slot)&&GameSourceSfxExact(a.sfx,b.sfx);
}
GameSourceSequencePool::GameSourceSequencePool(){Close();}
void GameSourceSequencePool::Close()noexcept{
 parent_return_node_=nullptr;advance_sfx_=false;control_tail_call_={};control_tail_.Reset();child_wave_pointer_=0;controls_.Close();state_.sfx.live=state_.sfx.platform_live=state_.sfx.current_readback=false;
 advance_slot_=false;slots_.Close();child_parent_=nullptr;state_.slot.live=state_.slot.current_readback=false;
 child_candidate_=nullptr;child_source_owner_=child_sound_owner_=child_sound_generation_=child_config_owner_=0;
 child_config_ready_=false;child_source_.reset();child_config_value_.reset();
 state_.child.live=state_.child.reserved=state_.child.source_free_linked=state_.child.config_readback=false;
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
uint8_t* GameSourceSequencePool::CopyEmptyConfigC39F4(uint8_t* out,const uint8_t* source){
 if(out!=&child_config_output_||source!=child_config_default_.data()||source[0]!=0)
  throw std::runtime_error("C39F4 finite owned empty-copy ABI unavailable");
 out[0]=source[0];++child_config_copy_writes_;return out;
}
uint8_t* GameSourceSequencePool::LookupNullConfig54A60(const char* input,const uint8_t* key,uint8_t* out){
 if(!child_config_owner_||!child_sound_owner_||!child_sound_generation_||input!=child_config_input_||input!=nullptr||key!=child_config_key_.data()||out!=&child_config_output_)
  throw std::runtime_error("54A60 NativeOwnedNullSgxConfig input unavailable before nonnull parser");
 //The actual NULL branch skips key comparison and copies the actual default.
 return CopyEmptyConfigC39F4(out,child_config_default_.data());
}
GameFsStatus GameSourceSequencePool::WriteNullConfig54B3C(uint32_t& output,uint32_t fallback){
 try{
  if(!child_config_value_||&output!=child_config_value_.get()||fallback!=0)
   return {GameFsCode::Unavailable,0,"54B3C actual owned config destination/default unavailable"};
  auto* copied=LookupNullConfig54A60(child_config_input_,child_config_key_.data(),&child_config_output_);
  if(copied!=&child_config_output_||*copied!=0)return {GameFsCode::Unavailable,0,"54B3C unsupported nonempty numeric parser"};
  output=fallback;++child_config_g_writes_;
  child_config_ready_=output==fallback&&child_config_output_==0&&child_config_copy_writes_==1;
  return child_config_ready_?GameFsStatus{}:GameFsStatus{GameFsCode::Unavailable,0,"54B74 current owned G readback changed"};
 }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
GameFsStatus GameSourceSequencePool::InitializeNativeChildComponent(uint64_t owner,uint64_t generation){
 if(child_source_||child_config_value_||!owner||!generation)
  return {GameFsCode::Unavailable,0,"NativeIndexedOwnedChildPool16 cold owner unavailable"};
 //NativeOwnedNullSgxConfig is an explicit Windows configuration policy.
 //It is NOT the unknown original PSP configuration or whole SGX startup.
 child_config_value_.reset(new(std::nothrow) uint32_t);
 if(!child_config_value_)return {GameFsCode::Unavailable,0,"NativeOwnedNullSgxConfig real cell allocation failed"};
 static std::atomic<uint64_t> next{0};child_config_owner_=++next;++child_config_generation_;
 child_sound_owner_=owner;child_sound_generation_=generation;
 child_config_ready_=false;child_config_input_=nullptr;child_config_output_=0xCD;
 child_config_copy_writes_=child_config_g_writes_=0;
 *child_config_value_=0;++child_config_g_writes_; //selected cell within source54878 clear
 const auto query=WriteNullConfig54B3C(*child_config_value_,0);if(!query)return query;
 std::unique_ptr<uint32_t[]> block(new(std::nothrow) uint32_t[544]);
 if(!block)return {GameFsCode::Unavailable,0,"NativeIndexedOwnedChildPool16 real allocation failed"};
 child_zero_writes_=child_zero_readbacks_=child_header_writes_=0;
 //C++ semantic iterations, not measured host machine load/store counts.
 for(size_t i=0;i<544;++i){block[i]=0;++child_zero_writes_;}
 for(size_t i=0;i<544;++i){if(block[i]!=0)return {GameFsCode::Unavailable,0,"Child source-layout zero readback"};++child_zero_readbacks_;}
 child_source_=std::move(block);child_head_={};child_head_.link0=child_head_.link4=&child_head_;child_head_.id=-1;
 auto* raw=reinterpret_cast<uint8_t*>(child_source_.get());
 for(size_t i=0;i<child_nodes_.size();++i){auto& n=child_nodes_[i];n={};n.id=int16_t(i);n.flags=0;
  //54B9C physical offsets: new.link4=H.link4;H.link4=new;
  //new.link0=H;old.link0=new. Direction names do not override offsets.
  auto* old=child_head_.link4;n.link4=old;child_head_.link4=&n;n.link0=&child_head_;old->link0=&n;
  const auto offset=i*136;raw[offset+8]=uint8_t(i);raw[offset+9]=uint8_t(i>>8);++child_header_writes_;
  raw[offset+10]=raw[offset+11]=0;++child_header_writes_;
 }
 child_source_owner_=++next;++child_source_generation_;++child_source_serial_;
 child_candidate_=nullptr;return {};
}
GameFsStatus GameSourceSequencePool::BeginChild5ABF0(const ChildPacket& packet,const BankModeRead& read_mode){
 auto& a=state_.child;a.entered=true;a.frontier=state_.wave.frontier=state_.control.frontier=state_.frontier=0x5ABF0;
 if(&packet!=&child_packet_||!current_port_||child_candidate_||!child_source_||!child_source_owner_||!child_config_ready_||!read_mode
    ||state_.wave.channels!=1||state_.port_input.packet_storage!=reinterpret_cast<uintptr_t>(&packet))
  return {GameFsCode::Unavailable,0,"5ABF0 current private child/packet component unavailable"};
 auto* chosen=child_head_.link4; //actual physical+4, never a forced candidate ID
 if(chosen==&child_head_||!chosen||chosen->id<0){a.frontier=state_.wave.frontier=state_.control.frontier=state_.frontier=0x5AF10;
  return {GameFsCode::Unavailable,0,"Unsupported actual negative child free-candidate branch before source return"};}
 size_t index=child_nodes_.size();for(size_t i=0;i<child_nodes_.size();++i)if(&child_nodes_[i]==chosen){index=i;break;}
 if(index>=child_nodes_.size())return {GameFsCode::Unavailable,0,"5AC34 actual child candidate membership unavailable"};
 auto* raw=reinterpret_cast<uint8_t*>(child_source_.get());const auto offset=index*136;
 const auto id=int16_t(uint16_t(raw[offset+8])|(uint16_t(raw[offset+9])<<8));
 const auto flags=uint16_t(raw[offset+10])|(uint16_t(raw[offset+11])<<8);
 if(id<0||id!=chosen->id||size_t(id)!=index||flags!=chosen->flags||flags||chosen->link0!=&child_head_||!chosen->link4||chosen->link4->link0!=chosen)
  return {GameFsCode::Unavailable,0,"5AC3C actual signed child ID/header/link coherence unavailable"};
 child_candidate_=chosen; //host-only reservation; SOURCE free links unchanged
 a.live=a.reserved=a.source_free_linked=a.header_readback=true;
 a.pool_owner=child_source_owner_;a.pool_generation=child_source_generation_;a.pool_serial=child_source_serial_;
 a.sound_owner=child_sound_owner_;a.sound_generation=child_sound_generation_;a.config_owner=child_config_owner_;a.config_generation=child_config_generation_;
 a.pool_base=reinterpret_cast<uintptr_t>(child_source_.get());a.node_address=a.pool_base+offset;a.node_storage=reinterpret_cast<uintptr_t>(chosen);
 a.head_storage=reinterpret_cast<uintptr_t>(&child_head_);a.port_storage=reinterpret_cast<uintptr_t>(current_port_);a.packet_storage=reinterpret_cast<uintptr_t>(&packet);a.bank_mode_slot=packet.mode_slot;
 a.config_storage=reinterpret_cast<uintptr_t>(&child_config_input_);a.key_storage=reinterpret_cast<uintptr_t>(child_config_key_.data());a.default_storage=reinterpret_cast<uintptr_t>(child_config_default_.data());
 a.output_storage=reinterpret_cast<uintptr_t>(&child_config_output_);a.g_storage=reinterpret_cast<uintptr_t>(child_config_value_.get());
 a.candidate_id=id;a.parent_port_id=current_port_->link.id;a.note=state_.control.note;a.channels=state_.wave.channels;a.bank=state_.bank;
 a.bytes=2176;a.count=uint32_t(child_nodes_.size());a.stride=136;a.index=uint32_t(index);a.flags=flags;
 a.zero_writes=child_zero_writes_;a.zero_readbacks=child_zero_readbacks_;a.header_writes=child_header_writes_;
 a.channel_offset=uint32_t(offset+0x2A);a.channel_before=raw[a.channel_offset];
 for(int32_t i=0;i<a.channels;++i){raw[a.channel_offset+uint32_t(i)]=0xFF;++a.channel_writes;}
 a.channel_after=raw[a.channel_offset];a.channel_written=a.channel_after==0xFF;a.channel_pc=0x5AC70;a.caller_pc=0x5AC80;
 a.candidate_links={reinterpret_cast<uintptr_t>(chosen->link0),reinterpret_cast<uintptr_t>(chosen->link4)};a.config_key=child_config_key_;
 a.config_null=child_config_input_==nullptr;a.config_copy_returned=child_config_copy_writes_==1;a.config_written=child_config_g_writes_==2;
 a.config_readback=child_config_ready_&&child_config_value_&&*child_config_value_==0&&child_config_output_==0;
 a.config_default=0;a.config_out_byte=child_config_output_;a.config_copy_writes=child_config_copy_writes_;a.g_writes=child_config_g_writes_;a.g_writer_pc=0x54B74;a.config_call_pc=0x5CCCC;
 if(!a.channel_written||!a.config_readback)return {GameFsCode::Unavailable,0,"5AC70/config actual readback unavailable"};
 if(advance_slot_)state_.child=Snapshot().child;
 return DispatchChild5F568(packet,read_mode);
}
GameFsStatus GameSourceSequencePool::DispatchChild5F568(const ChildPacket& packet,const BankModeRead& read_mode){
 auto& a=state_.child;
 a.frontier=state_.wave.frontier=state_.control.frontier=state_.frontier=0x5F568;
 if(&packet!=&child_packet_||!child_candidate_||!a.source_free_linked||packet.mode_slot!=state_.wave.control_slot_storage)
  return {GameFsCode::Unavailable,0,"5F568 actual owned child packet unavailable"};
 a.mode_read_pc=0x5F57C;a.frontier=state_.wave.frontier=state_.control.frontier=state_.frontier=a.mode_read_pc;
 uint32_t mode=0;const auto mode_status=read_mode(packet.mode_slot,mode);if(!mode_status)return mode_status;
 a.bank_mode=int32_t(mode);a.bank_mode_read=true;a.format=int32_t(packet.words[2]);
 if(mode!=1||packet.words[2]!=3)return {GameFsCode::Unavailable,0,"Unsupported5F568 mode/format outside selected source branch"};
 a.format_dispatched=true;a.format_table_word=0xDF520;a.format_target=0x5F5F4;
 a.frontier=state_.wave.frontier=state_.control.frontier=state_.frontier=0x5F5F8;
 if(!child_config_value_||!child_config_ready_||child_sound_owner_!=a.sound_owner||child_sound_generation_!=a.sound_generation
    ||child_config_owner_!=a.config_owner||child_config_generation_!=a.config_generation)
  return {GameFsCode::Unavailable,0,"5F5F8 actual PH1-cell issuer retired"};
 a.sse_use=int32_t(*child_config_value_);a.g_read=true;a.g_read_pc=0x5F5F8;a.g_reference_segment=1;a.g_source_addend=0xE47F4;
 if(a.sse_use!=0||!a.config_null||!a.config_readback)return {GameFsCode::Unavailable,0,"5F5F8 NativeOwnedNullSgxConfig current value changed"};
 const int32_t kind=a.sse_use?0:1; //5F604 vs5F614; actual field read chooses kind
 return AllocateSfxPort5CA60(kind,int32_t(packet.words[1]),int32_t(packet.words[0]),packet);
}
GameFsStatus GameSourceSequencePool::AllocateSfxPort5CA60(int32_t kind,int32_t argument,int32_t packed,const ChildPacket& packet){
 auto& a=state_.child;a.allocator_kind=kind;a.allocator_argument=argument;a.packed=packed;a.allocator_pc=0x5F5B8;
 a.frontier=state_.wave.frontier=state_.control.frontier=state_.frontier=0x5CA60;
 if(&packet!=&child_packet_||!a.g_read||kind!=(a.sse_use?0:1)||argument!=int32_t(packet.words[1])||packed!=int32_t(packet.words[0]))
  return {GameFsCode::Unavailable,0,"5CA60 actual source dispatch arguments changed"};
 if(packed!=0)return {GameFsCode::Unavailable,0,"Unsupported5CB54 packed allocation branch"};
 return SelectSfxSlot5C8F4(kind,argument,packed,packet);
}
GameFsStatus GameSourceSequencePool::SelectSfxSlot5C8F4(int32_t kind,int32_t argument,int32_t packed,const ChildPacket& packet){
 auto& a=state_.child;a.slot_entered=true;a.slot_pc=0x5CA8C;a.slot_delay_pc=0x5CA90;
 a.frontier=state_.wave.frontier=state_.control.frontier=state_.frontier=0x5C8F4;
 if(&packet!=&child_packet_||kind!=a.allocator_kind||argument!=a.allocator_argument||packed!=a.packed||!child_candidate_)
  return {GameFsCode::Unavailable,0,"5C8F4 actual slot-selection ABI changed"};
 if(!advance_slot_)return {GameFsCode::Unavailable,0,"Unsupported selected5C8F4(actual kind, current child packet) before slot pool/body/result"};
 auto& s=state_.slot;s.entered=true;s.phase=1;s.frontier=0x5C8F4;
 const auto h=slots_.Snapshot();if(!h.live||h.sound_owner!=a.sound_owner||h.sound_generation!=a.sound_generation)return {GameFsCode::Unavailable,0,"5C8F4 actual native slot H owner unavailable"};
 s.live=s.header_read=true;s.owner=h.owner;s.generation=h.generation;s.serial=h.serial;s.sound_owner=h.sound_owner;s.sound_generation=h.sound_generation;
 s.base=h.base;s.count=h.count;s.bytes=h.bytes;s.zero_writes=h.zero_writes;s.zero_readbacks=h.zero_readbacks;s.id_writes=h.id_writes;s.header_zero_writes=h.header_zero_writes;s.range_writes=h.range_writes;
 s.parent_storage=reinterpret_cast<uintptr_t>(current_port_);s.packet_storage=reinterpret_cast<uintptr_t>(&packet);s.mode_slot=packet.mode_slot;
 s.kind=kind;s.argument=argument;s.packed=packed;s.low=int32_t(h.header[(kind*8+8)/4]);s.high=int32_t(h.header[(kind*8+12)/4]);s.cursor_before=int32_t(h.header[(kind*4+0x48)/4]);
 GameNativeSfxSlotPool16::Selection selection;int32_t value=-1;const auto result=slots_.Allocate(kind,argument,packed,selection,value);
 s.selector_returned=selection.returned;s.selector_result=selection.value;s.selected_position=selection.position;s.free_reads=selection.free_reads;s.fallback_reads=selection.fallback_reads;
 if(!result)return result;s.allocator_returned=true;s.allocator_result=value;s.dispatcher_returned=true;s.phase=2;
 //The source allows negative/full-width values. This finite owner requires an
 //actual same-slot nonnegative signed byte before indexing callbacks.
 if(value<0||value>127||uint32_t(value)>=h.count)return {GameFsCode::Unavailable,0,"Native SFX full-return/signed-byte identity policy"};
 const auto current=slots_.Read(uint32_t(value));if(current.id!=value)return {GameFsCode::Unavailable,0,"5CB20 actual returned word0 readback changed"};
 s.storage=h.base+32*uint32_t(value);s.allocation_flags=current.flags;s.slot_half6=uint16_t(current.half6);s.slot_word8=current.word8;s.slot_wordC=current.wordC;s.slot_kind=current.kind;
 s.cursor_after=int32_t(slots_.Snapshot().header[(kind*4+0x48)/4]);
 auto* raw=reinterpret_cast<uint8_t*>(child_source_.get())+a.index*136;
 raw[0x2A]=uint8_t(value);s.stored_channel=int8_t(raw[0x2A]);s.callback_id=s.stored_channel;s.channel_returned=s.callback_id==value;
 if(!s.channel_returned)return {GameFsCode::Unavailable,0,"5ACA4 actual channel byte readback changed"};
 s.callback_contexts[0]=reinterpret_cast<uintptr_t>(raw+0x2A);s.callback_codes[0]=0x5AA90;s.callback_context_pc[0]=0x5CB74;s.callback_code_pc[0]=0x5CB7C;
 const auto registration=slots_.Register(s.callback_id,s.callback_codes[0],s.callback_contexts[0]);if(!registration)return registration;
 const auto read=slots_.Read(uint32_t(s.callback_id));s.first_registered=read.callback_code==s.callback_codes[0]&&read.callback_context==s.callback_contexts[0];s.phase=3;
 if(!s.first_registered)return {GameFsCode::Unavailable,0,"First actual5CB64 registration readback failed"};return {};
}
GameFsStatus GameSourceSequencePool::InitializeNativeSlotComponent(uint64_t owner,uint64_t generation){
 const auto init=slots_.Initialize(owner,generation);if(!init)return init;
 //NativeSfxKind1Range16 is a declared Windows configuration. PSP current
 //config/s4/capacity and the complete SGX bootstrap are not established.
 int32_t returned=0;const auto range=slots_.SetRange(1,0,16,returned);
 if(!range||returned!=16){slots_.Close();return {GameFsCode::Unavailable,0,"Native SFX kind1 range publication failed"};}
 const auto runtime=slots_.PublishRuntimeKind1();if(!runtime){slots_.Close();return runtime;}
 const auto controls=controls_.Initialize(owner,generation,slots_.Snapshot().base,16);
 if(!controls){slots_.Close();controls_.Close();return controls;}return {};
}
GameFsStatus GameSourceSequencePool::ContinueSlotChild(const std::function<GameFsStatus()>& retain,uint8_t*& returned_node){
 returned_node=nullptr;auto& s=state_.slot;const auto& entry=state_.child;
 if(!s.first_registered||!s.channel_returned||!child_candidate_||!current_port_||!child_source_||!retain||entry.index>=child_nodes_.size())
  return {GameFsCode::Unavailable,0,"5ACF0 actual child/slot continuation owner unavailable"};
 auto* raw=reinterpret_cast<uint8_t*>(child_source_.get())+entry.index*136;
 s.callback_contexts[1]=reinterpret_cast<uintptr_t>(raw);s.callback_codes[1]=0x5AAC4;s.callback_context_pc[1]=0x5CB74;s.callback_code_pc[1]=0x5CB7C;
 const auto registration=slots_.Register(int8_t(raw[0x2A]),s.callback_codes[1],s.callback_contexts[1]);if(!registration)return registration;
 const auto read=slots_.Read(uint32_t(s.callback_id));s.second_registered=read.callback_code==s.callback_codes[1]&&read.callback_context==s.callback_contexts[1];
 if(!s.second_registered)return {GameFsCode::Unavailable,0,"Second actual5CB64 registration readback failed"};s.phase=4;
 const auto half=[&](size_t at,uint16_t value){raw[at]=uint8_t(value);raw[at+1]=uint8_t(value>>8);};
 const auto word=[&](size_t at,uint32_t value){for(size_t i=0;i<4;++i)raw[at+i]=uint8_t(value>>(8*i));};
 half(8,uint16_t(entry.note));child_candidate_->id=int16_t(entry.note); //initial candidate receipt is immutable
 child_parent_=current_port_;s.child_parent=reinterpret_cast<uintptr_t>(child_parent_); //full host source+C pointer
 auto& inner=child_inner_[entry.index];inner={};inner.id=-1;inner.flags=0;inner.next=inner.previous=&inner;
 half(0x18,UINT16_MAX);half(0x1A,0); //embedded+10 sentinel ID/flags; pointers have full-width semantic storage
 half(0x2E,4096);half(0x2C,4096);raw[0x28]=uint8_t(entry.channels);word(0x1C,0);word(0x20,0);
 half(0x24,uint16_t(entry.bank)); //5AD54 delay before5A490
 const auto parameter=[&](size_t at,uint16_t value){half(0x30+at,value);++s.parameter_writes;s.known_halfwords|=uint64_t(1)<<(at/2);};
 parameter(0,0);parameter(2,0);for(size_t at=0x18;at<=0x46;at+=2)parameter(at,0);
 for(size_t k=0;k<4;++k){parameter(0x20+12*k,4096);parameter(0x22+12*k,1000);}
 s.parameters_initialized=true;
 for(size_t at=0x78;at<0x88;at+=4){word(at,0);++s.scratch_writes;}s.scratch_zeroed=true;
 parameter(0,UINT16_MAX);s.constructed=true;s.phase=5;
 s.retain_attempted=true;s.retain_before=state_.current_refs;s.retain_pc=0x5AD6C;
 const auto acquired=retain(); //SOURCE ignores v0. Native publication requires real successful Acquire/readback.
 if(!acquired)return acquired;
 s.retain_after=state_.current_refs;s.retained=s.retain_result==1&&s.retain_after==s.retain_before+1;
 if(!s.retained)return {GameFsCode::Unavailable,0,"NONMATCHING native child Acquire success/readback policy"};s.phase=6;
 auto* node=child_candidate_;auto* head=&current_port_->child0;
 if(node->link0!=node){node->link0->link4=node->link4;node->link4->link0=node->link0;}
 auto* old=head->link0;node->link4=head;old->link4=node;node->link0=old;current_port_->link.flags|=2;head->link0=node;
 s.free_unlinked=s.parent_linked=true;s.current_parent_flags=current_port_->link.flags;s.phase=7;
 s.child_pointer=reinterpret_cast<uintptr_t>(raw);s.child_returned=true;s.return_pc=0x5ADE4;s.return_delay_pc=0x5ADE8;s.phase=8;
 returned_node=raw;return {}; //actual typed Windows result of completed source5ABF0 body
}
GameFsStatus GameSourceSequencePool::ParentAfterChild5B228(uint8_t* returned_node){
 auto& s=state_.slot;const auto& entry=state_.child;
 if(!s.child_returned||!child_source_||!returned_node||returned_node!=reinterpret_cast<uint8_t*>(child_source_.get())+entry.index*136||reinterpret_cast<uintptr_t>(returned_node)!=s.child_pointer)
  return {GameFsCode::Unavailable,0,"5B228 actual returned node pointer owner unavailable"};
 //Parent5B130 uses the true node pointer and current signed channel. This is
 //separate from a slot ID, initial free ID, current note, or success status.
 try{
  const auto d=wave_view_.Describe();if(d.host_address+d.offset!=state_.wave.result_pointer||d.size<0x34||s.child_pointer!=entry.node_address)
   return {GameFsCode::Unavailable,0,"5B244 current WAVE/child return pointer lease changed"};
  s.wave_word30=wave_view_.ReadLE32(0x30);child_packet_.words[6]=s.wave_word30;s.packet_word6=child_packet_.words[6];s.packet_written=true;
 }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
 const auto effect=Enter6016C(int8_t(returned_node[0x2A]),0,child_packet_,0);
 if(!effect||!advance_sfx_)return effect;return ContinueParentSfx(returned_node,parent_return_node_);
}
GameFsStatus GameSourceSequencePool::Enter6016C(int32_t id,int32_t zero1,const ChildPacket& packet,int32_t zero3){
 auto& s=state_.slot;
 if(!s.child_returned||!s.packet_written||&packet!=&child_packet_||packet.mode_slot!=wave_output_||id!=s.callback_id||zero1||zero3)
  return {GameFsCode::Unavailable,0,"6016C actual child/slot/packet arguments changed"};
 s.consumer_entered=true;s.consumer_pc=0x5B264;s.consumer_delay_pc=0x5B268;s.frontier=state_.wave.frontier=state_.control.frontier=state_.frontier=0x6016C;s.phase=9;
 if(!advance_sfx_)return {GameFsCode::Unavailable,0,"Unsupported actual6016C(signed SFX id,0,same owned packet,0) before body/result"};
 auto& a=state_.sfx;a.entered=true;a.slot_id=id;a.packet_storage=reinterpret_cast<uintptr_t>(&packet);a.mode_slot=packet.mode_slot;
 const auto h=slots_.Snapshot();const auto d=controls_.Snapshot();const auto record=slots_.Read(uint32_t(id));
 a.live=d.live;a.platform_live=d.platform_live;a.owner=d.owner;a.generation=d.generation;a.sound_owner=d.sound_owner;a.sound_generation=d.sound_generation;
 a.base=d.base;a.core=d.core;a.slot_base=d.slot_base;a.slot_storage=h.base+uint32_t(id)*32;a.count=d.count;a.bytes=d.bytes;a.zero_writes=d.zero_writes;a.zero_readbacks=d.zero_readbacks;
 a.grain=d.grain;a.voices=d.voices;a.output=d.output;a.rate=d.rate;a.dry=d.dry;a.wet=d.wet;a.left=d.left;a.right=d.right;
 if(!d.live||d.slot_base!=h.base||d.sound_owner!=state_.child.sound_owner||d.sound_generation!=state_.child.sound_generation||id<0||uint32_t(id)>=h.count||record.id!=id||record.kind!=1)
  return {GameFsCode::Unavailable,0,"6016C current G4C alias/SFX ID/kind owner unavailable"};
 a.alias_read=true;a.runtime_index=record.word1C;a.index_read=true;a.slot_flags_before=record.flags;
 if(a.runtime_index>=d.count||packet.words[2]!=3||!control_tail_call_)return {GameFsCode::Unavailable,0,"5FD78 current normal command/index/tail reader unavailable"};
 const auto tail=control_tail_call_(packet.mode_slot,control_tail_);if(!tail)return tail;
 try{const auto t=control_tail_.Describe();a.tail_owner=t.owner;a.tail_generation=t.generation;a.tail_serial=t.serial;a.tail_offset=t.offset;a.tail_bytes=t.size;
  if(t.host_address>UINTPTR_MAX-t.offset||packet.words[6]>=t.size)return {GameFsCode::Unavailable,0,"5FDDC actual BANK tail/offset extent"};
  a.tail_base=uintptr_t(t.host_address+t.offset);if(a.tail_base>UINTPTR_MAX-packet.words[6])return {GameFsCode::Unavailable,0,"5FDE4 native data address overflow"};
  a.data_pointer=a.tail_base+packet.words[6];a.tail_read=true;
 }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
 a.record_storage=a.base+100*a.runtime_index;
 auto written=controls_.Command0(a.runtime_index,a.data_pointer,packet.words[12],a.write_value);if(!written)return written;
 a.write_pc={0x5FDE8,0x5FDEC,0x5FE00,0x5FE08,0x5FE18,0x5FE20,0x5FE30,0x5FE38,0x5FE48,0x5FE50,0x5FE5C,0x5FE6C};
 const auto finished=slots_.FinishCommand0(uint32_t(id),a.handler_result);if(!finished)return finished;
 const auto current=slots_.Read(uint32_t(id));a.slot_flags_after=current.flags;a.slot_byte5=current.byte5;a.write_value[10]=current.byte5;a.write_value[11]=current.flags;
 a.dirty_written=true;a.writes=12;a.handler_pc=0x5FDA8;a.handler_return_pc=0x5FE68;a.handler_return_delay_pc=0x5FE6C;a.handler_returned=true;
 a.consumer_returned=true;a.consumer_return_pc=0x601AC;
 GameNativeSfxControl16::Record dirty;if(!controls_.Read(a.runtime_index,dirty))return {GameFsCode::Unavailable,0,"5FD78 current dirty readback unavailable"};
 a.dirty_flags=dirty.flags;a.dirty_word8=dirty.word8;a.dirty_word10=dirty.word10;a.dirty_word2C=dirty.word2C;a.dirty_word30=dirty.word30;return {};
}
GameFsStatus GameSourceSequencePool::ContinueParentSfx(uint8_t* node,uint8_t*& returned_node){
 returned_node=nullptr;auto& a=state_.sfx;
 if(!a.consumer_returned||!node||reinterpret_cast<uintptr_t>(node)!=state_.slot.child_pointer||!current_port_)return {GameFsCode::Unavailable,0,"5B274 actual child/WAVE owner unavailable"};
 try{a.wave_wordC=wave_view_.ReadLE32(0x0C);}catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
 a.pitch_before=int16_t(uint16_t(node[0x2E])|(uint16_t(node[0x2F])<<8));
 a.pitch_product=int32_t(uint32_t(a.pitch_before)*a.wave_wordC);
 child_wave_pointer_=state_.wave.result_pointer;a.child_wave=child_wave_pointer_; //5B288 full host adaptation of source Child+20
 const int64_t product=int64_t(a.pitch_product)*INT64_C(0x2F8DF18F);const int32_t hi=int32_t(uint64_t(product)>>32);
 const int32_t shifted=hi>=0?hi/8192:-int32_t((uint32_t(-int64_t(hi))+8191)/8192);
 a.pitch_result=int32_t(uint32_t(shifted)-uint32_t(a.pitch_product<0?-1:0));a.pitch_after=int16_t(uint16_t(a.pitch_result));
 node[0x2E]=uint8_t(a.pitch_after);node[0x2F]=uint8_t(uint32_t(a.pitch_after)>>8);
 returned_node=node;return {}; //typed source5B130 v0, not its passive audit
}
GameFsStatus GameSourceSequencePool::CommandAfterParent5B49C(Port& port,uint8_t* node){
 auto& a=state_.sfx;
 if(!node)return {GameFsCode::Unavailable,0,"Unsupported actual5B49C null Child return branch"};
 if(&port!=current_port_||!child_source_||node!=reinterpret_cast<uint8_t*>(child_source_.get())+state_.child.index*136)
  return {GameFsCode::Unavailable,0,"5B49C returned Child pointer current owner changed"};
 //The caller received/tested the actual typed result before its source reads.
 //Port+28 uses initialized parameter halfwords0/1; +2C uses UNKNOWN2/3.
 if(node[0x28]!=1||!(current_port_->known&1)||!(current_port_->known&2))return {GameFsCode::Unavailable,0,"5B4C4 selected channel/known Port+28 unavailable"};
 a.port_word28=uint32_t(current_port_->parameters[0])|(uint32_t(current_port_->parameters[1])<<16);a.port28_read=true;
 a.port2C_attempted=true;a.port_read_pc=a.frontier=0x5B4C8;
 state_.slot.frontier=state_.wave.frontier=state_.control.frontier=state_.frontier=a.frontier;
 return {GameFsCode::Unavailable,0,"Unsupported actual5B4C8 Port+0x2C read before unknown parameter halfwords2/3"};
}
GameSourceSequenceSnapshot GameSourceSequencePool::Snapshot()const{
 auto out=state_;
 if(out.child.entered&&!out.slot.entered){auto& a=out.child;
  a.live=bool(child_source_)&&bool(child_config_value_)&&child_source_owner_==a.pool_owner&&child_source_generation_==a.pool_generation;
  a.reserved=a.live&&child_candidate_&&reinterpret_cast<uintptr_t>(child_candidate_)==a.node_storage;
  a.source_free_linked=a.reserved&&child_head_.link4==child_candidate_&&child_candidate_->link0==&child_head_&&child_candidate_->link4&&child_candidate_->link4->link0==child_candidate_;
  a.config_readback=a.live&&child_config_ready_&&child_config_owner_==a.config_owner&&child_config_generation_==a.config_generation&&*child_config_value_==0&&child_config_output_==0;
  if(a.live&&a.channel_offset<2176){const auto* raw=reinterpret_cast<const uint8_t*>(child_source_.get());a.channel_after=raw[a.channel_offset];a.channel_written=a.channel_writes==1&&a.channel_after==0xFF;}
 }
 if(out.slot.entered){auto& s=out.slot;const auto h=slots_.Snapshot();
  s.live=h.live&&h.owner==s.owner&&h.generation==s.generation&&h.serial==s.serial&&h.base==s.base&&h.sound_owner==out.child.sound_owner&&h.sound_generation==out.child.sound_generation;
  s.current_readback=s.live&&child_source_&&child_candidate_&&current_port_&&child_parent_==current_port_;
  if(s.current_readback){const auto* raw=reinterpret_cast<const uint8_t*>(child_source_.get())+out.child.index*136;
   s.current_note=int16_t(uint16_t(raw[8])|(uint16_t(raw[9])<<8));s.current_flags=uint16_t(raw[10])|(uint16_t(raw[11])<<8);
   s.stored_channel=int8_t(raw[0x2A]);s.current_parent_flags=current_port_->link.flags;
   s.child_links={reinterpret_cast<uintptr_t>(child_candidate_->link0),reinterpret_cast<uintptr_t>(child_candidate_->link4)};
   s.free_unlinked=child_head_.link4!=child_candidate_&&child_candidate_->link0!=&child_head_;
   const auto& head=current_port_->child0;s.parent_linked=head.next==child_candidate_&&head.previous==child_candidate_&&child_candidate_->next==&head&&child_candidate_->previous==&head;
   const auto& inner=child_inner_[out.child.index];s.child_sentinel_storage=reinterpret_cast<uintptr_t>(&inner);
   s.child_sentinel_links={reinterpret_cast<uintptr_t>(inner.next),reinterpret_cast<uintptr_t>(inner.previous)};s.child_sentinel_ids={inner.id,int32_t(inner.flags)};
   for(size_t i=0;i<36;++i)s.parameters[i]=uint16_t(raw[0x30+2*i])|(uint16_t(raw[0x31+2*i])<<8);
   for(size_t i=0;i<4;++i){const size_t o=0x78+4*i;s.scratch78[i]=uint32_t(raw[o])|(uint32_t(raw[o+1])<<8)|(uint32_t(raw[o+2])<<16)|(uint32_t(raw[o+3])<<24);}
   const auto current=slots_.Read(uint32_t(s.callback_id));
   s.low=int32_t(h.header[(s.kind*8+8)/4]);s.high=int32_t(h.header[(s.kind*8+12)/4]);s.cursor_after=int32_t(h.header[(s.kind*4+0x48)/4]);
   s.slot_word8=current.word8;s.slot_wordC=current.wordC;s.slot_kind=current.kind;s.allocation_flags=current.flags;s.slot_half6=uint16_t(current.half6);
   s.packet_word6=child_packet_.words[6];auto expected_packet=state_.port_input.words;expected_packet[6]=state_.slot.wave_word30;
   const bool packet_readback=child_packet_.words==expected_packet&&child_packet_.mode_slot==state_.port_input.mode_slot&&child_packet_.port_word24==state_.port_input.value;
   const auto half=[&](size_t at){return uint16_t(raw[at])|(uint16_t(raw[at+1])<<8);};
   const auto word=[&](size_t at){return uint32_t(raw[at])|(uint32_t(raw[at+1])<<8)|(uint32_t(raw[at+2])<<16)|(uint32_t(raw[at+3])<<24);};
   s.current_readback=current.callback_readback&&packet_readback&&current.id==s.callback_id&&current.flags==s.allocation_flags&&current.half6==int16_t(s.argument)&&current.callback_code==s.callback_codes[1]&&current.callback_context==s.callback_contexts[1]
    &&s.low==0&&s.high==15&&s.cursor_after==1&&current.word8==uint32_t(s.packed)&&current.wordC==0&&current.kind==uint32_t(s.kind)&&child_candidate_->id==s.current_note&&child_candidate_->flags==s.current_flags
    &&half(0x18)==uint16_t(inner.id)&&half(0x1A)==inner.flags&&half(0x24)==uint16_t(out.child.bank)&&raw[0x28]==uint8_t(out.child.channels)&&half(0x2C)==4096&&half(0x2E)==uint16_t(out.sfx.parent_returned?out.sfx.pitch_after:4096)&&word(0x1C)==0&&word(0x20)==0;
  }
 }
 if(out.sfx.entered){auto& a=out.sfx;const auto d=controls_.Snapshot();const auto h=slots_.Snapshot();GameNativeSfxControl16::Record record;
  const auto slot=slots_.Read(uint32_t(a.slot_id));a.live=d.live;a.platform_live=d.platform_live;
  a.current_readback=out.slot.current_readback&&d.live&&d.platform_live&&d.owner==a.owner&&d.generation==a.generation&&d.base==a.base&&d.core==a.core&&d.slot_base==h.base&&h.base==a.slot_base
   &&d.sound_owner==a.sound_owner&&d.sound_generation==a.sound_generation&&controls_.Read(a.runtime_index,record)&&a.runtime_index==slot.word1C&&slot.id==a.slot_id&&slot.kind==1
   &&record.flags==a.dirty_flags&&record.data==a.data_pointer&&record.word8==a.dirty_word8&&record.word10==a.dirty_word10&&record.word2C==a.dirty_word2C&&record.word30==a.dirty_word30
   &&slot.flags==a.slot_flags_after&&slot.byte5==a.slot_byte5&&child_wave_pointer_==a.child_wave&&a.parent_result==out.slot.child_pointer
   &&a.packet_storage==reinterpret_cast<uintptr_t>(&child_packet_)&&a.mode_slot==wave_output_&&current_port_&&a.port_word28==(uint32_t(current_port_->parameters[0])|(uint32_t(current_port_->parameters[1])<<16));
  try{const auto t=control_tail_.Describe();a.current_readback=a.current_readback&&t.owner==a.tail_owner&&t.generation==a.tail_generation&&t.serial==a.tail_serial&&t.offset==a.tail_offset&&t.size==a.tail_bytes&&t.host_address+t.offset==a.tail_base&&a.data_pointer==a.tail_base+child_packet_.words[6]&&wave_view_.ReadLE32(0x0C)==a.wave_wordC;}
  catch(const std::exception&){a.current_readback=false;}
 }
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
    if(advance_sfx_){uint8_t* returned_node=nullptr;
     const auto result=Child5B130(port,r.note,r.bank,r.selected_wave,int8_t(r.selected_byteC),0,-1,wave,port_input,returned_node);
     if(!result)return result;return CommandAfterParent5B49C(port,returned_node);
    }
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
GameFsStatus GameSourceSequencePool::Child5B130(Port& port,int32_t note,int32_t bank,uint32_t wave,int32_t byteC,int32_t packed,int32_t extra,const WaveCall& lookup,const ControlCall& port_input,uint8_t*& returned_node){
 returned_node=nullptr;parent_return_node_=nullptr;
 const auto effect=Child5B130(port,note,bank,wave,byteC,packed,extra,lookup,port_input);
 if(!effect){parent_return_node_=nullptr;return effect;}
 returned_node=parent_return_node_;parent_return_node_=nullptr;
 //Only this callee's genuine typed return supplies the command caller.
 auto& a=state_.sfx;a.parent_result=reinterpret_cast<uintptr_t>(returned_node);a.parent_return_pc=0x5B2D4;a.parent_returned=true;state_.control.child_returned=true;
 return {};
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
GameFsStatus GameSourceSequencePool::ReadProducedPort24(const ChildCall& child){
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
 return AllocateChild5ABF0(*p,state_.control.note,state_.control.bank,w.channels,child_packet_,child);
}
GameFsStatus GameSourceSequencePool::AllocateChild5ABF0(Port& port,int32_t note,int32_t bank,int32_t channels,const ChildPacket& packet,const ChildCall& child){
 auto& a=state_.port_input;a.child_entered=true;a.child_pc=0x5B220;a.child_delay_pc=0x5B224;a.note=note;a.bank=bank;a.channels=channels;
 a.frontier=state_.wave.frontier=state_.control.frontier=state_.frontier=0x5ABF0;
 if(&port!=current_port_||&packet!=&child_packet_||a.packet_storage!=reinterpret_cast<uintptr_t>(&packet)||a.words!=packet.words||a.mode_slot!=packet.mode_slot||packet.mode_slot!=wave_output_||a.value!=packet.port_word24
    ||note!=state_.control.note||bank!=state_.bank||channels!=state_.wave.channels)
  return {GameFsCode::Unavailable,0,"5ABF0 actual owned packet/port ABI changed"};
 if(child)return child(packet);
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
