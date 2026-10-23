#pragma once
#include "game_options_edit_regressions.h"

// ROOT supplement: stop at the real final-LF countdown edge, rather than
// setting complete68 or injecting a rate/pose into the component.
inline void GameOptionsEditCompletionEdgeChecks(){
    using namespace dokodemo::options_edit_tests;
    Rig r;
    const auto edge=[](const auto& s){return s.message.state64==2&&!s.message.complete68
        &&s.message.total&&s.message.shown==s.message.total&&s.message.delay70==1;};
    const auto queued_lf=[](const auto& s){return s.message.state64==2&&!s.message.complete68
        &&s.message.total&&s.message.shown==s.message.total
        &&s.message.delay70==(s.message.rate_setting==2?10:7);};
    // shown==total alone also holds after the last ordinary glyph. First
    // witness its distinct source LF delay, then count that same delay to1.
    r.Until(queued_lf,"actual row2 LF queued");
    r.Until(edge,"edge before Right");
    const auto before=r.front.Snapshot();
    r.Send(Right);
    const auto right=r.front.Snapshot();
    C(right.gain_returned&&right.message.complete68,"edge Right genuine return and natural completion");
    C(right.message.text_calls==before.message.text_calls&&right.message.rate_updates==before.message.rate_updates
        &&right.message.rate_setting==2,"edge Right does not eagerly service new complete68");
    r.Send(0);
    const auto serviced=r.front.Snapshot();
    C(serviced.message.rate_setting==3&&serviced.message.rate_updates==right.message.rate_updates+1
        &&serviced.message.text_calls==right.message.text_calls+1&&!serviced.message.complete68,
        "edge next ordinary invocation services actual complete68 once");
    r.Until(queued_lf,"actual row3 LF queued");
    r.Until(edge,"edge before Cancel");
    const auto cancel_before=r.front.Snapshot();
    r.Send(Cross);
    const auto cancel=r.front.Snapshot();
    C(cancel.phase==4&&cancel.restore_writes==3&&cancel.settings.writes==7&&cancel.message.complete68,
        "edge Cancel restores actual changed backup then naturally completes");
    C(cancel.settings.words==std::array<U,3>{{2,3,3}}&&cancel.current_words==std::array<U,3>{{3,3,3}}
        &&cancel.message.rate_setting==3&&cancel.message.rate_updates==cancel_before.message.rate_updates
        &&cancel.message.text_calls==cancel_before.message.text_calls,
        "edge Cancel has no eager helper and keeps local C distinct from restored R");
    const auto updates=cancel.message.updates;
    r.Tick();
    C(r.front.Snapshot().message.updates==updates&&r.front.Snapshot().message.text_calls==cancel.message.text_calls,
        "edge closing phase does not invent another message service");
    Drain(r);
}

inline void GameOptionsManualBarWorkingPoseChecks(){
    using namespace dokodemo::options_edit_tests;
    Ui image(100,5,5);image.Link(2,0,96);
    // One source-style position track: constructor base plus two distinct
    // relative authored poses. A frame receipt alone cannot prove this moved.
    const U keys=image.Alloc(8),values=image.Alloc(48);
    Put(image.b,image.clips+2*52+3*8,1);Put(image.b,image.clips+2*52+3*8+4,keys);
    Put(image.b,keys,96,2);Put(image.b,keys+2,3,2);Put(image.b,keys+4,values);
    for(U i=0;i<3;++i){Float(image.b,values+i*16,i?float(10+10*i):10.0f);
        Float(image.b,values+i*16+4,i?0.0f:5.0f);Put(image.b,values+i*16+8,1);Put(image.b,values+i*16+12,i?7+i:0);}
    struct ManualSource final:GameResourceSource{
        Bytes bytes;
        GameFsStatus OpenResource(const std::string& p,PackageView& out)override{
            if(p=="option\\gui\\option\\OPTION.CB2A"){out=View(bytes);return {};}
            if(p=="option\\gui\\option\\options-test.GIM"){out=View(Gim());return {};}
            return {GameFsCode::NotFound,0,"manual test resource"};
        }
    } source;source.bytes=std::move(image.b);
    auto resource=std::make_shared<GameUiResource>();Ok(resource->Load(source,"option\\gui\\option\\OPTION.CB2A","option\\gui\\option\\"),"manual resource");
    GameUiInstance clone;C(clone.Clone(resource)&&clone.Bind(2,2,1),"manual constructor base");
    auto* child=clone.Node(96);auto* unrelated=clone.Node(95);
    C(child&&unrelated&&child->base_position==std::array<float,2>{{10,5}},"manual independent child base");
    unrelated->position={{55,66}};child->position={{-999,-999}};
    GameUiControl control{8,2,2,1};std::string error;
    C(clone.SampleControl(2,control,error)&&child->position==std::array<float,2>{{30,5}},"manual frame8 reaches working descendant");
    control.frame=9;
    C(clone.SampleControl(2,control,error)&&child->position==std::array<float,2>{{40,5}},"manual frame9 moves working descendant");
    C(child->base_position==std::array<float,2>{{10,5}}&&unrelated->position==std::array<float,2>{{55,66}},"manual preserves base and other subtree");
    control.frame=8;
    C(clone.SampleControl(2,control,error)&&child->position==std::array<float,2>{{30,5}},"manual sample does not accumulate");
    control.mode=3;const auto stable=child->position;
    C(!clone.SampleControl(2,control,error)&&child->position==stable,"manual unsupported mode leaves working state");
    // The native joined renderer must not add a fourth automatic copy of
    // the root owned by the three source manual passes. Other roots and
    // the manual root's visible/pose state remain intact.
    clone.Node(0)->texture=1;clone.Node(2)->texture=1;
    // The actual OPTION root0 anchors97/98/99 reference root2. Exercise
    // that separate path as well as the automatic root2 loop entry.
    clone.Node(0)->encoded_initial[1]=95;clone.Node(95)->texture=-3;
    for(U root=0;root<5;++root)clone.ShowRoot(root,root==0||root==2);
    GameGraphics graphics;C(graphics.Initialize({}),"manual joined graphics");
    GameUiDrawStats automatic,joined,manual;
    C(clone.Draw(graphics,automatic,error)&&automatic.quads==3,"manual ordinary automatic roots and reference");
    C(clone.Draw(graphics,joined,error,nullptr,{},0,UINT32_MAX,-1,2)&&joined.quads==1,
        "manual joined pass excludes exactly manual root2");
    C(clone.Draw(graphics,manual,error,nullptr,{},0,UINT32_MAX,2,2)&&manual.quads==1
        &&clone.Node(2)->visible&&child->position==stable,
        "manual explicit root2 still draws without visibility or pose mutation");
}
