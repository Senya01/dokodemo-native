#pragma once
#include "script_vm.h"
#include "recovered/rng_native.h"
#include <array>
#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

// Synthetic SOURCE-authored checks. ROOT owns compilation and execution.
// Selected selector=0/argc=0 only; no game route, calendar or seed fixture.
namespace dokodemo_op25_checks {
using namespace dokodemo::script;
using dokodemo::recovered::RngNativeOwner;
inline void need(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(std::string("VM25: ")+what);
}
inline bool same(const ValueWitness& a, const ValueWitness& b) {
    return a.kind==b.kind && a.word==b.word && a.table==b.table && a.index==b.index
        && a.cp932_bytes==b.cp932_bytes && a.cp932_sha256==b.cp932_sha256;
}
inline bool same_value(const Value& a, const Value& b) {
    return a.kind==b.kind && a.word==b.word && a.table==b.table && a.index==b.index
        && a.cp932==b.cp932;
}
inline Value typed(Value::Kind kind, uint32_t word) {
    Value v; v.kind=kind; v.word=word; return v;
}
inline Value ref(Table table, uint16_t index) {
    Value v; v.kind=Value::Kind::Reference; v.table=table; v.index=index; return v;
}
inline void put(std::vector<uint8_t>& b, std::size_t at, uint32_t word, unsigned n) {
    for (unsigned i=0;i<n;++i) b.at(at+i)=uint8_t(word>>(8*i));
}
struct Fixture {
    std::vector<uint8_t> code;
    std::vector<uint32_t> ctx;
    Program program;
    uint32_t pc() const { return uint32_t(code.size()); }
    void bytes(std::initializer_list<uint8_t> b) { code.insert(code.end(),b); }
    uint32_t call(uint16_t index=0, uint8_t selector=0) {
        const auto at=pc(); bytes({0x25,uint8_t(index),uint8_t(index>>8),selector});
        if (selector) bytes({0,0}); return at;
    }
    uint32_t function(int result=-1, uint8_t cells=0) {
        const auto at=pc(); bytes({0x30,cells});
        if (result>=0) bytes({0x80,uint8_t(result),0x70});
        bytes({0x31}); return at;
    }
    void load() {
        // Exact KSC v20030204 header + code + local + Ctx28 + global sections.
        std::vector<uint8_t> b(28,0);
        put(b,0,0x0043534b,4); put(b,4,0x20030204,4);
        put(b,12,pc(),4); put(b,22,1,2); put(b,24,1,2);
        put(b,26,uint32_t(ctx.size()),2);
        b.insert(b.end(),code.begin(),code.end());
        const auto words=[&](uint32_t w) {
            const auto at=b.size(); b.resize(at+4); put(b,at,w,4);
        };
        words(0x20000005); for (auto w:ctx) words(w); words(0x20000006);
        Diagnostic d; need(program.load_test_fixture(std::move(b),d),"synthetic KSC parse");
    }
};
inline RngNativeOwner owner(uint64_t state) {
    RngNativeOwner r;
    r.ImportCurrentState64(state,0x2531,"Explicit synthetic VM25 unit state; no game provenance");
    std::array<uint8_t,0x4004> scratch{}; scratch.fill(0x5a); r.ImportScratch(scratch);
    return r;
}
inline Event step(Vm& vm, int opcode) {
    const auto e=vm.step(); need(e.opcode==opcode,"executed opcode");
    need(e.status!=Status::Stopped,"unexpected refusal"); return e;
}
inline void preserved(const VmStateWitness& before, const VmStateWitness& after) {
    need(before.valid && after.valid,"valid state observations");
    need(before.run_id==after.run_id && before.clock_tick==after.clock_tick
        && before.deadline_tick==after.deadline_tick,"run/clock/absolute deadline preserved");
    need(before.context_registered==after.context_registered
        && before.context_word==after.context_word,"context owner identity preserved");
    need(before.active_blocks==after.active_blocks
        && before.event_scope_depth==after.event_scope_depth
        && before.queued_callbacks==after.queued_callbacks
        && before.event_envelope_present==after.event_envelope_present,"block/event state preserved");
    need(same(before.result,after.result),"caller result preserved by call/empty callee");
    need(before.operand_cells==after.operand_cells
        && before.operands_captured==after.operands_captured,"no implicit selection push/pop");
    for (uint32_t i=0;i<before.operands_captured;++i)
        need(same(before.operands[i],after.operands[i]),"caller operand retained");
}

inline void owned_call_return() {
    Fixture f;
    f.bytes({0x30,2,0x80,73,0x70,0x80,9,0x44});
    const auto block=f.pc(); f.bytes({0x18,0,0,0,0x80,41});
    const auto call=f.call(); f.bytes({0x70});
    const auto close=f.pc(); f.bytes({0x1a,0x38});
    put(f.code,block+1,close,3);
    const auto target=f.function(); f.ctx={0x90000001,0x70000000|target}; f.load();
    Vm vm(f.program); vm.clock_tick(100);
    need(vm.start({Value::integer(101),Value::integer(102)}).status==Status::Running,"owned root start");
    const auto context=vm.context_value();
    auto rng=owner(0x123456789abcdef0ULL), expected=rng;
    const auto random=expected.NextC3234(); unsigned advances=0;
    vm.random_source([&] { ++advances; return rng.NextC3234(); });
    step(vm,0x30); step(vm,0x80); step(vm,0x70); step(vm,0x80); step(vm,0x44);
    need(vm.deadline_tick()>0,"real nonzero 44 deadline");
    // This clock advance belongs ONLY to a synthetic unit, after its real44.
    vm.clock_tick(vm.deadline_tick()+13); step(vm,0x18); step(vm,0x80);
    vm.store_reference(ref(Table::Local,0),Value::integer(56));
    const auto before=vm.current_witness();
    need(before.current_pc==call && before.active_blocks==1 && before.operand_cells==1
        && vm.result().signed_integer()==73,"meaningful caller setup");
    std::vector<FrameTransition> observed;
    vm.frame_observer([&](const FrameTransition& t) { observed.push_back(t); });
    vm.trace_normal_return_at(target+2);
    step(vm,0x25); const auto in=vm.current_witness(); const auto w=vm.list_call25();
    preserved(before,in);
    need(in.current_pc==target && in.call_depth==1 && in.frame_cells==0
        && in.frame_id!=before.frame_id,"argc0 new owned frame at selected prologue");
    need(vm.active_call_matches(before.frame_id,before.code_entry,call,call+4,in.frame_id,target)
        && vm.owns_active_frame(before.frame_id,before.code_entry),"owned caller/callee linkage");
    need(observed.size()==1 && observed[0].opcode==0x25
        && observed[0].callback_receipt==0 && !vm.frame_observer_failed(),"one passive25 transition");
    need(observed[0].before.frame_id==before.frame_id && observed[0].before.current_pc==call
        && observed[0].after.frame_id==in.frame_id && observed[0].after.current_pc==target,
        "observer reports committed real transition");
    preserved(before,observed[0].before); preserved(in,observed[0].after);
    need(w.calls==1 && w.opcode_pc==call && w.ctx28_index==0 && w.header_word==0x90000001
        && w.count==1 && w.random_value==random && w.selected_index==0
        && w.selected_word==(0x70000000|target) && w.target==target && w.return_pc==call+4
        && w.caller_frame==before.frame_id && w.callee_frame==in.frame_id
        && w.operand_cells==1 && w.clock_tick==before.clock_tick && w.deadline_tick==before.deadline_tick,
        "all fifteen ListCall25Witness fields");
    need(advances==1 && vm.random_selections()==1 && rng.CurrentSeed64()==expected.CurrentSeed64()
        && rng.Snapshot().scratch==expected.Snapshot().scratch,"one actual C3234; no history mutation");
    step(vm,0x30); step(vm,0x31); const auto after=vm.current_witness(); preserved(before,after);
    need(after.frame_id==before.frame_id && after.code_entry==before.code_entry
        && after.current_pc==call+4 && after.call_depth==0 && after.frame_cells==2,
        "31 restores actual owned caller and command-end PC");
    for (uint32_t i=0;i<before.frame_captured;++i)
        need(same(before.frame_values[i],after.frame_values[i]),"caller frame cell retained");
    need(!vm.owns_active_frame(in.frame_id,target) && vm.owns_active_frame(before.frame_id,0),
        "returned callee retired; caller remains live");
    need(vm.read_slot(Table::Local,0).signed_integer()==56
        && vm.read_slot(Table::Global,0).signed_integer()==6
        && vm.context_value().context_owner.lock()==context.context_owner.lock(),"tables/context preserved");
    const auto& returned=vm.last_normal_return();
    need(returned.valid && returned.opcode_pc==target+2 && returned.before.frame_id==in.frame_id
        && returned.after.frame_id==before.frame_id && returned.after.current_pc==call+4,
        "normal return witness belongs to real caller");
    need(observed.size()==2 && observed[1].opcode==0x31,"one normal31 observer transition");
    step(vm,0x70); step(vm,0x1a);
    need(vm.active_control_blocks()==0 && vm.control_blocks_entered()==1
        && vm.control_blocks_closed()==1,"caller block closes after consuming own operand");
    need(step(vm,0x38).status==Status::Complete && advances==1,"synthetic caller completes");
}

inline void three_code_real_rng() {
    Fixture f; f.bytes({0x30,0}); const auto call=f.call(); f.bytes({0x38});
    const std::array<uint32_t,3> targets{{f.function(11),f.function(22),f.function(33)}};
    // Upper payload bits are not the count: source77404 uses low12 only.
    f.ctx={0x90abc003,0x70000000|targets[0],0x70000000|targets[1],0x70000000|targets[2]}; f.load();
    const std::array<uint64_t,4> states{{0,1,0x0123456789abcdefULL,0xfedcba9876543210ULL}};
    for (const auto state:states) {
        Vm vm(f.program); auto rng=owner(state), expected=rng;
        const auto random=expected.NextC3234(), choice=random%3; unsigned advances=0;
        vm.random_source([&] { ++advances; return rng.NextC3234(); });
        need(vm.start().status==Status::Running,"three-code unit start"); step(vm,0x30); step(vm,0x25);
        need(vm.pc()==targets[choice] && vm.list_call25().random_value==random
            && vm.list_call25().count==3 && vm.list_call25().selected_index==choice
            && vm.list_call25().selected_word==f.ctx[choice+1]
            && same_value(vm.last_random_value(),typed(Value::Kind::Code,f.ctx[choice+1])),
            "target derives from actual owned next value modulo live count");
        step(vm,0x30); step(vm,0x80); step(vm,0x70); step(vm,0x31);
        need(vm.pc()==call+4 && vm.result().signed_integer()==int32_t(11*(choice+1)),
            "selected callee may legitimately change result before normal31");
        need(advances==1 && vm.random_selections()==1 && vm.list_call25().calls==1
            && rng.CurrentSeed64()==expected.CurrentSeed64()
            && rng.Snapshot().scratch==expected.Snapshot().scratch,"exactly one shared-owner advance");
        need(step(vm,0x38).status==Status::Complete,"three-code caller completes");
    }
}

inline void live_ctx_overwrite() {
    Fixture f; f.bytes({0x30,0}); const auto call=f.call(1); f.bytes({0x38});
    const auto old=f.function(), a=f.function(), b=f.function();
    f.ctx={0x20000000,0x90000001,0x70000000|old,0}; f.load();
    Vm vm(f.program); need(vm.start().status==Status::Running,"live overwrite start"); step(vm,0x30);
    const auto list=ref(Table::Ctx28,1);
    vm.store_reference(list,typed(Value::Kind::ArrayHeader,0x90000002));
    vm.store_reference(vm.indexed_reference(list,0),typed(Value::Kind::Code,0x70000000|a));
    vm.store_reference(vm.indexed_reference(list,1),typed(Value::Kind::Code,0x70000000|b));
    auto rng=owner(0x1020304050607080ULL), expected=rng;
    const auto random=expected.NextC3234(), target=random%2?b:a; unsigned advances=0;
    vm.random_source([&] { ++advances; return rng.NextC3234(); }); step(vm,0x25);
    need(vm.pc()==target && vm.pc()!=old && vm.list_call25().ctx28_index==1
        && vm.list_call25().header_word==0x90000002 && vm.list_call25().count==2
        && vm.list_call25().selected_word==(0x70000000|target),"selection reads overwritten live Ctx28");
    need(f.program.initial_word(Table::Ctx28,1)==0x90000001
        && f.program.initial_word(Table::Ctx28,2)==(0x70000000|old),"initial metadata remains distinct");
    step(vm,0x30); step(vm,0x31);
    need(vm.pc()==call+4 && advances==1 && rng.CurrentSeed64()==expected.CurrentSeed64(),
        "live overwrite makes one owned call and returns");
}

inline void refused_domains() {
    enum Bad { Selector, Index, HeaderKind, HeaderTag, UnknownHeader, Extent, MissingRng,
        ScalarElement, UnknownElement, CodeTag, TargetInterior, TargetNotPrologue,
        Empty, UnavailableOwner, NegativeRng, BadCount };
    for (unsigned which=Selector;which<=BadCount;++which) {
        Fixture f; f.bytes({0x30,1});
        const auto call=f.call(which==Index?uint16_t(65535):uint16_t(0),which==Selector?1:0);
        const auto complete=f.pc(); f.bytes({0x38}); const auto target=f.function();
        f.ctx={0x90000001,0x70000000|target}; f.load();
        need(f.program.command(call)->size==(which==Selector?6u:4u)
            && f.program.command(complete)->opcode==0x38,"selector parser preserves exact command end");
        Vm vm(f.program); need(vm.start({Value::integer(18)}).status==Status::Running,"negative unit start");
        step(vm,0x30);
        if (which==HeaderKind) vm.store_reference(ref(Table::Ctx28,0),typed(Value::Kind::Scalar,0x90000001));
        if (which==HeaderTag) vm.store_reference(ref(Table::Ctx28,0),typed(Value::Kind::ArrayHeader,0x80000001));
        if (which==UnknownHeader) vm.store_reference(ref(Table::Ctx28,0),typed(Value::Kind::Unknown,0x90000001));
        if (which==Extent) vm.store_reference(ref(Table::Ctx28,0),typed(Value::Kind::ArrayHeader,0x90000002));
        if (which==BadCount) vm.store_reference(ref(Table::Ctx28,0),typed(Value::Kind::ArrayHeader,0x90000fff));
        if (which==Empty) vm.store_reference(ref(Table::Ctx28,0),typed(Value::Kind::ArrayHeader,0x90000000));
        if (which==ScalarElement) vm.store_reference(ref(Table::Ctx28,1),Value::integer(7));
        if (which==UnknownElement) vm.store_reference(ref(Table::Ctx28,1),Value{});
        if (which==CodeTag) vm.store_reference(ref(Table::Ctx28,1),typed(Value::Kind::Code,0x60000000|target));
        if (which==TargetInterior) vm.store_reference(ref(Table::Ctx28,1),typed(Value::Kind::Code,0x70000001));
        if (which==TargetNotPrologue) vm.store_reference(ref(Table::Ctx28,1),typed(Value::Kind::Code,0x70000000|complete));
        auto rng=owner(0x3141592653589793ULL), expected=rng; RngNativeOwner unavailable;
        unsigned attempts=0;
        if (which!=MissingRng) vm.random_source([&] {
            ++attempts;
            if (which==UnavailableOwner) return unavailable.NextC3234();
            const auto actual=rng.NextC3234();
            // Deliberate negative callback, outside admitted C3234 output domain.
            return which==NegativeRng ? actual|0x80000000u : actual;
        });
        const auto before=vm.current_witness(); const auto failure=vm.step(); const auto after=vm.current_witness();
        const bool consumed=which==ScalarElement || which==UnknownElement || which==CodeTag
            || which==TargetInterior || which==TargetNotPrologue || which==Empty || which==NegativeRng;
        if (consumed) (void)expected.NextC3234();
        need(failure.status==Status::Stopped && failure.opcode==0x25 && failure.pc==call
            && failure.file_offset==call+28 && !failure.diagnostic.reason.empty(),"explicit source/domain refusal");
        need(attempts==(consumed||which==UnavailableOwner?1u:0u)
            && rng.CurrentSeed64()==expected.CurrentSeed64()
            && rng.Snapshot().scratch==expected.Snapshot().scratch,"partial RNG effects match refusal phase");
        need(after.frame_id==before.frame_id && after.call_depth==before.call_depth && after.current_pc==call
            && after.frame_cells==before.frame_cells && after.operand_cells==before.operand_cells
            && same(before.result,after.result) && vm.list_call25().calls==0 && vm.random_selections()==0,
            "refusal does not admit call or publish successful selection");
        const auto executed=vm.executed(); const auto seed=rng.CurrentSeed64(); const auto count=attempts;
        for (unsigned repeat=0;repeat<3;++repeat)
            need(vm.step().status==Status::Stopped && vm.pc()==call && vm.executed()==executed
                && attempts==count && rng.CurrentSeed64()==seed,"stopped refusal never replays RNG or call");
    }
    Fixture truncated; truncated.bytes({0x30,0,0x25,0,0});
    // load() deliberately throws when the real synthetic parser rejects truncation.
    bool rejected=false; try { truncated.load(); } catch (const std::runtime_error&) { rejected=true; }
    need(rejected,"truncated25 cannot become a command");
}

inline void nested_retirement() {
    Fixture f; f.bytes({0x30,1}); const auto root_call=f.call(); f.bytes({0x38});
    const auto outer=f.pc(); f.bytes({0x30,1}); const auto inner_call=f.call(2); f.bytes({0x31});
    const auto inner=f.function();
    f.ctx={0x90000001,0x70000000|outer,0x90000001,0x70000000|inner}; f.load();
    Vm vm(f.program); auto rng=owner(0x1111222233334444ULL), expected=rng; unsigned advances=0;
    vm.random_source([&] { ++advances; return rng.NextC3234(); });
    need(vm.start({Value::integer(81)}).status==Status::Running,"nested unit start");
    const auto context=vm.context_value(); step(vm,0x30); const auto root=vm.current_witness();
    step(vm,0x25); const auto first=vm.current_witness(); step(vm,0x30); step(vm,0x25);
    const auto second=vm.current_witness();
    need(second.call_depth==2 && second.frame_id!=first.frame_id && vm.list_call25().calls==2
        && vm.list_call25().opcode_pc==inner_call && vm.list_call25().caller_frame==first.frame_id
        && vm.active_call_matches(first.frame_id,outer,inner_call,inner_call+4,second.frame_id,inner),
        "nested25 records immediate owning caller");
    step(vm,0x30); step(vm,0x31);
    need(vm.pc()==inner_call+4 && vm.current_witness().frame_id==first.frame_id
        && vm.call_depth()==1 && !vm.owns_active_frame(second.frame_id,inner),"inner retirement restores outer");
    step(vm,0x31);
    need(vm.pc()==root_call+4 && vm.current_witness().frame_id==root.frame_id && vm.call_depth()==0
        && !vm.owns_active_frame(first.frame_id,outer) && vm.read_slot(Table::Frame,0).signed_integer()==81
        && vm.context_value().context_owner.lock()==context.context_owner.lock(),"outer retirement restores root owner");
    (void)expected.NextC3234(); (void)expected.NextC3234();
    need(advances==2 && vm.random_selections()==2 && rng.CurrentSeed64()==expected.CurrentSeed64(),
        "one actual RNG advance per nested call");
    need(step(vm,0x38).status==Status::Complete,"nested root completes");
    const auto executed=vm.executed();
    need(vm.step().status==Status::Stopped && vm.executed()==executed && advances==2,
        "completed VM does not replay retired nested calls");
}
} // namespace dokodemo_op25_checks

inline void GameVmOp25RegressionChecks() {
    dokodemo_op25_checks::owned_call_return();
    dokodemo_op25_checks::three_code_real_rng();
    dokodemo_op25_checks::live_ctx_overwrite();
    dokodemo_op25_checks::refused_domains();
    dokodemo_op25_checks::nested_retirement();
}
