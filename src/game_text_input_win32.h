#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "game_text_input.h"

namespace native_port {
// Same HWND-filtered binding for the native pump and a test-owned message-only
// window. This does not create/focus a window or synthesize physical input.
template<class Deliver,class Discard>
void PumpGameTextWindowKey(HWND window,const MSG& message,GameTextKeyGate& gate,
                          Deliver deliver,Discard discard) {
    PumpGameTextKey(gate,uint32_t(message.wParam),
        [&](GameTextCharacter& out){MSG c{};if(!PeekMessageW(&c,window,WM_CHAR,WM_CHAR,PM_REMOVE))return false;
            out={wchar_t(c.wParam),intptr_t(c.lParam)};return true;},
        [&]{MSG c{};return PeekMessageW(&c,window,WM_CHAR,WM_CHAR,PM_NOREMOVE)!=FALSE;},
        [&]{DispatchMessageW(&message);},[&]{TranslateMessage(&message);},deliver,discard);
}
}
