#pragma once
#include "../src/recovered/game_diary_native.h"
#include "../src/recovered/game_content_native.h"
#include "../src/core_hash.h"
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace dokodemo::diary_tests {
using namespace recovered;
using U=uint32_t;using Q=uint64_t;using Bytes=std::vector<uint8_t>;
using I=GameRecordImage;using L=GameNotebookLease;using F=GameInputFrame;
inline void C(bool yes,const char* id){if(!yes)throw std::runtime_error(std::string("diary ")+id);}
inline void Put(Bytes& b,size_t at,U n,U width=4){for(U j=0;j<width;++j)b.at(at+j)=uint8_t(n>>(8*j));}
inline void Float(Bytes& b,U at,float f){U w=0;std::memcpy(&w,&f,4);Put(b,at,w);}
inline bool EqSlot(const GameRecordSlot& a,const GameRecordSlot& b){return a.flag==b.flag&&a.reserved==b.reserved
&&a.value==b.value&&a.payload==b.payload&&a.date==b.date&&a.source_known_payload_bytes==b.source_known_payload_bytes;}
inline bool EqBank(const GameRecordBank& a,const GameRecordBank& b){
if(a.names!=b.names||a.marker!=b.marker)return false;
for(U r=0;r<28;++r)if(!EqSlot(a.rows[r],b.rows[r]))return false;return true;}
inline bool EqImage(const I& a,const I& b){if(a.live!=b.live||a.generation!=b.generation||a.lookup!=b.lookup||a.sha!=b.sha)return false;
for(U k=0;k<11;++k)if(!EqBank(a.banks[k],b.banks[k]))return false;return true;}
inline Bytes Encode(const I& a){Bytes b;const auto w=[&](U n){for(U j=0;j<4;++j)b.push_back(uint8_t(n>>(8*j)));};
w(a.live?1:0);w(U(a.generation));w(U(a.generation>>32));
for(const auto& bank:a.banks){for(const auto& r:bank.rows){b.push_back(r.flag);
for(auto v:r.reserved)b.push_back(v);w(U(r.value));for(auto v:r.payload)b.push_back(v);
for(auto v:r.date)w(U(v));w(r.source_known_payload_bytes);}
for(auto v:bank.names)b.push_back(v);w(U(bank.marker));}for(auto v:a.lookup)w(U(v));return b;}
inline auto Hash(const I& a){const auto b=Encode(a);return core::sha256(b.data(),b.size());}
inline I LiteralImage(){I a;a.live=true;a.generation=0x1122334455667788ull;
for(U k=0;k<11;++k){auto& b=a.banks[k];for(U r=0;r<28;++r){auto& s=b.rows[r];
s.flag=uint8_t((k+r)%4);s.reserved={{0xA5,uint8_t(k),uint8_t(r)}};s.value=-700+int32_t(31*k+r);
for(U j=0;j<200;++j)s.payload[j]=uint8_t(17*k+3*r+j);
s.date={{2000+int32_t(k),1+int32_t(r%12),1+int32_t(r%28)}};s.source_known_payload_bytes=0xA1000000+k*28+r;}
for(U j=0;j<32;++j)b.names[j]=uint8_t(0x80+k+j);b.marker=int32_t(0xCAFEB000+k);}
for(U j=0;j<28;++j)a.lookup[j]=-900-int32_t(j);a.sha=Hash(a);return a;}
inline Bytes LiteralBytes(){Bytes b(69512);Put(b,0,1);Put(b,4,0x55667788);Put(b,8,0x11223344);
for(U k=0;k<11;++k){const U base=12+k*6308;for(U r=0;r<28;++r){const U at=base+r*224;
b[at]=uint8_t((k+r)%4);b[at+1]=0xA5;b[at+2]=uint8_t(k);b[at+3]=uint8_t(r);
Put(b,at+4,U(-700+int32_t(31*k+r)));for(U j=0;j<200;++j)b[at+8+j]=uint8_t(17*k+3*r+j);
Put(b,at+208,2000+k);Put(b,at+212,1+r%12);Put(b,at+216,1+r%28);Put(b,at+220,0xA1000000+k*28+r);}
for(U j=0;j<32;++j)b[base+6272+j]=uint8_t(0x80+k+j);Put(b,base+6304,0xCAFEB000+k);}
for(U j=0;j<28;++j)Put(b,69400+j*4,U(-900-int32_t(j)));return b;}
inline GameDiaryOpenInputs Inputs(){GameDiaryOpenInputs in;in.profile.live=true;in.profile.actor=0;
in.profile.nickname="synthetic nickname";in.profile.player="synthetic player";
for(U j=0;j<16;++j){in.profile.source_nickname[j]=uint8_t(0x41+j);in.profile.source_player[j]=uint8_t(0x71+j);}
in.profile.date=GameProfile::PackDate(1995,2,3);in.script_generation=71;in.run_id=11;
in.clock_word=0x30000000u+18*60;in.hour=18;in.date={{2000,1,1}};in.manager58=0;return in;}
inline L Lease(){return {71,11,13,31,19,41,27};}
inline F Packet(const L& l,Q seq,U keys=0){F f;f.focused=f.test_injected=true;f.buttons=keys;f.sequence=seq;
f.main_menu_epoch=l.input_epoch;f.message_owner_generation=l.script_generation;f.message_context=l.context;
f.returned_owner=l.available_owner;f.returned_generation=l.available_generation;return f;}
inline I Blank(){auto a=LiteralImage();for(auto& r:a.banks[0].rows){r.flag=0;r.value=-1;r.date={{-1,-1,-1}};}
a.lookup.fill(-1);a.sha=Hash(a);return a;}
inline I Patch(I a,const GameDiaryOpenInputs& in,U marks,const std::vector<int32_t>& ids){
for(U r=0;r<28;++r)if(marks&(1u<<r))a.banks[0].rows[r].flag=1;
std::copy(in.profile.source_nickname.begin(),in.profile.source_nickname.end(),a.banks[0].names.begin());
std::copy(in.profile.source_player.begin(),in.profile.source_player.end(),a.banks[0].names.begin()+16);a.banks[0].marker=in.manager58;
a.lookup.fill(-1);std::copy(ids.begin(),ids.end(),a.lookup.begin());a.sha=Hash(a);return a;}
inline void Projection(const I& before,const GameDiaryOpenInputs& in,U candidate,U marks,
const std::vector<int32_t>& rows,const std::vector<int32_t>& ids){
const auto want=Patch(before,in,marks,ids);GameDiarySelection s;const auto got=GameProjectDiaryOpen(before,in,s);
C(EqImage(want,got)&&EqBank(want.banks[0],s.bank),"B.patch-all-fields");
C(s.candidate==candidate&&s.count==rows.size()&&s.count>=1&&s.lookup==want.lookup,"B.candidate-lookup");
std::array<int32_t,28> rr;rr.fill(-1);std::copy(rows.begin(),rows.end(),rr.begin());
C(s.rows==rr&&s.selection==U(std::find(rows.begin(),rows.end(),int32_t(candidate))-rows.begin()),"B.row-order-ordinal");}
inline void BankCases(){auto lit=LiteralImage();const auto raw=LiteralBytes();
C(raw.size()==69512&&Encode(lit)==raw&&GameHashRecordImage(lit)==core::sha256(raw.data(),raw.size()),"B.literal-hash");
const auto hash=GameHashRecordImage(lit);for(U field=0;field<7;++field){auto a=lit;auto& last=a.banks[10];
if(field==0)last.rows[27].payload[199]^=1;if(field==1)last.rows[27].reserved[2]^=1;
if(field==2)++last.rows[27].source_known_payload_bytes;if(field==3)++last.rows[27].date[2];
if(field==4)last.names[31]^=1;if(field==5)++last.marker;if(field==6)++a.lookup[27];
C(GameHashRecordImage(a)!=hash&&GameHashRecordImage(a)==Hash(a),"B.tail-fields");}
lit.sha.fill(0xEF);C(GameHashRecordImage(lit)==hash,"B.hash-excludes-digest");
auto in=Inputs();auto a=Blank();Projection(a,in,0,0,{0},{});
auto& b=a.banks[0];b.rows[0].value=91;b.rows[0].flag=2;b.rows[1].value=91;b.rows[2].value=5;b.rows[2].flag=255;
b.rows[3].flag=2;b.rows[4].value=44;b.rows[4].date=in.date;b.rows[6].flag=7;b.rows[27].flag=9;b.rows[27].value=103;
a.sha=Hash(a);Projection(a,in,3,7,{0,1,2,3,6,27},{91,91,5,-1,-1,103});
for(U mode:{0u,7u})for(U hour:{18u,19u})for(U flag:{0u,2u,255u}){a=Blank();in=Inputs();
in.mode=mode;in.hour=hour;in.clock_word=0x30000000u+hour*60;auto& r=a.banks[0].rows;
r[0].value=91;r[1].value=12;r[2].value=12;r[2].date=in.date;r[2].flag=uint8_t(flag);
r[4].flag=2;r[27].flag=255;r[27].value=12;a.sha=Hash(a);
const bool mark=mode==0||hour>=19;std::vector<int32_t> ids={91,12};if(mark||flag)ids.push_back(12);ids.push_back(-1);ids.push_back(12);
Projection(a,in,2,3|(mark?4u:0u),{0,1,2,4,27},ids);}
a=Blank();in=Inputs();in.mode=7;std::vector<int32_t> rows,ids;
for(U r=0;r<28;++r){a.banks[0].rows[r].value=50+int32_t(r);rows.push_back(int32_t(r));ids.push_back(50+int32_t(r));}
a.sha=Hash(a);Projection(a,in,27,0x0FFFFFFF,rows,ids);
bool threw=false;try{(void)GameSelectDiary39824(a.banks[0],in.date,24,0);}catch(const std::exception&){threw=true;}C(threw,"B.hour-domain");}

struct Storage final:PackageStorageLease {Bytes data,known;bool live=true;
explicit Storage(Bytes b):data(std::move(b)),known(data.size(),1){}
bool Span(PackageStorageSpan& s,std::string& error)const override{if(!live){error="synthetic lease retired";return false;}
s.bytes=const_cast<uint8_t*>(data.data());s.initialized=const_cast<uint8_t*>(known.data());s.size=data.size();
s.host_address=Q(reinterpret_cast<uintptr_t>(data.data()));s.owner=s.host_address;s.generation=s.serial=1;s.readable=true;s.writable=false;return true;}};
inline PackageView View(Bytes b){const auto n=b.size();return PackageStorage::Attach(std::make_shared<Storage>(std::move(b)),n);}
inline PackageView Gim(U color){Bytes b(132);const char magic[]="MIG.00.1PSP";std::copy(magic,magic+sizeof(magic),b.begin());
for(U at:{16u,32u,48u}){Put(b,at,at==16?2:at==32?3:4,2);Put(b,at+2,16,2);Put(b,at+4,132-at);Put(b,at+8,at==48?84:16);Put(b,at+12,16);}
Put(b,64,48,2);Put(b,68,3,2);Put(b,72,1,2);Put(b,74,1,2);Put(b,76,32,2);Put(b,78,4,2);Put(b,80,1,2);
Put(b,88,48);Put(b,92,64);Put(b,96,68);Put(b,104,1,2);Put(b,106,1,2);Put(b,108,3,2);Put(b,110,1,2);Put(b,112,64);Put(b,128,color);return View(std::move(b));}
inline PackageView UiImage(bool malformed=false){constexpr U nodes=40,clips=808,keys=1024,values=1280,name=1700;Bytes b(1740);
Put(b,0,malformed?0:0x30413242);Put(b,4,1);Put(b,8,32);Put(b,12,96);Put(b,16,nodes);Put(b,20,4);Put(b,24,clips);Put(b,28,4);Put(b,36,name);
const char stem[]="diary-synthetic.GIM";std::copy(stem,stem+sizeof(stem),b.begin()+name);
Put(b,nodes+2,92,2);Put(b,nodes+92*8,93,2);Put(b,nodes+93*8,94,2);Put(b,nodes+8+2,78,2);
for(U i=0;i<4;++i){const U c=clips+i*52,k=keys+i*16,v=values+i*32;Put(b,c+48,8);
Put(b,c,1);Put(b,c+4,k);Put(b,k,i,2);Put(b,k+2,1,2);Put(b,k+4,v);Put(b,v,i==0?1:0);
Put(b,c+24,1);Put(b,c+28,k+8);Put(b,k+8,i==0?92:i==1?93:i==2?94:78,2);Put(b,k+10,1,2);Put(b,k+12,v+12);
Float(b,v+12,i==3?40.0f:-80.0f);Float(b,v+16,i==0?-60.0f:i==1?-24.0f:i==2?14.0f:40.0f);}
//Open rebinds root0: source96C48 resets its entire encoded subtree. Keep
//all three root0 callback anchors in clip0 rather than relying on another
//root's old sparse-channel state surviving that reset.
Put(b,clips+24,3);Put(b,clips+28,1088);
for(U i=0;i<3;++i){Put(b,1088+i*8,92+i,2);Put(b,1090+i*8,1,2);Put(b,1092+i*8,1408+i*16);
Float(b,1408+i*16,-80);Float(b,1412+i*16,i==0?-60.0f:i==1?-24.0f:14.0f);}
return View(std::move(b));}
inline PackageView Font(){constexpr U count=16384,pixels=60+count,palette=pixels+8192;Bytes b(palette+64);
Put(b,0,0x31544658);Put(b,4,1,2);Put(b,6,1,2);Put(b,8,count);b[12]=1;Put(b,16,60);
Put(b,20,count,2);Put(b,22,256,2);Put(b,24,1,2);Put(b,28,4);Put(b,32,256,2);Put(b,36,64,2);
Put(b,40,8192);Put(b,44,pixels);Put(b,48,3);Put(b,52,16);Put(b,56,palette);
std::fill(b.begin()+60,b.begin()+pixels,uint8_t(1));std::fill(b.begin()+pixels,b.begin()+palette,uint8_t(0x11));Put(b,palette+4,0xFFFFFFFF);return View(std::move(b));}
struct FakeResourceSource final:GameResourceSource,GameDiaryIo {
inline static Q owners=1000;GameDiaryIoSnapshot io;U ui_reads=0,image_calls=0;bool missing=false,malformed=false,fail_image=false,empty_error=false;
std::weak_ptr<GameImage> picture;
FakeResourceSource(){io.live=io.ready=true;io.owner=++owners;io.generation=7;}
GameDiaryIoSnapshot DiaryIo()const override{return io;}
void Pending(){++io.requests;++io.pending;io.ready=false;}
void Complete(){C(io.pending>0,"fixture.pending");--io.pending;++io.completed;io.ready=io.pending==0;}
GameFsStatus OpenResource(const std::string& path,PackageView& out)override{Pending();
if(path=="enikki\\gui\\enikki\\ENIKKI.CB2A"){++ui_reads;if(missing)return {GameFsCode::NotFound,17,"synthetic ENIKKI missing"};out=UiImage(malformed);}
else if(path=="enikki\\gui\\enikki\\diary-synthetic.GIM"||path=="synthetic\\seed.GIM")out=Gim(0xFF205060);
else if(path=="synthetic\\d11_N.GIM"){if(fail_image)return {GameFsCode::IoFailure,29,empty_error?"":"synthetic diary image IO failure"};out=Gim(0xFF4080E0);}
else return {GameFsCode::NotFound,31,"unexpected diary synthetic path"};Complete();return {};}
GameFsStatus RequestDiaryImage(const std::string& stem,std::shared_ptr<GameImage>& out)override{++image_calls;
if(stem!="d11_N")return {GameFsCode::NotFound,32,"unexpected diary image pair"};PackageView v;auto st=OpenResource("synthetic\\"+stem+".GIM",v);if(!st)return st;
auto p=std::make_shared<GameImage>();st=p->DecodeGim(v);if(!st)return st;out=p;picture=p;return {};}
void Seed(){std::shared_ptr<GameImage> p;C(bool(TextureResource("synthetic\\seed.GIM",p))&&p&&io.completed==1,"fixture.seed");}
};
struct Case {
FakeResourceSource source;GameRecordStore records;GameDiary diary;GameTransition fade;GameDiaryOpenInputs in=Inputs();L lease=Lease();Q seq=0;I before;
explicit Case(bool positive=true,U count=3){C(fade.Initialize({{0,0}}),"fixture.transition");source.Seed();records.ResetCold249D0();
if(positive)for(U j=0;j<count;++j){auto& r=records.At(0,j);r.value=12;r.flag=j?2:0;r.date=j?GameRecordDate{{1999,12,31}}:in.date;
r.payload[0]='A';r.payload[1]='B';r.source_known_payload_bytes=3;}
for(U k=1;k<11;++k){auto& r=records.At(k,27);r.reserved={{0xE1,uint8_t(k),0xFC}};r.payload[199]=uint8_t(k+170);r.source_known_payload_bytes=k;}
before=records.Image();C(bool(diary.Request(before,in,lease)),"fixture.request");}
GameDiarySnapshot Get()const{return diary.Snapshot();}
void Tick(){C(bool(diary.Tick(source,source,records,fade)),"fixture.tick");}
void Ready(){if(Get().phase==1)Tick();for(U n=0;Get().phase==2&&n<128;++n){fade.Update();Tick();}
C(Get().phase==3&&Get().state154==1&&fade.Snapshot().state==2,"fixture.ready");}
F Frame(U keys=0){return Packet(lease,++seq,keys);}
void Step(U keys=0){C(diary.Input(Frame(keys)),"fixture.input");Tick();}
void Press(U keys){Step();Step(keys);}
void Poll(){for(U n=0;Get().phase==4&&n<128;++n){fade.Update();Tick();}C(Get().phase==6,"fixture.poll");}
};
inline void NoSuccess(const GameDiary& d){const auto s=d.Snapshot();C(s.polls==0&&s.retirements==0&&s.retired_generation==0&&!d.ClosedAndRetired(),"no-success");}
inline bool Status(const GameFsStatus& a,const GameFsStatus& b){return a.code==b.code&&a.native_error==b.native_error&&a.detail==b.detail;}
inline void Terminal(Case& c,const GameFsStatus& first){const auto old=c.Get();const Q reads=c.source.io.requests;const U calls=c.source.image_calls;
const auto bank=c.records.Image();C(!first&&old.failed&&Status(old.failure,first),"terminal.first-status");
const auto second=c.diary.Tick(c.source,c.source,c.records,c.fade);const auto now=c.Get();
C(Status(first,second)&&now.phase==old.phase&&now.state154==old.state154&&now.nested20==old.nested20
&&now.opened_generation==old.opened_generation&&c.source.io.requests==reads&&c.source.image_calls==calls
&&EqImage(bank,c.records.Image()),"terminal.no-replay");NoSuccess(c.diary);}

inline void StoreAndAdmission(){auto p=std::make_unique<Case>();auto& c=*p;GameRecordInput synthetic{12,"AB"};
C(bool(c.records.Capture71(c.in.date,&synthetic)),"A.typed-producer");c.before=c.records.Image();auto empty=std::make_unique<GameDiary>();auto& d=*empty;const auto idle=d.Snapshot();
C(idle.state154==3&&idle.phase==0&&idle.opened_generation==0&&!d.ClosedAndRetired(),"A.ctor-idle");
auto want=Patch(c.before,c.in,1,{12,12,12});const auto stats=c.records.Snapshot();GameDiarySelection s;I approved;
C(bool(c.records.OpenDiary(c.before,c.in.profile.source_nickname,c.in.profile.source_player,0,c.in.date,18,0,s,approved))
&&EqImage(want,approved)&&EqImage(want,c.records.Image())&&s.candidate==0&&s.count==3,"A.store-commit");
const auto after=c.records.Snapshot();C(stats.calls==after.calls&&stats.writes==after.writes&&stats.selected==after.selected
&&stats.protected_calls==after.protected_calls&&stats.capacity_errors==after.capacity_errors&&stats.date==after.date
&&stats.last_copied==after.last_copied&&stats.last_protected==after.last_protected&&stats.generation==after.generation,"A.metadata");
const auto refuse=[&](const I& witness){const auto saved=c.records.Image();GameDiarySelection out;out.count=99;I outimage;outimage.generation=987;
C(!c.records.OpenDiary(witness,c.in.profile.source_nickname,c.in.profile.source_player,0,c.in.date,18,0,out,outimage)
&&EqImage(saved,c.records.Image())&&out.count==99&&outimage.generation==987,"A.store-refused");};
refuse(c.before);auto bad=c.records.Image();bad.sha[31]^=1;refuse(bad);
bad=c.records.Image();++bad.banks[10].rows[27].source_known_payload_bytes;bad.sha=Hash(bad);refuse(bad);
bad=c.records.Image();++bad.generation;bad.sha=Hash(bad);refuse(bad);
const auto old=c.records.Image();c.records.ResetCold249D0();refuse(old);
const auto before=c.records.Image();const auto reject=[&](I image,GameDiaryOpenInputs in,L l){
const auto x=d.Snapshot();C(!d.Request(image,in,l),"A.request-refused");const auto y=d.Snapshot();
C(y.phase==x.phase&&y.generation==x.generation&&y.requests==x.requests&&!y.live&&!y.owner47C
&&y.opened_generation==0&&EqImage(before,c.records.Image()),"A.atomic-request");};
bad=before;bad.sha[0]^=1;reject(bad,c.in,c.lease);bad=before;bad.live=false;bad.sha=Hash(bad);reject(bad,c.in,c.lease);
bad=before;bad.generation=0;bad.sha=Hash(bad);reject(bad,c.in,c.lease);
for(U variant=0;variant<8;++variant){auto in=c.in;if(variant==0)in.profile.live=false;if(variant==1)in.profile.actor=5;
if(variant==2)in.manager58=1;if(variant==3)in.script_generation=0;if(variant==4)in.run_id=0;
if(variant==5)in.clock_word=0x20000000;if(variant==6)++in.date[2];if(variant==7)++in.hour;reject(before,in,c.lease);}
for(auto member:{&L::script_generation,&L::run_id,&L::context,&L::parent_generation,&L::available_owner,&L::available_generation,&L::input_epoch}){
auto l=c.lease;l.*member=0;reject(before,c.in,l);}
C(bool(d.Request(before,c.in,c.lease)),"A.request-valid");const auto requested=d.Snapshot();
C(!d.Request(before,c.in,c.lease)&&d.Snapshot().generation==requested.generation&&d.Snapshot().requests==requested.requests
&&EqImage(before,c.records.Image()),"A.duplicate-request");d.Close();}

inline void Lifecycle(){for(bool positive:{true,false}){auto p=std::make_unique<Case>(positive);auto& c=*p;
C(c.Get().state154==3&&c.Get().phase==1&&c.Get().opened_generation==0,"L.request-not-open");NoSuccess(c.diary);
C(c.fade.StartGameOpen(),"L.shared-open");for(U n=0;c.fade.Snapshot().state!=2&&n<128;++n)c.fade.Update();
C(c.fade.Snapshot().state==2,"L.shared-endpoint");c.fade.CloseWipe();c.Tick();auto s=c.Get();
C(s.state154==4&&s.opened_generation==s.generation&&s.owner47C&&s.text_owner&&s.nested_owner&&s.text_capacity==1024,"L.actual-open");
const auto expected=Patch(c.before,c.in,positive?1:0,positive?std::vector<int32_t>{12,12,12}:std::vector<int32_t>{});
C(EqImage(expected,c.records.Image())&&s.count15C==(positive?3u:1u)&&s.selected_id==(positive?12:-1),"L.B1");
c.Tick();C(c.Get().state154==4&&c.Get().current164==c.Get().selection160,"L.wait-real-close");
for(U n=0;c.fade.Snapshot().state!=0&&n<128;++n)c.fade.Update();C(c.fade.Snapshot().state==0,"L.real-closed");
c.Tick();C(c.Get().state154==5,"L.4-to-5");c.Tick();C(c.Get().state150==1&&c.Get().nested20==0,"L.prepare-selected");
C(c.Get().image_frame==(positive?1:0)&&c.Get().image_stem==(positive?"d11_N":""),"L.actual-pair");
c.source.Pending();for(U next:{1u,2u,3u}){c.Tick();C(c.Get().nested20==next&&c.Get().state154==5,"L.nested-0-1-2-3");NoSuccess(c.diary);}
C(c.source.image_calls==(positive?1u:0u)&&c.Get().io.owner==c.source.io.owner&&c.source.io.completed>=3,"L.same-seeded-owner");
c.Tick();C(c.Get().nested20==3&&c.fade.Snapshot().state==0,"L.pending-barrier");c.source.Complete();c.source.io.ready=false;
c.Tick();C(c.Get().nested20==3,"L.not-ready-barrier");c.source.io.ready=true;c.Tick();C(c.Get().nested20==4,"L.barrier-to-4");
c.Tick();C(c.Get().state154==0&&c.Get().image_ready==1&&c.fade.Snapshot().state==1,"L.start-fade");
c.Tick();C(c.Get().state154==0&&c.Get().phase==2,"L.wait-real-open");c.Ready();NoSuccess(c.diary);
if(!positive){GameGraphics g;GameFontResource f;C(g.Initialize({})&&bool(f.Initialize(Font())),"L.zero-draw-setup");
C(bool(c.diary.Draw(g,f))&&c.Get().callback_reaches==0&&c.Get().draw_frames==0,"L.actual-flag-zero-gate");}
c.Press(Cross);C(c.Get().state154==2&&c.Get().phase==4,"L.source-close");c.Tick();C(c.Get().state154==2,"L.close-waits-transition");
c.Poll();s=c.Get();C(s.state154==3&&!s.visible&&!s.request12&&s.polls==1&&s.retirement_requests==1&&s.retirements==0
&&s.live&&s.text_owner&&s.nested_owner&&s.retirement_pending&&!c.diary.ClosedAndRetired(),"L.poll6");
if(positive)C(!c.source.picture.expired(),"L.picture-kept-until-drain");c.source.ClearTextureCache();c.Tick();s=c.Get();
C(s.phase==7&&!s.live&&!s.owner47C&&!s.text_owner&&!s.nested_owner&&!s.retirement_pending&&s.text_capacity==0
&&s.retirements==1&&s.retired_generation==s.generation&&c.diary.ClosedAndRetired()&&c.source.picture.expired(),"L.drain7");
c.Tick();C(c.Get().polls==1&&c.Get().retirements==1&&EqImage(expected,c.records.Image()),"L.drain-once");
const auto b0=c.records.Image();C(bool(c.diary.Request(b0,c.in,c.lease)),"L.second-request");s=c.Get();
C(s.opens==1&&s.generation==2&&s.opened_generation==0&&s.state154==3&&!c.diary.ClosedAndRetired()
&&EqImage(b0,c.records.Image()),"L.new-open-receipt");c.diary.Close();}
for(U stop:{1u,3u,6u}){auto p=std::make_unique<Case>();auto& c=*p;if(stop>=3)c.Ready();if(stop==6){c.Press(Cross);c.Poll();}
const auto old=c.Get();const auto bank=c.records.Image();c.diary.Close();const auto s=c.Get();
C(!s.live&&!s.request12&&!s.text_owner&&!s.nested_owner&&!s.owner47C&&s.retired_generation==0&&!c.diary.ClosedAndRetired()
&&s.polls==old.polls&&s.retirements==old.retirements&&c.source.picture.expired()&&EqImage(bank,c.records.Image()),"L.forced-close-no-stamp");}}

inline void Failures(){for(U kind=0;kind<4;++kind){auto p=std::make_unique<Case>();auto& c=*p;
if(kind==0)c.source.missing=true;if(kind==1)c.source.malformed=true;if(kind==2)c.source.io.live=false;
if(kind==3)++c.records.At(10,27).payload[199];const auto bank=c.records.Image();const auto st=c.diary.Tick(c.source,c.source,c.records,c.fade);
C(!st&&c.Get().opened_generation==0&&!c.Get().owner47C&&EqImage(bank,c.records.Image()),"F.precommit-B0");Terminal(c,st);}
for(bool empty:{false,true}){auto p=std::make_unique<Case>();auto& c=*p;c.source.fail_image=true;c.source.empty_error=empty;
c.Tick();c.Tick();c.Tick();c.Tick();c.Tick();C(c.Get().nested20==2,"F.image-request-frontier");
const auto b1=c.records.Image();const auto st=c.diary.Tick(c.source,c.source,c.records,c.fade);
C(st.code==GameFsCode::IoFailure&&st.native_error==29&&st.detail==(empty?"":"synthetic diary image IO failure")
&&c.source.image_calls==1&&c.Get().image_requests==0&&c.Get().image_ready==0&&EqImage(b1,c.records.Image()),"F.image-failure-B1");Terminal(c,st);}
for(U kind=0;kind<5;++kind){auto p=std::make_unique<Case>();auto& c=*p;c.diary.Close();auto& row=c.records.At(0,0);
if(kind==0)row.value=13;if(kind==1){c.in.manager58=1;c.in.profile.actor=1;}if(kind==2)row.source_known_payload_bytes=0;
if(kind==3)row.source_known_payload_bytes=201;if(kind==4){row.source_known_payload_bytes=200;row.payload.fill('X');}
c.before=c.records.Image();C(bool(c.diary.Request(c.before,c.in,c.lease)),"F.unsupported-request");c.Tick();c.Tick();
const auto b1=c.records.Image();const auto st=c.diary.Tick(c.source,c.source,c.records,c.fade);
const char* detail=kind<2?"UnsupportedDiaryImagePair:actual actor/record has no recovered pair":kind<4?"UnsupportedDiaryTextKnownExtent":"UnsupportedDiaryTextTerminator";
C(!st&&st.detail==detail&&c.Get().state154==5&&c.source.image_calls==0&&EqImage(b1,c.records.Image()),"F.selected-frontier");Terminal(c,st);}
for(U kind=0;kind<3;++kind){auto p=std::make_unique<Case>();auto& c=*p;c.Ready();c.Step();
if(kind==0)c.source.io.live=false;if(kind==1)++c.source.io.owner;if(kind==2)++c.source.io.generation;
const auto old=c.Get();C(!c.diary.Input(c.Frame(Cross))&&c.Get().last_sequence==old.last_sequence,"F.retired-io-input");
const auto st=c.diary.Tick(c.source,c.source,c.records,c.fade);C(!st&&c.Get().close_requests==0,"F.retired-io-tick");Terminal(c,st);}
}

inline void InputCases(){auto p=std::make_unique<Case>();auto& c=*p;c.Ready();
c.Step(Right|Cross);C(c.Get().navigation==0&&c.Get().close_requests==0,"I.held-entry");c.Step();
const auto accepted=c.Frame(Right);C(c.diary.Input(accepted),"I.accept-prefix");
for(auto field:{&F::main_menu_epoch,&F::message_owner_generation,&F::message_context,&F::returned_owner,&F::returned_generation}){
auto f=c.Frame();++(f.*field);const auto seq=c.Get().last_sequence;C(!c.diary.Input(f)&&c.Get().last_sequence==seq,"I.stale-identity");}
for(Q seq:{Q(0),accepted.sequence-1,accepted.sequence})C(!c.diary.Input(Packet(c.lease,seq,Cross)),"I.sequence-freshness");
c.Tick();C(c.Get().selection160==1&&c.Get().current164==0&&c.Get().state154==4&&c.Get().navigation==1,"I.valid-prefix");
c.Ready();C(c.Get().selected_row==1&&c.Get().current164==1,"I.reprepared");c.Step(Left|Cross);
C(c.Get().phase==3&&c.Get().selection160==1,"I.reopen-held-gate");
const auto epoch=c.Get().input_epoch;C(!c.diary.SetInputEpoch(epoch)&&!c.diary.SetInputEpoch(epoch-1)&&c.diary.SetInputEpoch(epoch+1),"I.epoch-boundary");
C(!c.diary.Input(c.Frame(Cross)),"I.old-epoch-refused");c.lease.input_epoch=epoch+1;c.Step(Cross);C(c.Get().close_requests==0,"I.epoch-held-gate");
for(bool overflow:{true,false}){c.Step();C(c.diary.Input(c.Frame(Cross)),"I.pending-close");auto f=F{};f.focused=overflow;f.overflow_resync=overflow;
C(!c.diary.Input(f),"I.resync-zero");c.Tick();c.Step(Cross);C(c.Get().close_requests==0,"I.resync-clears-edge");}
c.Press(Right|Left|Cross);C(c.Get().selection160==2&&c.Get().close_requests==0,"I.right-priority");c.Ready();
c.Press(Right|Left|Cross);C(c.Get().phase==3&&c.Get().selection160==2&&c.Get().close_requests==0,"I.right-bound-no-wrap");
c.Press(Left|Cross);C(c.Get().selection160==1&&c.Get().close_requests==0,"I.left-priority");c.Ready();c.Press(Left);c.Ready();
c.Press(Left|Cross);C(c.Get().selection160==0&&c.Get().phase==3&&c.Get().close_requests==0,"I.left-bound-no-wrap");
c.Press(Circle|Up|Down);C(c.Get().phase==3&&c.Get().selection160==0,"I.no-note-confirm-route");c.Press(Cross);C(c.Get().phase==4,"I.cross-close");
GameMenuInput input;F f;f.focused=true;input.Observe(f);input.Tick();f.buttons=Right;input.Observe(f);
for(U tick=0;tick<=12;++tick){input.Tick();C(input.Counter(Right)==int32_t(tick)
&&input.Repeat(Right,10,2)==(tick==0?1u:(tick==10||tick==12)?2u:0u),"I.public-repeat-10-2");}
}

inline void RenderCases(){GameGraphics g;GameFontResource font;C(g.Initialize({})&&bool(font.Initialize(Font())),"R.graphics-font");
{FakeResourceSource source;source.Seed();auto resource=std::make_shared<GameUiResource>();
C(bool(resource->Load(source,"enikki\\gui\\enikki\\ENIKKI.CB2A","enikki\\gui\\enikki\\")),"R.fixture-load");
C(resource->RootCount()==4&&resource->InitialNodes().size()==96&&resource->Clips().size()==4,"R.fixture-shape");GameUiInstance clone;C(clone.Clone(resource),"R.fixture-clone");
for(U i=0;i<4;++i){C(resource->Clips()[i].channels[0].count==1&&resource->Clips()[i].channels[3].count==(i?1u:3u)&&clone.Bind(i,i,0),"R.all-frame0-binds");
auto* ctl=clone.AddControl(i);C(ctl!=nullptr,"R.four-controls");*ctl={i?1u:0u,i,uint8_t(i?2:1),1};}
for(U i=0;i<4;++i)C(clone.SetSourceCallback(i==0?92:i==1?93:i==2?94:78,i==0?0x393A4:i==1?0x3953C:i==2?0x39684:0x3973C),"R.actual-callback-bind");
C(clone.ShowRoot(0),"R.root");std::string error;C(clone.Update(error),"R.fixture-update");
GameUiMatrix world;C(!clone.WorldMatrix(78,world),"R.manual-root-hidden");}
auto p=std::make_unique<Case>();auto& c=*p;c.Ready();C(g.Clear(0xFF000000)&&g.SetScissor(0,0,480,272),"R.clear");
C(bool(c.diary.Draw(g,font)),"R.actual-diary-draw");auto s=c.Get();
C(s.callback_mask==15&&s.callback_reaches==4&&s.image_frame==1&&s.image_draws==1&&s.draw_frames==1&&s.quads==1,"R.four-real-callbacks");
C(s.font_resets==3&&s.font_flushes==3&&s.emissions==2&&s.glyphs==8&&s.text_stage==5&&s.text_used==0
&&s.text_scale_x==1&&s.text_scale_y==1&&s.flush_selector==-1,"R.font-state");
GameGraphicsFrame frame;C(g.SwapBuffers()&&g.CopyFront(frame)&&frame.width==480&&frame.height==272&&!frame.diagnostic,"R.copy-front");
C(frame.pixels.at(size_t(176)*frame.stride+280)==0xFFE08040&&frame.pixels.at(size_t(112)*frame.stride+155)==0xFF07182B
&&frame.pixels.at(76*frame.stride+142)==0xFF07182B&&frame.pixels.at(0)==0xFF000000,"R.pixels");
const auto config=g.Snapshot().config;C(config.scissor_left==0&&config.scissor_top==0&&config.scissor_right==480&&config.scissor_bottom==272,"R.scissor-preserved");
font.Close();const auto st=c.diary.Draw(g,font);C(!st,"R.font-retired");const auto failed=c.Get();
C(!c.diary.Draw(g,font)&&c.Get().callback_reaches==failed.callback_reaches&&c.Get().draw_frames==failed.draw_frames,"R.draw-no-replay");Terminal(c,st);
{auto q=std::make_unique<Case>();auto& d=*q;d.Ready();C(bool(font.Initialize(Font())),"R.font-reopen");d.source.io.live=false;
const auto old=d.Get();const auto refused=d.diary.Draw(g,font);C(!refused&&d.Get().callback_reaches==old.callback_reaches&&d.Get().draw_frames==0,"R.retired-io");Terminal(d,refused);}
{auto q=std::make_unique<Case>();auto& d=*q;d.diary.Close();auto& row=d.records.At(0,0);row.payload.fill(0);row.payload[0]=0x81;row.source_known_payload_bytes=2;
d.before=d.records.Image();C(bool(d.diary.Request(d.before,d.in,d.lease)),"R.truncated-request");d.Ready();
const auto refused=d.diary.Draw(g,font);C(!refused&&refused.detail=="truncated actual CP932 message"&&d.Get().draw_frames==0,"R.truncated-CP932-frontier");Terminal(d,refused);}
}
} // namespace dokodemo::diary_tests

inline void GameDiaryRegressionChecks(){using namespace dokodemo::diary_tests;
BankCases();StoreAndAdmission();Lifecycle();Failures();InputCases();RenderCases();}
