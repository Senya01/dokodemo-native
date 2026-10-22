#pragma once
// Independent synthetic component tests, authored without execution.
// Accepted parent supplied: a1858e2552636ee58faf16e9b8cd6bb5bc795f6a.
// API: explicit CORE_RELEASE.v1, then authorized CORE_RELEASE.v2 overlay.
// ROOT alone includes/calls/builds/runs this entry. No original assets required.
#include "game_notebook_regressions.h"
#include "core_hash.h"
#include <array>
#include <cstring>

namespace dokodemo::notebook_self_tests {
using namespace recovered;
namespace B=notebook_tests; // Only fixture/value/UI/input/passive comparison primitives.
using U=uint32_t;using Q=uint64_t;using V=script::Value;using T=script::Table;
using Image=GameNotebookSelfImage;using Projection=GameNotebookSelfProjection;
using S=GameNotebookSnapshot;using Rows=GameNotebookRows;using Lease=GameNotebookLease;
inline constexpr auto Project=GameProjectNotebookSelf;
inline constexpr auto Text=GameNotebookSelfCallbackText;
inline constexpr auto Emits=GameNotebookSelfCallbackEmits;
inline void Check(bool ok,const char* id){if(!ok)throw std::runtime_error(std::string("NOTE self ")+id);}
template<class Fn>inline void Throws(Fn fn,const char* id){bool threw=false;try{fn();}catch(const std::exception&){threw=true;}Check(threw,id);}
inline bool ValueEqual(const V& a,const V& b){return a.kind==b.kind&&a.word==b.word&&a.table==b.table&&a.index==b.index&&a.cp932==b.cp932;}
inline bool FieldsEqual(const Image& a,const Image& b){
 if(a.fields.size()!=b.fields.size()||a.owner.sha!=b.owner.sha||a.code_sha!=b.code_sha)return false;
 for(size_t i=0;i<a.fields.size();++i)if(!ValueEqual(a.fields[i],b.fields[i]))return false;return true;
}
inline bool ProjectionEqual(const Projection& a,const Projection& b){return a.text==b.text&&a.text_present==b.text_present
 &&a.selector90==b.selector90&&a.year94==b.year94&&a.month98==b.month98&&a.day9C==b.day9C
 &&a.valueA0==b.valueA0&&a.valueA4==b.valueA4&&a.valueA8==b.valueA8;}
inline bool LeaseEqual(const Lease& a,const Lease& b){return a.script_generation==b.script_generation&&a.run_id==b.run_id
 &&a.context==b.context&&a.parent_generation==b.parent_generation&&a.available_owner==b.available_owner
 &&a.available_generation==b.available_generation&&a.input_epoch==b.input_epoch;}
inline void SameParent(const S& a,const S& b){Check(a.generation==b.generation&&LeaseEqual(a.lease,b.lease)
 &&a.rows.sha==b.rows.sha&&a.rows.slot0.sha==b.rows.slot0.sha&&b.live&&b.owner46C&&b.request9
 &&b.phase==3&&b.state23C==1&&b.loader469==1&&a.close_requests==b.close_requests&&a.polls==b.polls
 &&a.retirements==b.retirements,"S001");}
inline V Owned(std::string text,U tag=6){auto v=B::Text(std::move(text));v.word=tag<<28;return v;}

struct Data {
 script::Program program=B::Program();script::Vm vm;
 V root=B::Ref(T::Global,4),row=B::Ref(T::Global,128),friend_row=B::Ref(T::Global,256);
 std::array<V,30> expected{};U accesses=0;Q generation=71;
 Data():vm(program){
 Check(vm.start().status==script::Status::Running,"S002");(void)vm.context_value();
 expected.fill(B::Nil());expected[0]=V::integer(2);expected[1]=Owned("\x82\xA0" "A");
 expected[9]=V::integer(3);expected[21]=Owned("\x82\xA2" "B",5);expected[22]=V::integer(-1);
 expected[23]=V::integer(0x01000229);expected[24]=V::integer(0);expected[25]=Owned("C");
 expected[26]=Owned("D",5);expected[29]=V::integer(77);
 vm.store_reference(root,B::Header(100));vm.store_reference(B::Ref(T::Global,5),row);
 vm.store_reference(friend_row,B::Header(1));vm.store_reference(B::Ref(T::Global,257),B::Nil());
 //99 synthetic empty slots alias one stable fixture row; no real actor assertion.
 for(U i=1;i<100;++i)vm.store_reference(B::Ref(T::Global,uint16_t(5+i)),friend_row);
 Install(row,30);vm.access_observer([this](const V&,bool){++accesses;});
 }
 void Install(V at,U cells){vm.store_reference(at,B::Header(cells));
 for(U i=0;i<cells;++i){auto cell=at;cell.index=uint16_t(at.index+i+1);
 vm.store_reference(cell,i<expected.size()?expected[i]:B::Nil());}}
 void Store(U field,V value){auto cell=row;cell.index=uint16_t(row.index+field+1);vm.store_reference(cell,std::move(value));}
 Image Capture(Q gen=0){const auto before=vm.current_witness();const auto hits=accesses;
 auto image=GameObserveNotebookSelf(vm,gen?gen:generation,root);
 Check(B::SameVm(before,vm.current_witness())&&accesses==hits,"S003");return image;}
 Rows Observe(){const auto before=vm.current_witness();const auto hits=accesses;auto rows=GameObserveNotebookRows(vm,generation,root);
 Check(B::SameVm(before,vm.current_witness())&&hits==accesses,"S004");return rows;}
};
inline std::vector<uint8_t> ExpectedSlotBytes(const Data& d){
 std::vector<uint8_t> bytes;const auto n=[&](U x){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(x>>(i*8)));};
 const auto value=[&](const V& v){n(U(v.kind));n(v.word);n(U(v.table));n(v.index);n(U(v.cp932.size()));bytes.insert(bytes.end(),v.cp932.begin(),v.cp932.end());};
 value(B::Ref(T::Global,4));value(B::Header(100));value(B::Ref(T::Global,128));value(B::Header(30));
 for(const auto& v:d.expected)value(v);return bytes;
}

