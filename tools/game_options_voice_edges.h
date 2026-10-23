#pragma once
#include "game_options_voice_regressions.h"
//ROOT-owned supplement: genuine but wrong-value/unknown/dispatched second
//query outcomes must refuse; public scalar transport never issues authority.
inline void GameOptionsVoiceSecondQueryChecks(){
 using namespace dokodemo::recovered;namespace v=game_options_voice_regressions;
 struct Provider final:GameOptionSettings{
  v::Settings impl;uint32_t damage=0;
  GameOptionSettingsSnapshot OptionSettings()const override{return impl.OptionSettings();}
  GameSourceSoundSnapshot OptionSoundOwner()const override{return impl.OptionSoundOwner();}
  bool OptionRowWritable()const override{return true;}
  GameFsStatus RestoreOptionSettings(const v::Root& r,const v::Words& w)override{return impl.RestoreOptionSettings(r,w);}
  GameFsStatus WriteOptionRow(const v::Root& r,GameOptionRowCause c,uint32_t n,uint32_t x,GameOptionRowWriteReceipt& o)override{return impl.WriteOptionRow(r,c,n,x,o);}
  GameFsStatus ReadOptionFeedback(const v::Root& r,GameSourceFeedbackRead& o)override{return impl.ReadOptionFeedback(r,o);}
  GameFsStatus ServiceOptionVoice(const v::Root& r,GameSourceFeedbackRead& o)override{return impl.ServiceOptionVoice(r,o);}
  GameFsStatus QueryOptionGain(const v::Root& r,int32_t value,GameOptionGainObservation& o)override{
   if(!impl.gain_calls)return impl.QueryOptionGain(r,value,o);
   v::Check(GameOptionSnapshotExact(r,impl.root)&&value==105,"Q01");++impl.gain_calls;
   const auto status=impl.f.bank.QuerySourceGain(impl.OptionSoundOwner(),damage==0?45:105,o.source);
   o.source_id_known=damage!=1;o.source_dispatched=damage==2;return status;
  }
 };
 for(uint32_t damage=0;damage<3;++damage){
  v::ui::Source source;GameFontResource font;GameTransition fade;GameSystemOptionFrontier front;Provider p;p.damage=damage;p.impl.front=&front;
  v::Ok(font.Initialize(v::ui::Font()),"Q02");v::Check(fade.Initialize({{0,0}}),"Q03");
  v::Ok(front.Request({71,11,13,31,19,41},p.impl.root.words),"Q04");v::Ok(front.Initialize(source,font,p,fade),"Q05");front.SetChildLease({71,11,19,1,13,31,41});
  const auto tick=[&](){const auto s=front.Snapshot();p.impl.gui=s.generation;p.impl.sequence=s.last_sequence;p.impl.epoch=s.input_epoch;return front.Tick(fade);};
  uint64_t sequence=0;const auto send=[&](uint32_t buttons){const auto s=front.Snapshot();const auto& l=s.child_lease;GameInputFrame f;f.focused=true;f.buttons=buttons;f.sequence=++sequence;f.main_menu_epoch=s.input_epoch;f.message_owner_generation=l.script_generation;f.message_context=l.context;f.returned_owner=l.available_owner;f.returned_generation=l.available_generation;return front.Input(f);};
  for(uint32_t n=0;n<40&&front.Snapshot().phase!=3;++n){fade.Update();v::Ok(tick(),"Q06");}
  for(uint32_t button:{0u,uint32_t(Down),0u}){v::Check(send(button),"Q07");v::Ok(tick(),"Q08");}
  v::Check(send(Left),"Q09");const auto failure=tick();const auto s=front.Snapshot();const auto sound=p.OptionSoundOwner();
  v::Check(failure.code==GameFsCode::Unavailable&&failure.detail=="Options second actual gain return"&&s.failed&&s.source_consumer==0x4AFC8,"Q10");
  v::Check(s.row_writes==7&&s.gain_queries==2&&!s.gain_returned&&p.impl.gain_calls==2&&p.impl.service_calls==0&&GameSourceSoundGainValid(s.gain.source),"Q11");
  v::Check(sound.queries==2&&sound.last_query_gain==(damage==0?45:105)&&sound.voice.returned&&!sound.voice.services&&p.impl.root.words==v::Words{2,2,3}&&p.impl.root.writes==7,"Q12");
  v::NoExit(s);v::Check(v::old::SameStatus(failure,tick())&&p.impl.gain_calls==2,"Q13");front.Abort();v::Check(!p.impl.restore_calls&&!p.impl.legacy_calls&&p.impl.root.writes==7,"Q14");p.impl.f.bank.Close();
 }
}
