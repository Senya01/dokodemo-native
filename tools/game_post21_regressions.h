#pragma once
#include "../src/script_vm.h"
#include "recovered/game_scene_ui_native.h"
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Synthetic API composition checks. "post21" names the current work window,
// not an original-script coverage claim. No original packages are read.
// Opcode48 extracts an owned Event; the host decides when to call run again.
// There is no assumed opcode48UntilAck or generic source469 clear here.
// SELECT uses the existing native controller and Vm ownership policy.
namespace dokodemo::post21_regression_detail {
inline void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("post21 regression: " + message);
}
inline void StatusIs(const script::Event& event, script::Status status, const char* where) {
    Require(event.status == status, std::string(where) + ": " + event.diagnostic.text());
}
inline void Append(std::vector<uint8_t>& code, std::initializer_list<uint8_t> bytes) {
    code.insert(code.end(), bytes.begin(), bytes.end());
}
inline void Word(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.at(at + i) = uint8_t(value >> (8 * i));
}
inline std::string OwnerText() { return std::string(170, 'o') + "\x83\x67\x83\x8D"; }
inline std::vector<std::string> FirstOptions() {
    return {std::string(145, 'a'), std::string("\x83\x67\x83\x8D") + std::string(150, 'b')};
}
inline std::vector<std::string> SuccessorOptions() {
    return {std::string(155, 'c'), std::string(160, 'd') + "\x83\x67"};
}
inline void Options(std::vector<uint8_t>& code, const std::vector<std::string>& options) {
    for (const auto& option : options) {
        code.push_back(0x8E); code.insert(code.end(), option.begin(), option.end()); code.push_back(0);
    }
    Append(code, {0x8B, 0x21, uint8_t(options.size())});
}
inline void Message(std::vector<uint8_t>& code, const char* prefix, int result_slot = -1) {
    code.push_back(0x48);
    while (*prefix) code.push_back(uint8_t(*prefix++));
    if (result_slot >= 0) Append(code, {0x14, uint8_t(result_slot), 0, ':'});
    Append(code, {0x14, 0, 0, 0});
}
struct Fixture { script::Program program; uint32_t unknown_pc = 0; };
inline Fixture Build() {
    // A real frame reference and operand57 remain below both SELECTs.
    // The two actual positive results are consumed into distinct frame cells.
    std::vector<uint8_t> code{0x30, 3, 0x91, 0, 0, 0x80, 57};
    Message(code, "message:"); Options(code, FirstOptions());
    Append(code, {0x91, 1, 0, 0x89, 0x51});
    Message(code, "result:", 1); Options(code, SuccessorOptions());
    Append(code, {0x91, 2, 0, 0x89, 0x51});
    Message(code, "successor:", 2);
    Fixture fixture; fixture.unknown_pc = uint32_t(code.size());
    // Parsed byte00 has no implemented VM semantics. This synthetic cut does
    // not identify or reconstruct the real unknown linear21 successor.
    code.push_back(0x00);
    std::vector<uint8_t> bytes(28 + code.size());
    Word(bytes, 0, 0x0043534B); Word(bytes, 4, 0x20030204);
    Word(bytes, 12, uint32_t(code.size()));
    std::copy(code.begin(), code.end(), bytes.begin() + 28);
    script::Diagnostic error;
    const bool loaded = fixture.program.load_test_fixture(std::move(bytes), error);
    Require(loaded, "load synthetic mixed-boundary fixture: " + error.text());
    return fixture;
}
inline recovered::GameInputFrame Frame(uint32_t buttons, bool focused, uint64_t sequence,
                                       uint64_t context, uint64_t owner) {
    recovered::GameInputFrame frame;
    frame.buttons = buttons; frame.focused = focused; frame.sequence = sequence;
    frame.message_context = context; frame.message_owner_generation = owner;
    return frame;
}
inline std::vector<std::string> Take(script::Vm& vm, const std::vector<std::string>& expected) {
    auto arguments = vm.take_choice_arguments();
    Require(arguments.size() == expected.size(), "linked SELECT has exactly its own options");
    std::vector<std::string> options;
    for (size_t i = 0; i < arguments.size(); ++i) {
        Require(arguments[i].kind == script::Value::Kind::String && arguments[i].cp932 == expected[i],
                "linked SELECT transfers the correct owned option bytes in order");
        options.push_back(std::move(arguments[i].cp932));
    }
    return options;
}
inline void CallerIs(const script::Vm& vm, const std::string& owner) {
    Require(vm.stack().size() == 2 && vm.stack()[0].kind == script::Value::Kind::Reference
            && vm.peek_reference(vm.stack()[0]).cp932 == owner
            && vm.stack()[1].signed_integer() == 57
            && vm.read_slot(script::Table::Frame, 0).cp932 == owner,
            "linked boundaries retain their original live frame and earlier operands");
}
inline void Interactive(const recovered::GameChoiceController& controller, const char* where) {
    const auto state = controller.Snapshot();
    Require(state.live && state.state == 1 && state.confirmations == 0, where);
}