// Synthetic219-node NOTE. root0->76->ordinary; root6->211..218, root8 marker.
inline PackageView NoteImage(U ordinary=92,bool bad_future=false){
 constexpr U nodes=40,clips=1792,keys=2312,values=2824,name=3464;
 std::vector<uint8_t> b(3504);B::Put(b,0,0x30413242);B::Put(b,4,1);B::Put(b,8,32);
 B::Put(b,12,219);B::Put(b,16,nodes);B::Put(b,20,10);B::Put(b,24,clips);B::Put(b,28,10);B::Put(b,36,name);
 const char stem[]="self-synthetic.GIM";std::copy(stem,stem+sizeof(stem),b.begin()+name);
 B::Put(b,nodes+2,76,2);B::Put(b,nodes+76*8+2,ordinary,2);B::Put(b,nodes+6*8+2,211,2);
 for(U i=211;i<218;++i)B::Put(b,nodes+i*8,i+1,2);
 U next_key=keys,next_value=values;
 const auto track=[&](U clip,U channel,U target,U a,U c,U count=1){
 const auto at=clips+clip*52+channel*8;B::Put(b,at,1);B::Put(b,at+4,next_key);
 B::Put(b,next_key,target,2);B::Put(b,next_key+2,count,2);B::Put(b,next_key+4,next_value);
 B::Put(b,next_value,a);B::Put(b,next_value+4,c);next_key+=8;next_value+=channel==1||channel==3?16:12;
 };
 for(U i=0;i<10;++i)B::Put(b,clips+i*52+48,i==0?3:32);
 track(0,0,0,1,0);track(6,0,6,1,0);track(8,0,8,1,0);
 B::Put(b,clips+6*52+24,8);B::Put(b,clips+6*52+28,next_key);
 for(U i=0;i<8;++i){B::Put(b,next_key,211+i,2);B::Put(b,next_key+2,1,2);B::Put(b,next_key+4,next_value);
 const float x=-160.0f+float(i)*35,y=-90.0f+float(i)*20;U wx=0,wy=0;
 std::memcpy(&wx,&x,4);std::memcpy(&wy,&y,4);B::Put(b,next_value,wx);B::Put(b,next_value+4,wy);
 next_key+=8;next_value+=16;
 }
 if(bad_future){
 const auto at=clips+8*52+16;B::Put(b,at,1);B::Put(b,at+4,next_key);
 B::Put(b,next_key,8,2);B::Put(b,next_key+2,2,2);B::Put(b,next_key+4,next_value);
 B::Put(b,next_value,0);B::Put(b,next_value+4,0);B::Put(b,next_value+8,0);
 B::Put(b,next_value+12,0);B::Put(b,next_value+16,3);B::Put(b,next_value+20,2);
 }
 return B::View(std::move(b));
}
struct Resources final:GameResourceSource {
 U ordinary=92;bool bad_future=false;U opens=0;
 GameFsStatus OpenResource(const std::string& p,PackageView& out)override{
 if(p=="mtomo\\gui\\tomodachi_note\\NOTE.CB2A"){++opens;out=NoteImage(ordinary,bad_future);return {};}
 if(p=="mtomo\\gui\\tomodachi_note\\self-synthetic.GIM"){out=B::Gim();return {};}
 return {GameFsCode::NotFound,0,"unrecognized synthetic self resource"};
 }
};
// Type1: all possible accepted16-bit glyph indexes>=32, no type0 plane fallback.
inline PackageView FontImage(U glyph_count=65504){
 constexpr U width_at=60,pages=4,stride=8192;const U texture_at=width_at+glyph_count,palette_at=texture_at+pages*stride;
 std::vector<uint8_t> b(palette_at+64);B::Put(b,0,0x31544658);B::Put(b,4,1,2);B::Put(b,6,1,2);
 B::Put(b,8,glyph_count);b[12]=1;B::Put(b,16,width_at);B::Put(b,20,16384,2);B::Put(b,22,256,2);
 B::Put(b,24,pages,2);B::Put(b,28,4);B::Put(b,32,256,2);B::Put(b,36,64,2);B::Put(b,40,stride);
 B::Put(b,44,texture_at);B::Put(b,48,3);B::Put(b,52,16);B::Put(b,56,palette_at);
 std::fill(b.begin()+width_at,b.begin()+texture_at,uint8_t(1));std::fill(b.begin()+texture_at,b.begin()+palette_at,uint8_t(0x11));
 B::Put(b,palette_at+4,0xFFFFFFFF);return B::View(std::move(b));
}
struct Case {
 Resources source;GameTransition fade;GameNotebook note;RngNativeOwner rng;Lease lease{};Q seq=0;
 explicit Case(const Rows& rows,U ordinary=92,bool bad_future=false){
 source.ordinary=ordinary;source.bad_future=bad_future;lease=B::Lease(rows);
 rng.ImportCurrentState64(1,991,"synthetic self test fixed seed; no live scenario");
 Check(fade.Initialize({{0,0}})&&bool(note.Request(rows,lease,0)),"S005");
 Tick();Ready();
 }
 S Get()const{return note.Snapshot();}
 void Tick(){Check(bool(note.Tick(source,fade,&rng)),"TICK");}
 void Ready(){for(U i=0;Get().phase==2&&i<96;++i){fade.Update();Tick();}Check(Get().phase==3&&Get().state23C==1,"READY");}
 GameInputFrame Packet(U keys=0){return B::Frame(lease,++seq,keys);}
 void Step(U keys=0){Check(note.Input(Packet(keys)),"S006");Tick();}
 void Press(U keys){Step();Step(keys);}
 void Boundary(){const auto old=Get().input_epoch;Check(note.SetInputEpoch(old+1),"S007");lease.input_epoch=old+1;}
 void Pending(){const auto old=Get();const auto seed=rng.CurrentSeed64();Press(Triangle);const auto s=Get();
 Check(s.state26C==2&&s.requested274==1&&s.ordinary270==3&&s.state240==0
 &&s.preview_requests==old.preview_requests+1&&s.producer_returns==old.producer_returns&&s.rng_calls==old.rng_calls
 &&s.preview_opens==old.preview_opens&&s.input_boundaries==old.input_boundaries&&rng.CurrentSeed64()==seed,"S008");SameParent(old,s);}
 void Enter(const Image& image){Pending();Check(bool(note.PrepareSelf(image)),"PREPARE");Tick();Boundary();}
 void Back(){Press(Cross);Check(Get().ordinary270==2&&Get().state26C==3,"S009");Tick();Boundary();Check(Get().state26C==1&&Get().ordinary270==3,"S010");}
 void Poll(){for(U i=0;Get().phase==4&&i<96;++i){fade.Update();Tick();}Check(Get().phase==6,"POLL");}
};
inline Q NextSeed(Q seed){return seed*UINT64_C(0x5851f42d4c957f2d)+1;}
inline void NoProduction(const S& s,Q seed,const Case& c){Check(s.producer_returns==0&&s.rng_calls==0&&s.preview_opens==0
 &&s.preview_generation==0&&s.ordinary270==3&&s.phase==3&&s.state23C==1&&s.visible&&s.owner46C&&s.request9
 &&s.close_requests==0&&s.polls==0&&s.retirements==0&&c.rng.CurrentSeed64()==seed,"S011");}
