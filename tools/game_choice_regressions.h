#pragma once
#include "../src/script_vm.h"
#include "recovered/game_scene_ui_native.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Explicit synthetic native fixtures only. These checks neither load game data
// nor establish original-script, rendering, audio, or full-game coverage.
namespace dokodemo::choice_regression_detail {
inline void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("choice regression: " + message);
}
inline void StatusIs(const script::Event& event, script::Status expected, const char* where) {
    Require(event.status == expected, std::string(where) + ": " + event.diagnostic.text());
}
inline std::vector<std::string> Options() {
    return {"first actual option", "\x83\x67\x83\x8D", "third / literal", "fourth", std::string(160, 'q')};
}
inline recovered::GameInputFrame Frame(uint32_t buttons, bool focused, uint64_t sequence,
                                       uint64_t context, uint64_t owner) {
    recovered::GameInputFrame frame;
    frame.buttons = buttons; frame.focused = focused; frame.sequence = sequence;
    frame.message_context = context; frame.message_owner_generation = owner;
    return frame;
}
inline void PutWord(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = uint8_t(value >> (8 * i));
}
struct Fixture {
    script::Program program;
    uint32_t queued_pc = 0, deadline_pc = 0;
};
inline Fixture Build(bool choice_is_callback = false) {
    // Host7A captures a real frame reference,7B observes the continuation,
    // 7C observes a queued callback,7D the sleeping root,7E its frame owner.
    std::vector<uint8_t> code{0x30,3,0x91,0,0,0x8B,0x7A,1};
    for (const auto& text : Options()) {
        code.push_back(0x8E); code.insert(code.end(), text.begin(), text.end()); code.push_back(0);
    }
    const std::vector<uint8_t> continuation{
        0x8B,0x21,5,0x95,0,0,0x89,0x51,0x90,0,0,0x90,1,0,0x94,0,0,0x8B,0x7B,3};
    code.insert(code.end(), continuation.begin(), continuation.end());
    code.push_back(choice_is_callback ? uint8_t(0x32) : uint8_t(0x31));
    Fixture fixture; fixture.queued_pc = uint32_t(code.size());
    const std::vector<uint8_t> queued{0x30,3,0x90,0,0,0x90,1,0,0x8B,0x7C,2,0x32};
    code.insert(code.end(), queued.begin(), queued.end());
    fixture.deadline_pc = uint32_t(code.size());
    const std::vector<uint8_t> root{
        0x30,1,0x91,0,0,0x8B,0x7E,1,0x80,9,0x70,0x80,7,0x44,0x90,0,0,0x8B,0x7D,1,0x38};
    code.insert(code.end(), root.begin(), root.end());
    std::vector<uint8_t> bytes(28 + code.size() + 4);
    PutWord(bytes,0,0x0043534B); PutWord(bytes,4,0x20030204);
    PutWord(bytes,12,uint32_t(code.size())); bytes[24] = 1;
    std::copy(code.begin(), code.end(), bytes.begin() + 28);
    PutWord(bytes,28 + code.size(),0x20000000);
    script::Diagnostic error;
    Require(fixture.program.load_test_fixture(std::move(bytes),error), "load explicit synthetic fixture: " + error.text());
    return fixture;
}
inline script::Value Code(uint32_t pc) {
    script::Value value; value.kind = script::Value::Kind::Code; value.word = 0x70000000u | pc; return value;
}
inline std::vector<std::string> TakeOptions(script::Vm& vm) {
    auto arguments = vm.take_choice_arguments();
    const auto expected = Options();
    Require(arguments.size() == expected.size(), "five arguments include the first option, without a title slot");
    std::vector<std::string> options;
    for (size_t i = 0; i < arguments.size(); ++i) {
        Require(arguments[i].kind == script::Value::Kind::String && arguments[i].cp932 == expected[i],
                "all five owned options keep order and CP932 bytes");
        options.push_back(std::move(arguments[i].cp932));
    }
    bool twice_refused = false;
    try { (void)vm.take_choice_arguments(); } catch (const std::exception&) { twice_refused = true; }
    Require(twice_refused, "choice arguments transfer only once");
    return options;
}