inline void MessageSelectResultSuccessorRequireDistinctEdges() {
    using namespace recovered;
    auto fixture = Build(); script::Vm vm(fixture.program);
    std::vector<script::Value> caller{script::Value::string(OwnerText())};
    StatusIs(vm.start(caller), script::Status::Running, "mixed chain start");
    caller[0].cp932 = "changed caller temporary";
    const auto first = vm.run(100);
    StatusIs(first, script::Status::Message, "initial message");
    Require(first.message_cp932 == "message:" + OwnerText(), "initial Event owns its expanded frame text");
    CallerIs(vm, OwnerText());

    uint64_t sequence = 0;
    const auto frame = [&](uint32_t buttons, uint64_t context, uint64_t owner = 1001, bool focused = true) {
        return Frame(buttons, focused, ++sequence, context, owner);
    };
    GameMessageInput message; message.Begin(901, 1001);
    Require(!message.Observe(frame(0, 901)) && message.Observe(frame(Circle, 901)),
            "initial message consumes one focused release and physical press");
    // Calling run is an explicit host continuation, not a source48 barrier.
    StatusIs(vm.run(100), script::Status::Choice, "SELECT after acknowledged message");
    const auto choice_pc = vm.pc(); const auto choice_executed = vm.executed();
    auto options = Take(vm, FirstOptions());
    GameChoiceController controller;
    Require(controller.Begin(options, 902, 1001, 0), "first linked SELECT owner");
    options[0] = "changed option conversion temporary";
    controller.Tick(1);
    Require(!controller.Input(frame(0, 902)), "release during opening cannot prime the live SELECT");
    controller.Tick(0);
    Require(controller.Input(frame(Circle, 902)), "held message press sampled by SELECT"); controller.Tick(0);
    Interactive(controller, "one physical message edge cannot confirm its following SELECT");
    Require(!controller.Input(frame(0, 901)) && !controller.Input(frame(Circle, 901)),
            "queued frames still owned by the message cannot arm SELECT");
    Require(!controller.Input(frame(0, 902, 1000)) && controller.Input(frame(Circle, 902)),
            "retired owner release cannot make the current held Circle fresh"); controller.Tick(0);
    Interactive(controller, "old owner release leaves SELECT unconfirmed");
    Require(controller.Input(frame(0, 902, 1001, false)) && controller.Input(frame(Circle, 902)),
            "focus-loss release and held refocus are sampled"); controller.Tick(0);
    Interactive(controller, "unfocused release cannot arm the next SELECT confirmation");
    Require(controller.Input(frame(0, 902)) && controller.Input(frame(Down, 902)), "fresh navigation to second option");
    controller.Tick(0);
    Require(controller.Snapshot().row == 1 && controller.Snapshot().result == 2, "first SELECT has positive result2");
    Require(controller.Input(frame(0, 902)) && controller.Input(frame(Circle, 902)), "first SELECT's own confirmation edge");
    controller.Tick(0);
    Require(controller.Snapshot().state == 2 && controller.Snapshot().confirmations == 1,
            "first SELECT enters closing once");
    const auto closing_release = frame(0, 902), closing_press = frame(Circle, 902);
    Require(!controller.Input(closing_release) && !controller.Input(closing_press), "closing SELECT admits no second input result");
    controller.Tick(1);
    Require(controller.Snapshot().state == 2, "active closing clip postpones host publication");
    StatusIs(vm.run(100), script::Status::Waiting, "script consumer cannot run during SELECT close");
    Require(vm.pc() == choice_pc && vm.executed() == choice_executed && vm.choice_waiting()
            && !vm.complete_choice(3), "invalid result and pending VM run preserve the selected continuation");
    CallerIs(vm, OwnerText());
    controller.Tick(0);
    Require(controller.Snapshot().state == 3 && vm.complete_choice(controller.Snapshot().result)
            && !vm.complete_choice(2), "host publishes closed result2 once");
    const auto result = vm.run(100);
    StatusIs(result, script::Status::Message, "actual result consumer emits its message");
    Require(result.message_cp932 == "result:2:" + OwnerText()
            && vm.read_slot(script::Table::Frame, 1).signed_integer() == 2,
            "result2 is consumed by a real script assignment and inline substitution");
    Require(first.message_cp932 == "message:" + OwnerText() && controller.Snapshot().options == FirstOptions(),
            "result extraction preserves earlier owned message and SELECT text");

    message.Begin(903, 1001);
    Require(!message.Observe(closing_release) && !message.Observe(closing_press)
            && !message.Observe(frame(Circle, 903)) && !message.Armed(),
            "closing frames and held SELECT edge cannot acknowledge the result message");
    Require(!message.Observe(frame(0, 903, 1001, false)) && !message.Observe(frame(Circle, 903)),
            "result message needs its own focused release after refocus");
    const auto release = frame(0, 903);
    Require(!message.Observe(release) && message.Armed(), "result message owns its focused release");
    auto duplicate = release; duplicate.buttons = Circle;
    Require(!message.Observe(duplicate) && message.Observe(frame(Circle, 903)),
            "result message rejects duplicate sequence and accepts one fresh press");
    StatusIs(vm.run(100), script::Status::Choice, "successor SELECT after result message");
    Require(controller.Begin(Take(vm, SuccessorOptions()), 904, 1001, 0), "same native controller admits next completed boundary");
    controller.Tick(0);
    Require(controller.Input(frame(Circle, 904)), "held result-message press sampled at successor SELECT"); controller.Tick(0);
    Interactive(controller, "result-message press cannot also select its successor");
    Require(!controller.Input(frame(0, 903)) && !controller.Input(frame(0, 902))
            && !controller.Input(frame(0, 904, 1000)) && controller.Input(frame(Circle, 904)),
            "neither prior target nor retired owner can supply successor release"); controller.Tick(0);
    Interactive(controller, "successor still needs its own focused release");
    Require(controller.Input(frame(0, 904)) && controller.Input(frame(Circle, 904)),
            "successor's fresh quick release/press survives one game tick"); controller.Tick(0);
    Require(controller.Snapshot().state == 2 && controller.Snapshot().result == 1
            && controller.Snapshot().confirmations == 1, "successor closes with its own one-based result1");
    controller.Tick(0);
    Require(controller.Snapshot().state == 3 && vm.complete_choice(controller.Snapshot().result)
            && !vm.complete_choice(1), "successor result publication is independently single-use");
    const auto successor = vm.run(100);
    StatusIs(successor, script::Status::Message, "successor's actual consumer");
    Require(successor.message_cp932 == "successor:1:" + OwnerText()
            && vm.read_slot(script::Table::Frame, 1).signed_integer() == 2
            && vm.read_slot(script::Table::Frame, 2).signed_integer() == 1
            && vm.display_count() == 3 && vm.pc() == fixture.unknown_pc && !vm.stopped(),
            "separate result consumers preserve result2,result1 and leave the unknown successor unexecuted");
    CallerIs(vm, OwnerText());
}