inline void CheckDate(const Image& base,U packed,int year,int month,int day,int index,const char* id){
 auto image=base;image.fields[23]=B::Nil();image.fields[23].word=0x20000000u|(packed&0x0FFFFFFFu);const auto p=Project(image);
 Check(p.year94==year&&p.month98==month&&p.day9C==day&&p.valueA8==index,id);
}

// Family1: independent bytes/source-literal expectations; pure unsupported cuts.
inline void PassiveAndText(){
 Data d;const auto image=d.Capture(),again=d.Capture();const auto rows=d.Observe();const auto before=d.vm.current_witness();
 Check(image.owner.valid&&image.owner.cells==30&&image.owner.row_header==0x9000001E&&image.owner.row_index==128
 &&image.owner.index==0&&image.owner.script_generation==71&&image.owner.run_id==before.run_id
 &&image.code_sha==before.code_sha256&&FieldsEqual(image,again)&&rows.records==100&&rows.cells==99&&rows.populated==0,"S012");
 for(U i=0;i<30;++i)Check(ValueEqual(image.fields[i],d.expected[i]),"S013");
 const auto bytes=ExpectedSlotBytes(d);Check(image.owner.sha==core::sha256(bytes.data(),bytes.size()),"S014");
 auto p=Project(image);Check(p.selector90==-1&&p.year94==2000&&p.month98==2&&p.day9C==29&&p.valueA0==3&&p.valueA4==0&&p.valueA8==11,"S015");
 const std::array<std::string,8> expected={{"\x82\xA0" "A","\x82\xA2" "B","\x82\x60\x8C\x5E","\x82\x60\x8C\x5E",
 "2000\x94\x4E" " 2\x8C\x8E" "29\x93\xFA","\x82\xA4\x82\xA8\x8D\xC0","C","D"}};
 for(U i=0;i<8;++i)Check(Text(p,211+i)==expected[i]&&Emits(p,211+i),"S016");
 Check(Text(p,213)==Text(p,214),"S017");
 CheckDate(image,0x10000229,1900,2,29,11,"S018");
 CheckDate(image,0x09990229,-1,2,29,11,"S019");
 auto yearless=image;yearless.fields[23]=V::integer(0x09990203);p=Project(yearless);
 Check(Text(p,215)==" 2\x8C\x8E" " 3\x93\xFA","S020");
 CheckDate(image,0x01000A0F,2000,10,15,6,"S021");
 struct Edge{U x;int expected;};
 const Edge edges[]={{0,-1},{0x100,-1},{0x101,9},{0x11F,9},{0x120,10},{0x218,10},{0x219,11},
 {0x229,11},{0x320,11},{0x321,0},{0x41F,0},{0x420,1},{0x520,1},{0x521,2},{0x621,2},{0x622,3},
 {0x722,3},{0x723,4},{0x822,4},{0x823,5},{0x922,5},{0x923,6},{0x1023,6},{0x1024,7},
 {0x1121,7},{0x1122,8},{0x1221,8},{0x1222,9},{0x1231,9},{0x1232,-1},{0xFFFF,-1}};
 for(const auto& e:edges){auto a=image;a.fields[23]=V::integer(int32_t(e.x));Check(Project(a).valueA8==e.expected,"S022");}
 const std::array<std::string,4> a0={{"\x81\x48","\x92\x6A","\x8F\x97","\x82\x60\x8C\x5E"}};
 const std::array<std::string,5> a4={{"\x82\x60\x8C\x5E","\x82\x61\x8C\x5E","AB\x8C\x5E","\x82\x6E\x8C\x5E","\x81\x48"}};
 const std::array<std::string,13> a8={{"\x82\xA8\x82\xD0\x82\xC2\x82\xB6\x8D\xC0","\x82\xA8\x82\xA4\x82\xB5\x8D\xC0",
 "\x82\xD3\x82\xBD\x82\xB2\x8D\xC0","\x82\xA9\x82\xC9\x8D\xC0","\x82\xB5\x82\xB5\x8D\xC0","\x82\xA8\x82\xC6\x82\xDF\x8D\xC0",
 "\x82\xC4\x82\xF1\x82\xD1\x82\xF1\x8D\xC0","\x82\xB3\x82\xBB\x82\xE8\x8D\xC0","\x82\xA2\x82\xC4\x8D\xC0","\x82\xE2\x82\xAC\x8D\xC0",
 "\x82\xDD\x82\xB8\x82\xAA\x82\xDF\x8D\xC0","\x82\xA4\x82\xA8\x8D\xC0","\x81\x48"}};
 for(int i=-1;i<=13;++i){p.valueA0=p.valueA4=p.valueA8=i;
 Check(Text(p,213)==(i>=0&&i<4?a0[size_t(i)]:std::string{})&&Emits(p,213)==(i>=0&&i<4),"S023");
 Check(Text(p,214)==(i>=0&&i<5?a4[size_t(i)]:std::string{})&&Emits(p,214)==(i>=0&&i<5),"S024");
 Check(Text(p,216)==(i>=0&&i<13?a8[size_t(i)]:std::string{})&&Emits(p,216)==(i>=0&&i<13),"S025");}
 p.year94=7;p.month98=2;p.day9C=3;Check(Text(p,215)=="   7\x94\x4E" " 2\x8C\x8E" " 3\x93\xFA","S026");
 p.month98=-1;Check(!Emits(p,215)&&Text(p,215).empty(),"S027");
 p.month98=0;p.day9C=-1;Check(!Emits(p,215),"S028");
 auto blank=image;for(U i:{1u,21u,25u,26u,9u,22u,23u,24u})blank.fields[i]=B::Nil();
 auto absent=Project(blank);Check(absent.selector90==-1&&absent.year94==-1&&absent.valueA0==-1,"S029");
 for(U node=211;node<=218;++node)Check(!Emits(absent,node)&&Text(absent,node).empty(),"S030");
 for(U i:{1u,21u,25u,26u})blank.fields[i]=Owned("");const auto empty=Project(blank);
 for(U node:{211u,212u,217u,218u})Check(Emits(empty,node)&&Text(empty,node).empty(),"S031");
 for(U field:{9u,22u,23u,24u})for(U tag:{1u,3u,4u,5u,6u,9u,15u}){
 auto bad=image;bad.fields[field]=B::Nil();bad.fields[field].word=tag<<28;Throws([&]{(void)Project(bad);},"S032");}
 for(U field:{1u,21u,25u,26u}){
 for(U tag:{2u,4u,7u}){auto bad=image;bad.fields[field]=Owned("x",tag);Throws([&]{(void)Project(bad);},"S033");}
 for(auto text:{std::string("x\0y",3),std::string(1024,'x')}){auto bad=image;bad.fields[field]=Owned(text);Throws([&]{(void)Project(bad);},"S034");}
 }
 for(U field:{9u,22u}){auto a=image;a.fields[field]=V::integer(-2);auto q=Project(a);Check(field==9?q.valueA0==2:q.selector90==-2,"S035");}
 Throws([&]{(void)Text(p,219);},"S036");
 Throws([&]{(void)Emits(p,210);},"S037");
 Check(B::SameVm(before,d.vm.current_witness())&&FieldsEqual(image,again),"S038");
}

