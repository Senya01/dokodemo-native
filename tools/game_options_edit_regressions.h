#pragma once
#include "../src/recovered/game_content_native.h"
#include "../src/recovered/game_system_option_native.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

// E### labels: external options-edit-tests-v1/CHECK_LABELS.v1.json.
// SOURCE-authored component checks, not executed by their author. The settings
// writer below is synthetic public transport, never a private actor issuer.
// Query success ALWAYS executes the real GameSourceSoundControl::Gain4AFBC.
namespace dokodemo::options_edit_tests {
using namespace recovered;
using U=uint32_t;using Q=uint64_t;using Bytes=std::vector<uint8_t>;
inline void C(bool v,const char* id){if(!v)throw std::runtime_error(std::string("options-edit ")+id);}
inline void Ok(const GameFsStatus& s,const char* id){if(!s)throw std::runtime_error(std::string("options-edit ")+id+": "+s.detail);}
inline bool Eq(const GameFsStatus& a,const GameFsStatus& b){return a.code==b.code&&a.native_error==b.native_error&&a.detail==b.detail;}
inline void Put(Bytes& b,U at,U v,U n=4){for(U i=0;i<n;++i)b.at(at+i)=uint8_t(v>>(8*i));}
inline void Float(Bytes& b,U at,float v){U bits;std::memcpy(&bits,&v,4);Put(b,at,bits);}
inline U Bits(float v){U bits;std::memcpy(&bits,&v,4);return bits;}
// Named primitive constructors adapted ONLY from first86 lines of the Open
// header: Storage/View/Gim/Font/Ui/Option/Dialog/Source/Help/Lease/Child.
struct Storage final:PackageStorageLease {
    Bytes bytes,known;bool live=true;Q generation=1;
    explicit Storage(Bytes b):bytes(std::move(b)),known(bytes.size(),1){}
    bool Span(PackageStorageSpan& s,std::string& e)const override{
        if(!live){e="E073";return false;}
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
// Synthetic atlas ink, not original FONT or Japanese readability evidence.
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
    GameFsStatus OpenResource(const std::string& p,PackageView& out)override{
        if(p=="option\\gui\\option\\OPTION.CB2A"){out=View(Option());return {};}
        if(p=="data\\gui\\dialog\\DIALOG.CB2A"){out=View(Dialog());return {};}
        if(p=="option\\gui\\option\\options-test.GIM"||p=="data\\gui\\dialog\\options-test.GIM"){out=View(Gim());return {};}
        return {GameFsCode::NotFound,31,"E003"};
    }
};
inline PackageView Bank(std::shared_ptr<Storage>* lease=nullptr){Bytes b(16);Put(b,0,0x44584753);return View(std::move(b),lease);}
inline GameMainMenuLease Lease(){return {71,11,13,31,19,41};}
inline GameNotebookLease Child(){return {71,11,19,1,13,31,41};}

inline void ColdRecords(GameSourceSoundControl& sound){
    for(U i=0;i<31;++i){const auto& r=sound.Record(i);
        C(r.name==0&&r.context54==0&&r.context58==0,"E004");
        C(r.resource==0&&r.resource_state==0&&r.vtable==(i<6?0xDD0A0u:0xDD0B0u),"E074");
        C(r.bank_handle==-1&&r.bank_index==-1&&r.handle==-1,"E005");
        C(r.gain==128&&Bits(r.rate)==0x3F800000&&r.state18==0&&r.state1C==0&&r.state20==0,"E075");
        C(r.step==4&&r.position==0&&r.request==0&&r.flags==0&&!r.bank_attached&&r.bank_handle_known&&r.name_known,"E076");
    }
}
inline void SoundLifecycle(){
    GameSourceSoundControl s;GameSourceSoundGainReceipt out;out.value=777;
    C(!s.Gain4AFBC(s.Snapshot(),105,out)&&out.value==777,"E077");
    Ok(s.Initialize17F0(),"E078");ColdRecords(s);const auto cold=s.Snapshot();
    C(cold.live&&cold.cold_initialized&&cold.owner&&cold.generation&&cold.cold_receipt==1&&cold.id_writes==1,"E079");
    C(cold.constructor==0x17F0&&cold.last_id_writer==0x18E4&&cold.current_id==-1&&cold.previous_id==-1&&cold.pending_id==-1,"18E0/E4/E8");
    C(!cold.automatic&&!cold.automatic_phase&&cold.se_clean_mask==0x3F&&cold.bgm_clean_mask==0x1FFFFFF,"E080");
    C(!s.Initialize17F0()&&GameSourceSoundSnapshotExact(cold,s.Snapshot()),"E081");
    C(!s.BindSe5(View(Bytes(16)))&&GameSourceSoundSnapshotExact(cold,s.Snapshot()),"E082");
    std::shared_ptr<Storage> lease;auto bank=Bank(&lease);Ok(s.BindSe5(bank),"actual SE5 borrow");const auto attached=s.Snapshot();
    C(GameSourceSoundNegativeReady(attached)&&attached.current_id==cold.current_id&&attached.id_writes==cold.id_writes,"E006");
    C(attached.bank.owner==bank.Describe().owner&&attached.bank_bindings==1&&attached.se_clean_mask==0x1F,"E083");
    const auto& se=s.Record(5);C(se.bank_attached&&se.bank_index==5&&!se.name_known&&!se.bank_handle_known&&se.handle==-1,"E007");
    C(!s.BindSe5(Bank())&&GameSourceSoundSnapshotExact(attached,s.Snapshot()),"E084");
    Ok(s.Gain4AFBC(attached,105,out),"E085");const auto first=out;
    C(GameSourceSoundGainValid(out)&&out.value==105&&out.id==-1&&out.negative_guard&&!out.dispatched&&out.returned,"E008");
    C(out.read_pc==0x4AFC8&&out.return_pc==0x4AFE0&&out.after.queries==1,"E009");
    C(!s.Gain4AFBC(attached,105,out)&&GameSourceSoundGainExact(out,first),"E010");
    GameSourceSoundControl foreign;Ok(foreign.Initialize17F0(),"foreign cold");Ok(foreign.BindSe5(Bank()),"foreign lease");
    C(!s.Gain4AFBC(foreign.Snapshot(),105,out)&&GameSourceSoundGainExact(out,first),"E086");
    const auto live=s.Snapshot();lease->live=false;C(!s.Snapshot().bank_live,"E087");
    C(!s.Gain4AFBC(live,105,out)&&s.Snapshot().queries==1,"E012");
    s.Close();C(!s.Snapshot().live&&!s.Gain4AFBC(live,105,out),"E088");
    Ok(s.Initialize17F0(),"E089");ColdRecords(s);
    C(s.Snapshot().generation==cold.generation+1&&s.Snapshot().owner!=cold.owner,"E090");
    Ok(s.BindSe5(Bank()),"new bank");C(!s.Gain4AFBC(live,105,out),"stale generation");
    const auto before=s.Snapshot();const auto refused=s.RequestBgm(4);C(!refused,"E091");const auto failed=s.Snapshot();
    C(failed.failed&&failed.current_id==before.current_id&&failed.id_writes==before.id_writes&&failed.queries==before.queries&&failed.bgm_attempts==1,"E014");
    C(Eq(refused,s.RequestBgm(9))&&Eq(refused,s.Gain4AFBC(failed,105,out))&&GameSourceSoundSnapshotExact(failed,s.Snapshot()),"E092");
}
inline void GainMutationNegatives(){
    GameSourceSoundControl s;Ok(s.Initialize17F0(),"mutation cold");Ok(s.BindSe5(Bank()),"mutation bank");
    GameSourceSoundGainReceipt good;Ok(s.Gain4AFBC(s.Snapshot(),105,good),"E094");
    for(U n=0;n<25;++n){auto r=good;switch(n){
        case 0:r.returned=false;break;case 1:r.negative_guard=false;break;case 2:r.dispatched=true;break;
        case 3:r.read_pc+=4;break;case 4:r.return_pc+=4;break;case 5:r.id=0;break;case 6:++r.value;break;
        case 7:++r.before.owner;break;case 8:++r.after.generation;break;case 9:r.before.live=false;break;
        case 10:r.before.cold_initialized=false;break;case 11:r.before.bank_live=false;break;case 12:r.before.failed=true;break;
        case 13:++r.before.cold_receipt;break;case 14:++r.before.id_writes;break;case 15:r.before.last_id_writer=0x4A604;break;
        case 16:r.before.current_id=0;break;case 17:r.before.previous_id=0;break;case 18:r.before.se_clean_mask=0x3F;break;
        case 19:r.before.bgm_clean_mask=0;break;case 20:++r.before.bank_bindings;break;case 21:++r.before.bgm_attempts;break;
        case 22:++r.after.queries;break;case 23:++r.after.last_query_gain;break;case 24:++r.after.bank.serial;break;}
        C(!GameSourceSoundGainValid(r)&&!GameSourceSoundGainExact(good,r),"E015");
    }
    auto overflow=good;overflow.before.queries=UINT64_MAX;overflow.after.queries=0;C(!GameSourceSoundGainValid(overflow),"E016");
}

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
    GameFsStatus RestoreOptionSettings(const GameOptionSettingsSnapshot&,const std::array<U,3>&)override{++restore_calls;return {GameFsCode::Unavailable,0,"E096"};}
    GameFsStatus Write(const GameOptionSettingsSnapshot& expected,U stage,U value,GameOptionWriteReceipt& out,bool cancel){
        const U fields[]={1,0,1,2},calls[]={0x351CC,0x34CCC,0x34CD4,0x34CDC},setters[]={0x24B34,0x24B1C,0x24B34,0x24B4C};
        auto& issued=cancel?backup:right;
        if(!GameOptionSnapshotExact(expected,state)||stage!=issued.size()||stage>=(cancel?3u:4u)||state.writes==UINT64_MAX)
            return {GameFsCode::Unavailable,0,"E018"};
        if(!cancel&&stage==fail_stage)return {GameFsCode::IoFailure,61,"E097"};
        GameOptionWriteReceipt r;r.gui_generation=gui;r.input_sequence=sequence;r.input_epoch=epoch;r.stage=stage;
        r.field=cancel?stage:fields[stage];r.call_pc=cancel?0x3526C+stage*8:calls[stage];r.setter=cancel?0x24B1C+stage*0x18:setters[stage];r.value=value;
        r.before=state;state.words[r.field]=value;++state.writes;r.after=state;issued.push_back(r);events.push_back(r.call_pc);out=r;return {};
    }
    GameFsStatus WriteOptionRight(const GameOptionSettingsSnapshot& e,U stage,U v,GameOptionWriteReceipt& r)override{return Write(e,stage,v,r,false);}
    GameFsStatus WriteOptionBackup(const GameOptionSettingsSnapshot& e,U stage,U v,GameOptionWriteReceipt& r)override{return Write(e,stage,v,r,true);}
    GameFsStatus QueryOptionGain(const GameOptionSettingsSnapshot& expected,int32_t value,GameOptionGainObservation& out)override{
        ++gain_calls;events.push_back(0x4AFBC);
        if(!GameOptionSnapshotExact(expected,state)||value!=105||fail_gain)return {GameFsCode::Unavailable,73,"E098"};
        GameOptionGainObservation observation;const auto status=sound.Gain4AFBC(sound.Snapshot(),value,observation.source);if(!status)return status;
        observation.source_id_known=true;observation.source_dispatched=false; // from actual successful call above
        if(corrupt_gain)observation.source.return_pc+=4;out=observation;return {};
    }
};
struct Rig {
    Source source;GameFontResource font;Settings settings;GameTransition fade;GameSystemOptionFrontier front;Q sequence=0;
    Rig(){Ok(font.Initialize(Font()),"fixture FONT");C(fade.Initialize({{240,136}}),"fixture transition");
        Ok(front.Request(Lease(),{{2,3,3}}),"E099");front.SetChildLease(Child());
        Ok(front.Initialize(source,font,settings,fade),"E100");
        for(U i=0;i<400&&front.Snapshot().phase!=3;++i){fade.Update();Ok(front.Tick(fade),"natural open wait");}
        C(front.Snapshot().phase==3&&front.SetInputEpoch(42),"E020");settings.gui=front.Snapshot().generation;settings.epoch=42;
        Send(0);
    }
    GameInputFrame Frame(U buttons){GameInputFrame f;f.buttons=buttons;f.focused=true;f.sequence=++sequence;f.main_menu_epoch=42;
        f.message_owner_generation=71;f.message_context=19;f.returned_owner=13;f.returned_generation=31;return f;}
    GameFsStatus Try(U buttons){auto f=Frame(buttons);settings.sequence=f.sequence;C(front.Input(f),"E101");return front.Tick(fade);}
    void Send(U buttons){Ok(Try(buttons),"E102");}
    void Tick(){fade.Update();Ok(front.Tick(fade),"E103");}
    template<class P> void Until(P p,const char* id){for(U i=0;i<600&&!p(front.Snapshot());++i)Tick();C(p(front.Snapshot()),id);}
    void Complete(){Until([](const auto& x){return x.message.complete68;},"E104");}
    void Active(){Until([](const auto& x){return x.message.state64==2&&!x.message.complete68&&x.message.shown>0&&x.message.shown<x.message.total&&x.message.delay70>1;},"E105");}
};
inline void ReceiptNegatives(){
    Rig r;r.Active();r.Send(Right);const auto s=r.front.Snapshot();C(r.settings.right.size()==4,"E106");
    for(U i=0;i<4;++i){const auto good=s.right_receipts[i];C(GameOptionRightReceiptValid(good)&&GameOptionReceiptExact(good,r.settings.right[i]),"E021");
        for(U n=0;n<14;++n){auto x=good;switch(n){case 0:x.stage=4;break;case 1:x.field=(x.field+1)%3;break;case 2:x.call_pc+=4;break;
            case 3:x.setter+=4;break;case 4:x.value=2;break;case 5:x.gui_generation=0;break;case 6:x.input_sequence=0;break;case 7:x.input_epoch=0;break;
            case 8:x.before.live=false;break;case 9:x.after.owner++;break;case 10:x.after.generation++;break;case 11:x.after.writes++;break;
            case 12:x.after.words[(x.field+1)%3]++;break;case 13:x.before.writes=UINT64_MAX;break;}
            C(!GameOptionRightReceiptValid(x)&&!GameOptionReceiptExact(good,x),"E022");
        }
    }
    r.Send(0);r.Complete();r.Send(Cross);const auto closed=r.front.Snapshot();
    for(U i=0;i<3;++i){const auto good=closed.restore_receipts[i];C(GameOptionBackupReceiptValid(good),"E107");
        C(good.call_pc==0x3526C+8*i&&good.setter==0x24B1C+0x18*i&&good.field==i&&good.value==(i?3u:2u),"E023");
        C(!GameOptionRightReceiptValid(good),"E024");
        for(U n=0;n<10;++n){auto x=good;switch(n){case 0:x.stage=3;break;case 1:x.field=(x.field+1)%3;break;case 2:x.call_pc+=4;break;
            case 3:x.setter+=4;break;case 4:x.input_epoch=0;break;case 5:x.gui_generation=0;break;case 6:x.after.words[(i+1)%3]++;break;
            case 7:x.after.writes++;break;case 8:x.after.owner++;break;case 9:x.before.writes=UINT64_MAX;break;}
            C(!GameOptionBackupReceiptValid(x)&&!GameOptionReceiptExact(good,x),"E025");
        }
    }
}
inline void SameMessageBody(const GameOwnedMessageSnapshot& a,const GameOwnedMessageSnapshot& b){
    C(a.text==b.text&&a.shown==b.shown&&a.total==b.total&&a.text_calls==b.text_calls&&a.updates==b.updates,"E026");
    C(a.state64==b.state64&&a.complete68==b.complete68&&a.close69==b.close69&&a.appearance==b.appearance&&a.delay70==b.delay70&&a.idle74==b.idle74,"E108");
    C(a.live==b.live&&a.registered==b.registered&&a.references==b.references&&a.capacity==b.capacity&&a.mode98C==b.mode98C&&a.resets==b.resets,"E027");
}
inline void SharedRate(){
    Source source;GameFontResource font;Ok(font.Initialize(Font()),"shared FONT");Settings root;
    GameOwnedOptionMessage a,b;Ok(a.Initialize(source,font,2),"message A");Ok(a.BindRateSource(root),"A current getter");
    Ok(b.Initialize(source,font,2,a.Shared()),"E110");Ok(b.BindRateSource(root),"B current getter");
    Ok(a.Text("AB\n"),"A ordinary/LF");Ok(b.Text("CD\n"),"B independent text");
    for(U i=0;i<12&&a.Snapshot().state64!=2;++i)Ok(a.Tick(),"A appearance");
    C(a.Snapshot().state64==2&&a.Snapshot().shown==0&&a.Snapshot().appearance==1,"E028");
    Ok(a.Tick(),"first ordinary2");C(a.Snapshot().delay70==4&&a.Snapshot().shown==1,"ordinary4");
    const auto before=a.Snapshot(),other=b.Snapshot();Ok(a.SetRate(3),"shared150B8 row3");SameMessageBody(before,a.Snapshot());SameMessageBody(other,b.Snapshot());
    C(a.Snapshot().rate_setting==3&&b.Snapshot().rate_setting==3&&Bits(a.Snapshot().rate)==0x3F666666,"E111");
    for(U delay:{3u,2u,1u}){Ok(a.Tick(),"countdown4");C(a.Snapshot().delay70==int32_t(delay)&&a.Snapshot().shown==1,"pending4");}
    Ok(a.Tick(),"second ordinary3");C(a.Snapshot().shown==2&&a.Snapshot().delay70==3,"ordinary3");
    for(U i=0;i<3;++i)Ok(a.Tick(),"LF3 dispatch");C(a.Snapshot().delay70==7,"LF7");
    const auto queued=a.Snapshot();Ok(b.SetRate(2),"E115");SameMessageBody(queued,a.Snapshot());
    C(a.Snapshot().rate_setting==2&&Bits(a.Snapshot().rate)==0x3F400000,"E116");
    for(U i=0;i<7;++i)Ok(a.Tick(),"pending LF7");C(a.Snapshot().complete68,"complete at7");
    // Genuine internal appearance completion samples the bound current root,
    // even when constructor2 and the other owner last published2.
    root.state.words[0]=3;const auto applications=b.Snapshot().rate_updates;
    for(U i=0;i<12&&b.Snapshot().state64!=2;++i)Ok(b.Tick(),"E118");
    C(b.Snapshot().shown==0&&b.Snapshot().rate_setting==3&&b.Snapshot().rate_updates==applications+1&&a.Snapshot().rate_setting==3,"E033");
    const auto keep=b.Shared();a.Close();C(b.Snapshot().references==1&&b.Snapshot().live&&b.Shared()==keep,"E034");
    Ok(b.Tick(),"surviving owner");GameGraphics graphics;C(graphics.Initialize({}),"shared graphics");Ok(b.Draw(graphics),"surviving Draw");
    C(b.Snapshot().callbacks&&b.Snapshot().emissions&&b.Snapshot().used>0,"E121");
    const auto stable=b.Snapshot();C(!b.SetRate(4),"unsupported rate");SameMessageBody(stable,b.Snapshot());font.Close();C(!b.Tick(),"retired FONT");b.Close();
}
inline void Row2Lf(){
    Source source;GameFontResource font;Ok(font.Initialize(Font()),"LF2 FONT");GameOwnedOptionMessage m;Ok(m.Initialize(source,font,2),"LF2 owner");Ok(m.Text("A\n"),"LF2 text");
    for(U i=0;i<12&&m.Snapshot().state64!=2;++i)Ok(m.Tick(),"LF2 appearance");Ok(m.Tick(),"LF2 ordinary");
    for(U i=0;i<4;++i)Ok(m.Tick(),"ordinary4 wait");C(m.Snapshot().delay70==10,"LF10");
    for(U i=0;i<9;++i){Ok(m.Tick(),"LF2 pending");C(!m.Snapshot().complete68,"not early LF2");}
    Ok(m.Tick(),"LF2 tenth");C(m.Snapshot().complete68,"complete at10");
}
inline void CheckRight(const Rig& r,const GameSystemOptionSnapshot& before){const auto s=r.front.Snapshot();
    C(!s.failed&&s.gain_returned&&s.right_message_continued&&s.right_steps==1&&s.right_writes==4,"E126");
    C(s.current_words==std::array<U,3>{{3,3,3}}&&s.passive_settings==std::array<U,3>{{2,3,3}}&&s.settings.words==s.current_words,"E127");
    C(s.settings.writes==4&&s.previous50==0&&s.selection56==0&&s.substate48==0&&s.gain_value==105&&s.gain_queries==1,"E128");
    C(s.gain.source_id_known&&!s.gain.source_dispatched&&GameSourceSoundGainValid(s.gain.source)&&r.settings.sound.Snapshot().queries==1,"E041");
    C(s.message.updates==before.message.updates+1&&s.after_key_updates==before.after_key_updates+1,"E042");
    C(r.settings.events==std::vector<U>({0x351CC,0x34CCC,0x34CD4,0x34CDC,0x4AFBC}),"E043");
}
inline void Bars(Rig& r,std::array<U,3> frames){GameGraphics g;C(g.Initialize({}),"bar graphics");const auto before=r.front.Snapshot();
    Ok(r.front.Draw(g,r.font),"E129");const auto s=r.front.Snapshot();
    C(s.bar_frames==frames&&s.manual_bars==before.manual_bars+3&&s.draw_frames==before.draw_frames+1&&s.quads>=3,"E044");
}
inline void Drain(Rig& r){
    r.Until([](const auto& s){return s.phase==6;},"E130");const auto poll=r.front.Snapshot();
    C(poll.polls==1&&poll.close_completions==1&&poll.retirement_requests==1&&poll.poll_completed&&poll.retirement_pending,"E131");
    C(poll.live&&poll.owner474&&poll.template_owner&&poll.clone_owner&&poll.message.live&&poll.message.references==1&&!poll.retirements,"E132");
    C(!r.front.ClosedAndRetired()&&!r.front.Input(r.Frame(Cross)),"E133");
    const auto calls=r.settings.events;const auto updates=poll.message.updates;r.Tick();const auto retired=r.front.Snapshot();
    C(retired.phase==7&&r.front.ClosedAndRetired()&&retired.retired_generation==retired.generation&&retired.retirements==1,"E045");
    C(!retired.live&&!retired.owner474&&!retired.template_owner&&!retired.clone_owner&&!retired.message.live&&!retired.message.references,"E046");
    C(!retired.woken,"E047");
    for(U i=0;i<3;++i)r.Tick();C(r.front.Snapshot().polls==1&&r.front.Snapshot().retirements==1&&r.settings.events==calls&&r.front.Snapshot().message.updates==updates,"E048");
    // Real actor paused18 wake is ROOT's integration obligation, not issued by
    // this public provider. ClosedAndRetired is the component precondition only.
}
inline void EditedCancel(bool complete){
    Rig r;Bars(r,{{8,4,4}});if(complete)r.Complete();else r.Active();const auto before=r.front.Snapshot();r.Send(Right);CheckRight(r,before);
    const auto after=r.front.Snapshot();if(complete){C(after.message.rate_setting==3&&after.message.text_calls==before.message.text_calls+1&&after.message.rate_updates==before.message.rate_updates+1,"E049");}
    else {C(after.message.text_calls==before.message.text_calls&&after.message.rate_updates==before.message.rate_updates&&after.message.delay70==before.message.delay70-1&&after.message.shown==before.message.shown,"E050");}
    Bars(r,{{9,4,4}});r.Send(0);
    // Reach row3 through its next genuine complete68 service when necessary.
    if(!complete){r.Complete();r.Tick();C(r.front.Snapshot().message.rate_setting==3,"E134");}
    if(complete)r.Complete();else r.Active();const auto cancel_before=r.front.Snapshot();r.Send(Cross);const auto s=r.front.Snapshot();
    C(s.phase==4&&s.state44==2&&s.close_requests==1&&s.restore_writes==3&&r.settings.restore_calls==0,"E051");
    C(s.settings.words==std::array<U,3>{{2,3,3}}&&s.settings.writes==7&&s.current_words==std::array<U,3>{{3,3,3}}&&s.passive_settings==std::array<U,3>{{2,3,3}},"E135");
    C(s.previous50==0&&s.after_key_updates==cancel_before.after_key_updates+1&&s.message.updates==cancel_before.message.updates+1,"E052");
    C(s.gain_queries==1&&r.settings.gain_calls==1&&r.settings.sound.Snapshot().queries==1,"E053");
    C(s.source_ui_sound_intents==4&&r.settings.events==std::vector<U>({0x351CC,0x34CCC,0x34CD4,0x34CDC,0x4AFBC,0x3526C,0x35274,0x3527C}),"E054");
    if(complete)C(s.message.rate_setting==2&&s.message.rate_updates==cancel_before.message.rate_updates+1&&s.message.text_calls==cancel_before.message.text_calls+1,"E055");
    else C(s.message.rate_setting==3&&s.message.rate_updates==cancel_before.message.rate_updates&&s.message.text_calls==cancel_before.message.text_calls&&s.message.delay70==cancel_before.message.delay70-1,"E056");
    Bars(r,{{9,4,4}});Drain(r);
}
inline void FailedPrefix(){
    Rig r;r.Active();r.settings.fail_gain=true;const auto before=r.front.Snapshot();const auto failed=r.Try(Right);C(!failed,"E136");const auto prefix=r.front.Snapshot();
    C(prefix.failed&&prefix.right_writes==4&&prefix.settings.writes==4&&prefix.settings.words==std::array<U,3>{{3,3,3}}&&prefix.current_words[0]==3,"E057");
    C(!prefix.gain_returned&&!prefix.right_message_continued&&prefix.source_consumer==0x4AFC8&&prefix.message.updates==before.message.updates,"E137");
    GameGraphics g;C(g.Initialize({}),"failed graphics");const auto events=r.settings.events;
    C(Eq(failed,r.front.Tick(r.fade))&&Eq(failed,r.front.Draw(g,r.font))&&Eq(failed,r.front.Initialize(r.source,r.font,r.settings,r.fade)),"E058");
    C(!r.front.Input(r.Frame(Cross))&&r.settings.events==events&&r.front.Snapshot().polls==0&&r.front.Snapshot().retirements==0,"E059");
    r.front.Abort();C(r.settings.state.words==std::array<U,3>{{3,3,3}}&&r.settings.state.writes==4,"E060");
    Rig corrupt;corrupt.Active();corrupt.settings.corrupt_gain=true;C(!corrupt.Try(Right)&&corrupt.front.Snapshot().failed&&!corrupt.front.Snapshot().gain_returned,"E061");
    Rig partial;partial.Active();partial.settings.fail_stage=2;C(!partial.Try(Right),"E138");
    C(partial.settings.state.writes==2&&partial.front.Snapshot().right_writes==2&&partial.settings.gain_calls==0,"E062");
}
inline void UnsupportedAndRetired(){
    for(U mask:{U(Down|Right),U(Up|Right),U(Left),U(Circle)}){Rig r;const auto status=r.Try(mask);C(!status&&r.front.Snapshot().failed&&r.settings.right.empty()&&r.settings.gain_calls==0,"E063");}
    {Rig r;r.Active();const auto before=r.front.Snapshot();r.Send(Right|Cross);CheckRight(r,before);C(r.settings.backup.empty()&&!r.front.Snapshot().close_requests,"E064");
        r.Send(Cross);C(!r.front.Snapshot().close_requests,"E065");r.Send(0);r.Send(Cross);C(r.front.Snapshot().restore_writes==3,"E066");}
    {Rig r;r.Active();r.Send(Right);r.Send(0);C(!r.Try(Right)&&r.front.Snapshot().failed&&r.settings.right.size()==4,"E067");}
    for(U n=0;n<4;++n){Rig r;auto f=r.Frame(Right);switch(n){case 0:f.main_menu_epoch=41;break;case 1:f.returned_generation++;break;case 2:f.focused=false;break;case 3:f.overflow_resync=true;break;}
        C(!r.front.Input(f)&&r.settings.right.empty(),"E068");}
    {Rig r;auto f=r.Frame(0);C(r.front.Input(f)&&!r.front.Input(f),"E139");}
    {Rig r;r.settings.bank->live=false;C(!r.front.Input(r.Frame(Right)),"E069");C(!r.front.Tick(r.fade)&&r.front.Snapshot().failed&&r.settings.right.empty(),"E070");}
    {Rig r;r.settings.state.generation++;C(!r.front.Input(r.Frame(Right))&&!r.front.Tick(r.fade)&&r.settings.right.empty(),"E071");}
    {Rig r;r.settings.state.live=false;C(!r.front.Tick(r.fade)&&r.settings.right.empty(),"E140");}
    {Rig r;r.font.Close();C(!r.front.Tick(r.fade)&&r.settings.right.empty(),"E072");}
    {Rig r;r.fade.Retire();C(!r.front.Tick(r.fade)&&r.settings.right.empty(),"E141");}
}
} // namespace dokodemo::options_edit_tests

inline void GameOptionsEditRegressionChecks(){
    using namespace dokodemo::options_edit_tests;
    SoundLifecycle();GainMutationNegatives();SharedRate();Row2Lf();
    ReceiptNegatives();EditedCancel(true);EditedCancel(false);FailedPrefix();UnsupportedAndRetired();
}
