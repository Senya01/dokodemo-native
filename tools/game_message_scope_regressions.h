#pragma once
#include "../src/script_vm.h"
#include "recovered/game_input_native.h"
#include <algorithm>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Synthetic native fixtures only; no original resource or PSP execution.
// The barrier below is an EXPLICIT opcode0D/0E scope, NOT opcode48UntilAck.
// Opcode48 returns owned extracted Event text; fresh acknowledgement belongs
// to the host. Original469, post32 and nonzero62 gaps are not closed here.
namespace dokodemo::message_scope_regression_detail {
inline void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("message scope regression: " + message);
}
inline void StatusIs(const script::Event& event, script::Status status, const char* where) {
    Require(event.status == status, std::string(where) + ": " + event.diagnostic.text());
}
inline void Put(std::vector<uint8_t>& bytes, size_t at, uint32_t value, unsigned width = 4) {
    for (unsigned i = 0; i < width; ++i) bytes.at(at+i) = uint8_t(value >> (8*i));
}
inline void Append(std::vector<uint8_t>& code, std::initializer_list<uint8_t> bytes) {
    code.insert(code.end(),bytes.begin(),bytes.end());
}
inline script::Program Program(std::vector<uint8_t> code) {
    std::vector<uint8_t> bytes(28+code.size());
    Put(bytes,0,0x0043534B); Put(bytes,4,0x20030204); Put(bytes,12,uint32_t(code.size()));
    std::copy(code.begin(),code.end(),bytes.begin()+28);
    script::Program program; script::Diagnostic error;
    Require(program.load_test_fixture(std::move(bytes),error),"load synthetic fixture: " + error.text());
    return program;
}
inline script::Value Code(uint32_t pc) {
    script::Value value; value.kind=script::Value::Kind::Code; value.word=0x70000000u|pc; return value;
}
inline std::string OwnerText() { return std::string(150,'m') + "\x83\x67\x83\x8D"; }
inline std::string SavedResult() { return std::string(170,'r') + "\x83\x8D"; }
inline std::string Message(const char* prefix) { return std::string(prefix) + OwnerText() + ":41"; }
struct Fixture { script::Program program; uint32_t message_pc=0,second_pc=0,queued_pc=0,deadline_pc=0; };
inline Fixture Build(bool message_is_callback=false, bool explicit_scope=true) {
    // Root owns a block, operand27 and a caller frame across an actual call.
    std::vector<uint8_t> code{0x30,2,0x91,0,0,0x8B,0x7A,1};
    const auto block=code.size(); Append(code,{0x18,0,0,0,0x80,27,0x90,0,0,0x90,1,0});
    const auto call=code.size(); Append(code,{0x20,2,0,0,0,0x89,0x8B,0x7D,2});
    Put(code,block+1,uint32_t(code.size()),3); Append(code,{0x1A,0x31});
    Fixture fixture; fixture.message_pc=uint32_t(code.size()); Put(code,call+2,fixture.message_pc,3);
    Append(code,{0x30,3,0x91,0,0,0x8B,0x7B,1,0x80,55});
    if (explicit_scope) code.push_back(0x0D);
    const auto text=[&](const char* prefix) {
        code.push_back(0x48); while (*prefix) code.push_back(uint8_t(*prefix++));
        Append(code,{0x14,0,0,':',0x14,1,0,0});
    };
    text("first:");
    if (explicit_scope) code.push_back(0x0E);
    fixture.second_pc=uint32_t(code.size()); text("second:");
    Append(code,{0x89,0x90,0,0,0x90,1,0,0x8B,0x7E,4});
    code.push_back(message_is_callback ? uint8_t(0x32) : uint8_t(0x31));
    fixture.queued_pc=uint32_t(code.size());
    Append(code,{0x30,3,0x90,0,0,0x90,1,0,0x8B,0x7C,2,0x32});
    fixture.deadline_pc=uint32_t(code.size());
    Append(code,{0x30,1,0x91,0,0,0x8B,0x7A,1,0x80,9,0x70,0x80,7,0x44,
                 0x90,0,0,0x8B,0x7F,1,0x38});
    fixture.program=Program(std::move(code)); return fixture;
}
inline bool Same(const script::Value& a, const script::Value& b) {
    return a.kind==b.kind && a.word==b.word && a.table==b.table && a.index==b.index && a.cp932==b.cp932
        && !a.frame_owner.owner_before(b.frame_owner) && !b.frame_owner.owner_before(a.frame_owner)
        && !a.context_owner.owner_before(b.context_owner) && !b.context_owner.owner_before(a.context_owner);
}
struct Continuation {
    uint32_t pc=0,calls=0,blocks=0,scope=0; uint64_t deadline=0,executed=0;
    script::Value result,frame0; std::vector<script::Value> stack;
};
inline Continuation Capture(const script::Vm& vm) {
    return {vm.pc(),vm.call_depth(),vm.active_control_blocks(),vm.event_scope_depth(),
            vm.deadline_tick(),vm.executed(),vm.result(),vm.read_slot(script::Table::Frame,0),vm.stack()};
}
inline void Preserved(const script::Vm& vm, const Continuation& saved, uint32_t pc, const char* where) {
    Require(vm.pc()==pc && vm.call_depth()==saved.calls && vm.active_control_blocks()==saved.blocks
            && vm.deadline_tick()==saved.deadline && Same(vm.result(),saved.result)
            && Same(vm.read_slot(script::Table::Frame,0),saved.frame0) && vm.stack().size()==saved.stack.size(),where);
    for (size_t i=0;i<saved.stack.size();++i) Require(Same(vm.stack()[i],saved.stack[i]),where);
}
inline void Unchanged(const script::Vm& vm, const Continuation& saved, const char* where) {
    Preserved(vm,saved,saved.pc,where);
    Require(vm.event_scope_depth()==saved.scope && vm.executed()==saved.executed,where);
}
inline recovered::GameInputFrame Frame(uint32_t buttons, bool focused, uint64_t sequence,
                                       uint64_t context, uint64_t owner) {
    recovered::GameInputFrame frame; frame.buttons=buttons; frame.focused=focused; frame.sequence=sequence;
    frame.message_context=context; frame.message_owner_generation=owner; return frame;
}

