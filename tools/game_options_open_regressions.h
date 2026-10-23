#pragma once
#include "../src/recovered/game_content_native.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <utility>

// SOURCE-authored public-API checks. No old test entry, VM, platform runner,
// private owner transport or original asset is used by this header.
namespace dokodemo::options_open_tests {
using namespace recovered;
using U=uint32_t;using Q=uint64_t;using Bytes=std::vector<uint8_t>;
inline void C(bool ok,const char* id){if(!ok)throw std::runtime_error(std::string("options-open ")+id);}
inline void Ok(const GameFsStatus& s,const char* id){if(!s)throw std::runtime_error(std::string("options-open ")+id+": "+s.detail);}
inline bool Eq(const GameFsStatus& a,const GameFsStatus& b){return a.code==b.code&&a.native_error==b.native_error&&a.detail==b.detail;}
inline void Put(Bytes& b,U at,U value,U n=4){for(U i=0;i<n;++i)b.at(at+i)=uint8_t(value>>(8*i));}
inline void Float(Bytes& b,U at,float value){U bits;std::memcpy(&bits,&value,4);Put(b,at,bits);}
struct Storage final:PackageStorageLease {
    Bytes bytes,known;explicit Storage(Bytes b):bytes(std::move(b)),known(bytes.size(),1){}
    bool Span(PackageStorageSpan& s,std::string&)const override{
        s.bytes=const_cast<uint8_t*>(bytes.data());s.initialized=const_cast<uint8_t*>(known.data());s.size=bytes.size();
        s.host_address=Q(reinterpret_cast<uintptr_t>(bytes.data()));s.owner=s.host_address;s.generation=s.serial=1;s.readable=true;s.writable=false;return true;
    }
};
inline PackageView View(Bytes b,std::weak_ptr<Storage>* receipt=nullptr){const auto n=b.size();auto p=std::make_shared<Storage>(std::move(b));if(receipt)*receipt=p;return PackageStorage::Attach(std::move(p),n);}
inline Bytes Gim(){Bytes b(132);const char magic[]="MIG.00.1PSP";std::copy(magic,magic+sizeof(magic),b.begin());
    for(U at:{16u,32u,48u}){Put(b,at,at==16?2:at==32?3:4,2);Put(b,at+2,16,2);Put(b,at+4,132-at);Put(b,at+8,at==48?84:16);Put(b,at+12,16);}
    Put(b,64,48,2);Put(b,68,3,2);Put(b,72,1,2);Put(b,74,1,2);Put(b,76,32,2);Put(b,78,4,2);Put(b,80,1,2);
    Put(b,88,48);Put(b,92,64);Put(b,96,68);Put(b,104,1,2);Put(b,106,1,2);Put(b,108,3,2);Put(b,110,1,2);Put(b,112,64);Put(b,128,0xFF205060);return b;
}
// Checked synthetic XFT: one visible pixel per glyph. This verifies atlas ink
// and metrics, never Japanese readability or the original FONT's appearance.
inline PackageView Font(U width=1,U height=1){constexpr U count=16384,pixels=60+count;const U stride=8192*width*height,palette=pixels+stride;Bytes b(palette+64);
    Put(b,0,0x31544658);Put(b,4,width,2);Put(b,6,height,2);Put(b,8,count);b[12]=1;Put(b,16,60);
    Put(b,20,count,2);Put(b,22,256,2);Put(b,24,1,2);Put(b,28,4);Put(b,32,256*width,2);Put(b,36,64*height,2);
    Put(b,40,stride);Put(b,44,pixels);Put(b,48,3);Put(b,52,16);Put(b,56,palette);
    std::fill(b.begin()+60,b.begin()+pixels,uint8_t(width));std::fill(b.begin()+pixels,b.begin()+palette,uint8_t(0x11));Put(b,palette+4,0xFFFFFFFF);return View(std::move(b));
}
struct Ui {
    Bytes b;U clips;
    Ui(U nodes,U roots,U count):b(40+nodes*8+count*52+32),clips(40+nodes*8){
        Put(b,0,0x30413242);Put(b,4,1);Put(b,8,32);Put(b,12,nodes);Put(b,16,40);Put(b,20,count);Put(b,24,clips);Put(b,28,roots);
        const U name=clips+count*52;Put(b,36,name);const char stem[]="options-test.GIM";std::copy(stem,stem+sizeof(stem),b.begin()+name);
        for(U i=0;i<nodes;++i)Put(b,40+i*8+4,14,2);
        for(U i=0;i<count;++i)Put(b,clips+i*52+48,8);
    }
    U Alloc(U n){const U at=U(b.size());b.resize(b.size()+n);return at;}
    void Link(U node,U sibling,U child){Put(b,40+node*8,sibling,2);Put(b,42+node*8,child,2);}
    // One key record per channel in these small fixtures; use Tracks for lists.
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
inline Bytes Dialog(bool partial=false){Ui u(partial?48:49,4,5);u.Link(0,0,26);if(!partial)u.Link(3,0,48);
    for(U i=0;i<5;++i)u.Tracks(i,0,{{i<4?i:3,{{1,0,1,0}}}});
    if(partial)u.Pos(4,{{3,{{-64,32,4,2}}}});
    else u.Pos(4,{{3,{{-64,32,4,2}}},{48,{{2,3,1,1}}}});return std::move(u.b);
}
struct Source final:GameResourceSource {
    U fault=0,opens=0,option_reads=0,dialog_reads=0,image_reads=0;std::weak_ptr<Storage> option,dialog;
    GameFsStatus OpenResource(const std::string& path,PackageView& out)override{++opens;
        if(path=="option\\gui\\option\\OPTION.CB2A"){++option_reads;if(fault==1)return {GameFsCode::NotFound,17,"missing OPTION"};out=View(Option(),&option);return {};}
        if(path=="data\\gui\\dialog\\DIALOG.CB2A"){++dialog_reads;if(fault==2)return {GameFsCode::NotFound,23,"missing DIALOG"};out=View(Dialog(fault==4),&dialog);return {};}
        if(path=="option\\gui\\option\\options-test.GIM"||path=="data\\gui\\dialog\\options-test.GIM"){
            ++image_reads;if(fault==3&&path[0]=='d')return {GameFsCode::IoFailure,29,"DIALOG texture IO"};out=View(Gim());return {};}
        return {GameFsCode::NotFound,31,"unexpected synthetic resource path"};
    }
};
inline const std::string& Help(){static const std::string s="\x82\xBD\x82\xBE\x82\xA2\x82\xDC\x83\x65\x83\x58\x83\x67\x92\x86\x81\x63\n";return s;}
inline GameMainMenuLease Lease(){return {71,11,13,31,19,41};}
inline GameNotebookLease Child(){return {71,11,19,1,13,31,41};}
struct Settings final:GameOptionSettings {
    GameOptionSettingsSnapshot value;U calls=0,fault=0;std::vector<std::pair<U,U>> stores;GameSystemOptionFrontier* observer=nullptr;U updates_at_restore=0;
    Settings(){value.live=true;value.owner=101;value.generation=7;value.writes=11;value.words={{2,0xA1B2C3D4,0xFFEEDDCC}};}
    GameOptionSettingsSnapshot OptionSettings()const override{return value;}
    GameFsStatus RestoreOptionSettings(const GameOptionSettingsSnapshot& expected,const std::array<U,3>& words)override{
        ++calls;if(observer)updates_at_restore=observer->Snapshot().message.updates;
        if(fault==1)return {GameFsCode::IoFailure,73,"backup restore refused"};
        if(!value.live||value.owner!=expected.owner||value.generation!=expected.generation||value.words!=expected.words)
            return {GameFsCode::Unavailable,0,"synthetic settings identity changed"};
        for(U i=0;i<3;++i){stores.push_back({i,words[i]});value.words[i]=words[i];++value.writes;}
        if(fault==2)value.words[2]^=1;return {};
    }
};
struct Owner {
    Source io;GameFontResource font;Settings settings;GameTransition fade;GameSystemOptionFrontier options;Q sequence=0;
    Owner(){Ok(font.Initialize(Font()),"F01");C(fade.Initialize({{0,0}})&&fade.Snapshot().state==0,"F02");
        settings.observer=&options;Ok(options.Request(Lease(),settings.value.words),"F03");Ok(options.Initialize(io,font,settings,fade),"F04");options.SetChildLease(Child());}
    GameSystemOptionSnapshot Get()const{return options.Snapshot();}
    GameInputFrame Packet(U keys){GameInputFrame f;f.focused=f.test_injected=true;f.sequence=++sequence;f.buttons=keys;
        const auto s=Get();f.message_owner_generation=s.child_lease.script_generation;f.message_context=s.child_lease.context;
        f.returned_owner=s.child_lease.available_owner;f.returned_generation=s.child_lease.available_generation;f.main_menu_epoch=s.input_epoch;return f;}
    void Ready(){for(U i=0;i<128&&Get().phase==2;++i){fade.Update();Ok(options.Tick(fade),"F05");}C(Get().phase==3&&fade.Snapshot().state==2,"F06");}
    void Step(U keys){C(options.Input(Packet(keys)),"F07");Ok(options.Tick(fade),"F08");}
};
inline void Graphics(GameGraphics& g){GameGraphicsConfig c;c.depth_test=false;c.depth_write=false;C(g.Initialize(c),"G01");}
inline GameGraphicsFrame Capture(GameGraphics& g){GameGraphicsFrame f;C(g.SwapBuffers()&&g.CopyFront(f),"G02");C(f.width==480&&f.height==272&&f.stride==512&&!f.diagnostic,"G03");return f;}
inline U Ink(const GameGraphicsFrame& f){U n=0;for(U y=0;y<f.height;++y)for(U x=0;x<f.width;++x)if(f.pixels.at(size_t(y)*f.stride+x)==0xFF552F0D)++n;return n;}
inline bool SameNode(const GameUiNode& a,const GameUiNode& b){return a.encoded_initial==b.encoded_initial&&a.texture==b.texture&&a.visible==b.visible
    &&a.base_position==b.base_position&&a.position==b.position&&a.base_scale==b.base_scale&&a.scale==b.scale&&a.base_angle==b.base_angle&&a.angle==b.angle
    &&a.base_color==b.base_color&&a.color==b.color&&a.source_callback_pc==b.source_callback_pc&&a.draw_group==b.draw_group;}

inline void BindFamily(){
    Ui u(18,2,6);u.Link(0,0,4);u.Link(4,5,0);u.Link(5,6,7);u.Link(1,0,14);
    u.Tracks(0,0,{{4,{{-13,0,-13,0}}},{5,{{-14,0,-14,0}}}});
    u.Pos(0,{{0,{{-171,0,171,0}}},{4,{{3,4,0,0}}},{5,{{3,4,0,0}}},{6,{{3,4,0,0}}},{7,{{3,4,0,0}}}});
    u.Tracks(0,1,{{0,{{2,3,1,1}}},{4,{{2,3,1,1}}},{5,{{2,3,1,1}}},{6,{{2,3,1,1}}},{7,{{2,3,1,1}}}});
    u.Tracks(0,2,{{0,{{0.25f,0,0,0}}},{6,{{0.25f,0,0,0}}}});
    u.Tracks(0,4,{{0,{{0,0,0,0}}}}); // overwritten below with exact integer bits
    const U color_keys=U(u.b[u.clips+36])|(U(u.b[u.clips+37])<<8)|(U(u.b[u.clips+38])<<16)|(U(u.b[u.clips+39])<<24);
    const U color_at=U(u.b[color_keys+4])|(U(u.b[color_keys+5])<<8)|(U(u.b[color_keys+6])<<16)|(U(u.b[color_keys+7])<<24);Put(u.b,color_at,0xA0B0C0D0);Put(u.b,color_at+12,0x80808080);
    u.Pos(1,{{12,{{86,-21,0,0}}},{13,{{9,-3,0,0}}},{14,{{22,7,0,0}}}});u.Pos(5,{{4,{{-137,84,0,0}}},{5,{{-137,84,0,0}}}});
    struct One final:GameResourceSource {Bytes bytes;explicit One(Bytes b):bytes(std::move(b)){}GameFsStatus OpenResource(const std::string& p,PackageView& out)override{out=View(p=="BIND.CB2A"?bytes:Gim());return {};}} io(std::move(u.b));
    auto resource=std::make_shared<GameUiResource>();Ok(resource->Load(io,"BIND.CB2A",""),"B01");GameUiInstance copy;C(copy.Clone(resource),"B02");
    C(copy.Bind(1,1,0)&&copy.Bind(0,0,0)&&copy.ShowRoot(0)&&copy.SetSourceCallback(4,0x1234),"B03");copy.Node(4)->draw_group=7;
    C(copy.AddControl(0)!=nullptr&&copy.AddControl(1)!=nullptr,"B04");*copy.Control(0)={1,0,2,1};std::string error;C(copy.Update(error),"B05");
    const auto before=copy.Nodes();C(copy.Bind(0,5,0),"B06");
    for(U i:{0u,4u,5u,6u,7u}){const auto& a=before[i];const auto& b=*copy.Node(i);
        C(b.base_scale==std::array<float,2>{{1,1}}&&b.base_angle==0&&b.base_color==0x80808080,"B07");
        C(b.position==a.position&&b.scale==a.scale&&b.angle==a.angle&&b.color==a.color,"B08");
        C(b.encoded_initial==a.encoded_initial&&b.texture==a.texture&&b.source_callback_pc==a.source_callback_pc&&b.draw_group==a.draw_group&&b.visible==a.visible,"B09");}
    for(U i:{1u,12u,13u,14u})C(SameNode(before[i],*copy.Node(i)),"B10");
    C(copy.Node(0)->base_position==std::array<float,2>{{0,0}}&&copy.Node(6)->base_position==std::array<float,2>{{0,0}},"B11");
    C(copy.Control(0)->clip==0&&copy.Control(0)->frame==1&&copy.Control(0)->mode==2&&copy.Control(0)->active==1&&copy.Control(1)->clip==1,"B12");
    *copy.Control(0)={0,5,2,1};C(copy.Update(error),"B13");GameUiMatrix m;
    C(copy.WorldMatrix(12,m)&&m.tx==-51&&m.ty==63,"B14");C(copy.WorldMatrix(13,m)&&m.tx==-128&&m.ty==81,"B15");
    const auto stable=copy.Nodes();C(copy.Update(error),"B16");for(U i=0;i<stable.size();++i)C(SameNode(stable[i],*copy.Node(i)),"B17");
    C(copy.Bind(0,0,0),"B18");*copy.Control(0)={1,0,2,1};C(copy.Update(error)&&copy.Node(0)->position[0]==0,"B19");
    C(copy.Bind(0,5,0),"B20");*copy.Control(0)={0,5,2,1};C(copy.Update(error)&&copy.WorldMatrix(12,m)&&m.tx==-51,"B21");
    copy.Node(6)->encoded_initial[0]=4;const auto corrupt=copy.Nodes();C(!copy.Bind(0,5,0),"B22");for(U i=0;i<corrupt.size();++i)C(SameNode(corrupt[i],*copy.Node(i)),"B23");
}

inline void MessageFamily(){
    Source io;GameFontResource font;Ok(font.Initialize(Font()),"M01");GameOwnedOptionMessage a,b;
    Ok(a.Initialize(io,font,2),"M02");auto shared=a.Shared();const U reads=io.opens;Ok(b.Initialize(io,font,2,shared),"M03");
    C(io.opens==reads&&a.Snapshot().references==2&&b.Snapshot().references==2&&a.Shared()==b.Shared(),"M04");
    C(reads==2&&io.dialog_reads==1&&io.image_reads==1&&!io.dialog.expired(),"M05");
    const auto initial=a.Snapshot();C(initial.live&&initial.template_owner&&initial.clone_owner&&initial.text_owner&&initial.capacity==1024&&initial.mode98C==4
        &&initial.columns==9&&initial.rows==2&&initial.width==144&&initial.height==36&&initial.rate==0.75f&&initial.complete68&&!initial.registered,"M06");
    GameFontBitmap metric;std::array<GameFontDrawRecord,9> records;Ok(font.RasterizeCp932({Help().substr(0,18)},480,metric,records.data(),9,0xFF552F0D,18,0),"M07");
    C(metric.glyphs==9&&metric.advance_width==9&&metric.width==480&&metric.height==18,"M08");
    for(U i=0;i<9;++i)C(records[i].x==float(i)&&records[i].y==0&&records[i].width==1&&records[i].height==1,"M09");
    Ok(a.Text(Help()),"M10");C(a.Snapshot().text==Help()&&a.Snapshot().total==9&&a.Snapshot().shown==0&&a.Snapshot().idle74==90&&a.Snapshot().state64==1&&a.Snapshot().registered,"M11");
    GameGraphics g;Graphics(g);C(g.Clear(0xFF000000),"M12");Ok(a.Draw(g),"M13");C(a.Snapshot().callbacks==1&&a.Snapshot().emissions==0&&Ink(Capture(g))==0,"M14");
    for(U i=0;i<16&&a.Snapshot().state64==1;++i){const auto old=a.Snapshot();Ok(a.Tick(),"M15");C(a.Snapshot().shown==0&&a.Snapshot().idle74==90&&a.Snapshot().appearance>old.appearance,"M16");}
    C(a.Snapshot().state64==2&&a.Snapshot().appearance==1,"M17");
    Ok(a.Tick(),"M18");C(a.Snapshot().shown==1&&a.Snapshot().delay70==4,"M19");
    for(U i=1;i<=3;++i){Ok(a.Tick(),"M20");C(a.Snapshot().shown==1&&a.Snapshot().delay70==int32_t(4-i)&&a.Snapshot().idle74==90,"M21");}
    Ok(a.Tick(),"M22");C(a.Snapshot().shown==2&&a.Snapshot().delay70==4,"M23");
    const auto prefix=a.Snapshot();Ok(a.Text(Help()),"M24");C(a.Snapshot().shown==prefix.shown&&a.Snapshot().total==18&&a.Snapshot().state64==2&&a.Snapshot().appearance==1&&a.Snapshot().delay70==4,"M25");
    for(U i=0;i<64&&a.Snapshot().shown<9;++i)Ok(a.Tick(),"M26");C(a.Snapshot().shown==9&&a.Snapshot().delay70==4,"M27");
    for(U i=0;i<4;++i)Ok(a.Tick(),"M28");C(a.Snapshot().shown==9&&a.Snapshot().delay70==10&&!a.Snapshot().complete68,"M29");
    C(g.Clear(0xFF000000),"M30");Ok(a.Draw(g),"M31");auto front=Capture(g);
    C(a.Snapshot().used==9&&a.Snapshot().batch_stage==5&&a.Snapshot().resets==1&&a.Snapshot().flushes==1&&a.Snapshot().flush_selector==-1&&a.Snapshot().scale_x==1&&a.Snapshot().scale_y==1,"M32");
    C(Ink(front)==9,"M33");for(U i=0;i<9;++i)C(front.pixels.at(174*front.stride+183+i)==0xFF552F0D,"M34");
    const auto scissor=g.Snapshot().config;C(scissor.scissor_left==0&&scissor.scissor_top==0&&scissor.scissor_right==480&&scissor.scissor_bottom==272,"M35");
    for(U i=1;i<=9;++i){Ok(a.Tick(),"M36");C(a.Snapshot().shown==9&&a.Snapshot().delay70==int32_t(10-i),"M37");}
    Ok(a.Tick(),"M38");C(a.Snapshot().shown==10&&a.Snapshot().delay70==4,"M39");
    C(g.Clear(0xFF000000),"M40");Ok(a.Draw(g),"M41");front=Capture(g);C(Ink(front)==10&&front.pixels.at(192*front.stride+183)==0xFF552F0D,"M42");
    for(U i=0;i<100&&!a.Snapshot().complete68;++i)Ok(a.Tick(),"M43");C(a.Snapshot().complete68&&a.Snapshot().shown==18,"M44");
    for(U i=1;i<=89;++i){Ok(a.Tick(),"M45");C(a.Snapshot().idle74==int32_t(89-i)&&a.Snapshot().state64==2&&!a.Snapshot().close69,"M46");}
    Ok(a.Tick(),"M67");C(a.Snapshot().state64==2&&a.Snapshot().close69,"M68");Ok(a.Tick(),"M69");C(a.Snapshot().state64==3,"M70");
    for(U i=0;i<9;++i){Ok(a.Tick(),"M47");C(a.Snapshot().state64==3&&a.Snapshot().appearance>0,"M71");}Ok(a.Tick(),"M72");C(a.Snapshot().state64==0&&!a.Snapshot().registered&&a.Snapshot().appearance==0&&a.Snapshot().text.empty(),"M48");
    {GameOwnedOptionMessage p;Ok(p.Initialize(io,font,2,shared),"M58");Ok(p.Text("A\n"),"M59");
        for(U i=0;i<160&&p.Snapshot().state64!=3;++i)Ok(p.Tick(),"M60");C(p.Snapshot().state64==3,"M61");Ok(p.Text("B\n"),"M62");
        for(U i=0;i<16&&p.Snapshot().state64==3;++i)Ok(p.Tick(),"M63");C(p.Snapshot().state64==1&&p.Snapshot().registered&&p.Snapshot().appearance==0&&p.Snapshot().shown==1,"M64");
        for(U i=0;i<80&&!p.Snapshot().complete68;++i)Ok(p.Tick(),"M65");C(p.Snapshot().shown==2&&p.Snapshot().complete68,"M66");}
    // More than 102 lifetime help writes must reclaim consumed input, while
    // actual unconsumed queue capacity remains bounded at1024 units.
    for(U cycle=0;cycle<104;++cycle){Ok(a.Text(Help()),"M49");for(U i=0;i<100&&!a.Snapshot().complete68;++i)Ok(a.Tick(),"M50");C(a.Snapshot().complete68&&a.Snapshot().total==9*(cycle+1)&&a.Snapshot().shown==a.Snapshot().total,"M51");}
    GameOwnedOptionMessage full;Ok(full.Initialize(io,font,2,shared),"M52");for(U i=0;i<102;++i)Ok(full.Text(Help()),"M53");const auto cap=full.Snapshot();const auto refused=full.Text(Help());
    C(!refused&&refused.detail=="15C88 unconsumed owned text ring capacity reached"&&full.Snapshot().shown==0&&full.Snapshot().total==cap.total&&full.Snapshot().text==cap.text,"M54");full.Close();
    a.Close();C(b.Snapshot().references==1&&b.Snapshot().live&&!io.dialog.expired(),"M55");Ok(b.Text(Help()),"M56");
    b.Close();C(io.dialog.expired()&&!b.Snapshot().live&&!b.Snapshot().text_owner&&b.Snapshot().references==0,"M57");
}

inline void FailureFamily(){
    for(U fault=1;fault<=4;++fault){Source io;io.fault=fault;GameFontResource font;Ok(font.Initialize(Font()),"E01");Settings settings;GameTransition fade;C(fade.Initialize({{0,0}}),"E02");GameSystemOptionFrontier f;
        Ok(f.Request(Lease(),settings.value.words),"E03");const auto status=f.Initialize(io,font,settings,fade);const auto first=f.Snapshot();const U reads=io.opens;
        C(!status&&first.failed&&!first.live&&!first.owner474&&!first.opened&&!first.poll_completed&&!first.retired&&first.initializer_attempts==1,"E04");
        C(first.template_owner==(fault!=1)&&first.clone_owner==(fault!=1)&&first.phase==(fault==1?1u:2u),"E05");
        if(fault==1)C(status.code==GameFsCode::NotFound&&status.native_error==17&&status.detail=="missing OPTION","E06");
        if(fault==2)C(status.code==GameFsCode::NotFound&&status.native_error==23&&status.detail=="missing DIALOG","E07");
        if(fault==3)C(status.code==GameFsCode::IoFailure&&status.native_error==29&&status.detail=="DIALOG texture IO","E08");
        if(fault==4)C(status.detail=="153D4 callback node owner"&&first.message.template_owner&&first.message.clone_owner&&!first.message.text_owner&&first.message.references==0,"E09");
        io.fault=0;GameGraphics g;Graphics(g);C(Eq(f.Initialize(io,font,settings,fade),status)&&Eq(f.Advance(io),status)&&Eq(f.Tick(fade),status)&&Eq(f.Draw(g,font),status)&&io.opens==reads,"E10");
        f.Abort();C(io.option.expired()&&io.dialog.expired()&&!f.Snapshot().template_owner&&!f.Snapshot().message.live&&Eq(f.Tick(fade),status)&&!f.Snapshot().poll_completed,"E11");
    }
    {Source io;Settings settings;GameFontResource cold;GameTransition fade;C(fade.Initialize({{0,0}}),"E12");GameSystemOptionFrontier f;Ok(f.Request(Lease(),settings.value.words),"E13");
        const auto s=f.Initialize(io,cold,settings,fade);C(!s&&s.detail=="153D4 live borrowed FONT/owner required"&&f.Snapshot().phase==2&&f.Snapshot().template_owner&&!f.Snapshot().owner474&&io.dialog_reads==0,"E14");f.Abort();}
    {Source io;GameSystemOptionFrontier f;Settings settings;Ok(f.Request(Lease(),settings.value.words),"E15");const auto s=f.Advance(io);
        C(!s&&s.detail=="Unsupported System resource10 owned message constructor153D4 at34A08"&&f.Snapshot().phase==2&&f.Snapshot().template_owner&&f.Snapshot().clone_owner&&!f.Snapshot().owner474&&!f.Snapshot().opened,"E16");f.Abort();}
    {Source io;GameFontResource font,other;Ok(font.Initialize(Font()),"E17");Ok(other.Initialize(Font()),"E18");GameOwnedOptionMessage a,b;
        Ok(a.Initialize(io,font,2),"E19");const U reads=io.opens;auto shared=a.Shared();const auto s=b.Initialize(io,other,2,shared);
        C(!s&&s.detail=="153D4 shared FONT identity replaced"&&!b.Snapshot().live&&a.Snapshot().references==1&&io.opens==reads,"E20");b.Close();C(a.Snapshot().references==1,"E21");
        const auto before=a.Snapshot();C(!a.Text(std::string("\x81",1))&&!a.Text("1234567890")&&!a.Text("a\nb")&&a.Snapshot().text==before.text&&a.Snapshot().total==0,"E22");}
    for(U fault=1;fault<=2;++fault){Owner t;t.Ready();t.Step(0);t.settings.fault=fault;const auto before=t.Get();C(t.options.Input(t.Packet(Cross)),"E23");const auto s=t.options.Tick(t.fade);
        C(!s&&t.Get().failed&&t.Get().phase==3&&t.Get().close_requests==0&&t.Get().message.updates==before.message.updates&&t.settings.calls==1,"E24");
        C(s.detail==(fault==1?"backup restore refused":"Options backup readback mismatch")&&Eq(t.options.Tick(t.fade),s)&&t.settings.calls==1,"E25");}
    for(U kind=0;kind<2;++kind){Source io;GameFontResource font;Ok(font.Initialize(kind?Font(1,19):Font(17)),"E26");GameOwnedOptionMessage m;Ok(m.Initialize(io,font,2),"E27");const auto s=m.Text(Help());
        C(!s&&m.Snapshot().text.empty()&&m.Snapshot().total==0&&m.Snapshot().text_calls==0,"E28");if(!kind)C(s.detail=="15C88 actual atlas line exceeds144x18","E29");}
}

inline void InputFamily(){
    {Owner t;t.Ready();t.Step(Cross);C(t.Get().phase==3&&t.settings.calls==0,"I01");t.Step(Cross);C(t.Get().close_requests==0,"I02");
        t.Step(0);const auto accepted=t.Packet(0);C(t.options.Input(accepted)&&!t.options.Input(accepted),"I03");Ok(t.options.Tick(t.fade),"I04");
        for(U field=0;field<5;++field){auto f=t.Packet(Cross);if(field==0)++f.message_owner_generation;if(field==1)++f.message_context;if(field==2)++f.returned_owner;if(field==3)++f.returned_generation;if(field==4)++f.main_menu_epoch;
            const auto before=t.Get();C(!t.options.Input(f)&&t.Get().last_sequence==before.last_sequence&&t.Get().rejected==before.rejected+1,"I05");Ok(t.options.Tick(t.fade),"I06");C(t.Get().phase==3&&t.settings.calls==0,"I07");}
        for(U kind=0;kind<2;++kind){auto f=t.Packet(Cross);if(kind)f.overflow_resync=true;else f.focused=false;C(!t.options.Input(f),"I08");Ok(t.options.Tick(t.fade),"I09");t.Step(Cross);C(t.settings.calls==0,"I10");t.Step(0);}
        const Q old=t.Get().input_epoch;C(!t.options.SetInputEpoch(old)&&!t.options.SetInputEpoch(old-1)&&t.options.SetInputEpoch(old+1),"I11");
        auto stale=t.Packet(Cross);stale.main_menu_epoch=old;C(!t.options.Input(stale),"I12");t.Step(Cross);C(t.settings.calls==0,"I13");t.Step(0);t.Step(Cross);C(t.Get().phase==4&&t.settings.calls==1,"I14");}
    const U keys[]={Down,Up,Right,Left,Circle};const U pcs[]={0x350B0,0x3512C,0x3516C,0x351EC,0x352BC};
    for(U i=0;i<5;++i){Owner t;t.Ready();t.Step(0);const auto before=t.Get();U chord=Cross;for(U j=i;j<5;++j)chord|=keys[j];
        C(t.options.Input(t.Packet(chord)),"I15");const auto s=t.options.Tick(t.fade);const auto after=t.Get();
        C(!s&&s.code==GameFsCode::Unavailable&&after.failed&&after.source_consumer==pcs[i]&&after.unsupported==before.unsupported+1,"I16");
        C(after.phase==3&&after.close_requests==0&&t.settings.calls==0&&after.selection56==0&&after.previous50==0&&after.substate48==0&&after.after_key_updates==before.after_key_updates,"I17");
        C(Eq(t.options.Tick(t.fade),s)&&t.Get().unsupported==after.unsupported,"I18");}
    for(U field=0;field<4;++field){Owner t;t.Ready();t.Step(0);if(field==0)++t.settings.value.owner;if(field==1)++t.settings.value.generation;if(field==2)t.settings.value.words[1]^=0x80000000;if(field==3)t.font.Close();
        C(!t.options.Input(t.Packet(Cross)),"I19");const auto s=t.options.Tick(t.fade);C(!s&&s.detail=="Options bound settings/FONT owner replaced"&&t.settings.calls==0&&!t.Get().poll_completed,"I20");}
}

inline void LifecycleFamily(){
    Owner t;auto s=t.Get();C(s.phase==2&&s.state44==0&&s.owner474&&s.live&&s.visible&&s.request10&&s.loader469==1&&s.opens==1&&s.opened&&s.source_consumer==0x34F1C,"L01");
    C(t.fade.Snapshot().state==1&&s.message.text_calls==0&&s.message.updates==0&&t.settings.calls==0&&s.source_ui_sound_intents==1,"L02");
    C(!t.options.Input(t.Packet(Cross)),"L03");Ok(t.options.Tick(t.fade),"L04");C(t.Get().phase==2&&t.Get().message.updates==0,"L05");
    t.Ready();s=t.Get();C(s.state44==1&&s.after_key_updates==0&&s.message.text_calls==0&&s.message.updates==0&&s.close_requests==0,"L06");
    t.Step(0);C(t.Get().message.text==Help()&&t.Get().message.text_calls==1&&t.Get().message.updates==1&&t.Get().message.shown==0,"L07");
    GameGraphics g;Graphics(g);C(g.Clear(0xFF000000),"L08");Ok(t.options.Draw(g,t.font),"L09");Capture(g);C(t.Get().draw_frames==1&&t.Get().manual_bars==3&&t.Get().quads>=4,"L10");
    const auto before=t.Get();const auto words=t.settings.value.words;const Q writes=t.settings.value.writes;t.Step(Cross);s=t.Get();
    C(s.phase==4&&s.state44==2&&s.close_requests==1&&s.owner474&&s.request10&&s.loader469==1&&t.fade.Snapshot().state==3,"L11");
    C(t.settings.calls==1&&t.settings.stores.size()==3&&t.settings.value.words==words&&t.settings.value.writes==writes+3&&s.settings.writes==writes+3,"L12");
    for(U i=0;i<3;++i)C(t.settings.stores[i]==std::make_pair(i,words[i]),"L13");
    C(t.settings.updates_at_restore==before.message.updates&&s.after_key_updates==before.after_key_updates+1&&s.message.updates==before.message.updates+1&&s.message.live,"L14");
    C(s.selection56==0&&s.previous50==0&&s.substate48==0&&s.source_ui_sound_intents==3,"L15");
    // Selection==previous is the public witness for the source gain skip.
    // This interface has no gain consumer: do not invent a gain counter test.
    for(U i=0;i<128&&t.Get().phase==4;++i){t.fade.Update();Ok(t.options.Tick(t.fade),"L16");}
    s=t.Get();C(t.fade.Snapshot().state==0&&s.phase==6&&s.state44==3&&!s.visible&&!s.request10&&s.loader469==0&&s.poll_completed&&s.polls==1&&s.close_completions==1,"L17");
    C(s.owner474&&s.live&&s.template_owner&&s.clone_owner&&s.message.live&&s.message.text_owner&&s.retirement_pending&&!s.retired&&s.retirements==0&&!t.io.option.expired()&&!t.io.dialog.expired(),"L18");
    C(!t.options.ClosedAndRetired()&&t.options.Modal(),"L19");const U updates=s.message.updates;
    Ok(t.options.Tick(t.fade),"L20");s=t.Get();C(s.phase==7&&s.retired&&s.retired_generation==s.generation&&s.retirements==1&&!s.retirement_pending&&!s.owner474&&!s.live&&!s.requested&&!s.template_owner&&!s.clone_owner&&!s.message.live&&!s.message.text_owner,"L21");
    C(t.options.ClosedAndRetired()&&!t.options.Modal()&&s.polls==1&&s.message.updates==updates&&t.io.option.expired()&&t.io.dialog.expired(),"L22");
    const U reads=t.io.opens;Ok(t.options.Tick(t.fade),"L28");Ok(t.options.Tick(t.fade),"L29");C(!t.Get().failed&&t.Get().retirements==1&&t.Get().polls==1&&t.io.opens==reads&&t.Get().message.updates==updates,"L30");
    const Q generation=s.generation;Ok(t.options.Request(Lease(),words),"L23");Ok(t.options.Initialize(t.io,t.font,t.settings,t.fade),"L24");t.options.SetChildLease(Child());s=t.Get();
    C(s.generation==generation+1&&s.requests==2&&s.phase==2&&s.message.text.empty()&&s.message.total==0&&s.message.shown==0&&s.message.text_calls==0&&s.message.delay70==0,"L25");
    t.Ready();t.Step(0);C(t.Get().message.text==Help()&&t.Get().message.total==9&&t.Get().message.shown==0,"L26");
    // Abort is exercised only as explicit teardown; never earns poll/wake.
    t.options.Abort();C(!t.Get().poll_completed&&!t.Get().retired&&!t.Get().woken,"L27");
}
} // namespace dokodemo::options_open_tests

inline void GameOptionsOpenRegressionChecks(){using namespace dokodemo::options_open_tests;BindFamily();MessageFamily();FailureFamily();InputFamily();LifecycleFamily();}
