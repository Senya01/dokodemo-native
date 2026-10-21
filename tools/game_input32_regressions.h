#pragma once
#include "../src/script_vm.h"
#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// New, linked, explicitly synthetic VM checks for builtin 0x20 (decimal 32)
// and the reached referenced-array branch of opcode 0x57. No old suite is
// included or invoked. These fixtures do not load original resources or prove
// the editor UI, editor timeout, Windows host lifetime, or PSP matching.
namespace dokodemo::input32_regression_detail {
using script::Table;
using script::Value;
using script::Status;

inline void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error("input32 regression: " + message);
}
inline void StatusIs(const script::Event& event, Status expected, const char* where) {
    Require(event.status==expected,std::string(where)+": "+event.diagnostic.text());
}
template<class F> inline bool Refuses(F action) {
    try { action(); } catch (const std::exception&) { return true; }
    return false;
}
inline void Put(std::vector<uint8_t>& bytes, size_t at, uint32_t word, unsigned width=4) {
    for (unsigned i=0;i<width;++i) bytes.at(at+i)=uint8_t(word>>(8*i));
}
inline void Append(std::vector<uint8_t>& code, std::initializer_list<uint8_t> bytes) {
    code.insert(code.end(),bytes.begin(),bytes.end());
}
inline void Text(std::vector<uint8_t>& code, const std::string& text) {
    Require(text.find('\0')==std::string::npos,"synthetic inline string excludes terminator");
    code.push_back(0x8E);code.insert(code.end(),text.begin(),text.end());code.push_back(0);
}
inline script::Program Program(std::vector<uint8_t> code) {
    std::vector<uint8_t> bytes(28+code.size());
    Put(bytes,0,0x0043534B);Put(bytes,4,0x20030204);Put(bytes,12,uint32_t(code.size()));
    std::copy(code.begin(),code.end(),bytes.begin()+28);
    script::Program program;script::Diagnostic error;
    Require(program.load_test_fixture(std::move(bytes),error),"load explicit synthetic program: "+error.text());
    return program;
}
inline Value Code(uint32_t pc) {
    Value value;value.kind=Value::Kind::Code;value.word=0x70000000u|pc;return value;
}
inline Value Header(uint32_t length, uint32_t flags=0) {
    Require(length<=0xFFF && !(flags&0xF0000FFFu),"finite synthetic array header");
    Value value;value.kind=Value::Kind::ArrayHeader;value.word=0x90000000u|flags|length;return value;
}
inline Value Nil() { Value value;value.kind=Value::Kind::Scalar;return value; }
inline bool Same(const Value& a, const Value& b) {
    return a.kind==b.kind && a.word==b.word && a.table==b.table && a.index==b.index && a.cp932==b.cp932
        && !a.frame_owner.owner_before(b.frame_owner) && !b.frame_owner.owner_before(a.frame_owner)
        && !a.context_owner.owner_before(b.context_owner) && !b.context_owner.owner_before(a.context_owner);
}
inline void NilIs(const Value& value, const char* where) {
    Require(value.kind==Value::Kind::Scalar && value.word==0,where);
}
inline std::string Prompt() { return std::string(173,'p')+"\x83\x67\x83\x8D"; }
inline std::string Answer() { return std::string(181,'a')+"\x83\x67"; }
inline std::string Auxiliary() { return std::string(193,'r')+"\x83\x8D"; }
inline std::string Marker() { return std::string(159,'m')+"\x83\x67"; }