inline void ControllerAndQueuedChoice() {
    using namespace recovered;
    GameChoiceController controller;
    script::Value retired_reference, retired_context;
    const auto expected = Options();
    {
        auto fixture = Build(); script::Vm vm(fixture.program);
        script::Value frame_reference;
        uint32_t callbacks = 0, continuations = 0;
        const std::string owner_text = std::string(140,'s') + "\x83\x67";
        const std::vector<std::string> queued_texts{std::string(180,'a'), std::string(170,'b') + "\x83\x8D"};
        vm.host_service([&](uint8_t id, const std::vector<script::Value>& arguments) {
            if (id == 0x7A) {
                Require(arguments.size() == 1, "frame capture argument count"); frame_reference = arguments[0];
                Require(vm.peek_reference(frame_reference).cp932 == owner_text, "choice owns its caller string");
                return script::Value::string("owned result that SELECT must replace");
            }
            if (id == 0x7C) {
                Require(callbacks < queued_texts.size() && arguments.size() == 2
                        && arguments[0].cp932 == queued_texts[callbacks]
                        && arguments[1].signed_integer() == int32_t(73 + callbacks),
                        "FIFO callbacks own strings independently of enqueue temporaries");
                Require(vm.peek_reference(frame_reference).cp932 == owner_text,
                        "saved caller frame remains readable during the callback");
                ++callbacks; return script::Value::string("temporary callback result");
            }
            Require(id == 0x7B && arguments.size() == 3 && arguments[0].cp932 == owner_text
                    && arguments[1].signed_integer() == 41 && arguments[2].signed_integer() == 5,
                    "positive fifth choice reaches the original caller frame and result consumer");
            ++continuations; return script::Value::integer(99);
        });
        std::vector<script::Value> caller{script::Value::string(owner_text),script::Value::integer(41)};
        StatusIs(vm.start(caller),script::Status::Running,"choice start");
        caller[0].cp932 = "mutated external caller";
        StatusIs(vm.run(100),script::Status::Choice,"choice request");
        const auto continuation_pc = vm.pc();
        Require(!vm.complete_choice(1), "result cannot publish before option conversion");
        auto options = TakeOptions(vm);
        Require(controller.Begin(options,101,201), "controller accepts all five VM options");
        options[0] = "mutated conversion temporary";
        Require(controller.Snapshot().options == expected, "controller owns a distinct option list");
        controller.Tick(1);
        Require(controller.Snapshot().state == 0, "opening clip keeps choice noninteractive");
        controller.Tick(0);
        uint64_t sequence = 0;
        const auto input = [&](uint32_t buttons, bool focused = true) {
            return controller.Input(Frame(buttons,focused,++sequence,101,201));
        };
        Require(input(Circle), "held initial Circle sampled"); controller.Tick(0);
        Require(controller.Snapshot().state == 1 && controller.Snapshot().confirmations == 0,
                "held Circle at creation does not select");
        Require(!controller.Input(Frame(0,true,++sequence,100,201))
                && !controller.Input(Frame(Circle,true,++sequence,100,201)), "stale context cannot arm or select");
        Require(!controller.Input(Frame(0,true,++sequence,101,200)), "stale owner cannot arm");
        Require(!controller.Input(Frame(Circle,true,sequence,101,201)), "duplicate sequence cannot select");
        Require(input(0,false) && input(Circle,true), "focus-loss and held-refocus frames sampled"); controller.Tick(0);
        Require(controller.Snapshot().state == 1, "refocus needs a fresh focused release");
        Require(input(0), "focused release"); controller.Tick(0);
        Require(input(Cross), "Cross sampled"); controller.Tick(0);
        Require(controller.Snapshot().state == 1 && controller.Snapshot().result == 1
                && controller.Snapshot().confirmations == 0, "Cross is disabled and first option is one-based result1");
        for (uint32_t row = 1; row < 5; ++row) {
            Require(input(0), "navigation release"); controller.Tick(0);
            Require(input(Down), "navigation press"); controller.Tick(0);
            Require(controller.Snapshot().row == row && controller.Snapshot().result == row + 1,
                    "each actual option has its own accessible one-based result");
        }
        for (unsigned i = 0; i < 14; ++i) controller.Tick(0);
        Require(controller.Snapshot().row == 4, "held repeat stops at the final row");
        Require(input(0) && input(Down), "fresh Down after release"); controller.Tick(0);
        Require(controller.Snapshot().row == 0, "fresh Down wraps from final row");
        Require(input(0) && input(Up), "fresh Up after release"); controller.Tick(0);
        Require(controller.Snapshot().row == 4, "fresh Up wraps to the fifth option");
        Require(input(0) && input(Circle), "fresh confirmation"); controller.Tick(0);
        Require(controller.Snapshot().state == 2 && controller.Snapshot().result == 5
                && controller.Snapshot().confirmations == 1, "fifth selection starts closing exactly once");
        controller.Tick(1);
        Require(controller.Snapshot().state == 2, "closing clip must finish before publication"); controller.Tick(0);
        Require(controller.Snapshot().state == 3, "close reaches publication state");
        StatusIs(vm.run(20),script::Status::Waiting,"VM remains pending throughout UI close");
        Require(vm.pc() == continuation_pc && continuations == 0 && !vm.complete_choice(6),
                "invalid result cannot advance the choice consumer");
        const auto token = vm.context_value(); const auto callback = Code(fixture.queued_pc);
        for (uint32_t i = 0; i < queued_texts.size(); ++i) {
            auto temporary = script::Value::string(queued_texts[i]);
            Require(vm.post_callback(token,callback,temporary,script::Value::integer(int32_t(73+i))) == 0,
                    "callback admitted while SELECT owns its wait"); temporary.cp932 = "mutated enqueue temporary";
        }
        Require(vm.queued_callbacks() == 2 && callbacks == 0, "pending SELECT defers both callbacks");
        Require(vm.complete_choice(controller.Snapshot().result) && !vm.choice_waiting()
                && !vm.complete_choice(1), "closed UI publishes the positive result once");
        for (uint32_t i = 0; i < 2; ++i) {
            StatusIs(vm.run(4),script::Status::HostYield,"callback body");
            Require(callbacks == i+1 && vm.result().cp932 == "temporary callback result" && continuations == 0,
                    "callback's temporary result does not run the suspended continuation");
            StatusIs(vm.run(1),script::Status::HostYield,"callback return");
        }
        Require(vm.result().signed_integer() == 5 && vm.pc() == continuation_pc
                && vm.callbacks_returned() == 2 && vm.queued_callbacks() == 0
                && vm.peek_reference(frame_reference).cp932 == owner_text,
                "both callbacks restore the actual positive result,PC and original owned frame");
        StatusIs(vm.run(30),script::Status::Complete,"positive caller continuation");
        Require(continuations == 1 && vm.read_slot(script::Table::Global,0).signed_integer() == 5,
                "the fifth result reaches a real synthetic script consumer once");
        retired_reference = frame_reference; retired_context = token;
    }
    Require(retired_reference.frame_owner.expired() && retired_context.context_owner.expired(),
            "destroyed VM releases its frame and context owners");
    Require(controller.Snapshot().options == expected, "transferred UI strings survive VM teardown");
    auto fixture = Build(); script::Vm replacement(fixture.program); replacement.start();
    Require(replacement.post_callback(retired_context,Code(fixture.queued_pc),{}, {}) == -1,
            "retired context cannot dispatch into a replacement owner");
    bool retired_ref_refused = false;
    try { (void)replacement.load_reference(retired_reference); } catch (const std::exception&) { retired_ref_refused = true; }
    Require(retired_ref_refused, "retired frame reference has an explicit read refusal");
}

