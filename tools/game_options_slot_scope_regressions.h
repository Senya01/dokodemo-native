#pragma once
// ROOT integration supplement: genuine public component10 must not issue GUI authority.
// Reuses synthetic public UI/data primitives; no private test seam or old family call.
#include <type_traits>
namespace game_options_slot_scope_regressions {
using namespace game_options_voice_regressions;
static_assert(!std::is_default_constructible_v<GameOptionsChildCallScope>);
static_assert(!std::is_copy_constructible_v<GameOptionsChildCallScope>);
static_assert(!std::is_move_constructible_v<GameOptionsChildCallScope>);
static_assert(!std::is_destructible_v<GameOptionsChildCallScope>);
struct Settings final:GameOptionSettings {
game_options_port24_regressions::Fixture f;Root root{true,701,17,0,{{2,3,3}}};GameSystemOptionFrontier* front=nullptr;
Q gui=0,sequence=0,epoch=0;
U row_calls=0,gain_calls=0,feedback_calls=0,service_calls=0,restore_calls=0,legacy_calls=0;
bool historic=false;
std::vector<U> events;std::vector<Row> rows;
Root OptionSettings()const override{return root;}
Sound OptionSoundOwner()const override{return f.bank.SourceControl();}
bool OptionRowWritable()const override{return true;}
GameFsStatus RestoreOptionSettings(const Root&,const Words&)override{++restore_calls;return {GameFsCode::Unavailable,0,"test restore unsupported"};}
GameFsStatus WriteOptionRow(const Root& expected,Cause cause,U stage,U value,Row& out)override{
Check(GameOptionSnapshotExact(expected,root)&&gui&&sequence&&epoch&&front,"V025");
const auto s=front->Snapshot();Row r;r.cause=cause;r.local_before=root.words;r.local_after=root.words;
const U ordinal=row_calls++;
if(cause==Cause::Down){
Check(stage<3&&ordinal==stage&&value==root.words[stage]&&s.row_writes==stage
&&s.selection56==1&&s.previous50==1,"V026");
}else if(cause==Cause::Left){
Check(!stage&&ordinal==3&&root.words==Words{2,3,3}&&value==2
&&s.row_writes==3&&s.previous50==UINT32_MAX,"V027");
r.selection_before=1;--r.local_after[1];
}else{
Check(cause==Cause::LeftCommon&&stage<3&&ordinal==4+stage&&value==root.words[stage]
&&root.words==Words{2,2,3}&&s.row_writes==4+stage&&s.previous50==1
&&s.row_feedback.feedback_returned&&GameSourceVoiceFeedbackValid(s.row_feedback)
&&s.sound_owner.voice.returned&&!s.sound_owner.voice.services,"V028");
r.selection_before=1;
}
auto& w=r.write;w.gui_generation=gui;w.input_sequence=sequence;w.input_epoch=epoch;w.stage=stage;
w.field=cause==Cause::Left?1:stage;w.call_pc=cause==Cause::Left?0x351CC:0x34CCC+8*stage;
w.setter=0x24B1C+0x18*w.field;w.value=value;w.before=root;root.words[w.field]=value;++root.writes;w.after=root;
Check(GameOptionRowReceiptValid(r),"V029");rows.push_back(r);events.push_back(w.call_pc);out=r;return {};
}
GameFsStatus QueryOptionGain(const Root& expected,int32_t value,GameOptionGainObservation& out)override{
Check(GameOptionSnapshotExact(expected,root)&&value==105,"V030");
++gain_calls;events.push_back(0x4AFBC);GameOptionGainObservation r;
const auto status=f.bank.QuerySourceGain(f.bank.SourceControl(),value,r.source);if(!status)return status;
const auto meta=f.bank.Snapshot();r.metadata_ready=meta.metadata_ready;r.native_metadata_slot=meta.native_metadata_slot;
r.source_id_known=true;r.source_dispatched=r.source.dispatched;out=r;return status;
}
GameFsStatus ReadOptionFeedback(const Root& expected,Feedback& out)override{
Check(GameOptionSnapshotExact(expected,root)&&root.words==Words{2,2,3}&&row_calls==4,"V031");
++feedback_calls;events.push_back(0x4AC38);
return historic?f.bank.ReadOptionsFeedback(f.bank.SourceControl(),root.words[1],out)
:f.bank.ReadOptionsVoiceFeedback(f.bank.SourceControl(),root.words[1],out);
}
GameFsStatus ServiceOptionVoice(const Root& expected,Feedback& out)override{
Check(GameOptionSnapshotExact(expected,root)&&row_calls==7&&gain_calls==2,"V032");
const auto s=front->Snapshot();
Check(s.draw_frames>s.row_left_draw_frames_before&&s.bar_frames==Words{3,8,4}
&&s.row_feedback.feedback_returned&&s.previous50==1,"V033");
++service_calls;events.push_back(0x56254);
f.bank.ObserveOptionsWorkerTick();f.bank.ObserveOptionsWorkerTick();
const auto status=f.bank.ServiceOptionsSlot(f.bank.SourceControl(),out);
Check(!status&&GameSourceSlotPrefixValid(out)&&Same(out.after,f.bank.SourceControl()),"S-SCOPE genuine component10");return status;
}
GameFsStatus WriteOptionRight(const Root&,U,U,GameOptionWriteReceipt&)override{++legacy_calls;return {GameFsCode::Unavailable,0,"unexpected Right"};}
GameFsStatus WriteOptionBackup(const Root&,U,U,GameOptionWriteReceipt&)override{++legacy_calls;return {GameFsCode::Unavailable,0,"unexpected Cancel"};}
GameFsStatus WriteOptionYes(const Root&,U,U,GameOptionWriteReceipt&)override{++legacy_calls;return {GameFsCode::Unavailable,0,"unexpected Yes"};}
};
struct Rig {
ui::Source source;GameFontResource font;Settings settings;GameTransition fade;GameSystemOptionFrontier front;GameGraphics graphics;
Q sequence=0;Gui before_left;GameOwnedMessageSnapshot frozen_message;
explicit Rig(bool historic=false){
settings.historic=historic;settings.front=&front;
Ok(font.Initialize(ui::Font()),"V034");Check(graphics.Initialize({}),"V035");
Check(fade.Initialize({{0,0}}),"V036");
Ok(front.Request({71,11,13,31,19,41},settings.root.words),"V037");
Ok(front.Initialize(source,font,settings,fade),"V038");front.SetChildLease({71,11,19,1,13,31,41});
for(U n=0;n<40&&front.Snapshot().phase!=3;++n){fade.Update();Ok(Tick(),"V039");}
Check(front.Snapshot().phase==3,"V040");source.blocked=true;
}
~Rig(){front.Abort();}
bool Send(U buttons){
const auto s=front.Snapshot();const auto& l=s.child_lease;GameInputFrame f;
f.buttons=buttons;f.focused=true;f.sequence=++sequence;f.main_menu_epoch=s.input_epoch;
f.message_owner_generation=l.script_generation;f.message_context=l.context;
f.returned_owner=l.available_owner;f.returned_generation=l.available_generation;return front.Input(f);
}
GameFsStatus Tick(){
const auto s=front.Snapshot();settings.gui=s.generation;settings.sequence=s.last_sequence;settings.epoch=s.input_epoch;return front.Tick(fade);
}
GameFsStatus Draw(){return front.Draw(graphics,font);}
void Neutral(){Check(Send(0),"V041");Ok(Tick(),"V042");}
GameFsStatus Left(){
Neutral();Ok(Draw(),"V043");frozen_message=front.Snapshot().message;
Check(Send(Down),"V044");Ok(Tick(),"V045");Neutral();Ok(Draw(),"V046");
before_left=front.Snapshot();Frozen(frozen_message,before_left.message);
Check(before_left.bar_frames==Words{3,9,4}&&before_left.row_writes==3&&before_left.gain_queries==1,"V047");
Check(Send(dokodemo::recovered::Left),"V048");return Tick();
}
};
inline void UnsealedComponent(){
 Rig r;Ok(r.Left(),"S-SCOPE actual Left/7stores/2queries");Ok(r.Draw(),"S-SCOPE actual bar8 draw");
 const auto before=r.front.Snapshot();Check(before.row_writes==7&&before.gain_queries==2&&before.bar_frames==Words{3,8,4},"S-SCOPE offered frame");
 const auto status=r.Tick();const auto after=r.front.Snapshot();
 Check(!status&&status.code==GameFsCode::Unavailable&&status.detail.find("lacks actual private issued callscope")!=std::string::npos,"S-SCOPE second gate refusal");
 Check(r.settings.service_calls==1&&after.failed&&after.row_voice_service.outcome==10&&GameSourceSlotPrefixValid(after.row_voice_service),"S-SCOPE genuine transport refused");
 Check(!after.child_service_scope_serial&&!after.child_service_sealed&&!after.child_service_committed&&!after.child_service_consumed,"S-SCOPE no issued cache");
 Check(Same(after.sound_owner,before.sound_owner)&&!Same(after.sound_owner,r.settings.f.bank.SourceControl()),"S-SCOPE no blind adoption");
 Check(!after.close_requests&&!after.polls&&!after.retirements&&!after.woken&&!after.restore_writes&&after.settings.words==Words{2,2,3},"S-SCOPE no lifecycle promotion");
 const auto current=r.settings.f.bank.SourceControl();Check(!r.Tick()&&r.settings.service_calls==1&&Same(current,r.settings.f.bank.SourceControl()),"S-SCOPE failed no replay");
}
}
inline void GameOptionsSlotScopeRegressionChecks(){game_options_slot_scope_regressions::UnsealedComponent();}