struct RequestFixture {
    script::Program program;
    uint32_t input_pc=0,continuation_pc=0,idle_pc=0,error_pc=0;
};
inline RequestFixture Request(uint8_t argc=2, uint8_t operands=2, bool unsupported_successor=false) {
    std::vector<uint8_t> code{0x30,operands,0x80,61};
    Text(code,Marker());code.push_back(0x70);
    for (uint8_t i=0;i<operands;++i) Append(code,{0x90,i,0});
    RequestFixture f;f.input_pc=uint32_t(code.size());Append(code,{0x8B,0x20,argc});
    f.continuation_pc=uint32_t(code.size());if(unsupported_successor)code.push_back(0x00);code.push_back(0x38);
    f.idle_pc=uint32_t(code.size());Append(code,{0x30,0});
    f.error_pc=uint32_t(code.size());Append(code,{0x00,0x38});
    f.program=Program(std::move(code));return f;
}
inline void InputResultIs(script::Vm& vm, const Value& reference,
                          const std::string& text, const std::string& auxiliary) {
    Require(reference.kind==Value::Kind::Reference && reference.table==Table::InputResult && uint32_t(reference.table)==4
            && reference.index==0 && !reference.frame_owner.expired(),"native result is a live weak buffer reference");
    const auto header=vm.load_reference(reference);
    Require(header.kind==Value::Kind::ArrayHeader && header.word==0x90000002u,"fixed result header declares exactly two cells");
    const auto first=vm.load_reference(vm.indexed_reference(reference,0));
    const auto second=vm.load_reference(vm.indexed_reference(reference,1));
    if (text.empty()) {
        NilIs(first,"empty text produces first NIL");NilIs(second,"empty text discards auxiliary and produces second NIL");
    } else {
        Require(first.kind==Value::Kind::String && first.cp932==text && second.kind==Value::Kind::String
                && second.cp932==auxiliary,"two owning strings preserve exact text and auxiliary bytes");
    }
    Require(Refuses([&]{(void)vm.indexed_reference(reference,2);})
            && Refuses([&]{(void)vm.read_slot(Table::InputResult,3);}),"result array has no third payload or fourth backing cell");
}

// Group 1: malformed requests fail at the actual builtin; successful requests
// retain their original arguments across repeated host copies and paused runs.
inline void ShapeAndRetainedState8() {
    const std::vector<std::pair<uint8_t,std::vector<Value>>> invalid{
        {0,{}},{1,{Value::integer(2)}},
        {3,{Value::integer(2),Value::string(Prompt()),Value::integer(7)}},
        {2,{}},{2,{Value::integer(2)}},
        {2,{Value::string("not an integer mode"),Value::string(Prompt())}},
        {2,{Nil(),Value::string(Prompt())}},
        {2,{Value::integer(2),Value::integer(7)}}
    };
    for (const auto& test:invalid) {
        auto f=Request(test.first,uint8_t(test.second.size()));script::Vm vm(f.program);uint32_t hosts=0;
        vm.host_service([&](uint8_t,const std::vector<Value>&){++hosts;return Nil();});
        StatusIs(vm.start(test.second),Status::Running,"invalid shape start");
        const auto error=vm.run(50);
        Require(error.status==Status::Stopped && error.pc==f.input_pc && error.opcode==0x8B
                && !error.diagnostic.reason.empty() && !vm.input_waiting() && vm.display_state()==0
                && vm.event_scope_depth()==0 && hosts==0,"invalid shape fails before any input ownership or host fallback");
        Require(vm.stack().size()==test.second.size()+1 && vm.stack()[0].signed_integer()==61
                && vm.result().cp932==Marker(),"rejected request keeps earlier operands and prior result");
        Require(!vm.complete_input(Answer(),Auxiliary()) && Refuses([&]{(void)vm.input_arguments();}),"failed request admits no late completion or arguments");
        const auto executed=vm.executed();const auto again=vm.step();
        Require(again.diagnostic.text()==error.diagnostic.text() && vm.executed()==executed,"first shape diagnostic remains the failure");
    }
    auto f=Request();script::Vm vm(f.program);
    std::vector<Value> caller{Value::integer(2),Value::string(Prompt())};
    StatusIs(vm.start(caller),Status::Running,"retained request start");caller[1].cp932="mutated caller temporary";
    const auto event=vm.run(50);
    Require(event.status==Status::Input && event.pc==f.input_pc && event.next_pc==f.continuation_pc
            && vm.pc()==f.continuation_pc && vm.input_waiting() && vm.display_state()==8 && vm.event_scope_depth()==1
            && vm.result().kind==Value::Kind::Unknown && vm.stack().size()==1 && vm.stack()[0].signed_integer()==61,
            "builtin32 clears old result, consumes exactly two arguments and suspends state8 with one retained scope");
    auto copy=vm.input_arguments();
    Require(copy.size()==2 && copy[0].signed_integer()==2 && copy[1].cp932==Prompt(),"state8 owns original request order and bytes");
    copy[0]=Value::integer(99);copy[1].cp932="mutated host copy";copy.clear();
    const auto before=vm.executed();StatusIs(vm.step(),Status::Waiting,"state8 step pause");
    StatusIs(vm.run(25),Status::Waiting,"state8 budget pause");
    const auto retained=vm.input_arguments();
    Require(retained.size()==2 && retained[0].signed_integer()==2 && retained[1].cp932==Prompt()
            && vm.executed()==before && vm.pc()==f.continuation_pc && vm.event_scope_depth()==1,
            "input_arguments copies cannot transfer or alter VM originals and pending execution cannot consume the continuation");
}