// Family2: real component Open/Triangle/Prepare/nextTick/Draw/Back/close/drain.
inline void PublicLifecycle(){
 Data d;const auto image=d.Capture();Case c(d.Observe());GameGraphics g;GameFontResource font;
 Check(g.Initialize({})&&bool(font.Initialize(FontImage())),"S039");
 auto initial=c.Get();Check(initial.clip==0&&initial.state26C==1&&initial.ordinary270==3&&initial.text_capacity==1024,"S040");
 Check(bool(c.note.Draw(g,font)),"S041");initial=c.Get();Check(initial.font_resets==1&&initial.font_flushes==1&&initial.callback_empty_returns==1&&initial.quads==1,"S042");
 c.Pending();const auto ordinary_epoch=c.lease;auto caller=image;Check(bool(c.note.PrepareSelf(caller)),"S043");
 caller.fields[1]=Owned("caller changed after by-value preparation");
 const auto seed=c.rng.CurrentSeed64();c.Tick();auto entered=c.Get();const Q next=NextSeed(seed);const U raw=U(next>>32)&0x7FFFFFFF;
 Check(entered.state26C==3&&entered.ordinary270==1&&entered.producer_returns==1&&entered.rng_calls==1&&entered.preview_opens==1
 &&entered.preview_frame==22&&entered.variant278==raw%3+1&&entered.mode==0&&entered.input_boundaries==initial.input_boundaries+1
 &&entered.rng_raw==raw&&entered.rng_seed_before==seed&&entered.rng_seed_after==next&&c.rng.CurrentSeed64()==next
 &&ProjectionEqual(entered.preview,Project(image))&&entered.preview_record.sha==image.owner.sha,"S044");SameParent(initial,entered);
 c.Boundary();const auto high=c.Get().last_sequence;
 Check(!c.note.Input(B::Frame(ordinary_epoch,++c.seq))&&!c.note.Input(B::Frame(ordinary_epoch,++c.seq,Cross))&&c.Get().last_sequence==high,"S045");
 for(auto member:{&GameInputFrame::message_owner_generation,&GameInputFrame::message_context,&GameInputFrame::returned_owner,&GameInputFrame::returned_generation}){
 auto wrong=c.Packet(Cross);++(wrong.*member);Check(!c.note.Input(wrong)&&c.Get().last_sequence==high,"S046");}
 auto replay=B::Frame(c.lease,high,Cross);Check(!c.note.Input(replay),"S047");
 c.Step(Cross);Check(c.Get().ordinary270==1,"S048");
 for(bool overflow:{false,true}){auto lost=c.Packet();lost.focused=overflow;lost.overflow_resync=overflow;
 if(overflow){GameInputMailbox box;for(U i=0;i<129;++i)box.Push(lost);Check(box.Pop(lost)&&lost.overflow_resync,"S049");}
 Check(!c.note.Input(lost),"S049");c.Step(Cross);Check(c.Get().ordinary270==1,"S050");}
 for(U i=0;i<2;++i){c.Tick();const auto pre=c.Get();Check(g.Clear(0xFF112233)&&bool(c.note.Draw(g,font))&&g.SwapBuffers(),"S051");
 const auto s=c.Get();GameGraphicsFrame pixels;Check(g.CopyFront(pixels)&&pixels.width==480&&pixels.height==272,"S052");
 Check(std::count(pixels.pixels.begin(),pixels.pixels.end(),0xFF552F0Du)>0,"S053");
 Check(s.preview_callback_mask==255&&s.callback_reaches==pre.callback_reaches+8&&s.font_resets==pre.font_resets+8&&s.font_flushes==pre.font_flushes+8
 &&s.preview_emissions==pre.preview_emissions+8&&s.preview_empty_emissions==0&&s.preview_guarded_callbacks==0&&s.preview_glyphs==pre.preview_glyphs+24
 &&s.quads==2&&s.preview_draws==pre.preview_draws+1&&s.text_scale_x==1&&s.text_scale_y==1&&s.text_flush_selector==-1
 &&s.scissor_top==initial.scissor_top&&s.scissor_bottom==initial.scissor_bottom&&s.text_capacity==1024&&s.producer_returns==1&&s.rng_calls==1
 &&c.rng.CurrentSeed64()==next,"S054");SameParent(initial,s);
 }
 Check(c.note.Input(c.Packet())&&c.note.Input(c.Packet(Cross)),"S055");
 auto stale=B::Frame(ordinary_epoch,++c.seq,Cross);Check(!c.note.Input(stale),"S056");c.Tick();
 const auto back=c.Get();Check(back.ordinary270==2&&back.preview_back_requests==1&&back.state26C==3,"S057");SameParent(initial,back);
 const auto preview_epoch=c.lease;c.Tick();const auto returned=c.Get();Check(returned.state26C==1&&returned.ordinary270==3&&returned.preview_returns==1
 &&returned.input_boundaries==entered.input_boundaries+1&&returned.mode==1&&returned.frame==0&&returned.clip==0,"S058");SameParent(initial,returned);
 c.Boundary();Check(!c.note.Input(B::Frame(preview_epoch,++c.seq))&&!c.note.Input(B::Frame(preview_epoch,++c.seq,Cross)),"S059");
 c.Step(Cross);Check(c.Get().phase==3,"S060");c.Step();
 const auto pre_draw=c.Get();Check(bool(c.note.Draw(g,font))&&c.Get().font_flushes==pre_draw.font_flushes+1&&c.Get().quads==1,"S061");
 c.Press(Cross);const auto closing=c.Get();Check(closing.phase==4&&closing.state23C==2&&closing.close_requests==1&&closing.polls==0,"S062");
 c.Poll();const auto polled=c.Get();Check(polled.state23C==3&&polled.polls==1&&polled.close_completions==1&&polled.retirement_pending&&polled.text_owner
 &&polled.owner46C&&polled.live&&!polled.request9&&polled.loader469==0&&polled.retirements==0&&!c.note.ClosedAndRetired(),"S063");
 GameFontResource absent;Check(bool(c.note.Draw(g,absent)),"S064");c.Tick();const auto retired=c.Get();
 Check(retired.phase==7&&retired.retired_generation==retired.generation&&retired.retirements==1&&!retired.live&&!retired.owner46C&&!retired.text_owner
 &&retired.text_capacity==0&&retired.text_used==0&&c.note.ClosedAndRetired()&&!c.note.Modal()&&LeaseEqual(retired.lease,initial.lease),"S065");
}