inline void ExplicitScopeFifoAndFreshSuccessor() {
    auto fixture=Build(); script::Vm vm(fixture.program);
    script::Value root_reference,inner_reference; uint32_t callbacks=0,inner_consumers=0,root_consumers=0;
    const std::vector<std::string> queued{std::string(180,'a'),std::string(190,'b')+"\x83\x67"};
    vm.host_service([&](uint8_t id,const std::vector<script::Value>& arguments) {
        if (id==0x7A || id==0x7B) {
            Require(arguments.size()==1,"capture real frame reference");
            (id==0x7A ? root_reference : inner_reference)=arguments[0];
            Require(vm.peek_reference(arguments[0]).cp932==OwnerText(),"caller copied its owned CP932 string");
            return script::Value::string(SavedResult());
        }
        if (id==0x7C) {
            Require(callbacks<queued.size() && arguments.size()==2 && arguments[0].cp932==queued[callbacks]
                    && arguments[1].signed_integer()==int32_t(73+callbacks),"FIFO callback owns enqueue temporaries");
            Require(vm.peek_reference(root_reference).cp932==OwnerText()
                    && vm.peek_reference(inner_reference).cp932==OwnerText(),"callback retains both suspended frames");
            ++callbacks; return script::Value::string("temporary callback result");
        }
        if (id==0x7E) {
            Require(arguments.size()==4 && arguments[0].signed_integer()==55 && arguments[1].cp932==SavedResult()
                    && arguments[2].cp932==OwnerText() && arguments[3].signed_integer()==41,
                    "inner continuation consumes retained operand,result and actual frame once");
            ++inner_consumers; return script::Value::integer(91);
        }
        Require(id==0x7D && arguments.size()==2 && arguments[0].signed_integer()==27
                && arguments[1].signed_integer()==91,"outer continuation retains operand and callee result");
        ++root_consumers; return script::Value::integer(99);
    });
    std::vector<script::Value> caller{script::Value::string(OwnerText()),script::Value::integer(41)};
    StatusIs(vm.start(caller),script::Status::Running,"scoped message start"); caller[0].cp932="changed external caller";
    const auto first=vm.run(100); StatusIs(first,script::Status::Message,"first extracted message");
    Require(first.opcode==0x48 && first.message_cp932==Message("first:") && first.display_state==6
            && vm.event_scope_depth()==1 && vm.call_depth()==1 && vm.active_control_blocks()==1,
            "owned extracted Event; remaining barrier is the encoded0D,not message acknowledgement");
    const auto saved=Capture(vm); const auto token=vm.context_value();
    for (uint32_t i=0;i<queued.size();++i) {
        auto temporary=script::Value::string(queued[i]);
        Require(vm.post_callback(token,Code(fixture.queued_pc),temporary,script::Value::integer(int32_t(73+i)))==0,
                "explicit0D admits queued callback"); temporary.cp932="changed enqueue temporary";
        Unchanged(vm,saved,"enqueue preserves caller result/frame/PC/operands/deadline/source scope");
    }
    StatusIs(vm.run(0),script::Status::HostYield,"empty native slice within explicit0D");
    Unchanged(vm,saved,"empty slice cannot release explicit scope");
    Require(vm.queued_callbacks()==2 && callbacks==0,"both callbacks remain behind explicit0D");

    recovered::GameMessageInput gate; gate.Begin(101,201);
    Require(!gate.Observe(Frame(recovered::Circle,true,1,101,201)) && !gate.Armed(),"held first Circle cannot continue host");
    Require(!gate.Observe(Frame(0,true,2,100,201)) && !gate.Armed(),"old message release cannot arm host");
    Require(!gate.Observe(Frame(0,true,3,101,201)) && gate.Armed(),"fresh host release arms first message");
    const auto first_press=Frame(recovered::Circle,true,4,101,201);
    Require(gate.Observe(first_press) && !gate.Observe(first_press)
            && !gate.Observe(Frame(recovered::Circle,true,5,101,201)),"one host acknowledgement; double completion refused");
    Unchanged(vm,saved,"host acknowledgement itself has no VM completion API or source-depth mutation");
    StatusIs(vm.run(1),script::Status::HostYield,"host continuation executes explicit0E");
    Require(vm.event_scope_depth()==0 && vm.queued_callbacks()==1 && callbacks==0
            && vm.callbacks_returned()==0 && vm.pc()==fixture.queued_pc
            && vm.read_slot(script::Table::Frame,0).cp932==queued[0],"0E drains ONE owned FIFO node without executing its body");
    for (uint32_t i=0;i<queued.size();++i) {
        StatusIs(vm.run(4),script::Status::HostYield,"queued callback body");
        Require(callbacks==i+1 && inner_consumers==0 && root_consumers==0
                && vm.result().cp932=="temporary callback result","callback result cannot consume suspended continuation");
        StatusIs(vm.run(1),script::Status::HostYield,"queued callback return");
    }
    Preserved(vm,saved,fixture.second_pc,"FIFO restores result,frames,call/block scopes,operands and continuation after0E");
    Require(vm.callbacks_returned()==2 && vm.queued_callbacks()==0 && vm.event_scope_depth()==0
            && first.message_cp932==Message("first:"),"returned Event still owns first text after callbacks");
    const auto second=vm.run(20); StatusIs(second,script::Status::Message,"successor extracted message");
    Require(second.message_cp932==Message("second:") && vm.event_scope_depth()==0,
            "unscoped opcode48 introduces no artificial until-ack barrier");
    const auto successor=Capture(vm); gate.Begin(102,201);
    const auto rejected=[&](const recovered::GameInputFrame& frame,const char* why) {
        Require(!gate.Observe(frame),why); Unchanged(vm,successor,"rejected host input preserves successor continuation");
    };
    rejected(Frame(0,true,6,101,201),"old release cannot arm successor");
    rejected(Frame(recovered::Circle,true,7,101,201),"old queued edge cannot consume successor");
    rejected(Frame(0,true,8,102,200),"retired owner cannot arm successor");
    rejected(Frame(recovered::Circle,true,9,102,201),"held Circle across messages cannot consume successor");
    rejected(Frame(0,false,10,102,201),"unfocused release cannot arm successor");
    rejected(Frame(recovered::Circle,true,11,102,201),"held refocus still requires focused release");
    rejected(Frame(0,true,12,102,201),"focused release arms without acknowledgement");
    Require(gate.Armed(),"successor owns its fresh release");
    rejected(Frame(recovered::Circle,true,12,102,201),"duplicate sequence cannot acknowledge successor");
    Require(gate.Observe(Frame(recovered::Circle,true,13,102,201)),"successor accepts its own fresh edge");
    StatusIs(vm.run(100),script::Status::Complete,"host resumes acknowledged successor");
    Require(inner_consumers==1 && root_consumers==1 && vm.control_blocks_closed()==1
            && vm.result().signed_integer()==99,"ordinary native call/block continuation completes once");
}