// Group 2: completion shapes and ownership are checked through real indexed
// references, then retained weak references are tested across restart/death.
inline void OwnedResultsAndWeakRetirement() {
    auto f=Request();
    const std::vector<std::pair<std::string,std::string>> answers{
        {Answer(),Auxiliary()},{Answer(),""},{"","auxiliary must be ignored"}
    };
    for (const auto& expected:answers) {
        script::Vm vm(f.program);StatusIs(vm.start({Value::integer(2),Value::string(Prompt())}),Status::Running,"result start");
        StatusIs(vm.run(50),Status::Input,"result request");
        auto text=expected.first,auxiliary=expected.second;
        if (!text.empty()) {
            Require(Refuses([&]{(void)vm.complete_input(text,std::string("a\0b",3));})
                    && vm.input_waiting() && vm.display_state()==8 && vm.event_scope_depth()==1
                    && vm.input_arguments()[1].cp932==Prompt() && vm.result().kind==Value::Kind::Unknown,
                    "invalid auxiliary cannot publish a half-completed owning result or lose request originals");
        }
        Require(vm.complete_input(text,auxiliary) && !vm.input_waiting() && vm.display_state()==2
                && vm.event_scope_depth()==0 && vm.pc()==f.continuation_pc,"completion closes its input scope without executing caller");
        text.assign(200,'x');auxiliary.assign(200,'y');
        const auto reference=vm.result();InputResultIs(vm,reference,expected.first,expected.second);
        const auto element=vm.indexed_reference(reference,0);const auto owned=vm.load_reference(element);
        Require(Refuses([&]{(void)vm.input_arguments();}) && !vm.complete_input("late","late")
                && Same(vm.result(),reference),"completion releases request originals and refuses a second publication");
        StatusIs(vm.start_at(f.idle_pc,{}),Status::Running,"retire result by restart");
        Require(reference.frame_owner.expired() && element.frame_owner.expired()
                && Refuses([&]{(void)vm.load_reference(reference);}) && Refuses([&]{vm.store_reference(element,Nil());})
                && !vm.complete_input("stale completion","stale auxiliary"),"restart retires result references and admits no completion before a new request");
        if (!expected.first.empty()) Require(owned.cp932==expected.first,"copied owning string survives buffer retirement without pinning buffer");
    }
    Value retired,retired_element,owned;
    {
        script::Vm vm(f.program);vm.start({Value::integer(2),Value::string(Prompt())});
        StatusIs(vm.run(50),Status::Input,"destroyed owner request");Require(vm.complete_input(Answer(),Auxiliary()),"destroyed owner completion");
        retired=vm.result();retired_element=vm.indexed_reference(retired,1);owned=vm.load_reference(retired_element);
    }
    script::Vm replacement(f.program);replacement.start_at(f.idle_pc,{});
    Require(retired.frame_owner.expired() && retired_element.frame_owner.expired() && owned.cp932==Auxiliary()
            && Refuses([&]{(void)replacement.load_reference(retired_element);}),"VM death expires weak result owners while copied strings stay owned");
}

