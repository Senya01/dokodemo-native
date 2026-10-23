#pragma once
#include "../src/recovered/game_content_native.h"
#include "../src/recovered/game_system_option_native.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
// SOURCE-only public synthetic component checks. Author executed zero.
// No private actor authority or actual Bootstrap owner identity is issued here.
namespace dokodemo::options_confirm_tests {
using namespace recovered;
using U=uint32_t;using Q=uint64_t;using Bytes=std::vector<uint8_t>;
inline void C(bool v,const char* id){if(!v)throw std::runtime_error(std::string("options-confirm ")+id);}
inline void Ok(const GameFsStatus& s,const char* id){if(!s)throw std::runtime_error(std::string("options-confirm ")+id+": "+s.detail);}
inline bool Eq(const GameFsStatus& a,const GameFsStatus& b){return a.code==b.code&&a.native_error==b.native_error&&a.detail==b.detail;}
inline void Put(Bytes& b,U at,U v,U n=4){for(U i=0;i<n;++i)b.at(at+i)=uint8_t(v>>(8*i));}
inline void Float(Bytes& b,U at,float v){U bits;std::memcpy(&bits,&v,4);Put(b,at,bits);}
inline U Bits(float v){U bits;std::memcpy(&bits,&v,4);return bits;}
struct Storage final:PackageStorageLease {
  Bytes bytes,known;bool live=true;Q generation=1;
  explicit Storage(Bytes b):bytes(std::move(b)),known(bytes.size(),1){}
  bool Span(PackageStorageSpan& s,std::string& e)const override{
      if(!live){e="fixture 073";return false;}
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
inline PackageView Font(std::shared_ptr<Storage>* lease=nullptr){constexpr U count=16384,pixels=60+count,stride=8192,palette=pixels+stride;Bytes b(palette+64);
  Put(b,0,0x31544658);Put(b,4,1,2);Put(b,6,1,2);Put(b,8,count);b[12]=1;Put(b,16,60);
  Put(b,20,count,2);Put(b,22,256,2);Put(b,24,1,2);Put(b,28,4);Put(b,32,256,2);Put(b,36,64,2);
  Put(b,40,stride);Put(b,44,pixels);Put(b,48,3);Put(b,52,16);Put(b,56,palette);
  std::fill(b.begin()+60,b.begin()+pixels,uint8_t(1));std::fill(b.begin()+pixels,b.begin()+palette,uint8_t(0x11));Put(b,palette+4,0xFFFFFFFF);return View(std::move(b),lease);
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
  GameFsStatus OpenResource(const std::string& p,PackageView& out)override{
      if(p=="option\\gui\\option\\OPTION.CB2A"){out=View(Option());return {};}
      if(p=="data\\gui\\dialog\\DIALOG.CB2A"){out=View(Dialog());return {};}
      if(p=="option\\gui\\option\\options-test.GIM"||p=="data\\gui\\dialog\\options-test.GIM"){out=View(Gim());return {};}
      return {GameFsCode::NotFound,31,"fixture 003"};
  }
};
inline PackageView Bank(std::shared_ptr<Storage>* lease=nullptr){Bytes b(16);Put(b,0,0x44584753);return View(std::move(b),lease);}
inline GameMainMenuLease Lease(){return {71,11,13,31,19,41};}
inline GameNotebookLease Child(){return {71,11,19,1,13,31,41};}

struct Settings final:GameOptionSettings {
  GameOptionSettingsSnapshot state{true,101,7,0,{{2,3,3}}};
  GameSourceSoundControl sound;std::shared_ptr<Storage> bank;
  Q gui=0,sequence=0,epoch=0;U gain_calls=0,restore_calls=0,fail_stage=99;
  bool fail_gain=false,corrupt_gain=false,right_writable=true,backup_writable=true;
  std::vector<GameOptionWriteReceipt> right,backup;std::vector<U> events;
  Settings(){Ok(sound.Initialize17F0(),"fixture cold");Ok(sound.BindSe5(Bank(&bank)),"fixture SE5");}
  GameOptionSettingsSnapshot OptionSettings()const override{return state;}
  GameSourceSoundSnapshot OptionSoundOwner()const override{return sound.Snapshot();}
  bool OptionRightWritable()const override{return right_writable;}
  bool OptionBackupWritable()const override{return backup_writable;}
  GameFsStatus RestoreOptionSettings(const GameOptionSettingsSnapshot&,const std::array<U,3>&)override{++restore_calls;return {GameFsCode::Unavailable,0,"fixture 096"};}
  GameFsStatus Write(const GameOptionSettingsSnapshot& expected,U stage,U value,GameOptionWriteReceipt& out,bool cancel){
      const U fields[]={1,0,1,2},calls[]={0x351CC,0x34CCC,0x34CD4,0x34CDC},setters[]={0x24B34,0x24B1C,0x24B34,0x24B4C};
      auto& issued=cancel?backup:right;
      if(!GameOptionSnapshotExact(expected,state)||stage!=issued.size()||stage>=(cancel?3u:4u)||state.writes==UINT64_MAX)
          return {GameFsCode::Unavailable,0,"fixture 018"};
      if(!cancel&&stage==fail_stage)return {GameFsCode::IoFailure,61,"fixture 097"};
      GameOptionWriteReceipt r;r.gui_generation=gui;r.input_sequence=sequence;r.input_epoch=epoch;r.stage=stage;
      r.field=cancel?stage:fields[stage];r.call_pc=cancel?0x3526C+stage*8:calls[stage];r.setter=cancel?0x24B1C+stage*0x18:setters[stage];r.value=value;
      r.before=state;state.words[r.field]=value;++state.writes;r.after=state;issued.push_back(r);events.push_back(r.call_pc);out=r;return {};
  }
  GameFsStatus WriteOptionRight(const GameOptionSettingsSnapshot& e,U stage,U v,GameOptionWriteReceipt& r)override{return Write(e,stage,v,r,false);}
  GameFsStatus WriteOptionBackup(const GameOptionSettingsSnapshot& e,U stage,U v,GameOptionWriteReceipt& r)override{return Write(e,stage,v,r,true);}
  GameFsStatus QueryOptionGain(const GameOptionSettingsSnapshot& expected,int32_t value,GameOptionGainObservation& out)override{
      ++gain_calls;events.push_back(0x4AFBC);
      if(!GameOptionSnapshotExact(expected,state)||value!=105||fail_gain)return {GameFsCode::Unavailable,73,"fixture 098"};
      GameOptionGainObservation observation;const auto status=sound.Gain4AFBC(sound.Snapshot(),value,observation.source);if(!status)return status;
      observation.source_id_known=true;observation.source_dispatched=false; // from actual successful call above
      if(corrupt_gain)observation.source.return_pc+=4;out=observation;return {};
  }
};
// Three explicit key times admit main frame30 and choice frames1/2. This is
// synthetic B2A/GIM/FONT ink, never original asset or readability evidence.
inline void Keys(Ui& u,U clip,U kind,U node,const std::array<std::array<float,2>,3>& v){
  const U key=u.Alloc(8),stride=(kind==1||kind==3)?16:12,at=u.Alloc(3*stride);
  Put(u.b,u.clips+clip*52+kind*8,1);Put(u.b,u.clips+clip*52+kind*8+4,key);
  Put(u.b,key,node,2);Put(u.b,key+2,3,2);Put(u.b,key+4,at);
  for(U i=0;i<3;++i){const U p=at+i*stride;
      if(kind==0||kind==4)Put(u.b,p,U(v[i][0]));else{Float(u.b,p,v[i][0]);if(kind==1||kind==3)Float(u.b,p+4,v[i][1]);}
      Put(u.b,p+stride-8,1);Put(u.b,p+stride-4,i==2?(clip?2:30):i);
  }
}
inline Bytes Trc(){Ui u(28,2,2);u.Link(0,0,2);u.Link(2,22,0);u.Link(1,0,24);u.Link(24,26,0);u.Link(26,27,0);
  u.Tracks(0,0,{{2,{{1,0,1,0}}}});u.Pos(0,{{22,{{14,14,0,0}}}});
  Keys(u,0,1,2,{{{{1,1}},{{1,1}},{{160,46}}}});
  u.Tracks(1,0,{{24,{{1,0,1,0}}},{26,{{1,0,1,0}}},{27,{{1,0,1,0}}}});
  Keys(u,1,3,24,{{{{0,0}},{{0,0}},{{72,0}}}});return std::move(u.b);
}
struct PromptSource final:GameResourceSource {
  U opens=0;bool blocked=false;std::shared_ptr<Storage> backing;
  GameFsStatus OpenResource(const std::string& path,PackageView& out)override{
      ++opens;if(blocked)return {GameFsCode::IoFailure,88,"unexpected resource IO"};
      if(path=="data\\gui\\trc\\TRC.CB2A"){out=View(Trc(),&backing);return {};}
      if(path=="data\\gui\\trc\\options-test.GIM"){out=View(Gim());return {};}
      Source base;return base.OpenResource(path,out);
  }
};
inline const std::string& Question(){static const std::string s="\x90\xDD\x92\xE8\x82\xF0\x94\xBD\x89\x66\x82\xB5\x82\xDC\x82\xB7\x82\xA9\x81\x48";return s;}
inline bool Near(float a,float b){return std::abs(a-b)<0.0002f;}
struct PromptRig {
  PromptSource source;GameFontResource font;std::shared_ptr<GameUiResource> resource=std::make_shared<GameUiResource>();
  GameUiInstance clone;std::vector<GameFontDrawRecord> records;std::shared_ptr<Storage> font_backing;GameTrcPrompt prompt;Q seq=0,epoch=91;
  PromptRig():records(1024){std::memset(records.data(),0xA5,records.size()*sizeof(GameFontDrawRecord));Ok(font.Initialize(Font(&font_backing)),"fixture FONT");
      Ok(resource->Load(source,"data\\gui\\trc\\TRC.CB2A","data\\gui\\trc\\"),"fixture TRC");
      C(clone.Clone(resource)&&clone.Bind(0,0,0)&&clone.Bind(1,1,0),"fixture clone");
      C(clone.AddControl(0)&&clone.AddControl(1)&&clone.ShowRoot(0,false),"fixture controls");
      Ok(prompt.Initialize(clone,resource,font,records.data(),1024),"fixture borrow");}
  ~PromptRig(){prompt.Close();}
  GameInputFrame Frame(U b){GameInputFrame f;f.buttons=b;f.focused=true;f.sequence=++seq;f.main_menu_epoch=epoch;return f;}
  void Send(U b){C(prompt.Input(Frame(b)),"prompt current frame");Ok(prompt.Tick(),"prompt input tick");}
  void Open(){source.blocked=true;Ok(prompt.Open(73,epoch),"open borrowed prompt");}
  void Active(){for(U i=0;i<20&&prompt.Snapshot().state864!=1;++i)Ok(prompt.Tick(),"natural expansion");C(prompt.Snapshot().state864==1,"active bounded wait");}
  void No(){Send(0);Send(Down);Send(0);Send(Circle);C(prompt.Snapshot().state864==2&&prompt.Snapshot().result86C==2,"No starts contraction raw2");
      for(U i=0;i<20&&prompt.Snapshot().state864!=3;++i)Ok(prompt.Tick(),"natural contraction");C(prompt.Snapshot().state864==3,"idle bounded wait");}
};
inline void BorrowAndDraw(){PromptRig r;const auto cold=r.prompt.Snapshot();const U io=r.source.opens;
  C(cold.live&&!cold.active&&cold.state864==3&&cold.owner&&cold.generation&&cold.resource_live&&cold.font_live,"B01");
  C(r.resource->InitialNodes().size()==28&&r.resource->RootCount()==2&&r.resource->Clips().size()==2,"B02");
  C(r.resource->Clips()[0].channels[0].count&&r.resource->Clips()[1].channels[0].count,"B03");
  C(r.clone.Node(22)->source_callback_pc==0x481E4,"B04");
  C(!r.prompt.Initialize(r.clone,r.resource,r.font,r.records.data(),1024),"B05");
  GameFontBitmap reference;std::array<GameFontDrawRecord,1024> expected;
  Ok(r.font.RasterizeCp932({Question()},320,reference,expected.data(),1024,0xFF2B1807,23,0),"B06");
  C(reference.glyphs==10&&!reference.pixels.empty()&&std::any_of(reference.pixels.begin(),reference.pixels.end(),[](U p){return p!=0;}),"B07");
  r.Open();auto s=r.prompt.Snapshot();
  C(s.state864==-1&&s.active&&!s.cancel_enabled&&s.mode==0&&s.selection==0&&s.choices==2&&s.result86C==0,"B08");
  C(s.columns==10&&s.rows==2&&s.shape_frame==30&&s.glyphs==10,"B09");
  C(std::memcmp(r.records.data(),expected.data(),10*sizeof(GameFontDrawRecord))==0,"B10");
  const auto* tail=reinterpret_cast<const uint8_t*>(r.records.data()+10);
  C(std::all_of(tail,reinterpret_cast<const uint8_t*>(r.records.data()+1024),[](uint8_t b){return b==0xA5;}),"B11");
  C(!r.prompt.Open(74,r.epoch)&&r.prompt.Snapshot().opens==1,"B12");
  Ok(r.prompt.Tick(),"B13");s=r.prompt.Snapshot();
  C(s.state864==0&&s.progress==0&&r.clone.Node(0)->visible,"B14");
  C(r.clone.Control(0)->frame==30&&r.clone.Control(0)->mode==2&&r.clone.Control(0)->active==1&&r.clone.Control(1)->frame==1,"B15");
  Ok(r.prompt.Tick(),"B16");C(Near(r.prompt.Snapshot().progress,.1f)&&r.prompt.Snapshot().state864==0,"B17");r.Active();
  GameGraphics g;C(g.Initialize({}),"B18");C(g.Clear(0xFF000000),"B19");const auto before=g.Snapshot();
  const auto draw_before=r.prompt.Snapshot();Ok(r.prompt.Draw(g),"B20");s=r.prompt.Snapshot();
  C(s.callbacks==draw_before.callbacks+1&&s.flushes==draw_before.flushes+1&&s.draws==draw_before.draws+1,"B21");
  C(g.Snapshot().mesh_triangles>before.mesh_triangles&&g.Snapshot().mesh_pixels>before.mesh_pixels,"B22");
  C(Near(r.clone.Node(0)->position[0],-94)&&Near(r.clone.Node(0)->position[1],-37)&&Near(r.clone.Node(1)->position[1],13),"B23");
  C(Near(r.clone.Node(2)->scale[0],160)&&Near(r.clone.Node(2)->scale[1],46),"B24");
  C(g.SwapBuffers(),"B25");GameGraphicsFrame image;C(g.CopyFront(image),"B26");
  C(std::any_of(image.pixels.begin(),image.pixels.end(),[](U p){return p!=0xFF000000;}),"B27");
  r.No();s=r.prompt.Snapshot();C(!s.active&&s.returns==1&&s.no_choices==1&&!r.clone.Node(0)->visible,"B28");
  C(r.clone.Control(0)->active==0&&r.clone.Control(0)->mode==0,"B29");
  const auto stats=g.Snapshot();Ok(r.prompt.Draw(g),"B30");C(g.Snapshot().mesh_triangles==stats.mesh_triangles&&r.prompt.Snapshot().draws==s.draws,"B31");
  C(r.source.opens==io&&s.owner==cold.owner&&s.generation==cold.generation&&s.resource_live&&s.font_live,"B32");
  ++r.epoch;Ok(r.prompt.Open(73,r.epoch),"B33");r.Active();r.Send(Circle);
  C(r.prompt.Snapshot().state864==1&&!r.prompt.Snapshot().failed&&r.prompt.Snapshot().result86C==0,"B34");
  r.No();C(r.prompt.Snapshot().opens==2&&r.prompt.Snapshot().returns==2&&r.prompt.Snapshot().invocation==2,"B35");
  r.prompt.Close();C(!r.prompt.Snapshot().live&&!r.prompt.Snapshot().resource_live&&!r.prompt.Snapshot().font_live,"B36");
  C(!r.prompt.Open(73,r.epoch)&&r.resource->Image().Describe().readable&&r.font.Snapshot().ready&&r.clone.Node(27),"B37");
  r.records.clear();r.records.shrink_to_fit();const auto retired=r.prompt.Tick();
  C(!retired&&retired.code==GameFsCode::Unavailable&&r.prompt.Snapshot().returns==2,"B38");
}
inline void PromptInput(){PromptRig r;r.Open();r.Active();r.Send(0);r.Send(Down|Up|Circle);
  C(r.prompt.Snapshot().selection==1&&r.prompt.Snapshot().state864==1&&r.prompt.Snapshot().result86C==0,"I01");
  r.Send(0);r.Send(Cross);C(r.prompt.Snapshot().disabled_cross==1&&r.prompt.Snapshot().state864==1&&r.prompt.Snapshot().result86C==0,"I02");
  r.Send(0);r.Send(Down);C(r.prompt.Snapshot().selection==0,"I03");
  r.Send(0);r.Send(Up);C(r.prompt.Snapshot().selection==1,"I04");
  r.Send(0);r.Send(Down);for(U i=0;i<10;++i)r.Send(Down);
  C(r.prompt.Snapshot().selection==1,"I05");for(U i=0;i<4;++i)r.Send(Down);
  C(r.prompt.Snapshot().selection==1,"I06");
  r.Send(0);r.Send(Up);for(U i=0;i<14;++i)r.Send(Up);C(r.prompt.Snapshot().selection==0,"I07");
  r.Send(0);auto f=r.Frame(0);C(r.prompt.Input(f),"I08");C(!r.prompt.Input(f),"I09");
  auto wrong=r.Frame(Down);--wrong.main_menu_epoch;C(!r.prompt.Input(wrong),"I10");
  C(!r.prompt.SetInputEpoch(r.epoch)&&r.prompt.SetInputEpoch(++r.epoch),"I11");
  r.Send(Circle);C(!r.prompt.Snapshot().failed&&r.prompt.Snapshot().state864==1,"I12");
  r.Send(0);r.Send(Down);r.Send(0);r.Send(Circle|Cross);
  C(r.prompt.Snapshot().result86C==2&&r.prompt.Snapshot().state864==2&&r.prompt.Snapshot().disabled_cross==1,"I13");
  const auto s=r.prompt.Snapshot();Ok(r.prompt.Tick(),"I14");C(r.prompt.Snapshot().progress<s.progress&&r.prompt.Snapshot().state864==2,"I15");
}
inline void YesAndRetirement(){
  {PromptRig r;r.Open();r.Active();r.Send(0);C(r.prompt.Input(r.Frame(Circle)),"Y01");const auto before=r.prompt.Snapshot();
      const auto failure=r.prompt.Tick();auto s=r.prompt.Snapshot();
      C(!failure&&failure.code==GameFsCode::Unavailable&&failure.detail.find("3534C")!=std::string::npos,"Y02");
      C(s.failed&&s.yes_refusals==1&&s.active&&s.state864==1&&s.result86C==0&&s.no_choices==0&&s.returns==0,"Y03");
      C(s.owner==before.owner&&s.generation==before.generation&&s.opens==before.opens,"Y04");
      GameGraphics g;C(g.Initialize({}),"Y05");const auto io=r.source.opens;
      C(Eq(failure,r.prompt.Tick())&&Eq(failure,r.prompt.Draw(g))&&Eq(failure,r.prompt.Open(73,r.epoch)),"Y06");
      C(!r.prompt.Input(r.Frame(Down))&&r.prompt.Snapshot().updates==s.updates&&r.prompt.Snapshot().yes_refusals==1&&r.source.opens==io,"Y07");}
  for(U cause=0;cause<6;++cause){PromptRig r;r.Open();r.Active();
      if(cause==0)r.source.backing->live=false;else if(cause==1)++r.source.backing->generation;else if(cause==2)r.font.Close();else if(cause==3)r.clone.Close();else if(cause==4)r.font_backing->live=false;else ++r.font_backing->generation;
      const auto failure=r.prompt.Tick();const auto s=r.prompt.Snapshot();
      C(!failure&&failure.code==GameFsCode::Unavailable&&s.failed&&s.result86C==0&&s.returns==0,"Y08");
      C(Eq(failure,r.prompt.Tick())&&r.prompt.Snapshot().updates==s.updates,"Y09");}
  {PromptRig r;r.Open();r.Active();r.prompt.Abort(99);C(r.prompt.Snapshot().active,"Y10");
      r.prompt.Abort(73);C(!r.prompt.Snapshot().active&&r.prompt.Snapshot().returns==0&&r.prompt.Snapshot().result86C==0,"Y11");}
  {PromptRig r;GameTrcPrompt p;C(!p.Initialize(r.clone,r.resource,r.font,r.records.data(),1023)&&!p.Snapshot().live,"Y12");
      C(!p.Initialize(r.clone,r.resource,r.font,nullptr,1024)&&!p.Snapshot().live,"Y13");}
}
// Settings and GameInputFrame are synthetic public transports. This fixture
// observes actual component boundary counters; it never issues actor authority.
struct ParentRig {
  PromptRig trc;Settings settings;GameTransition fade;GameSystemOptionFrontier front;Q sequence=0;U transported=0;
  explicit ParentRig(bool with_prompt=true){C(fade.Initialize({{240,136}}),"parent transition");Ok(front.Request(Lease(),{{2,3,3}}),"parent request");
      front.SetChildLease(Child());Ok(front.Initialize(trc.source,trc.font,settings,fade,with_prompt?&trc.prompt:nullptr),"parent initialize");trc.source.blocked=true;
      for(U i=0;i<400&&front.Snapshot().phase!=3;++i)Tick();C(front.Snapshot().phase==3,"parent active wait");
      settings.gui=front.Snapshot().generation;Send(0);}
  ~ParentRig(){front.Abort();}
  void Transport(){const auto s=front.Snapshot();if(s.phase==3&&s.input_boundaries>transported){
      C(s.input_boundaries==transported+1&&s.boundary_cause>=1&&s.boundary_cause<=4,"P01");
      C(front.SetInputEpoch(s.input_epoch+1),"P02");transported=s.input_boundaries;settings.epoch=front.Snapshot().input_epoch;}}
  GameInputFrame Frame(U b){GameInputFrame f;f.buttons=b;f.focused=true;f.sequence=++sequence;f.main_menu_epoch=front.Snapshot().input_epoch;
      f.message_owner_generation=71;f.message_context=19;f.returned_owner=13;f.returned_generation=31;return f;}
  GameFsStatus Try(U b){auto f=Frame(b);settings.sequence=f.sequence;C(front.Input(f),"parent accepts current transport");const auto status=front.Tick(fade);if(status)Transport();return status;}
  void Send(U b){Ok(Try(b),"parent input tick");}
  void Tick(){fade.Update();Ok(front.Tick(fade),"parent natural tick");Transport();}
  void Enter(){Send(Circle);const auto s=front.Snapshot();C(s.substate48==3&&s.prompt.state864==-1&&s.prompt_opens==s.prompt.opens,"P03");
      for(U i=0;i<24&&trc.prompt.Snapshot().state864!=1;++i)Tick();C(trc.prompt.Snapshot().state864==1,"P04");}
};
inline void ParentStillEditing(const GameSystemOptionSnapshot& s,Q generation){
  C(s.generation==generation&&s.phase==3&&s.state44==1&&s.live&&s.owner474&&s.visible&&s.request10&&s.loader469==1,"P05");
  C(!s.close_requests&&!s.close_completions&&!s.polls&&!s.poll_completed&&!s.retirement_requests&&!s.retirements&&!s.retired&&!s.woken,"P06");
}
inline void ProbeSourceNo(ParentRig& r){const auto before=r.front.Snapshot();const auto root=r.settings.OptionSettings();const U rc=r.settings.restore_calls,gc=r.settings.gain_calls;
  const auto right=r.settings.right.size(),backup=r.settings.backup.size();
  const auto check=[&](){const auto s=r.front.Snapshot();ParentStillEditing(s,before.generation);
      C(GameOptionSnapshotExact(root,r.settings.OptionSettings())&&GameOptionSnapshotExact(root,s.settings),"P07");
      C(s.current_words==before.current_words&&s.passive_settings==before.passive_settings&&s.selection56==before.selection56&&s.previous50==before.previous50,"P08");
      C(r.settings.right.size()==right&&r.settings.backup.size()==backup&&r.settings.restore_calls==rc&&r.settings.gain_calls==gc,"P09");
      C(GameSourceSoundSnapshotExact(before.sound_owner,s.sound_owner),"P10");};
  const auto step=[&](U b){const auto old=r.front.Snapshot();r.Send(b);const auto s=r.front.Snapshot();
      C(s.after_key_updates==old.after_key_updates+1&&s.message.updates==old.message.updates+1,"P11");check();};
  step(0);step(Down);C(r.trc.clone.Control(1)->frame==2&&r.trc.clone.Node(24)->position[0]==72,"P12");
  step(0);step(Cross);C(r.front.Snapshot().prompt.disabled_cross==before.prompt.disabled_cross+1,"P13");
  step(0);step(Circle);C(r.front.Snapshot().substate48==3&&r.trc.prompt.Snapshot().state864==2,"P14");
  for(U i=0;i<24&&r.front.Snapshot().substate48;++i){const auto old=r.front.Snapshot();r.Tick();const auto s=r.front.Snapshot();
      C(s.after_key_updates==old.after_key_updates+1&&s.message.updates==old.message.updates+1,"P15");check();}
  const auto s=r.front.Snapshot();C(!s.substate48&&s.prompt.result86C==2&&s.prompt.state864==3&&s.prompt_returns==s.prompt_opens,"P16");
  C(s.prompt.owner==before.prompt.owner&&s.prompt.generation==before.prompt.generation&&s.prompt.invocation==before.prompt.invocation,"P17");
  C(s.child_lease.script_generation==before.child_lease.script_generation&&s.child_lease.context==before.child_lease.context&&s.child_lease.available_owner==before.child_lease.available_owner&&s.child_lease.available_generation==before.child_lease.available_generation,"P18");
}
inline void ParentNoAndCross(){
  {ParentRig r;r.Enter();ProbeSourceNo(r);C(r.settings.state.writes==0&&r.front.Snapshot().current_words==std::array<U,3>{{2,3,3}},"P19");
      const auto old=r.front.Snapshot();r.Send(Circle);C(r.front.Snapshot().prompt_opens==old.prompt_opens&&!r.front.Snapshot().substate48,"P20");
      r.Send(0);r.Enter();ProbeSourceNo(r);C(r.front.Snapshot().prompt_opens==2&&r.front.Snapshot().prompt_returns==2,"P21");}
  {ParentRig r;r.Send(Right);const auto changed=r.front.Snapshot();
      C(changed.right_writes==4&&changed.settings.writes==4&&changed.current_words==std::array<U,3>{{3,3,3}}&&changed.passive_settings==std::array<U,3>{{2,3,3}},"P22");
      C(r.settings.gain_calls==1&&changed.gain_returned&&GameSourceSoundGainValid(changed.gain.source)&&changed.gain.source.value==105,"P23");
      r.Send(0);r.Enter();ProbeSourceNo(r);C(r.settings.state.writes==4,"P24");C(r.front.Snapshot().source_ui_sound_intents==3,"P33");
      r.Send(Cross);C(r.front.Snapshot().phase==3&&r.settings.state.writes==4,"P25");r.Send(0);r.Send(Cross);
      auto s=r.front.Snapshot();C(s.phase==4&&s.state44==2&&s.restore_writes==3&&s.settings.writes==7&&r.settings.backup.size()==3,"P26");
      C(r.fade.Snapshot().live&&r.fade.Snapshot().state==3,"P27");
      C(s.current_words==std::array<U,3>{{3,3,3}}&&s.settings.words==std::array<U,3>{{2,3,3}}&&s.after_key_updates>changed.after_key_updates,"P28");
      for(U i=0;i<3;++i)C(GameOptionBackupReceiptValid(s.restore_receipts[i])&&GameOptionReceiptExact(s.restore_receipts[i],r.settings.backup[i]),"P29");
      for(U i=0;i<60&&r.front.Snapshot().phase!=6;++i)r.Tick();s=r.front.Snapshot();
      C(s.phase==6&&s.polls==1&&s.poll_completed&&s.retirement_pending&&!s.retirements&&s.live&&!s.woken,"P30");
      r.Tick();s=r.front.Snapshot();C(s.phase==7&&s.retirements==1&&s.retired&&r.front.ClosedAndRetired()&&!s.live&&!s.woken,"P31");
      const auto calls=r.settings.events.size();r.Tick();C(r.front.Snapshot().retirements==1&&r.front.Snapshot().polls==1&&r.settings.events.size()==calls,"P32");}
}
inline void ParentRefusals(){
  {ParentRig r;const auto old=r.front.Snapshot();r.trc.prompt.Close();
      Ok(r.trc.prompt.Initialize(r.trc.clone,r.trc.resource,r.trc.font,r.trc.records.data(),1024),"N08");
      C(!r.front.Input(r.Frame(Cross))&&!r.front.Snapshot().failed,"N09");
      const auto failure=r.front.Tick(r.fade);const auto s=r.front.Snapshot();
      C(!failure&&failure.code==GameFsCode::Unavailable&&s.failed&&s.prompt.owner!=old.prompt_bound_owner&&s.prompt_bound_owner==old.prompt_bound_owner&&s.prompt_bound_generation==old.prompt_bound_generation,"N10");
      C(!s.close_requests&&!s.polls&&!s.woken&&GameOptionSnapshotExact(s.settings,old.settings)&&s.after_key_updates==old.after_key_updates&&Eq(failure,r.front.Tick(r.fade)),"N11");}
  {ParentRig r(false);const auto old=r.front.Snapshot();const auto failure=r.Try(Circle);const auto s=r.front.Snapshot();
      C(!failure&&failure.code==GameFsCode::Unavailable&&s.failed&&s.source_consumer==0x352BC&&s.unsupported==old.unsupported+1,"N01");
      C(!s.substate48&&s.source_ui_sound_intents==old.source_ui_sound_intents&&s.after_key_updates==old.after_key_updates&&s.message.updates==old.message.updates,"N02");
      C(GameOptionSnapshotExact(s.settings,old.settings)&&r.settings.events.empty()&&!s.close_requests&&!s.polls,"N03");
      C(Eq(failure,r.front.Tick(r.fade))&&r.front.Snapshot().unsupported==s.unsupported,"N04");}
  {ParentRig r;r.Enter();r.Send(0);const auto old=r.front.Snapshot();const auto failure=r.Try(Circle);const auto s=r.front.Snapshot();
      C(!failure&&failure.code==GameFsCode::Unavailable&&s.failed&&s.prompt.failed&&s.prompt.yes_refusals==1&&s.prompt.result86C==0&&s.prompt.state864==1,"N05");
      C(GameOptionSnapshotExact(s.settings,old.settings)&&r.settings.events.empty()&&s.close_requests==0&&s.polls==0&&s.after_key_updates==old.after_key_updates,"N06");
      C(Eq(failure,r.front.Tick(r.fade))&&r.front.Snapshot().prompt.yes_refusals==1,"N07");}
}
inline void ParentInputRevocation(){for(U loss=0;loss<2;++loss)for(U queued:{U(Circle),U(Down)}){
  ParentRig r;r.Enter();r.Send(0);auto pending=r.Frame(queued);C(r.front.Input(pending),"R01");
  auto revoke=r.Frame(0);revoke.focused=loss!=0;revoke.overflow_resync=loss!=0;
  C(!r.front.Input(revoke),"R02");r.Tick();auto s=r.front.Snapshot();
  C(!s.failed&&s.prompt.state864==1&&s.prompt.selection==0&&s.prompt.result86C==0&&!s.prompt.no_choices&&!s.prompt.yes_refusals,"R03");
  ParentStillEditing(s,s.generation);r.Send(queued);s=r.front.Snapshot();
  C(!s.failed&&s.prompt.selection==0&&s.prompt.state864==1&&s.prompt.result86C==0,"R04");
  ProbeSourceNo(r);C(r.settings.state.writes==0&&!r.front.Snapshot().close_requests,"R05");
}}
} // namespace dokodemo::options_confirm_tests
inline void GameOptionsConfirmRegressionChecks(){using namespace dokodemo::options_confirm_tests;
  BorrowAndDraw();PromptInput();YesAndRetirement();ParentNoAndCross();ParentRefusals();ParentInputRevocation();
}
