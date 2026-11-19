#include "gameplay/room_input_gate.h"
#include <iostream>
#include <stdexcept>

using namespace dokodemo::gameplay;
namespace {
constexpr unsigned circle=0x2000, right=0x20;
constexpr RoomInputFrame open{true,true,false,true};
using E=RoomInputEvent;

void Check(RoomInputGate& gate,unsigned buttons,const RoomInputFrame& frame,E event) {
    if (gate.Step(buttons,frame)!=event) throw std::runtime_error("Unexpected main-flow event");
}

void ColdRoute() {
    RoomInputGate gate;
    gate.RequestAction(); // 8DDA3 enters scripted action 6; no player key
    Check(gate,0,open,E::none); // room -> await
    Check(gate,0,open,E::none); // await -> 5/0
    Check(gate,0,open,E::none); // 5/0 -> 5/4
    Check(gate,circle,open,E::during_action);
    Check(gate,circle,open,E::none); // held through the invitation
    constexpr RoomInputFrame prompt_busy{false,false,true,false};
    Check(gate,0,prompt_busy,E::none);
    Check(gate,circle,prompt_busy,E::none); // nested GUI owns the press
    Check(gate,circle,open,E::none); // no re-edge on room return
    gate.FinishAction(); // 8D9B5 follows the returned KSC greeting
    Check(gate,0,open,E::none); // consume 9 -> 5/11
    Check(gate,circle,open,E::none); // latch return phase, no dispatch
    Check(gate,circle,open,E::none);
    Check(gate,0,open,E::none);
    Check(gate,circle,open,E::ordinary);
    gate.RequestAction(); // ordinary room callback accepts: 8DA44 -> GAME0
    Check(gate,circle,open,E::none); // 23CB8 clears both flags -> 5/0
    gate.FinishAction(); // early/stale 9 before the substate 0 clear
    Check(gate,0,open,E::none);
    Check(gate,circle,open,E::during_action);
    Check(gate,0,open,E::none);
    gate.FinishAction();
    Check(gate,circle,open,E::during_action); // edge wins before late 9
    Check(gate,circle,open,E::none); // following held poll consumes 9
    Check(gate,0,open,E::none);
    Check(gate,circle,open,E::ordinary);
}

void Eligibility() {
    RoomInputGate gate;
    for (RoomInputFrame blocked: {RoomInputFrame{false,true,false,true},
                                  RoomInputFrame{true,false,false,true},
                                  RoomInputFrame{true,true,true,true}}) {
        Check(gate,0,blocked,E::none);
        Check(gate,circle,blocked,E::none);
        Check(gate,circle,open,E::none);
    }
    Check(gate,0,open,E::none);
    Check(gate,right|circle,RoomInputFrame{true,true,false,false},E::ordinary);
    gate.FinishAction(); // early event rejects ordinary callback -> GAME3 only
    Check(gate,0,open,E::none);
    Check(gate,right|circle,open,E::menu); // source Right preempts Circle
}

void MenuReturn(int result, E event) {
    RoomInputGate gate;
    Check(gate, right | circle, open, E::menu);
    Check(gate, 0, open, E::none);
    Check(gate, circle, open, E::none);
    if (gate.Step(circle, open, result) != E::none) throw std::runtime_error("Menu selected before animation return");
    Check(gate, circle, open, event);
    gate.RequestAction();
    for (unsigned i = 0; i < 5; ++i) Check(gate, circle, open, E::none);
    gate.FinishAction();
    Check(gate, circle, open, E::none);
    Check(gate, circle, open, E::none);
    Check(gate, circle, open, E::none);
    Check(gate, 0, open, E::none);
    Check(gate, right, open, E::menu);
    if (gate.Step(0, open, -1) != E::none) throw std::runtime_error("Menu cancel posted teaching");
    Check(gate, circle, open, E::none);
    Check(gate, circle, open, E::none);
    Check(gate, 0, open, E::none);
    Check(gate, circle, open, E::ordinary);
}

void PendingOrder() {
    RoomInputGate gate;
    constexpr RoomInputFrame busy{true,true,true,false};
    gate.RequestAction(); gate.FinishAction();
    Check(gate,circle,busy,E::none); // no input, 23C30 still takes 8
    Check(gate,0,busy,E::none); // 8 first in phase1, both flags cleared
    Check(gate,circle,busy,E::none); // enter substate4
    Check(gate,0,open,E::none);
    Check(gate,circle,open,E::during_action);
    gate.FinishAction();
    Check(gate,0,busy,E::none); // don't consume 9 while extension busy
    Check(gate,circle,open,E::during_action);
    Check(gate,0,open,E::none);
    Check(gate,0,open,E::none);
    Check(gate,circle,open,E::ordinary);
}

void ReturnWaitsForScript() {
    RoomInputGate gate;
    Check(gate,right,open,E::menu);
    if (gate.Step(circle,open,1)!=E::none) throw std::runtime_error("Return fired at result poll");
    gate.RequestAction();
    gate.FinishAction();
    Check(gate,circle,open,E::return_home); // clears both earlier requests, posts exactly once
    for (unsigned i=0; i<4; ++i) Check(gate,0,open,E::none);
    Check(gate,right,open,E::none);
    Check(gate,0,open,E::none);
    Check(gate,circle,open,E::none);
    gate.FinishAction();
    Check(gate,0,open,E::none);
    Check(gate,right,open,E::menu);
}

void InventoryMenuReturn() {
    RoomInputGate gate;
    Check(gate, right, open, E::menu);
    if (gate.Step(circle, open, 5) != E::inventory || !gate.owns_inventory())
        throw std::runtime_error("Inventory result lost its menu owner");
    Check(gate, 0, open, E::none);
    Check(gate, right, open, E::none);
    Check(gate, circle, open, E::none);
    gate.FinishInventory();
    if (gate.owns_inventory()) throw std::runtime_error("Menu retained a closed inventory");
    Check(gate, circle, open, E::none);
    Check(gate, 0, open, E::none);
    Check(gate, right, open, E::menu);
    if (gate.Step(circle, open, 5) != E::inventory)
        throw std::runtime_error("Second inventory request lost");
    gate.FinishInventory();
    Check(gate, 0, open, E::none);
    Check(gate, circle, open, E::ordinary);
}

void NotebookMenuReturn(int result) {
    RoomInputGate gate;
    Check(gate,right,open,E::menu);
    if (gate.Step(circle,open,result)!=E::notebook)
        throw std::runtime_error("Selected notebook resource did not open");
    Check(gate,0,open,E::none);
    Check(gate,circle,open,E::none);
    gate.FinishNotebook();
    Check(gate,circle,open,E::none);
    Check(gate,0,open,E::none);
    Check(gate,right,open,E::menu);
}
}