struct CallerFixture { script::Program program;uint32_t copy_pc=0,after_copy_pc=0,callee_pc=0; };
inline CallerFixture Caller() {
    // Actual caller-owned array [header, text, auxiliary] is passed to a real
    // callee; only a weak native input-result reference comes back through31.
    std::vector<uint8_t> code{0x30,8,0x4E,1,0,2,0,0x91,2,0};
    Text(code,"old destination text");Append(code,{0x51,0x91,3,0});Text(code,"old destination auxiliary");
    Append(code,{0x51,0x80,37,0x91,1,0});
    const auto call_pc=uint32_t(code.size());Append(code,{0x20,1,0,0,0});
    Append(code,{0x91,4,0,0x89,0x51,0x91,1,0,0x90,4,0});Text(code,Marker());code.push_back(0x70);
    CallerFixture f;f.copy_pc=uint32_t(code.size());code.push_back(0x57);f.after_copy_pc=uint32_t(code.size());
    // F4 really reads both copied cells in the caller before its host consumer.
    Append(code,{0x91,1,0,0x85,0xF4,0x91,1,0,0x80,1,0xF4,0x90,0,0,0x8B,0x7D,3,0x38});
    f.callee_pc=uint32_t(code.size());Put(code,call_pc+2,f.callee_pc,3);
    Append(code,{0x30,1,0x91,0,0,0x8B,0x7A,1,0x80,2});Text(code,Prompt());
    Append(code,{0x8B,0x20,2,0x89,0x70,0x31});f.program=Program(std::move(code));return f;
}
inline void StepTo(script::Vm& vm, uint32_t target, const char* where) {
    for (unsigned i=0;vm.pc()!=target && i<100;++i) StatusIs(vm.step(),Status::Running,where);
    Require(vm.pc()==target,std::string(where)+": finite target reached");
}
inline void CallerCopyCase(bool empty) {
    auto f=Caller();script::Vm vm(f.program);Value callee_reference;uint32_t consumers=0;
    const auto text=empty?std::string{}:Answer(),auxiliary=empty?std::string{}:Auxiliary();
    vm.host_service([&](uint8_t id,const std::vector<Value>& arguments) {
        if (id==0x7A) {
            Require(arguments.size()==1 && arguments[0].kind==Value::Kind::Reference,"callee exports its actual frame reference");
            callee_reference=arguments[0];return Value::string("prior callee result");
        }
        Require(id==0x7D && arguments.size()==3 && arguments[2].cp932==Prompt()
                && callee_reference.frame_owner.expired() && vm.call_depth()==0,"real caller consumer runs only after callee retirement");
        if (empty) {NilIs(arguments[0],"caller reads first NIL");NilIs(arguments[1],"caller reads second NIL");}
        else Require(arguments[0].cp932==text && arguments[1].cp932==auxiliary,"F4 caller consumer reads copied original strings after source mutation");
        Require(vm.stack().size()==1 && vm.stack()[0].signed_integer()==37,"earlier caller operand survives input, ordinary return,57 and cell reads");
        ++consumers;return Value::integer(91);
    });
    StatusIs(vm.start({Value::string(Prompt())}),Status::Running,"linked caller start");
    StatusIs(vm.run(100),Status::Input,"linked callee input");Require(vm.call_depth()==1,"input reached through actual call20");
    Require(vm.complete_input(text,empty?"discarded":auxiliary),"linked callee completion");
    const auto input_reference=vm.result();StepTo(vm,f.copy_pc,"return input reference to caller");
    Require(callee_reference.frame_owner.expired() && Refuses([&]{(void)vm.load_reference(callee_reference);})
            && Same(vm.read_slot(Table::Frame,4),input_reference),"ordinary frame reference expires while distinct returned result buffer stays readable");
    const auto destination=vm.stack().at(vm.stack().size()-2);const auto result_before=vm.result();
    const auto stack_before=vm.stack().size();const auto executed=vm.executed();
    const auto copied=vm.step();
    Require(copied.status==Status::Running && copied.opcode==0x57 && copied.next_pc==f.after_copy_pc
            && f.after_copy_pc==f.copy_pc+1 && vm.executed()==executed+1 && vm.stack().size()+2==stack_before
            && Same(vm.result(),result_before),"true57 is one ordinary instruction consuming two references without changing result30");
    Require(vm.load_reference(destination).word==0x90000002u,"57 retains caller array header");
    InputResultIs(vm,input_reference,text,auxiliary);
    const auto src0=vm.indexed_reference(input_reference,0),src1=vm.indexed_reference(input_reference,1);
    const auto dst0=vm.indexed_reference(destination,0),dst1=vm.indexed_reference(destination,1);
    vm.store_reference(src0,Value::string("source changed after copy"));vm.store_reference(src1,Value::string("source auxiliary changed"));
    if (empty) {NilIs(vm.load_reference(dst0),"copied NIL independent of source overwrite");NilIs(vm.load_reference(dst1),"second copied NIL independent of overwrite");}
    else Require(vm.load_reference(dst0).cp932==text && vm.load_reference(dst1).cp932==auxiliary,"destination owns deep string copies independent of result buffer");
    StatusIs(vm.run(50),Status::Complete,"actual caller cell consumer");Require(consumers==1,"caller consumes exactly once");
    vm.store_reference(dst0,Value::string("caller changed its own text"));
    Require(vm.load_reference(src0).cp932=="source changed after copy" && vm.load_reference(src1).cp932=="source auxiliary changed",
            "destination changes cannot mutate either source string");
}
inline void ResultToCallerArrayAndConsumer() { CallerCopyCase(false);CallerCopyCase(true); }

