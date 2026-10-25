#pragma once
#include "recovered/game_text_native.h"
#include <algorithm>

namespace native_port {
using dokodemo::recovered::GameTextKeyGate;
using dokodemo::recovered::GameTextKind;
struct GameTextCharacter { wchar_t value=0; intptr_t info=0; };
struct GameTextKeyReceipt { uint32_t key=UINT32_MAX; uint64_t context=0,epoch=0; };

// The Win32 binding filters this HWND's WM_CHAR. The component driver supplies
// only a queue; it cannot replace the production foreground or owner checks.
template<class Take,class Has,class Dispatch,class Translate,class Deliver,class Discard>
void PumpGameTextKey(GameTextKeyGate& gate,uint32_t key,Take take,Has has,
                     Dispatch dispatch,Translate translate,Deliver deliver,Discard discard) {
    constexpr unsigned bound=128;
    GameTextCharacter character;
    for(unsigned i=0;i<bound&&take(character);++i)discard(character.value);
    const bool empty=!has();
    const auto context=gate.Id(),before=gate.Epoch();
    dispatch();
    translate();
    const auto epoch=gate.Epoch();
    const bool owned=empty&&epoch==before+1&&gate.Id()==context;
    for(unsigned i=0;i<bound&&take(character);++i) {
        if(owned)deliver(character,GameTextKeyReceipt{key,context,epoch});
        else discard(character.value);
    }
}

// Windows virtual-key values. F12 remains a caller-owned presentation action.
template<class Post>
bool DispatchGameTextKey(GameTextKeyGate& gate,uint32_t key,bool repeat,bool composing,Post post) {
    if(!gate.KeyDown(key,repeat)||composing)return false;
    const auto context=gate.Id();
    switch(key) {
    case 0x0D:if(!repeat)post(GameTextKind::Confirm,context);break;
    case 0x1B:if(!repeat)post(GameTextKind::Cancel,context);break;
    case 0x08:post(GameTextKind::Backspace,context);break;
    case 0x2E:post(GameTextKind::Delete,context);break;
    case 0x25:post(GameTextKind::Left,context);break;
    case 0x27:post(GameTextKind::Right,context);break;
    case 0x24:post(GameTextKind::Home,context);break;
    case 0x23:post(GameTextKind::End,context);break;
    case 0x7B:return true;
    }
    return false;
}
struct GameTextDelivery { bool admitted=false,posted=false; };
template<class Commit>
GameTextDelivery DeliverGameTextCharacter(const GameTextKeyGate& gate,wchar_t value,
    const GameTextKeyReceipt& key,bool focused,bool composing,wchar_t& high,Commit commit) {
    GameTextDelivery result;
    result.admitted=!composing&&value>=32&&value!=127&&focused&&
        gate.TranslatedCharacter(key.key,key.context,key.epoch);
    if(!result.admitted)return result;
    if(value>=0xD800&&value<=0xDBFF){high=value;return result;}
    std::wstring text;
    if(high){text.push_back(high);high=0;}
    text.push_back(value);result.posted=commit(text,key.context);
    return result;
}
template<class Sink>
bool SubmitGameTextEvent(GameTextKind kind,uint64_t context,std::string_view text,bool test,Sink sink) {
    dokodemo::recovered::GameTextEvent event;
    event.context=context;event.kind=kind;event.test_injected=test;
    if(text.size()>event.text.size())return false;
    event.bytes=uint16_t(text.size());std::copy(text.begin(),text.end(),event.text.begin());
    return bool(sink(event));
}
template<class Encode,class Post>
bool CommitGameTextUnicode(std::wstring_view value,uint64_t context,Encode encode,Post post) {
    std::string bytes,error;
    if(!encode(value,bytes,error)){post(GameTextKind::InvalidEncoding,context,std::string_view{});return false;}
    return post(GameTextKind::Commit,context,bytes);
}
}