// Family3: capture identity/physical-shape drift; public hashes are transport.
inline void CaptureRefusals(){
 Data d;const auto image=d.Capture(),frozen=image;const auto old_rows=d.Observe();const auto p=Project(image);
 for(U kind=0;kind<5;++kind){
 if(kind==0)d.Store(29,V::integer(78));
 if(kind==1){auto v=d.expected[1];v.cp932="\x82\xA0" "Z";d.Store(1,v);}
 if(kind==2){d.Install(B::Ref(T::Global,512),30);d.vm.store_reference(B::Ref(T::Global,5),B::Ref(T::Global,512));}
 if(kind==3)d.Install(d.row,31);
 if(kind==4){d.vm.store_reference(B::Ref(T::Global,5),d.row);d.vm.store_reference(d.root,B::Header(99));}
 const auto changed=d.Capture();Check(changed.owner.sha!=image.owner.sha&&ProjectionEqual(Project(image),p)&&FieldsEqual(image,frozen),"S066");
 if(kind==0||kind==1||kind==3)Check(d.Observe().sha==old_rows.sha,"S067");
 Case c(old_rows);c.Pending();const auto seed=c.rng.CurrentSeed64();Check(!c.note.PrepareSelf(changed),"S068");NoProduction(c.Get(),seed,c);
 Check(c.Get().state26C==1&&c.Get().unsupported==1&&c.Get().input_boundaries==1,"S069");
 d.vm.store_reference(d.root,B::Header(100));d.vm.store_reference(B::Ref(T::Global,5),d.row);d.Install(d.row,30);
 }
 auto before=d.vm.current_witness();const auto hits=d.accesses;
 Throws([&]{(void)GameObserveNotebookSelf(d.vm,0,d.root);},"S070");
 Throws([&]{(void)GameObserveNotebookSelf(d.vm,71,B::Nil());},"S071");
 Check(B::SameVm(before,d.vm.current_witness())&&hits==d.accesses,"S072");
 for(auto bad:{B::Ref(T::Frame,0),B::Nil()}){d.vm.store_reference(B::Ref(T::Global,5),bad);Throws([&]{(void)d.Capture();},"S073");}
 d.vm.store_reference(B::Ref(T::Global,5),d.row);
 for(U cells:{0u,65u}){d.vm.store_reference(d.row,B::Header(cells));Throws([&]{(void)d.Capture();},"S074");}d.Install(d.row,30);
 auto bad_text_cell=B::Nil();bad_text_cell.cp932="non-string bytes";d.Store(1,bad_text_cell);Throws([&]{(void)d.Capture();},"S075");d.Install(d.row,30);
 for(U field:{1u,21u,25u,26u}){auto malformed=image;malformed.fields[field]=Owned("x");malformed.fields[field].kind=V::Kind::Scalar;
 Throws([&]{(void)Project(malformed);},"S076");}
 for(U field:{9u,22u,23u,24u}){auto malformed=image;malformed.fields[field].kind=V::Kind::String;
 Throws([&]{(void)Project(malformed);},"S077");}
 for(U cells:{0u,26u}){auto malformed=image;malformed.owner.cells=cells;malformed.fields.resize(cells);Throws([&]{(void)Project(malformed);},"S078");}
 script::Vm foreign(d.program);Throws([&]{(void)GameObserveNotebookSelf(foreign,71,d.root);},"S079");
 auto narrow=image;narrow.fields[0]=B::Nil();Throws([&]{(void)Project(narrow);},"S080");
}

