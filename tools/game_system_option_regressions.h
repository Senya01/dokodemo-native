#pragma once
#include "../src/recovered/game_system_option_native.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace dokodemo::system_option_tests {
using namespace recovered;
using U=uint32_t;using Q=uint64_t;using Bytes=std::vector<uint8_t>;
using L=GameMainMenuLease;using M=GameMainMenuSnapshot;using S=GameSystemOptionSnapshot;
inline void C(bool yes,const char* id){if(!yes)throw std::runtime_error(std::string("system-option ")+id);}
inline L Lease(){return {71,11,13,31,19,41};}
inline bool EqL(const L& a,const L& b){return a.script_generation==b.script_generation&&a.run_id==b.run_id
&&a.available_owner==b.available_owner&&a.available_generation==b.available_generation
&&a.message_context==b.message_context&&a.input_epoch==b.input_epoch;}
inline bool EqF(const GameFsStatus& a,const GameFsStatus& b){return a.code==b.code&&a.native_error==b.native_error&&a.detail==b.detail;}
inline bool Stopped(const S& s){return !s.owner474&&!s.opened&&!s.poll_completed&&!s.retired&&!s.woken;}
struct Menu {
GameMainMenuControl m;L lease=Lease();Q seq=0;
Menu(){C(m.Begin(lease,true,false),"A01");m.Tick(1);C(m.Snapshot().state78==0,"A02");m.Tick(2);Send(0);}
GameInputFrame Packet(U keys){GameInputFrame f;f.focused=f.test_injected=true;f.buttons=keys;f.sequence=++seq;
f.message_owner_generation=lease.script_generation;f.message_context=lease.message_context;
f.returned_owner=lease.available_owner;f.returned_generation=lease.available_generation;f.main_menu_epoch=lease.input_epoch;return f;}
void Send(U keys){C(m.Input(Packet(keys)),"A03");m.Tick(2);}
void Fresh(U keys){Send(0);Send(keys);}
M Get()const{return m.Snapshot();}
void Enter(U key){Fresh(Up);C(Get().row==6&&Get().column==0&&Get().substate7C==0,"A04");Fresh(key);
C(Get().state78==1&&Get().substate7C==3&&Get().row==6&&Get().column==0&&Get().result==-1,"A05");}
};
inline void MenuFamily(){
for(auto member:{&L::script_generation,&L::run_id,&L::available_owner,&L::available_generation,&L::message_context,&L::input_epoch}){
GameMainMenuControl m;auto l=Lease();l.*member=0;C(!m.Begin(l,true,false)&&!m.Snapshot().live,"A06");}
{Menu t;C(!t.m.Begin(t.lease,true,false)&&t.Get().generation==1&&EqL(t.Get().lease,t.lease),"A07");
for(U field=0;field<5;++field){auto f=t.Packet(Up);if(field==0)++f.message_owner_generation;if(field==1)++f.message_context;
if(field==2)++f.returned_owner;if(field==3)++f.returned_generation;if(field==4)++f.main_menu_epoch;
const auto before=t.Get();C(!t.m.Input(f),"A08");t.m.Tick(2);C(t.Get().row==0&&t.Get().substate7C==0&&t.Get().rejected==before.rejected+1,"A09");}
auto f=t.Packet(0);C(t.m.Input(f)&&!t.m.Input(f),"A10");t.m.Tick(2);
f=t.Packet(Up);f.overflow_resync=true;C(!t.m.Input(f),"A11");t.m.Tick(2);C(t.Get().row==0,"A12");}
for(U enter:{U(Circle),U(Right)})for(U back:{U(Cross),U(Left)}){
Menu t;t.Enter(enter);const auto parent=t.Get();for(U i=0;i<3;++i)t.Send(enter);
C(t.Get().state78==1&&t.Get().substate7C==3&&t.Get().result==-1,"A13");
t.Fresh(Right);C(t.Get().substate7C==3&&t.Get().column==0&&t.Get().unsupported==parent.unsupported,"A14");
for(U dir:{U(Down),U(Up)}){const auto old=t.Get();t.Fresh(dir|Circle|Cross|Left);
if(dir==Down)C(t.Get().unsupported==old.unsupported+1&&t.Get().unsupported_pc==0x4E67Cu,"A15");
else C(t.Get().unsupported==old.unsupported&&t.Get().column==2,"A15-fresh-Up02");
C(t.Get().state78==1&&t.Get().substate7C==3&&t.Get().row==6&&t.Get().column==(dir==Down?0:2)&&t.Get().result==-1,"A16");
t.Send(dir|Circle|Cross|Left);C(t.Get().substate7C==3&&t.Get().state78==1,"A17");}
t.Fresh(back);const auto returned=t.Get();
C(returned.substate7C==0&&returned.state78==1&&returned.row==6&&returned.column==2&&returned.result==-1&&returned.cancels==0,"A18");
C(returned.generation==parent.generation&&returned.opens==parent.opens&&EqL(returned.lease,parent.lease),"A19");
t.Send(back);C(t.Get().state78==1&&t.Get().cancels==0,"A20");t.Fresh(back);
C(t.Get().state78==2&&t.Get().row==-1&&t.Get().column==-1&&t.Get().cancels==1&&t.Get().closed==0,"A21");
t.m.Tick(1);C(t.Get().state78==2&&t.Get().closed==0,"A22");t.m.Tick(2);
C(t.Get().state78==3&&t.Get().result==-1&&t.Get().closed==1&&EqL(t.Get().lease,parent.lease),"A23");}
for(U enter:{U(Circle),U(Right)}){Menu t;t.Enter(enter);t.Fresh(Circle);
C(t.Get().state78==2&&t.Get().result==-1&&t.Get().closed==0&&t.Get().unsupported==0,"A24");
for(U i=0;i<3;++i)t.m.Tick(1);C(t.Get().state78==2&&t.Get().result==-1&&t.Get().closed==0,"A25");
t.m.Tick(2);C(t.Get().state78==3&&t.Get().result==13&&t.Get().closed==1&&t.Get().cancels==0,"A26");
t.m.Tick(2);C(t.Get().closed==1&&EqL(t.Get().lease,t.lease),"A27");}
}
inline void Put(Bytes& b,U at,U n,U width=4){for(U j=0;j<width;++j)b.at(at+j)=uint8_t(n>>(8*j));}
inline void Float(Bytes& b,U at,float n){U w=0;std::memcpy(&w,&n,4);Put(b,at,w);}
struct Storage final:PackageStorageLease {
Bytes bytes,known;explicit Storage(Bytes b):bytes(std::move(b)),known(bytes.size(),1){}
bool Span(PackageStorageSpan& s,std::string&)const override{s.bytes=const_cast<uint8_t*>(bytes.data());s.initialized=const_cast<uint8_t*>(known.data());
s.size=bytes.size();s.host_address=Q(reinterpret_cast<uintptr_t>(bytes.data()));s.owner=s.host_address;
s.generation=s.serial=1;s.readable=true;s.writable=false;return true;}
};
inline Bytes Ui(bool bad_clip=false){
constexpr U nodes=40,clips=840,keys=1100,values=1180,name=1400;Bytes b(1440);
Put(b,0,0x30413242);Put(b,4,1);Put(b,8,32);Put(b,12,100);Put(b,16,nodes);
Put(b,20,bad_clip?4:5);Put(b,24,clips);Put(b,28,5);Put(b,36,name);
const char stem[]="system-synthetic.GIM";std::copy(stem,stem+sizeof(stem),b.begin()+name);
Put(b,nodes+2,97,2);Put(b,nodes+97*8,98,2);Put(b,nodes+98*8,99,2);
for(U i=0;i<5;++i){const U c=clips+i*52,k=keys+i*16,v=values+i*44;
Put(b,c,1);Put(b,c+4,k);Put(b,c+48,8);Put(b,k,i,2);Put(b,k+2,1,2);Put(b,k+4,v);Put(b,v,1);
Put(b,c+24,1);Put(b,c+28,k+8);Put(b,k+8,i,2);Put(b,k+10,2,2);Put(b,k+12,v+12);
Float(b,v+12,float(10+i));Float(b,v+16,float(20+i));Float(b,v+28,90);Float(b,v+32,180);Put(b,v+36,1);Put(b,v+40,1);}
return b;}
inline Bytes Gim(){Bytes b(132);const char magic[]="MIG.00.1PSP";std::copy(magic,magic+sizeof(magic),b.begin());
for(U at:{16u,32u,48u}){Put(b,at,at==16?2:at==32?3:4,2);Put(b,at+2,16,2);Put(b,at+4,132-at);Put(b,at+8,at==48?84:16);Put(b,at+12,16);}
Put(b,64,48,2);Put(b,68,3,2);Put(b,72,1,2);Put(b,74,1,2);Put(b,76,32,2);Put(b,78,4,2);Put(b,80,1,2);
Put(b,88,48);Put(b,92,64);Put(b,96,68);Put(b,104,1,2);Put(b,106,1,2);Put(b,108,3,2);Put(b,110,1,2);Put(b,112,64);Put(b,128,0xFF205060);return b;}
struct Source final:GameResourceSource {
U fault=0,opens=0,ui_reads=0,image_reads=0;std::weak_ptr<Storage> ui_lease;
PackageView Attach(Bytes b,bool ui){const auto n=b.size();auto p=std::make_shared<Storage>(std::move(b));if(ui)ui_lease=p;return PackageStorage::Attach(std::move(p),n);}
GameFsStatus OpenResource(const std::string& path,PackageView& out)override{++opens;
if(path=="option\\gui\\option\\OPTION.CB2A"){++ui_reads;if(fault==1)return {GameFsCode::NotFound,17,"missing OPTION"};out=Attach(Ui(fault==2),true);return {};}
if(path=="option\\gui\\option\\system-synthetic.GIM"){++image_reads;if(fault==3||fault==4)return {GameFsCode::IoFailure,29,fault==4?"":"texture IO"};out=Attach(Gim(),false);return {};}
return {GameFsCode::NotFound,31,"unexpected path"};}
};
inline void PublicImage(){Source io;auto image=std::make_shared<GameUiResource>();
C(bool(image->Load(io,"option\\gui\\option\\OPTION.CB2A","option\\gui\\option\\")),"B01");
C(io.opens==2&&io.ui_reads==1&&io.image_reads==1&&!io.ui_lease.expired(),"B02");
C(image->RootCount()==5&&image->InitialNodes().size()==100&&image->Clips().size()==5,"B03");
for(const auto& clip:image->Clips())C(clip.channels[0].count==1&&clip.channels[3].count==1,"B04");
C(image->Textures().size()==1&&image->Textures()[0].image&&image->Textures()[0].image->Width()==1,"B05");
GameUiInstance copy;C(copy.Clone(image),"B06");
for(U root:{0u,1u,3u,2u,4u}){const U frame=(root==2||root==4)?1:0;
C(copy.Bind(root,root,frame),"B07");auto* n=copy.Node(root);
C(n&&n->base_position[0]==float((frame?100:10)+root)&&n->base_position[1]==float((frame?200:20)+root),"B08");}
C(copy.ShowRoot(0,false),"B09");
for(U clip=0;clip<3;++clip){C(copy.AddControl(clip)!=nullptr,"B10");if(clip){auto* c=copy.Control(clip);c->frame=1;c->mode=2;c->active=1;}}
for(U i=0;i<3;++i){const auto* c=copy.Control(i);C(c&&c->clip==i&&c->frame==(i?1u:0u)&&c->mode==(i?2:0)&&c->active==(i?1:0),"B11");}
C(copy.Control(3)==nullptr,"B12");for(U node:{2u,97u,98u,99u})C(copy.Node(node)!=nullptr,"B13");
C(!copy.Bind(100,0,0)&&!copy.Bind(0,5,0),"B14");
std::weak_ptr<GameUiResource> owner=image;image.reset();C(!owner.expired()&&!io.ui_lease.expired(),"B15");
copy.Close();C(owner.expired()&&io.ui_lease.expired(),"B16");}
inline std::array<U,3> Settings(){return {{0x01020304,0xA1B2C3D4,0xFFEEDDCC}};}
inline void FrontierFamily(){PublicImage();
for(auto member:{&L::script_generation,&L::run_id,&L::available_owner,&L::available_generation,&L::message_context,&L::input_epoch}){
GameSystemOptionFrontier f;auto l=Lease();l.*member=0;C(!f.Request(l,Settings())&&!f.Snapshot().requested&&f.Snapshot().requests==0,"B17");}
Source io;GameSystemOptionFrontier f;C(!f.Advance(io)&&io.opens==0&&!f.Snapshot().failed,"B18");
auto cur=Settings();const auto saved=cur;C(bool(f.Request(Lease(),cur)),"B19");cur.fill(0);
const auto rq=f.Snapshot();C(rq.requested&&rq.phase==1&&rq.requests==1&&rq.generation==1
&&rq.initializer_attempts==0&&rq.passive_settings==saved&&EqL(rq.lease,Lease())&&Stopped(rq),"B20");
C(!f.Request(Lease(),cur)&&f.Snapshot().passive_settings==saved&&f.Snapshot().requests==1&&io.opens==0,"B21");
const auto status=f.Advance(io);const auto ss=f.Snapshot();
C(!status&&status.code==GameFsCode::Unavailable&&status.native_error==0&&status.detail=="Unsupported System resource10 owned message constructor153D4 at34A08","B22");
C(ss.failed&&ss.requested&&ss.constructor_entered&&ss.template_owner&&ss.clone_owner&&!io.ui_lease.expired(),"B23");
C(ss.phase==2&&ss.source_constructor==0x347C8&&ss.source_consumer==0x153D4&&ss.initializer_attempts==1
&&ss.resource==10&&ss.result==13&&ss.allocation_bytes==0x7C&&Stopped(ss),"B24");
C(io.opens==2&&io.ui_reads==1&&io.image_reads==1&&ss.passive_settings==saved&&EqL(ss.lease,Lease()),"B25");
for(U i=0;i<4;++i)C(EqF(f.Advance(io),status)&&io.opens==2&&f.Snapshot().initializer_attempts==1
&&f.Snapshot().template_owner&&f.Snapshot().clone_owner&&Stopped(f.Snapshot()),"B26");
f.Abort();const auto ab=f.Snapshot();C(!ab.requested&&!ab.template_owner&&!ab.clone_owner&&io.ui_lease.expired()
&&ab.failed&&EqF(ab.failure,status)&&Stopped(ab)&&ab.passive_settings==saved,"B27");
f.Abort();C(!f.Request(Lease(),cur)&&EqF(f.Advance(io),status)&&io.opens==2&&f.Snapshot().requests==1&&Stopped(f.Snapshot()),"B28");}
inline void FailureFamily(){for(U fault=1;fault<=4;++fault){Source io;io.fault=fault;GameSystemOptionFrontier f;
C(bool(f.Request(Lease(),Settings())),"C01");const auto first=f.Advance(io);const auto before=f.Snapshot();const auto reads=io.opens;
C(!first&&before.failed&&before.constructor_entered&&before.initializer_attempts==1&&before.phase==1
&&!before.template_owner&&!before.clone_owner&&io.ui_lease.expired()&&before.source_consumer==0&&Stopped(before),"C02");
if(fault==1)C(first.code==GameFsCode::NotFound&&first.native_error==17&&first.detail=="missing OPTION"&&reads==1,"C03");
if(fault==2)C(first.code==GameFsCode::Unavailable&&first.detail=="System constructor static root binding"&&reads==2,"C04");
if(fault>=3)C(first.code==GameFsCode::IoFailure&&first.native_error==29&&first.detail==(fault==4?"":"texture IO")&&reads==2,"C05");
io.fault=0;for(U i=0;i<3;++i)C(EqF(f.Advance(io),first)&&io.opens==reads&&f.Snapshot().initializer_attempts==1
&&EqF(f.Snapshot().failure,first)&&f.Snapshot().passive_settings==Settings()&&Stopped(f.Snapshot()),"C06");
C(!f.Request(Lease(),{{2,3,3}})&&f.Snapshot().requests==1&&f.Snapshot().generation==1&&EqL(f.Snapshot().lease,Lease()),"C07");
f.Abort();C(!f.Snapshot().requested&&f.Snapshot().failed&&EqF(f.Advance(io),first)&&io.opens==reads
&&!f.Request(Lease(),Settings())&&Stopped(f.Snapshot()),"C08");}}
}

inline void GameSystemOptionRegressionChecks(){using namespace dokodemo::system_option_tests;MenuFamily();FrontierFamily();FailureFamily();}
