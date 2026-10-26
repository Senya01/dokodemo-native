#pragma once
#include "../src/recovered/game_main_menu_native.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
// 1-5: supplied-mode control-tests. 6: public UI Tick, synthetic CB2A/GIM.
// No original-asset, PSP, host or CoreMenuVM callback proof.
namespace dokodemo::main_menu_tests {
using namespace recovered;
using Control=GameMainMenuControl;using Snapshot=GameMainMenuSnapshot;
using MenuLease=GameMainMenuLease;using InputFrame=GameInputFrame;
inline void Check(bool ok,const char* text) {
if(!ok)throw std::runtime_error(std::string("main-menu control-test: ")+text);
}
inline MenuLease Lease() { return {11,22,33,44,55,66}; }
inline bool SameLease(const MenuLease& a,const MenuLease& b) {
return a.script_generation==b.script_generation&&a.run_id==b.run_id
  &&a.available_owner==b.available_owner&&a.available_generation==b.available_generation
  &&a.message_context==b.message_context&&a.input_epoch==b.input_epoch;
}
inline bool Same(const Snapshot& a,const Snapshot& b) {
return a.live==b.live&&a.full==b.full&&a.source_scene_nonzero==b.source_scene_nonzero
  &&a.generation==b.generation&&SameLease(a.lease,b.lease)&&a.state78==b.state78&&a.substate7C==b.substate7C
  &&a.opens==b.opens&&a.cancels==b.cancels&&a.closed==b.closed&&a.rejected==b.rejected
  &&a.unsupported==b.unsupported&&a.row==b.row&&a.column==b.column&&a.result==b.result
  &&a.frame==b.frame&&a.clip==b.clip&&a.mode==b.mode&&a.draw_frames==b.draw_frames&&a.quads==b.quads;
}
inline InputFrame Frame(MenuLease lease,uint64_t sequence,uint32_t buttons=0,bool focused=true) {
InputFrame f;f.buttons=buttons;f.focused=focused;f.test_injected=true;f.sequence=sequence;
f.message_owner_generation=lease.script_generation;f.message_context=lease.message_context;
f.returned_owner=lease.available_owner;f.returned_generation=lease.available_generation;
f.main_menu_epoch=lease.input_epoch;return f;
}
struct Case {
Control menu;MenuLease lease=Lease();uint64_t sequence=0;
explicit Case(bool scene=false) {
  Check(menu.Begin(lease,true,scene),"valid Begin");
  menu.Tick(2);
  Check(menu.Snapshot().state78==1,"mode2 admits input");
}
bool Input(uint32_t keys=0,bool focus=true) { return menu.Input(Frame(lease,++sequence,keys,focus)); }
void Step(uint32_t keys=0,bool focus=true) {
  Check(Input(keys,focus),"current input");menu.Tick(2);
}
Snapshot State()const { return menu.Snapshot(); }
void Press(uint32_t keys) { Step();Step(keys); }
};
inline void LiteralResults() {
// ROOT_MENU_OPEN.v1.json D8410/D8480: independent 44-word oracle.
constexpr int32_t full[7][4]={{0,-1,-1,-1},{2,-1,-1,-1},{1,-1,-1,-1},
  {3,4,6,7},{8,9,10,11},{5,-1,-1,-1},{13,14,15,-1}};
constexpr int32_t compact[4][4]={{16,-1,-1,-1},{3,4,6,7},{5,-1,-1,-1},{13,14,15,-1}};
for(int32_t r=0;r<7;++r)for(int32_t c=0;c<4;++c)
  Check(Control::Resolve(true,r,c)==full[r][c],"full result cell");
for(int32_t r=0;r<4;++r)for(int32_t c=0;c<4;++c)
  Check(Control::Resolve(false,r,c)==compact[r][c],"compact result cell");
for(bool mode:{false,true}) {
  const int32_t rows=mode?7:4;
  for(int32_t r:{INT32_MIN,-2,-1,rows,rows+1,INT32_MAX})for(int32_t c:{0,3})
      Check(Control::Resolve(mode,r,c)==-1,"bad row refuses");
  for(int32_t c:{INT32_MIN,-2,-1,4,5,INT32_MAX})for(int32_t r:{0,rows-1})
      Check(Control::Resolve(mode,r,c)==-1,"bad column refuses");
}
}
inline void InputLeases() {
using Field=uint64_t InputFrame::*;
constexpr Field identities[]={&InputFrame::message_owner_generation,&InputFrame::message_context,
  &InputFrame::returned_owner,&InputFrame::returned_generation,&InputFrame::main_menu_epoch};
for(auto field:identities) {
  Case f;f.Step(Down);
  auto wrong=Frame(f.lease,++f.sequence);++(wrong.*field);
  Check(!f.menu.Input(wrong),"foreign release");
  wrong=Frame(f.lease,++f.sequence,Down);++(wrong.*field);
  Check(!f.menu.Input(wrong),"foreign press");
  f.menu.Tick(2);f.Step(Down);
  Check(f.State().row==0&&f.State().rejected==2,"wrong identity cannot arm");
  f.Press(Down);Check(f.State().row==1,"fresh pair after refusal");
}
Case focus;focus.Step(Down);
Check(focus.State().row==0,"held at open cannot move");
focus.Step(0,false);focus.Step(Down);
Check(focus.State().row==0,"unfocused release cannot arm");
focus.Press(Down);Check(focus.State().row==1,"focused release arms");
focus.Step(0,false);focus.Step(Down);
Check(focus.State().row==1,"refocus held");
Case stale;stale.Step();
Check(!stale.menu.Input(Frame(stale.lease,stale.sequence,Down))
  &&!stale.menu.Input(Frame(stale.lease,0,Cross)),"stale sequences");
stale.menu.Tick(2);
Check(stale.State().row==0&&stale.State().state78==1,"stale press has no effect");
stale.Step(Down);Check(stale.State().row==1,"newer press admitted");
Case overflow;
overflow.Input();overflow.Input(Down); // unticked edge
auto lost=Frame(overflow.lease,++overflow.sequence);lost.overflow_resync=true;
Check(!overflow.menu.Input(lost),"overflow disarms queued edge");
overflow.menu.Tick(2);overflow.Step(Down);
Check(overflow.State().row==0,"overflow held cannot move");
overflow.Press(Down);Check(overflow.State().row==1,"overflow needs release");
Case resync;resync.Step();InputFrame zero{};zero.overflow_resync=true;
Check(!resync.menu.Input(zero),"zero resync rejects");resync.Step(Cross);
Check(resync.State().state78==1,"zero resync disarms Cross");resync.Press(Cross);
Check(resync.State().state78==2&&resync.State().cancels==1,"release after zero resync");
Case epoch;const auto old=epoch.lease;epoch.menu.Close();++epoch.lease.input_epoch;
Check(epoch.menu.Begin(epoch.lease,true,false),"epoch reuses token");
epoch.menu.Tick(2);GameInputMailbox queued;
queued.Push(Frame(old,++epoch.sequence));queued.Push(Frame(old,++epoch.sequence,Down));
InputFrame sample;
while(queued.Pop(sample))Check(!epoch.menu.Input(sample),"old queued epoch rejected");
epoch.Step(Down);Check(epoch.State().row==0,"old release cannot arm");
epoch.Press(Down);const auto now=epoch.State();
Check(now.row==1&&now.lease.available_owner==old.available_owner
  &&now.lease.available_generation==old.available_generation&&SameLease(now.lease,epoch.lease),"epoch preserves token");
}
inline void SourceRows() {
constexpr int32_t down[2][6]={{1,3,4,5,6,0},{2,3,4,5,6,0}};
constexpr int32_t up[2][6]={{6,5,4,3,1,0},{6,5,4,3,2,0}};
for(unsigned scene=0;scene<2;++scene) {
  Case f(scene!=0);
  for(auto row:down[scene]){f.Press(Down);Check(f.State().row==row,"Down skip/wrap");}
  for(auto row:up[scene]){f.Press(Up);Check(f.State().row==row,"Up skip/wrap");}
}
Case repeat;repeat.Press(Down);
for(unsigned tick=1;tick<=24;++tick) {
  repeat.Step(Down);
  const int32_t row=tick<10?1:tick<12?3:tick<14?4:tick<16?5:6;
  Check(repeat.State().row==row,"Down repeat10/2 clamps");
}
repeat.Press(Down);Check(repeat.State().row==0,"fresh Down wraps");
repeat.Press(Up);
for(unsigned tick=1;tick<=24;++tick) {
  repeat.Step(Up);
  const int32_t row=tick<10?6:tick<12?5:tick<14?4:tick<16?3:tick<18?1:0;
  Check(repeat.State().row==row,"Up repeat10/2 clamps");
}
Case both;both.Press(Down|Up);
Check(both.State().row==1,"Down has priority over Up");
for(uint32_t cancel:{uint32_t(Cross),uint32_t(Left)}) {
  Case f;f.Step(cancel);
  Check(f.State().state78==1,"held Cancel at open refuses");
  f.Press(cancel);const auto s=f.State();
  Check(s.state78==2&&s.result==-1&&s.row==-1&&s.column==-1&&s.cancels==1&&s.closed==0,"fresh Cancel starts close/-1");
}
}
inline void ControlLifecycle() {
Control m;auto lease=Lease();uint64_t seq=0;
Check(!m.Input(Frame(lease,++seq,Cross))&&!m.Snapshot().live,"cold input cannot open");
Check(m.Begin(lease,true,false),"lifecycle begins");
for(unsigned i=0;i<3;++i)m.Tick(1);
Check(m.Snapshot().state78==0&&!m.Input(Frame(lease,++seq)),"mode1 gates opening");
m.Tick(2);m.Input(Frame(lease,++seq));m.Input(Frame(lease,++seq,Cross));m.Tick(2);
const auto closing=m.Snapshot();
Check(closing.state78==2&&closing.result==-1&&closing.cancels==1&&closing.closed==0,"cancel still unfinished");
for(unsigned i=0;i<3;++i)m.Tick(1);
Check(m.Snapshot().state78==2&&!m.Input(Frame(lease,++seq,Left)),"mode1 gates closing");
m.Tick(2);for(unsigned i=0;i<3;++i)m.Tick(2);
Check(m.Snapshot().state78==3&&m.Snapshot().closed==1&&m.Snapshot().cancels==1,"completion counted once");
++lease.input_epoch;Check(m.Begin(lease,true,true),"completed lease reopens");
Check(m.Snapshot().generation==closing.generation+1&&m.Snapshot().opens==2
  &&m.Snapshot().state78==0&&m.Snapshot().row==0&&SameLease(m.Snapshot().lease,lease),"reopen resets generation/row");
m.Close();m.Tick(2);
Check(!m.Snapshot().live&&m.Snapshot().closed==1,"retired open cannot finish");
Check(m.Begin(lease,true,false),"retired reopen");m.Tick(2);
m.Input(Frame(lease,++seq));m.Input(Frame(lease,++seq,Left));m.Tick(2);m.Close();m.Tick(2);
Check(!m.Snapshot().live&&m.Snapshot().closed==1,"retired close cannot finish");
}
inline void Refusals() {
using Field=uint64_t MenuLease::*;
constexpr Field fields[]={&MenuLease::script_generation,&MenuLease::run_id,
  &MenuLease::available_owner,&MenuLease::available_generation,
  &MenuLease::message_context,&MenuLease::input_epoch};
Control cold;const auto initial=cold.Snapshot();
for(auto field:fields){auto bad=Lease();bad.*field=0;
  Check(!cold.Begin(bad,true,false)&&Same(initial,cold.Snapshot()),"zero lease refusal");}
Check(!cold.Begin(Lease(),false,false)&&Same(initial,cold.Snapshot()),"compact Begin unsupported");
for(uint32_t phase=0;phase<3;++phase) {
  Control m;const auto lease=Lease();Check(m.Begin(lease,true,false),"refusal phase starts");
  if(phase)m.Tick(2);
  if(phase==2){m.Input(Frame(lease,1));m.Input(Frame(lease,2,Cross));m.Tick(2);}
  const auto before=m.Snapshot();auto other=lease;++other.run_id;++other.input_epoch;
  Check(!m.Begin(other,true,true)&&!m.Begin(other,false,true)&&Same(before,m.Snapshot()),"live replacement unchanged");
}
for(int32_t row=0;row<7;++row) {
  if(row==0||row==4||row==5||row==6)continue; // separate finite owning regressions
  Case f(row==2);
  for(unsigned guard=0;f.State().row!=row&&guard<7;++guard)f.Press(Down);
  Check(f.State().row==row,"reach each full-mode row");
  const auto before=f.State();uint32_t unsupported=before.unsupported;
  for(uint32_t keys:{uint32_t(Circle),uint32_t(Circle|Cross),uint32_t(Circle|Left),uint32_t(Circle|Right|Cross)}) {
      f.Press(keys);auto expected=before;expected.unsupported=++unsupported;
      Check(Same(expected,f.State()),"Circle priority; lease unchanged");
  }
  for(uint32_t keys:{uint32_t(Right),uint32_t(Right|Cross),uint32_t(Right|Left)}) {
      f.Press(keys);if(row==3||row==4||row==6)++unsupported;
      auto expected=before;expected.unsupported=unsupported;
      Check(Same(expected,f.State()),"Right priority; lease unchanged");
  }
  f.Press(Cross);Check(f.State().state78==2&&f.State().cancels==1
      &&SameLease(f.State().lease,f.lease),"Cancel after refusal");
}
}
inline void Row4OpenBack() {
for(bool scene:{false,true})for(uint32_t open:{uint32_t(Circle|Cross),uint32_t(Right|Cross)})
for(uint32_t back:{uint32_t(Cross),uint32_t(Left)}) {
  Case f(scene);while(f.State().row!=4)f.Press(Down);
  const auto owner=f.State();f.Press(open);auto s=f.State();
  Check(s.substate7C==2&&s.state78==1&&s.column==0&&s.result==-1
    &&s.generation==owner.generation&&SameLease(s.lease,owner.lease)&&s.opens==1&&s.cancels==0,"row4 opens same owner ahead of Cancel");
  f.menu.Tick(1);f.Step(back);Check(f.State().substate7C==2,"transition held Back cannot arm");
  auto bad=Frame(f.lease,++f.sequence,back);++bad.message_context;
  Check(!f.menu.Input(bad),"submenu foreign context refuses");f.menu.Tick(2);
  for(uint32_t keys:{uint32_t(Down|Circle|Cross),uint32_t(Up|Circle|Left),uint32_t(Circle|Cross)}) {
    const auto before=f.State();f.Press(keys);s=f.State();
    Check(s.unsupported==before.unsupported+1&&s.substate7C==2&&s.column==0&&s.state78==1&&s.result==-1
      &&s.closed==0&&s.cancels==0&&SameLease(s.lease,owner.lease),"unknown navigation and leaf refuse before close/effects");
    Check(s.unsupported_pc==((keys&Down)?0x12E4Cu:(keys&Up)?0x12F20u:0x12F90u),"exact refused consumer");
  }
  f.Press(back);s=f.State();
  Check(s.substate7C==0&&s.row==4&&s.state78==1&&s.result==-1&&s.closed==0&&s.cancels==0
    &&s.generation==owner.generation&&SameLease(s.lease,owner.lease),"Back retains main owner without consumption/rearm");
  f.Step(back);Check(f.State().state78==1,"held Back cannot cancel parent");
  f.Press(back);Check(f.State().state78==2&&f.State().cancels==1,"separate fresh Cancel closes parent");
  f.menu.Tick(1);Check(f.State().closed==0,"Cancel waits actual close control");f.menu.Tick(2);
  Check(f.State().state78==3&&f.State().closed==1&&f.State().result==-1,"Cancel returns once");
  f.menu.Close();Check(!f.Input(Circle),"retired submenu rejects input");
}
}
inline void Put(std::vector<uint8_t>& b,size_t at,uint32_t word,unsigned width=4) {
for(unsigned i=0;i<width;++i)b.at(at+i)=uint8_t(word>>(8*i));
}
struct Storage final:PackageStorageLease {
std::vector<uint8_t> bytes,known;
explicit Storage(std::vector<uint8_t> b):bytes(std::move(b)),known(bytes.size(),1){}
bool Span(PackageStorageSpan& s,std::string&)const override {
  s.bytes=const_cast<uint8_t*>(bytes.data());s.initialized=const_cast<uint8_t*>(known.data());
  s.size=bytes.size();s.host_address=uint64_t(reinterpret_cast<uintptr_t>(bytes.data()));
  s.owner=s.host_address;s.generation=s.serial=1;s.readable=true;s.writable=false;return true;
}
};
inline PackageView View(std::vector<uint8_t> b) {
const auto size=b.size();return PackageStorage::Attach(std::make_shared<Storage>(std::move(b)),size);
}
struct Resources final:GameResourceSource {
uint32_t templates=0,textures=0;bool missing=false;
GameFsStatus OpenResource(const std::string& path,PackageView& out)override {
  if(path=="data\\gui\\main_menu\\MMENU.CB2A") {
      ++templates;if(missing)return {GameFsCode::NotFound,0,"synthetic template unavailable"};
      // Synthetic: 22 roots, 30 clips, texture keys, ends3/4.
      constexpr uint32_t nodes=40,clips=216,keys=1776,values=1792,name=1804;
      std::vector<uint8_t> b(1824);Put(b,0,0x30413242);Put(b,4,1);Put(b,8,32);
      Put(b,12,22);Put(b,16,nodes);Put(b,20,30);Put(b,24,clips);Put(b,28,22);Put(b,36,name);
      const char stem[]="test-menu.GIM";for(size_t i=0;i<sizeof(stem);++i)b.at(name+i)=uint8_t(stem[i]);
      for(uint32_t i=0;i<30;++i)Put(b,clips+i*52+48,i==0?3:i==1?4:12);
      for(uint32_t i=0;i<2;++i){Put(b,clips+i*52,1);Put(b,clips+i*52+4,keys+i*8);Put(b,keys+i*8+2,1,2);Put(b,keys+i*8+4,values);}
      Put(b,clips+4*52,1);Put(b,clips+4*52+4,keys); // synthetic submenu clip uses the same white quad
      Put(b,values,1);out=View(std::move(b));return {};
  }
  if(path=="data\\gui\\main_menu\\test-menu.GIM") {
      ++textures;std::vector<uint8_t> b(132);const char magic[]="MIG.00.1PSP";
      for(size_t i=0;i<sizeof(magic);++i)b[i]=uint8_t(magic[i]);
      for(uint32_t at:{16u,32u,48u}){Put(b,at,at==16?2:at==32?3:4,2);Put(b,at+2,16,2);
          Put(b,at+4,132-at);Put(b,at+8,at==48?84:16);Put(b,at+12,16);}
      Put(b,64,48,2);Put(b,68,3,2);Put(b,72,1,2);Put(b,74,1,2);Put(b,76,32,2);Put(b,78,4,2);Put(b,80,1,2);
      Put(b,88,48);Put(b,92,64);Put(b,96,68);Put(b,104,1,2);Put(b,106,1,2);Put(b,108,3,2);Put(b,110,1,2);
      Put(b,112,64);Put(b,128,0xFF4080E0);out=View(std::move(b));return {};
  }
  return {GameFsCode::NotFound,0,"unexpected synthetic menu resource"};
}
};
inline void Ui(bool ok,const char* text) { if(!ok)throw std::runtime_error(std::string("main-menu synthetic-UI: ")+text); }
inline void SyntheticClips() {
Resources source;GameMainMenu menu;auto lease=Lease();uint64_t seq=0;
Ui(!menu.Open(source,lease,false,false)&&source.templates==0&&!menu.Modal(),"compact refuses before load");
source.missing=true;Ui(!menu.Open(source,lease,true,false)&&!menu.Snapshot().live,"missing resource cannot open");source.missing=false;
Ui(bool(menu.Open(source,lease,true,false)),"loader/clone/bind fixture");
const auto opened=menu.Snapshot();
Ui(opened.state78==0&&opened.clip==0&&opened.frame==0&&opened.mode==1&&menu.Modal(),"control0 starts unfinished");
Ui(!menu.Open(source,lease,true,true)&&Same(opened,menu.Snapshot()),"live Open preserves clone");
GameGraphics graphics;Ui(graphics.Initialize({})&&bool(menu.Draw(graphics)),"real fixture texture draw");
Ui(menu.Snapshot().quads==1&&menu.Snapshot().draw_frames==1,"texture reaches Draw");
for(uint32_t frame=1;frame<=3;++frame) {
  Ui(!menu.Input(Frame(lease,++seq,Cross)),"opening rejects Cancel");
  Ui(bool(menu.Tick()),"actual opening Update");const auto s=menu.Snapshot();
  Ui(s.frame==frame&&s.clip==0&&s.mode==(frame<3?1u:2u)&&s.state78==(frame<3?0u:1u),"end3 completes opening");
}
Ui(menu.Input(Frame(lease,++seq,Cross))&&bool(menu.Tick())&&menu.Snapshot().state78==1,"opening held cannot arm");
Ui(menu.Input(Frame(lease,++seq))&&menu.Input(Frame(lease,++seq,Cross))&&bool(menu.Tick()),"fresh Cancel starts clip1");
auto s=menu.Snapshot();Ui(s.state78==2&&s.clip==1&&s.frame==0&&s.mode==1&&s.result==-1&&s.closed==0,"clip1 starts unfinished");
for(uint32_t frame=1;frame<=4;++frame) {
  Ui(bool(menu.Tick()),"actual closing Update");s=menu.Snapshot();
  Ui(s.frame==frame&&s.clip==1&&s.state78==(frame<4?2u:3u)&&s.mode==(frame<4?1u:0u)
      &&s.closed==(frame<4?0u:1u)&&menu.Modal()==(frame<4),"end4 completes closing");
}
const auto completed=menu.Snapshot();Ui(bool(menu.Tick())&&bool(menu.Draw(graphics))&&Same(completed,menu.Snapshot()),"completed menu inert");
const auto old=lease;++lease.input_epoch;
Ui(bool(menu.Open(source,lease,true,true)),"real close allows reopen");
Ui(menu.Snapshot().generation==opened.generation+1&&menu.Snapshot().opens==2
  &&source.templates==2&&source.textures==1&&SameLease(menu.Snapshot().lease,lease),"reopen generation/token/cache");
for(unsigned i=0;i<3;++i)Ui(bool(menu.Tick()),"reopen completes through Update");
Ui(!menu.Input(Frame(old,++seq))&&!menu.Input(Frame(old,++seq,Left)),"old epoch cannot arm reopen");
Ui(menu.Input(Frame(lease,++seq,Left))&&bool(menu.Tick())&&menu.Snapshot().state78==1,"old release cannot arm Left");
menu.Close();const auto retired=menu.Snapshot();
Ui(!retired.live&&!menu.Modal()&&!menu.Input(Frame(lease,++seq,Cross))&&bool(menu.Tick()),"retired menu rejects input");
}
inline void SyntheticRow4Clip() {
Resources source;GameMainMenu menu;const auto lease=Lease();uint64_t seq=0;GameGraphics graphics;
Ui(graphics.Initialize({})&&bool(menu.Open(source,lease,true,false)),"row4 fixture opens");
for(unsigned i=0;i<3;++i)Ui(bool(menu.Tick()),"opening clip completes");
const auto edge=[&](uint32_t key){
  for(uint32_t k:{0u,key})Ui(menu.Input(Frame(lease,++seq,k))&&bool(menu.Tick()),"real UI fresh edge");
};
while(menu.Snapshot().row!=4)edge(Down);const auto owner=menu.Snapshot();edge(Right);
auto s=menu.Snapshot();Ui(s.substate7C==2&&s.clip==4&&s.frame==0&&s.mode==1,"authored row4 clip4 starts");
for(unsigned i=0;i<12;++i)Ui(bool(menu.Tick()),"actual clip4 Update");
s=menu.Snapshot();Ui(s.mode==2&&s.substate7C==2&&bool(menu.Draw(graphics))&&menu.Snapshot().quads==1,"clip4 draws owned resource");
edge(Left);s=menu.Snapshot();Ui(s.substate7C==0&&s.clip==0&&s.frame==0&&s.mode==1
  &&s.row==4&&s.generation==owner.generation&&SameLease(s.lease,owner.lease),"Back reopens main render on same generation");
for(unsigned i=0;i<3;++i)Ui(bool(menu.Tick()),"main clip Update after Back");
edge(Cross);Ui(menu.Snapshot().clip==1&&menu.Snapshot().state78==2,"fresh parent Cancel uses actual closing clip");
for(unsigned i=0;i<4;++i)Ui(bool(menu.Tick()),"closing Update");
Ui(menu.Snapshot().state78==3&&menu.Snapshot().closed==1&&menu.Snapshot().result==-1,"actual owned close/result");
menu.Close();Ui(!menu.Modal()&&!menu.Input(Frame(lease,++seq,Right)),"retired UI rejects input");
}
} // namespace dokodemo::main_menu_tests
inline void GameMainMenuRegressionChecks() {
using namespace dokodemo::main_menu_tests;
LiteralResults();InputLeases();SourceRows();ControlLifecycle();Refusals();Row4OpenBack();SyntheticClips();SyntheticRow4Clip();
}
