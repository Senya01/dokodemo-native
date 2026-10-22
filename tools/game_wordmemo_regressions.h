#pragma once
// Author-only. ROOT executes. CORE_RELEASE.v3.
#include "../src/recovered/game_wordmemo_native.h"
#include "../src/core_hash.h"
#include "game_notebook_regressions.h"
#include <cstring>

namespace dokodemo::wordmemo_tests {
using namespace recovered;
namespace B=notebook_tests;
using Bytes=std::vector<uint8_t>;
using U=uint32_t;using Q=uint64_t;using V=script::Value;using T=script::Table;
using R=GameWordMemoRows;using L=GameWordMemoLease;using S=GameWordMemoSnapshot;using F=GameInputFrame;
inline void C(bool ok,const char* id){if(!ok)throw std::runtime_error(std::string("wordmemo ")+id);}
inline V Ref(U index){return B::Ref(T::Global,uint16_t(index));}
inline bool EqV(const V& a,const V& b){return a.kind==b.kind&&a.word==b.word&&a.table==b.table&&a.index==b.index&&a.cp932==b.cp932
&&!a.frame_owner.owner_before(b.frame_owner)&&!b.frame_owner.owner_before(a.frame_owner)
&&!a.context_owner.owner_before(b.context_owner)&&!b.context_owner.owner_before(a.context_owner);}
inline bool EqR(const R& a,const R& b){return a.valid==b.valid&&a.script_generation==b.script_generation&&a.run_id==b.run_id
&&a.root_table==b.root_table&&a.root_index==b.root_index&&a.root_header==b.root_header&&a.records==b.records&&a.cells==b.cells
&&a.eligible_records==b.eligible_records&&a.code_sha==b.code_sha&&a.sha==b.sha;}
inline bool EqS(const S& a,const S& b){
#define E(x) a.x==b.x
return E(live)&&E(visible)&&E(request8)&&E(owner470)&&E(retirement_pending)&&E(generation)
&&E(retired_generation)&&E(input_epoch)&&E(last_sequence)&&B::SameLease(a.lease,b.lease)&&EqR(a.rows,b.rows)
&&E(phase)&&E(state9B0)&&E(state9B4)&&E(category9D4)&&E(category9D8)&&E(loader469)&&E(selection9C6)
&&E(requests)&&E(opens)&&E(close_requests)&&E(close_completions)&&E(polls)&&E(retirement_requests)
&&E(retirements)&&E(rejected)&&E(unsupported)&&E(navigation)&&E(frame)&&E(clip)&&E(mode)
&&E(draw_frames)&&E(quads)&&E(callback_reaches)&&E(callback_null_returns)&&E(text_owner)&&E(text_capacity)
&&E(text_used)&&E(text_setup_stage)&&E(font_resets)&&E(font_flushes)&&E(text_scale_x)&&E(text_scale_y)
&&E(text_flush_selector)&&E(scissor_top)&&E(scissor_bottom)&&E(error);
#undef E
}
inline script::Program Program(){
const Bytes code={0x30,0,0x38};Bytes b(28+code.size()+4u*(2048+80+80));
B::Put(b,0,0x0043534B);B::Put(b,4,0x20030204);B::Put(b,12,U(code.size()));B::Put(b,22,80,2);B::Put(b,24,2048,2);B::Put(b,26,80,2);
std::copy(code.begin(),code.end(),b.begin()+28);script::Program p;script::Diagnostic d;
C(p.load_test_fixture(std::move(b),d),"W001");return p;
}
struct Data {
script::Program program=Program();script::Vm vm;V root=Ref(4),one=Ref(128),two=Ref(256);Q generation=71;
Data():vm(program){C(vm.start().status==script::Status::Running,"W002");(void)vm.context_value();
vm.store_reference(root,B::Header(2));vm.store_reference(Ref(5),one);vm.store_reference(Ref(6),two);
vm.store_reference(one,B::Header(4));vm.store_reference(Ref(129),B::Nil());vm.store_reference(Ref(130),B::Text(std::string("A\0B",3)));
vm.store_reference(Ref(131),B::Text(""));vm.store_reference(Ref(132),V::integer(77));
vm.store_reference(two,B::Header(2));vm.store_reference(Ref(257),V::integer(48));vm.store_reference(Ref(258),B::Text("last"));}
std::vector<V> Cells()const{std::vector<V> v;for(U i:{4u,5u,6u,128u,129u,130u,131u,132u,256u,257u,258u})v.push_back(vm.peek_reference(Ref(i)));return v;}
void Unchanged(const script::VmStateWitness& w,const std::vector<V>& old)const{
C(B::SameVm(w,vm.current_witness()),"W003");const auto after=Cells();C(after.size()==old.size(),"W004");
for(size_t i=0;i<old.size();++i)C(EqV(old[i],after[i]),"W005");}
R Capture(){const auto w=vm.current_witness();const auto cells=Cells();const auto r=GameObserveWordMemoRows(vm,generation,root);Unchanged(w,cells);return r;}
void Refuse(V registered,Q gen=71){const auto w=vm.current_witness();const auto cells=Cells();bool threw=false;
try{(void)GameObserveWordMemoRows(vm,gen,registered);}catch(const std::exception&){threw=true;}
C(threw,"W006");Unchanged(w,cells);}
};
inline Bytes Oracle(){
Bytes bytes;const auto val=[&](U kind,U word,U table,U index,std::string text={}){
for(U n:{kind,word,table,index,U(text.size())})for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(n>>(8*i)));
bytes.insert(bytes.end(),text.begin(),text.end());};
val(4,0,2,4);val(5,0x90000002,0,0);val(4,0,2,128);val(5,0x90000004,0,0);
val(1,0,0,0);val(2,0x50000000,0,0,std::string("A\0B",3));val(2,0x50000000,0,0);val(1,0x2000004D,0,0);
val(4,0,2,256);val(5,0x90000002,0,0);val(1,0x20000030,0,0);val(2,0x50000000,0,0,"last");return bytes;
}
struct Samples{R empty,populated;};
inline Samples CaptureAndFlags(){
Data d;const auto base=d.Capture();const auto bytes=Oracle();
C(bytes.size()==247&&B::Hex(core::sha256(bytes.data(),bytes.size()))=="4f30b5eceb1c006b784bfd5445dac0d5af33933e579b87dcee3309f036a8b987","W007");
C(base.valid&&base.records==2&&base.cells==6&&base.eligible_records==0&&base.root_table==2&&base.root_index==4
&&base.root_header==0x90000002&&base.script_generation==71&&base.run_id==d.vm.current_witness().run_id
&&base.code_sha==d.vm.current_witness().code_sha256&&base.sha==core::sha256(bytes.data(),bytes.size()),"W008");
C(EqR(base,d.Capture()),"W009");
const auto change=[&](U at,V v){const auto old=d.vm.peek_reference(Ref(at));d.vm.store_reference(Ref(at),v);
C(d.Capture().sha!=base.sha,"W010");d.vm.store_reference(Ref(at),old);C(EqR(base,d.Capture()),"W011");};
V v=d.root;v.word=1;C(GameObserveWordMemoRows(d.vm,71,v).sha!=base.sha,"W012");
v=d.one;v.word=1;change(5,v);v=B::Header(2);v.word|=0x4000;change(4,v);
v=B::Header(4);v.word|=0x4000;change(128,v);change(132,V::integer(78));
change(258,B::Text("last!"));change(130,B::Text(std::string("A\0C",3)));
v=B::Text("");v.kind=V::Kind::Scalar;change(131,v);
v=V::integer(77);v.kind=V::Kind::Timestamp;change(132,v);change(129,V::integer(0));
d.vm.store_reference(d.one,B::Header(30));for(U i=4;i<30;++i)d.vm.store_reference(Ref(129+i),V::integer(0));
const auto late=d.Capture();d.vm.store_reference(Ref(158),V::integer(7));C(d.Capture().sha!=late.sha&&late.cells==32,"W112");d.vm.store_reference(d.one,B::Header(4));
for(V at:{d.root,d.one}){auto old=d.vm.peek_reference(at);d.vm.store_reference(at,V::integer(2));d.Refuse(d.root);d.vm.store_reference(at,old);}
R populated;for(U flag:{32u,64u,96u,48u,80u,112u,0u,0x0FFFFFFFu}){
d.vm.store_reference(Ref(129),V::integer(int32_t(flag)));const auto r=d.Capture();
const U want=(flag==32||flag==64||flag==96)?1u:0u;
C(r.eligible_records==want&&r.records==2&&r.cells==6,"W013");if(flag==32)populated=r;
}d.vm.store_reference(Ref(129),B::Nil());
d.Refuse(d.root,0);d.Refuse(B::Nil());d.Refuse(Ref(65535));
for(T t:{T::Local,T::Ctx28,T::Frame,T::InputResult}){v=d.root;v.table=t;d.Refuse(v);
v=d.one;v.table=t;d.vm.store_reference(Ref(5),v);d.Refuse(d.root);d.vm.store_reference(Ref(5),d.one);}
script::Vm foreign(d.program);C(foreign.start().status==script::Status::Running,"W014");const auto context=foreign.context_value();
auto back=std::make_shared<std::vector<V>>(1);std::weak_ptr<std::vector<V>> stale=back;
for(bool expired:{false,true}){if(expired)back.reset();
v=d.root;v.frame_owner=stale;d.Refuse(v);v=d.one;v.frame_owner=stale;
d.vm.store_reference(Ref(5),v);d.Refuse(d.root);d.vm.store_reference(Ref(5),d.one);}
v=d.root;v.context_owner=context.context_owner;d.Refuse(v);v=d.one;v.context_owner=context.context_owner;
d.vm.store_reference(Ref(5),v);d.Refuse(d.root);d.vm.store_reference(Ref(5),d.one);
V stale_ctx;{script::Vm dead(d.program);C(dead.start().status==script::Status::Running,"W015");stale_ctx=dead.context_value();}
v=d.root;v.context_owner=stale_ctx.context_owner;d.Refuse(v);v=d.one;v.context_owner=stale_ctx.context_owner;
d.vm.store_reference(Ref(5),v);d.Refuse(d.root);d.vm.store_reference(Ref(5),d.one);
for(U count:{601u,4095u}){d.vm.store_reference(d.root,B::Header(count));d.Refuse(d.root);}d.vm.store_reference(d.root,B::Header(2));
d.vm.store_reference(Ref(5),B::Nil());d.Refuse(d.root);d.vm.store_reference(Ref(5),d.one);
for(U count:{0u,65u}){d.vm.store_reference(d.one,B::Header(count));d.Refuse(d.root);}d.vm.store_reference(d.one,B::Header(4));
for(U tag:{1u,3u,4u,5u,9u}){v=B::Nil();v.word=tag<<28;d.vm.store_reference(Ref(129),v);d.Refuse(d.root);}
d.vm.store_reference(Ref(129),B::Nil());v=B::Nil();v.word=1;d.vm.store_reference(Ref(129),v);d.Refuse(d.root);d.vm.store_reference(Ref(129),B::Nil());
for(V leaf:{Ref(600),B::Header(1),context,V{}}){d.vm.store_reference(Ref(258),leaf);d.Refuse(d.root);}
for(auto kind:{V::Kind::Code,V::Kind::Opaque}){v=B::Nil();v.kind=kind;d.vm.store_reference(Ref(258),v);d.Refuse(d.root);}
v=B::Nil();v.cp932="bad scalar payload";d.vm.store_reference(Ref(258),v);d.Refuse(d.root);
d.vm.store_reference(Ref(258),B::Text(std::string(65537,'x')));d.Refuse(d.root);d.vm.store_reference(Ref(258),B::Text("last"));
C(EqR(base,d.Capture()),"W016");
d.vm.store_reference(d.root,B::Header(600));d.vm.store_reference(Ref(1024),B::Header(1));d.vm.store_reference(Ref(1025),B::Nil());
for(U i=0;i<600;++i)d.vm.store_reference(Ref(5+i),Ref(1024));const auto bound=d.Capture();
C(bound.records==600&&bound.cells==600&&bound.eligible_records==0,"W017");
script::Vm cold(d.program);bool threw=false;try{(void)GameObserveWordMemoRows(cold,71,d.root);}catch(const std::exception&){threw=true;}C(threw,"W018");
return {base,populated};
}
inline L Lease(const R& r){return {r.script_generation,r.run_id,13,31,19,41,27};}
inline void AdmissionAndWitness(const Samples& rows){
GameWordMemo memo;const auto idle=memo.Snapshot();C(idle.state9B0==3&&idle.phase==0&&!memo.ClosedAndRetired()&&!memo.Modal(),"W019");
auto lease=Lease(rows.empty);const auto refuse=[&](R r,L l){const auto old=memo.Snapshot();C(!memo.Request(r,l)&&EqS(old,memo.Snapshot()),"W020");};
refuse(rows.populated,lease);auto bad=rows.empty;bad.valid=false;refuse(bad,lease);bad=rows.empty;++bad.run_id;refuse(bad,lease);
bad=rows.empty;++bad.script_generation;refuse(bad,lease);
for(auto member:{&L::script_generation,&L::run_id,&L::context,&L::parent_generation,&L::available_owner,&L::available_generation,&L::input_epoch}){auto l=lease;l.*member=0;refuse(rows.empty,l);}
C(bool(memo.Request(rows.empty,lease)),"W021");auto admitted=memo.Snapshot();
C(admitted.phase==1&&admitted.state9B0==3&&admitted.generation==1&&admitted.requests==1&&admitted.opens==0&&admitted.request8&&admitted.live&&!admitted.owner470&&!memo.ClosedAndRetired(),"W022");
refuse(rows.empty,lease);memo.Close();refuse(rows.populated,lease);
{using W=script::VmStateWitness;W c;c.valid=c.context_registered=true;c.run_id=11;c.frame_id=23;c.executed=19;
c.clock_tick=100;c.deadline_tick=200;c.relative_deadline23=100;c.current_pc=43;c.code_entry=7;c.context_word=0x80000002;
c.call_depth=2;c.active_blocks=3;c.event_scope_depth=1;c.caller_entry=1;c.caller_call_pc=13;c.caller_return_pc=17;
c.caller_frame_id=5;c.caller_stack_base=2;c.frame_cells=c.operand_cells=c.frame_captured=c.operands_captured=16;c.code_sha256.fill('a');c.code_sha256[64]=0;
for(U i=0;i<16;++i){auto& a=c.frame_values[i];a.kind=V::Kind::Scalar;a.word=0x20000000+i;a.table=2;a.index=i;
auto& b=c.operands[i];b=a;b.word+=32;}c.result=c.frame_values[0];W s=c;++s.event_scope_depth;
const auto exact=[&](const W& a,const W& b){return GameWordMemoPausedOwnerExact(a,b);};C(exact(s,c),"W023");
W advanced=c;advanced.clock_tick+=33;advanced.relative_deadline23-=33;C(exact(s,advanced),"W024");
for(auto member:{&W::run_id,&W::frame_id,&W::executed,&W::deadline_tick,&W::caller_frame_id,&W::caller_stack_base,&W::frame_cells,&W::operand_cells}){
auto m=c;++(m.*member);C(!exact(s,m),"W025");}
for(auto member:{&W::current_pc,&W::code_entry,&W::context_word,&W::call_depth,&W::active_blocks,&W::event_scope_depth,&W::queued_callbacks,
&W::caller_entry,&W::caller_call_pc,&W::caller_return_pc,&W::frame_captured,&W::operands_captured}){
auto m=c;++(m.*member);C(!exact(s,m),"W026");}
for(auto member:{&W::valid,&W::context_registered,&W::stopped,&W::event_envelope_present}){auto m=c;m.*member=!(m.*member);C(!exact(s,m),"W027");}
auto m=c;m.code_sha256[63]^=1;C(!exact(s,m),"W028");
for(U where=0;where<3;++where)for(U component=0;component<6;++component){m=c;auto& v=where==0?m.result:where==1?m.frame_values[15]:m.operands[15];
if(component==0)v.kind=V::Kind::String;if(component==1)++v.word;if(component==2)++v.table;if(component==3)++v.index;
if(component==4)++v.cp932_bytes;if(component==5)v.cp932_sha256[31]^=1;C(!exact(s,m),"W029");}
m=s;m.event_scope_depth=0;C(!exact(m,c),"W030");
m=s;auto n=c;m.frame_cells=n.frame_cells=17;C(!exact(m,n),"W031");
m=s;n=c;m.frame_cells=n.frame_cells=17;m.frame_captured=n.frame_captured=17;C(!exact(m,n),"W032");
m=s;n=c;m.operand_cells=n.operand_cells=17;m.operands_captured=n.operands_captured=17;C(!exact(m,n),"W033");
}
}
inline void Float(Bytes& b,U at,float f){U w=0;std::memcpy(&w,&f,4);B::Put(b,at,w);}
inline PackageView MemoImage(bool cb=true,bool scissor=true){
constexpr U nodes=40,clips=2488,keys=2904,values=2992,name=3200;Bytes b(3240);
B::Put(b,0,0x30413242);B::Put(b,4,1);B::Put(b,8,32);B::Put(b,12,306);B::Put(b,16,nodes);B::Put(b,20,8);B::Put(b,24,clips);B::Put(b,28,8);B::Put(b,36,name);
const char stem[]="wordmemo-synthetic.GIM";std::copy(stem,stem+sizeof(stem),b.begin()+name);
B::Put(b,nodes+2,scissor?70:(cb?304:0),2);B::Put(b,nodes+70*8+2,cb?304:0,2);B::Put(b,nodes+70*8+4,4,2);B::Put(b,nodes+304*8,305,2);
for(U i=0;i<8;++i){const U c=clips+i*52,k=keys+i*8,v=values+i*12;
B::Put(b,c,1);B::Put(b,c+4,k);B::Put(b,c+48,i==0?3:14);B::Put(b,k,i,2);B::Put(b,k+2,1,2);B::Put(b,k+4,v);B::Put(b,v,i==0?1:0);}
B::Put(b,clips+24,2);B::Put(b,clips+28,keys+64);
for(U i=0;i<2;++i){const U k=keys+64+i*8,v=values+96+i*16;B::Put(b,k,i==0?0:70,2);B::Put(b,k+2,1,2);B::Put(b,k+4,v);
Float(b,v,i==0?-40.0f:0);Float(b,v+4,i==0?-24.0f:-8.0f);}
B::Put(b,clips+8,1);B::Put(b,clips+12,keys+80);B::Put(b,keys+80,0,2);B::Put(b,keys+82,1,2);B::Put(b,keys+84,values+128);
Float(b,values+128,4);Float(b,values+132,4);return B::View(std::move(b));
}
struct Resources final:GameResourceSource {
bool cb=true,scissor=true,missing=false;U opens=0,textures=0;
GameFsStatus OpenResource(const std::string& path,PackageView& out)override{
if(path=="mkotoba\\gui\\kotoba_memo\\MEMO.CB2A"){++opens;if(missing)return {GameFsCode::NotFound,0,"synthetic missing MEMO"};out=MemoImage(cb,scissor);return {};}
if(path=="mkotoba\\gui\\kotoba_memo\\wordmemo-synthetic.GIM"){++textures;out=B::Gim();return {};}
return {GameFsCode::NotFound,0,"unexpected wordmemo fixture path"};}
};
struct Case {
Resources source;GameTransition fade;GameWordMemo m;L lease;Q sequence=0;
explicit Case(const R& rows,bool ready=true,bool cb=true,bool scissor=true):lease(Lease(rows)){
source.cb=cb;source.scissor=scissor;C(fade.Initialize({{0,0}})&&fade.Snapshot().state==0,"W034");
C(bool(m.Request(rows,lease)),"W035");Tick();if(ready)Ready();}
S Get()const{return m.Snapshot();}
void Tick(){C(bool(m.Tick(source,fade)),"W036");}
void Ready(){for(U i=0;Get().phase==2&&i<96;++i){fade.Update();Tick();}C(Get().phase==3&&fade.Snapshot().state==2,"W037");}
F Packet(U keys=0){return B::Frame(lease,++sequence,keys);}
void Step(U keys=0){C(m.Input(Packet(keys)),"W038");Tick();}
void Press(U keys){Step();Step(keys);}
void Reject(F f){auto want=Get();++want.rejected;C(!m.Input(f)&&EqS(want,Get()),"W039");}
void Poll(){for(U i=0;Get().phase==4&&i<96;++i){fade.Update();Tick();}C(Get().phase==6,"W040");}
};
inline void LifecycleAndInput(const R& rows){
Case c(rows,false);auto s=c.Get();C(s.phase==2&&s.state9B0==0&&s.live&&s.visible&&s.request8&&s.owner470&&s.loader469==1
&&s.opens==1&&s.text_owner&&s.text_capacity==1024&&s.text_used==0&&s.frame==0&&s.clip==0&&s.mode==1&&c.fade.Snapshot().state==1,"W041");
c.Reject(B::Frame(c.lease,1000,Cross));C(!c.m.SetInputEpoch(28),"W042");
for(U i=0;i<4;++i)c.Tick();C(c.Get().phase==2&&c.fade.Snapshot().state==1,"W043");
for(U i=0;i<96&&c.fade.Snapshot().state!=2;++i)c.fade.Update();C(c.Get().phase==2,"W044");
c.Reject(B::Frame(c.lease,1001,Cross));c.Tick();C(c.Get().phase==3&&c.Get().close_requests==0,"W045");
c.Step(Cross);C(c.Get().phase==3,"W046");c.Press(Cross);s=c.Get();
C(s.phase==4&&s.state9B0==2&&s.request8&&s.visible&&s.text_owner&&s.loader469==1&&s.close_requests==1&&s.polls==0&&c.fade.Snapshot().state==3,"W047");
for(U i=0;i<4;++i)c.Tick();C(c.Get().phase==4,"W048");c.Poll();s=c.Get();
C(s.state9B0==3&&!s.visible&&s.live&&s.owner470&&s.text_owner&&s.text_capacity==1024&&s.retirement_pending&&!s.request8&&s.loader469==0
&&s.polls==1&&s.close_completions==1&&s.retirement_requests==1&&s.retirements==0&&s.retired_generation==0&&!c.m.ClosedAndRetired(),"W049");
C(!c.m.Request(rows,c.lease)&&EqS(s,c.Get()),"W050");c.Tick();s=c.Get();
C(s.phase==7&&!s.live&&!s.owner470&&!s.text_owner&&s.text_capacity==0&&!s.retirement_pending&&s.retired_generation==s.generation
&&s.retirements==1&&c.m.ClosedAndRetired()&&!c.m.Modal()&&EqR(s.rows,rows),"W051");c.Tick();C(EqS(s,c.Get()),"W052");
C(bool(c.m.Request(rows,c.lease)),"W053");s=c.Get();C(s.generation==2&&s.requests==2&&s.polls==1&&s.retirements==1
&&s.retired_generation==0&&!c.m.ClosedAndRetired(),"W054");c.m.Close();
C(!c.m.ClosedAndRetired()&&c.Get().retired_generation==0&&c.Get().retirements==1,"W055");
for(bool polled:{false,true}){Case forced(rows);if(polled){forced.Press(Cross);forced.Poll();}const auto old=forced.Get();forced.m.Close();const auto end=forced.Get();
C(end.phase==7&&!end.live&&!end.text_owner&&!end.request8&&!end.owner470&&end.polls==old.polls&&end.retirements==old.retirements
&&end.retired_generation==0&&!forced.m.ClosedAndRetired(),"W056");}
Case in(rows);for(auto field:{&F::message_owner_generation,&F::message_context,&F::returned_owner,&F::returned_generation,&F::main_menu_epoch}){
auto f=B::Frame(in.lease,1000,Cross);++(f.*field);in.Reject(f);}in.Reject(B::Frame(in.lease,0,Cross));
in.Step(Cross);in.Reject(B::Frame(in.lease,in.sequence,Cross));C(in.Get().phase==3,"W057");
const auto old_lease=in.lease;const std::array<F,2> oldbuf={B::Frame(old_lease,900),B::Frame(old_lease,901,Cross)};
C(!in.m.SetInputEpoch(27)&&!in.m.SetInputEpoch(26)&&in.m.SetInputEpoch(28),"W058");++in.lease.input_epoch;
for(const auto& f:oldbuf)in.Reject(f);C(oldbuf[0].main_menu_epoch==27&&oldbuf[1].buttons==Cross
&&in.Get().lease.input_epoch==27&&in.Get().input_epoch==28,"W059");
in.Step(Cross);C(in.Get().phase==3,"W060");in.Press(Cross);C(in.Get().phase==4,"W061");
for(U boundary=0;boundary<3;++boundary){Case focus(rows);focus.Step();C(focus.m.Input(focus.Packet(Cross)),"W062");
if(boundary<2){F lost{};lost.focused=boundary==1;lost.overflow_resync=boundary==1;focus.Reject(lost);}else focus.m.DisarmInput();
focus.Tick();focus.Step(Cross);C(focus.Get().phase==3,"W063");focus.Press(Cross);C(focus.Get().phase==4,"W064");}
Case pending(rows);pending.Step();C(pending.m.Input(pending.Packet(Cross)),"W065");auto stale=B::Frame(pending.lease,1000);++stale.returned_generation;
pending.Reject(stale);pending.Tick();C(pending.Get().phase==4,"W066");
Case dead(rows);dead.Press(Cross);dead.Poll();dead.fade.Retire();C(!dead.m.Tick(dead.source,dead.fade)
&&dead.Get().retirements==0&&dead.Get().text_owner&&dead.Get().retired_generation==0,"W067");dead.m.Close();
}
// Family4: 13 categories conditional on eligible0.
inline void CategoriesAndPriority(const R& rows){
C(rows.records==2&&rows.eligible_records==0,"W068");Case c(rows);c.Step();C(c.Get().selection9C6==-1,"W069");
for(U want=2;want<=13;++want){c.Press(Right);C(c.Get().category9D4==want&&c.Get().category9D8==want&&c.Get().selection9C6==-1
&&c.Get().rows.eligible_records==0&&c.Get().unsupported==0,"W070");}
for(U i=0;i<30;++i)c.Step(Right);C(c.Get().category9D4==13,"W071");c.Press(Right);C(c.Get().category9D4==1,"W072");
c.Press(Left);C(c.Get().category9D4==13,"W073");for(U i=0;i<50;++i)c.Step(Left);C(c.Get().category9D4==1,"W074");
Case repeat(rows);repeat.Step();repeat.Step(Right);for(U i=1;i<=9;++i){repeat.Step(Right);C(repeat.Get().category9D4==2,"W075");}
repeat.Step(Right);C(repeat.Get().category9D4==3,"W076");repeat.Step(Right);C(repeat.Get().category9D4==3,"W077");repeat.Step(Right);C(repeat.Get().category9D4==4,"W078");
for(U key:{Down,Up,Right,Left,Circle}){Case prio(rows);prio.Press(key|Cross);const auto s=prio.Get();C(s.phase==3&&s.close_requests==0
&&s.selection9C6==-1&&s.unsupported==0,"W079");}
Case all(rows);all.Press(Down|Up|Right|Left|Circle|Cross);C(all.Get().category9D4==1&&all.Get().phase==3,"W080");
all.Press(Up|Right|Cross);C(all.Get().category9D4==1&&all.Get().phase==3,"W081");all.Press(Right|Left|Cross);
C(all.Get().category9D4==2&&all.Get().phase==3,"W082");
Case held(rows);held.Step();held.Step(Down);for(U i=1;i<=9;++i)held.Step(Down);held.Step(Down|Cross);
C(held.Get().phase==3&&held.Get().close_requests==0,"W083");held.Press(Cross);C(held.Get().phase==4,"W084");
}
// Family5: callbacks/FONT/view-space scissor.
inline void DrawAndOwners(const R& rows){
GameGraphics g;GameFontResource font;C(g.Initialize({})&&bool(font.Initialize(B::FontImage()))&&font.Snapshot().ready,"W085");
Resources source;auto r=std::make_shared<GameUiResource>();C(bool(r->Load(source,"mkotoba\\gui\\kotoba_memo\\MEMO.CB2A","mkotoba\\gui\\kotoba_memo\\")),"W086");
C(r->RootCount()==8&&r->InitialNodes().size()==306&&r->Clips().size()==8&&r->Textures().size()==1,"W087");
for(U i=0;i<8;++i)C(r->Clips()[i].channels[0].count==1&&r->Clips()[i].field30>1,"W088");
GameUiInstance ui;C(ui.Clone(r),"W089");for(U i:{0u,2u,4u,5u,6u,7u,1u,3u})C(ui.Bind(i,i,(i==1||i==3)?1u:0u),"W090");
for(U clip:{0u,2u,6u,7u})C(ui.AddControl(clip)!=nullptr,"W091");
for(U i=1;i<4;++i){auto* ctl=ui.Control(i);ctl->frame=1;ctl->mode=2;ctl->active=1;}
ui.Control(1)->frame=13;C(ui.Control(1)->clip==2&&r->Clips()[2].field30>=14,"W092");
C(ui.ShowRoot(0)&&ui.SetSourceCallback(304,0x2EF4C)&&ui.SetSourceCallback(305,0x2F01C),"W093");std::string error;C(ui.Update(error),"W094");
GameUiMatrix world;C(ui.WorldMatrix(70,world)&&world.ty==-32,"W095");
struct Probe final:GameUiCallbacks{U mask=0;bool DrawUiCallback(U pc,U node,const GameUiMatrix&,U,GameGraphics&,std::string&)override{
if(pc==0x2EF4C&&node==304){mask|=1;return true;}if(pc==0x2F01C&&node==305){mask|=2;return true;}return false;}} probe;
GameUiDrawStats stats;C(ui.Draw(g,stats,error,&probe)&&probe.mask==3&&stats.callbacks==2&&stats.quads==1,"W096");
Case c(rows);GameFontResource absent;const auto initial=c.Get();C(!c.m.Draw(g,absent)&&EqS(initial,c.Get()),"W097");
C(g.Clear(0xFF000000)&&g.SetScissor(0,0,480,272),"W098");const auto pixels=g.Snapshot().mesh_pixels;
C(bool(c.m.Draw(g,font)),"W099");auto s=c.Get();const auto config=g.Snapshot().config;
C(s.callback_reaches==2&&s.callback_null_returns==2&&s.draw_frames==1&&s.quads==1&&g.Snapshot().mesh_pixels>pixels,"W100");
C(s.text_owner&&s.text_capacity==1024&&s.text_used==0&&s.text_setup_stage==5&&s.text_scale_x==1&&s.text_scale_y==1
&&s.text_flush_selector==-1&&s.font_resets==1&&s.font_flushes==1,"W101");
C(s.scissor_top==104&&s.scissor_bottom==262&&s.scissor_bottom-s.scissor_top==158&&config.scissor_left==0&&config.scissor_top==0
&&config.scissor_right==480&&config.scissor_bottom==272,"W102");
C(bool(c.m.Draw(g,font))&&c.Get().callback_reaches==4&&c.Get().callback_null_returns==4&&c.Get().font_flushes==2,"W103");
font.Close();s=c.Get();C(!c.m.Draw(g,font)&&EqS(s,c.Get()),"W104");
c.Press(Cross);c.Poll();s=c.Get();C(bool(c.m.Draw(g,font))&&EqS(s,c.Get()),"W105");c.Tick();s=c.Get();
C(bool(c.m.Draw(g,font))&&EqS(s,c.Get()),"W106");C(bool(font.Initialize(B::FontImage())),"W107");
Case unreached(rows,true,false);C(bool(unreached.m.Draw(g,font))&&unreached.Get().callback_reaches==0&&unreached.Get().callback_null_returns==0,"W108");
Case no_scissor(rows,true,true,false);C(!no_scissor.m.Draw(g,font)&&no_scissor.Get().callback_reaches==2&&no_scissor.Get().callback_null_returns==2
&&no_scissor.Get().draw_frames==0&&no_scissor.Get().font_flushes==0,"W109");
GameWordMemo missing;Resources absent_src;absent_src.missing=true;GameTransition fade;C(fade.Initialize({{0,0}})&&bool(missing.Request(rows,Lease(rows))),"W110");
C(!missing.Tick(absent_src,fade)&&missing.Snapshot().opens==0&&!missing.Snapshot().owner470&&!missing.Snapshot().text_owner,"W111");missing.Close();
}
}
inline void GameWordMemoRegressionChecks(){using namespace dokodemo::wordmemo_tests;
const auto samples=CaptureAndFlags();AdmissionAndWitness(samples);LifecycleAndInput(samples.empty);CategoriesAndPriority(samples.empty);DrawAndOwners(samples.empty);
}