int main() try {
    MenuReturn(0, E::teaching);
    MenuReturn(1, E::return_home);
    MenuReturn(2, E::outing);
    for (int result = 8; result <= 11; ++result) MenuReturn(result, E::friends_action);
    ReturnWaitsForScript();
    InventoryMenuReturn();
    RoomInputGate options;
    Check(options,right,open,E::menu);
    if (options.Step(circle,open,13)!=E::options) throw std::runtime_error("Missing system Options route");
    Check(options,0,open,E::none);
    Check(options,circle,open,E::none);
    options.FinishOptions();
    Check(options,circle,open,E::none);
    Check(options,0,open,E::none);
    Check(options,right,open,E::menu);
    for (const int result : {14,15}) {
        const auto dismiss = result == 14 ? circle : 0x4000u;
        RoomInputGate save;
        Check(save,right,open,E::menu);
        if (save.Step(circle,open,result)!=E::save_request || !save.shows_save_notice())
            throw std::runtime_error("Missing System save route");
        Check(save,circle,open,E::none);
        Check(save,right,open,E::none);
        Check(save,0,open,E::none);
        Check(save,dismiss,open,E::dismiss_save_notice);
        if (!save.room_controls_ready() || save.shows_save_notice())
            throw std::runtime_error("Missing return from save notice");
        Check(save,dismiss,open,E::none);
        Check(save,0,open,E::none);
        Check(save,right,open,E::menu);
    }
    NotebookMenuReturn(3);
    NotebookMenuReturn(4);
    NotebookMenuReturn(6);
    ColdRoute();
    Eligibility();
    PendingOrder();
    std::cout<<"Room event (0,0), (12,5), GAME0/3 ordering, modal edges and Right priority passed\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr<<e.what()<<'\n';
    return 1;
}