// Family4: v2 recovery, duplicate transaction, no RNG on missing/future UI.
inline void PreparationRefusals(){
 Data d;const auto rows=d.Observe();const auto image=d.Capture();
 for(U which=0;which<8;++which){Case c(rows);c.Pending();const auto seed=c.rng.CurrentSeed64();auto bad=image;
 if(which==0)bad.owner.valid=false;if(which==1)++bad.owner.script_generation;if(which==2)++bad.owner.run_id;
 if(which==3)bad.owner.sha[31]^=1;if(which==4)bad.code_sha[0]^=1;
 if(which==5)bad.fields[22]=Owned("numeric-parser-cut");
 if(which==6)bad.fields[26]=Owned(std::string("a\0b",3));
 if(which==7)bad.fields.pop_back();
 const auto pending=c.Get();Check(!c.note.PrepareSelf(bad),"S081");const auto refused=c.Get();NoProduction(refused,seed,c);
 Check(refused.state26C==1&&refused.unsupported==pending.unsupported+1&&refused.input_boundaries==pending.input_boundaries+1,"S082");
 c.Boundary();c.Step(Cross);Check(c.Get().phase==3,"S083");c.Press(Cross);Check(c.Get().phase==4&&c.Get().close_requests==1&&c.Get().rng_calls==0,"S084");
 }
 {Case c(rows);const auto before=c.Get();Check(!c.note.PrepareSelf(image),"S085");
 Check(c.Get().state26C==before.state26C&&c.Get().unsupported==before.unsupported&&c.Get().input_boundaries==before.input_boundaries,"S086");
 c.Pending();Check(bool(c.note.PrepareSelf(image)),"S087");auto bad=image;bad.owner.sha[0]^=1;
 Check(!c.note.PrepareSelf(bad)&&!c.note.PrepareSelf(image),"S088");
 Check(c.Get().state26C==2&&c.Get().unsupported==0&&c.Get().input_boundaries==0,"S089");c.Tick();
 Check(c.Get().preview_record.sha==image.owner.sha&&c.Get().producer_returns==1&&c.Get().rng_calls==1,"S090");}
 {Case c(rows);c.Step();Check(c.note.Input(c.Packet(Triangle)),"S091");
 Check(bool(c.note.Tick(c.source,c.fade,nullptr)),"S092");NoProduction(c.Get(),1,c);Check(c.Get().state26C==1,"S093");}
 {Case c(rows);c.Pending();Check(bool(c.note.PrepareSelf(image)),"S094");RngNativeOwner unavailable;
 Check(!c.note.Tick(c.source,c.fade,nullptr)&&!c.note.Tick(c.source,c.fade,&unavailable),"S095");NoProduction(c.Get(),1,c);}
 {Case c(rows,92,true);c.Pending();Check(bool(c.note.PrepareSelf(image)),"S096");
 Check(!c.note.Tick(c.source,c.fade,&c.rng),"S097");NoProduction(c.Get(),1,c);
 Check(c.Get().state26C==2&&!c.note.Tick(c.source,c.fade,&c.rng),"S098");NoProduction(c.Get(),1,c);}
}