struct CopyFixture { script::Program program;uint32_t copy_pc=0,after_pc=0; };
inline CopyFixture Copy(const std::vector<Value>& cells, uint8_t lhs, uint8_t rhs,
                        uint8_t count, bool lhs_reference=true, bool rhs_reference=true) {
    Require(cells.size()<=255 && uint32_t(lhs)+count<cells.size(),"finite copy fixture cells");
    std::vector<uint8_t> code{0x30,uint8_t(cells.size()),0x80,37};
    Append(code,{uint8_t(lhs_reference?0x91:0x90),lhs,0,uint8_t(rhs_reference?0x91:0x90),rhs,0});
    Text(code,Marker());code.push_back(0x70);CopyFixture f;f.copy_pc=uint32_t(code.size());code.push_back(0x57);
    f.after_pc=uint32_t(code.size());
    for (uint8_t i=0;i<count;++i) Append(code,{0x90,uint8_t(lhs+1+i),0});
    Append(code,{0x8B,0x7D,count,0x38});f.program=Program(std::move(code));return f;
}
inline void CopyCase(const std::vector<Value>& cells, uint8_t lhs, uint8_t rhs, const std::vector<Value>& expected,
                      bool source_unchanged=true) {
    auto f=Copy(cells,lhs,rhs,uint8_t(expected.size()));script::Vm vm(f.program);uint32_t consumers=0;
    vm.host_service([&](uint8_t id,const std::vector<Value>& arguments) {
        Require(id==0x7D && arguments.size()==expected.size(),"finite array caller consumer shape");
        for (size_t i=0;i<expected.size();++i) Require(Same(arguments[i],expected[i]),"finite array caller consumes expected value at "+std::to_string(i));
        ++consumers;return Value::integer(91);
    });
    StatusIs(vm.start(cells),Status::Running,"finite copy start");StepTo(vm,f.copy_pc,"finite copy setup");
    const auto result=vm.result();const auto size=vm.stack().size();StatusIs(vm.step(),Status::Running,"finite57");
    Require(vm.pc()==f.after_pc && vm.stack().size()+2==size && Same(vm.result(),result)
            && Same(vm.read_slot(Table::Frame,lhs),cells[lhs]),"finite57 retains result, header and earlier operand");
    if (source_unchanged) {
        const auto length=cells[rhs].word&0xFFF;
        for (uint32_t i=0;i<=length;++i) Require(Same(vm.read_slot(Table::Frame,uint16_t(rhs+i)),cells[rhs+i]),"disjoint source remains unchanged");
    }
    StatusIs(vm.run(50),Status::Complete,"finite array caller consumer");Require(consumers==1,"finite consumer executes once");
}
// Group 4: lengths are masked, unequal tails keep the observed release quirk,
// and an overlapping finite case distinguishes forward copy from a snapshot.
inline void FiniteArrayDomainsAndFailures() {
    const auto a=Value::string("source A"),b=Value::string("source B"),c=Value::string("source C");
    const auto d0=Value::string("old0"),d1=Value::string("old1"),d2=Value::string("old2"),d3=Value::string("old3");
    CopyCase({Header(4,0x12000),d0,d1,d2,d3,Header(2,0x34000),a,b},0,5,{a,b,Nil(),d3});
    CopyCase({Header(4),d0,d1,d2,d3,Header(0)},0,5,{Nil(),d1,d2,d3});
    CopyCase({Header(1),d0,Header(3),a,b,c},0,2,{a});
    CopyCase({Header(0),Header(0),Value::integer(77)},0,1,{});
    CopyCase({Header(2),a,b,Value::integer(77)},0,0,{a,b});
    // Source header0/data1..3; destination header2/data3..5. Writing dst3
    // changes the value later read as src3, so the last copy must be A.
    CopyCase({Header(3),a,Header(3),c,d2,d3},2,0,{a,Header(3),a},false);

    struct Invalid { std::vector<Value> cells;uint8_t lhs,rhs;bool lhs_reference,rhs_reference; };
    const std::vector<Invalid> invalid{
        {{Header(1),d0,Header(1),a},0,2,false,true}, // value instead of lhs reference
        {{Header(1),d0,Header(1),a},0,2,true,false}, // value instead of rhs reference
        {{Value::integer(2),d0,Header(1),a},0,2,true,true},
        {{Header(1),d0,Value::string("not a header"),a},0,2,true,true},
        {{Header(2),d0,d1,Header(2),a},0,3,true,true}, // truncated source extent
        {{Header(2),a,b,Header(2),d0},3,0,true,true}  // truncated destination extent
    };
    for (const auto& test:invalid) {
        auto f=Copy(test.cells,test.lhs,test.rhs,0,test.lhs_reference,test.rhs_reference);script::Vm vm(f.program);uint32_t consumers=0;
        vm.host_service([&](uint8_t,const std::vector<Value>&){++consumers;return Nil();});
        StatusIs(vm.start(test.cells),Status::Running,"invalid array start");StepTo(vm,f.copy_pc,"invalid array setup");
        const auto result=vm.result();const auto error=vm.step();
        Require(error.status==Status::Stopped && error.pc==f.copy_pc && error.opcode==0x57
                && !error.diagnostic.reason.empty() && consumers==0 && Same(vm.result(),result)
                && vm.step().diagnostic.text()==error.diagnostic.text(),"bad array operands/extents stop at57 without a fabricated caller success");
        // Extent failures may already have copied an earlier element. No
        // atomic/rollback guarantee is invented for this forward operation.
    }
}

