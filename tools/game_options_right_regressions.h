#pragma once
#include "game_options_open_regressions.h"
#include <tuple>
// SOURCE only; executed 0. Synthetic receipts have no private issuer credit.
namespace dokodemo::options_right_tests {
using namespace recovered;
namespace fx = options_open_tests;
using U=uint32_t;using Q=uint64_t;using Words=std::array<U,3>;
using SettingsState=GameOptionSettingsSnapshot;using WriteReceipt=GameOptionWriteReceipt;
inline void C(bool v,const char* id){if(!v)throw std::runtime_error(std::string("options-right ")+id);}
inline bool Eq(const GameFsStatus& a,const GameFsStatus& b){return a.code==b.code&&a.native_error==b.native_error&&a.detail==b.detail;}
inline void Ok(const GameFsStatus& s,const char* id){C(bool(s),id);}
inline Words Entry(){return {{2,3,3}};}
inline Words Edited(){return {{3,3,3}};}
inline U Field(U s){return std::array<U,4>{{1,0,1,2}}.at(s);}
inline U Pc(U s){return std::array<U,4>{{0x351CC,0x34CCC,0x34CD4,0x34CDC}}.at(s);}
inline U Setter(U s){return std::array<U,4>{{0x24B34,0x24B1C,0x24B34,0x24B4C}}.at(s);}
inline bool MessageSame(const GameOwnedMessageSnapshot& a,const GameOwnedMessageSnapshot& b){
 return std::tie(a.live,a.references,a.state64,a.complete68,a.close69,a.text_calls,a.updates,a.draws,a.callbacks,
     a.emissions,a.glyphs,a.shown,a.total,a.capacity,a.used,a.batch_stage,a.resets,a.flushes,a.delay70,a.idle74,
     a.flush_selector,a.appearance,a.rate,a.scale_x,a.scale_y,a.text)==
     std::tie(b.live,b.references,b.state64,b.complete68,b.close69,b.text_calls,b.updates,b.draws,b.callbacks,
     b.emissions,b.glyphs,b.shown,b.total,b.capacity,b.used,b.batch_stage,b.resets,b.flushes,b.delay70,b.idle74,
     b.flush_selector,b.appearance,b.rate,b.scale_x,b.scale_y,b.text);
}
inline WriteReceipt Receipt(SettingsState b,U s){
 WriteReceipt r;r.gui_generation=1;r.input_sequence=3;r.input_epoch=41;r.stage=s;
 r.field=Field(s);r.call_pc=Pc(s);r.setter=Setter(s);r.value=3;r.before=b;
 b.words[r.field]=3;++b.writes;r.after=b;return r;
}
inline void Corrupt(WriteReceipt& r,U fault){switch(fault){
 case 1:r.field=(r.field+1)%3;break;case 2:++r.call_pc;break;
 case 3:r.stage=(r.stage+1)%4;r.field=Field(r.stage);r.call_pc=Pc(r.stage);r.setter=Setter(r.stage);break;
 case 4:r.after.words[(r.field+1)%3]^=0x80000000u;break;case 5:++r.after.writes;break;
 case 6:++r.gui_generation;break;case 7:++r.input_sequence;break;case 8:++r.input_epoch;break;
 case 9:++r.before.owner;++r.after.owner;break;case 10:++r.before.generation;++r.after.generation;break;
 case 11:--r.before.writes;--r.after.writes;break;case 12:r.after.live=false;break;
 case 13:r.setter^=4;break;case 14:r.value=2;break;case 15:r.after.words[r.field]=2;break;
 case 16:r.before.live=r.after.live=false;break;case 18:r.input_sequence=0;break;
 case 19:r.gui_generation=0;break;case 20:r.input_epoch=0;break;
}}
struct Readonly:GameOptionSettings {
 SettingsState value{true,7001,8001,29,{{2,3,3}}};mutable U reads=0;U restores=0;
 SettingsState OptionSettings()const override{++reads;return value;}
 GameFsStatus RestoreOptionSettings(const SettingsState&,const Words&)override{
     ++restores;return {GameFsCode::Unavailable,59,"unexpected synthetic restore"};}
};
struct Provider final:Readonly {
 GameSystemOptionFrontier* gui=nullptr;bool writable=true;int fail_stage=-1;
 U fault=0,fault_stage=0,queries=0,attempts=0,base_sound=0,base_after=0;
 GameFsStatus failure{GameFsCode::IoFailure,73,"synthetic first writer failure"};
 GameFsStatus query_failure{GameFsCode::Unavailable,0,"Unsupported Options source sound ID read4AFC8"};
 std::vector<WriteReceipt> issued;
 bool OptionRightWritable()const override{return writable;}
 GameFsStatus WriteOptionRight(const SettingsState& expected,U stage,U word,WriteReceipt& out)override{
     ++attempts;const auto s=gui->Snapshot();
     C(stage==issued.size()&&stage<4&&word==3,"R001");
     C(GameOptionSnapshotExact(expected,value)&&GameOptionSnapshotExact(s.settings,value),"R002");
     C(s.current_words==Edited()&&s.passive_settings==Entry()&&s.right_steps==1,"R003");
     C(s.previous50==(stage?0:UINT32_MAX)&&s.selection56==0&&s.substate48==0,"R004");
     C(s.source_ui_sound_intents==base_sound+(stage?1:0)&&s.after_key_updates==base_after+(stage?1:0),"R005");
     C(s.right_writes==stage&&s.gain_queries==0&&s.source_consumer==Pc(stage),"R006");
     if(int(stage)==fail_stage)return failure;
     out=Receipt(value,stage);out.gui_generation=s.generation;out.input_sequence=s.last_sequence;out.input_epoch=s.input_epoch;
     value.words[Field(stage)]=word;++value.writes;
     C(GameOptionSnapshotExact(value,out.after),"R007");issued.push_back(out);
     if(stage==fault_stage){if(fault==17)value.words[2]^=0x80000000u;else Corrupt(out,fault);}return {};
 }
 GameFsStatus QueryOptionGain(const SettingsState& expected,int32_t gain,GameOptionGainObservation& out)override{
     ++queries;const auto s=gui->Snapshot();
     C(issued.size()==4&&s.right_writes==4&&s.gain_queries==1&&gain==105,"R008");
     C(GameOptionSnapshotExact(expected,value)&&value.words==Edited()&&value.writes==33,"R009");
     C(s.source_consumer==0x4AFC8&&s.previous50==0&&s.passive_settings==Entry(),"R010");
     out.metadata_ready=true;out.native_metadata_slot=63;out.source_id_known=false;out.source_dispatched=false;
     return query_failure; // There is deliberately no positive return stub.
 }
};
struct Rig {
 fx::Source source;GameFontResource font;GameTransition fade;GameSystemOptionFrontier option;Provider settings;Q sequence=0;
 Rig(){settings.gui=&option;Ok(font.Initialize(fx::Font()),"R011");C(fade.Initialize({{0,0}}),"R012");
     fade.SetDuration(1);Ok(option.Request(fx::Lease(),Entry()),"R013");option.SetChildLease(fx::Child());
     Ok(option.Initialize(source,font,settings,fade),"R014");
     for(U n=0;n<256&&option.Snapshot().phase!=3;++n){fade.Update();Ok(option.Tick(fade),"R015");}
     C(option.Snapshot().phase==3&&option.Snapshot().state44==1,"R016");
     settings.base_sound=option.Snapshot().source_ui_sound_intents;settings.base_after=option.Snapshot().after_key_updates;
 }
 GameInputFrame Frame(U buttons=0){const auto l=fx::Child();GameInputFrame f;f.buttons=buttons;f.focused=true;f.sequence=++sequence;
     f.message_owner_generation=l.script_generation;f.message_context=l.context;f.returned_owner=l.available_owner;
     f.returned_generation=l.available_generation;f.main_menu_epoch=option.Snapshot().input_epoch;return f;}
 void Send(U buttons){C(option.Input(Frame(buttons)),"R017");}
 void Idle(){Ok(option.Tick(fade),"R018");settings.base_after=option.Snapshot().after_key_updates;}
 GameFsStatus RunRight(U mask=recovered::Right){Send(0);Send(mask);return option.Tick(fade);}
};
inline void Terminal(Rig& r,const GameFsStatus& first){
 const auto before=r.option.Snapshot();const auto root=r.settings.value;
 const auto opens=r.source.opens,attempts=r.settings.attempts,queries=r.settings.queries,reads=r.settings.reads;
 GameGraphics graphics;const auto draws=graphics.Snapshot().draw_calls;
 for(U i=0;i<3;++i){C(Eq(r.option.Tick(r.fade),first),"R019");
     C(!r.option.Input(r.Frame(Right|Cross)),"R020");C(Eq(r.option.Draw(graphics,r.font),first),"R021");
     C(Eq(r.option.Initialize(r.source,r.font,r.settings,r.fade),first),"R022");}
 const auto after=r.option.Snapshot();
 C(r.source.opens==opens&&r.settings.attempts==attempts&&r.settings.queries==queries&&r.settings.reads==reads,"R023");
 C(MessageSame(before.message,after.message)&&after.draw_frames==before.draw_frames&&graphics.Snapshot().draw_calls==draws,"R024");
 C(after.phase==before.phase&&after.source_consumer==before.source_consumer&&after.source_ui_sound_intents==before.source_ui_sound_intents
     &&after.after_key_updates==before.after_key_updates&&after.right_writes==before.right_writes&&Eq(after.failure,first),"R025");
 r.option.Abort();const auto dead=r.option.Snapshot();
 C(!dead.live&&!dead.visible&&!dead.owner474&&!dead.template_owner&&!dead.clone_owner&&!dead.message.live&&r.source.option.expired()&&r.source.dialog.expired(),"R026");
 C(GameOptionSnapshotExact(root,r.settings.value)&&r.settings.restores==0,"R027");
 C(!dead.poll_completed&&!dead.retired&&!dead.woken&&!dead.polls&&!dead.retirements&&!dead.retirement_requests&&!dead.close_requests,"R028");
 C(Eq(r.option.Tick(r.fade),first)&&Eq(r.option.Draw(graphics,r.font),first)
     &&Eq(r.option.Initialize(r.source,r.font,r.settings,r.fade),first)&&!r.option.Input(r.Frame(Right)),"R029");
 C(r.source.opens==opens&&r.settings.attempts==attempts&&r.settings.queries==queries&&r.settings.reads==reads,"R030");
}
inline void Helpers(){
 SettingsState b{true,71,81,29,{{2,3,3}}};
 for(U stage=0;stage<4;++stage){const auto good=Receipt(b,stage);C(GameOptionRightReceiptValid(good)&&GameOptionReceiptExact(good,good),"R031");
     for(U fault=1;fault<=20;++fault){if(fault==17)continue;auto bad=good;Corrupt(bad,fault);
         C(!GameOptionReceiptExact(good,bad),"R032");
         if(fault!=3&&fault!=6&&fault!=7&&fault!=8&&fault!=9&&fault!=10&&fault!=11)C(!GameOptionRightReceiptValid(bad),"R033");}
     b=good.after;}
 auto overflow=Receipt(b,0);overflow.before.writes=UINT64_MAX;overflow.after.writes=0;C(!GameOptionRightReceiptValid(overflow),"R034");
 for(U bit=0;bit<32;++bit){auto full=b;full.words={{1u<<bit,0xFEED0003u,0x80000003u}};
     C(GameOptionSnapshotExact(full,full),"R035");
     for(U field=0;field<3;++field){auto changed=full;changed.words[field]^=1u<<bit;C(!GameOptionSnapshotExact(full,changed),"R036");}
     const auto saved=Receipt(full,0);C(GameOptionRightReceiptValid(saved)&&saved.after.words[0]==full.words[0]&&saved.after.words[2]==full.words[2],"R037");}
}
inline void Defaults(){
 Readonly p;const auto b=p.value;auto receipt=Receipt(b,0),saved=receipt;GameOptionGainObservation gain;
 C(!p.OptionRightWritable(),"R038");
 C(p.WriteOptionRight(b,0,3,receipt).code==GameFsCode::Unavailable&&GameOptionReceiptExact(saved,receipt),"R039");
 C(p.QueryOptionGain(b,105,gain).code==GameFsCode::Unavailable&&!gain.source_id_known&&!gain.source_dispatched
     &&GameOptionSnapshotExact(b,p.value),"R040");
 Rig r;r.settings.writable=false;const auto status=r.RunRight();const auto s=r.option.Snapshot();
 C(status.code==GameFsCode::Unavailable&&s.source_consumer==0x3516C&&!s.right_steps&&!r.settings.attempts&&!r.settings.queries
     &&r.settings.value.words==Entry()&&!r.settings.restores,"R041");Terminal(r,status);
}
inline void Prefix(){for(bool active:{false,true}){
 Rig r;if(active){r.Send(0);r.Idle();C(!r.option.Snapshot().message.complete68,"R042");}
 const auto before=r.option.Snapshot();const auto status=r.RunRight();const auto s=r.option.Snapshot();
 C(Eq(status,r.settings.query_failure)&&s.failed&&s.source_consumer==0x4AFC8,"R043");
 C(s.right_steps==1&&s.right_writes==4&&r.settings.issued.size()==4&&r.settings.attempts==4&&r.settings.queries==1,"R044");
 C(s.current_words==Edited()&&s.passive_settings==Entry()&&s.previous50==0&&s.selection56==0&&s.substate48==0,"R045");
 C(GameOptionSnapshotExact(s.settings,r.settings.value)&&s.settings.words==Edited()&&s.settings.writes==33,"R046");
 for(U i=0;i<4;++i){const auto& receipt=s.right_receipts[i];C(GameOptionReceiptExact(receipt,r.settings.issued[i])&&GameOptionRightReceiptValid(receipt)
     &&receipt.field==Field(i)&&receipt.setter==Setter(i)&&receipt.call_pc==Pc(i)&&receipt.before.writes==29+i&&receipt.after.writes==30+i,"R047");
     if(i!=1)C(receipt.before.words==receipt.after.words,"R048");}
 C(s.gain_value==105&&s.gain_queries==1&&s.gain.metadata_ready&&s.gain.native_metadata_slot==63&&!s.gain.source_id_known&&!s.gain.source_dispatched
     &&!s.gain_returned&&!s.right_message_continued,"R049");
 C(s.source_ui_sound_intents==before.source_ui_sound_intents+1&&s.after_key_updates==before.after_key_updates+1,"R050");
 C(MessageSame(before.message,s.message)&&s.message.updates==s.right_message_updates_before&&s.draw_frames==s.right_draw_frames_before
     &&s.message.rate==0.75f,"R051");Terminal(r,status);
}}
inline void Faults(){for(U stage=0;stage<4;++stage)for(U fault=1;fault<=20;++fault){
 Rig r;r.settings.fault=fault;r.settings.fault_stage=stage;const auto status=r.RunRight();const auto s=r.option.Snapshot();
 C(status.code==GameFsCode::Unavailable&&status.detail=="Options actual source writer receipt/readback mismatch","R052");
 C(s.failed&&s.right_writes==stage&&r.settings.attempts==stage+1&&r.settings.value.writes==30+stage&&!r.settings.queries
     &&s.source_consumer==Pc(stage),"R053");Terminal(r,status);
}}
inline void PartialFailures(){for(U stage=0;stage<4;++stage)for(bool empty:{false,true}){
 Rig r;r.settings.fail_stage=int(stage);r.settings.failure={GameFsCode::IoFailure,101+stage,empty?"":"first provider status"};
 const auto status=r.RunRight();const auto s=r.option.Snapshot();
 C(Eq(status,r.settings.failure)&&Eq(s.failure,status),"R054");
 C(s.right_writes==stage&&r.settings.issued.size()==stage&&r.settings.value.writes==29+stage&&r.settings.attempts==stage+1&&!r.settings.queries,"R055");
 C(s.current_words==Edited()&&s.passive_settings==Entry()&&s.previous50==(stage?0:UINT32_MAX)
     &&r.settings.value.words==(stage>1?Edited():Entry()),"R056");Terminal(r,status);
}}
inline void QueryFailures(){for(U n=0;n<3;++n){Rig r;r.settings.query_failure={n?GameFsCode::IoFailure:GameFsCode::Unavailable,211+n,n==2?"":"first query status"};
 const auto status=r.RunRight();C(Eq(status,r.settings.query_failure)&&r.option.Snapshot().right_writes==4&&r.settings.queries==1,"R057");Terminal(r,status);}}
inline void Priority(){for(U mask:{U(Down|Up|Right|Cross),U(Up|Right|Cross),U(Right|Cross)}){
 Rig r;const auto status=r.RunRight(mask);const auto s=r.option.Snapshot();
 const U pc=(mask&Down)?0x350B0:(mask&Up)?0x3512C:0x4AFC8;
 C(status.code==GameFsCode::Unavailable&&s.source_consumer==pc&&!r.settings.restores,"R058");
 C(r.settings.attempts==((mask&(Down|Up))?0u:4u),"R059");Terminal(r,status);
}}
inline void InputGates(){
 for(U kind=0;kind<8;++kind){Rig r;
     if(kind==0)r.Send(Right); // held at context creation: no focused release
     else if(kind==1||kind==2){r.Send(0);r.Send(Right);auto lost=r.Frame(Right);lost.focused=kind!=1;lost.overflow_resync=kind==2;
         C(!r.option.Input(lost),"R060");r.Send(Right);}
     else{auto bad=r.Frame(0);if(kind==3)--bad.main_menu_epoch;else if(kind==4)++bad.message_context;
         else if(kind==5)++bad.returned_owner;else if(kind==6)++bad.returned_generation;else ++bad.message_owner_generation;
         C(!r.option.Input(bad),"R061");r.Send(Right);}
     r.Idle();C(!r.settings.attempts&&!r.settings.queries&&!r.option.Snapshot().failed,"R062");
     const auto status=r.RunRight();C(r.option.Snapshot().right_writes==4,"R063");Terminal(r,status);
 }
 for(U kind=0;kind<2;++kind){Rig r;r.Send(0);r.Send(Right);r.Send(kind?0:Right);const auto latest=r.sequence;
     const auto status=r.option.Tick(r.fade);C(r.option.Snapshot().right_writes==4&&r.option.Snapshot().right_receipts[0].input_sequence==latest,"R064");Terminal(r,status);}
 Rig r;r.Send(0);auto duplicate=r.Frame(Right);--duplicate.sequence;C(!r.option.Input(duplicate),"R065");
 r.Idle();C(!r.settings.attempts,"R066");const auto status=r.RunRight();Terminal(r,status);
}
inline void RetiredSettings(){for(U kind=0;kind<5;++kind){Rig r;r.Send(0);switch(kind){
 case 0:r.settings.value.live=false;break;case 1:++r.settings.value.owner;break;case 2:++r.settings.value.generation;break;
 case 3:++r.settings.value.writes;break;case 4:r.settings.value.words[2]^=0x80000000u;break;}
 C(!r.option.Input(r.Frame(Right)),"R067");const auto status=r.option.Tick(r.fade);
 C(status.code==GameFsCode::Unavailable&&!r.settings.attempts&&!r.settings.queries&&r.option.Snapshot().right_steps==0,"R068");Terminal(r,status);
}}
inline void ColdActor(){GameActorScript actor;GameOwnerEventTarget target;GameSystemOptionSnapshot gui;WriteReceipt receipt;
 C(actor.PrepareMenuOptionWrite(target,gui,0,3,receipt).code==GameFsCode::Unavailable,"R069");
 C(actor.CommitMenuOptionWrite(target,gui,receipt).code==GameFsCode::Unavailable,"R070");
 C(actor.VerifyMenuOptionPrefix(target,gui).code==GameFsCode::Unavailable,"R071");
}
}
inline void GameOptionsRightRegressionChecks(){using namespace dokodemo::options_right_tests;
 Helpers();Defaults();Prefix();Faults();PartialFailures();QueryFailures();Priority();InputGates();RetiredSettings();ColdActor();
}
