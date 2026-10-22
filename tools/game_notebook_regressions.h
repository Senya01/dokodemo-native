#pragma once
// Static authoring; ROOT owns integration/execution.
#include "../src/recovered/game_notebook_native.h"
#include "../src/recovered/game_main_menu_native.h"
#include "../src/recovered/game_content_native.h"
#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace dokodemo::notebook_tests {
using namespace recovered;
using U=uint32_t;using H=uint16_t;using Q=uint64_t;
using V=script::Value;using T=script::Table;using S=GameNotebookSnapshot;
using FC=GameFsCode;using R=GameNotebookRows;using L=GameNotebookLease;using F=GameInputFrame;using ML=GameMainMenuLease;
inline void Ck(bool ok,unsigned id){if(!ok)throw std::runtime_error("notebook component N"+std::to_string(id));}
inline void Put(std::vector<uint8_t>& b,size_t at,U word,unsigned width=4){
for(unsigned i=0;i<width;++i)b.at(at+i)=uint8_t(word>>(8*i));
}
struct Storage final:PackageStorageLease {
std::vector<uint8_t> bytes,known;
explicit Storage(std::vector<uint8_t> b):bytes(std::move(b)),known(bytes.size(),1){}
bool Span(PackageStorageSpan& s,std::string&)const override{
s.bytes=const_cast<uint8_t*>(bytes.data());s.initialized=const_cast<uint8_t*>(known.data());
s.size=bytes.size();s.host_address=Q(reinterpret_cast<uintptr_t>(bytes.data()));
s.owner=s.host_address;s.generation=s.serial=1;s.readable=true;s.writable=false;return true;
}
};
inline PackageView View(std::vector<uint8_t> b){const auto n=b.size();return PackageStorage::Attach(std::make_shared<Storage>(std::move(b)),n);}
inline PackageView Gim(){
std::vector<uint8_t> b(132);const char magic[]="MIG.00.1PSP";
for(size_t i=0;i<sizeof(magic);++i)b[i]=uint8_t(magic[i]);
for(U at:{16u,32u,48u}){Put(b,at,at==16?2:at==32?3:4,2);Put(b,at+2,16,2);
Put(b,at+4,132-at);Put(b,at+8,at==48?84:16);Put(b,at+12,16);}
Put(b,64,48,2);Put(b,68,3,2);Put(b,72,1,2);Put(b,74,1,2);Put(b,76,32,2);Put(b,78,4,2);Put(b,80,1,2);
Put(b,88,48);Put(b,92,64);Put(b,96,68);Put(b,104,1,2);Put(b,106,1,2);Put(b,108,3,2);Put(b,110,1,2);
Put(b,112,64);Put(b,128,0xFF4080E0);return View(std::move(b));
}
inline PackageView MenuImage(){
constexpr U nodes=40,clips=216,keys=1776,values=1792,name=1804;
std::vector<uint8_t> b(1824);Put(b,0,0x30413242);Put(b,4,1);Put(b,8,32);Put(b,12,22);
Put(b,16,nodes);Put(b,20,30);Put(b,24,clips);Put(b,28,22);Put(b,36,name);
const char stem[]="test-menu.GIM";for(size_t i=0;i<sizeof(stem);++i)b.at(name+i)=uint8_t(stem[i]);
for(U i=0;i<30;++i)Put(b,clips+i*52+48,i==0?3:i==1?4:12);
for(U i=0;i<2;++i){Put(b,clips+i*52,1);Put(b,clips+i*52+4,keys+i*8);Put(b,keys+i*8+2,1,2);Put(b,keys+i*8+4,values);}
Put(b,values,1);return View(std::move(b));
}
inline PackageView NoteImage(U child){
constexpr U nodes=40,clips=1792,keys=2312,values=2392,name=2512;
std::vector<uint8_t> b(2544);Put(b,0,0x30413242);Put(b,4,1);Put(b,8,32);Put(b,12,219);
Put(b,16,nodes);Put(b,20,10);Put(b,24,clips);Put(b,28,10);Put(b,36,name);Put(b,nodes+2,76,2);Put(b,nodes+76*8,child,2);
const char stem[]="test-note.GIM";for(size_t i=0;i<sizeof(stem);++i)b.at(name+i)=uint8_t(stem[i]);
for(U i=0;i<10;++i){const auto c=clips+i*52,k=keys+i*8,v=values+i*12;
Put(b,c,1);Put(b,c+4,k);Put(b,c+48,i==0?3:12);Put(b,k,i,2);Put(b,k+2,1,2);Put(b,k+4,v);Put(b,v,1);}
return View(std::move(b));
}
struct Resources final:GameResourceSource {
U child=92,note_opens=0,menu_opens=0,texture_opens=0;bool missing_note=false;
explicit Resources(U n=92):child(n){}
GameFsStatus OpenResource(const std::string& p,PackageView& out)override{
if(p=="mtomo\\gui\\tomodachi_note\\NOTE.CB2A"){++note_opens;if(missing_note)return {FC::NotFound,0,"synthetic NOTE absent"};out=NoteImage(child);return {};}
if(p=="data\\gui\\main_menu\\MMENU.CB2A"){++menu_opens;out=MenuImage();return {};}
if(p=="mtomo\\gui\\tomodachi_note\\test-note.GIM"||p=="data\\gui\\main_menu\\test-menu.GIM"){++texture_opens;out=Gim();return {};}
return {FC::NotFound,0,"unexpected synthetic path"};
}
};
inline PackageView FontImage(){
std::vector<uint8_t> b(268);Put(b,0,0x31544658);Put(b,4,1,2);Put(b,6,1,2);Put(b,8,96);b[12]=1;
Put(b,16,60);Put(b,20,96,2);Put(b,22,96,2);Put(b,24,1,2);Put(b,28,4);Put(b,32,96,2);Put(b,36,1,2);
Put(b,40,48);Put(b,44,156);Put(b,48,3);Put(b,52,16);Put(b,56,204);
std::fill(b.begin()+60,b.begin()+156,uint8_t(1));std::fill(b.begin()+156,b.begin()+204,uint8_t(0x11));Put(b,208,0xFFFFFFFF);
return View(std::move(b));
}
inline script::Program Program(){
const std::vector<uint8_t> code={0x30,0,0x38};
std::vector<uint8_t> b(28+code.size()+4u*(2048+80));Put(b,0,0x0043534B);Put(b,4,0x20030204);Put(b,12,U(code.size()));
Put(b,22,80,2);Put(b,24,2048,2);std::copy(code.begin(),code.end(),b.begin()+28);
script::Program p;script::Diagnostic d;Ck(p.load_test_fixture(std::move(b),d),99);return p;
}
inline V Ref(T table,H index){V v;v.kind=V::Kind::Reference;v.table=table;v.index=index;return v;}
inline V Header(U n){V v;v.kind=V::Kind::ArrayHeader;v.word=0x90000000u|n;return v;}
inline V Nil(){V v;v.kind=V::Kind::Scalar;return v;}
inline V Text(std::string s){V v;v.kind=V::Kind::String;v.word=0x50000000;v.cp932=std::move(s);return v;}
inline bool SameValue(const script::ValueWitness& a,const script::ValueWitness& b){
return a.kind==b.kind&&a.word==b.word&&a.table==b.table&&a.index==b.index&&a.cp932_bytes==b.cp932_bytes&&a.cp932_sha256==b.cp932_sha256;
}
inline bool SameVm(const script::VmStateWitness& a,const script::VmStateWitness& b){
if(a.valid!=b.valid||a.context_registered!=b.context_registered||a.event_envelope_present!=b.event_envelope_present||a.stopped!=b.stopped
||a.run_id!=b.run_id||a.frame_id!=b.frame_id||a.executed!=b.executed||a.clock_tick!=b.clock_tick||a.deadline_tick!=b.deadline_tick
||a.relative_deadline23!=b.relative_deadline23||a.current_pc!=b.current_pc||a.code_entry!=b.code_entry||a.context_word!=b.context_word
||a.call_depth!=b.call_depth||a.active_blocks!=b.active_blocks||a.event_scope_depth!=b.event_scope_depth||a.queued_callbacks!=b.queued_callbacks
||a.caller_entry!=b.caller_entry||a.caller_call_pc!=b.caller_call_pc||a.caller_return_pc!=b.caller_return_pc||a.caller_frame_id!=b.caller_frame_id
||a.caller_stack_base!=b.caller_stack_base||a.frame_cells!=b.frame_cells||a.operand_cells!=b.operand_cells||a.frame_captured!=b.frame_captured
||a.operands_captured!=b.operands_captured||a.code_sha256!=b.code_sha256||!SameValue(a.result,b.result))return false;
for(size_t i=0;i<a.frame_values.size();++i)if(!SameValue(a.frame_values[i],b.frame_values[i])||!SameValue(a.operands[i],b.operands[i]))return false;
return true;
}
inline bool SameRows(const R& a,const R& b){
return a.valid==b.valid&&a.script_generation==b.script_generation&&a.run_id==b.run_id&&a.root_table==b.root_table&&a.root_index==b.root_index
&&a.root_header==b.root_header&&a.records==b.records&&a.populated==b.populated&&a.cells==b.cells&&a.code_sha==b.code_sha&&a.sha==b.sha;
}
inline bool SameLease(const L& a,const L& b){
return a.script_generation==b.script_generation&&a.run_id==b.run_id&&a.context==b.context&&a.parent_generation==b.parent_generation
&&a.available_owner==b.available_owner&&a.available_generation==b.available_generation&&a.input_epoch==b.input_epoch;
}
inline bool Same(const S& a,const S& b){
return a.live==b.live&&a.visible==b.visible&&a.request9==b.request9&&a.owner46C==b.owner46C&&a.retirement_pending==b.retirement_pending
&&a.generation==b.generation&&a.retired_generation==b.retired_generation&&a.input_epoch==b.input_epoch&&a.last_sequence==b.last_sequence
&&SameLease(a.lease,b.lease)&&SameRows(a.rows,b.rows)&&a.phase==b.phase&&a.state23C==b.state23C&&a.state240==b.state240&&a.mode244==b.mode244
&&a.count248==b.count248&&a.count24C==b.count24C&&a.category264==b.category264&&a.category268==b.category268&&a.ordinary270==b.ordinary270
&&a.selection250==b.selection250&&a.loader498==b.loader498&&a.loader469==b.loader469&&a.requests==b.requests&&a.opens==b.opens
&&a.close_requests==b.close_requests&&a.close_completions==b.close_completions&&a.polls==b.polls&&a.retirement_requests==b.retirement_requests
&&a.retirements==b.retirements&&a.rejected==b.rejected&&a.unsupported==b.unsupported&&a.frame==b.frame&&a.clip==b.clip&&a.mode==b.mode
&&a.draw_frames==b.draw_frames&&a.quads==b.quads&&a.font_resets==b.font_resets&&a.font_flushes==b.font_flushes&&a.callback_reaches==b.callback_reaches
&&a.callback_empty_returns==b.callback_empty_returns&&a.text_owner==b.text_owner&&a.text_capacity==b.text_capacity&&a.text_used==b.text_used&&a.text_setup_stage==b.text_setup_stage
&&a.text_scale_x==b.text_scale_x&&a.text_scale_y==b.text_scale_y&&a.text_flush_selector==b.text_flush_selector&&a.scissor_top==b.scissor_top&&a.scissor_bottom==b.scissor_bottom&&a.error==b.error;
}
inline std::string Hex(const std::array<uint8_t,32>& a){const char* d="0123456789abcdef";std::string s;for(auto n:a){s+=d[n>>4];s+=d[n&15];}return s;}
inline R Rows(){
const auto p=Program();script::Vm vm(p);Ck(vm.start().status==script::Status::Running,1);
const auto root=Ref(T::Global,4),one=Ref(T::Local,0),two=Ref(T::Global,32);
vm.store_reference(root,Header(3));vm.store_reference(Ref(T::Global,5),Text("slot-zero"));
vm.store_reference(Ref(T::Global,6),one);vm.store_reference(Ref(T::Global,7),two);
vm.store_reference(one,Header(3));vm.store_reference(Ref(T::Local,1),Nil());vm.store_reference(Ref(T::Local,2),Text("A"));
V date;date.kind=V::Kind::Timestamp;date.word=0x30001234;vm.store_reference(Ref(T::Local,3),date);
vm.store_reference(two,Header(2));vm.store_reference(Ref(T::Global,33),Nil());vm.store_reference(Ref(T::Global,34),Text("B"));
U accesses=0;vm.access_observer([&](const V&,bool){++accesses;});
const auto observe=[&]{const auto before=vm.current_witness();const auto hits=accesses;auto r=GameObserveNotebookRows(vm,71,root);
Ck(SameVm(before,vm.current_witness())&&hits==accesses,2);return r;};
const auto original=observe();Ck(SameRows(original,observe())&&accesses==0,3);
Ck(original.valid&&original.records==3&&original.populated==0&&original.cells==5&&original.root_table==U(T::Global)
&&original.root_index==4&&original.root_header==0x90000003&&original.script_generation==71&&original.run_id==vm.current_witness().run_id
&&original.code_sha==vm.current_witness().code_sha256&&original.code_sha[64]==0&&std::string(original.code_sha.data())==p.source().sha256,4);
Ck(Hex(original.sha)=="b615842c71a8d1492eddf46e5a01b89affaae0b936c51fa85bab18d526dffb60",5);
vm.store_reference(Ref(T::Global,5),Text("changed slot0"));auto changed=observe();
Ck(changed.sha!=original.sha&&changed.cells==5&&changed.populated==0,6);vm.store_reference(Ref(T::Global,5),Text("slot-zero"));
vm.store_reference(Ref(T::Global,34),Text("last field changed"));Ck(observe().sha!=original.sha,7);vm.store_reference(Ref(T::Global,34),Text("B"));
vm.store_reference(Ref(T::Local,1),V::integer(0));changed=observe();
Ck(changed.populated==1&&changed.sha!=original.sha,8);vm.store_reference(Ref(T::Local,1),Nil());
const auto refuse=[&](V r,Q generation=71){const auto before=vm.current_witness();const auto hits=accesses;bool threw=false;
try{(void)GameObserveNotebookRows(vm,generation,r);}catch(const std::exception&){threw=true;}
Ck(threw&&SameVm(before,vm.current_witness())&&hits==accesses,9);};
refuse(root,0);refuse(Nil());refuse(Ref(T::Frame,0));refuse(Ref(T::Global,65535));
vm.store_reference(root,V::integer(3));refuse(root);vm.store_reference(root,Header(1025));refuse(root);vm.store_reference(root,Header(3));
vm.store_reference(Ref(T::Global,6),Nil());refuse(root);vm.store_reference(Ref(T::Global,6),Ref(T::Frame,0));refuse(root);vm.store_reference(Ref(T::Global,6),one);
for(U n:{0u,65u}){vm.store_reference(one,Header(n));refuse(root);}vm.store_reference(one,Header(3));
V malformed=Nil();malformed.cp932="wrong-kind text";vm.store_reference(Ref(T::Local,2),malformed);refuse(root);vm.store_reference(Ref(T::Local,2),Text("A"));
for(auto kind:{V::Kind::Unknown,V::Kind::Timestamp,V::Kind::Opaque}){V bad;bad.kind=kind;vm.store_reference(Ref(T::Local,1),bad);refuse(root);}
V bad=Nil();bad.word=1;vm.store_reference(Ref(T::Local,1),bad);refuse(root);vm.store_reference(Ref(T::Local,1),Nil());
Ck(SameRows(original,observe()),10);
vm.store_reference(root,Header(0));const auto zero=observe();Ck(zero.records==0&&zero.cells==0&&zero.populated==0&&zero.sha!=original.sha,11);
vm.store_reference(root,Header(1024));vm.store_reference(one,Header(1));
for(H i=1;i<1024;++i)vm.store_reference(Ref(T::Global,H(5+i)),one);
const auto bound=observe();Ck(bound.records==1024&&bound.cells==1023&&bound.populated==0,12);
script::Vm cold(p);bool threw=false;try{(void)GameObserveNotebookRows(cold,71,root);}catch(const std::exception&){threw=true;}
Ck(threw,13);return original;
}
inline L Lease(const R& r){return {r.script_generation,r.run_id,13,31,19,41,27};}
inline F Frame(L l,Q seq,U keys=0){
F f;f.buttons=keys;f.focused=true;f.test_injected=true;f.sequence=seq;f.main_menu_epoch=l.input_epoch;
f.message_owner_generation=l.script_generation;f.message_context=l.context;f.returned_owner=l.available_owner;f.returned_generation=l.available_generation;return f;
}
struct Case {
Resources source;GameTransition fade;GameNotebook b;L lease;Q seq=0;
Case(const R& rows,U node=92,bool ready=true):source(node),lease(Lease(rows)){
Ck(fade.Initialize({{0,0}})&&bool(b.Request(rows,lease,0))&&bool(b.Tick(source,fade)),14);if(ready)Ready();
}
S Get()const{return b.Snapshot();}
void Tick(){Ck(bool(b.Tick(source,fade)),15);}
void Ready(){for(U i=0;b.Snapshot().phase==2&&i<96;++i){fade.Update();Tick();}Ck(b.Snapshot().phase==3&&fade.Snapshot().state==2,16);}
void Step(U keys=0){Ck(b.Input(Frame(lease,++seq,keys)),17);Tick();}
void Press(U keys){Step();Step(keys);}
void Poll(){for(U i=0;b.Snapshot().phase==4&&i<96;++i){fade.Update();Tick();}Ck(b.Snapshot().phase==6,18);}
};
inline void Admission(const R& rows){
GameNotebook b;const auto cold=b.Snapshot();auto lease=Lease(rows);Ck(!b.Modal()&&!b.ClosedAndRetired(),19);
const auto refuse=[&](R r,L l,U prior=0){Ck(b.Request(r,l,prior).code==FC::Unavailable&&Same(cold,b.Snapshot()),20);};
for(U prior:{1u,2u,0xFFFFFFFFu})refuse(rows,lease,prior);
auto r=rows;r.valid=false;refuse(r,lease);r=rows;++r.script_generation;refuse(r,lease);r=rows;++r.run_id;refuse(r,lease);r=rows;r.populated=1;refuse(r,lease);
for(auto field:{&L::script_generation,&L::run_id,&L::context,&L::parent_generation,&L::available_owner,&L::available_generation,&L::input_epoch}){
auto bad=lease;bad.*field=0;refuse(rows,bad);}
Ck(bool(b.Request(rows,lease,0)),21);const auto requested=b.Snapshot();
Ck(requested.phase==1&&requested.live&&requested.request9&&!requested.owner46C&&requested.generation==1&&requested.requests==1&&requested.count248==3&&requested.count24C==0&&SameRows(requested.rows,rows),22);
Ck(!b.Request(rows,lease,0)&&Same(requested,b.Snapshot()),23);
GameTransition absent;Resources source;Ck(!b.Tick(source,absent)&&source.note_opens==0&&b.Snapshot().phase==1&&!b.Snapshot().owner46C,24);
GameTransition fade;Ck(fade.Initialize({{0,0}}),25);source.missing_note=true;
Ck(b.Tick(source,fade).code==FC::NotFound&&b.Snapshot().opens==0&&!b.Snapshot().owner46C,26);
source.missing_note=false;Ck(bool(b.Tick(source,fade))&&b.Snapshot().generation==1&&b.Snapshot().opens==1,27);b.Close();
}
inline void Fixture(){
Resources source;auto image=std::make_shared<GameUiResource>();Ck(bool(image->Load(source,"mtomo\\gui\\tomodachi_note\\NOTE.CB2A","mtomo\\gui\\tomodachi_note\\")),28);
Ck(image->RootCount()==10&&image->InitialNodes().size()==219&&image->Clips().size()==10&&image->Textures().size()==1&&source.note_opens==1&&source.texture_opens==1,29);
GameUiInstance clone;Ck(clone.Clone(image),30);
for(U i=0;i<10;++i)Ck(image->Clips()[i].channels[0].count==1&&clone.Bind(i,i,0),31);
constexpr U clips[]={0,1,5,3,4,6,8};for(U i=0;i<7;++i){auto* c=clone.AddControl(clips[i]);Ck(c&&c->clip==clips[i],32);}
Ck(clone.Control(7)==nullptr&&clone.ShowRoot(0)&&clone.VisibleRoots()==1,33);GameUiMatrix world;
Ck(clone.WorldMatrix(76,world)&&world.ty==0&&!clone.WorldMatrix(219,world),97);
}
inline void Lifecycle(const R& rows){
Case c(rows,92,false);auto s=c.Get();Ck(s.phase==2&&s.state23C==0&&s.live&&s.visible&&s.owner46C&&s.request9&&s.loader469==1&&s.loader498==0&&s.mode244==0&&s.ordinary270==3&&s.clip==0&&s.frame==0&&s.mode==1&&s.text_owner&&s.text_capacity==1024&&s.text_used==0&&c.fade.Snapshot().state==1,34);
Ck(!c.b.SetInputEpoch(28)&&!c.b.Input(Frame(c.lease,1,Cross)),35);
for(U i=0;i<4;++i)c.Tick();Ck(c.Get().phase==2&&c.Get().mode==2&&c.fade.Snapshot().state==1,36);
for(U i=0;c.fade.Snapshot().state!=2&&i<96;++i)c.fade.Update();
Ck(c.Get().phase==2&&!c.b.Input(Frame(c.lease,2,Cross)),37);c.Tick();
Ck(c.Get().phase==3&&c.Get().close_requests==0,38);
c.Press(Cross);s=c.Get();Ck(s.phase==4&&s.state23C==2&&s.loader469==1&&s.visible&&s.owner46C&&s.request9&&s.close_requests==1&&s.polls==0&&c.fade.Snapshot().state==3,39);
for(U i=0;i<4;++i)c.Tick();Ck(c.Get().phase==4,40);c.Poll();s=c.Get();
Ck(s.state23C==3&&!s.visible&&s.live&&s.owner46C&&s.retirement_pending&&!s.request9&&s.loader469==0&&s.polls==1&&s.close_completions==1&&s.retirement_requests==1&&s.retirements==0&&s.retired_generation==0&&!c.b.ClosedAndRetired(),41);
const auto pending=s;Ck(!c.b.Request(rows,c.lease,0)&&Same(pending,c.Get()),42);c.Tick();s=c.Get();
Ck(s.phase==7&&!s.live&&!s.owner46C&&!s.retirement_pending&&!s.text_owner&&s.text_capacity==0&&s.text_used==0&&s.retired_generation==s.generation&&s.retirements==1&&c.b.ClosedAndRetired()&&!c.b.Modal()&&SameRows(s.rows,rows),43);
const auto done=s;c.Tick();Ck(Same(done,c.Get()),44);
Ck(bool(c.b.Request(rows,c.lease,0)),45);s=c.Get();
Ck(s.generation==2&&s.requests==2&&s.retirements==1&&s.polls==1&&s.retired_generation==0&&!c.b.ClosedAndRetired(),46);c.b.Close();
}
inline void Inputs(const R& rows){
Case c(rows);
for(auto field:{&F::message_owner_generation,&F::message_context,&F::returned_owner,&F::returned_generation,&F::main_menu_epoch}){
auto f=Frame(c.lease,1000,Cross);++(f.*field);Ck(!c.b.Input(f)&&c.Get().last_sequence==0,47);}
Ck(!c.b.Input(Frame(c.lease,0,Cross)),48);c.Step(Cross);Ck(c.Get().phase==3,49);
Ck(!c.b.Input(Frame(c.lease,c.seq,Cross)),50);c.Step();
F unfocused{};unfocused.focused=false;Ck(!c.b.Input(unfocused),51);
c.Step(Cross);Ck(c.Get().phase==3,52);
c.Step();Ck(c.b.Input(Frame(c.lease,++c.seq,Cross)),53);
F lost{};lost.overflow_resync=true;Ck(!c.b.Input(lost),54);c.Tick();c.Step(Cross);
Ck(c.Get().phase==3,55);c.Step();c.b.DisarmInput();c.Step(Cross);
Ck(c.Get().phase==3,56);c.Step();
const auto old=c.lease;Ck(!c.b.SetInputEpoch(old.input_epoch)&&!c.b.SetInputEpoch(old.input_epoch-1)&&c.b.SetInputEpoch(old.input_epoch+1),57);++c.lease.input_epoch;
Ck(!c.b.Input(Frame(old,1000))&&c.Get().lease.input_epoch==old.input_epoch,58);c.Step(Cross);
Ck(c.Get().phase==3,59);c.Press(Cross);Ck(c.Get().phase==4,60);c.b.Close();
}
inline void Categories(const R& rows){
Case c(rows);c.Step();Ck(c.Get().selection250==-1,61);
for(U key:{Down,Up,Circle}){c.Press(key);const auto s=c.Get();Ck(s.phase==3&&s.unsupported==0&&s.category264==1&&s.category268==1&&s.selection250==-1,62);}
for(U expected:{2u,3u,4u,1u}){c.Press(Right);Ck(c.Get().category264==expected&&c.Get().category268==expected&&c.Get().count24C==0,63);}
c.Press(Left);Ck(c.Get().category264==4,64);
for(U i=0;i<40;++i)c.Step(Left);Ck(c.Get().category264==1,65);
c.Press(Right);for(U i=0;i<40;++i)c.Step(Right);Ck(c.Get().category264==4,66);
c.Press(Right);Ck(c.Get().category264==1&&c.Get().unsupported==0&&c.Get().selection250==-1,67);c.b.Close();
}
inline void Priority(const R& rows){
Case c(rows);for(U key:{Down,Up,Right,Left,Circle,Triangle}){const auto n=c.Get().unsupported;c.Press(key|Cross);const auto s=c.Get();
Ck(s.phase==3&&s.close_requests==0&&s.unsupported==n+(key==Triangle?1u:0u),68);}
c.Press(Down|Right|Cross);Ck(c.Get().category264==1&&c.Get().phase==3,69);
c.Press(Circle|Triangle|Cross);Ck(c.Get().unsupported==1&&c.Get().phase==3,70);
c.Press(Cross);Ck(c.Get().phase==4&&c.Get().close_requests==1,71);c.b.Close();
}
inline void ForceClose(const R& rows){
for(bool polled:{false,true}){Case c(rows);if(polled){c.Press(Cross);c.Poll();}const auto before=c.Get();c.b.Close();const auto s=c.Get();
Ck(s.phase==7&&!s.live&&!s.visible&&!s.request9&&!s.owner46C&&!s.retirement_pending&&s.loader469==before.loader469&&s.polls==before.polls
&&s.retirements==before.retirements&&s.retired_generation==0&&!c.b.ClosedAndRetired(),72);
c.Tick();Ck(!c.b.ClosedAndRetired()&&c.Get().retirements==0,73);}
Case retired(rows);retired.Press(Cross);retired.Poll();retired.fade.Retire();const auto pending=retired.Get();
Ck(!retired.b.Tick(retired.source,retired.fade)&&retired.Get().retirements==pending.retirements&&retired.Get().retired_generation==0,74);retired.b.Close();
}
inline void Draw(const R& rows){
GameGraphics graphics;GameFontResource font;Ck(graphics.Initialize({})&&bool(font.Initialize(FontImage()))&&font.Snapshot().ready,75);
constexpr U nodes[]={92,94,95,96,211,212,213,214,215,216,217,218};
constexpr U pcs[]={0x2BA60,0x2BAE4,0x2BB8C,0x2BC34,0x2BED8,0x2BFDC,0x2C0DC,0x2C1DC,0x2C340,0x2C440,0x2C544,0x2C648};
for(U i=0;i<12;++i){Case c(rows,nodes[i],false);Ck(graphics.SetScissor(1,2,3,4),100);const auto d=c.b.Draw(graphics,font);const auto s=c.Get();
if(i<4){const auto cfg=graphics.Snapshot().config;Ck(bool(d)&&s.callback_reaches==1&&s.callback_empty_returns==1&&s.draw_frames==1&&s.quads==1&&s.font_resets==1&&s.font_flushes==1
&&s.text_owner&&s.text_capacity==1024&&s.text_used==0&&s.text_setup_stage==5&&s.text_scale_x==1&&s.text_scale_y==1&&s.text_flush_selector==-1&&s.scissor_top==136&&s.scissor_bottom==296
&&cfg.scissor_left==0&&cfg.scissor_top==0&&cfg.scissor_right==480&&cfg.scissor_bottom==272,98);}
else Ck(d.code==FC::Unavailable&&d.detail=="NOTE unsupported source callback emission/context at "+std::to_string(pcs[i])&&s.callback_reaches==1&&s.callback_empty_returns==0&&s.font_resets==0&&s.font_flushes==0&&s.draw_frames==0&&s.text_setup_stage==0,76);c.b.Close();}
Case c(rows);GameFontResource absent;const auto before=c.Get();Ck(!c.b.Draw(graphics,absent)&&c.Get().callback_reaches==before.callback_reaches&&c.Get().font_resets==before.font_resets,77);
c.Press(Cross);c.Poll();const auto hidden=c.Get();Ck(bool(c.b.Draw(graphics,absent))&&Same(hidden,c.Get()),78);c.Tick();
font.Close();Ck(!font.Snapshot().ready,79);
}
inline F MenuFrame(ML l,Q seq,U key){return Frame({l.script_generation,l.run_id,l.message_context,1,l.available_owner,l.available_generation,l.input_epoch},seq,key);}
inline void Menu(){
for(bool enabled:{false,true})for(bool profiles:{false,true}){
Resources source;GameMainMenu menu;const auto ms=[&]{return menu.Snapshot();};ML lease{71,17,19,41,13,27};Q seq=0;
Ck(enabled?bool(menu.Open(source,lease,true,false,true,profiles)):bool(menu.Open(source,lease,true,false)),80);
const auto steady=[&]{for(U i=0;ms().mode==1&&i<24;++i)Ck(bool(menu.Tick()),81);Ck(ms().mode==2&&ms().state78==1,82);};
const auto step=[&](U key){Ck(menu.Input(MenuFrame(lease,++seq,key)),83);Ck(bool(menu.Tick()),84);};
const auto press=[&](U key){step(0);step(key);};
steady();press(Down);press(Down);Ck(ms().row==3&&ms().column==0&&ms().notebook_enabled==enabled,85);
press(Circle);
if(!enabled){Ck(ms().state78==1&&ms().substate7C==0&&ms().unsupported==1&&ms().result==-1,86);
press(Right|Cross);Ck(ms().state78==1&&ms().cancels==0&&ms().unsupported==2,87);menu.Close();continue;}
auto s=ms();Ck(s.substate7C==1&&s.clip==(profiles?3u:2u)&&s.mode==1&&s.frame==0&&s.result==-1,88);steady();
press(Right);Ck(ms().state78==1&&ms().column==0,89);
const U max=profiles?3:2;
for(U col=1;col<=max;++col){press(Down);press(Circle);s=ms();Ck(s.column==int32_t(col)&&s.substate7C==1&&s.state78==1&&s.result==-1&&s.unsupported==col,90);}
press(Cross);s=ms();Ck(s.substate7C==0&&s.state78==1&&s.row==3&&s.cancels==0&&s.result==-1&&s.clip==0,91);steady();
press(Right);Ck(ms().substate7C==1&&ms().column==0,92);steady();press(Circle);s=ms();
Ck(s.state78==2&&s.clip==1&&s.mode==1&&s.result==-1&&s.closed==0,93);
for(U i=0;ms().state78==2&&i<24;++i)Ck(bool(menu.Tick()),94);s=ms();
Ck(s.state78==3&&s.result==3&&s.row==3&&s.column==0&&s.closed==1&&s.cancels==0&&s.render_live,95);
Ck(bool(menu.RetireClosedRender())&&!ms().render_live&&ms().result==3&&ms().render_retirements==1,96);
}
}
} // namespace dokodemo::notebook_tests
inline void GameNotebookRegressionChecks(){
using namespace dokodemo::notebook_tests;
const auto rows=Rows();Admission(rows);Fixture();Lifecycle(rows);Inputs(rows);Categories(rows);Priority(rows);ForceClose(rows);Draw(rows);Menu();
}
