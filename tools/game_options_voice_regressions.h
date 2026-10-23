#pragma once
// SOURCE ONLY; unrun. CORE_RELEASE.v7. Diagnostic names: REPORT.v1.json.
// Synthetic inputs; actual public Sound APIs; NON-MATCHING policies.
#include "game_options_se5_regressions.h"
#include "game_options_row1_regressions.h"
namespace game_options_voice_regressions {
using namespace dokodemo::recovered;
namespace old=game_options_se5_regressions;
namespace ui=dokodemo::options_row1_tests;
using U=uint32_t;using Q=uint64_t;using Words=std::array<U,3>;
using Sound=GameSourceSoundSnapshot;using Voice=GameSourceVoiceSnapshot;
using Feedback=GameSourceFeedbackRead;using Gui=GameSystemOptionSnapshot;
using Root=GameOptionSettingsSnapshot;using Cause=GameOptionRowCause;
using Row=GameOptionRowWriteReceipt;
inline void Check(bool value,const char* id){if(!value)throw std::runtime_error(std::string("options-voice ")+id);}
inline void Ok(const GameFsStatus& s,const char* id){if(!s)throw std::runtime_error(std::string("options-voice ")+id+": "+s.detail);}
inline bool Same(const Sound& a,const Sound& b){return old::Same(a,b)&&GameSourceVoiceExact(a.voice,b.voice);}
template<class A> inline bool Zero(const A& a){return std::all_of(a.begin(),a.end(),[](auto x){return x==0;});}
inline void Frozen(const GameOwnedMessageSnapshot& a,const GameOwnedMessageSnapshot& b){Check(ui::MessageKey(a)==ui::MessageKey(b),"V001");}
inline void NoExit(const Gui& s){
Check(!s.right_steps&&!s.right_writes&&!s.restore_writes&&!s.yes_writes&&!s.yes_consumed&&!s.close_kind
&&!s.close_requests&&!s.close_completions&&!s.polls&&!s.poll_completed&&!s.retirement_requests&&!s.retirements
&&!s.retired&&!s.woken&&!s.prompt_opens&&!s.prompt_returns,"V002");
}
struct Input {
std::shared_ptr<old::Lease> lease=std::make_shared<old::Lease>();
PackageView view;
explicit Input(U auxiliary=0){
lease->bytes.assign(207,0xA5);lease->known.assign(207,1);
view=PackageStorage::Attach(lease,207).Subview(7,193);
for(U n=0;n<193;++n)view.WriteU8(n,0);
view.WriteLE32(0,0x44584753);view.WriteLE32(8,188);view.WriteLE32(12,0x80000001);
view.WriteLE32(16,0x444E4752);view.WriteLE32(20,4);
view.WriteLE32(28,0x44514553);view.WriteLE32(32,128);
view.WriteLE32(40,1);view.WriteLE32(44,64);
view.WriteLE32(68,7);view.WriteLE32(96,112);view.WriteLE32(116,auxiliary);
view.WriteLE32(164,0x45564157);view.WriteLE32(168,4);
view.WriteLE32(176,0x454D414E);view.WriteLE32(180,4);
}
};
struct Fixture {
Input input;GameSoundBank bank;
explicit Fixture(U auxiliary=0):input(auxiliary){Ok(bank.InitializeSource17F0(),"V003");Ok(bank.Register(input.view),"V004");}
};
inline void CheckReturn(const Feedback& r,const Sound& before,const Sound& now){
Check(GameSourceVoiceFeedbackValid(r)&&!GameSourceFeedbackReachedValid(r)&&!GameSourceVoiceServiceValid(r),"V005");
Check(Same(r.owner,before)&&Same(r.after,now),"V006");
Check(r.outcome==1&&r.attempted&&r.bank_attached&&r.bank_handle_known&&r.read_returned&&r.getter_returned
&&r.start_returned&&r.handle_stored&&r.feedback_returned&&r.context_index==5&&r.read_pc==0x4AC84
&&r.frontier==0x4AD4C&&r.selector==0&&r.params_written==6&&r.virtual_target==0x4AF54
&&r.wrapper==0x56930&&r.issuer==0x56598&&r.bank_handle==63&&r.gain==45&&r.rate==1.0f
&&r.group==0&&r.event==6&&r.extra==0&&r.mode==2,"V007");
const auto& v=now.voice;
Check(now.live&&now.bank_live&&!now.failed&&!now.feedback_failed&&now.feedback_attempts==1
&&now.feedback_writes==6&&now.feedback_gain==45&&now.feedback_frontier==0x4AD4C,"V008");
Check(v.live&&v.active&&v.returned&&v.owner&&v.generation&&v.allocation==1
&&v.id>=0&&v.id<16&&v.id!=r.bank_handle&&v.bank==63&&now.se5.handle==v.id
&&now.se5.bank_handle==63&&now.current_id==-1&&now.last_id_writer==0x18E4,"V009");
Check(v.auxiliary_null&&v.bank_ids==std::array<int32_t,4>{63,63,63,63}
&&v.bank_refs_before==0&&v.bank_refs_after==1&&v.retain_writer==0x55A2C
&&old::SameRegistration(before.registration,now.registration)&&now.registration.word30==0,"V010");
Check(v.flags==3&&v.mode==2&&!v.group&&v.event==6&&!v.extra&&v.descriptor==0x7210
&&v.initialized_mask==0x7F&&Zero(v.words74)&&Zero(v.words80)&&Zero(v.wordsA0)
&&Zero(v.initial_lane_controls)&&Zero(v.initial_lane_flags),"V011");
Check(v.initial_left==4096&&v.initial_right==4096&&v.initial_dirty_mask==0xC&&v.initial_port_writes==2
&&v.port_writes==5&&v.master_left==128&&v.master_right==128&&v.return_pc==0x567BC,"V012");
std::array<uint16_t,12> ports{};ports[0]=ports[1]=0x20C;ports[2]=ports[3]=1440;ports[9]=4096;
Check(v.working==ports&&Zero(v.published)&&Zero(v.consumer)&&v.dirty==1&&v.persistent==1
&&!v.published_dirty&&!v.published_persistent&&!v.consumer_dirty&&!v.consumer_persistent,"V013");
Check(!v.services&&!v.callback_entered&&!v.callback_returned&&!v.callback_pc&&!v.frontier
&&!v.initializer_store_pc&&!v.sequence_result_known&&v.sequence_slot==0,"V014");
Check(now.se5.gain==45&&now.se5.rate==1&&now.se5.request==6&&!now.se5.position&&!now.se5.resource_state
&&now.queries==before.queries&&now.last_query_gain==before.last_query_gain,"V015");
}
inline Feedback Start(Fixture& f){
const auto before=f.bank.SourceControl();Feedback r;
Ok(f.bank.ReadOptionsVoiceFeedback(before,2,r),"V016");
CheckReturn(r,before,f.bank.SourceControl());return r;
}
inline void ReturnedVoice(){
Fixture f;const auto before=f.bank.SourceControl();
Check(before.registration.id==63&&before.registration.live&&before.registration.attached&&before.registration.coherent
&&before.registration.writer==0x4AB58&&before.registration.word30==0&&!before.voice.live,"V017");
Start(f);const auto returned=f.bank.SourceControl();Feedback duplicate;
Check(!f.bank.ReadOptionsVoiceFeedback(before,2,duplicate)&&!duplicate.attempted&&Same(returned,f.bank.SourceControl()),"V018");
Check(!f.bank.ReadOptionsVoiceFeedback(returned,2,duplicate)&&!duplicate.attempted&&Same(returned,f.bank.SourceControl()),"V019");
GameSourceSoundGainReceipt query;Ok(f.bank.QuerySourceGain(returned,105,query),"V020");
Check(GameSourceSoundGainValid(query)&&query.id==-1&&query.value==105&&query.returned&&query.negative_guard&&!query.dispatched
&&query.read_pc==0x4AFC8&&query.return_pc==0x4AFE0&&Same(query.before,returned),"V021");
auto expected=returned;++expected.queries;expected.last_query_gain=105;
Check(Same(expected,query.after)&&Same(expected,f.bank.SourceControl())
&&GameSourceVoiceExact(returned.voice,expected.voice),"V022");
const auto meta=f.bank.Snapshot();
Check(meta.metadata_ready&&!meta.mixer_ready&&!meta.playback_requests&&meta.last_playback_result==-1,"V023");
}
struct Settings final:GameOptionSettings {
Fixture f;Root root{true,701,17,0,{{2,3,3}}};GameSystemOptionFrontier* front=nullptr;
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
return f.bank.ServiceOptionsVoice(f.bank.SourceControl(),out);
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
inline void LeftState(Rig& r){
const auto s=r.front.Snapshot();const auto sound=r.settings.f.bank.SourceControl();
Check(!s.failed&&s.live&&s.phase==3&&s.state44==1&&s.selection56==1&&s.previous50==1&&!s.substate48
&&s.row_selections==1&&s.row_left_steps==1&&s.row_feedback_attempts==1&&s.row_writes==7
&&s.gain_queries==2&&s.gain_value==105&&s.gain_returned&&s.marker_frame==2,"V049");
Check(s.current_words==Words{2,2,3}&&s.settings.words==Words{2,2,3}&&s.passive_settings==Words{2,3,3}
&&s.settings.writes==7&&GameOptionSnapshotExact(s.settings,r.settings.root),"V050");
Check(s.after_key_updates==r.before_left.after_key_updates+1&&s.draw_frames==r.before_left.draw_frames
&&s.row_left_draw_frames_before==r.before_left.draw_frames&&s.source_ui_sound_intents==r.before_left.source_ui_sound_intents+1
&&!s.voice_service_attempts&&!r.settings.service_calls,"V051");
CheckReturn(s.row_feedback,r.before_left.sound_owner,s.row_feedback.after);
Check(Same(s.sound_owner,sound)&&GameSourceVoiceExact(s.row_feedback.after.voice,sound.voice)&&sound.queries==2
&&sound.last_query_gain==105&&GameSourceSoundGainValid(s.gain.source)
&&Same(s.gain.source.before,s.row_feedback.after)&&Same(s.gain.source.after,sound),"V052");
Check(r.settings.rows.size()==7&&r.settings.row_calls==7&&r.settings.gain_calls==2&&r.settings.feedback_calls==1,"V053");
for(U n=0;n<7;++n){
Check(GameOptionRowReceiptValid(s.row_receipts[n])&&GameOptionRowReceiptExact(s.row_receipts[n],r.settings.rows[n]),"V054");
Check(s.row_receipts[n].cause==(n<3?Cause::Down:n==3?Cause::Left:Cause::LeftCommon),"V055");
}
const std::vector<U> order={0x34CCC,0x34CD4,0x34CDC,0x4AFBC,0x351CC,0x4AC38,0x34CCC,0x34CD4,0x34CDC,0x4AFBC};
Check(r.settings.events==order,"V056");
Frozen(r.frozen_message,s.message);NoExit(s);
}
inline void OptionsLeft(){
Rig r;Ok(r.Left(),"V057");LeftState(r);const auto before=r.front.Snapshot();
Ok(r.Draw(),"V058");const auto drawn=r.front.Snapshot();
Check(drawn.bar_frames==Words{3,8,4}&&drawn.draw_frames==before.draw_frames+1
&&drawn.manual_bars==before.manual_bars+3&&!drawn.voice_service_attempts&&drawn.quads>0,"V059");
Frozen(before.message,drawn.message);NoExit(drawn);
const auto root=r.settings.root;const auto sound=r.settings.f.bank.SourceControl();
r.front.Abort();
Check(GameOptionSnapshotExact(root,r.settings.root)&&Same(sound,r.settings.f.bank.SourceControl())
&&!r.settings.restore_calls&&!r.settings.legacy_calls&&!r.settings.service_calls,"V060");
NoExit(r.front.Snapshot());
}
inline void CheckService(const Feedback& r,const Sound& before,const Sound& now){
Check(GameSourceVoiceServiceValid(r)&&!GameSourceVoiceFeedbackValid(r)&&!GameSourceFeedbackReachedValid(r)
&&Same(r.owner,before)&&Same(r.after,now),"V061");
Check(r.outcome==2&&r.frontier==0x57CF0&&r.feedback_returned&&r.start_returned&&r.handle_stored
&&now.feedback_frontier==0x4AD4C&&!now.feedback_failed&&now.se5.handle==before.se5.handle,"V062");
const auto& v=now.voice;
Check(v.services==1&&v.callback_entered&&!v.callback_returned&&!v.sequence_result_known
&&v.callback_pc==0x578CC&&v.frontier==0x57CF0&&v.initializer_store_pc==0x578D4
&&v.sequence_slot==-1&&v.words80[0]==UINT32_MAX,"V063");
Check(v.words80[1]==0&&v.words80[2]==0&&v.words80[3]==0&&Zero(v.words74)&&Zero(v.wordsA0)
&&Zero(v.initial_lane_controls)&&Zero(v.initial_lane_flags),"service-no58C04-output");
std::array<uint16_t,12> stored{};stored[1]=0x20C;stored[2]=stored[3]=1440;stored[9]=4096;
auto consumed=stored;consumed[0]=0x20C;
Check(v.working==stored&&v.published==stored&&v.consumer==consumed&&v.dirty==0&&v.persistent==1
&&v.published_dirty==0&&v.published_persistent==1&&v.consumer_dirty==1&&v.consumer_persistent==1,"56254-562D4-copy-clear");
Check(v.id==before.voice.id&&v.owner==before.voice.owner&&v.generation==before.voice.generation
&&v.bank_refs_after==1&&v.port_writes==5&&v.initial_port_writes==2
&&old::SameRegistration(before.registration,now.registration)&&now.registration.word30==0&&now.bank_live
&&now.queries==before.queries,"V064");
}
inline void DistinctService(){
Rig r;Ok(r.Left(),"V065");LeftState(r);
const auto before_draw=r.front.Snapshot();const auto sound=r.settings.f.bank.SourceControl();const auto events=r.settings.events;
for(U n=0;n<2;++n)Ok(r.Tick(),"V066");
Check(!r.settings.service_calls&&Same(sound,r.settings.f.bank.SourceControl())&&r.settings.events==events
&&r.front.Snapshot().draw_frames==before_draw.draw_frames,"V067");
Frozen(before_draw.message,r.front.Snapshot().message);
Ok(r.Draw(),"V068");const auto drawn=r.front.Snapshot();
Check(drawn.bar_frames==Words{3,8,4}&&drawn.draw_frames>drawn.row_left_draw_frames_before,"V069");
const auto failure=r.Tick();const auto stopped=r.front.Snapshot();const auto after=r.settings.f.bank.SourceControl();
Check(failure.code==GameFsCode::Unavailable&&failure.detail.find("57CF0")!=std::string::npos
&&stopped.failed&&stopped.source_consumer==0x57CF0&&stopped.voice_service_attempts==1
&&r.settings.service_calls==1&&r.settings.events.back()==0x56254,"V070");
CheckService(stopped.row_voice_service,sound,after);
Check(GameSourceFeedbackExact(stopped.row_feedback,drawn.row_feedback)&&stopped.row_writes==7
&&stopped.gain_queries==2&&stopped.bar_frames==Words{3,8,4}&&stopped.draw_frames==drawn.draw_frames,"V071");
const auto root=r.settings.root;const U opens=r.source.opens;const auto terminal_events=r.settings.events;
for(U n=0;n<2;++n){
Check(old::SameStatus(r.Tick(),failure)&&old::SameStatus(r.Draw(),failure)&&!r.Send(Left|Cross|Circle),"V072");
Check(old::SameStatus(r.front.Initialize(r.source,r.font,r.settings,r.fade),failure),"V073");
}
Check(GameOptionSnapshotExact(root,r.settings.root)&&Same(after,r.settings.f.bank.SourceControl())&&r.source.opens==opens
&&r.settings.events==terminal_events&&r.settings.service_calls==1&&!r.settings.restore_calls&&!r.settings.legacy_calls,"V074");
Frozen(drawn.message,r.front.Snapshot().message);NoExit(r.front.Snapshot());
r.front.Abort();Check(Same(after,r.settings.f.bank.SourceControl())&&GameOptionSnapshotExact(root,r.settings.root),"V075");
NoExit(r.front.Snapshot());
}
struct Change {const char* id;void(*apply)(Feedback&);};
inline void ReceiptMutations(const Feedback& issued,bool service){
const auto valid=service?&GameSourceVoiceServiceValid:&GameSourceVoiceFeedbackValid;
Check(valid(issued),"V076");
#define VOICE_BAD(ID, BODY) {ID,[](Feedback& r){BODY;}}
const Change envelope[]={
VOICE_BAD("V077",r.attempted=false),VOICE_BAD("V078",r.bank_attached=false),
VOICE_BAD("V079",r.bank_handle_known=false),VOICE_BAD("V080",r.read_returned=false),
VOICE_BAD("V081",r.getter_returned=false),VOICE_BAD("V082",r.start_returned=false),
VOICE_BAD("V083",r.handle_stored=false),VOICE_BAD("V084",r.feedback_returned=false),
VOICE_BAD("V085",r.context_index=4),VOICE_BAD("V086",r.read_pc=0x4ACA0),
VOICE_BAD("V087",r.frontier=0x58C04),VOICE_BAD("V088",r.selector=1),
VOICE_BAD("V089",r.gain=105),VOICE_BAD("V090",r.params_written=5),
VOICE_BAD("V091",r.virtual_target=0),VOICE_BAD("V092",r.wrapper=0),
VOICE_BAD("V093",r.issuer=0),VOICE_BAD("V094",r.bank_handle=-1),
VOICE_BAD("V095",r.group=1),VOICE_BAD("V096",r.event=5),
VOICE_BAD("V097",r.extra=1),VOICE_BAD("V098",r.mode=1),
VOICE_BAD("V099",r.rate=0.5f),VOICE_BAD("V100",++r.context_owner),
VOICE_BAD("V101",++r.context_generation),VOICE_BAD("V102",r.outcome=0)
};
const Change state[]={
VOICE_BAD("V103",r.after.voice.live=false),VOICE_BAD("V104",r.after.voice.active=false),
VOICE_BAD("V105",r.after.voice.returned=false),VOICE_BAD("V106",r.after.voice.auxiliary_null=false),
VOICE_BAD("V107",r.after.voice.owner=0),VOICE_BAD("V108",r.after.voice.generation=0),
VOICE_BAD("V109",++r.after.voice.allocation),VOICE_BAD("V110",++r.after.voice.bank),
VOICE_BAD("V111",r.after.voice.id=r.bank_handle),VOICE_BAD("V112",r.after.voice.flags=1),
VOICE_BAD("V113",r.after.voice.mode=1),VOICE_BAD("V114",r.after.voice.group=1),
VOICE_BAD("V115",r.after.voice.event=5),VOICE_BAD("V116",r.after.voice.extra=1),
VOICE_BAD("V117",r.after.voice.descriptor=0),VOICE_BAD("V118",r.after.voice.bank_refs_before=1),
VOICE_BAD("V119",r.after.voice.bank_refs_after=2),VOICE_BAD("V120",r.after.voice.retain_writer=0),
VOICE_BAD("V121",r.after.voice.initial_left=1440),VOICE_BAD("V122",r.after.voice.initial_right=1440),
VOICE_BAD("V123",r.after.voice.initial_dirty_mask=0),VOICE_BAD("V124",r.after.voice.initial_port_writes=0),
VOICE_BAD("V125",r.after.voice.initialized_mask=0),VOICE_BAD("V126",r.after.voice.port_writes=2),
VOICE_BAD("V127",r.after.voice.return_pc=0),VOICE_BAD("V128",r.after.voice.callback_returned=true),
VOICE_BAD("V129",r.after.voice.sequence_result_known=true),VOICE_BAD("V130",r.after.voice.master_left=127),
VOICE_BAD("V131",++r.after.voice.services),VOICE_BAD("V132",r.after.voice.callback_pc=0x58C04),
VOICE_BAD("V133",r.after.voice.frontier=0x58C04),VOICE_BAD("V134",r.after.voice.initializer_store_pc=0x57908),
VOICE_BAD("V135",r.after.se5.handle=r.bank_handle),VOICE_BAD("V136",r.after.current_id=r.bank_handle),
VOICE_BAD("V137",++r.after.queries),VOICE_BAD("V138",++r.after.registration.word30),
VOICE_BAD("V139",r.after.registration.digest[0]^=1),VOICE_BAD("V140",--r.after.registration.tail.size)
};
#undef VOICE_BAD
for(const auto& c:envelope){auto bad=issued;c.apply(bad);Check(!valid(bad)&&!GameSourceFeedbackExact(issued,bad),c.id);}
for(const auto& c:state){auto bad=issued;c.apply(bad);Check(!valid(bad)&&!GameSourceFeedbackExact(issued,bad),c.id);}
for(U lane=0;lane<3;++lane)for(U at=0;at<12;++at){auto bad=issued;
auto& port=lane==0?bad.after.voice.working:lane==1?bad.after.voice.published:bad.after.voice.consumer;port[at]^=1;
Check(!valid(bad)&&!GameSourceVoiceExact(issued.after.voice,bad.after.voice),"V141");
}
for(U at=0;at<4;++at){auto bad=issued;bad.after.voice.bank_ids[at]^=1;Check(!valid(bad),"V142");}
for(U at=0;at<2;++at){auto bad=issued;bad.after.voice.words74[at]^=1;Check(!valid(bad),"V143");}
for(U at=0;at<4;++at){auto bad=issued;bad.after.voice.words80[at]^=1;Check(!valid(bad),"V144");}
for(U at=0;at<8;++at){auto bad=issued;bad.after.voice.wordsA0[at]^=1;Check(!valid(bad),"wordsA0-mutation");}
for(U at=0;at<6;++at){auto bad=issued;bad.after.voice.initial_lane_controls[at]^=1;Check(!valid(bad),"V145");
bad=issued;bad.after.voice.initial_lane_flags[at]^=1;Check(!valid(bad),"V146");
}
for(U at=0;at<6;++at){auto bad=issued;auto& v=bad.after.voice;
switch(at){case 0:v.dirty^=1;break;case 1:v.persistent^=1;break;case 2:v.published_dirty^=1;break;
case 3:v.published_persistent^=1;break;case 4:v.consumer_dirty^=1;break;case 5:v.consumer_persistent^=1;break;}
Check(!valid(bad),"V147");
}
{auto bad=issued;bad.after.voice.callback_entered=!bad.after.voice.callback_entered;Check(!valid(bad),"V148");}
{auto bad=issued;bad.after.voice.sequence_slot=service?0:-1;Check(!valid(bad),"V149");}
}
inline void Authority(){
Fixture f;const auto original=f.bank.SourceControl();const auto issued=Start(f);ReceiptMutations(issued,false);
const auto returned=f.bank.SourceControl();Feedback out;
for(U n=0;n<7;++n){auto forged=returned;
switch(n){case 0:++forged.owner;break;case 1:++forged.generation;break;case 2:++forged.registration.serial;break;
case 3:++forged.registration.word30;break;case 4:++forged.voice.owner;break;case 5:++forged.voice.bank_refs_after;break;
case 6:forged.voice.working[2]=4096;break;}
Check(!f.bank.ServiceOptionsVoice(forged,out)&&!out.attempted&&Same(returned,f.bank.SourceControl()),"V150");
}
Fixture foreign;const auto untouched=foreign.bank.SourceControl();
Check(!foreign.bank.ServiceOptionsVoice(returned,out)&&!out.attempted&&Same(untouched,foreign.bank.SourceControl()),"V151");
GameSourceSoundGainReceipt query;Ok(f.bank.QuerySourceGain(returned,105,query),"V152");
const auto current=f.bank.SourceControl();
Check(!f.bank.ServiceOptionsVoice(returned,out)&&!out.attempted&&Same(current,f.bank.SourceControl()),"V153");
const auto stopped=f.bank.ServiceOptionsVoice(current,out);Check(stopped.code==GameFsCode::Unavailable,"V154");
CheckService(out,current,f.bank.SourceControl());ReceiptMutations(out,true);const auto after=f.bank.SourceControl();
Feedback repeat;Check(!f.bank.ServiceOptionsVoice(after,repeat)&&!repeat.attempted&&Same(after,f.bank.SourceControl()),"V155");
Check(old::SameRegistration(original.registration,after.registration),"V156");
std::weak_ptr<old::Lease> weak=f.input.lease;f.input.view.Reset();f.input.lease.reset();
Check(!weak.expired()&&Same(after,f.bank.SourceControl()),"V157");
f.bank.Close();const auto closed=f.bank.SourceControl();
Check(weak.expired()&&!closed.live&&!closed.bank_live&&!closed.voice.live&&!closed.voice.active
&&!closed.registration.live&&!closed.se5.bank_handle_known,"V158");
Check(!f.bank.ServiceOptionsVoice(after,repeat)&&!repeat.attempted&&Same(closed,f.bank.SourceControl()),"V159");
for(U n=0;n<7;++n){Fixture a;Start(a);const auto live=a.bank.SourceControl();
switch(n){case 0:a.input.view.WriteU8(0,0);break;case 1:a.input.view.WriteU8(116,1);break;
case 2:a.input.view.WriteU8(188,1);break;case 3:a.input.lease->known[7+192]=0;break;
case 4:++a.input.lease->generation;break;case 5:++a.input.lease->serial;break;case 6:a.input.lease->live=false;break;}
const auto invalid=a.bank.SourceControl();Check(!invalid.bank_live,"V160");
Check(!a.bank.ServiceOptionsVoice(live,repeat)&&!repeat.attempted&&Same(invalid,a.bank.SourceControl()),"V161");
Check(!a.bank.ReadOptionsVoiceFeedback(live,2,repeat)&&!repeat.attempted&&Same(invalid,a.bank.SourceControl()),"V162");
}
Fixture reopen;const auto first=Start(reopen);reopen.bank.Close();
Ok(reopen.bank.InitializeSource17F0(),"V163");Ok(reopen.bank.Register(reopen.input.view),"V164");
const auto fresh=reopen.bank.SourceControl();
Check(fresh.registration.id==first.after.registration.id&&fresh.registration.generation!=first.after.registration.generation
&&fresh.registration.serial>first.after.registration.serial&&fresh.owner!=first.after.owner,"V165");
Check(!reopen.bank.ReadOptionsVoiceFeedback(first.owner,2,repeat)&&!repeat.attempted&&Same(fresh,reopen.bank.SourceControl()),"V166");
Start(reopen);
Fixture partial(1);const auto before=partial.bank.SourceControl();Feedback failed;
const auto failure=partial.bank.ReadOptionsVoiceFeedback(before,2,failed);const auto pending=partial.bank.SourceControl();
Check(failure.code==GameFsCode::Unavailable&&pending.bank_live&&pending.voice.live&&!pending.voice.active
&&!pending.voice.returned&&pending.voice.bank_refs_after==1&&pending.voice.allocation==1
&&pending.voice.port_writes==0&&!failed.start_returned&&!failed.handle_stored&&!failed.feedback_returned
&&pending.se5.handle==-1&&pending.feedback_failed,"V167");
Check(!GameSourceVoiceFeedbackValid(failed)&&!GameSourceFeedbackReachedValid(failed),"V168");
Check(old::SameStatus(failure,partial.bank.ReadOptionsVoiceFeedback(before,2,repeat))
&&GameSourceFeedbackExact(failed,repeat)&&Same(pending,partial.bank.SourceControl()),"V169");
Check(!partial.bank.ServiceOptionsVoice(pending,repeat)&&!repeat.attempted&&Same(pending,partial.bank.SourceControl()),"V170");
partial.bank.Close();Check(!partial.bank.SourceControl().voice.live,"V171");
}
inline void HistoricAndAbort(){
old::Feedback();old::Legacy();
Rig r(true);const auto failure=r.Left();const auto stopped=r.front.Snapshot();const auto sound=r.settings.f.bank.SourceControl();
Check(failure.code==GameFsCode::Unavailable&&failure.detail.find("56598")!=std::string::npos
&&stopped.failed&&stopped.row_writes==4&&stopped.gain_queries==1&&stopped.previous50==UINT32_MAX
&&GameSourceFeedbackReachedValid(stopped.row_feedback)&&!GameSourceVoiceFeedbackValid(stopped.row_feedback)
&&!sound.voice.live&&!sound.voice.returned&&!sound.registration.word30&&sound.se5.handle==-1,"V172");
Check(stopped.draw_frames==r.before_left.draw_frames&&stopped.bar_frames==r.before_left.bar_frames
&&!stopped.voice_service_attempts&&!r.settings.service_calls,"V173");
const auto root=r.settings.root;
Check(root.words==Words{2,2,3}&&root.writes==4&&sound.queries==1,"V174");
Frozen(r.frozen_message,stopped.message);NoExit(stopped);
r.front.Abort();
Check(!r.front.Snapshot().live&&GameOptionSnapshotExact(root,r.settings.root)&&Same(sound,r.settings.f.bank.SourceControl())
&&!r.settings.restore_calls&&!r.settings.legacy_calls,"V175");
Check(old::SameStatus(failure,r.Tick())&&old::SameStatus(failure,r.Draw()),"V176");
NoExit(r.front.Snapshot());
}
} 
inline void GameOptionsVoiceRegressionChecks(){
namespace voice=game_options_voice_regressions;
voice::ReturnedVoice();
voice::OptionsLeft();
voice::DistinctService();
voice::Authority();
voice::HistoricAndAbort();
}

