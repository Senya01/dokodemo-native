#pragma once
#include "../src/recovered/game_system_option_native.h"
#include "../src/recovered/game_content_native.h" // public FONT definition only
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <tuple>
namespace dokodemo::options_row1_tests {
using namespace recovered;
// SOURCE only; unrun. Synthetic transport is not private authority.
using U=uint32_t;using Q=uint64_t;using Bytes=std::vector<uint8_t>;using Words=std::array<U,3>;using RowReceipt=GameOptionRowWriteReceipt;using RootSnapshot=GameOptionSettingsSnapshot;using Cause=GameOptionRowCause;using GuiSnapshot=GameSystemOptionSnapshot;
inline void C(bool v,const char* id){if(!v)throw std::runtime_error(std::string("options-row1 ")+id);}
inline void Ok(const GameFsStatus& s,const char* id){if(!s)throw std::runtime_error(std::string("options-row1 ")+id+": "+s.detail);}
inline constexpr auto RootEq=&GameOptionSnapshotExact;
inline constexpr auto SoundEq=&GameSourceSoundSnapshotExact;
inline bool Eq(const GameFsStatus& a,const GameFsStatus& b){return a.code==b.code&&a.native_error==b.native_error&&a.detail==b.detail;}
inline void Put(Bytes& b,U at,U v,U n=4){for(U i=0;i<n;++i)b.at(at+i)=uint8_t(v>>(8*i));}
inline void Float(Bytes& b,U at,float v){U bits;std::memcpy(&bits,&v,4);Put(b,at,bits);}
struct Storage final:PackageStorageLease {
Bytes bytes,known;bool live=true;Q generation=1;
explicit Storage(Bytes b):bytes(std::move(b)),known(bytes.size(),1){}
bool Span(PackageStorageSpan& s,std::string& e)const override{
if(!live){e="H002";return false;}
s.bytes=const_cast<uint8_t*>(bytes.data());s.initialized=const_cast<uint8_t*>(known.data());s.size=bytes.size();
s.host_address=Q(reinterpret_cast<uintptr_t>(bytes.data()));s.owner=s.host_address;s.generation=generation;s.serial=1;s.readable=true;s.writable=false;return true;
}
};
inline PackageView View(Bytes b,std::shared_ptr<Storage>* receipt=nullptr){const auto n=b.size();auto p=std::make_shared<Storage>(std::move(b));if(receipt)*receipt=p;return PackageStorage::Attach(p,n);}
inline Bytes Gim(){Bytes b(132);const char magic[]="MIG.00.1PSP";std::copy(magic,magic+sizeof(magic),b.begin());
for(U at:{16u,32u,48u}){Put(b,at,at==16?2:at==32?3:4,2);Put(b,at+2,16,2);Put(b,at+4,132-at);Put(b,at+8,at==48?84:16);Put(b,at+12,16);}
Put(b,64,48,2);Put(b,68,3,2);Put(b,72,1,2);Put(b,74,1,2);Put(b,76,32,2);Put(b,78,4,2);Put(b,80,1,2);
Put(b,88,48);Put(b,92,64);Put(b,96,68);Put(b,104,1,2);Put(b,106,1,2);Put(b,108,3,2);Put(b,110,1,2);Put(b,112,64);Put(b,128,0xFF205060);return b;
}
inline PackageView Font(){constexpr U count=16384,pixels=60+count,stride=8192,palette=pixels+stride;Bytes b(palette+64);
Put(b,0,0x31544658);Put(b,4,1,2);Put(b,6,1,2);Put(b,8,count);b[12]=1;Put(b,16,60);
Put(b,20,count,2);Put(b,22,256,2);Put(b,24,1,2);Put(b,28,4);Put(b,32,256,2);Put(b,36,64,2);
Put(b,40,stride);Put(b,44,pixels);Put(b,48,3);Put(b,52,16);Put(b,56,palette);
std::fill(b.begin()+60,b.begin()+pixels,uint8_t(1));std::fill(b.begin()+pixels,b.begin()+palette,uint8_t(0x11));Put(b,palette+4,0xFFFFFFFF);return View(std::move(b));
}
struct Ui {
Bytes b;U clips;
Ui(U nodes,U roots,U count):b(40+nodes*8+count*52+32),clips(40+nodes*8){
Put(b,0,0x30413242);Put(b,4,1);Put(b,8,32);Put(b,12,nodes);Put(b,16,40);Put(b,20,count);Put(b,24,clips);Put(b,28,roots);
const U name=clips+count*52;Put(b,36,name);const char stem[]="options-test.GIM";std::copy(stem,stem+sizeof(stem),b.begin()+name);
for(U i=0;i<nodes;++i)Put(b,40+i*8+4,14,2);for(U i=0;i<count;++i)Put(b,clips+i*52+48,8);
}
U Alloc(U n){const U at=U(b.size());b.resize(b.size()+n);return at;}
void Link(U node,U sibling,U child){Put(b,40+node*8,sibling,2);Put(b,42+node*8,child,2);}
void Tracks(U clip,U kind,const std::vector<std::pair<U,std::array<float,4>>>& values){
const U keys=Alloc(U(values.size())*8);Put(b,clips+clip*52+kind*8,U(values.size()));Put(b,clips+clip*52+kind*8+4,keys);
const U stride=(kind==1||kind==3)?16:12;
for(U i=0;i<values.size();++i){const U at=Alloc(stride*2);Put(b,keys+i*8,values[i].first,2);Put(b,keys+i*8+2,2,2);Put(b,keys+i*8+4,at);
    for(U key=0;key<2;++key){const U p=at+key*stride;const auto& v=values[i].second;
        if(kind==0||kind==4)Put(b,p,U(int32_t(key&&kind==0?v[2]:v[0])));
        else{Float(b,p,key?v[2]:v[0]);if(kind==1||kind==3)Float(b,p+4,key?v[3]:v[1]);}
        if(kind!=0)Put(b,p+stride-8,1);Put(b,p+stride-4,key?1:0);
    }
}
}
void Pos(U clip,const std::vector<std::pair<U,std::array<float,4>>>& v){Tracks(clip,3,v);}
};
inline Bytes Option(){Ui u(100,5,5);u.Link(0,0,97);u.Link(97,98,0);u.Link(98,99,0);
for(U i=0;i<5;++i){u.Tracks(i,0,{{i,{{1,0,1,0}}}});u.Pos(i,{{i,{{float(10+i),float(20+i),0,0}}}});}
u.Pos(0,{{0,{{-80,-60,0,0}}},{97,{{0,0,0,0}}},{98,{{0,24,0,0}}},{99,{{0,48,0,0}}}});return std::move(u.b);
}
inline Bytes Dialog(){Ui u(49,4,5);u.Link(0,0,26);u.Link(3,0,48);
for(U i=0;i<5;++i)u.Tracks(i,0,{{i<4?i:3,{{1,0,1,0}}}});
u.Pos(4,{{3,{{-64,32,4,2}}},{48,{{2,3,1,1}}}});return std::move(u.b);
}
struct Source final:GameResourceSource {
U opens=0;bool blocked=false;
GameFsStatus OpenResource(const std::string& p,PackageView& out)override{
++opens;if(blocked)return {GameFsCode::IoFailure,88,"H003"};
if(p=="option\\gui\\option\\OPTION.CB2A"){out=View(Option());return {};}
if(p=="data\\gui\\dialog\\DIALOG.CB2A"){out=View(Dialog());return {};}
if(p=="option\\gui\\option\\options-test.GIM"||p=="data\\gui\\dialog\\options-test.GIM"){out=View(Gim());return {};}
return {GameFsCode::NotFound,31,"H004"};
}
};
inline PackageView Bank(std::shared_ptr<Storage>* lease){Bytes b(16);Put(b,0,0x44584753);return View(std::move(b),lease);}
inline auto MessageKey(const GameOwnedMessageSnapshot& s){return std::tie(s.live,s.template_owner,s.clone_owner,s.text_owner,s.registered,s.complete68,s.close69,
s.references,s.state64,s.mode98C,s.columns,s.rows,s.width,s.height,s.text_calls,s.updates,s.draws,s.callbacks,s.emissions,s.glyphs,s.shown,s.total,
s.capacity,s.used,s.batch_stage,s.resets,s.flushes,s.rate_setting,s.rate_updates,s.delay70,s.idle74,s.flush_selector,s.appearance,s.rate,s.scale_x,s.scale_y,s.text);}
inline void FrozenMessage(const GameOwnedMessageSnapshot& a,const GameOwnedMessageSnapshot& b){C(MessageKey(a)==MessageKey(b),"H005");}
struct Settings final:GameOptionSettings {
RootSnapshot state{true,101,7,0,{{2,3,3}}};
GameSourceSoundControl sound;std::shared_ptr<Storage> bank;GameSystemOptionFrontier* front=nullptr;
Q gui=0,sequence=0,epoch=0;U attempts=0,gain_calls=0,feedback_calls=0,restore_calls=0,legacy_calls=0;
U fail_before=99,damage_at=99,damage=0;bool row_writable=true,fail_gain=false,damage_gain=false;
GameFsStatus injected{GameFsCode::IoFailure,61,"H006"};
std::vector<RowReceipt> rows;std::vector<U> events;
Settings(){Ok(sound.Initialize17F0(),"H007");Ok(sound.BindSe5(Bank(&bank)),"H008");}
RootSnapshot OptionSettings()const override{return state;}
GameSourceSoundSnapshot OptionSoundOwner()const override{return sound.Snapshot();}
bool OptionRowWritable()const override{return row_writable;}
GameFsStatus RestoreOptionSettings(const RootSnapshot&,const Words&)override{++restore_calls;return {GameFsCode::Unavailable,0,"H009"};}
GameFsStatus WriteOptionRow(const RootSnapshot& expected,Cause cause,U stage,U value,RowReceipt& out)override{
const U ordinal=attempts++;
if(ordinal==fail_before)return injected;
if(!RootEq(expected,state)||state.writes==UINT64_MAX||!gui||!sequence||!epoch)
    return {GameFsCode::Unavailable,0,"H010"};
RowReceipt r;r.cause=cause;r.local_before=state.words;r.local_after=state.words;
if(cause==Cause::Down){
    if(stage>=3||ordinal!=stage||value!=state.words[stage])return {GameFsCode::Unavailable,0,"H011"};
    const auto s=front->Snapshot();C(s.selection56==1&&s.previous50==1&&s.row_selections==1&&s.row_writes==stage,"H012");
    C(s.current_words==state.words&&s.settings.writes==state.writes,"H013");
}else if(cause==Cause::Left){
    if(stage||ordinal!=3||!state.words[1]||state.words[1]>INT32_MAX||value!=state.words[1]-1)return {GameFsCode::Unavailable,0,"H014"};
    r.selection_before=1;--r.local_after[1];
}else return {GameFsCode::Unavailable,0,"H015"};
auto& w=r.write;w.gui_generation=gui;w.input_sequence=sequence;w.input_epoch=epoch;w.stage=stage;w.field=cause==Cause::Down?stage:1;
w.call_pc=cause==Cause::Down?0x34CCC+8*stage:0x351CC;w.setter=0x24B1C+0x18*w.field;w.value=value;
w.before=state;state.words[w.field]=value;++state.writes;w.after=state;rows.push_back(r);events.push_back(w.call_pc);out=r;
if(ordinal==damage_at)switch(damage){case 0:out.cause=Cause(99);break;case 1:out.write.input_sequence++;break;
    case 2:out.write.input_epoch++;break;case 3:out.write.gui_generation++;break;case 4:out.local_after[0]^=0x100;break;
    case 5:out.write.after.writes++;break;case 6:out.write.call_pc+=4;break;case 7:state.words[0]^=0x100;break;}
return {};
}
GameFsStatus QueryOptionGain(const RootSnapshot& expected,int32_t value,GameOptionGainObservation& out)override{
++gain_calls;events.push_back(0x4AFBC);if(fail_gain)return injected;
if(!RootEq(expected,state)||value!=105)return {GameFsCode::Unavailable,0,"H016"};
GameOptionGainObservation r;const auto status=sound.Gain4AFBC(sound.Snapshot(),value,r.source);if(!status)return status;
r.source_id_known=true;r.source_dispatched=false;if(damage_gain)r.source.return_pc+=4;out=r;return {};
}
GameFsStatus ReadOptionFeedback(const RootSnapshot& expected,GameSourceFeedbackRead& out)override{
++feedback_calls;events.push_back(0x4AC38);
if(!RootEq(expected,state))return {GameFsCode::Unavailable,0,"H017"};
return sound.Feedback4AC38(sound.Snapshot(),out); // no successful feedback stub
}
GameFsStatus WriteOptionRight(const RootSnapshot&,U,U,GameOptionWriteReceipt&)override{++legacy_calls;return {GameFsCode::Unavailable,0,"H018"};}
GameFsStatus WriteOptionBackup(const RootSnapshot&,U,U,GameOptionWriteReceipt&)override{++legacy_calls;return {GameFsCode::Unavailable,0,"H019"};}
GameFsStatus WriteOptionYes(const RootSnapshot&,U,U,GameOptionWriteReceipt&)override{++legacy_calls;return {GameFsCode::Unavailable,0,"H020"};}
};
struct Rig {
Source source;GameFontResource font;Settings settings;GameTransition fade;GameSystemOptionFrontier front;Q seq=0;
explicit Rig(U row1=3,Q writes=0){settings.state.words[1]=row1;settings.state.writes=writes;settings.front=&front;
Ok(font.Initialize(Font()),"H021");C(fade.Initialize({{0,0}}),"H022");
Ok(front.Request({71,11,13,31,19,41},settings.state.words),"H023");Ok(front.Initialize(source,font,settings,fade),"H024");
front.SetChildLease({71,11,19,1,13,31,41});
for(U n=0;n<40&&front.Snapshot().phase!=3;++n){fade.Update();Ok(Tick(),"H025");}
C(front.Snapshot().phase==3,"H026");source.blocked=true;
}
~Rig(){front.Abort();}
GameInputFrame Frame(U buttons){const auto s=front.Snapshot();const auto& l=s.child_lease;GameInputFrame f;f.buttons=buttons;f.focused=true;f.sequence=++seq;
f.main_menu_epoch=s.input_epoch;f.message_owner_generation=l.script_generation;f.message_context=l.context;f.returned_owner=l.available_owner;f.returned_generation=l.available_generation;return f;}
bool Send(U buttons){return front.Input(Frame(buttons));}
GameFsStatus Tick(){const auto s=front.Snapshot();settings.gui=s.generation;settings.sequence=s.last_sequence;settings.epoch=s.input_epoch;return front.Tick(fade);}
void Neutral(){C(Send(0),"H027");Ok(Tick(),"H028");}
GuiSnapshot DownNow(){Neutral();const auto before=front.Snapshot();C(Send(Down),"H029");Ok(Tick(),"H030");return before;}
GameFsStatus Draw(GameGraphics& g){return front.Draw(g,font);}
};
inline void NoTerminal(const GuiSnapshot& s){C(!s.right_steps&&!s.right_writes&&!s.restore_writes&&!s.yes_writes&&!s.yes_consumed&&!s.close_kind
&&!s.close_requests&&!s.close_completions&&!s.polls&&!s.poll_completed&&!s.retirement_requests&&!s.retirements&&!s.retired&&!s.woken&&!s.prompt_opens&&!s.prompt_returns,"H031");}
inline void NoReplay(Rig& r,const GameFsStatus& first){
const auto root=r.settings.state;const auto sound=r.settings.sound.Snapshot();const auto before=r.front.Snapshot();const auto events=r.settings.events;
const U io=r.source.opens,attempts=r.settings.attempts;GameGraphics g;C(g.Initialize({}),"H032");
for(U n=0;n<3;++n){C(Eq(r.Tick(),first)&&Eq(r.Draw(g),first),"H033");C(!r.Send(Left|Cross|Circle),"H034");
C(Eq(r.front.Initialize(r.source,r.font,r.settings,r.fade),first),"H035");}
C(RootEq(root,r.settings.state)&&SoundEq(sound,r.settings.sound.Snapshot()),"H036");
C(r.settings.attempts==attempts&&r.settings.events==events&&r.source.opens==io&&!r.settings.restore_calls&&!r.settings.legacy_calls,"H037");
const auto after=r.front.Snapshot();C(after.after_key_updates==before.after_key_updates&&after.bar_frames==before.bar_frames,"H038");
FrozenMessage(before.message,after.message);NoTerminal(after);r.front.Abort();const auto aborted=r.front.Snapshot();
C(RootEq(root,r.settings.state)&&!aborted.live&&!aborted.poll_completed&&!aborted.retired&&!aborted.woken,"H039");
C(Eq(r.Tick(),first)&&Eq(r.Draw(g),first)&&!r.settings.restore_calls,"H040");
}
inline void CausalDown(){Rig r;r.Neutral();GameGraphics g;C(g.Initialize({}),"H041");Ok(r.Draw(g),"H042");
const auto b=r.front.Snapshot();const auto owner=r.settings.sound.Snapshot();C(r.Send(Down),"H043");Ok(r.Tick(),"H044");const auto s=r.front.Snapshot();
C(s.row_selections==1&&s.selection56==1&&s.previous50==1&&s.marker_frame==2&&s.row_writes==3&&!s.row_left_steps,"D01");
C(s.current_words==b.current_words&&s.passive_settings==b.passive_settings&&s.settings.words==b.settings.words&&s.settings.writes==b.settings.writes+3,"D02");
for(U n=0;n<3;++n){const auto& q=s.row_receipts[n];C(GameOptionRowReceiptValid(q)&&GameOptionRowReceiptExact(q,r.settings.rows[n]),"D03");
C(q.write.stage==n&&q.write.field==n&&q.cause==Cause::Down&&q.selection_before==0&&q.selection_after==1,"D04");
C(q.write.before.words==q.write.after.words&&q.write.after.writes==q.write.before.writes+1,"D05");
C(RootEq(q.write.before,n?s.row_receipts[n-1].write.after:b.settings),"D06");}
C(s.gain_queries==1&&s.gain_returned&&r.settings.gain_calls==1&&GameSourceSoundGainValid(s.gain.source)&&s.gain_value==105,"D07");
C(SoundEq(owner,s.gain.source.before)&&SoundEq(s.gain.source.after,r.settings.sound.Snapshot())&&s.gain.source.after.queries==owner.queries+1,"D08");
C(s.gain.source.id==-1&&s.gain.source.negative_guard&&!s.gain.source.dispatched&&!s.gain.metadata_ready&&!s.gain.source_dispatched,"D09");
C(s.source_ui_sound_intents==b.source_ui_sound_intents+1&&s.after_key_updates==b.after_key_updates+1&&!s.row_feedback_attempts,"D10");
FrozenMessage(s.row_message_before,s.message);Ok(r.Draw(g),"H045");const auto drawn=r.front.Snapshot();
C(drawn.bar_frames==Words{{3,9,4}}&&drawn.manual_bars==b.manual_bars+3&&drawn.draw_frames==s.row_draw_frames_before+1,"D11");
FrozenMessage(s.message,drawn.message);const auto writes=r.settings.state.writes;const U queries=r.settings.gain_calls;
for(U n=0;n<3;++n){C(r.Send(0),"H046");Ok(r.Tick(),"H047");}
C(r.settings.state.writes==writes&&r.settings.gain_calls==queries,"D12");FrozenMessage(drawn.message,r.front.Snapshot().message);NoTerminal(r.front.Snapshot());
}
inline void LeftDirectFailure(){Rig r;r.DownNow();GameGraphics g;C(g.Initialize({}),"H048");Ok(r.Draw(g),"H049");r.Neutral();
const auto b=r.front.Snapshot();const auto owner=r.settings.sound.Snapshot();C(r.Send(Left),"H050");const auto status=r.Tick();const auto s=r.front.Snapshot();
C(!status&&status.code==GameFsCode::Unavailable&&status.detail=="Unsupported source SE5 bank handle read4AC84: registered handle producer unknown","L01");
C(s.failed&&s.source_consumer==0x4AC84&&s.row_writes==4&&s.row_left_steps==1&&s.selection56==1&&s.previous50==UINT32_MAX,"L02");
C(s.current_words==Words{{2,2,3}}&&s.settings.words==s.current_words&&r.settings.state.words==s.current_words&&s.passive_settings==Words{{2,3,3}}&&s.settings.writes==4,"L03");
const auto& q=s.row_receipts[3];C(GameOptionRowReceiptValid(q)&&q.cause==Cause::Left&&q.write.call_pc==0x351CC&&q.write.setter==0x24B34&&q.write.field==1&&q.write.value==2,"L04");
C(RootEq(q.write.before,b.settings)&&RootEq(q.write.after,r.settings.state)&&q.local_before==b.current_words&&q.local_after==s.current_words,"L05");
C(r.settings.events==std::vector<U>({0x34CCC,0x34CD4,0x34CDC,0x4AFBC,0x351CC,0x4AC38}),"L06");
C(s.row_feedback_attempts==1&&r.settings.feedback_calls==1&&s.row_feedback.attempted&&s.row_feedback.bank_attached&&!s.row_feedback.bank_handle_known&&!s.row_feedback.read_returned&&s.row_feedback.context_index==5&&s.row_feedback.read_pc==0x4AC84,"L07");
C(r.settings.sound.Record(5).bank_handle==-1&&!r.settings.sound.Record(5).bank_handle_known&&r.settings.sound.Record(5).bank_index==5,"L08");
C(SoundEq(owner,s.row_feedback.owner)&&SoundEq(owner,r.settings.sound.Snapshot())&&s.gain_queries==1&&r.settings.gain_calls==1,"L09");
C(s.after_key_updates==s.row_left_after_key_before&&s.draw_frames==s.row_left_draw_frames_before&&s.source_ui_sound_intents==b.source_ui_sound_intents&&s.bar_frames==Words{{3,9,4}},"L10");
FrozenMessage(s.row_message_before,s.message);NoTerminal(s);NoReplay(r,status);
}
inline RowReceipt Shape(Cause cause,Words words={{2,3,3}},U stage=0){
RowReceipt r;r.cause=cause;r.selection_before=cause==Cause::Down?0:1;r.local_before=r.local_after=words;
if(cause==Cause::Left)--r.local_after[1];auto& w=r.write;w.gui_generation=1;w.input_sequence=11;w.input_epoch=41;w.stage=stage;
w.field=cause==Cause::Down?stage:1;w.call_pc=cause==Cause::Down?0x34CCC+8*stage:0x351CC;w.setter=0x24B1C+0x18*w.field;w.value=r.local_after[w.field];
w.before={true,101,7,9,words};w.after=w.before;w.after.words[w.field]=w.value;++w.after.writes;return r;
}
inline void Full32Receipts(){
for(auto cause:{Cause::Down,Cause::Left}){const auto valid=Shape(cause,{{0x102,0x10003,0x80000003}});C(GameOptionRowReceiptValid(valid),"R01");
C(valid.write.value>255&&valid.write.before.words[2]==0x80000003,"R02");
for(U n=0;n<32;++n){auto r=valid;auto& w=r.write;switch(n){case 0:r.cause=Cause(0);break;case 1:r.row_count++;break;case 2:r.level_count++;break;
    case 3:r.selection_before^=1;break;case 4:r.selection_after++;break;case 5:r.local_before[0]^=0x100;break;case 6:r.local_after[2]^=0x100;break;
    case 7:w.gui_generation=0;break;case 8:w.input_sequence=0;break;case 9:w.input_epoch=0;break;case 10:w.stage=3;break;case 11:w.field=3;break;
    case 12:w.call_pc+=4;break;case 13:w.setter+=4;break;case 14:w.value^=0x100;break;case 15:w.before.live=false;break;case 16:w.before.owner=0;break;
    case 17:w.before.generation=0;break;case 18:w.before.writes=UINT64_MAX;break;case 19:w.after.writes++;break;case 20:w.after.owner++;break;
    case 21:w.after.generation++;break;case 22:w.after.live=false;break;case 23:w.after.words[0]^=0x100;break;case 24:w.before.words[2]^=0x100;break;
    case 25:r.local_after[1]++;break;case 26:w.after.words[1]^=0x100;break;case 27:r.selection_after=UINT32_MAX;break;
    case 28:r.local_before[1]=0;break;case 29:r.local_before[1]=0x80000000;break;case 30:r.local_before[1]=UINT32_MAX;break;case 31:w.after.writes=w.before.writes;break;}
    C(!GameOptionRowReceiptValid(r)&&!GameOptionRowReceiptExact(valid,r),"R03");}
C(!GameOptionRightReceiptValid(valid.write)&&!GameOptionBackupReceiptValid(valid.write)&&!GameOptionYesReceiptValid(valid.write),"R04");
}
for(U value:{0u,0x80000000u,UINT32_MAX})C(!GameOptionRowReceiptValid(Shape(Cause::Left,{{2,value,3}})),"R05");
for(U value:{1u,0x10003u,0x7FFFFFFFu}){auto r=Shape(Cause::Left,{{2,value,3}});C(GameOptionRowReceiptValid(r)&&r.write.value==value-1,"R06");}
auto old=Shape(Cause::Left);old.write.value=3;old.write.after.words[1]=3;C(GameOptionRightReceiptValid(old.write)&&!GameOptionRowReceiptValid(old),"R07");
for(U stage=0;stage<3;++stage){auto w=Shape(Cause::Down,{{2,3,3}},stage).write;
w.call_pc=0x3526C+8*stage;C(GameOptionBackupReceiptValid(w)&&!GameOptionYesReceiptValid(w),"R08");
w.call_pc=0x3534C+8*stage;C(GameOptionYesReceiptValid(w)&&!GameOptionBackupReceiptValid(w),"R09");
auto row=Shape(Cause::Down,{{2,3,3}},stage);row.write=w;C(!GameOptionRowReceiptValid(row),"R10");}
}
inline void PartialFailures(){
for(U at=0;at<4;++at){Rig r;r.settings.fail_before=at;r.settings.injected.detail=at?"H051":"";
if(at==3)r.DownNow();r.Neutral();C(r.Send(at==3?Left:Down),"H052");const auto status=r.Tick();const auto s=r.front.Snapshot();
C(!status&&Eq(status,r.settings.injected)&&s.failed&&s.row_writes==at&&r.settings.state.writes==at,"P01");
C(!s.row_feedback_attempts&&!r.settings.feedback_calls&&r.settings.gain_calls==(at==3?1u:0u),"P02");
C(s.current_words==(at==3?Words{{2,2,3}}:Words{{2,3,3}})&&r.settings.state.words==Words{{2,3,3}},"P03");
if(at==3)C(s.after_key_updates==s.row_left_after_key_before&&s.previous50==UINT32_MAX,"P04");NoReplay(r,status);
}
for(U at=0;at<4;++at)for(U damage=0;damage<8;++damage){Rig r;r.settings.damage_at=at;r.settings.damage=damage;if(at==3)r.DownNow();r.Neutral();C(r.Send(at==3?Left:Down),"H053");
const auto status=r.Tick();const auto s=r.front.Snapshot();C(!status&&s.failed&&r.settings.state.writes==at+1&&s.row_writes==at,"P05");
C(!s.row_feedback_attempts&&r.settings.gain_calls==(at==3?1u:0u),"P06");NoReplay(r,status);}
for(U mode=0;mode<2;++mode){Rig r;r.settings.fail_gain=!mode;r.settings.damage_gain=mode!=0;r.Neutral();C(r.Send(Down),"H054");const auto status=r.Tick();
C(!status&&r.front.Snapshot().row_writes==3&&r.settings.state.writes==3&&!r.front.Snapshot().gain_returned,"P07");
C(r.settings.sound.Snapshot().queries==mode&&!r.settings.feedback_calls,"P08");NoReplay(r,status);}
{Rig r(3,UINT64_MAX);r.Neutral();C(r.Send(Down),"H055");const auto status=r.Tick();C(!status&&r.settings.state.writes==UINT64_MAX&&r.settings.rows.empty(),"P09");NoReplay(r,status);}
}
inline void InputAndBounds(){
{Rig r;r.Neutral();C(r.Send(Down|Up|Right|Left|Circle|Cross),"H056");Ok(r.Tick(),"H057");C(r.front.Snapshot().row_writes==3&&!r.settings.feedback_calls,"I01");NoTerminal(r.front.Snapshot());}
for(U buttons:std::array<U,5>{{Up|Left,Right|Left,Circle,Cross,Down}}){Rig r;r.DownNow();r.Neutral();const auto before=r.settings.state;C(r.Send(buttons),"H058");const auto status=r.Tick();
C(!status&&status.code==GameFsCode::Unavailable&&r.front.Snapshot().failed&&status.detail.find("Unsupported")!=std::string::npos,"I02");
C(RootEq(before,r.settings.state)&&!r.settings.feedback_calls,"I03");NoReplay(r,status);}
{Rig r;r.settings.row_writable=false;r.Neutral();C(r.Send(Down),"H059");const auto status=r.Tick();C(!status&&!r.front.Snapshot().row_selections&&r.settings.rows.empty(),"I04");NoReplay(r,status);}
for(U mode=0;mode<2;++mode){Rig r;r.Neutral();C(r.Send(Down),"H060");C(r.Send(mode?0:Down),"H061");const Q latest=r.seq;Ok(r.Tick(),"H062");
C(r.front.Snapshot().row_writes==3&&r.front.Snapshot().last_sequence==latest,"I05");for(const auto& row:r.settings.rows)C(row.write.input_sequence==latest,"I06");}
for(U mode=0;mode<3;++mode){Rig r;r.Neutral();C(r.Send(Down),"H063");if(mode<2){auto f=r.Frame(Down);f.focused=mode!=0;f.overflow_resync=mode==1;C(!r.front.Input(f),"H064");}
else C(r.front.SetInputEpoch(r.front.Snapshot().input_epoch+1),"H065");
C(r.Send(Down),"H066");Ok(r.Tick(),"H067");C(!r.front.Snapshot().row_selections&&r.settings.rows.empty(),"I07");
r.DownNow();C(r.front.Snapshot().row_writes==3,"I08");}
{Rig r;r.Neutral();auto f=r.Frame(Down);--f.main_menu_epoch;C(!r.front.Input(f),"H068");Ok(r.Tick(),"H069");C(r.settings.rows.empty(),"I09");
f=r.Frame(Down);C(r.front.Input(f)&&!r.front.Input(f),"H070");Ok(r.Tick(),"H071");C(r.settings.rows.size()==3,"I10");}
for(U value:{0u,0x80000000u,UINT32_MAX}){Rig r(value);r.DownNow();r.Neutral();const auto b=r.front.Snapshot();C(r.Send(Left),"H072");Ok(r.Tick(),"H073");const auto s=r.front.Snapshot();
C(s.current_words==b.current_words&&RootEq(s.settings,b.settings)&&s.previous50==1&&!s.row_left_steps&&!s.row_feedback_attempts&&r.settings.gain_calls==1,"I11");FrozenMessage(s.row_message_before,s.message);}
{Rig r(1);r.DownNow();r.Neutral();C(r.Send(Left),"H074");const auto status=r.Tick();C(!status&&r.settings.state.words[1]==0&&r.settings.state.writes==4&&r.front.Snapshot().previous50==UINT32_MAX,"I12");NoReplay(r,status);}
}
} // namespace dokodemo::options_row1_tests
inline void GameOptionsRow1RegressionChecks(){
using namespace dokodemo::options_row1_tests;
CausalDown();LeftDirectFailure();Full32Receipts();PartialFailures();InputAndBounds();
}
