#include "game_inventory_native.h"
#include "game_content_native.h"
#include <algorithm>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void Need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
GameFsStatus GameInventory::Request(std::vector<GameInventoryItem> items,bool home_context){
    if(Modal())return {GameFsCode::Busy,0,"Inventory resource14 already pending"};
    if(items.size()>170)return {GameFsCode::Bounds,0,"Native inventory170-row admission"};
    for(size_t i=0;i<items.size();++i)if(items[i].row!=i)return {GameFsCode::Bounds,0,"Inventory row index/provenance mismatch"};
    const auto generation=state_.generation+1;
    const auto requests=state_.requests+1,retired=state_.retirements;
    state_={};state_.generation=generation;state_.requests=uint32_t(requests);state_.retirements=retired;
    state_.items=std::move(items);state_.requested=state_.request14=true;state_.loader498=2;state_.phase=1;
    home_=home_context;input_.BeginContext();return {};
}
void GameInventory::Rebuild(){
    auto& rows=state_.filtered_rows;rows.clear();
    for(const auto& item:state_.items)if(item.valid&&item.id>=0
        &&(state_.filter==1||state_.filter==5||item.category==int32_t(state_.filter)-2))rows.push_back(item.row);
    //37CB4 descending scalar date. Stable equal-key order is native policy.
    if(state_.filter==5)std::stable_sort(rows.begin(),rows.end(),[&](uint32_t a,uint32_t b){return state_.items.at(a).date_word>state_.items.at(b).date_word;});
    state_.count=uint32_t(rows.size());if(state_.selection>=rows.size())state_.selection=rows.empty()?0:uint32_t(rows.size()-1);
    if(auto* count_control=clone_.Control(6))count_control->frame=state_.count?1:2;
    Select();
}
void GameInventory::Select(){
    state_.selected_row=state_.selected_id=-1;state_.selected_name.clear();state_.selected_description.clear();
    if(state_.selection<state_.filtered_rows.size()){
        state_.selected_row=int32_t(state_.filtered_rows[state_.selection]);
        const auto& item=state_.items.at(size_t(state_.selected_row));state_.selected_id=item.id;
        state_.selected_name=item.name;state_.selected_description=item.description;
    }
}
GameFsStatus GameInventory::Initialize(GameResourceSource& source,GameTransition& transition){
    try{
        auto resource=std::make_shared<GameUiResource>();
        const auto status=resource->Load(source,"mochi\\gui\\mochimono\\MOCHIMONO.CB2A","mochi\\gui\\mochimono\\");
        if(!status)return status;
        Need(clone_.Clone(resource),"Inventory own UI clone");
        for(uint32_t i=0;i<11;++i)Need(clone_.Bind(i,i,i==1?1:0),"Inventory source base pose");
        for(const auto clip:{0u,2u,4u,5u,6u,9u,8u,10u}){
            auto* c=clone_.AddControl(clip);Need(c!=nullptr,"Inventory source control");
            c->frame=clip?1:0;c->mode=clip?2:0;c->active=clip?1:0;
        }
        resource_=std::move(resource);state_.live=state_.owner480=true;state_.loader464=14;
        state_.resource_nodes=uint32_t(resource_->InitialNodes().size());state_.resource_clips=uint32_t(resource_->Clips().size());state_.texture_count=uint32_t(resource_->Textures().size());
        state_.stateB2C=3; //35858 constructor,not open completion
        state_.stateB2C=state_.stateB30=0;state_.filter=1;state_.selection=0;Rebuild();
        Need(clone_.ShowRoot(0)&&clone_.Bind(0,0,0),"Inventory root open");
        auto* control=clone_.Control(0);control->frame=0;control->mode=control->active=1;
        clone_.Control(1)->frame=state_.filter;clone_.Control(6)->frame=state_.count?1:2;
        state_.visible=true;state_.phase=2;++state_.opens;transition_=&transition;
        // Native restart of the existing D54C fade for this actual open request.
        transition.CloseWipe(true);Need(transition.StartGameOpen(),"Inventory source DA64 fade owner");
        return {};
    }catch(const std::exception& e){return {GameFsCode::Unavailable,0,e.what()};}
}
bool GameInventory::Input(const GameInputFrame& frame){
    if(!state_.live||state_.phase!=3)return false;
    input_.Observe(frame);state_.input_focused=frame.focused;return true;
}
GameFsStatus GameInventory::Tick(GameResourceSource& source,GameTransition& transition){
    try{
        if(state_.phase==1)return Initialize(source,transition);
        if(state_.phase==6){ //93504 is deferred;drain before releasing child owners
            clone_.Close();resource_.reset();state_.live=state_.owner480=state_.retirement_pending=false;
            state_.loader464=0;state_.phase=7;++state_.retirements;input_.BeginContext();return {};
        }
        if(!state_.live)return {};
        std::string error;Need(clone_.Update(error),error.c_str());
        if(state_.phase==2&&transition.Snapshot().state==2){state_.stateB2C=1;state_.phase=3;input_.BeginContext();}
        else if(state_.phase==4&&transition.Snapshot().state==0){
            Need(clone_.ShowRoot(0,false),"Inventory close root");auto* control=clone_.Control(0);control->mode=control->active=0;
            state_.visible=false;state_.stateB2C=3;state_.phase=5;++state_.close_completions;
        }else if(state_.phase==3){
            input_.Tick();
            if(state_.actions){
                const uint32_t count=home_?5:3;
                if(input_.Repeat(Down,10,2)){state_.action_selection=(state_.action_selection+1)%count;++state_.navigation;}
                else if(input_.Repeat(Up,10,2)){state_.action_selection=(state_.action_selection+count-1)%count;++state_.navigation;}
                else if(input_.First(Cross)||(input_.First(Circle)&&state_.action_selection==count-1)){
                    state_.select_result=input_.First(Cross)?0:int32_t(count);state_.actions=false;state_.stateB30=0;++state_.action_cancels;input_.BeginContext();
                }else if(input_.First(Circle)){
                    state_.action_frontier=0x1B4C0;return {GameFsCode::Unavailable,0,"Inventory action script events13..16 are not implemented"};
                }
            }else{
                //365F4 priority and first-press-only wrapping at each bound.
                const auto down=input_.Repeat(Down,10,2),up=input_.Repeat(Up,10,2),right=input_.Repeat(Right,10,2),left=input_.Repeat(Left,10,2);
                if(down){if(state_.count){if(state_.selection+1<state_.count)++state_.selection;else if(down==1)state_.selection=0;}++state_.navigation;Select();}
                else if(up){if(state_.count){if(state_.selection)--state_.selection;else if(up==1)state_.selection=state_.count-1;}++state_.navigation;Select();}
                else if(right||left){const auto prior=state_.filter;
                    if(right){if(state_.filter<5)++state_.filter;else if(right==1)state_.filter=1;}
                    else{if(state_.filter>1)--state_.filter;else if(left==1)state_.filter=5;}
                    if(prior!=state_.filter){state_.selection=0;Rebuild();clone_.Control(1)->frame=state_.filter;++state_.navigation;}
                }else if(input_.First(Circle)&&state_.count){state_.actions=true;state_.stateB30=1;state_.action_selection=0;++state_.action_opens;input_.BeginContext();}
                else if(input_.First(Cross)){state_.stateB2C=2;state_.phase=4;++state_.close_requests;transition.CloseWipe(false);input_.BeginContext();}
            }
        }
        if(state_.phase==5){
            //1A9B4:469 and return1 are identical in pending and completed paths.
            state_.loader468=1;state_.loader469=1;state_.loader498=0;state_.request14=state_.requested=false;
            ++state_.completed_polls;state_.retirement_pending=true;state_.phase=6;++state_.retirement_requests;
        }else if(state_.phase>=2&&state_.phase<=4){state_.loader468=0;state_.loader469=1;++state_.pending_polls;}
        return {};
    }catch(const std::exception& e){state_.error=e.what();return {GameFsCode::Unavailable,0,e.what()};}
}
void GameInventory::Close(){
    clone_.Close();resource_.reset();input_.BeginContext();transition_=nullptr;
    state_.live=state_.owner480=state_.visible=state_.actions=state_.requested=state_.request14=state_.retirement_pending=false;
    state_.phase=7;state_.loader464=state_.loader498=0;
}
GameFsStatus GameInventory::Draw(GameGraphics& graphics,const GameFontResource& font){
    if(!state_.live||!state_.visible)return {};
    std::string error;GameUiDrawStats stats;
    if(!clone_.Draw(graphics,stats,error))return {GameFsCode::Unavailable,0,error};
    state_.quads=stats.quads;state_.glyphs=0;
    //NativeListLayout: real CB2A/GIM background and original FONT. Source
    //row identity/text are retained;62px scrolling/callback pixels differ.
    const auto text=[&](const std::vector<std::string>& lines,int x,int y,uint32_t width)->GameFsStatus{
        GameFontBitmap bitmap;const auto status=font.RasterizeCp932(lines,width,bitmap,nullptr,0,0xFF302018);
        if(!status)return status;
        state_.glyphs+=bitmap.glyphs;
        if(!graphics.DrawImage(bitmap.pixels,bitmap.width,bitmap.height,x,y,0xFFFFFFFF,65535))
            return {GameFsCode::Unavailable,0,"Inventory FONT draw"};
        return {};
    };
    //The typed list deliberately does not invent a source item icon mapping.
    if(!graphics.FillRectangle(16,38,464,250,0xFFF5ECD5,65535))return {GameFsCode::Unavailable,0,"Inventory native panel"};
    const auto begin=state_.selection>2?state_.selection-2:0;
    for(uint32_t i=begin;i<state_.count&&i<begin+4;++i){
        const auto y=44+int32_t(i-begin)*29;
        if(i==state_.selection&&!graphics.FillRectangle(22,y-1,458,y+25,0xFFE3C996,65535))return {GameFsCode::Unavailable,0,"Inventory selection"};
        const auto status=text({state_.items.at(state_.filtered_rows[i]).name},30,y,416);if(!status)return status;
    }
    if(!state_.selected_description.empty()){const auto status=text({state_.selected_description},26,174,426);if(!status)return status;}
    if(state_.actions){
        //Exact DA5F4/604/5D0/5E8/5C4 labels; native SELECT layout only.
        const std::vector<std::string> all={"\x82\xA8\x95\x94\x89\xAE\x82\xC9\x82\xA9\x82\xB4\x82\xE9",
            "\x82\xB5\x82\xDC\x82\xC1\x82\xC4\x82\xA8\x82\xAD",
            "\x83\x76\x83\x8C\x83\x5B\x83\x93\x83\x67\x8C\xF0\x8A\xB7\x82\xC9\x8E\x67\x82\xA4",
            "\x82\xB7\x82\xC4\x82\xBF\x82\xE1\x82\xA4","\x82\xE2\x82\xC1\x82\xCF\x83\x84\x83\x81"};
        const auto first=home_?0u:2u;
        if(!graphics.FillRectangle(110,44,468,224,0xFFFDF5DF,65535))return {GameFsCode::Unavailable,0,"Inventory SELECT panel"};
        for(uint32_t i=first;i<all.size();++i){const auto y=52+int32_t(i-first)*30;
            if(i-first==state_.action_selection)graphics.FillRectangle(114,y-1,464,y+25,0xFFE3C996,65535);
            const auto status=text({all[i]},120,y,340);if(!status)return status;
        }
    }
    ++state_.draw_frames;return {};
}
} // namespace dokodemo::recovered
