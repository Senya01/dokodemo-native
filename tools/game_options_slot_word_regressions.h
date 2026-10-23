#pragma once
// ROOT correction after first actual Windows failure: WAVE+30 is an owned
// source input, not a zero default. The released standalone factory supplies
// real pre-registration bytes; no provider or private issuance is fabricated.
namespace game_options_slot_word_regressions {
inline void OwnedWaveWord(){
 using namespace game_options_slot_regressions;
 for(const uint32_t value:std::array<uint32_t,2>{{0,191232}}){
  Input input;input.view.WriteLE32(6272+0x30,value);GameSoundBank bank;
  Need(bool(bank.InitializeSource17F0())&&bool(bank.Register(input.view)),"SLOT-W-real-registration");
  Read voice,r;Need(bool(bank.ReadOptionsVoiceFeedback(bank.SourceControl(),2,voice)),"SLOT-W-voice");
  bank.ObserveOptionsWorkerTick();bank.ObserveOptionsWorkerTick();
  Unavailable(bank.ServiceOptionsSlot(bank.SourceControl(),r),"SLOT-W-6016C");
  const auto& s=r.after.voice.sequence.slot;
  Need(GameSourceSlotPrefixValid(r)&&s.wave_word30==value&&s.packet_word6==value&&s.packet_written&&s.current_readback
   &&GameSourceSoundSnapshotExact(r.after,bank.SourceControl()),"SLOT-W-actual-input-to-current-packet");
  auto changed=r;changed.after.voice.sequence.slot.wave_word30^=1;
  Need(!GameSourceSlotExact(s,changed.after.voice.sequence.slot)&&!GameSourceSlotPrefixValid(changed),"SLOT-W-source-input-binding");
  changed=r;changed.after.voice.sequence.slot.packet_word6^=1;
  Need(!GameSourceSlotExact(s,changed.after.voice.sequence.slot)&&!GameSourceSlotPrefixValid(changed),"SLOT-W-packet-binding");
  changed.after.voice.sequence.slot.wave_word30^=1;
  Need(GameSourceSlotPrefixValid(changed)&&!GameSourceSoundSnapshotExact(changed.after,bank.SourceControl()),"SLOT-W-coherent-transport-is-not-current-issuer");
  auto* current=reinterpret_cast<uint32_t*>(s.packet_storage)+6;const auto saved=*current;*current^=1;
  auto drift=r;drift.after=bank.SourceControl();const bool rejected=!drift.after.voice.sequence.slot.current_readback&&!GameSourceSlotPrefixValid(drift);
  *current=saved;Need(rejected&&GameSourceSoundSnapshotExact(r.after,bank.SourceControl()),"SLOT-W-real-owned-nonzero-drift-restore");
 }
}
}
inline void GameOptionsSlotWordRegressionChecks(){game_options_slot_word_regressions::OwnedWaveWord();}
