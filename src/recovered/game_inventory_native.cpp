#include "game_inventory_native.h"
#include "core_hash.h"
#include "game_content_native.h"
#include <algorithm>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
void Need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}
std::array<uint8_t,32> GameInventoryItemsHash(const std::vector<GameInventoryItem>& items){
    std::vector<uint8_t> bytes;
    const auto word=[&](uint32_t n){for(uint32_t i=0;i<4;++i)bytes.push_back(uint8_t(n>>(8*i)));};
    const auto text=[&](const std::string& s){word(uint32_t(s.size()));bytes.insert(bytes.end(),s.begin(),s.end());};
    word(uint32_t(items.size()));for(const auto& i:items){word(i.row);word(uint32_t(i.id));word(uint32_t(i.category));word(i.valid?1u:0u);word(i.date_word);
        for(auto x:i.source_words)word(x);for(auto x:i.source_kinds)word(x);for(auto x:i.control_words)word(x);for(auto x:i.control_kinds)word(x);text(i.name);text(i.description);}
    return core::sha256(bytes.data(),bytes.size());
}
uint32_t GameInventoryActionCount(GameInventoryOrigin origin,bool home,bool placement)noexcept{return origin==GameInventoryOrigin::MainMenu5?(home&&placement?4u:3u):(home?5u:3u);}
GameInventoryActionDescriptor GameInventoryActionAt(GameInventoryOrigin origin,bool home,uint32_t index,bool placement)noexcept{
    static constexpr const char* labels[]={"\x82\xA8\x95\x94\x89\xAE\x82\xC9\x82\xA9\x82\xB4\x82\xE9",
        "\x82\xB5\x82\xDC\x82\xC1\x82\xC4\x82\xA8\x82\xAD",
        "\x83\x76\x83\x8C\x83\x5B\x83\x93\x83\x67\x8C\xF0\x8A\xB7\x82\xC9\x8E\x67\x82\xA4",
        "\x82\xB7\x82\xC4\x82\xBF\x82\xE1\x82\xA4","\x82\xE2\x82\xC1\x82\xCF\x83\x84\x83\x81"};
    static constexpr uint32_t events[]={13,14,15,16,0},finite[]={2,1,4},placed[]={0,1,2,4};
    if(index>=GameInventoryActionCount(origin,home,placement))return {};
    // Named WindowsFiniteMenuMarkProjection: source labels/results retained,
    // visible order contains only admitted15/14/Cancel for this origin.
    const auto source=origin==GameInventoryOrigin::MainMenu5?(home&&placement?placed[index]:finite[index]):index+(home?0u:2u);
    return {events[source],origin==GameInventoryOrigin::MainMenu5?source+1:index+1,labels[source]};
}
GameFsStatus GameInventory::Request(std::vector<GameInventoryItem> items,bool home_context,GameInventoryActionDispatch dispatch,GameInventoryOrigin origin,bool placement,GameInventoryCompletionVerify verify){
    if(origin!=GameInventoryOrigin::Script41&&origin!=GameInventoryOrigin::MainMenu5)return {GameFsCode::Bounds,0,"Inventory request origin unavailable"};
    if(Modal())return {GameFsCode::Busy,0,"Inventory resource14 already pending"};
    if(origin==GameInventoryOrigin::MainMenu5&&placement&&(!home_context||!verify))return {GameFsCode::Unavailable,0,"Placement inventory requires home and owning completion verifier"};
    if(items.size()>170)return {GameFsCode::Bounds,0,"Native inventory170-row admission"};
    for(size_t i=0;i<items.size();++i)if(items[i].row!=i)return {GameFsCode::Bounds,0,"Inventory row index/provenance mismatch"};
    const auto generation=state_.generation+1;
    const auto requests=state_.requests+1,retired=state_.retirements;
    discard_dialog_.Retire();state_={};state_.generation=generation;state_.requests=uint32_t(requests);state_.retirements=retired;
    state_.origin=origin;state_.items=std::move(items);state_.requested=state_.request14=true;
    state_.placement_enabled=origin==GameInventoryOrigin::MainMenu5&&home_context&&placement;
    state_.loader498=origin==GameInventoryOrigin::MainMenu5?0:2;state_.phase=1;
    home_=home_context;dispatch_=std::move(dispatch);verify_=std::move(verify);input_.BeginContext();return {};
}
GameFsStatus GameInventory::CompleteAction(uint64_t generation,uint64_t receipt,std::vector<GameInventoryItem> items,const GameInventoryActionCertificate* certificate){
    if(!state_.live||state_.phase!=3||!state_.action_pending||generation!=state_.generation||receipt!=state_.action_receipt)
        return {GameFsCode::Unavailable,0,"Inventory action completion owner/receipt changed"};
    if(items.size()!=state_.items.size())return {GameFsCode::Bounds,0,"Inventory action row set replaced"};
    for(size_t i=0;i<items.size();++i)if(items[i].row!=i)return {GameFsCode::Bounds,0,"Inventory action row identity"};
    if(state_.origin==GameInventoryOrigin::MainMenu5){
        if(state_.action_row<0||size_t(state_.action_row)>=state_.items.size())return {GameFsCode::Bounds,0,"Menu action row extent"};
        const bool placement=state_.action_event==13||(state_.action_event==14&&state_.items.at(size_t(state_.action_row)).source_words[1]!=0);
        if(placement){
            if(!state_.placement_enabled||!certificate||!certificate->valid||certificate->generation!=generation||certificate->receipt!=receipt
               ||certificate->event!=state_.action_event||certificate->row!=uint32_t(state_.action_row)
               ||certificate->before_display!=GameInventoryItemsHash(state_.items)||certificate->after_display!=GameInventoryItemsHash(items)
               ||certificate->approved_image==std::array<uint8_t,32>{}||!verify_||!verify_(*certificate))return {GameFsCode::Unavailable,0,"Placement refresh lacks actor receipt/image certificate"};
            for(size_t i=0;i<items.size();++i){const auto& old=state_.items[i];const auto& n=items[i];const auto mask=certificate->mutable_fields[i];
                if(n.id!=old.id||n.valid!=old.valid||n.date_word!=old.date_word||n.name!=old.name||n.description!=old.description
                   ||n.control_words[0]!=old.control_words[0]||n.control_kinds[0]!=old.control_kinds[0]||(mask&~0x3E0u))
                    return {GameFsCode::Unavailable,0,"Placement refresh protected display identity"};
                for(uint32_t f=5;f<=9;++f){const auto at=f<=7?f-5:f-7;
                    const auto word=f<=7?n.source_words[at]:n.control_words[at],prior=f<=7?old.source_words[at]:old.control_words[at];
                    const auto kind=f<=7?n.source_kinds[at]:n.control_kinds[at],prior_kind=f<=7?old.source_kinds[at]:old.control_kinds[at];
                    if(!(mask&(1u<<f))&&(word!=prior||kind!=prior_kind))return {GameFsCode::Unavailable,0,"Placement refresh unapproved field delta"};
                }
                const auto category=(n.source_words[0]&0xFFFFFFF)==1?2:((n.source_words[2]>>28)==0?1:0);
                if(n.valid&&n.category!=category)return {GameFsCode::Unavailable,0,"Placement refresh category mismatch"};
            }
        }else{
        if((state_.action_event!=14&&state_.action_event!=15)||state_.action_row<0||size_t(state_.action_row)>=items.size())
            return {GameFsCode::Unavailable,0,"Menu mark completion event/row replaced"};
        for(size_t i=0;i<items.size();++i){auto expected=state_.items[i];
            if(i==size_t(state_.action_row)){expected.source_words[0]=state_.action_event==15?0x10000001u:0x10000000u;
                expected.source_kinds[0]=uint8_t(script::Value::Kind::Scalar);expected.category=state_.action_event==15?2:((expected.source_words[2]>>28)==0?1:0);}
            const auto& actual=items[i];
            if(actual.row!=expected.row||actual.id!=expected.id||actual.valid!=expected.valid||actual.date_word!=expected.date_word
               ||actual.name!=expected.name||actual.description!=expected.description||actual.source_words!=expected.source_words
               ||actual.source_kinds!=expected.source_kinds||actual.control_words!=expected.control_words||actual.control_kinds!=expected.control_kinds
               ||actual.category!=expected.category)return {GameFsCode::Unavailable,0,"Menu mark GUI refresh changed protected item identity/data"};
        }
        }
    }
    state_.items=std::move(items);state_.action_pending=false;++state_.action_completions;
    Rebuild();input_.BeginContext();return {};
}
GameFsStatus GameInventory::DispatchAction(uint32_t event,int32_t row){
    if(state_.origin==GameInventoryOrigin::MainMenu5){
        if(row<0||size_t(row)>=state_.items.size()||(event!=13&&event!=14&&event!=15)
           ||state_.items[size_t(row)].source_kinds[1]!=uint8_t(script::Value::Kind::Scalar)
           ||(event==13&&!state_.placement_enabled)||(state_.items[size_t(row)].source_words[1]!=0&&(!state_.placement_enabled||event!=14))){
            ++state_.unsupported_actions;input_.BeginContext();return {};
        }
    }
    if(!dispatch_)return {GameFsCode::Unavailable,0,"Inventory source action dispatcher absent"};
    const auto admitted=dispatch_(state_.generation,event,row);
    state_.action_queue_result=admitted.queue_result;input_.BeginContext();
    if(!admitted.status)return admitted.status;
    if(admitted.queue_result==1){state_.actions=true;state_.stateB30=1;return {};}
    if(admitted.queue_result!=0||!admitted.receipt)return {GameFsCode::Unavailable,0,"Inventory action queue did not accept its owner"};
    state_.select_result=int32_t(GameInventoryActionAt(state_.origin,home_,state_.action_selection,state_.placement_enabled).source_result);state_.action_pending=true;state_.action_receipt=admitted.receipt;state_.action_event=event;
    state_.action_row=row;++state_.action_events;state_.actions=false;state_.stateB30=0;
    return {};
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
    if(!state_.live||state_.phase!=3||state_.action_pending)return false;
    state_.input_focused=frame.focused;
    if(state_.discard_confirmation){
        if(frame.overflow_resync){auto lost=frame;lost.focused=false;lost.buttons=0;(void)discard_dialog_.Input(lost);return false;}
        return discard_dialog_.Input(frame);
    }
    input_.Observe(frame);return true;
}
GameFsStatus GameInventory::Tick(GameResourceSource& source,GameTransition& transition){
    try{
        if(state_.phase==1)return Initialize(source,transition);
        if(state_.phase==6){ //93504 is deferred;drain before releasing child owners
            clone_.Close();resource_.reset();state_.live=state_.owner480=state_.retirement_pending=false;
            state_.loader464=0;state_.phase=7;state_.retired_generation=state_.generation;++state_.retirements;input_.BeginContext();return {};
        }
        if(!state_.live)return {};
        std::string error;Need(clone_.Update(error),error.c_str());
        if(state_.phase==2&&transition.Snapshot().state==2){state_.stateB2C=1;state_.phase=3;input_.BeginContext();}
        else if(state_.phase==4&&transition.Snapshot().state==0){
            Need(clone_.ShowRoot(0,false),"Inventory close root");auto* control=clone_.Control(0);control->mode=control->active=0;
            state_.visible=false;state_.stateB2C=3;state_.phase=5;++state_.close_completions;
        }else if(state_.phase==3&&!state_.action_pending){
            if(state_.discard_confirmation){
                discard_dialog_.Tick();const auto d=discard_dialog_.Snapshot();
                state_.discard_dialog_state=d.state;state_.discard_result=d.result;state_.discard_progress=d.progress;
                if(d.state==3){
                    state_.discard_confirmation=false;input_.BeginContext();
                    Need(state_.discard_row==state_.selected_row&&state_.discard_item_id==state_.selected_id
                         &&state_.selected_row>=0&&state_.items.at(size_t(state_.selected_row)).valid,"Discard confirmation row owner changed");
                    if(d.result==1){++state_.discard_accepted;const auto status=DispatchAction(16,state_.discard_row);if(!status)return status;}
                    else{++state_.discard_cancelled;state_.actions=true;state_.stateB30=1;}
                }
            }else{
            input_.Tick();
            if(state_.actions){
                const uint32_t count=GameInventoryActionCount(state_.origin,home_,state_.placement_enabled);
                if(input_.Repeat(Down,10,2)){state_.action_selection=(state_.action_selection+1)%count;++state_.navigation;}
                else if(input_.Repeat(Up,10,2)){state_.action_selection=(state_.action_selection+count-1)%count;++state_.navigation;}
                else if(input_.First(Cross)||(input_.First(Circle)&&state_.action_selection==count-1)){
                    state_.select_result=input_.First(Cross)?0:int32_t(GameInventoryActionAt(state_.origin,home_,count-1,state_.placement_enabled).source_result);state_.actions=false;state_.stateB30=0;++state_.action_cancels;input_.BeginContext();
                }else if(input_.First(Circle)){
                    const auto event=GameInventoryActionAt(state_.origin,home_,state_.action_selection,state_.placement_enabled).event;
                    if(event==16&&state_.origin==GameInventoryOrigin::Script41){
                        Need(discard_dialog_.Begin(0,0),"Inventory TRC confirmation already active");
                        state_.discard_row=state_.selected_row;state_.discard_item_id=state_.selected_id;
                        state_.discard_confirmation=true;state_.discard_dialog_state=0;state_.discard_result=0;state_.discard_progress=0;
                        ++state_.discard_prompts;state_.actions=false;state_.stateB30=2;input_.BeginContext();
                    }else{const auto status=DispatchAction(event,state_.selected_row);if(!status)return status;}
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
                }else if(input_.First(Circle)&&state_.count){
                    if(state_.origin==GameInventoryOrigin::MainMenu5&&!dispatch_){++state_.unsupported_actions;input_.BeginContext();}
                    else{state_.actions=true;state_.stateB30=1;state_.action_selection=0;++state_.action_opens;input_.BeginContext();}
                }
                else if(input_.First(Cross)){state_.stateB2C=2;state_.phase=4;++state_.close_requests;transition.CloseWipe(false);input_.BeginContext();}
            }
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
    discard_dialog_.Retire();state_.discard_confirmation=false;state_.discard_dialog_state=3;
    clone_.Close();resource_.reset();input_.BeginContext();transition_=nullptr;dispatch_={};verify_={};state_.action_pending=false;
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
        // Source labels and finite origin mapping are shared with input.
        const auto count=GameInventoryActionCount(state_.origin,home_,state_.placement_enabled);
        if(!graphics.FillRectangle(110,44,468,224,0xFFFDF5DF,65535))return {GameFsCode::Unavailable,0,"Inventory SELECT panel"};
        for(uint32_t i=0;i<count;++i){const auto y=52+int32_t(i)*30;
            if(i==state_.action_selection)graphics.FillRectangle(114,y-1,464,y+25,0xFFE3C996,65535);
            const auto status=text({GameInventoryActionAt(state_.origin,home_,i,state_.placement_enabled).label},120,y,340);if(!status)return status;
        }
    }
    if(state_.discard_confirmation){
        //NativeInventoryTRCXYesZNo: retained DA62C text, native TRC timing;
        //source default focus/physical mapping/CB2A pixels are not claimed.
        if(!graphics.FillRectangle(62,80,454,172,0xFFFDF5DF,65535))return {GameFsCode::Unavailable,0,"Inventory TRC native panel"};
        const auto prompt=text({"\x96\x7B\x93\x96\x82\xC9\x82\xB7\x82\xC4\x82\xDC\x82\xB7\x82\xA9\x81\x48"},76,91,340);if(!prompt)return prompt;
        const auto buttons=text({"\x82\xCD\x82\xA2 (X)    \x82\xA2\x82\xA2\x82\xA6 (Z)"},76,127,340);if(!buttons)return buttons;
    }
    ++state_.draw_frames;return {};
}
} // namespace dokodemo::recovered