inline void UnscopedExtractionMayDispatch() {
    auto fixture=Build(false,false); script::Vm vm(fixture.program); uint32_t callbacks=0;
    vm.host_service([&](uint8_t id,const std::vector<script::Value>& arguments) {
        if (id==0x7B) {
            Require(vm.post_callback(vm.context_value(),Code(fixture.queued_pc),
                    script::Value::string(std::string(160,'u')),script::Value::integer(73))==0
                    && vm.queued_callbacks()==1,"callback queues inside current native run scope");
        } else if (id==0x7C) {
            Require(arguments.size()==2 && arguments[0].cp932==std::string(160,'u')
                    && arguments[1].signed_integer()==73,"post-extraction callback owns its arguments"); ++callbacks;
        } else Require(id==0x7A,"unexpected unscoped service");
        return script::Value::string(SavedResult());
    });
    vm.start({script::Value::string(OwnerText()),script::Value::integer(41)});
    const auto extracted=vm.run(100); StatusIs(extracted,script::Status::Message,"unscoped extracted Event");
    Require(extracted.message_cp932==Message("first:") && extracted.next_pc==fixture.second_pc
            && vm.pc()==fixture.queued_pc && vm.queued_callbacks()==0 && vm.event_scope_depth()==0 && callbacks==0,
            "run exit may dispatch after extraction; Event continuation and active callback PC are distinct");
    StatusIs(vm.run(4),script::Status::HostYield,"unscoped callback body");
    StatusIs(vm.run(1),script::Status::HostYield,"unscoped callback return");
    Require(callbacks==1 && vm.callbacks_returned()==1 && vm.pc()==extracted.next_pc
            && vm.result().cp932==SavedResult() && vm.call_depth()==1 && vm.active_control_blocks()==1
            && vm.stack().size()==2 && vm.stack()[0].signed_integer()==27 && vm.stack()[1].signed_integer()==55
            && vm.read_slot(script::Table::Frame,0).cp932==OwnerText()
            && extracted.message_cp932==Message("first:"),"unscoped event preserves owned text and full caller continuation");
}