struct QueueFixture {script::Program program;uint32_t input_pc=0,continuation_pc=0,queued_pc=0,idle_pc=0,error_pc=0;};
inline QueueFixture Queue(bool outer_scope) {
    std::vector<uint8_t> code{0x30,2,0x80,37};if(outer_scope)code.push_back(0x0D);
    Append(code,{0x90,0,0,0x90,1,0});QueueFixture f;f.input_pc=uint32_t(code.size());Append(code,{0x8B,0x20,2});
    f.continuation_pc=uint32_t(code.size());if(outer_scope)code.push_back(0x0E);
    Append(code,{0x89,0x8B,0x7D,1,0x38});f.queued_pc=uint32_t(code.size());
    Append(code,{0x30,3,0x90,0,0,0x90,1,0,0x8B,0x7C,2,0x32});
    f.idle_pc=uint32_t(code.size());Append(code,{0x30,0});f.error_pc=uint32_t(code.size());Append(code,{0x00,0x38});
    f.program=Program(std::move(code));return f;
}
inline void QueuedCompletion(bool outer_scope) {
    auto f=Queue(outer_scope);script::Vm vm(f.program);uint32_t callbacks=0,consumers=0;
    vm.host_service([&](uint8_t id,const std::vector<Value>& arguments) {
        if (id==0x7C) {
            Require(arguments.size()==2 && arguments[0].cp932==std::string(150,char('a'+callbacks))
                    && arguments[1].signed_integer()==int32_t(73+callbacks),"queued input callbacks retain FIFO owned arguments");
            ++callbacks;return Value::string("temporary callback result");
        }
        Require(id==0x7D && arguments.size()==1 && callbacks==2,"input continuation waits for both callback returns");
        InputResultIs(vm,arguments[0],Answer(),Auxiliary());++consumers;return Value::integer(91);
    });
    StatusIs(vm.start({Value::integer(2),Value::string(Prompt())}),Status::Running,"queued completion start");
    StatusIs(vm.run(50),Status::Input,"queued completion request");
    const auto token=vm.context_value();
    for (int32_t i=0;i<2;++i) {
        auto temporary=Value::string(std::string(150,char('a'+i)));
        Require(vm.post_callback(token,Code(f.queued_pc),temporary,Value::integer(73+i))==0,"enqueue while input owns nesting");
        temporary.cp932="mutated enqueue temporary";
    }
    Require(vm.queued_callbacks()==2 && callbacks==0,"state8 dispatches no queued callback");
    Require(vm.complete_input(Answer(),Auxiliary()),"queued completion publication");
    if (outer_scope) {
        Require(vm.pc()==f.continuation_pc && vm.event_scope_depth()==1 && vm.queued_callbacks()==2
                && vm.result().table==Table::InputResult,"completion releases only its own nested scope");
        StatusIs(vm.step(),Status::Running,"outer0E releases remaining scope");
    }
    Require(vm.pc()==f.queued_pc && vm.event_scope_depth()==0 && vm.queued_callbacks()==1 && callbacks==0
            && consumers==0,"eligible completion/release enters ONE callback and leaves second queued");
    for (uint32_t i=0;i<2;++i) {
        StatusIs(vm.run(4),Status::HostYield,"queued input callback body");
        Require(callbacks==i+1 && consumers==0 && vm.result().cp932=="temporary callback result","callback body cannot consume input continuation");
        StatusIs(vm.run(1),Status::HostYield,"queued input callback32");
    }
    Require(vm.callbacks_returned()==2 && vm.queued_callbacks()==0 && vm.pc()==f.continuation_pc+uint32_t(outer_scope)
            && vm.stack().size()==1 && vm.stack()[0].signed_integer()==37,"FIFO returns restore actual suspended input continuation and caller operand");
    InputResultIs(vm,vm.result(),Answer(),Auxiliary());StatusIs(vm.run(20),Status::Complete,"queued input actual consumer");
    Require(consumers==1,"input result reaches its continuation once");

    // Restart while input is pending abandons its queue. There is deliberately
    // no claim that this API distinguishes old/new UI generations once a new
    // request itself is pending; that belongs to the host contract.
    StatusIs(vm.start({Value::integer(2),Value::string(Prompt())}),Status::Running,"restart pending request start");
    StatusIs(vm.run(50),Status::Input,"restart pending request");
    Require(vm.post_callback(token,Code(f.queued_pc),Value::string("must not dispatch"),Value::integer(99))==0,"pending request has queued work to abandon");
    StatusIs(vm.start_at(f.idle_pc,{}),Status::Running,"pending input restart");
    Require(!vm.input_waiting() && vm.queued_callbacks()==0 && vm.event_scope_depth()==0
            && !vm.complete_input("late old answer","late old auxiliary"),"restart rejects late publication outside an input request and discards old queue");
    const auto error=vm.run(20);const auto executed=vm.executed();
    Require(error.status==Status::Stopped && error.pc==f.error_pc && error.opcode==0
            && error.diagnostic.reason=="Unsupported opcode semantics","next real unsupported opcode remains an honest cut");
    Require(!vm.complete_input(Answer(),Auxiliary()) && vm.run(20).diagnostic.text()==error.diagnostic.text()
            && vm.executed()==executed,"late completion never revives or replaces first unsupported diagnostic");
}