inline void UnknownSuccessorKeepsFirstErrorAndRestartRetiresFrame() {
    auto fixture = Build(); script::Vm vm(fixture.program);
    StatusIs(vm.start({script::Value::string(OwnerText())}), script::Status::Running, "unknown-cut chain start");
    const auto first = vm.run(100);
    StatusIs(first, script::Status::Message, "owned pre-SELECT Event");
    const auto old_reference = vm.stack().at(0); const auto token = vm.context_value();
    // This check exercises VM ownership and diagnostics, with explicit host
    // continuation. Physical input/clip sequencing is covered by the check above.
    StatusIs(vm.run(100), script::Status::Choice, "cut fixture first SELECT");
    recovered::GameChoiceController retained_options;
    Require(retained_options.Begin(Take(vm, FirstOptions()), 951, 1051, 0), "retain transferred first SELECT strings");
    Require(vm.complete_choice(2), "explicit positive first result");
    const auto result = vm.run(100); StatusIs(result, script::Status::Message, "owned result Event");
    StatusIs(vm.run(100), script::Status::Choice, "cut fixture successor SELECT");
    (void)Take(vm, SuccessorOptions()); Require(vm.complete_choice(1), "explicit positive successor result");
    const auto successor = vm.run(100); StatusIs(successor, script::Status::Message, "owned successor Event");
    const auto executed = vm.executed();
    const auto cut = vm.run(100);
    Require(cut.status == script::Status::Stopped && vm.stopped() && cut.opcode == 0x00
            && cut.pc == fixture.unknown_pc && cut.file_offset == fixture.unknown_pc + 28
            && cut.diagnostic.pc == fixture.unknown_pc && cut.diagnostic.file_offset == fixture.unknown_pc + 28
            && cut.diagnostic.opcode == 0x00
            && cut.diagnostic.reason == "Unsupported opcode semantics" && vm.executed() == executed,
            "unknown successor stops at its exact first unsupported byte without reporting completion");
    Require(!vm.complete_choice(1), "late result cannot revive a stopped successor");
    bool take_refused = false;
    try { (void)vm.take_choice_arguments(); } catch (const std::exception&) { take_refused = true; }
    script::Value callback; callback.kind = script::Value::Kind::Code; callback.word = 0x70000000u;
    Require(take_refused && vm.post_callback(token, callback, script::Value::integer(1), script::Value::integer(2)) == -1,
            "late conversion and callback admission refuse the stopped owner");
    for (unsigned i = 0; i < 2; ++i) {
        const auto repeated = vm.run(100);
        Require(repeated.status == script::Status::Stopped && repeated.pc == cut.pc && repeated.opcode == cut.opcode
                && repeated.file_offset == cut.file_offset && repeated.diagnostic.reason == cut.diagnostic.reason
                && repeated.diagnostic.pc == cut.diagnostic.pc && repeated.diagnostic.file_offset == cut.diagnostic.file_offset
                && repeated.diagnostic.opcode == cut.diagnostic.opcode && vm.pc() == fixture.unknown_pc
                && vm.executed() == executed, "late APIs and repeated run preserve the first error and execute nothing");
    }
    CallerIs(vm, OwnerText());

    const auto replacement_text = std::string(180, 'n') + "\x83\x8D";
    StatusIs(vm.start({script::Value::string(replacement_text)}), script::Status::Running, "explicit same-VM restart");
    Require(old_reference.frame_owner.expired(), "restart releases the old frame without destroying Vm");
    bool read_refused = false, write_refused = false;
    try { (void)vm.load_reference(old_reference); } catch (const std::exception&) { read_refused = true; }
    try { vm.store_reference(old_reference, script::Value::string("stale overwrite")); }
    catch (const std::exception&) { write_refused = true; }
    Require(read_refused && write_refused && vm.read_slot(script::Table::Frame, 0).cp932 == replacement_text,
            "retired frame refuses both reads and writes into the replacement run");
    const auto replacement = vm.run(100); StatusIs(replacement, script::Status::Message, "replacement owns its own message");
    Require(replacement.message_cp932 == "message:" + replacement_text
            && first.message_cp932 == "message:" + OwnerText()
            && result.message_cp932 == "result:2:" + OwnerText()
            && successor.message_cp932 == "successor:1:" + OwnerText()
            && retained_options.Snapshot().options == FirstOptions(),
            "owned Events and transferred SELECT strings survive first error and same-VM restart");
    CallerIs(vm, replacement_text);
    // A Vm context token is VM-scoped and survives start(); no invented
    // request-generation rejection is asserted for raw complete_choice(uint32_t).
    // GameActorScript target validation requires its real initialized owner.
}
} // namespace dokodemo::post21_regression_detail

inline void GamePost21RegressionChecks() {
    dokodemo::post21_regression_detail::MessageSelectResultSuccessorRequireDistinctEdges();
    dokodemo::post21_regression_detail::UnknownSuccessorKeepsFirstErrorAndRestartRetiresFrame();
}