inline void NestedDeadlineAndExpiredReference(bool expired_reference) {
    auto fixture=Build(true); script::Vm vm(fixture.program); script::Value root_reference,message_reference;
    uint32_t message_consumers=0,queued_calls=0,root_calls=0;
    const std::string root_text(155,'d'),queued_text(175,'q');
    vm.host_service([&](uint8_t id,const std::vector<script::Value>& arguments) {
        if (id==0x7A || id==0x7B) {
            Require(arguments.size()==1,"nested capture"); (id==0x7A ? root_reference : message_reference)=arguments[0];
        } else if (id==0x7E) {
            Require(arguments.size()==4 && arguments[0].signed_integer()==55 && arguments[1].cp932==SavedResult()
                    && arguments[2].cp932==OwnerText() && arguments[3].signed_integer()==41,"nested message continuation owns result/frame");
            ++message_consumers;
        } else if (id==0x7C) {
            Require(!expired_reference && arguments.size()==2 && arguments[0].cp932==queued_text
                    && arguments[1].signed_integer()==73 && vm.peek_reference(root_reference).cp932==root_text,
                    "owned queued callback retains sleeping root"); ++queued_calls;
        } else {
            Require(id==0x7F && arguments.size()==1 && arguments[0].cp932==root_text,"root deadline consumer"); ++root_calls;
        }
        return script::Value::string(SavedResult());
    });
    vm.clock_tick(100); StatusIs(vm.start_at(fixture.deadline_pc,{script::Value::string(root_text)}),script::Status::Running,"deadline start");
    StatusIs(vm.run(100),script::Status::Yield,"positive root deadline"); const auto root_pc=vm.pc();
    Require(vm.deadline_tick()==107 && vm.result().signed_integer()==9,"sleeping root owns seven remaining ticks and result9");
    const auto token=vm.context_value();
    Require(vm.post_callback(token,Code(fixture.message_pc),script::Value::string(OwnerText()),script::Value::integer(41))==0,"message callback interrupts sleeping root");
    StatusIs(vm.run(100),script::Status::Message,"nested first extraction"); const auto saved=Capture(vm);
    auto temporary=script::Value::string(queued_text);
    Require(vm.post_callback(token,Code(fixture.queued_pc),temporary,expired_reference ? message_reference : script::Value::integer(73))==0,"nested callback queues behind explicit scope/envelope");
    temporary.cp932="changed nested temporary"; Unchanged(vm,saved,"nested enqueue preserves live message callback");
    vm.clock_tick(103); StatusIs(vm.run(1),script::Status::HostYield,"nested explicit0E");
    Require(vm.event_scope_depth()==0 && vm.queued_callbacks()==1 && queued_calls==0,"outer event envelope independently defers queued callback after0E");
    StatusIs(vm.run(20),script::Status::Message,"nested successor extraction");
    const auto returned=vm.run(100); // Explicit host continuation; input policy was tested independently above.
    if (expired_reference) {
        Require(returned.status==script::Status::Stopped && vm.stopped()
                && returned.diagnostic.reason.find("reference expired")!=std::string::npos,
                "expired queued frame reference returns Stopped diagnostic through Vm::run");
        Require(message_consumers==1 && queued_calls==0 && root_calls==0 && message_reference.frame_owner.expired(),
                "expired reference executes neither queued body nor sleeping root"); return;
    }
    StatusIs(returned,script::Status::Yield,"message callback restores root then dispatches queue");
    Require(message_consumers==1 && queued_calls==0 && root_calls==0 && vm.callbacks_returned()==1
            && message_reference.frame_owner.expired(),"finished callback releases its own message frame");
    vm.clock_tick(105); StatusIs(vm.run(4),script::Status::HostYield,"queued owned callback body");
    StatusIs(vm.run(1),script::Status::HostYield,"queued owned callback return");
    Require(queued_calls==1 && vm.callbacks_returned()==2 && vm.result().signed_integer()==9 && vm.pc()==root_pc
            && vm.deadline_tick()==112 && vm.read_slot(script::Table::Frame,0).cp932==root_text,
            "named native envelope preserves root PC/result/frame and seven remaining deadline ticks");
    vm.clock_tick(111); const auto executed=vm.executed(); StatusIs(vm.run(20),script::Status::Yield,"deadline pending");
    Require(vm.executed()==executed && root_calls==0,"root cannot execute before preserved deadline");
    vm.clock_tick(112); StatusIs(vm.run(20),script::Status::Complete,"deadline continuation");
    Require(root_calls==1,"root deadline continuation runs once");
}