// Family5: reopen/new generation, callback predicates and borrowed FONT lifetime.
inline void LifetimeAndGuards(){
 Data d;auto image=d.Capture();const auto rows=d.Observe();Case c(rows);c.Enter(image);const auto first=c.Get();const auto seed1=c.rng.CurrentSeed64();c.Back();
 c.Pending();Check(!c.note.Tick(c.source,c.fade,&c.rng)&&c.Get().preview_opens==1&&c.Get().rng_calls==1&&c.rng.CurrentSeed64()==seed1,"S099");
 Check(bool(c.note.PrepareSelf(image)),"S100");c.Tick();c.Boundary();const auto second=c.Get();
 Check(second.generation==first.generation&&second.preview_generation==2&&second.producer_returns==2&&second.rng_calls==2
 &&c.rng.CurrentSeed64()==NextSeed(seed1)&&LeaseEqual(first.lease,second.lease),"S101");
 c.Back();c.Press(Cross);c.Poll();c.Tick();const auto retired=c.Get();++d.generation;auto newrows=d.Observe();auto newimage=d.Capture();
 c.lease=B::Lease(newrows);++c.lease.context;Check(bool(c.note.Request(newrows,c.lease,0)),"S102");c.Tick();c.Ready();
 Check(c.Get().generation==retired.generation+1&&!c.note.ClosedAndRetired()&&c.Get().rng_calls==0,"S103");
 c.Pending();const auto before=c.rng.CurrentSeed64();Check(!c.note.PrepareSelf(image),"S104");
 Check(c.Get().rng_calls==0&&c.rng.CurrentSeed64()==before&&c.Get().state26C==1,"S105");c.Boundary();c.Enter(newimage);
 Check(c.Get().preview_record.script_generation==d.generation&&c.Get().rng_calls==1,"S106");
 const auto live=c.Get();GameGraphics g;GameFontResource absent,font;Check(g.Initialize({}),"S107");
 Check(!c.note.Draw(g,absent)&&c.Get().callback_reaches==live.callback_reaches,"S108");
 Check(bool(font.Initialize(FontImage(96)))&&!c.note.Draw(g,font),"S109");
 Check(c.Get().rng_calls==live.rng_calls&&c.rng.CurrentSeed64()==live.rng_seed_after,"S110");
 Check(bool(font.Initialize(FontImage()))&&bool(c.note.Draw(g,font)),"S111");font.Close();
 Check(!c.note.Draw(g,font)&&c.Get().rng_calls==1,"S112");c.note.Close();
 Check(!c.note.ClosedAndRetired()&&!c.Get().text_owner&&!c.Get().live&&c.Get().polls==retired.polls,"S113");
 {Case wrong(rows,211);GameGraphics g2;GameFontResource f;Check(g2.Initialize({})&&bool(f.Initialize(FontImage())),"S114");
 Check(!wrong.note.Draw(g2,f)&&wrong.Get().callback_reaches==1&&wrong.Get().preview_callback_mask==0&&wrong.Get().font_resets==0,"S115");}
 {
 Data blank;for(U i:{1u,21u,25u,26u,9u,22u,23u,24u})blank.Store(i,B::Nil());
 Case gcase(blank.Observe());gcase.Enter(blank.Capture());GameGraphics gg;GameFontResource f;
 Check(gg.Initialize({})&&bool(f.Initialize(FontImage()))&&bool(gcase.note.Draw(gg,f)),"S116");
 const auto s=gcase.Get();Check(s.preview_callback_mask==255&&s.font_resets==8&&s.font_flushes==8&&s.preview_guarded_callbacks==8
 &&s.preview_emissions==0&&s.preview_glyphs==0,"S117");
 gcase.note.Close();}
 {Data empty;for(U i:{1u,21u,25u,26u})empty.Store(i,Owned(""));for(U i:{9u,22u,23u,24u})empty.Store(i,B::Nil());
 Case ecase(empty.Observe());ecase.Enter(empty.Capture());GameGraphics gg;GameFontResource f;
 Check(gg.Initialize({})&&bool(f.Initialize(FontImage()))&&bool(ecase.note.Draw(gg,f)),"S118");
 const auto s=ecase.Get();Check(s.preview_callback_mask==255&&s.font_resets==8&&s.font_flushes==8&&s.preview_emissions==4
 &&s.preview_empty_emissions==4&&s.preview_guarded_callbacks==4&&s.preview_glyphs==0,"S119");}
}
} // namespace dokodemo::notebook_self_tests

inline void GameNotebookSelfRegressionChecks(){
 using namespace dokodemo::notebook_self_tests;
 PassiveAndText();PublicLifecycle();CaptureRefusals();PreparationRefusals();LifetimeAndGuards();
}