inline void TimeoutRetirementAndOverflow() {
    using namespace recovered;
    GameChoiceController timeout;
    Require(timeout.Begin(Options(),301,401,2), "short deadline fixture"); timeout.Tick(0);
    timeout.Tick(0,false);
    Require(timeout.Snapshot().remaining == 2, "active TRC pauses the choice deadline");
    Require(timeout.Input(Frame(0,true,1,301,401)), "deadline release"); timeout.Tick(0);
    Require(timeout.Input(Frame(Circle,true,2,301,401)), "deadline Circle"); timeout.Tick(0);
    Require(timeout.Snapshot().state == 2 && timeout.Snapshot().timed_out && timeout.Snapshot().result == 0
            && timeout.Snapshot().confirmations == 0, "expiry beats same-tick Circle and produces zero");
    timeout.Tick(0);
    auto fixture = Build(); script::Vm vm(fixture.program); uint32_t consumed = 0;
    vm.host_service([&](uint8_t id, const std::vector<script::Value>& arguments) {
        if (id == 0x7B) {
            Require(arguments.size() == 3 && arguments[2].signed_integer() == 0, "timeout zero reaches script consumer"); ++consumed;
        } else Require(id == 0x7A, "unexpected timeout fixture service");
        return script::Value::integer(7);
    });
    vm.start({script::Value::string("timeout frame"),script::Value::integer(41)});
    StatusIs(vm.run(100),script::Status::Choice,"timeout VM request"); (void)TakeOptions(vm);
    Require(vm.complete_choice(timeout.Snapshot().result), "zero is an admitted completed result");
    StatusIs(vm.run(30),script::Status::Complete,"timeout continuation");
    Require(consumed == 1 && vm.read_slot(script::Table::Global,0).signed_integer() == 0, "timeout result consumed once");

    GameChoiceController retired; Require(retired.Begin(Options(),302,402,0), "no-deadline fixture"); retired.Tick(0);
    for (unsigned i = 0; i < 20; ++i) retired.Tick(0);
    Require(retired.Snapshot().state == 1 && !retired.Snapshot().timed_out, "zero timeout disables expiry");
    Require(retired.Input(Frame(0,true,1,302,402)), "old generation release"); retired.Retire();
    Require(!retired.Input(Frame(Circle,true,2,302,402)), "retired controller rejects late Circle"); retired.Tick(0);
    Require(retired.Begin(Options(),302,403,0), "replacement owner generation"); retired.Tick(0);
    Require(!retired.Input(Frame(0,true,3,302,402)) && !retired.Input(Frame(Circle,true,4,302,402)),
            "queued frames for retired generation cannot arm replacement");
    Require(retired.Input(Frame(Circle,true,5,302,403)), "held Circle on replacement"); retired.Tick(0);
    Require(retired.Snapshot().state == 1, "replacement requires its own release");
    Require(retired.Input(Frame(0,true,6,302,403)) && retired.Input(Frame(Circle,true,7,302,403)), "fresh replacement edge");
    retired.Tick(0); Require(retired.Snapshot().state == 2 && retired.Snapshot().result == 1, "replacement selects first option");

    GameChoiceController overflow; Require(overflow.Begin(Options(),303,404,0), "overflow fixture"); overflow.Tick(0);
    GameMessageInput message; message.Begin(303,404);
    const auto release = Frame(0,true,1,303,404);
    Require(overflow.Input(release) && !message.Observe(release) && message.Armed(), "release arms both consumers");
    GameInputMailbox mailbox;
    for (uint64_t i = 0; i < 129; ++i) mailbox.Push(Frame(Circle,true,100+i,303,404));
    GameInputFrame sampled;
    Require(mailbox.Pop(sampled) && sampled.overflow_resync && sampled.sequence == 0,
            "mailbox overflow emits typed reset before retained held frame");
    Require(!overflow.Input(sampled) && !message.Observe(sampled) && !message.Armed(),
            "typed reset disarms consumers despite zero sequence and missing context");
    Require(mailbox.Pop(sampled) && !sampled.overflow_resync && sampled.buttons == Circle
            && overflow.Input(sampled) && !message.Observe(sampled), "retained held Circle is sampled after reset");
    overflow.Tick(0);
    Require(overflow.Snapshot().state == 1 && !mailbox.Pop(sampled), "overflow-held frame cannot select");
    const auto fresh_release = Frame(0,true,300,303,404), fresh_circle = Frame(Circle,true,301,303,404);
    Require(overflow.Input(fresh_release) && !message.Observe(fresh_release)
            && overflow.Input(fresh_circle) && message.Observe(fresh_circle), "fresh release and press recover both consumers");
    overflow.Tick(0); Require(overflow.Snapshot().state == 2 && overflow.Snapshot().result == 1, "overflow recovery confirms exactly once");
}