inline void UnknownSourceUnwindStaysCut() {
    std::vector<uint8_t> code{0x30,0,0x80,7,0x44,0x38}; const auto callback_pc=uint32_t(code.size());
    Append(code,{0x30,3}); const auto block=code.size(); Append(code,{0x18,0,0,0,0x48,'x',0,0x32});
    Put(code,block+1,uint32_t(code.size()),3); Append(code,{0x1A,0x32});
    auto program=Program(std::move(code)); script::Vm vm(program); vm.clock_tick(100); vm.start();
    StatusIs(vm.run(10),script::Status::Yield,"unwind root deadline");
    Require(vm.post_callback(vm.context_value(),Code(uint32_t(callback_pc)),script::Value::integer(1),script::Value::integer(2))==0,"finite unwind callback dispatch");
    StatusIs(vm.run(10),script::Status::Message,"unwind callback extraction"); const auto cut=vm.run(10);
    Require(cut.status==script::Status::Stopped && vm.stopped() && cut.opcode==0x32
            && cut.diagnostic.reason.find("abnormal active unwind")!=std::string::npos
            && vm.callbacks_returned()==0 && vm.active_control_blocks()==1,
            "named native callback refuses abnormal active unwind; original nonzero62 remains unknown");
}
} // namespace dokodemo::message_scope_regression_detail

inline void GameMessageScopeRegressionChecks() {
    dokodemo::message_scope_regression_detail::ExplicitScopeFifoAndFreshSuccessor();
    dokodemo::message_scope_regression_detail::UnscopedExtractionMayDispatch();
    dokodemo::message_scope_regression_detail::NestedDeadlineAndExpiredReference(false);
    dokodemo::message_scope_regression_detail::NestedDeadlineAndExpiredReference(true);
    dokodemo::message_scope_regression_detail::UnknownSourceUnwindStaysCut();
}