inline void NestedInputRestoresDeadline() {
    std::vector<uint8_t> code{0x30,1,0x91,0,0,0x8B,0x7E,1,0x80,27,0x80,7,0x44};
    const auto root_pc=uint32_t(code.size());Append(code,{0x90,0,0,0x8B,0x7D,1,0x38});
    const auto input_pc=uint32_t(code.size());
    Append(code,{0x30,3,0x91,1,0,0x8B,0x7A,1,0x0D,0x90,0,0,0x90,1,0,0x8B,0x20,2,
                 0x0E,0x89,0x90,1,0,0x8B,0x7B,2,0x32});
    const auto queued_pc=uint32_t(code.size());Append(code,{0x30,3,0x90,0,0,0x90,1,0,0x8B,0x7C,2,0x32});
    auto program=Program(std::move(code));script::Vm vm(program);Value root_reference,input_frame_reference;
    uint32_t input_consumers=0,queued=0,root_consumers=0;
    vm.host_service([&](uint8_t id,const std::vector<Value>& arguments) {
        if (id==0x7E || id==0x7A) {
            Require(arguments.size()==1 && arguments[0].kind==Value::Kind::Reference,"nested input captures actual root/callback frame");
            (id==0x7E?root_reference:input_frame_reference)=arguments[0];return Value::integer(9);
        }
        if (id==0x7B) {
            Require(arguments.size()==2 && arguments[1].cp932==Prompt() && queued==0,"nested input consumer precedes queued callbacks");
            InputResultIs(vm,arguments[0],Answer(),Auxiliary());++input_consumers;
        } else if (id==0x7C) {
            Require(arguments.size()==2 && arguments[0].signed_integer()==int32_t(70+queued)
                    && arguments[1].cp932==Prompt() && input_frame_reference.frame_owner.expired(),"queued callback starts after input callback frame really retires");
            Require(vm.peek_reference(root_reference).cp932==Prompt(),"saved sleeping root frame stays owned");++queued;
        } else {
            Require(id==0x7D && arguments.size()==1 && arguments[0].cp932==Prompt() && queued==2,"restored deadline reaches root caller consumer");++root_consumers;
        }
        return Value::string("temporary nested result");
    });
    vm.clock_tick(100);StatusIs(vm.start({Value::string(Prompt())}),Status::Running,"nested input sleeping root start");
    StatusIs(vm.run(50),Status::Yield,"nested input root deadline");const auto root=vm.current_witness();
    Require(vm.pc()==root_pc && vm.deadline_tick()==107 && vm.result().signed_integer()==9,"root owns real seven-tick deadline and result9");
    const auto token=vm.context_value();Require(vm.post_callback(token,Code(input_pc),Value::integer(2),Value::string(Prompt()))==0,"input callback interrupts sleeping root");
    StatusIs(vm.run(50),Status::Input,"nested callback input request");
    for (int32_t i=0;i<2;++i) Require(vm.post_callback(token,Code(queued_pc),Value::integer(70+i),Value::string(Prompt()))==0,"queue behind nested input envelope");
    vm.clock_tick(150);Require(vm.complete_input(Answer(),Auxiliary()) && vm.event_scope_depth()==1
            && vm.queued_callbacks()==2 && queued==0,"nested input publication cannot drain before outer scope/envelope release");
    StatusIs(vm.run(4),Status::HostYield,"nested input0E and consumer");
    Require(vm.event_scope_depth()==0 && input_consumers==1 && queued==0 && vm.queued_callbacks()==2
            && !input_frame_reference.frame_owner.expired(),"0E releases scope but finite callback envelope still defers queue");
    StatusIs(vm.run(1),Status::HostYield,"input callback32 and first queue dispatch");
    Require(vm.pc()==queued_pc && vm.queued_callbacks()==1 && input_frame_reference.frame_owner.expired(),"32 retires input callback and dispatches only first queued entry");
    for (uint32_t i=0;i<2;++i) {
        StatusIs(vm.run(4),Status::HostYield,"nested queued callback body");vm.clock_tick(151+i);
        StatusIs(vm.run(1),Status::HostYield,"nested queued callback32");
    }
    const auto restored=vm.current_witness();
    Require(vm.pc()==root_pc && restored.frame_id==root.frame_id && restored.code_entry==root.code_entry
            && !restored.event_envelope_present && vm.callbacks_returned()==3 && queued==2 && root_consumers==0
            && vm.result().signed_integer()==9 && vm.stack().size()==1 && vm.stack()[0].signed_integer()==27
            && vm.deadline_tick()==159 && vm.deadline_writes()==1 && vm.event_scope_depth()==0,
            "three returns restore actual root identity/result/operand and preserve seven remaining ticks at clock152");
    const auto executed=vm.executed();vm.clock_tick(158);StatusIs(vm.run(20),Status::Yield,"restored input root deadline pending");
    Require(vm.executed()==executed && root_consumers==0,"input and callback completion cannot bypass restored deadline");
    vm.clock_tick(159);StatusIs(vm.run(20),Status::Complete,"restored input root deadline due");Require(root_consumers==1,"sleeping root consumes once at restored deadline");
}
// Group 5: direct/nested completion, one-entry drains, saved deadline, pending
// restart and stable unsupported successor form one linked lifecycle group.
inline void CompletionQueueDeadlineAndRestart() {
    QueuedCompletion(false);QueuedCompletion(true);NestedInputRestoresDeadline();
    auto f=Request(2,2,true);script::Vm vm(f.program);
    StatusIs(vm.start({Value::integer(2),Value::string(Prompt())}),Status::Running,"unsupported input successor start");
    StatusIs(vm.run(50),Status::Input,"unsupported successor input request");
    Require(vm.complete_input(Answer(),Auxiliary()),"complete before unsupported successor");
    const auto reference=vm.result();const auto error=vm.run(20);const auto executed=vm.executed();
    Require(error.status==Status::Stopped && error.pc==f.continuation_pc && error.opcode==0
            && error.diagnostic.reason=="Unsupported opcode semantics" && !vm.input_waiting()
            && Same(vm.result(),reference),"completed input reaches and retains the actual first unsupported successor");
    Require(!vm.complete_input("late","late") && vm.run(20).diagnostic.text()==error.diagnostic.text()
            && vm.executed()==executed && Same(vm.result(),reference),"late input completion cannot mask an unsupported continuation");
}
} // namespace dokodemo::input32_regression_detail

inline void GameInput32RegressionChecks() {
    dokodemo::input32_regression_detail::ShapeAndRetainedState8();
    dokodemo::input32_regression_detail::OwnedResultsAndWeakRetirement();
    dokodemo::input32_regression_detail::ResultToCallerArrayAndConsumer();
    dokodemo::input32_regression_detail::FiniteArrayDomainsAndFailures();
    dokodemo::input32_regression_detail::CompletionQueueDeadlineAndRestart();
}
