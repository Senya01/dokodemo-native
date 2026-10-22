#include "game_system_option_native.h"
#include <stdexcept>

namespace dokodemo::recovered {
GameFsStatus GameSystemOptionFrontier::Request(GameMainMenuLease lease,std::array<uint32_t,3> settings){
    if(state_.requested||state_.failed||!lease.script_generation||!lease.run_id||!lease.available_owner
       ||!lease.available_generation||!lease.message_context||!lease.input_epoch)
        return {GameFsCode::Unavailable,0,"System resource10 request owner unavailable"};
    const auto old=state_;state_={};state_.generation=old.generation+1;state_.requests=old.requests+1;
    state_.requested=true;state_.phase=1;state_.lease=lease;state_.passive_settings=settings;return {};
}
GameFsStatus GameSystemOptionFrontier::Refuse(GameFsStatus status){
    if(!state_.failed){state_.failed=true;state_.failure=std::move(status);}
    return state_.failure;
}
GameFsStatus GameSystemOptionFrontier::Advance(GameResourceSource& source){
    if(state_.failed)return state_.failure;
    if(!state_.requested||state_.phase!=1)return {GameFsCode::Unavailable,0,"System initializer has no request"};
    ++state_.initializer_attempts;state_.constructor_entered=true;
    try{
        auto image=std::make_shared<GameUiResource>();
        auto status=image->Load(source,"option\\gui\\option\\OPTION.CB2A","option\\gui\\option\\");
        if(!status)return Refuse(status);
        GameUiInstance copy;
        if(!copy.Clone(image))throw std::runtime_error("System constructor clone refused");
        //347C8 exact source constructor binding order and frames.
        for(uint32_t root:{0u,1u,3u})if(!copy.Bind(root,root,0))throw std::runtime_error("System constructor authored root binding");
        for(uint32_t root:{2u,4u})if(!copy.Bind(root,root,1))throw std::runtime_error("System constructor static root binding");
        if(!copy.ShowRoot(0,false))throw std::runtime_error("System constructor hidden root");
        for(uint32_t clip=0;clip<3;++clip){
            auto* c=copy.AddControl(clip);if(!c)throw std::runtime_error("System constructor control");
            if(clip){c->frame=1;c->mode=2;c->active=1;}
        }
        // Source stores direct node pointers while root0 is hidden. A Draw
        // world traversal intentionally cannot find descendants of that root.
        for(uint32_t node:{2u,97u,98u,99u})if(!copy.Node(node))throw std::runtime_error("System constructor source node owner");
        resource_=std::move(image);clone_=std::move(copy);
        state_.template_owner=state_.clone_owner=true;state_.source_consumer=0x153D4;state_.phase=2;
        //34A08 constructs a distinct9F0 shared message owner. Its template,
        // font/refcount, text-update and deleting lifetimes are not implemented.
        // Do not substitute the scene dialog, claim Open or post11/18/wake.
        return Refuse({GameFsCode::Unavailable,0,"Unsupported System resource10 owned message constructor153D4 at34A08"});
    }catch(const std::exception& e){return Refuse({GameFsCode::Unavailable,0,e.what()});}
}
void GameSystemOptionFrontier::Abort()noexcept{
    clone_.Close();resource_.reset();state_.template_owner=state_.clone_owner=false;
    state_.requested=false; // Native failed-construction teardown, not93504/poll.
}
}
