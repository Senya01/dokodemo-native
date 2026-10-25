#pragma once
#include "game_text_input.h"
#include "game_text_input_win32.h"
#include "game_window_input.h"
#include "game_text_win32.h"
#include "recovered/game_input_native.h"
#include "recovered/game_profile_native.h"
#include <deque>
#include <functional>
#include <stdexcept>
#include <vector>
#include <utility>

namespace game_text_transaction_tests {
using namespace dokodemo::recovered;
inline void Need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
// No HWND, keyboard polling, message posting or desktop access. Only the queue
// and translation output are modeled. Transaction/commands/encoding/mailbox/
// editor/profile below are the production code. This is not a GUI witness.
struct Driver {
    enum Kind { Down,Up,Character };
    struct Message {Kind kind;uint32_t key=0;wchar_t value=0;std::wstring text;bool repeat=false;};
    GameTextKeyGate gate;
    GameNameEntry editor;
    GameTextMailbox mailbox;
    std::deque<Message> queue;
    std::vector<char> order;
    std::vector<unsigned> delivered_per_key;
    std::function<void()> after_dispatch,after_translate,after_character;
    bool focused=true,composing=false,accept_sink=true;
    wchar_t high=0;
    unsigned discarded=0,delivered=0,posted=0,refused=0,dispatches=0,translations=0,taken=0,max_taken=0;
    Driver(){
        Need(editor.Begin("",4),"component editor opens");editor.ControlCompleted(2);
        gate.Context(editor.Snapshot().context,true);
        Need(Post(GameTextKind::FocusGained,gate.Id()),"component focus event");Flush();
    }
    bool Post(GameTextKind kind,uint64_t context,std::string_view text={}) {
        return native_port::SubmitGameTextEvent(kind,context,text,false,[&](const GameTextEvent& event){
            Need(!event.test_injected,"ordinary producer never relabels input as test injection");
            return accept_sink&&mailbox.Push(event);
        });
    }
    bool Commit(std::wstring_view value,uint64_t context){
        return native_port::CommitGameTextUnicode(value,context,dokodemo::EncodeGameText932,
            [&](auto kind,auto owner,std::string_view bytes){return Post(kind,owner,bytes);});
    }
    void Flush(){GameTextEvent e;while(mailbox.Pop(e))if(!editor.Apply(e))++refused;}
    void Key(uint32_t key,std::wstring text={},bool repeat=false){
        Message m{Down,key};m.text=std::move(text);m.repeat=repeat;queue.push_back(std::move(m));
        queue.push_back({Up,key}); // key-up is already queued when translation appends WM_CHAR
    }
    void OldCharacters(unsigned count){while(count--)queue.push_back({Character,0,L'X'});}
    bool Take(native_port::GameTextCharacter& out){
        for(auto i=queue.begin();i!=queue.end();++i)if(i->kind==Character){
            out={i->value,0};queue.erase(i);++taken;return true;
        }
        return false;
    }
    bool Has()const{for(const auto& m:queue)if(m.kind==Character)return true;return false;}
    void Deliver(wchar_t value,native_port::GameTextKeyReceipt receipt={}){
        order.push_back('C');++delivered;
        const auto r=native_port::DeliverGameTextCharacter(gate,value,receipt,focused,composing,high,
            [&](std::wstring_view text,uint64_t context){return Commit(text,context);});
        if(r.posted)++posted;
        if(after_character)after_character();
    }
    void Run(){
        unsigned budget=4096;
        while(!queue.empty()){
            Need(budget--!=0,"bounded component message driver");
            auto m=std::move(queue.front());queue.pop_front();
            if(m.kind==Up){order.push_back('U');gate.KeyUp(m.key);}
            else if(m.kind==Character)Deliver(m.value);
            else {
                const auto dispatch=[&]{order.push_back('D');++dispatches;
                    (void)native_port::DispatchGameTextKey(gate,m.key,m.repeat,composing,
                        [&](auto kind,auto context){return Post(kind,context);});
                    if(after_dispatch)after_dispatch();};
                const auto translate=[&]{order.push_back('T');++translations;
                    for(auto c:m.text)queue.push_back({Character,0,c});
                    if(after_translate)after_translate();};
                const auto before=delivered;taken=0;
                if(gate.Id())native_port::PumpGameTextKey(gate,m.key,
                    [&](auto& c){return Take(c);},[&]{return Has();},dispatch,translate,
                    [&](const auto& c,const auto& receipt){Deliver(c.value,receipt);},
                    [&](wchar_t){order.push_back('R');high=0;++discarded;});
                else {translate();dispatch();} // ordinary non-editor pump order
                delivered_per_key.push_back(delivered-before);
                if(taken>max_taken)max_taken=taken;
            }
            Flush();
        }
    }
};
inline void AkiAndEnter(){
    Driver d;d.Key(0xE7,L"A");d.Key('K',L"k");d.Key('I',L"i");d.Key(0x0D,L"\r");d.Run();
    Need(d.order.size()>=4&&std::string(d.order.begin(),d.order.begin()+4)=="DTCU",
         "production dispatch->translate->owned char happens before queued key-up");
    Need(d.dispatches==4&&d.translations==4&&d.posted==3,"each key dispatch/translation once; Enter char not text");
    auto s=d.editor.Snapshot();
    Need(s.staged=="Aki"&&s.source120=="Aki"&&s.units==3&&s.source_state==2&&!d.editor.Result(),
         "production Aki commits and Enter begins close without manufacturing result");
    d.editor.ControlCompleted(1);Need(!d.editor.Result(),"mode1 is not close completion");
    d.editor.ControlCompleted(2);
    GameProfile profile;profile.Nickname("Cat");profile.Begin(GameProfileField::Player);std::string error;
    Need(profile.Result(d.editor.Result(),2026,error)&&profile.Snapshot().player=="Aki"&&
         profile.Snapshot().present==2&&!profile.Snapshot().applied,"actual component player result, not room/game completion");
}
inline void QueueBounds(){
    for(unsigned n:{128u,129u,256u}){
        Driver d;d.Key('A',L"A");d.OldCharacters(n);d.Key('B',L"B");d.Run();
        Need(d.editor.Snapshot().staged==(n==128?"AB":"B"),"old/backlogged chars cannot acquire the next key");
        Need(d.max_taken<=256&&d.dispatches==2&&d.translations==2,"both drains bounded128, keys exactly once");
    }
    Driver d;d.queue.push_back({Driver::Character,0,L'X'});d.Run();
    Need(d.posted==0&&d.editor.Snapshot().staged.empty(),"unassociated raw character has no fallback admission");
    d.Key('Q',std::wstring(129,L'Q'));d.Key(0x0D,L"\r");d.Run();
    Need(d.delivered_per_key[0]==128&&d.discarded==1&&d.editor.Snapshot().source120=="QQQQ",
         "translation bound128; leftover cannot become Enter text; real field limit remains4");
    Need(d.refused==124,"capacity refusals occur in actual editor, not fabricated producer success");
}
inline void ReceiptInvalidation(){
    for(unsigned which=0;which<6;++which){
        Driver d;const auto context=d.gate.Id();
        d.after_translate=[&]{
            switch(which){
            case 0:d.gate.Context(context+1,true);break;
            case 1:d.focused=false;d.gate.Focus(false);break;
            case 2:d.gate.KeyDown('Z',false);break;
            case 3:d.gate.KeyUp('A');break;
            case 4:d.gate.Context(context+1,true);d.gate.Context(context,true);break;
            case 5:d.gate.Focus(false);d.gate.Focus(true);d.gate.KeyDown('A',false);break;
            }
        };
        d.Key('A',L"A");d.Run();
        Need(d.posted==0&&d.discarded==1&&d.editor.Snapshot().staged.empty(),"context/epoch/focus/nested key invalidates transaction");
    }
    Driver mid;mid.after_character=[&]{mid.gate.KeyUp('A');};mid.Key('A',L"AB");mid.Run();
    Need(mid.editor.Snapshot().staged=="A"&&mid.posted==1,"each delivered character rechecks receipt");
    Driver foreign;Need(foreign.Commit(L"X",foreign.gate.Id()+1),"component mailbox transport is not editor acceptance");foreign.Flush();
    Need(foreign.refused==1&&foreign.editor.Snapshot().staged.empty(),"actual editor rejects foreign context");
    Driver retired;retired.editor.Retire();retired.gate.Context(0,true);retired.Key('A',L"A");retired.Run();
    Need(retired.posted==0&&!retired.editor.Result(),"retired editor cannot adopt queued key chars");
}
inline void CommandsAndRefusals(){
    Driver d;d.Key('A',L"A");d.Run();
    d.Key(0x0D,L"\r",true);d.Run();Need(d.editor.Snapshot().source_state==1,"unadmitted/repeated Enter never confirms");
    d.composing=true;d.Key(0x0D,L"\r");d.Key('B',L"B");d.Run();
    Need(d.editor.Snapshot().staged=="A"&&d.editor.Snapshot().source_state==1,"composition blocks text and commands");
    d.composing=false;d.focused=false;d.gate.Focus(false);d.Key('C',L"C");d.Key(0x0D,L"\r");d.Run();
    Need(d.editor.Snapshot().staged=="A"&&d.editor.Snapshot().source_state==1,"foreground/focus refusal preserved");
    Driver invalid;invalid.Key(0xE7,L"\xD83D\xDC31");invalid.Run();
    Need(invalid.posted==0&&invalid.refused==1&&invalid.editor.Snapshot().staged.empty(),"actual CP932 rejects unsupported scalar atomically");
    Driver sink;sink.accept_sink=false;sink.Key('A',L"A");sink.Run();
    Need(sink.posted==0&&sink.editor.Snapshot().staged.empty(),"failed sink is not an accepted edit");
}
inline void ControllerIsolation(){
    native_port::GameWindowButtons keys;ControllerState controller;
    Need(keys.Update(Circle,true,false,true,false),"actual window state accepts focused down");
    auto sample=controller.Update(native_port::GameWindowControllerButtons(Down,keys.Mask(),true,false),128,128,1);
    Need(sample.buttons==(Circle|Down),"production aggregate retains held physical and window keys");
    keys.FocusLost();keys.Update(Circle,true,false,false,false);
    sample=controller.Update(native_port::GameWindowControllerButtons(Down,keys.Mask(),false,false),128,128,2);
    Need(sample.buttons==0&&sample.released==(Circle|Down),"focus release clears both sources");
    keys.Update(Down,true,false,true,false);
    Need(native_port::GameWindowControllerButtons(0,keys.Mask(),true,false)==Down,"refocus Down does not revive Circle");
    Need(!keys.Update(Circle,true,false,true,true),"editor down cannot enter window mask");
    sample=controller.Update(native_port::GameWindowControllerButtons(Circle|Start,keys.Mask(),true,true),128,128,3);
    Need(sample.buttons==0,"real shared sampler suppresses physical/window game buttons during editor");
    keys.Update(Down,false,false,true,true);Need(keys.Mask()==0,"editor still releases previous game key");
}
struct MessageOnlyWindow {
    Driver driver;
    HWND window=nullptr;
    HINSTANCE module=GetModuleHandleW(nullptr);
    static constexpr const wchar_t* name=L"DokoHeadlessTextTransaction";
    unsigned dispatched=0,keyups=0,translated=0,discarded=0;
    static LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM key,LPARAM info){
        auto* self=reinterpret_cast<MessageOnlyWindow*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(message==WM_NCCREATE){self=static_cast<MessageOnlyWindow*>(reinterpret_cast<CREATESTRUCTW*>(info)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(self&&message==WM_KEYDOWN){++self->dispatched;
            (void)native_port::DispatchGameTextKey(self->driver.gate,uint32_t(key),false,false,
                [&](auto kind,auto context){return self->driver.Post(kind,context);});return 0;}
        if(self&&message==WM_KEYUP){++self->keyups;self->driver.gate.KeyUp(uint32_t(key));return 0;}
        return DefWindowProcW(window,message,key,info);
    }
    MessageOnlyWindow(){
        WNDCLASSW c{};c.lpfnWndProc=Procedure;c.hInstance=module;c.lpszClassName=name;
        Need(RegisterClassW(&c)!=0,"register test-owned message-only class");
        window=CreateWindowExW(0,name,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,module,this);
        if(!window){UnregisterClassW(name,module);throw std::runtime_error("create message-only window");}
    }
    ~MessageOnlyWindow(){if(window)DestroyWindow(window);UnregisterClassW(name,module);}
    void Run(){
        Need(!IsWindowVisible(window)&&GetForegroundWindow()!=window,"message-only test never owns foreground");
        const auto bits=LPARAM(1)|(LPARAM(MapVirtualKeyW('A',MAPVK_VK_TO_VSC))<<16);
        Need(PostMessageW(window,WM_KEYDOWN,'A',bits)!=FALSE&&
             PostMessageW(window,WM_KEYUP,'A',bits|LPARAM(0xC0000000u))!=FALSE,"post only to test-owned HWND");
        MSG m{};unsigned budget=64;
        while(PeekMessageW(&m,window,0,0,PM_REMOVE)){
            Need(budget--!=0,"bounded message-only queue");
            if(m.message==WM_KEYDOWN)native_port::PumpGameTextWindowKey(window,m,driver.gate,
                [&](const auto& c,const auto& receipt){++translated;Need(keyups==0,"real WM_CHAR precedes queued key-up");
                    // Real foreground predicate stays false; no editor success
                    // is claimed from this OS-binding test.
                    const auto result=native_port::DeliverGameTextCharacter(driver.gate,c.value,receipt,
                        GetForegroundWindow()==window,false,driver.high,
                        [&](std::wstring_view text,uint64_t owner){return driver.Commit(text,owner);});
                    Need(!result.admitted&&!result.posted,"production foreground refusal is not overridden");},
                [&](wchar_t){++discarded;});
            else DispatchMessageW(&m);
        }
        driver.Flush();
        Need(dispatched==1&&keyups==1&&translated>=1&&discarded==0&&driver.editor.Snapshot().staged.empty(),
             "real TranslateMessage/owned queue executed without desktop input or accepted edit");
    }
};
inline void Run(){
    AkiAndEnter();QueueBounds();ReceiptInvalidation();CommandsAndRefusals();ControllerIsolation();
    MessageOnlyWindow window;window.Run();
}
}