inline void NestedChoiceDeadline(bool expired_reference) {
    auto fixture = Build(true); script::Vm vm(fixture.program);
    script::Value choice_reference, root_reference;
    uint32_t selected = 0, queued_calls = 0, root_calls = 0;
    const std::string root_text(150,'r'), choice_text(150,'c'), queued_text(150,'e');
    vm.host_service([&](uint8_t id, const std::vector<script::Value>& arguments) {
        if (id == 0x7A || id == 0x7E) {
            Require(arguments.size() == 1, "nested frame capture");
            (id == 0x7A ? choice_reference : root_reference) = arguments[0];
        } else if (id == 0x7B) {
            Require(arguments.size() == 3 && arguments[0].cp932 == choice_text
                    && arguments[1].signed_integer() == 41 && arguments[2].signed_integer() == 5,
                    "nested choice's positive result reaches its callback consumer"); ++selected;
        } else if (id == 0x7C) {
            Require(!expired_reference && arguments.size() == 2 && arguments[0].cp932 == queued_text
                    && arguments[1].signed_integer() == 73, "queued nested callback owns its scalar and string");
            Require(vm.peek_reference(root_reference).cp932 == root_text, "sleeping root frame remains owned"); ++queued_calls;
        } else {
            Require(id == 0x7D && arguments.size() == 1 && arguments[0].cp932 == root_text,
                    "deadline continuation restores sleeping root frame"); ++root_calls;
        }
        return script::Value::string("temporary nested callback result");
    });
    vm.clock_tick(100);
    StatusIs(vm.start_at(fixture.deadline_pc,{script::Value::string(root_text)}),script::Status::Running,"deadline root start");
    StatusIs(vm.run(100),script::Status::Yield,"root deadline");
    const auto root_pc = vm.pc();
    Require(vm.deadline_tick() == 107 && vm.result().signed_integer() == 9, "root has actual positive wait and saved result9");
    const auto token = vm.context_value();
    Require(vm.post_callback(token,Code(0),script::Value::string(choice_text),script::Value::integer(41)) == 0,
            "choice callback interrupts sleeping root");
    StatusIs(vm.run(100),script::Status::Choice,"nested choice request"); (void)TakeOptions(vm);
    auto temporary = script::Value::string(queued_text);
    Require(vm.post_callback(token,Code(fixture.queued_pc),temporary,
                             expired_reference ? choice_reference : script::Value::integer(73)) == 0,
            "nested choice queues a subsequent callback"); temporary.cp932 = "changed after enqueue";
    vm.clock_tick(103);
    Require(vm.complete_choice(5) && vm.result().signed_integer() == 5 && vm.queued_callbacks() == 1,
            "positive choice completion preserves the occupied outer envelope");
    const auto returned = vm.run(100);
    if (expired_reference) {
        // This finite corner is explicitly unsupported: no invented strong
        // ownership for a queued reference to a returned callback's frame.
        Require(returned.status == script::Status::Stopped && vm.stopped()
                && returned.diagnostic.reason.find("reference expired") != std::string::npos,
                "expired queued frame reference must stop diagnostically, never escape Vm::run");
        Require(selected == 1 && queued_calls == 0 && root_calls == 0 && choice_reference.frame_owner.expired(),
                "expired-reference refusal executes no queued callback or sleeping root"); return;
    }
    StatusIs(returned,script::Status::Yield,"choice callback restores waiting root then drains owned callback");
    Require(selected == 1 && queued_calls == 0 && root_calls == 0 && vm.callbacks_returned() == 1
            && vm.read_slot(script::Table::Global,0).signed_integer() == 5 && choice_reference.frame_owner.expired(),
            "nested choice result is consumed and its finished frame is released");
    vm.clock_tick(105);
    StatusIs(vm.run(4),script::Status::HostYield,"owned callback after nested choice");
    StatusIs(vm.run(1),script::Status::HostYield,"owned callback restores root envelope");
    Require(queued_calls == 1 && vm.callbacks_returned() == 2 && vm.result().signed_integer() == 9
            && vm.pc() == root_pc && vm.deadline_tick() == 112
            && vm.read_slot(script::Table::Frame,0).cp932 == root_text,
            "callbacks preserve root result,PC,owned frame and seven remaining deadline ticks");
    vm.clock_tick(111); const auto instructions = vm.executed();
    StatusIs(vm.run(20),script::Status::Yield,"deadline not yet reached");
    Require(vm.executed() == instructions && root_calls == 0, "positive deadline prevents premature root continuation");
    vm.clock_tick(112); StatusIs(vm.run(20),script::Status::Complete,"deadline root continuation");
    Require(root_calls == 1, "root resumes once after its preserved deadline");
}
} // namespace dokodemo::choice_regression_detail

inline void GameChoiceRegressionChecks() {
    dokodemo::choice_regression_detail::ControllerAndQueuedChoice();
    dokodemo::choice_regression_detail::TimeoutRetirementAndOverflow();
    dokodemo::choice_regression_detail::NestedChoiceDeadline(false);
    dokodemo::choice_regression_detail::NestedChoiceDeadline(true);
}
