#include "gameplay/room_talk_state.h"
#include "gameplay/boot_to_display.h"
#include "gameplay/cold_room_actor.h"
#include "script_vm.h"
#include "gameplay/room_activity_phase.h"
#include "gameplay/room_notebook.h"
#include "gameplay/room_word_entry.h"
#include "recovered/calendar_native.h"
#include "recovered/clock_native.h"
#include "recovered/package_storage_native.h"
#include "recovered/rng_native.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

using namespace dokodemo;

namespace dokodemo::gameplay {
struct RoomTalkOpcodeTests {
    using Value = RoomTalkState::Value;
    using Kind = RoomTalkState::Kind;

    static RoomTalkState Execute(std::vector<unsigned char> code,int initial,std::vector<Value> arguments) {
        RoomTalkState interpreter;
        interpreter.phase_=RoomTalkState::Phase::running;
        interpreter.locals_.push_back({Kind::integer,initial});
        interpreter.stack_=std::move(arguments);
        // Production dispatch reads the operation, then yields for a DELAY.
        code.insert(code.end(),{0x85,0x44});
        const auto end=code.size();
        interpreter.code_.emplace(0,std::move(code));
        interpreter.Execute(nullptr);
        if(interpreter.pc_!=end) throw std::runtime_error("Synthetic KSC program did not reach DELAY");
        return interpreter;
    }

    static RoomTalkState Execute(unsigned opcode,int initial,std::vector<Value> arguments) {
        return Execute(std::vector<unsigned char>{static_cast<unsigned char>(opcode)},initial,std::move(arguments));
    }

    static void CheckValue(const Value& actual, Kind kind, int expected, unsigned opcode) {
        if(actual.kind!=kind || actual.data!=expected)
            throw std::runtime_error("Unexpected KSC value for opcode "+std::to_string(opcode));
    }

    static void Builtins() {
        const auto check=[](unsigned char id,std::vector<Value> args,Kind type,int expected) {
            const auto argc=static_cast<unsigned char>(args.size());
            args.insert(args.begin(),{Kind::integer,57});
            const auto vm=Execute({0x8b,id,argc,0x89},0,std::move(args));
            if(vm.stack_.size()!=2) throw std::runtime_error("Wrong builtin stack effect");
            CheckValue(vm.stack_[0],Kind::integer,57,id);
            CheckValue(vm.stack_[1],type,expected,id);
        };
        check(0x01,{{}},Kind::integer,0);
        check(0x01,{{Kind::truth,1}},Kind::integer,1);
        check(0x01,{{Kind::integer,-1}},Kind::integer,2);
        check(0x01,{{Kind::calendar_time,112}},Kind::integer,3);
        check(0x01,{{Kind::fixed14,112}},Kind::integer,4);
        check(0x01,{{Kind::code,0x1234}},Kind::integer,7);
        check(0x02,{{Kind::integer,12},{Kind::string,0,"abc"}},Kind::nil,0);
        check(0x02,{},Kind::nil,0);
        check(0x03,{{Kind::integer,-9}},Kind::truth,1);
        check(0x03,{{Kind::truth,0}},Kind::truth,0);
        check(0x05,{{Kind::integer,0x4001}},Kind::integer,1);
        check(0x05,{{Kind::integer,-1}},Kind::integer,-1);
        check(0x06,{{Kind::integer,0x4201}},Kind::integer,0x201);
        check(0x06,{{Kind::integer,0x2000}},Kind::integer,-8192);
        check(0x0d,{{Kind::string,0,"A"}},Kind::integer,0x41);
        check(0x0d,{{Kind::string,0,"\x82\xa0"}},Kind::integer,0xa082);
        check(0x10,{{Kind::integer,-37}},Kind::integer,37);
        check(0x10,{{Kind::integer,-0x08000000}},Kind::integer,-0x08000000);
        check(0x50,{{Kind::string,0,"\x82\xa0" "A"},{Kind::string,0,"A"}},Kind::integer,2);
        check(0x50,{{Kind::string,0,"abc"},{Kind::string,0,"a"}},Kind::nil,0);
        check(0x50,{{Kind::string,0,"abc"},{Kind::string,0,"x"}},Kind::nil,0);
        check(0x50,{{Kind::string,0,"abc"},{Kind::string,0,""}},Kind::nil,0);
        const auto pair=Execute({0x8b,0x07,2,0x89},0,{{Kind::integer,-9000},{Kind::integer,9000}});
        CheckValue(pair.stack_.at(0),Kind::fixed14,-0x08000000+8191,0x07);
        check(0x05,{pair.stack_.at(0)},Kind::integer,-8192);
        check(0x06,{pair.stack_.at(0)},Kind::integer,8191);
        const auto as_text=Execute({0x8b,0x08,1,0x89},0,{{Kind::integer,-1234}});
        const auto byte_text=Execute({0x8b,0x0e,1,0x89},0,{{Kind::integer,0xa082}});
        const auto truncated=Execute({0x8b,0x0e,1,0x89},0,{{Kind::integer,0x0041}});
        const std::string kana="\x81\x5b\x82\xa1\x82\xef\x82\xc1";
        std::string bounded;
        for(int i=0;i<15;++i) bounded+="\x82\xa0";
        check(0x70,{{Kind::string,0,bounded}},Kind::truth,1);
        check(0x70,{{Kind::string,0,bounded+"\x82\xa0"}},Kind::truth,0);
        check(0x70,{{Kind::string,0,kana+"A"}},Kind::truth,0);
        check(0x70,{{Kind::string,0,"\x82\xc1"}},Kind::truth,0);
        const auto first=Execute({0x8b,0x71,1,0x89},0,{{Kind::string,0,kana}});
        const auto last=Execute({0x8b,0x72,1,0x89},0,{{Kind::string,0,kana}});
        const auto invalid=Execute({0x8b,0x72,1,0x89},0,{{Kind::string,0,kana+"A"}});
        const auto skipped=Execute({0x8b,0x71,1,0x89},0,{{Kind::string,0,"\x82\xc1"}});
        if(first.stack_.at(0).text!="\x82\xa2" || last.stack_.at(0).text!="\x82\xa6")
            throw std::runtime_error("Wrong KSC normalized kana endpoint");
        CheckValue(invalid.stack_.at(0),Kind::nil,0,0x72);
        CheckValue(skipped.stack_.at(0),Kind::nil,0,0x71);
        if(as_text.stack_.at(0).text!="-1234" || byte_text.stack_.at(0).text!="\x82\xa0" ||
           truncated.stack_.at(0).text!="A") throw std::runtime_error("Wrong KSC text conversion");
    }

    static void ArraysAndLimits() {
        constexpr int limits[]={-0x08000000,0x07ffffff,-8192,8191,0};
        for(unsigned char key:{0,1,2,3,4,255}) {
            const auto vm=Execute({0xa0,key},0,{});
            CheckValue(vm.stack_.at(0),Kind::integer,limits[std::min<unsigned>(key,4)],0xa0);
        }
        for(unsigned opcode:{0x56u,0x57u}) {
            const auto vm=Execute({
                0x30,8,                   // frame
                0x4e,0,0,3,0, 0x4e,4,0,2,0, // two owned arrays with different lengths
                0x80,9, 0x80,8, 0x80,7, 0x80,3, 0x71,0,0,
                0x80,12, 0x80,34, 0x80,2, 0x71,4,0,
                0x91,0,0, 0x91,4,0, static_cast<unsigned char>(opcode)
            },0,{});
            const auto& dst=*vm.locals_.at(0).cells;
            const auto& src=*vm.locals_.at(4).cells;
            CheckValue(dst.at(0),Kind::integer,12,opcode);
            CheckValue(dst.at(1),Kind::integer,34,opcode);
            CheckValue(dst.at(2),Kind::nil,0,opcode);
            CheckValue(src.at(0),Kind::integer,12,opcode);
            CheckValue(src.at(1),Kind::integer,34,opcode);
            if(vm.stack_.size()!=1-(opcode&1u)) throw std::runtime_error("Wrong array copy stack effect");
            if(opcode==0x56) CheckValue(vm.stack_.back(),Kind::frame_ref,4,opcode);
        }
        const auto short_target=Execute({
            0x30,8,
            0x4e,0,0,1,0, 0x4e,4,0,2,0,
            0x80,12, 0x80,34, 0x80,2, 0x71,4,0,
            0x91,0,0, 0x91,4,0, 0x57,
            0x91,4,0, 0x91,4,0, 0x57
        },0,{});
        CheckValue(short_target.locals_.at(0).cells->at(0),Kind::integer,12,0x57);
        CheckValue(short_target.locals_.at(4).cells->at(1),Kind::integer,34,0x57);
        if(short_target.locals_.at(0).cells->size()!=1) throw std::runtime_error("Array copy resized the target");
    }

    static void ReferenceOperations() {
        const auto cleared=Execute({0x91,0,0,0x4f,0x89},42,{});
        CheckValue(cleared.locals_.at(0),Kind::nil,0,0x4f);
        CheckValue(cleared.stack_.at(0),Kind::frame_ref,0,0x4f);
        RoomTalkState vm;
        vm.phase_=RoomTalkState::Phase::running;
        vm.code_.emplace(0,std::vector<unsigned char>{
            0x30,4, 0x4e,0,0,2,0, 0x80,12, 0x80,34, 0x80,2, 0x71,0,0,
            0x91,0,0, 0xf7,0, 0x4f, 0x85,0x44
        });
        vm.Execute(nullptr);
        if(vm.random_requested()!=2) throw std::runtime_error("Wrong F7 array bound");
        vm.SupplyRandom(2);
        if(vm.random_requested()!=2) throw std::runtime_error("F7 accepted an invalid draw");
        vm.SupplyRandom(1);
        CheckValue(vm.stack_.back(),Kind::element,1,0xf7);
        CheckValue(vm.Dereference(vm.stack_.back()),Kind::integer,34,0xf7);
        vm.Execute(nullptr);
        CheckValue(vm.locals_.at(0).cells->at(0),Kind::integer,12,0xf7);
        CheckValue(vm.locals_.at(0).cells->at(1),Kind::nil,0,0xf7);
        if(vm.random_requested() || !vm.stack_.empty()) throw std::runtime_error("F7 did not resume once");
    }

    static void BulkTables() {
        const auto row=[](std::vector<Value> entries) {
            return Value{Kind::array,0,{},0,std::make_shared<std::vector<Value>>(std::move(entries))};
        };
        const auto ints=[&](std::initializer_list<int> entries) {
            std::vector<Value> cells;
            for(int entry:entries) cells.push_back({Kind::integer,entry});
            return row(std::move(cells));
        };
        const auto first=ints({3,7}), second=ints({0,7}), targets=row({first,second});
        const auto condition=ints({0,8,2, 1,0,7}), actions=ints({1,0,42, 0,3,1});
        const auto selected=Execute({0x8b,0x62,3,0x89},0,{targets,condition,actions});
        CheckValue(selected.stack_.at(0),Kind::integer,1,0x62);
        CheckValue(first.cells->at(0),Kind::integer,2,0x62);
        CheckValue(first.cells->at(1),Kind::integer,42,0x62);
        CheckValue(second.cells->at(1),Kind::integer,7,0x62);
        const auto no_match=Execute({0x8b,0x62,3,0x89},0,{targets,row({}),ints({0,0,101})});
        CheckValue(no_match.stack_.at(0),Kind::integer,0,0x62);
        CheckValue(first.cells->at(0),Kind::integer,2,0x62);
        const auto updates=ints({0,1,8, 0,2,9, 99,16,0, 1,0,12345});
        const auto applied=Execute({0x8b,0x61,2,0x89},0,{targets,updates});
        CheckValue(applied.stack_.at(0),Kind::integer,0,0x61);
        for(const auto& target:{first,second}) {
            CheckValue(target.cells->at(0),Kind::integer,8,0x61);
            CheckValue(target.cells->at(1),Kind::integer,12345,0x61);
        }
    }

    static void Timers() {
        RoomTalkState vm;
        vm.phase_=RoomTalkState::Phase::running;
        vm.script_ticks_=100;
        vm.code_.emplace(0,std::vector<unsigned char>{
            0x80,5,0x41,0x8d,0x80,3,0x40,0x8d,0x43,0x8d,0x85,0x44
        });
        vm.Execute(nullptr);
        CheckValue(vm.stack_.at(0),Kind::integer,-5,0x41);
        if(vm.pc_!=7 || vm.script_deadline_!=108) throw std::runtime_error("KSC base-relative wait");
        for(unsigned tick=101;tick<108;++tick) {
            vm.Step(0,false,false,nullptr);
            if(vm.script_ticks_!=tick || vm.pc_!=7) throw std::runtime_error("KSC deadline resumed early");
        }
        vm.Step(0,false,false,nullptr);
        if(vm.pc_!=9 || vm.script_deadline_!=109) throw std::runtime_error("KSC one-tick yield");
        CheckValue(vm.stack_.at(1),Kind::integer,3,0x40);
        vm.Step(0,false,false,nullptr);
        if(vm.pc_!=12 || vm.script_deadline_!=0) throw std::runtime_error("KSC one-tick resume");
        CheckValue(vm.stack_.at(2),Kind::integer,4,0x43);
    }

    static void StopAndSuspend() {
        for(unsigned char opcode:{0x0f,0x38}) {
            RoomTalkState vm;
            vm.phase_=RoomTalkState::Phase::running;
            vm.code_.emplace(0,std::vector<unsigned char>{opcode,0x85,0x44});
            vm.Execute(nullptr);
            vm.Step(0,false,false,nullptr);
            if(vm.pc_!=1 || !vm.stack_.empty() || vm.ordinary_source_return() ||
               vm.phase_!=(opcode==0x0f?RoomTalkState::Phase::suspended:RoomTalkState::Phase::done))
                throw std::runtime_error("KSC stop or suspend state mismatch");
        }
    }

    static void LocalTables(const std::filesystem::path& directory) {
        for(unsigned actor=0;actor<5;++actor) {
            RoomTalkState vm;
            vm.phase_=RoomTalkState::Phase::running;
            vm.actor_number_=actor;
            vm.LoadScriptLocals(directory);
            if(vm.script_locals_.size()!=15) throw std::runtime_error("Missing actor KSC local table");
            const auto& original=vm.script_locals_.at(8);
            if(original.kind!=Kind::string || original.text.empty())
                throw std::runtime_error("Missing packed actor KSC local string");
            const auto initial=original.text;
            vm.code_.emplace(0,std::vector<unsigned char>{
                0x93,7,0,0x85,0xf4,              // alias array's first member
                0x92,8,0,                         // same initial string via direct slot
                0x80,42,0x80,1,0x72,7,0,         // replace one array member
                0x93,7,0,0x85,0xf4, 0x92,8,0,    // read again via both paths
                0x92,7,0,0x85,0xf5,0x4f,         // clear through indexed reference
                0x92,8,0, 0x93,7,0,0xf0,         // verify direct value and original length
                0x85,0x44
            });
            vm.Execute(nullptr);
            if(vm.stack_.size()!=6 || vm.stack_[0].text!=initial || vm.stack_[1].text!=initial)
                throw std::runtime_error("Actor local table accesses disagree");
            CheckValue(vm.stack_[2],Kind::integer,42,0x72);
            CheckValue(vm.stack_[3],Kind::integer,42,0x92);
            CheckValue(vm.stack_[4],Kind::nil,0,0xf5);
            CheckValue(vm.stack_[5],Kind::integer,7,0xf0);
            if(vm.script_locals_.at(9).kind!=Kind::string)
                throw std::runtime_error("Array initializer changed an untouched actor local");
            vm.stack_={{Kind::context}};
            vm.pc_=0;
            vm.code_.at(0)={0x97,8,0, 0x80,63,0x51, 0x92,8,0, 0x85,0x44};
            vm.Execute(nullptr);
            CheckValue(vm.stack_.at(0),Kind::integer,63,0x97);
            vm.stack_={{Kind::context}};
            vm.pc_=0;
            vm.code_.at(0)={0x96,8,0, 0x85,0x44};
            vm.Execute(nullptr);
            CheckValue(vm.stack_.at(0),Kind::integer,63,0x96);
            vm.stack_={{Kind::integer,7},{Kind::integer,12},{Kind::truth,1},
                       {Kind::integer,50},{Kind::integer,75},{Kind::integer,100}};
            vm.pc_=0;
            vm.code_.at(0)={0x8b,0x35,5,0x89,0x85,0x44};
            vm.Execute(nullptr);
            if(vm.stack_.size()!=2) throw std::runtime_error("Wrong local argument stack effect");
            CheckValue(vm.stack_[0],Kind::integer,7,0x35);
            CheckValue(vm.stack_[1],Kind::nil,0,0x35);
            CheckValue(vm.script_locals_.at(2),Kind::integer,12,0x35);
            CheckValue(vm.script_locals_.at(3),Kind::truth,1,0x35);
            CheckValue(vm.script_locals_.at(6),Kind::integer,100,0x35);
            CheckValue(vm.script_locals_.at(7),Kind::script_local_array,7,0x35);
        }
    }

    static void ContextTables() {
        const auto elapsed=Execute(0x9a,0,{{Kind::context}});
        CheckValue(elapsed.stack_.at(0),Kind::integer,0,0x9a);
        RoomTalkState vm;
        vm.phase_=RoomTalkState::Phase::running;
        vm.friends_introduction_=true;
        vm.global_arrays_.emplace(0x12,std::vector<Value>{{Kind::integer,8},{Kind::integer,9}});
        vm.friends_cells_[0x2a3c]={Kind::array,0,{},0,
            std::make_shared<std::vector<Value>>(2,Value{Kind::integer,9})};
        vm.code_.emplace(0,std::vector<unsigned char>{
            0x80,72, 0x80,1,0x73,0x12,0,
            0x80,73, 0x80,1,0x75,0x3c,0x2a, 0x85,0x44
        });
        vm.Execute(nullptr);
        CheckValue(vm.global_arrays_.at(0x12).at(0),Kind::integer,72,0x73);
        CheckValue(vm.global_arrays_.at(0x12).at(1),Kind::integer,9,0x73);
        CheckValue(vm.friends_cells_.at(0x2a3c).cells->at(0),Kind::integer,73,0x75);
        CheckValue(vm.friends_cells_.at(0x2a3c).cells->at(1),Kind::integer,9,0x75);
    }

    static void ProcedureArguments() {
        RoomTalkState vm;
        vm.phase_=RoomTalkState::Phase::running;
        vm.procedure_lists_.emplace(42,std::vector<unsigned>{0x100,0x200});
        vm.code_.emplace(0,std::vector<unsigned char>{
            0x80,15, 0x80,19,
            0x24,2,42,0,0, 0x89,0x85,0x44
        });
        vm.code_.emplace(0x100,std::vector<unsigned char>{0x85,0x70,0x31});
        vm.code_.emplace(0x200,std::vector<unsigned char>{
            0x90,0,0, 0x90,1,0, 0xe1,0x70,0x31
        });
        vm.Execute(nullptr);
        if(vm.random_requested()!=2 || vm.stack_.size()!=2)
            throw std::runtime_error("Wrong KSC procedure selection arguments");
        vm.SupplyRandom(1);
        if(vm.pc_!=0x200 || vm.locals_.size()!=2)
            throw std::runtime_error("Wrong KSC selected frame");
        CheckValue(vm.locals_[0],Kind::integer,15,0x24);
        CheckValue(vm.locals_[1],Kind::integer,19,0x24);
        vm.Execute(nullptr);
        CheckValue(vm.stack_.at(0),Kind::integer,-4,0x24);
        if(!vm.callers_.empty() || vm.pc_!=12)
            throw std::runtime_error("Wrong KSC selected procedure return");
    }

    static void Associations() {
        RoomTalkState vm;
        vm.phase_=RoomTalkState::Phase::running;
        vm.pool_.emplace(1,"fruit");
        vm.pool_.emplace(2,"vegetable");
        vm.code_.emplace(0,std::vector<unsigned char>{
            0x30,2,
            0x91,0,0,0x8f,0x51, 0x91,1,0,0x8f,0x51,
            0x90,0,0,0x88,1,0,0,0xfd,0x80,42,0x51,
            0x90,0,0,0x88,1,0,0,0xfc,
            0x90,0,0,0x88,2,0,0,0xfc,
            0x90,1,0,0x88,1,0,0,0xfc,
            0x90,0,0,0x88,1,0,0,0xfd,0x70,
            0x90,0,0,0x88,1,0,0,0xfc,
            0x85,0x44
        });
        vm.Execute(nullptr);
        if(vm.stack_.size()!=4) throw std::runtime_error("Wrong associative table stack effect");
        CheckValue(vm.stack_[0],Kind::integer,42,0xfc);
        CheckValue(vm.stack_[1],Kind::nil,0,0xfc);
        CheckValue(vm.stack_[2],Kind::nil,0,0x8f);
        CheckValue(vm.stack_[3],Kind::nil,0,0xfd);
        if(vm.locals_.at(0).entries->size()!=1 || !vm.locals_.at(1).entries->empty())
            throw std::runtime_error("KSC associative table allocations were shared");
    }

    static void DebugText() {
        for(unsigned char opcode:{0x49,0x4a}) {
            const auto vm=Execute({opcode,0x14,0,0,'A',0,0x80,64},15,{});
            CheckValue(vm.stack_.at(0),Kind::integer,64,opcode);
            if(vm.message_wait_ || vm.visible() || vm.message_pc_!=0)
                throw std::runtime_error("Retail debug text changed the visible message");
        }
    }

    static void CallbackReturn(unsigned char opcode,bool enable=true) {
        RoomTalkState interpreter;
        interpreter.phase_=RoomTalkState::Phase::running;
        interpreter.excursion_=true;
        interpreter.pc_=0x1c69;
        interpreter.queued_outing_callback_=0x1c59;
        std::vector<unsigned char> handler{0x3a};
        if(enable) handler.push_back(0x3b);
        if(opcode==0x1c) {
            // The nonlocal return must abandon the callback's ordinary frame.
            handler.insert(handler.end(),{0x20,0,0x00,0x10,0,0x38});
            interpreter.code_.emplace(0x1000,std::vector<unsigned char>{0x30,1,opcode});
        } else handler.push_back(opcode);
        interpreter.code_.emplace(0x1c59,std::move(handler));
        interpreter.code_.emplace(0x1c69,std::vector<unsigned char>{0x44,0x89,0x90,0,0,0x43});
        if(!enable) interpreter.outing_scene_request_=12;
        interpreter.locals_={{Kind::integer,51}};
        interpreter.result_={Kind::integer,52};
        interpreter.stack_={{Kind::integer,53},{Kind::integer,1}};

        // Execute the real callback delivery and return. Only a disabled
        // caller must wait for the scene owner before consuming another opcode.
        interpreter.Step(0,true,true,nullptr,false);
        const auto expected=enable?RoomTalkState::Phase::running:RoomTalkState::Phase::outing_scene;
        if(!interpreter.error_.empty() || interpreter.phase_!=expected ||
           interpreter.pc_!=0x1c6a || interpreter.outing_callback_ || !interpreter.callers_.empty())
            throw std::runtime_error("KSC callback did not restore its caller: "+interpreter.error_);
        if(!enable) {
            interpreter.Step(0,true,true,nullptr,false);
            if(interpreter.pc_!=0x1c6a || interpreter.FinishOutingScene(13) ||
               !interpreter.FinishOutingScene(12))
                throw std::runtime_error("KSC callback resumed without its scene owner");
        }
        interpreter.Step(0,true,true,nullptr,false);
        if(!interpreter.error_.empty() || interpreter.pc_!=0x1c6f || interpreter.stack_.size()!=3)
            throw std::runtime_error("KSC callback did not resume its saved instruction stream");
        CheckValue(interpreter.stack_[0],Kind::integer,53,opcode);
        CheckValue(interpreter.stack_[1],Kind::integer,52,opcode);
        CheckValue(interpreter.stack_[2],Kind::integer,51,opcode);
    }

    static void ProcedureRingCall(unsigned char opcode,unsigned owner) {
        RoomTalkState vm;
        vm.phase_=RoomTalkState::Phase::running;
        vm.ordinary_=true;
        const unsigned index=owner==1?0:0x102;
        const auto lo=static_cast<unsigned char>(index&255);
        const auto hi=static_cast<unsigned char>(index>>8);
        const auto scope=static_cast<unsigned char>(owner);
        auto cells=std::make_shared<std::vector<Value>>(2);
        if(owner==1) vm.locals_={{Kind::array,0,{},0,cells}};
        if(owner==2) {
            vm.script_locals_.resize(index+3);
            vm.script_locals_[index]={Kind::script_local_array,2,{},index};
        }
        if(owner==3) vm.global_arrays_[index].resize(2);
        if(owner==4) vm.friends_cells_[index]={Kind::array,0,{},0,cells};
        const auto slot=[&](unsigned i) -> const Value& {
            if(owner==2) return vm.script_locals_.at(index+1+i);
            if(owner==3) return vm.global_arrays_.at(index).at(i);
            return cells->at(i);
        };

        const unsigned char argc=(opcode&1U)?0:2;
        for(unsigned call=0;call<3;++call) {
            constexpr unsigned entry=0x10;
            const auto target=static_cast<unsigned char>(0x50+call*0x10);
            std::vector<unsigned char> program;
            switch(opcode) {
            case 0x2c: program={opcode,argc,lo,hi,scope,target,0,0}; break;
            case 0x2d: program={opcode,scope,lo,hi,target,0,0}; break;
            case 0x2e: program={opcode,scope,lo,hi,argc}; break;
            case 0x2f: program={opcode,scope,lo,hi}; break;
            default: throw std::runtime_error("Unexpected KSC procedure ring test command");
            }
            const unsigned resume=entry+unsigned(program.size());
            program.insert(program.end(),{0x89,0x43});
            vm.code_.insert_or_assign(entry,std::move(program));
            std::vector<unsigned char> callee{0x30,argc,0x43};
            if(argc) callee.insert(callee.end(),{0x90,1,0,0x70});
            callee.push_back(0x31);
            vm.code_.emplace(target,std::move(callee));
            vm.pc_=entry;
            vm.result_={Kind::integer,65};
            vm.stack_={{Kind::integer,63}};
            if(opcode&2U) vm.stack_.push_back({Kind::code,target});
            if(argc) {
                vm.stack_.push_back({Kind::integer,71});
                vm.stack_.push_back({Kind::integer,72});
            }

            vm.Step(0,true,true,nullptr,false);
            if(!vm.error_.empty() || vm.pc_!=unsigned(target)+3 || vm.callers_.size()!=1 ||
               vm.callers_.back().resume!=resume || vm.stack_.size()!=1 || vm.locals_.size()!=argc)
                throw std::runtime_error("KSC procedure ring did not enter its callee: "+vm.error_);
            CheckValue(slot(call%2),Kind::code,target,opcode);
            if(call) CheckValue(slot((call-1)%2),Kind::code,target-0x10,opcode);
            CheckValue(vm.result_,Kind::integer,65,opcode);
            if(argc) {
                CheckValue(vm.locals_[0],Kind::integer,71,opcode);
                CheckValue(vm.locals_[1],Kind::integer,72,opcode);
            }

            vm.Step(0,true,true,nullptr,false);
            if(!vm.error_.empty() || vm.pc_!=resume+2 || !vm.callers_.empty() || vm.stack_.size()!=2)
                throw std::runtime_error("KSC procedure ring did not return to its caller: "+vm.error_);
            CheckValue(vm.stack_[0],Kind::integer,63,opcode);
            CheckValue(vm.stack_[1],Kind::integer,argc?72:65,opcode);
        }
    }

    static void WordKeyboardResult(int mode,bool accept) {
        RoomTalkState vm;
        vm.phase_=RoomTalkState::Phase::running;
        vm.ordinary_=true;
        vm.code_.emplace(0,std::vector<unsigned char>{0x8b,0x20,2,0x89,0x57,0x43});
        auto dest=std::make_shared<std::vector<Value>>(2,Value{Kind::integer,9});
        vm.locals_={{Kind::array,0,{},0,dest}};
        vm.stack_={{Kind::integer,41},{Kind::frame_ref,0},{Kind::integer,mode},
                   {Kind::string,0,mode==2?"initial":""}};
        vm.result_={Kind::integer,49};

        vm.Step(0,true,true,nullptr,false);
        if(!vm.error_.empty() || !vm.word_entry_requested() || vm.pc_!=3 ||
           vm.stack_.size()!=2 || vm.result_.kind!=Kind::nil ||
           vm.keyboard_initial_reading_.has_value()!=(mode==2) ||
           (mode==2 && *vm.keyboard_initial_reading_!="initial"))
            throw std::runtime_error("KSC word editor did not wait with its original operands: "+vm.error_);
        vm.Step(0,true,true,nullptr,false);
        if(vm.pc_!=3 || !vm.word_entry_requested())
            throw std::runtime_error("KSC word editor advanced before host completion");
        vm.FinishWordEntry(accept?"Written":"",accept?"Reading":"");
        if(vm.keyboard_initial_reading_ || vm.result_.kind!=Kind::array || !vm.result_.cells)
            throw std::runtime_error("KSC word editor did not publish a pair");
        vm.Step(0,true,true,nullptr,false);
        if(!vm.error_.empty() || vm.pc_!=6 || vm.stack_.size()!=1)
            throw std::runtime_error("KSC word editor did not resume and copy its pair: "+vm.error_);
        CheckValue(vm.stack_.back(),Kind::integer,41,0x20);
        if(accept) {
            if(dest->at(0).kind!=Kind::string || dest->at(0).text!="Written" ||
               dest->at(1).kind!=Kind::string || dest->at(1).text!="Reading")
                throw std::runtime_error("KSC word editor returned the wrong string order");
        } else {
            CheckValue(dest->at(0),Kind::nil,0,0x20);
            CheckValue(dest->at(1),Kind::nil,0,0x20);
        }
    }

    static void AppendChildCall(std::vector<unsigned char>& program,unsigned char opcode,unsigned char target) {
        if(opcode==0x28) program.insert(program.end(),{0x80,41,0x80,52,opcode,2,target,0,0});
        else program.insert(program.end(),{opcode,target,0,0});
    }

    static void ChildScriptSchedule(unsigned char opcode) {
        RoomTalkState vm;
        vm.phase_=RoomTalkState::Phase::running;
        vm.ordinary_=true;
        vm.locals_={{Kind::integer,88}};
        vm.stack_={{Kind::integer,61}};
        vm.result_={Kind::integer,99};
        vm.script_timer_base_=50;
        vm.script_locals_.resize(6);
        const auto args=static_cast<unsigned char>(opcode==0x28?2:0);
        std::vector<unsigned char> root;
        AppendChildCall(root,opcode,0x40);
        root.push_back(0x43);
        AppendChildCall(root,opcode,0x80);
        root.insert(root.end(),{0x43,0x80,4,0x44});
        AppendChildCall(root,opcode,0x40);
        root.push_back(0x0f);
        vm.code_.emplace(0,std::move(root));
        const auto worker=[args](unsigned char index,unsigned char finish) {
            std::vector<unsigned char> code{0x30,args,0x93,index,0};
            if(args) code.insert(code.end(),{0x90,0,0});
            else code.insert(code.end(),{0x80,14});
            code.insert(code.end(),{0x51,0x21,0xc0,0,0,0x93,static_cast<unsigned char>(index+1),0});
            if(args) code.insert(code.end(),{0x90,1,0});
            else code.insert(code.end(),{0x80,17});
            code.insert(code.end(),{0x51,0x93,static_cast<unsigned char>(index+2),0,0x8d,0x51,finish});
            return code;
        };
        vm.code_.emplace(0x40,worker(0,0x31));
        vm.code_.emplace(0x80,worker(3,0x38));
        vm.code_.emplace(0xc0,std::vector<unsigned char>{0x30,0,0x80,101,0x70,0x80,2,0x44,0x31});
        const auto step=[&] {
            vm.Step(0,true,true,nullptr,false);
            if(!vm.error_.empty() || vm.executing_child_ || vm.locals_.size()!=1 ||
               vm.stack_.size()!=1 || vm.script_timer_base_!=50)
                throw std::runtime_error("Child KSC modified its parent execution: "+vm.error_);
            CheckValue(vm.locals_[0],Kind::integer,88,opcode);
            CheckValue(vm.stack_[0],Kind::integer,61,opcode);
            CheckValue(vm.result_,Kind::integer,99,opcode);
        };
        step();
        if(vm.child_scripts_.size()!=1 || vm.child_scripts_.front().calls.size()!=1)
            throw std::runtime_error("Child KSC did not keep its procedure frame over a yield");
        const auto first_lifetime=vm.child_scripts_.front().generation;
        CheckValue(vm.child_scripts_.front().result,Kind::integer,101,opcode);
        CheckValue(vm.script_locals_[0],Kind::integer,args?41:14,opcode);
        CheckValue(vm.script_locals_[1],Kind::nil,0,opcode);
        step();
        if(vm.child_scripts_.size()!=2 || vm.child_scripts_.back().phase!=RoomTalkState::Phase::running)
            throw std::runtime_error("Child KSC reused a live sibling");
        CheckValue(vm.script_locals_[3],Kind::integer,args?41:14,opcode);
        step();
        CheckValue(vm.script_locals_[1],Kind::integer,args?52:17,opcode);
        CheckValue(vm.script_locals_[2],Kind::integer,2,opcode);
        CheckValue(vm.script_locals_[4],Kind::nil,0,opcode);
        step();
        CheckValue(vm.script_locals_[4],Kind::integer,args?52:17,opcode);
        CheckValue(vm.script_locals_[5],Kind::integer,2,opcode);
        for(unsigned i=0;i<3;++i) step();
        if(vm.phase_!=RoomTalkState::Phase::suspended || vm.child_scripts_.size()!=2 ||
           vm.child_scripts_.front().generation==first_lifetime ||
           vm.child_scripts_.front().phase!=RoomTalkState::Phase::running)
            throw std::runtime_error("Child KSC did not recycle its first inactive slot");
        step();
        step();
        if(vm.phase_!=RoomTalkState::Phase::suspended ||
           vm.child_scripts_.front().phase!=RoomTalkState::Phase::done)
            throw std::runtime_error("Child KSC did not finish while its parent was suspended");
    }

    static void ChildScriptIsolation(unsigned char opcode) {
        RoomTalkState vm;
        vm.phase_=RoomTalkState::Phase::running;
        vm.ordinary_=true;
        vm.result_={Kind::integer,77};
        std::vector<unsigned char> root,worker;
        AppendChildCall(root,opcode,0x30);
        root.push_back(0x43);
        AppendChildCall(worker,opcode,0x60);
        worker.push_back(0x31);
        vm.code_.emplace(0,std::move(root));
        vm.code_.emplace(0x30,std::move(worker));
        vm.code_.emplace(0x60,std::vector<unsigned char>{0x48,0});
        vm.Step(0,true,true,nullptr,false);
        if(vm.error_.find("Unsupported child KSC operation")==std::string::npos ||
           vm.executing_child_ || vm.visible() || vm.root_children_.size()!=1 ||
           vm.child_scripts_.size()!=2 || vm.child_scripts_.front().children.size()!=1)
            throw std::runtime_error("Nested KSC child escaped its script-only scope: "+vm.error_);
        CheckValue(vm.result_,Kind::integer,77,opcode);
    }

    static void ChildScriptSiblingOrder(unsigned char opcode) {
        RoomTalkState vm;
        vm.phase_=RoomTalkState::Phase::running;
        vm.ordinary_=true;
        vm.script_locals_.resize(2);
        std::vector<unsigned char> root,worker;
        AppendChildCall(root,opcode,0x30);
        AppendChildCall(root,opcode,0x60);
        root.push_back(0x43);
        AppendChildCall(worker,opcode,0xc0);
        worker.push_back(0x38);
        vm.code_.emplace(0,std::move(root));
        vm.code_.emplace(0x30,std::move(worker));
        vm.code_.emplace(0x60,std::vector<unsigned char>{0x93,0,0,0x80,22,0x51,0x38});
        vm.code_.emplace(0xc0,std::vector<unsigned char>{0x93,1,0,0x92,0,0,0x51,0x38});
        vm.Step(0,true,true,nullptr,false);
        if(!vm.error_.empty() || vm.root_children_.size()!=2 || vm.child_scripts_.size()!=3)
            throw std::runtime_error("KSC sibling scheduling failed: "+vm.error_);
        CheckValue(vm.script_locals_[1],Kind::integer,22,opcode);
    }

    static void ChildScriptForeignFrame(unsigned char opcode) {
        RoomTalkState vm;
        vm.phase_=RoomTalkState::Phase::running;
        vm.ordinary_=true;
        vm.locals_={{Kind::integer,11}};
        vm.script_locals_={{Kind::frame_ref,0}};
        std::vector<unsigned char> root;
        AppendChildCall(root,opcode,0x40);
        root.push_back(0x43);
        vm.code_.emplace(0,std::move(root));
        // A reference read indirectly from shared data still has its root owner.
        vm.code_.emplace(0x40,std::vector<unsigned char>{0x30,1,0x92,0,0,0x80,20,0x51,0x38});
        vm.Step(0,true,true,nullptr,false);
        if(vm.error_.find("different context")==std::string::npos || vm.executing_child_)
            throw std::runtime_error("KSC child accepted a foreign frame reference: "+vm.error_);
        CheckValue(vm.locals_.at(0),Kind::integer,11,opcode);
    }

    static Value HostBuiltin(unsigned char opcode,std::vector<Value> args,
                             RoomTalkState::ScriptHostCallbacks callbacks={}) {
        RoomTalkState vm;
        vm.phase_=RoomTalkState::Phase::running;
        vm.ordinary_=true;
        vm.result_={Kind::integer,98};
        vm.BindScriptHostCallbacks(std::move(callbacks));
        const auto argc=static_cast<unsigned char>(args.size());
        vm.code_.emplace(0,std::vector<unsigned char>{0x8b,opcode,argc,0x89,0x43});
        vm.stack_={{Kind::integer,71}};
        vm.stack_.insert(vm.stack_.end(),args.begin(),args.end());
        vm.Step(0,true,true,nullptr,false);
        if(!vm.error_.empty() || vm.pc_!=5 || vm.stack_.size()!=2)
            throw std::runtime_error("KSC host builtin did not restore its operand stack: "+vm.error_);
        CheckValue(vm.stack_[0],Kind::integer,71,opcode);
        return vm.stack_[1];
    }

    static void HostCommand() {
        CheckValue(HostBuiltin(0x24,{{Kind::integer,-8}}),Kind::nil,0,0x24);
        RoomTalkState::ScriptHostCallbacks callbacks;
        unsigned calls=0;
        callbacks.command=[&](int selector,int secondary) {
            if(secondary!=-1) throw std::runtime_error("KSC host command lost its secondary selector");
            ++calls;
            return selector==5?0x08000000:2*selector;
        };
        CheckValue(HostBuiltin(0x24,{{Kind::string,0,"  -13 rest"},{Kind::integer,33}},callbacks),
                   Kind::integer,-26,0x24);
        CheckValue(HostBuiltin(0x24,{{Kind::fixed14,-3*0x4000+4}},callbacks),
                   Kind::integer,-0x08000000,0x24);
        if(calls!=2) throw std::runtime_error("Unexpected number of KSC host commands");
    }

    static void HostQuery() {
        using Tag=RoomTalkState::ScriptHostValue::Tag;
        CheckValue(HostBuiltin(0x40,{{},{},{}}),Kind::nil,0,0x40);
        RoomTalkState::ScriptHostCallbacks callbacks;
        unsigned calls=0;
        callbacks.query=[&](int selector,const auto& a,const auto& b) {
            if(a.tag!=Tag::string || a.text!="word" || b.tag!=Tag::vector || b.payload!=-49)
                throw std::runtime_error("KSC host query altered a typed argument");
            ++calls;
            return selector-int(a.text.size())+b.payload;
        };
        CheckValue(HostBuiltin(0x40,{{Kind::integer,-12},{Kind::string,0,"word"},{Kind::fixed14,-49},
                                     {Kind::integer,31}},callbacks),Kind::integer,-65,0x40);
        if(calls!=1) throw std::runtime_error("Unexpected number of KSC host queries");
    }

    static void HostNotification() {
        CheckValue(HostBuiltin(0x42,{{Kind::integer,92},{Kind::integer,5}}),Kind::nil,0,0x42);
        RoomTalkState::ScriptHostCallbacks callbacks;
        int state=0;
        callbacks.notify=[&](int selector,int value) { state=selector*100+value; };
        CheckValue(HostBuiltin(0x42,{{Kind::string,0," 12"},{Kind::fixed14,3*0x4000+4}},callbacks),
                   Kind::nil,0,0x42);
        if(state!=1205) throw std::runtime_error("KSC host notification was lost or reordered");
    }

    static void ScriptModuleBuiltin(RoomTalkState& vm,Value operand,int expected,unsigned char opcode=0x80,
                                    std::optional<Value> map={}) {
        vm.pc_=0;
        vm.phase_=RoomTalkState::Phase::running;
        vm.ordinary_=true;
        vm.result_={Kind::integer,96};
        vm.stack_={{Kind::integer,61},std::move(operand)};
        if(map) vm.stack_.push_back(std::move(*map));
        vm.code_.insert_or_assign(0,std::vector<unsigned char>{0x8b,opcode,
            static_cast<unsigned char>(map?2:1),0x89,0x43});
        vm.Step(0,true,true,nullptr,false);
        if(!vm.error_.empty() || vm.pc_!=5 || vm.stack_.size()!=2)
            throw std::runtime_error("KSC module load did not return to its caller: "+vm.error_);
        CheckValue(vm.stack_[0],Kind::integer,61,opcode);
        CheckValue(vm.stack_[1],Kind::integer,expected,opcode);
    }

    static void ScriptModuleLoad() {
        // Independently authored KSC with one routine and one local string.
        const std::vector<std::uint8_t> example{
            0x4b,0x53,0x43,0, 4,2,3,0x20, 0,0,0,0, 4,0,0,0, 3,0,0,0,
            0,0, 1,0, 0,0, 0,0, 0x80,17,0x70,0x38, 'o','k',0, 0,0,0,0x50
        };
        RoomTalkState vm;
        ScriptModuleBuiltin(vm,{Kind::string,0,"sample"},-1);
        unsigned reads=0;
        vm.BindScriptModuleReader([&](const std::string& name,std::vector<std::uint8_t>& bytes) {
            ++reads;
            if(name=="opaque") { bytes={42}; return true; }
            if(name!="sample" && name!="7") return false;
            bytes=example;
            return true;
        });
        ScriptModuleBuiltin(vm,{Kind::string,0,"missing"},-1);
        ScriptModuleBuiltin(vm,{Kind::integer,7},0);
        ScriptModuleBuiltin(vm,{Kind::string,0,"opaque"},1);
        ScriptModuleBuiltin(vm,{Kind::string,0,"sample"},2);
        if(vm.script_modules_[0].name!="7" || vm.script_modules_[0].bytes!=example ||
           vm.script_modules_[1].bytes!=std::vector<std::uint8_t>{42} ||
           vm.script_modules_[2].bytes!=example)
            throw std::runtime_error("KSC module handle does not retain the opened resource");
        for(int i=3;i<8;++i) ScriptModuleBuiltin(vm,{Kind::string,0,"sample"},i);
        ScriptModuleBuiltin(vm,{Kind::string,0,"sample"},-1);
        if(reads!=9 || vm.script_modules_.size()!=8)
            throw std::runtime_error("KSC module registration did not preserve its slot limit");
        script::Program original;
        script::Diagnostic error;
        if(original.load_original_bytes(example,"fixture.KSC",error) || original.loaded())
            throw std::runtime_error("Package bytecode was accepted by the original-image path");
    }

    static void ScriptMapRegistration() {
        const std::vector<std::uint8_t> map{
            'k','s','m',0, 2,0,0,0, 'a',0,'b','c',0,0,0,0,
            2,0,0,0, 0,0,0,0, 11,0,0,0, 16,0,0,0, 17,0,0,0
        };
        RoomTalkState vm;
        vm.BindScriptModuleReader([&](const std::string& name,std::vector<std::uint8_t>& bytes) {
            bytes=map;
            if(name=="short") bytes.pop_back();
            if(name=="bad-name") bytes.assign({'k','s','m',0,1,0,0,0,'a','b','c','d'});
            if(name=="wrong-kind") bytes[0]='x';
            return true;
        });
        ScriptModuleBuiltin(vm,{Kind::integer,0},-1,0x91);
        ScriptModuleBuiltin(vm,{Kind::string,0,"map"},0);
        ScriptModuleBuiltin(vm,{Kind::string,0,"short"},1);
        ScriptModuleBuiltin(vm,{Kind::string,0,"bad-name"},2);
        ScriptModuleBuiltin(vm,{Kind::string,0,"wrong-kind"},3);
        for(int i=1;i<=3;++i) ScriptModuleBuiltin(vm,{Kind::integer,i},-1,0x91);
        for(int i=0;i<8;++i) ScriptModuleBuiltin(vm,{Kind::integer,0},i,0x91);
        ScriptModuleBuiltin(vm,{Kind::integer,0},-1,0x91);
        if(vm.script_maps_.size()!=8 || vm.script_maps_[0].names!=std::vector<std::string>{"a","bc"} ||
           vm.script_maps_[0].records!=std::vector<std::array<std::uint32_t,2>>{{0,11},{16,17}})
            throw std::runtime_error("KSC metadata map did not retain its names and aligned records");
    }

    static void ScriptContextRegistration() {
        const std::vector<std::uint8_t> example{
            'K','S','C',0, 4,2,3,0x20, 1,0,0,0, 5,0,0,0, 3,0,0,0,
            0,0, 3,0, 0,0, 0,0, 0x38,0x80,17,0x70,0x38, 'o','k',0,
            0,0,0,0x50, 1,0,0,0x90, 42,0,0,0x20
        };
        RoomTalkState vm;
        vm.BindScriptModuleReader([&](const std::string& name,std::vector<std::uint8_t>& bytes) {
            if(name=="map") bytes={'k','s','m',0,0,0,0,0,0,0,0,0};
            else if(name=="invalid") bytes={'K','S','C',0};
            else bytes=example;
            return true;
        });
        ScriptModuleBuiltin(vm,{Kind::string,0,"context"},0);
        ScriptModuleBuiltin(vm,{Kind::string,0,"invalid"},1);
        ScriptModuleBuiltin(vm,{Kind::string,0,"map"},2);
        ScriptModuleBuiltin(vm,{Kind::integer,2},0,0x91);
        ScriptModuleBuiltin(vm,{Kind::integer,1},-1,0x90,Value{});
        ScriptModuleBuiltin(vm,{Kind::integer,0},-1,0x90,Value{Kind::integer,1});
        ScriptModuleBuiltin(vm,{Kind::integer,-1},-1,0x90,Value{});
        ScriptModuleBuiltin(vm,{Kind::integer,0},0,0x90,Value{});
        ScriptModuleBuiltin(vm,{Kind::integer,0},1,0x90,Value{Kind::integer,0});
        auto& first=vm.registered_scripts_.at(0);
        const auto& second=vm.registered_scripts_.at(1);
        if(first.phase!=RoomTalkState::Phase::done || first.program->header().entrypoint!=1 ||
           first.map_index || second.map_index!=0 || first.program!=second.program ||
           first.generation==second.generation || first.script_locals.at(0).text!="ok")
            throw std::runtime_error("Wrong registered KSC context identity or initial state");
        CheckValue(first.script_locals.at(1),Kind::script_local_array,1,0x90);
        CheckValue(first.script_locals.at(2),Kind::integer,42,0x90);
        first.script_locals.at(2)={Kind::integer,72};
        CheckValue(second.script_locals.at(2),Kind::integer,42,0x90);
        for(int i=2;i<8;++i) ScriptModuleBuiltin(vm,{Kind::integer,0},i,0x90,Value{});
        ScriptModuleBuiltin(vm,{Kind::integer,0},-1,0x90,Value{});
        if(vm.registered_scripts_.size()!=8 || vm.script_modules_[1].program ||
           vm.registered_scripts_[0].phase!=RoomTalkState::Phase::done)
            throw std::runtime_error("KSC context overflow or execution before activation");
    }

    static void ScriptContextActivation() {
        std::vector<std::uint8_t> code{
            0x38,0x30,2,
            0x93,0,0,0x90,0,0,0x90,1,0,0xe0,0x51,
            0x93,1,0,0x88,0,0,0,0x51
        };
        const auto child_operand=code.size()+1;
        code.insert(code.end(),{0x29,0,0,0,0x43,0x92,0,0,0x70,0x38});
        code[child_operand]=static_cast<std::uint8_t>(code.size());
        code.insert(code.end(),{0x93,2,0,0x92,0,0,0x51,0x38});
        std::vector<std::uint8_t> image{
            'K','S','C',0,4,2,3,0x20,1,0,0,0,static_cast<std::uint8_t>(code.size()),0,0,0,
            4,0,0,0,0,0,3,0,0,0,0,0
        };
        image.insert(image.end(),code.begin(),code.end());
        image.insert(image.end(),{'m','o','d',0,0,0,0,0,0,0,0,0,0,0,0,0});
        RoomTalkState vm;
        vm.pool_.emplace(0,"root");
        vm.script_locals_.assign(3,Value{Kind::integer,61});
        vm.BindScriptModuleReader([&](const std::string&,std::vector<std::uint8_t>& bytes) {
            bytes=image;
            return true;
        });
        ScriptModuleBuiltin(vm,{Kind::string,0,"exec"},0);
        ScriptModuleBuiltin(vm,{Kind::integer,0},0,0x90,Value{});
        ScriptModuleBuiltin(vm,{Kind::integer,0},1,0x90,Value{});
        const auto step=[&] {
            vm.Step(0,true,true,nullptr,false);
            if(!vm.error_.empty()) throw std::runtime_error(vm.error_);
        };
        auto& first=vm.registered_scripts_.at(0);
        auto& second=vm.registered_scripts_.at(1);
        const auto activate=[&](int handle,Value a,Value b,int expected) {
            vm.pc_=0;
            vm.phase_=RoomTalkState::Phase::running;
            vm.code_.insert_or_assign(0,std::vector<unsigned char>{0x8b,0xa0,3,0x89,0x0f});
            vm.stack_={{Kind::integer,66},{Kind::integer,handle},std::move(a),std::move(b)};
            step();
            if(vm.stack_.size()!=2 || vm.phase_!=RoomTalkState::Phase::suspended)
                throw std::runtime_error("A0 did not return to its own caller");
            CheckValue(vm.stack_[0],Kind::integer,66,0xa0);
            CheckValue(vm.stack_[1],Kind::integer,expected,0xa0);
        };
        activate(-1,{Kind::integer,1},{Kind::integer,2},-1);
        activate(0,{Kind::frame_ref,0},{Kind::integer,2},-1);
        CheckValue(first.script_locals[0],Kind::nil,0,0xa0);
        const auto initial_generation=first.generation;
        activate(0,{Kind::integer,27},{Kind::integer,15},0);
        if(first.pc!=1 || first.timer_base!=vm.script_ticks_ || first.generation==initial_generation)
            throw std::runtime_error("A0 did not arm a fresh activation at entry");
        CheckValue(first.script_locals[0],Kind::nil,0,0xa0);
        step();
        CheckValue(first.script_locals[0],Kind::integer,42,0xa0);
        if(first.script_locals[1].text!="mod")
            throw std::runtime_error("Registered KSC used its caller's string pool");
        CheckValue(first.script_locals[2],Kind::integer,42,0xa0);
        CheckValue(second.script_locals[0],Kind::nil,0,0xa0);
        CheckValue(vm.script_locals_[0],Kind::integer,61,0xa0);
        step();
        CheckValue(first.result,Kind::integer,42,0xa0);
        if(first.phase!=RoomTalkState::Phase::done)
            throw std::runtime_error("Activated KSC context did not finish");
        const auto generation=first.generation;
        activate(0,{Kind::integer,11},{Kind::integer,9},0);
        CheckValue(first.script_locals[0],Kind::integer,42,0xa0);
        activate(1,{Kind::integer,87},{Kind::integer,-5},0);
        step();
        step();
        if(first.generation==generation || first.phase!=RoomTalkState::Phase::done ||
           second.phase!=RoomTalkState::Phase::done)
            throw std::runtime_error("Registered KSC did not finish both lifetimes");
        CheckValue(first.result,Kind::integer,20,0xa0);
        CheckValue(second.result,Kind::integer,82,0xa0);
    }

    static void ScriptContextRendezvous() {
        const std::vector<std::uint8_t> waiting{
            0x8c,0x80,1,0x8b,0x26,2,0x80,41,0x70,0x38
        };
        const std::vector<std::uint8_t> signalling{
            0x43,0x80,0,0x8b,0x26,1,0x80,99,0x70,0x38
        };
        RoomTalkState vm;
        vm.BindScriptModuleReader([&](const std::string& name,std::vector<std::uint8_t>& bytes) {
            const auto& code=name=="waiting"?waiting:signalling;
            bytes={'K','S','C',0,4,2,3,0x20,0,0,0,0,static_cast<std::uint8_t>(code.size()),0,0,0,
                   0,0,0,0,0,0,0,0,0,0,0,0};
            bytes.insert(bytes.end(),code.begin(),code.end());
            return true;
        });
        ScriptModuleBuiltin(vm,{Kind::string,0,"waiting"},0);
        ScriptModuleBuiltin(vm,{Kind::string,0,"signalling"},1);
        ScriptModuleBuiltin(vm,{Kind::integer,0},0,0x90,Value{});
        ScriptModuleBuiltin(vm,{Kind::integer,1},1,0x90,Value{});
        const auto step=[&] {
            vm.Step(0,true,true,nullptr,false);
            if(!vm.error_.empty()) throw std::runtime_error(vm.error_);
        };
        auto& first=vm.registered_scripts_.at(0);
        auto& second=vm.registered_scripts_.at(1);
        vm.code_.insert_or_assign(0,std::vector<unsigned char>{
            0x8b,0xa0,1,0x89,0x80,1,0x8b,0xa0,1,0x89,0x0f
        });
        vm.pc_=0;
        vm.stack_={{Kind::integer,52},{Kind::integer,0}};
        step();
        step();
        const auto waiting_phase=RoomTalkState::Phase::rendezvous;
        if(first.phase!=waiting_phase || first.rendezvous_credit!=0xffffu ||
           second.rendezvous_credit!=1 || second.phase!=RoomTalkState::Phase::running)
            throw std::runtime_error("BI26 did not account for each context independently");
        step();
        if(first.phase!=waiting_phase || second.phase!=waiting_phase ||
           first.rendezvous_credit || second.rendezvous_credit ||
           first.deadline!=vm.script_ticks_+1 || second.deadline!=first.deadline)
            throw std::runtime_error("BI26 resumed before the zero transition's next tick");
        step();
        if(first.phase!=RoomTalkState::Phase::done || second.phase!=RoomTalkState::Phase::done ||
           vm.phase_!=RoomTalkState::Phase::suspended || vm.stack_.size()!=3)
            throw std::runtime_error("BI26 did not restore both continuations");
        CheckValue(first.result,Kind::integer,41,0x26);
        CheckValue(second.result,Kind::integer,99,0x26);
        CheckValue(vm.stack_[0],Kind::integer,52,0x26);

        vm.pc_=0;
        vm.phase_=RoomTalkState::Phase::running;
        vm.code_.insert_or_assign(0,std::vector<unsigned char>{0x8b,0x26,0,0x89,0x0f});
        step();
        if(vm.phase_!=waiting_phase) throw std::runtime_error("Empty BI26 did not yield");
        step();
        if(vm.phase_!=RoomTalkState::Phase::suspended) throw std::runtime_error("Empty BI26 lost its continuation");
        CheckValue(vm.stack_.back(),Kind::nil,0,0x26);

        vm.pc_=0;
        vm.phase_=RoomTalkState::Phase::running;
        vm.code_.insert_or_assign(0,std::vector<unsigned char>{0x8b,0x26,2,0x0f});
        vm.stack_={{Kind::integer,0},{Kind::integer,-1}};
        vm.Step(0,true,true,nullptr,false);
        if(vm.error_.find("Unknown KSC rendezvous context")==std::string::npos ||
           first.rendezvous_credit || second.rendezvous_credit)
            throw std::runtime_error("Invalid BI26 target partially updated the registry");
    }

    static void ScriptModuleArchive(const std::filesystem::path& game) {
        BootToDisplay boot;
        if(!boot.Initialize() || !boot.InitializeClock({},168) ||
           !boot.MountDataArchive(game/"assets/iso/PSP_GAME/USRDIR/data/DATA.BP"))
            throw std::runtime_error("Cannot mount the locally prepared archive");
        RoomTalkState vm;
        vm.BindScriptModuleReader([&](const auto& name,auto& bytes) { return boot.ReadScriptMember(name,bytes); });
        ScriptModuleBuiltin(vm,{Kind::string,0,"KSNEKO.BPM/absent.KSC"},-1);
        ScriptModuleBuiltin(vm,{Kind::string,0,"KSNEKO.BPM/../NEKO.KSC"},-1);
        int index=0;
        for(std::string actor:{"NEKO","ROBO","USAGI","INU","KAERU"}) {
            const auto name="KS"+actor+".BPM/"+actor+".KSC";
            ScriptModuleBuiltin(vm,{Kind::string,0,name},index);
            const auto& module=vm.script_modules_.at(index++);
            script::Program verified;
            script::Diagnostic error;
            if(!verified.load_original_bytes(module.bytes,name,error))
                throw std::runtime_error("KSC archive module did not match the original identity: "+error.text());
            ScriptModuleBuiltin(vm,{Kind::integer,index-1},index-1,0x90,Value{});
            const auto& context=vm.registered_scripts_.at(index-1);
            if(context.program->source().sha256!=verified.source().sha256 ||
               context.script_locals.size()!=verified.header().local_count ||
               context.phase!=RoomTalkState::Phase::done)
                throw std::runtime_error("Original KSC module did not register as a dormant context");
        }
    }

    static void Run() {
        ScriptModuleLoad();
        ScriptMapRegistration();
        ScriptContextRegistration();
        ScriptContextActivation();
        ScriptContextRendezvous();
        HostCommand();
        HostQuery();
        HostNotification();
        ChildScriptSchedule(0x28);
        ChildScriptIsolation(0x28);
        ChildScriptForeignFrame(0x28);
        ChildScriptSiblingOrder(0x28);
        ChildScriptSchedule(0x29);
        ChildScriptIsolation(0x29);
        ChildScriptForeignFrame(0x29);
        ChildScriptSiblingOrder(0x29);
        WordKeyboardResult(0,true);
        WordKeyboardResult(0,false);
        WordKeyboardResult(2,true);
        WordKeyboardResult(2,false);
        ProcedureRingCall(0x2c,1);
        ProcedureRingCall(0x2d,2);
        ProcedureRingCall(0x2e,3);
        ProcedureRingCall(0x2f,4);
        CallbackReturn(0x32);
        CallbackReturn(0x1c);
        CallbackReturn(0x1c,false);
        Builtins();
        DebugText();
        Associations();
        ProcedureArguments();
        ContextTables();
        StopAndSuspend();
        Timers();
        BulkTables();
        ReferenceOperations();
        ArraysAndLimits();
        constexpr int max=0x07ffffff, min=-0x08000000;
        struct Compound { unsigned even; int lhs,rhs,result; };
        const Compound assignments[]={
            {0x5a,max,1,min}, {0x5c,min,1,max}, {0x5e,0x04000000,2,min},
            {0x60,-11,4,-2}, {0x60,min,-1,min}, {0x62,-11,4,-3},
            {0x64,0x37,0x0e,0x06}, {0x66,0x37,0x0e,0x3f},
            {0x68,0x37,0x0e,0x39}, {0x6a,-17,34,-5}, {0x6c,0x04000001,33,min+2},
        };
        for(const auto& test:assignments) {
            // The handheld's statement-only AND/OR also have room-specific
            // modes and their regression paths are separate.
            const unsigned forms=test.even==0x64 || test.even==0x66 ? 1 : 2;
            for(unsigned form=0;form<forms;++form) {
                const auto opcode=test.even+form;
                const auto vm=Execute(opcode,test.lhs,{{Kind::frame_ref,0},{Kind::integer,test.rhs}});
                CheckValue(vm.locals_.at(0),Kind::integer,test.result,opcode);
                if(vm.stack_.size()!=1-form) throw std::runtime_error("Wrong compound stack effect");
                if(!form) CheckValue(vm.stack_.back(),Kind::integer,test.result,opcode);
            }
        }
        for(unsigned opcode:{0x52u,0x53u,0x54u,0x55u}) {
            const auto initial=opcode<0x54?max:min, updated=opcode<0x54?min:max;
            const auto vm=Execute(opcode,initial,{{Kind::frame_ref,0}});
            CheckValue(vm.locals_.at(0),Kind::integer,updated,opcode);
            if(vm.stack_.size()!=(opcode==0x52?1u:0u)) throw std::runtime_error("Wrong increment stack effect");
            if(opcode==0x52) CheckValue(vm.stack_.back(),Kind::integer,initial,opcode);
        }
        const auto negate=Execute(0xb0,0,{{Kind::integer,min}});
        CheckValue(negate.stack_.at(0),Kind::integer,min,0xb0);
        for(int value:{-1,0,1}) {
            const auto positive=Execute(0xba,0,{{Kind::integer,value}});
            CheckValue(positive.stack_.at(0),Kind::truth,value>0,0xba);
        }
        const auto xor_vm=Execute(0xd6,0,{{Kind::integer,0x37},{Kind::integer,0x0e}});
        CheckValue(xor_vm.stack_.at(0),Kind::integer,0x39,0xd6);
        const auto and_not=Execute(0xd7,0,{{Kind::integer,0x37},{Kind::integer,0x0e}});
        CheckValue(and_not.stack_.at(0),Kind::integer,0x31,0xd7);
        for(unsigned opcode:{0x60u,0x61u,0x62u,0x63u}) {
            bool refused=false;
            try {
                (void)Execute(opcode,5,{{Kind::frame_ref,0},{Kind::integer,0}});
            } catch(const std::runtime_error& error) {
                refused=std::string_view(error.what()).find("by zero")!=std::string_view::npos;
            }
            if(!refused) throw std::runtime_error("KSC compound zero divisor was not rejected");
        }
    }
};
}

void CheckFaceQuestions(const std::filesystem::path&,const recovered::PackageView&,gameplay::RoomTalkState);
namespace {
void Check(bool value,const char* what) { if(!value) throw std::runtime_error(what); }
struct Font : recovered::PackageStorageLease {
    std::vector<std::uint8_t> data,known;
    bool Span(recovered::PackageStorageSpan& out,std::string&) const override {
        out={const_cast<std::uint8_t*>(data.data()),const_cast<std::uint8_t*>(known.data()),
             data.size(),reinterpret_cast<std::uintptr_t>(data.data()),1,1,1,0,true,false}; return true;
    }
};
void Run(const std::filesystem::path& assets,const recovered::PackageView& font,int slot,bool discard,unsigned roll) {
    gameplay::AcceptedProfile p{};
    p.nickname.Assign("Toro"); p.player.Assign("Nana");
    gameplay::RoomTalkState talk;
    std::string error;
    Check(talk.Load(assets,p,error),error.c_str());
    talk.Step(0x2000,false,false,&font);
    talk.Step(0x2000,true,false,&font);
    Check(!talk.consumed() && talk.prompt_visible() && !talk.visible(),"Held Circle became a new talk event");
    talk.Step(0,true,false,&font); talk.Step(0x2000,true,false,&font);
    Check(talk.consumed() && !talk.prompt_visible() && talk.actor_command()==42,"Missing main room input/actor42");
    unsigned previous=0,awards=0,modals=0;
    bool nonbreaking_suffix=false;
    std::vector<unsigned> messages;
    for(int tick=0;tick<4000 && !talk.finished();++tick) {
        if(talk.message_pc()!=previous) {
            previous=talk.message_pc(); messages.push_back(previous);
            if(previous==0x7bcb0) Check(talk.text().find("Toro")!=std::string::npos,"Lost adopted nickname");
            if(previous==0x7bd95) Check(talk.text().find("Nana")!=std::string::npos,"Lost adopted player");
        }
        if(previous==0x7bd69) {
            for(const auto& line:talk.lines())
                if(line=="\x83\x6a\x83\x83\x81\x49") nonbreaking_suffix=true;
        }
        if(talk.gift_requested()) { ++awards; talk.SupplyGift(slot); }
        if(auto n=talk.random_requested()) {
            Check(*n==1 || *n==2,"Unexpected KSC random range"); talk.SupplyRandom(*n==1?0:roll);
        }
        if(auto request=talk.inventory_requested()) {
            Check(*request==slot,"Wrong live gift slot");
            for(int i=0;i<60;++i) talk.Step(0,true,false,&font);
            Check(talk.inventory_requested()==request,"Modal prematurely resumed the script");
            ++modals; talk.FinishInventory(discard);
        }
        talk.Step(0,true,tick%11==0,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        Check(!talk.consumed(),"Duplicate interaction");
    }
    std::vector<unsigned> expected{0x7bc93,0x7bcb0,0x7bcde,0x7bcf3,0x7bd1d,0x7bd42,0x7bd69,
                                   0x7bd95,0x7bdbc,0x7bef0,0x7bf0f};
    if(slot>=0) expected.insert(expected.end(),{0x89297,0x892a7,0x892c1,0x892e3});
    if(discard) expected.insert(expected.end(),{0x89304,0x8930d});
    if(slot>=0) expected.push_back(discard?0x7d577:roll==0?0x7d5fa:0x7d617);
    Check(nonbreaking_suffix,"BOOT break classes split small kana/punctuation at line start");
    Check(talk.finished() && talk.wide_camera_requested() && messages==expected && awards==1 && modals==unsigned(slot>=0),
          "Cold introduction path or gift return differed");
    talk.Step(0,true,true,&font);
    Check(!talk.wide_camera_requested(),"Repeated common cleanup camera request");
}
void Ordinary(const std::filesystem::path& dir,const recovered::PackageView& font,unsigned pc,
              unsigned first_pick,bool empty_editor=false,unsigned fallback_rank=40,bool teach_word=false,bool kanji=false) {
    gameplay::AcceptedProfile p{};
    p.nickname.Assign("Toro"); p.player.Assign("Nana");
    if(kanji && first_pick==600) p.player.Assign("\x96\xbc");
    gameplay::RoomTalkState talk;
    std::string error;
    Check(talk.Load(dir,p,error),error.c_str());
    talk.Step(0,true,true,&font,true);
    for(int tick=0;tick<8000 && !talk.finished();++tick) {
        if(talk.gift_requested()) talk.SupplyGift(-1);
        talk.Step(0,true,true,&font);
    }
    Check(talk.finished(),"Missing intro prerequisite");
    Check(pc==0x8dba?talk.BeginTeaching():talk.BeginOrdinaryTopic(pc),"Start selected topic");
    const std::string prior_saying=talk.vocabulary().player_saying();
    bool farewell_mark_cleared=false;
    std::vector<unsigned> texts;
    unsigned choices=0,editors=0,last_pc=talk.message_pc(),buttons=0;
    bool sent=false,first_ask=true;
    for(int tick=0;tick<12000 && !talk.finished();++tick) {
        if(auto bound=talk.random_requested()) {
            const unsigned choice=*bound==1057?fallback_rank:*bound==5?2u:*bound==2?1u:0u;
            if(pc==0x848f9 && teach_word && last_pc==0x84adf && *bound==1) {
                Check(!(talk.vocabulary().FieldInt(*talk.learned_word_row(),11)&2),
                      "84B0C did not release this row for the subsequent farewell draw");
                farewell_mark_cleared=true;
            }
            talk.SupplyRandom(choice);
        }
        if(talk.word_entry_requested()) {
            if((pc==0x8dba && !empty_editor) || teach_word) {
                Check(editors==0 || (pc==0x8dba && first_pick==18 && editors==1),"Unexpected repeat of accepted word");
                if(pc==0x8dba && first_pick==600) talk.FinishWordEntry(p.player.text(),kanji?"\x82\xc8":"Nana");
                else if(pc==0x848f9) talk.FinishWordEntry("\x83\x69\x83\x69","\x82\xc8\x82\xc8");
                else talk.FinishWordEntry(kanji?"\x96\xbc":"\x83\x69","\x82\xc8");
            } else {
                Check(empty_editor,"Unexpected teaching keyboard");
                talk.FinishWordEntry("","");
            }
            ++editors;
        }
        if(auto frontier=talk.ordinary_frontier()) {
            if(pc==0x8dba || teach_word) throw std::runtime_error("Teaching frontier "+std::to_string(*frontier));
            Check(pc==0x7d43a && fallback_rank==3 && *frontier==0x77920,
                  "Unexpected unresolved source in selected topic");
            Check(!talk.ordinary_source_return(),"Source frontier reported as complete");
            return;
        }
        unsigned next_buttons=0;
        if(talk.answer_visible()) {
            if(first_ask && talk.answer_interactive()) {
                ++choices; first_ask=false; sent=false;
                if(pc==0x8528b) {
                    const auto& labels=talk.answer_options();
                    Check(labels.size()==2 && labels[0]=="\x82\xa2\x82\xa2\x82\xe6","Wrong ASK choice text");
                }
            }
            if(talk.answer_interactive() && !sent && !buttons) {
                const unsigned pick=pc==0x8dba?(talk.answer_options().size()==19?(first_pick==18 && editors==1?18u:4u):0u):choices==1?first_pick:
                    (pc==0x846ab || pc==0x848f9)?((empty_editor || teach_word)?0u:1u):0u;
                if(talk.answer_cursor()<pick) next_buttons=0x40;
                else { next_buttons=0x2000; sent=true; }
            }
        } else first_ask=true;
        buttons=next_buttons;
        talk.Step(buttons,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(last_pc!=talk.message_pc()) {
            last_pc=talk.message_pc(); texts.push_back(last_pc);
            if(pc==0x848f9 && (last_pc==0x84a7c || last_pc==0x84ab7)) {
                const auto& operands=talk.message_word_operands();
                Check(talk.learned_word_row().has_value(),"Missing learned farewell row");
                const unsigned row=*talk.learned_word_row();
                Check(talk.vocabulary().FieldInt(row,7)==0x8000 && (talk.vocabulary().FieldInt(row,8)&32) &&
                      (talk.vocabulary().FieldInt(row,12)&4), "848F9 registered row fails its original DDAA attributes");
                Check(teach_word && talk.learned_word_row() && operands.size()==1 &&
                      operands[0].row==int(*talk.learned_word_row()) && operands[0].field==1 &&
                      operands[0].local==1 && talk.text().substr(operands[0].offset,operands[0].length)=="\x83\x69\x83\x69",
                      "Farewell question did not say the newly accepted row's spelling");
            }
            if(pc==0x848f9 && last_pc>=0x7d577 && last_pc<=0x7d617) {
                const auto& operands=talk.message_word_operands();
                std::cout<<"848F9 closing "<<std::hex<<last_pc<<std::dec<<" row "
                         <<(operands.empty()?-1:operands.front().row)<<"\n";
                if(teach_word) Check(operands.size()==1 && operands.front().row==int(*talk.learned_word_row()) &&
                                     operands.front().field==1 && operands.front().local==0,
                                     "Selected farewell draw did not say the accepted spelling");
            }
            if(pc==0x848f9 && last_pc==0x84a98) {
                const auto& operands=talk.message_word_operands();
                if(operands.empty()) {
                    Check(talk.text().find("\x82\xa8\x82\xd0\x82\xe9\x82\xcb")!=talk.text().npos,
                          "DDB9 lookup lost its fallback");
                } else {
                    const auto& op=operands.front();
                    Check(operands.size()==1 && op.local==2 && op.row!=int(*talk.learned_word_row()) &&
                          talk.text().substr(op.offset,op.length)==talk.vocabulary().FieldText(op.row,op.field),
                          "DDB9 action came from the farewell input or lost its source spelling");
                    std::cout<<"848F9 DDB9 action row "<<op.row<<" field "<<op.field<<"\n";
                }
            }
            if(pc==0x8528b && last_pc==0x852e5)
                Check(talk.text().find("Nana")!=talk.text().npos,"Missing player");
            if(pc==0x8528b && (last_pc==0x85357 || last_pc==0x853cd))
                Check(talk.text().find("\x82\xb3\x82\xf1\x82\xdb")!=talk.text().npos,"Lost selected DIC word");
        }
    }
    auto saw=[&](unsigned at){return std::find(texts.begin(),texts.end(),at)!=texts.end();};
    if(pc==0x7d43a) {
        if (fallback_rank==0) {
            const auto history=talk.fallback_list().SaveTable();
            Check(editors==1 && history.children.front().children.front().word==0x70077c33u,
                  "Lost 77C33 prompt after declined lesson");
        }
        else Check(saw(0x742b6),"Wrong indirect procedure obtained from F6");
    }
    if(pc==0x4e27 || pc==0x89acc) {
        Check(talk.finished() && talk.ordinary_rejected() && choices==0,"Cold fallback predicate");
        return;
    }
    if(pc==0x8dba && first_pick==600) {
        Check(talk.finished() && talk.ordinary_source_return() && !talk.counted_contact() &&
              saw(0x3fcf) && !talk.learned_word_row() && !talk.vocabulary().HasField(163,0),"Existing player word acquired a new row");
        return;
    }
    if(pc==0x8dba && empty_editor) {
        Check(talk.finished() && talk.ordinary_source_return() && !talk.counted_contact() &&
              editors==1 && !talk.learned_word_row() && !talk.vocabulary().HasField(163,0),"Cancelled teaching reservation");
        return;
    }
    Check(talk.finished() && talk.ordinary_source_return() && !talk.wide_camera_requested() &&
          !talk.ordinary_rejected() && choices>0,"Ordinary source did not return to its room owner");
    if(pc==0x8528b) {
        Check(saw(0x2a0b) && saw(0x852ac) && choices==1 && talk.last_answer()==int(first_pick+1),
              "Missing prelude or selected answer");
        if(!first_pick) Check(saw(0x85343) && saw(0x85357) && saw(0x85382) && saw(0x7d617),"Positive friendship branch");
        else Check(saw(0x94d9) && saw(0x853af) && saw(0x853cd) && saw(0x7d577),"Negative friendship branch");
    }
    if(pc==0x848f9 && (teach_word || empty_editor)) {
        Check(talk.vocabulary().player_saying()==prior_saying,"Farewell wrote the player saying");
        Check(editors==1 && saw(0x84a7c)==teach_word && saw(0x84ab7)==teach_word &&
              saw(0x84a98)==teach_word && saw(0x84b17)==empty_editor && saw(0x84b39)==empty_editor,
              "Empty and accepted farewells took the wrong continuation");
        if(teach_word) {
            Check(talk.learned_word_serial()==1 && talk.learned_word_row() &&
                  talk.learned_word()=="\x83\x69\x83\x69" && talk.learned_reading()=="\x82\xc8\x82\xc8",
                  "Farewell editor failed to register both fields");
            const auto row=*talk.learned_word_row();
            auto& words=talk.vocabulary();
            Check(farewell_mark_cleared && saw(0x7d617),"Missing eligible row or selected closing line");
            std::cout<<"848F9 accepted row "<<row<<" fields 7/8/11/12 "<<words.FieldInt(row,7)<<"/"
                     <<words.FieldInt(row,8)<<"/"<<words.FieldInt(row,11)<<"/"<<words.FieldInt(row,12)<<"\n";
            Check(words.FindWritten(talk.learned_word())==int(row) && words.FieldInt(row,7)==0x8000 &&
                  (words.FieldInt(row,8)&32) && (words.FieldInt(row,11)&2),
                  "Accepted farewell has incorrect category, greeting bits or reuse mark");

        } else Check(!talk.learned_word_row() && !talk.learned_word_serial(),"Empty farewell kept a reserved word");
        return;
    }
    if((pc==0x8dba && !empty_editor) || teach_word) {
        Check(talk.learned_word_serial()==1 && talk.learned_word()==(kanji?"\x96\xbc":"\x83\x69") && talk.learned_reading()=="\x82\xc8" &&
              talk.learned_word_row()==163 && (pc!=0x8dba || talk.counted_contact()),"Learned word missing after teaching");
    } else Check(editors==unsigned(empty_editor),"Word-entry cancel outcome");
    if(pc==0x8528b && !first_pick) {
        Check(talk.BeginRoomAttentionMessage(),"Attention message cannot start after source return");
        talk.Step(0,false,true,&font);
        Check(talk.message_pc()==0x8d3e4 && talk.cols()==5 && talk.rows()==2,"Attention balloon geometry");
        for(int tick=0;tick<200 && !talk.room_attention_return();++tick) talk.Step(0,false,true,&font);
        Check(talk.room_attention_return() && talk.message_pc()==0x8d8fe && !talk.visible() &&
              talk.cols()==9 && talk.rows()==4,"Attention message did not clear");
    }
}
unsigned ResumeActivity(gameplay::RoomActivityPhase& activity,gameplay::ColdRoomActor& actor,
                        gameplay::RoomTalkState& talk,gameplay::RoomActivitySelector& selector,
                        recovered::RngNativeOwner& rng,unsigned period,unsigned minute) {
    using namespace gameplay;
    activity.ReturnToRoom(minute,talk.mood(),rng);
    for(unsigned i=0;i<1500 && !activity.snapshot().started_command;++i) {
        Check(actor.Advance(minute),"Advance actor after source return");
        activity.Advance(minute,{period,actor.light_on(),talk.mood(),talk.mood_age(),actor.source_field50(),
                         actor.source_field5c(),actor.source_flag25a(),actor.started_activity(),actor.command_ready()},rng);
        if(activity.state()==RoomActivityPhase::State::select) {
            const RoomActivityInputs input{activity.first_after_cleanup(),0,int(actor.source_field58()),
                                            int(actor.source_field50()),static_cast<int>(period),talk.mood(),false};
            const auto selection=selector.Select(input,rng);
            activity.FinishSelection(input,selection,!selection.command || actor.RequestActivity(*selection.command),rng);
        }
    }
    const auto& resumed=activity.snapshot();
    Check(resumed.started_command && resumed.selected_command==resumed.started_command && resumed.inputs.first_after_cleanup,
          "Source return did not lead to a started room activity");
    return *resumed.started_command;
}

std::vector<std::string> VisitPlaces(const std::filesystem::path& dir,const recovered::PackageView& font,unsigned option) {
    using namespace gameplay;
    RoomTalkState talk;
    ColdRoomActor actor;
    RoomActivityPhase activity;
    RoomActivitySelector selector;
    AcceptedProfile profile{};
    profile.nickname.Assign("Toro"); profile.player.Assign("Nana");
    std::string error;
    constexpr unsigned period=2, minute=720; // Same daytime component inputs for the two replies.
    if(!talk.Load(dir,profile,error) || !actor.Load(dir.parent_path().parent_path().parent_path(),0,period,error) ||
       !selector.Load(dir,error)) throw std::runtime_error(error);
    recovered::RngNativeOwner rng;
    rng.ApplyInitializerTimeT32(168,1,"Visit places component");
    talk.vocabulary().StartActorScript(rng.NextC3234());
    activity.Enter(minute,rng);
    const auto tick=[&](unsigned buttons) {
        Check(actor.Advance(minute),"Advance visit actor");
        talk.SetCalendarMinute(minute);
        talk.SetActorField50(int(actor.source_field50()));
        talk.Step(buttons,actor.talk_ready(),actor.command_ready(),&font,actor.invitation_expired());
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        actor.SetMood(talk.mood());
        if(auto command=talk.actor_command()) Check(actor.Request(*command),"Play visit conversation ASC");
        if(auto n=talk.random_requested()) talk.SupplyRandom(rng.NextC3234()%*n);
        if(talk.gift_requested()) talk.SupplyGift(-1); // No inventory slot in this component.
    };
    for(unsigned i=0;i<10000 && !talk.finished();++i) tick(actor.talk_ready()?0x2000:0);
    Check(talk.finished() && talk.wide_camera_requested(),"Visit prerequisite did not return");
    activity.ReturnToRoom(minute,talk.mood(),rng);
    Check(activity.BeginOrdinary() && talk.BeginOrdinaryTopic(0x84f41),"Start selected visit question");
    const std::vector<std::string> labels{"\x82\xa2\x82\xeb\x82\xf1\x82\xc8\x8f\xea\x8f\x8a\x82\xa9\x82\x9f",
                                          "\x8c\xa9\x8a\x77\x82\xb5\x82\xc9\x8d\x73\x82\xb1\x82\xa4\x82\xe6"};
    unsigned held=0,last=0;
    bool sent=false;
    std::vector<unsigned> lines,commands;
    std::vector<std::string> reaction;
    for(unsigned i=0;i<16000 && !talk.finished();++i) {
        unsigned next=0;
        if(talk.answer_interactive() && !held && !sent) {
            Check(talk.message_pc()==0x84fb2 && talk.answer_options()==labels,"Wrong visit offer");
            if(talk.answer_cursor()!=option) next=0x40;
            else { next=0x2000; sent=true; }
        }
        tick(next); held=next;
        Check(!talk.ordinary_frontier(),"Visit conversation hit a source boundary");
        if(auto command=talk.actor_command(); sent && command && reaction.size()<4) commands.push_back(*command);
        const unsigned pc=talk.message_pc();
        if(pc!=last && pc>=0x84f41 && pc<0x8506d) {
            lines.push_back(pc);
            if(pc>=0x84fe3) {
                Check(sent && talk.last_answer()==int(option+1),"Visit reaction preceding accepted choice");
                reaction.emplace_back(talk.text());
            }
        }
        last=pc;
    }
    Check(lines==std::vector<unsigned>{0x84f7b,0x84f9a,0x84fb2,0x84fe3,0x84ff7,0x85024,0x85043} &&
          commands==std::vector<unsigned>{51,24,38},"Visit reaction sequence or actor commands");
    Check(talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected(),"Visit farewell did not return");
    const auto resumed=ResumeActivity(activity,actor,talk,selector,rng,period,minute);
    std::cout<<"84F41 answer "<<option+1<<" -> 84FE3, 84FF7, 85024, 85043 -> return -> ASC "<<resumed<<"\n";
    return reaction;
}

void FinishTeaching(gameplay::RoomTalkState& talk,const recovered::PackageView& font,const std::string& written,
                    const std::string& reading,const std::vector<unsigned>& policy,
                    std::vector<unsigned>& messages,std::vector<unsigned>& answers,bool gorgeous_trait=false) {
    unsigned last=talk.message_pc(),buttons=0;
    bool answered=false;
    for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
        if(auto bound=talk.random_requested()) talk.SupplyRandom(gorgeous_trait && *bound==19?18u:0u);
        if(talk.word_entry_requested()) talk.FinishWordEntry(written,reading);
        unsigned next=0;
        if(!talk.answer_visible()) answered=false;
        if(talk.answer_interactive() && !answered && !buttons) {
            const unsigned row=answers.size()<policy.size()?policy[answers.size()]:1;
            const auto labels=talk.answer_options();
            Check(row<=labels.size(),"Unexpected source options in teaching");
            if(talk.answer_cursor()+1<row) next=0x40;
            else {
                if(last==0x457f) Check(labels.size()==2 && labels[0]=="\x82\xb1\x82\xc6\x82\xce\x83\x81\x83\x82\x81\x48" && labels[1]=="\x89\xbd\x82\xbb\x82\xea\x81\x48","Missing first memo ASK");
                if(last==0x45e4) Check(labels.size()==2 && labels[0]=="\x93\xc7\x82\xdd\x95\xfb\x82\xc6\x82\xa9\x81\x48" && labels[1]=="\x92\xbc\x82\xb9\x82\xce\x82\xa2\x82\xa2\x82\xcc\x81\x48","Missing second memo ASK");
                if(gorgeous_trait && last==0xabe9) {
                    Check(labels==std::vector<std::string>({"\x83\x53\x81\x5b\x83\x57\x83\x83\x83\x58","\x83\x53\x81\x5b\x83\x57\x83\x83\x83\x58\x82\xb6\x82\xe1\x82\xc8\x82\xa2","\x82\xa6\x81\x48\x20\x89\xbd\x81\x48"}),"Wrong duplicate lesson trait choices");
                }
                next=0x2000; answered=true; answers.push_back(last);
            }
        }
        buttons=next; talk.Step(buttons,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Lesson source frontier "+std::to_string(*pc));
        if(talk.message_pc()!=last) { last=talk.message_pc(); messages.push_back(last); }
    }
}
// The first cold word with unlike normalized spelling/reading gets the memo
// lesson. 8BB0 remembers its completion in G59A2, across separate lessons.
void MemoLesson(const std::filesystem::path& dir,const recovered::PackageView& font,unsigned memo_answer) {
    gameplay::AcceptedProfile profile{};
    profile.nickname.Assign("Toro"); profile.player.Assign("Nana");
    gameplay::RoomTalkState talk;
    std::string error;
    if(!talk.Load(dir,profile,error)) throw std::runtime_error(error);
    talk.Step(0,true,true,&font,true);
    for(unsigned tick=0;tick<8000 && !talk.finished();++tick) {
        if(talk.gift_requested()) talk.SupplyGift(-1);
        talk.Step(0,true,true,&font);
    }
    const std::string na="\x82\xc8",kanji="\x96\xbc";
    for(unsigned lesson=0;lesson<3;++lesson) {
        Check(talk.BeginTeaching(),"Start repeated direct teaching");
        // A normalization match first, then the unrelated kanji spelling, then another new word.
        const std::string written=lesson==0?"\x83\x69":lesson==1?kanji:kanji+na;
        const std::string reading=lesson==2?na+na:na;
        std::vector<unsigned> messages, answers;
        const std::vector<unsigned> policy={5,1,3,2,memo_answer,memo_answer};
        FinishTeaching(talk,font,written,reading,policy,messages,answers);
        Check(talk.finished() && talk.ordinary_source_return() && talk.counted_contact() &&
              talk.learned_word_serial()==lesson+1 && talk.learned_word()==written && talk.learned_reading()==reading,
              "Memo lesson did not return with the accepted word");
        std::vector<unsigned> observed;
        for(unsigned value:messages) if(value>=0x454f && value<0x468c) observed.push_back(value);
        if(lesson==1) {
            const std::vector<unsigned> expected={0x4557,0x4567,0x457f,0x45b0,0x45e4,0x4610,0x4628,0x4658,0x466d};
            Check(observed==expected && answers.size()==6 && answers[4]==0x457f && answers[5]==0x45e4 &&
                  talk.last_answer()==int(memo_answer),"Wrong original memo messages or responses");
        } else Check(observed.empty() && answers.size()==4,"Memo appeared on a skipped or repeated lesson");
    }
}

// Component entry at the ordinary script's real 4E27 callback. The window
// check enters through the room menu/Circle and leaves topic selection to it.
enum class ReadingAnswer { confirm, defer, cancel_edit, retry_edit, append_kana };
void ReadingQuestion(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                     const std::string& spelling,const std::string& reading,ReadingAnswer answer,
                     gameplay::RoomWordEntry* keyboard=nullptr) {
    auto& words=talk.vocabulary();
    const int row=int(*talk.learned_word_row());
    words.BeginConversation();
    Check(talk.BeginOrdinaryTopic(0x4e27),"Start ordinary reading question");
    std::vector<unsigned> asks;
    unsigned last=0,previous_buttons=0,editors=0;
    const std::string edited=answer==ReadingAnswer::append_kana?reading+"\x82\xc8":"1A";
    bool answered=false,saw_written=false,saw_reading=false,saw_echo=false;
    std::vector<unsigned> correction_preview;
    for(unsigned tick=0;tick<9000 && !talk.finished();++tick) {
        if(auto bound=talk.random_requested()) {
            Check(*bound!=0,"Unexpected empty draw while checking a learned reading");
            talk.SupplyRandom(0);
        }
        if(talk.word_entry_requested()) {
            Check(talk.keyboard_initial_reading()==reading,"Correction did not reopen on the stored reading");
            ++editors;
            if(answer==ReadingAnswer::append_kana) {
                Check(keyboard && editors==1,"Missing keyboard or unexpected correction retry");
                keyboard->OpenReading(*talk.keyboard_initial_reading(),0,true);
                for(int i=0;i<15;++i) keyboard->Step(0,&font);
                Check(keyboard->written()==reading && keyboard->row()==1 && keyboard->column()==1,
                      "Script reading editor did not open on the current reading");
                auto tap=[&](unsigned button){ keyboard->Step(button,&font); keyboard->Step(0,&font); };
                tap(0x2000);
                Check(keyboard->picking(),"Failed to open the kana group");
                for(int i=0;i<5;++i) keyboard->Step(0,&font);
                tap(0x2000);
                Check(keyboard->written()==edited,"Edited reading was not appended to the draft");
                tap(0x8000);
                tap(0x2000);
                for(int i=0;i<30 && keyboard->phase()!=gameplay::RoomWordEntry::Phase::closed_word;++i)
                    keyboard->Step(0,&font);
                Check(keyboard->phase()==gameplay::RoomWordEntry::Phase::closed_word,"Type-2 keyboard did not return");
                talk.FinishWordEntry(keyboard->written(),keyboard->reading());
            } else if(answer==ReadingAnswer::cancel_edit) talk.FinishWordEntry({},{});
            else {
                Check(answer==ReadingAnswer::retry_edit && editors<=2,"Unexpected reading editor");
                talk.FinishWordEntry(editors==1?"ABC":edited,{});
            }
        }
        unsigned buttons=0;
        if(!talk.answer_visible()) answered=false;
        if(talk.answer_interactive() && !answered && !previous_buttons) {
            Check(talk.answer_options().size()==2,"Expected two reading-question options");
            const bool second=(asks.empty() && answer!=ReadingAnswer::confirm) ||
                              (asks.size()==1 && answer==ReadingAnswer::defer) ||
                              (asks.size()==2 && answer==ReadingAnswer::retry_edit);
            if(second && talk.answer_cursor()==0) buttons=0x40;
            else { buttons=0x2000; answered=true; asks.push_back(talk.message_pc()); }
        }
        talk.Step(buttons,true,true,&font);
        previous_buttons=buttons;
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Unresolved reading question at " + std::to_string(*pc));
        if(talk.message_pc()==last) continue;
        last=talk.message_pc();
        if(answer==ReadingAnswer::append_kana && (last==0x4287 || last==0x429e)) {
            const auto& expected=last==0x4287?spelling:edited;
            Check(talk.text().find(expected)!=std::string_view::npos && words.FieldText(row,2)==reading,
                  "Correction preview did not show the proposed spelling/reading before acceptance");
            correction_preview.push_back(last);
        }
        if(last==0xc266) {
            const auto& operands=talk.message_word_operands();
            Check(operands.size()==1 && operands.front().row==row && operands.front().field==2 &&
                  operands.front().control_pc==0xc268 && operands.front().local==0 &&
                  talk.text().substr(operands.front().offset,operands.front().length)==words.FieldText(row,2),
                  "Reading acknowledgement did not repeat this row's current reading");
            saw_echo=true;
        }
        if(last==0x41fd || last==0x4212) {
            Check(talk.message_word_rows()==std::vector<int>{row},"Substitution lost its selected dictionary row");
            if(last==0x41fd) saw_written=talk.text().find(spelling)!=std::string::npos;
            else saw_reading=talk.text().find(reading)!=std::string::npos;
        }
    }
    Check(saw_written && saw_reading && talk.finished() && talk.ordinary_source_return(),
          "Expected the selected row's spelling, reading and ordinary return");
    const bool changed=answer==ReadingAnswer::retry_edit || answer==ReadingAnswer::append_kana;
    const bool confirmed=changed || answer==ReadingAnswer::confirm;
    Check(saw_echo==confirmed,"Reading echo appeared on the wrong answer path");
    const unsigned expected_asks=answer==ReadingAnswer::append_kana?3u:
                                 answer==ReadingAnswer::retry_edit?4u:answer==ReadingAnswer::confirm?1u:2u;
    Check(asks.size()==expected_asks && words.FieldText(row,1)==spelling &&
          words.FieldText(row,2)==(changed?edited:reading),"Wrong stored fields after reading question");
    Check(editors==(answer==ReadingAnswer::retry_edit?2u:changed || answer==ReadingAnswer::cancel_edit?1u:0u),"Reading editor count");
    Check(bool(words.FieldInt(row,0)&0x2000)==confirmed,"Reading confirmation flag after answer");
    if(answer==ReadingAnswer::append_kana) {
        Check(correction_preview==std::vector<unsigned>{0x4287,0x429e} &&
              asks==std::vector<unsigned>{0x4212,0x4230,0x429e},
              "Unexpected reading editor confirmation or question order");
        gameplay::RoomNotebook memo;
        memo.Open(words,0);
        memo.Step(0);
        for(unsigned tab=2;tab<=5;++tab) {
            memo.Step(0x20);
            memo.Step(0);
        }
        const auto* item=memo.selected_item();
        Check(memo.list_ready() && memo.tab()==5 && memo.count()==1 && item && int(item->row)==row &&
              item->written==spelling && item->reading==edited,"Notebook did not read back the corrected row");
        std::cout<<"4E27 correction: one keyboard, three answers, current field 2 at C266, memo row "<<row<<" tab 5\n";
    }
    if(!confirmed) words.ChangeMemoReading(row,edited);
    words.BeginConversation();
    Check(talk.BeginOrdinaryTopic(0x4e27),"Recheck confirmed or edited row");
    talk.Step(0,true,true,&font);
    Check(talk.finished() && talk.ordinary_rejected() && !talk.random_requested(),
          "Previously confirmed or edited reading still offered by 4E27");
}

void ClothingStory(gameplay::RoomTalkState talk,const recovered::PackageView& font,std::optional<unsigned> queued,
                   unsigned color_rank,unsigned first,unsigned second) {
    const std::string colors[]={"\x90\xd4\x82\xa2","\x89\xa9\x90\x46\x82\xa2","\x90\xc2\x82\xa2","\x94\x92\x82\xa2"};
    const auto& color=colors[color_rank];
    auto& words=talk.vocabulary();
    const unsigned garment=*talk.learned_word_row(),place=first?141:125;
    const std::string clothing(talk.learned_word()),location(words.FieldText(place,1));
    words.BeginConversation();
    talk.SetMood(2);
    Check(talk.BeginOrdinaryTopic(queued.value_or(0x7d43a),queued.has_value()),"Open fashion conversation");
    std::vector<unsigned> lines,answers;
    bool sent=false;
    unsigned held=0,last=talk.message_pc(),dispatches=0,color_draws=0,place_draws=0,mood_changes=0;
    for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
        if(auto bound=talk.random_requested()) {
            unsigned pick=0;
            if(*bound==1057) { ++dispatches; pick=690; }
            if(*bound==100 && lines.empty()) pick=first?25:24; // both sides of 92F9's 25% secondary bypass
            if(*bound==17 && lines.empty()) { pick=place-125; ++place_draws; }
            if(*bound==4 && talk.message_pc()==0x3af58) { pick=color_rank; ++color_draws; }
            talk.SupplyRandom(pick);
        }
        if(!talk.answer_visible()) sent=false;
        unsigned buttons=0;
        if(talk.answer_interactive() && !sent && !held) {
            const auto pc=talk.message_pc();
            Check(pc==0x3af58 || pc==0x3afe9,"Unexpected queued fashion question");
            const auto& choices=talk.answer_options();
            if(pc==0x3af58)
                Check(choices==std::vector<std::string>{"\x16"+location+"\x16\x82\xc5\x81\x48",
                    "\x91\xd2\x82\xbf\x8d\x87\x82\xed\x82\xb9\x81\x48"},"Wrong rendezvous options");
            else Check(choices==std::vector<std::string>{"\x83\x81\x83\x8d\x83\x81\x83\x8d\x85\x40",
                      "\x82\xed\x82\xc1\x82\xcd\x82\xc1\x82\xcd"},"Wrong second fashion options or game glyph 8540");
            if(talk.answer_cursor()!=(pc==0x3af58?first:second)) buttons=0x40;
            else {buttons=0x2000; sent=true; answers.push_back(pc);}
        }
        held=buttons; talk.Step(buttons,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Fashion story source boundary "+std::to_string(*pc));
        Check(!talk.word_entry_requested(),"Queued fashion conversation reopened lesson");
        if(!mood_changes && talk.mood()!=2) {
            Check(second==1 && last==0x3b073 && talk.mood()==1 && talk.mood_age()==0,"Wrong 2151(1) effect or source order");
            ++mood_changes;
        }
        auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(talk.message_indirect_topic()!=0x3ae67) continue;
        lines.push_back(pc);
        if(pc==0x3aeff) Check(talk.text().find("\x16Toro\x16")==0,"Lost GAME15 speaker name");
        if(pc==0x3afe9) Check(talk.text().find("\x16Nana\x16")!=std::string_view::npos,"Lost GAME16 player name");
        if(pc==0x3af58 || pc==0x3afa4 || pc==0x3b054) {
            const auto& ops=talk.message_word_operands();
            const bool at_place=pc==0x3af58;
            const auto& value=at_place?location:clothing;
            Check(ops.size()==1 && ops[0].row==int(at_place?place:garment) && ops[0].local==(at_place?2u:1u) &&
                  ops[0].field==1 && talk.text().substr(ops[0].offset,ops[0].length)==value,"Wrong name or location operand");
            if(at_place) Check(talk.text().find("\x85\x40")!=std::string_view::npos,"Lost rendezvous glyph 8540");
            else Check(talk.text().find("\x16"+color+"\x16 \x16"+clothing+"\x16")!=std::string_view::npos,
                       "Stored CT6E00 color no longer matches the clothing reply");
        }
        if(pc==0x3b020 || pc==0x3b02b) Check(talk.text().find("\x85\x40\n")!=std::string_view::npos,"Lost shy response glyph");
    }
    std::vector<unsigned> expected{0x29cd,0x3aeff,0x3af24,0x3af37,0x3af58,0x3afa4,0x3afb6,0x3afe9};
    if(second==0) expected.insert(expected.end(),{0x3b020,0x3b02b});
    else expected.insert(expected.end(),{0x94d9,0x3b054,0x3b073});
    Check(dispatches==unsigned(!queued) && color_draws==1 && place_draws==1 &&
          answers==std::vector<unsigned>{0x3af58,0x3afe9} && lines==expected &&
          mood_changes==second && talk.mood()==(second?1:2) && talk.finished() && talk.ordinary_source_return() &&
          !talk.ordinary_rejected() && talk.TakeTopicEnqueues().empty() && talk.learned_word_serial()==2,
          "Incomplete queued fashion topic or wrong answer effects");
}

void FreeClothingLesson(gameplay::RoomTalkState talk,const recovered::PackageView& font) {
    const std::string spelling="Ribbon",reading="\x82\xe8\x82\xda\x82\xf1";
    Check(talk.vocabulary().FindWritten(spelling)==-1 && talk.BeginTeaching(),"Start free clothing lesson");
    std::vector<unsigned> lines,asks;
    // The fourteenth answer in 3CB7 writes category 8192 and subtype bit 8
    // directly. 67E8 tests that bit before its optional 6831 classifier.
    FinishTeaching(talk,font,spelling,reading,{14},lines,asks);
    const auto row=talk.learned_word_row();
    Check(row && talk.learned_word_serial()==2 && talk.learned_word()==spelling && talk.learned_reading()==reading &&
          talk.vocabulary().FieldInt(*row,7)==8192 && (talk.vocabulary().FieldInt(*row,8)&8) &&
          (talk.vocabulary().FieldInt(*row,0)&36)==36 && !asks.empty() && asks.front()==0x780c &&
          std::find(asks.begin(),asks.end(),0x6831)==asks.end() && talk.finished() &&
          talk.ordinary_source_return() && talk.counted_contact() && talk.TakeTopicEnqueues().empty(),
          "Free clothing category did not take the source early return and register the same word");
    ClothingStory(talk,font,{},2,1,0);
}

enum class PromptedCase { robot_refuses_lesson, new_lesson, new_drink, new_clothing, refuse_clothing, eligible_word, starter_body_part,
                          known_television, missing_television, known_friend, starter_occupation };

void PromptedLookup(gameplay::RoomTalkState talk,const recovered::PackageView& font,PromptedCase scenario) {
    auto& words=talk.vocabulary();
    const int row=int(*talk.learned_word_row());
    const bool body_part=scenario==PromptedCase::starter_body_part;
    const bool occupation=scenario==PromptedCase::starter_occupation;
    const bool television=scenario==PromptedCase::known_television || scenario==PromptedCase::missing_television;
    const bool friend_question=scenario==PromptedCase::known_friend;
    const bool existing=scenario==PromptedCase::eligible_word || scenario==PromptedCase::known_television || friend_question;
    const bool clothing=scenario==PromptedCase::new_clothing || scenario==PromptedCase::refuse_clothing;
    const bool refuse=scenario==PromptedCase::robot_refuses_lesson || scenario==PromptedCase::missing_television ||
                      scenario==PromptedCase::refuse_clothing;
    // The window taught a robot. These component cases change fields on a
    // copy of that row to exercise the other accepted source predicates.
    if(scenario==PromptedCase::eligible_word) words.WriteNumber(row,7,1);
    if(scenario==PromptedCase::known_television) {
        words.WriteNumber(row,7,2048);
        words.WriteNumber(row,8,4);
    }
    if(friend_question) {
        words.WriteNumber(row,7,4);
        words.WriteNumber(row,8,16);
        words.WriteNumber(row,9,4);
    }
    words.BeginConversation();
    Check(talk.BeginOrdinaryTopic(0x7d43a),"Start common topic dispatcher");
    const bool drink=scenario==PromptedCase::new_drink;
    const bool teach=scenario==PromptedCase::new_lesson || drink || scenario==PromptedCase::new_clothing;
    unsigned held=0,asks=0,editors=0;
    bool answered=false,matched=false;
    for(unsigned tick=0; tick<16000 && !talk.finished(); ++tick) {
        if(auto bound=talk.random_requested()) {
            unsigned pick=0;
            if(*bound==1057) {
                if(clothing) pick=690;
                else if(drink) pick=197;
                else if(television) pick=855;
                else if(body_part) pick=317;
                else if(occupation) pick=240;
                else if(friend_question) pick=375;
                else pick=311;
            }
            else if((body_part || occupation) && *bound>1) pick=1;
            talk.SupplyRandom(pick);
        }
        unsigned buttons=0;
        if(!talk.answer_visible()) answered=false;
        if(talk.answer_interactive() && !answered && !held) {
            if(refuse && talk.answer_cursor()==0) buttons=0x40;
            else { buttons=0x2000; answered=true; ++asks; }
        }
        held=buttons;
        talk.Step(buttons,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Pending prompted lookup at "+std::to_string(*pc));
        if(clothing) Check(talk.message_pc()!=0x6831 && talk.message_pc()!=0x3aeff,
                           "Inline clothing lesson missed its supplied subtype or fell through into the date");
        if(talk.word_entry_requested()) {
            Check(teach && editors==0,"Unexpected editor in prompted lookup");
            ++editors;
            talk.FinishWordEntry("Luna","Luna");
        }
        if(existing && (talk.message_pc()==0x2b510 || talk.message_pc()==0x5c3f5 ||
                        talk.message_pc()==0x5695f)) {
            for(const auto& operand:talk.message_word_operands())
                if(operand.row==row && operand.field==1 &&
                   talk.text().substr(operand.offset,operand.length)==words.FieldText(row,1)) matched=true;
        } else if((body_part && talk.message_pc()==0x5bc86) ||
                  (occupation && talk.message_pc()==0x61ef1)) {
            const int starter=body_part ? 61 : 3;
            for(const auto& operand:talk.message_word_operands())
                if(operand.row==starter && operand.field==1 &&
                   talk.text().substr(operand.offset,operand.length)==words.FieldText(starter,1)) matched=true;
        } else if(!body_part && !occupation && !existing && talk.message_pc()==0x9389) matched=true;
    }
    Check(matched && asks>0 && talk.finished() && talk.ordinary_source_return(),
          "Missing selected word/question, answer or prompted topic return");
    const auto enqueued=talk.TakeTopicEnqueues();
    if(teach) {
        Check(editors==1 && talk.learned_word_serial()==2 && talk.learned_word()=="Luna",
              "New prompted word was not registered");
        Check(enqueued.size()==1 && enqueued.front()==(clothing?0x3ae67u:drink?0x65faau:0x5c31bu),
              "Missing real deferred topic after teaching");
        if(clothing) {
            const auto learned=talk.learned_word_row();
            Check(learned && (words.FieldInt(*learned,0)&36)==36 && words.FieldInt(*learned,7)==8192 &&
                  (words.FieldInt(*learned,8)&8) && (words.FieldInt(*learned,14)&0x200000) &&
                  (words.FieldInt(*learned,15)&0x200000),"Wrong prompted clothing category, subtype or trait");
            for(unsigned rank=0;rank<4;++rank) ClothingStory(talk,font,*enqueued.front(),rank,rank&1,rank>>1);
        }
        if(drink) {
            const auto learned=talk.learned_word_row();
            Check(learned && words.FieldInt(*learned,7)==256 && (words.FieldInt(*learned,0)&36)==36,
                  "Prompted drink lesson registered the wrong category");
        }
    } else {
        Check(talk.learned_word_serial()==1 && !words.HasField(row+1,0) && enqueued.empty(),
              "Unexpected registration or deferred topic");
    }
}

void PainPhrase(gameplay::RoomTalkState talk,const recovered::PackageView& font,unsigned rank,unsigned answer) {
    // The four pool strings in CT5627, independently read from NEKO.KSC.
    const std::string phrases[]={"\x83\x59\x83\x4c\x83\x59\x83\x4c","\x83\x60\x83\x4e\x83\x60\x83\x4e",
                                 "\x83\x4c\x83\x85\x83\x93","\x83\x59\x83\x4c\x83\x93\x83\x59\x83\x4c\x83\x93"};
    const auto& phrase=phrases[rank];
    talk.vocabulary().BeginConversation();
    Check(talk.BeginOrdinaryTopic(0x7d43a),"Start common dispatcher for phrase test");
    bool said=false, selected=false, replied=false;
    unsigned held=0;
    for(unsigned tick=0; tick<10000 && !talk.finished(); ++tick) {
        if(auto count=talk.random_requested()) {
            if(*count==4) {
                talk.SupplyRandom(*count);
                Check(talk.random_requested()==count,"Out-of-range rank advanced the conversation");
            }
            talk.SupplyRandom(*count==1057 ? 891 : *count==4 ? rank : 0);
        }
        unsigned buttons=0;
        if(talk.answer_interactive() && !selected && !held) {
            const auto& choices=talk.answer_options();
            Check(choices.size()==2 && choices[1].find(phrase)!=std::string::npos &&
                  choices[0].find(phrase)==std::string::npos,"ASK has the wrong selected phrase");
            if(talk.answer_cursor()!=answer) buttons=0x40;
            else { buttons=0x2000; selected=true; }
        }
        held=buttons;
        talk.Step(buttons,true,true,&font);
        Check(talk.error().empty() && !talk.ordinary_frontier(),"Pain topic hit a source boundary");
        if(talk.message_pc()==0x27ad2 && talk.text().find(phrase)!=std::string::npos) {
            said=true;
            for(const auto& op:talk.message_word_operands())
                Check(op.local!=1,"Script-pool phrase was attributed to a vocabulary row");
        }
        if(talk.message_pc()==0x27b23 && selected && talk.last_answer()==int(answer+1))
            replied=talk.text().find("Nana")!=std::string::npos;
    }
    Check(said && replied && talk.ordinary_source_return(),"Missing selected phrase, reply or return");
}

void RelatedRoomQuestion(gameplay::RoomTalkState talk,const recovered::PackageView& font,bool recipe) {
    talk.vocabulary().BeginConversation();
    Check(talk.BeginOrdinaryTopic(0x7d43a),"Begin recipe or stress conversation");
    bool first=false,second=false,last=false,answered=false;
    for(unsigned tick=0; tick<10000 && !talk.finished(); ++tick) {
        if(auto count=talk.random_requested())
            talk.SupplyRandom(*count==1057 ? (recipe ? 907u : 332u) : *count-1);
        unsigned buttons=0;
        if(talk.answer_interactive() && !answered) { buttons=0x2000; answered=true; }
        talk.Step(buttons,true,true,&font);
        Check(talk.error().empty() && !talk.ordinary_frontier(),"Related room question did not execute");
        const auto pc=talk.message_pc();
        if(recipe) {
            // Separate draws from CT53A0 and CT53A5 survive the following word lookup.
            if(pc==0x26089) first=talk.text().find("\x93\xfc\x82\xea\x82\xc4")!=std::string::npos;
            if(pc==0x260a3) second=talk.text().find("\x83\x4c\x81\x5b\x83\x93")!=std::string::npos;
            if(pc==0x260fe) last=talk.text().find("\x82\xdc\x82\xc1\x82\xbd\x82\xad")!=std::string::npos;
        } else {
            if(pc==0x5a6e1) first=true;
            if(pc==0x5a702) second=!talk.text().empty();
            if(pc==0x5a737) last=talk.text().find("Nana")!=std::string::npos;
        }
    }
    Check(first && second && last && answered==recipe && talk.ordinary_source_return(),
          "Missing recipe continuation or stress warning");
}

void DrinkContact(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                  std::optional<unsigned> drink,unsigned residue,unsigned answer) {
    auto& words=talk.vocabulary();
    const auto prior_lessons=talk.learned_word_serial();
    words.BeginConversation();
    Check(talk.BeginOrdinaryTopic(0x7d43a),"Start D090 drink conversation");
    std::vector<unsigned> asks,lines,draws;
    bool sent=false,selected=false;
    unsigned buttons=0,previous=talk.message_pc();
    for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
        if(auto bound=talk.random_requested()) {
            unsigned pick=0;
            if(*bound==1057) {
                Check(!selected,"Unexpected D090 drink retry");
                pick=197; selected=true;
            } else if(selected && lines.empty()) {
                draws.push_back(*bound);
                if(*bound==3 && drink) pick=residue;
            }
            talk.SupplyRandom(pick);
        }
        if(!talk.answer_visible()) sent=false;
        unsigned next=0;
        if(talk.answer_interactive() && !sent && !buttons) {
            const auto pc=talk.message_pc();
            Check(pc==(drink ? 0x6607fu : 0x9389u),"Unexpected drink-topic question");
            if(drink) Check(talk.answer_options()==std::vector<std::string>{
                "\x82\xaa\x82\xf1\x82\xce\x82\xea","\x82\xa8\x82\xc8\x82\xa9 \x82\xb1\x82\xed\x82\xb7\x82\xe6\x81\x48"},
                "Wrong labels for the drink quantity question");
            if(talk.answer_cursor()!=answer) next=0x40;
            else { next=0x2000; sent=true; asks.push_back(pc); }
        }
        buttons=next;
        talk.Step(buttons,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Drink conversation missing source "+std::to_string(*pc));
        Check(!talk.word_entry_requested(),"Drink lookup unexpectedly opened another editor");
        const auto pc=talk.message_pc();
        if(pc==previous) continue;
        previous=pc;
        if(talk.message_indirect_topic()!=0x65faa) continue;
        if(pc==0x9389 || pc==0x29cd) { lines.push_back(pc); continue; }
        if(pc!=0x66032 && pc!=0x66057 && pc!=0x6607f && pc!=0x660cf && pc!=0x660ff) continue;
        lines.push_back(pc);
        const bool use_drink=pc==0x66032 || pc==0x6607f;
        const auto& used=talk.message_word_operands();
        Check(drink && used.size()==1 && used[0].local==(use_drink?1u:2u) &&
              used[0].row==int(use_drink?*drink:60u) && used[0].field==1 &&
              talk.text().substr(used[0].offset,used[0].length)==words.FieldText(used[0].row,1),
              "Wrong drink/body substitution or invented source for numeric local");
        if(pc==0x660cf)
            Check(talk.text().find("\x16Nana\x16")==0,"Encouragement answer lost the current player name");
        if(pc==0x6607f) {
            // BOOT 75778 renders a signed integer with ASCII %d in this
            // unformatted local. Bind its single digit to the CP932 units.
            const std::string amount="\x16\x82\xf0 "+std::to_string(residue+1)+"\x83\x8a\x83\x62\x83\x67\x83\x8b";
            Check(talk.text().substr(used[0].offset+used[0].length,amount.size())==amount,
                  "Wrong 2919 remainder or integer spelling in the displayed quantity");
        }
    }
    Check(selected && talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected() &&
          talk.learned_word_serial()==prior_lessons && talk.TakeTopicEnqueues().empty(),"Incomplete drink-topic return");
    if(!drink) {
        Check(asks==std::vector<unsigned>{0x9389} && lines==asks,"Declined drink lesson entered the quantity story");
        return;
    }
    Check(asks==std::vector<unsigned>{0x6607f} &&
          lines==std::vector<unsigned>{0x29cd,0x66032,0x66057,0x6607f,answer==0 ? 0x660cfu : 0x660ffu} &&
          std::count(draws.begin(),draws.end(),3u)==1 && draws.size()>=2 && draws[draws.size()-2]==3 && draws.back()==5,
          "Wrong drink draws, order, reply or final return");
}

void DrinkLessonAndContacts(gameplay::RoomTalkState talk,const recovered::PackageView& font) {
    DrinkContact(talk,font,{},0,1);
    const std::string written="Fizz",reading="\x82\xd3\x82\xa1\x82\xb8";
    const auto initial=talk.learned_word_serial();
    Check(talk.vocabulary().FindWritten(written)==-1 && talk.BeginTeaching(),"Start a fresh drink lesson");
    std::vector<unsigned> messages,answers;
    // Select 食べ物・飲み物 in the category ASK, then 飲み物 in 6102.
    FinishTeaching(talk,font,written,reading,{8,2},messages,answers);
    const auto drink=talk.learned_word_row();
    Check(talk.finished() && talk.ordinary_source_return() && talk.counted_contact() &&
          talk.learned_word_serial()==initial+1 && drink && talk.vocabulary().FieldInt(*drink,7)==256 &&
          (talk.vocabulary().FieldInt(*drink,0)&36)==36 && talk.learned_word()==written &&
          talk.learned_reading()==reading && std::find(answers.begin(),answers.end(),0x612c)!=answers.end(),
          "Lesson did not register a source-selected drink");
    for(unsigned pick=0;pick<3;++pick)
        for(unsigned answer=0;answer<2;++answer) DrinkContact(talk,font,drink,pick,answer);
}

enum class FutureVisit { refuse_name, teach_name, robot_story };

// The queue handle, when present, came from 9408 -> 249F in an earlier
// completed contact in this instance. Otherwise use the ordinary D090 entry.
std::vector<std::optional<unsigned>> FutureContact(gameplay::RoomTalkState& talk,const recovered::PackageView& font,
        FutureVisit stage,std::optional<unsigned> queued={},unsigned first=0,unsigned second=0,unsigned century_draw=0,
        bool no_vehicle=false) {
    const bool story=stage==FutureVisit::robot_story;
    auto& words=talk.vocabulary();
    const auto before=talk.learned_word_serial();
    if(no_vehicle) // Explicit component-only empty CT7338; exercise the original 車 fallback.
        for(unsigned row=51;row<=59;++row) words.WriteNumber(row,0,words.FieldInt(row,0)&~4);
    words.BeginConversation();
    Check(talk.BeginOrdinaryTopic(queued.value_or(0x7d43a),queued.has_value()),"Start future robot conversation");
    bool sent=false,prompt=false;
    unsigned held=0,editors=0,dispatches=0,quantity_draws=0,vehicles=0,last=talk.message_pc();
    std::vector<unsigned> story_lines,story_answers;
    const std::string spelling="Mira",reading="\x82\xdd\x82\xe7";
    for(unsigned tick=0;tick<20000 && !talk.finished();++tick) {
        if(auto bound=talk.random_requested()) {
            unsigned pick=0;
            if(*bound==1057) { ++dispatches; pick=639; }
            if(story && story_lines.empty()) {
                if(*bound==9) { ++vehicles; pick=second?8:0; }
                if(*bound==10) { ++quantity_draws; pick=century_draw; }
            }
            talk.SupplyRandom(pick);
        }
        if(talk.word_entry_requested()) {
            Check(stage==FutureVisit::teach_name && editors++==0,"Unexpected future-topic editor");
            talk.FinishWordEntry(spelling,reading);
        }
        unsigned buttons=0;
        if(!talk.answer_visible()) sent=false;
        if(talk.answer_interactive() && !sent && !held) {
            const unsigned pc=talk.message_pc();
            unsigned pick=0;
            if(pc==0x9389) pick=stage==FutureVisit::refuse_name?1:0;
            if(pc==0x3f164 || pc==0x3f276) {
                Check(story,"Name lesson entered the robot story before queued return");
                if(pc==0x3f164) {
                    Check(talk.answer_options()==std::vector<std::string>{"\x82\xc7\x82\xf1\x82\xc8\x96\xa2\x97\x88\x81\x48",
                        "\x82\xd3\x81\x5b\x82\xf1"},"Wrong choices for the first future question");
                    pick=first;
                } else {
                    Check(talk.answer_options()==std::vector<std::string>{"\x16"+spelling+"\x16\x8c\x5e\x81\x48",
                        "\x83\x8d\x83\x7b\x83\x62\x83\x67\x81\x48"},"Wrong robot-model answer labels");
                    pick=second;
                }
            }
            if(talk.answer_cursor()!=pick) buttons=0x40;
            else {
                buttons=0x2000; sent=true;
                if(story) story_answers.push_back(pc);
            }
        }
        held=buttons;
        talk.Step(buttons,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Future-topic source boundary "+std::to_string(*pc));
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(pc==0x9389 && talk.message_indirect_topic()==0x3f0b0) {
            Check(talk.text().find("\x16Nana\x16")!=std::string_view::npos,"Missing GAME16 player in name request");
            prompt=true;
        }
        if(pc<0x3f145 || pc>0x3f2e7) continue;
        Check(story && talk.message_indirect_topic()==0x3f0b0,"Future story lost its owning topic");
        story_lines.push_back(pc);
        if(pc==0x3f145) Check(talk.text().find("\x16Toro\x16")==0,"Missing GAME15 nickname");
        if(pc==0x3f1ad)
            Check(talk.text().find(std::to_string(21+century_draw)+"\x90\xa2\x8b\x49")==0 &&
                  talk.message_word_operands().empty(),"Wrong result/format of 2919(21,30)");
        if(pc==0x3f255 || pc==0x3f2a7) {
            const auto& fields=talk.message_word_operands();
            const bool with_vehicle=pc==0x3f2a7 && !no_vehicle;
            // 497A returns field 13 when this learned person has a display name.
            const auto learned=talk.learned_word_row();
            Check(learned && *learned<600,"Expected a taught celebrity in the ordinary dictionary");
            const unsigned name_field=words.HasField(*learned,13)?13:1;
            Check(fields.size()==(with_vehicle?2u:1u) && talk.learned_word_row() &&
                  fields.back().row==int(*learned) && fields.back().local==1 && fields.back().field==name_field &&
                  words.FieldText(*learned,name_field)==spelling &&
                  talk.text().substr(fields.back().offset,fields.back().length)==spelling,
                  "Wrong celebrity word in future robot text");
            if(with_vehicle) {
                const auto& v=fields.front();
                Check(v.local==3 && v.row==(second?59:51) && v.field==1 &&
                      talk.text().substr(v.offset,v.length)==words.FieldText(v.row,1),"Wrong CT7338 vehicle");
            } else if(pc==0x3f2a7)
                Check(talk.text().find("\x16\x8e\xd4\x16")!=std::string_view::npos,"Missing fallback 車");
        }
    }
    Check(talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected() &&
          dispatches==unsigned(!queued) && editors==unsigned(stage==FutureVisit::teach_name) &&
          prompt==!story && talk.learned_word_serial()==before+editors,"Incomplete 3F0B0 name lookup or return");
    if(story) {
        std::vector<unsigned> expected{0x3f145,0x3f164};
        if(first==0) expected.insert(expected.end(),{0x3f197,0x3f1ad});
        else expected.insert(expected.end(),{0x3f1db,0x3f1e4,0x3f20e});
        expected.insert(expected.end(),{0x3f231,0x3f255,0x3f276,0x3f2a7,0x3f2e7});
        Check(story_lines==expected && story_answers==std::vector<unsigned>{0x3f164,0x3f276} &&
              quantity_draws==1 && vehicles==unsigned(!no_vehicle),"Wrong future branching or draws before conversation");
    } else Check(story_lines.empty(),"Early modal return fell through into future robot story");
    return talk.TakeTopicEnqueues();
}

void FutureRobotQuestions(gameplay::RoomTalkState cold,const recovered::PackageView& font) {
    auto declined=cold;
    Check(FutureContact(declined,font,FutureVisit::refuse_name).empty(),"Refused name enqueued a future conversation");
    auto learned=cold;
    const auto scheduled=FutureContact(learned,font,FutureVisit::teach_name);
    const auto row=learned.learned_word_row();
    Check(scheduled==std::vector<std::optional<unsigned>>{0x3f0b0} && row && learned.learned_word()=="Mira" &&
          learned.learned_reading()=="\x82\xdd\x82\xe7" && learned.vocabulary().FieldInt(*row,7)==1 &&
          (learned.vocabulary().FieldInt(*row,12)&6),"Inline celebrity lesson lost its source criteria or queue handle");
    for(unsigned first:{0u,1u}) for(unsigned second:{0u,1u}) {
        auto branch=learned;
        Check(FutureContact(branch,font,FutureVisit::robot_story,scheduled.front(),first,second,second?9:0).empty(),
              "Completed queued robot conversation unexpectedly re-queued");
    }
    // An independently taught favourite is available on the initial D090
    // contact itself. Empty only the vehicle rows in this last component case.
    Check(cold.BeginTeaching(),"Start independent celebrity lesson");
    std::vector<unsigned> messages,answers;
    FinishTeaching(cold,font,"Mira","\x82\xdd\x82\xe7",{1},messages,answers);
    Check(cold.ordinary_source_return() && cold.learned_word()=="Mira" && cold.learned_word_row() &&
          cold.vocabulary().FieldInt(*cold.learned_word_row(),7)==1 &&
          (cold.vocabulary().FieldInt(*cold.learned_word_row(),12)&6),"Ordinary lesson did not produce an eligible celebrity");
    Check(FutureContact(cold,font,FutureVisit::robot_story,{},0,1,9,true).empty(),
          "Directly selected robot conversation unexpectedly enqueued an action");
}

void PrankCall(gameplay::RoomTalkState talk,const recovered::PackageView& font,std::optional<unsigned> row,
               unsigned answer,int initial_mood) {
    auto& words=talk.vocabulary();
    if(!row) // Explicit missing-object fixture. The actual 128C must supply its own マクラ.
        for(unsigned i=146;i<=162;++i) words.WriteNumber(i,0,words.FieldInt(i,0)&~4);
    const std::string object=row?std::string(words.FieldText(*row,1)):"\x83\x7d\x83\x4e\x83\x89";
    const auto learned=talk.learned_word_row();
    const auto serial=talk.learned_word_serial();
    words.BeginConversation();
    talk.SetMood(initial_mood);
    Check(talk.BeginOrdinaryTopic(0x7d43a),"Start D090 prank call");
    unsigned dispatches=0,draws=0,buttons=0,last=talk.message_pc(),resets=0;
    bool answered=false;
    std::vector<unsigned> lines;
    for(unsigned tick=0;tick<12000 && !talk.finished();++tick) {
        if(auto bound=talk.random_requested()) {
            unsigned pick=0;
            if(*bound==1057) { ++dispatches; pick=822; }
            if(lines.empty() && *bound==17) {
                Check(row.has_value(),"Empty CT5E15 unexpectedly drew a word"); ++draws; pick=*row-146;
            }
            talk.SupplyRandom(pick);
        }
        unsigned next=0;
        if(talk.answer_interactive() && !answered && !buttons) {
            Check(talk.message_pc()==0x2e414 && talk.text()=="\x8d\xa2\x82\xe9\x82\xe6\x82\xcb\x81\x49" &&
                  talk.answer_options()==std::vector<std::string>{"\x8d\xa2\x82\xc1\x82\xbd\x82\xcb\x81\x63",
                      "\x83\x80\x83\x4a\x82\xc2\x82\xad\x82\xcb\x81\x49"},"Wrong prank-call question or answers");
            if(talk.answer_cursor()!=answer) next=0x40;
            else { next=0x2000; answered=true; }
        }
        const unsigned age=talk.mood_age();
        buttons=next; talk.Step(buttons,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Prank-call source boundary "+std::to_string(*pc));
        if(talk.mood_age()!=age+1) {
            Check(answer==1 && last==0x2e4e6 && talk.mood()==2 && talk.mood_age()==0 && resets==0,
                  "Wrong frame or value for 2151(2) and CT2872");
            ++resets;
        }
        Check(talk.mood()==(resets?2:initial_mood),"Unexpected mood change before or after the selected reply");
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(pc<0x2e38b || pc>0x2e4e6) continue;
        Check(talk.message_indirect_topic()==0x2e36e,"Prank-call conversation lost D090 owner");
        lines.push_back(pc);
        if(pc==0x2e3d4 || pc==0x2e441 || pc==0x2e4e6) {
            const auto& ops=talk.message_word_operands();
            Check(talk.text().find("\x16"+object+"\x16")!=std::string_view::npos &&
                  ops.size()==unsigned(row.has_value()),"Missing prank-call word or spurious fallback owner");
            if(row)
                Check(ops[0].row==int(*row) && ops[0].local==0 && ops[0].field==1 &&
                      talk.text().substr(ops[0].offset,ops[0].length)==object,"Wrong CT5E15 row or field");
            if(pc!=0x2e3d4) Check(talk.text().find("\x16Toro\x16")==0,"Wrong GAME15 nickname in reply");
        }
    }
    std::vector<unsigned> expected{0x2e38b,0x2e39f,0x2e3d4,0x2e414};
    if(answer==0) expected.insert(expected.end(),{0x2e441,0x2e47a,0x2e494});
    else expected.insert(expected.end(),{0x2e4c6,0x2e4e6});
    Check(answered && lines==expected && dispatches==1 && draws==unsigned(row.has_value()) && resets==answer &&
          talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected() &&
          learned==talk.learned_word_row() && serial==talk.learned_word_serial() && talk.TakeTopicEnqueues().empty(),
          "Incomplete prank-call reply, changed lesson or wrong mood timestamp");
}

void MonthlyMagazine(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                     std::optional<std::array<int32_t,5>> date,unsigned answer,bool fallback=false) {
    auto& words=talk.vocabulary();
    const auto learned=talk.learned_word_row();
    Check(learned==163 && talk.learned_word_serial()==1,"Expected the real lesson before the magazine topic");
    words.BeginConversation();
    // All 109 imported C4F1 matches start with usage 5. The actual learned
    // robot has usage below 5, so the first 1113 collection pass finds only it.
    Check(words.FieldInt(*learned,7)==2 && (words.FieldInt(*learned,12)&6),"Learned word not eligible for C4F1");
    if(fallback) {
        // A component-only copy leaves the matching words in the dictionary
        // but gives them the excluded category. 128C then returns its own literal.
        for(unsigned row=1;row<=*learned;++row)
            if((words.FieldInt(row,0)&4) && (words.FieldInt(row,12)&6)) words.WriteNumber(row,7,4096);
    }
    std::uint32_t minute=0;
    if(date) {
        minute=recovered::CalendarScalar7C660(recovered::CalendarRecord689B8(*date));
        talk.SetCalendarMinute(minute);
    }
    Check(talk.BeginOrdinaryTopic(0x7d43a),"Start D090 calendar conversation");
    std::vector<unsigned> messages,asks;
    unsigned draws=0,dispatches=0,last=talk.message_pc(),held=0;
    bool sent=false,fault=false;
    for(unsigned tick=0;tick<12000 && !talk.finished();++tick) {
        if(auto bound=talk.random_requested()) {
            unsigned pick=0;
            if(*bound==1057) { ++dispatches; pick=30; }
            else if(*bound==1 && messages.empty()) { ++draws; }
            talk.SupplyRandom(pick);
        }
        unsigned buttons=0;
        if(!talk.answer_visible()) sent=false;
        if(talk.answer_interactive() && !sent && !held) {
            Check(date && talk.message_pc()==0x74bc7,"Extra question or missing date in magazine topic");
            const std::string month=std::to_string((*date)[1]),day=std::to_string((*date)[2]);
            Check(talk.text()=="\x8d\xa1\x93\xfa\x82\xcd "+day+"\x93\xfa \x82\xbe\x82\xc1\x82\xaf\x81\x60\x81\x48" &&
                  talk.answer_options()==std::vector<std::string>{"\x8d\xa1\x93\xfa\x82\xcd\x81\x63",
                      month+"\x8c\x8e"+day+"\x93\xfa\x82\xbe\x82\xe6"} && talk.message_word_operands().empty(),
                  "Wrong calendar question, date formatting or answer order");
            if(talk.answer_cursor()!=answer) buttons=0x40;
            else { buttons=0x2000; sent=true; asks.push_back(talk.message_pc()); }
        }
        held=buttons; talk.Step(buttons,true,true,&font);
        if(!talk.error().empty() || talk.ordinary_frontier()) {
            if(!date) { fault=true; break; }
            if(!talk.error().empty()) throw std::runtime_error(talk.error());
            throw std::runtime_error("Magazine source boundary "+std::to_string(*talk.ordinary_frontier()));
        }
        Check(!talk.word_entry_requested(),"Magazine topic unexpectedly requested a new lesson");
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(talk.message_indirect_topic()!=0x7528e) continue;
        messages.push_back(pc);
        // 2246 and 2254 have already returned before 74BBB calls 29B7.
        // A later minute rollover must not overwrite these saved frame locals.
        if(pc==0x29cd && date && (*date)[3]==23 && (*date)[4]==59) talk.SetCalendarMinute(minute+1);
        if(pc==0x752e9) {
            const auto& ops=talk.message_word_operands();
            const auto title=fallback?std::string("\x82\xa9\x82\xc1\x82\xcf\x82\xdc\x82\xab"):talk.learned_word();
            Check(talk.text().find("\x81\x75\x8c\x8e\x8a\xa7 \x83\x6c\x83\x52\x82\xc5\x82\xe0 \x82\xed\x82\xa9\x82\xe9 "+title+
                      "\x81\x76\x82\xcc \x94\xad\x94\x84\x93\xfa")==0 &&
                  ops.size()==(fallback?0u:1u),"Wrong magazine title or invented fallback dictionary owner");
            if(!fallback) Check(ops[0].row==int(*learned) && ops[0].field==1 && ops[0].local==0 &&
                                talk.text().substr(ops[0].offset,ops[0].length)==words.FieldText(*learned,1),
                                "Magazine did not consume the previously learned spelling");
        }
    }
    Check(dispatches==1 && draws==unsigned(!fallback),"Wrong 7528E selection or C4F1 draw");
    if(!date) {
        Check(fault && messages.empty() && asks.empty() && !talk.ordinary_source_return(),
              "Missing calendar silently supplied a date or a complete return");
        return;
    }
    Check(talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected() &&
          messages==std::vector<unsigned>{0x29cd,0x74bc7,0x74bfd,0x752c8,0x752e9} &&
          asks==std::vector<unsigned>{0x74bc7} && talk.TakeTopicEnqueues().empty() && talk.learned_word_serial()==1,
          "Incomplete 74BBB/7528E return or unexpected extra dialogue");
}

void MagazineDates(const gameplay::RoomTalkState& talk,const recovered::PackageView& font) {
    // The room's civil fields and BI09's PSP tick are two views of the
    // same UTC+9 frame sample. 2100 has no February 29.
    const auto century_minute=recovered::CalendarScalar7C660(recovered::CalendarRecord689B8({2100,3,1,0,0}));
    Check(century_minute==52680960u && century_minute==recovered::ClockScalar7C694(66243139200000000ull) &&
          recovered::CalendarBcdDate68FE0(century_minute-1)==0xa00228 &&
          recovered::CalendarBcdDate68FE0(century_minute)==0xa00301,
          "Current minute sources or Gregorian century boundary disagree");
    Check(recovered::CalendarBcdDate68FE0(-1)==0x0101 && recovered::CalendarBcdDate68FE0(-1440)==0x0100,
          "Negative tag-3 payload lost BOOT truncation semantics");
    const std::array<int32_t,5> epoch{2000,1,1,0,0},leap_day{2000,2,29,23,59},march{2000,3,1,0,0};
    Check(recovered::CalendarScalar7C660(recovered::CalendarRecord689B8(epoch))==0 &&
          recovered::CalendarScalar7C660(recovered::CalendarRecord689B8(leap_day))+1==
          recovered::CalendarScalar7C660(recovered::CalendarRecord689B8(march)),"Calendar fixture minute boundaries");
    MonthlyMagazine(talk,font,{},0);
    MonthlyMagazine(talk,font,epoch,0);
    MonthlyMagazine(talk,font,leap_day,1);
    MonthlyMagazine(talk,font,march,0,true);
}

#include "room_quarrel_test.inc"

void AskWearableTraits(gameplay::RoomTalkState& talk,const recovered::PackageView& font,unsigned initial_pool,
                      std::array<unsigned,2> ranks,std::array<std::string_view,2> labels,bool positive_first=true) {
    talk.vocabulary().BeginConversation();
    Check(talk.BeginOrdinaryTopic(0x84e93),"Begin two trait questions");
    unsigned held=0,asked=0,draws=0;
    bool sent=false;
    for(unsigned tick=0;tick<14000 && !talk.finished();++tick) {
        if(auto bound=talk.random_requested()) {
            if(draws<ranks.size() && *bound==initial_pool-draws) talk.SupplyRandom(ranks[draws++]);
            else talk.SupplyRandom(0);
        }
        unsigned next=0;
        if(!talk.answer_visible()) sent=false;
        if(talk.answer_interactive() && !held && !sent) {
            const unsigned choice=(!asked && !positive_first)?1u:0u;
            const auto& operands=talk.message_word_operands();
            Check(asked<2 && talk.message_pc()==0xabe9 && operands.size()==1 &&
                  talk.learned_word_row() && operands.front().row==int(*talk.learned_word_row()) &&
                  operands.front().field==1 && talk.answer_options().front()==labels[asked] &&
                  talk.text().substr(operands.front().offset,operands.front().length)==talk.learned_word(),
                  "The two trait questions must use the garment and the selected KSC forms");
            if(talk.answer_cursor()!=choice) next=0x40;
            else { next=0x2000; sent=true; ++asked; }
        }
        held=next; talk.Step(next,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
    }
    Check(asked==2 && draws==2 && talk.ordinary_source_return(),"Did not finish both source-selected trait questions");
}

void RelearnAfterDeletion(gameplay::RoomTalkState talk,const recovered::PackageView& font) {
    auto& words=talk.vocabulary();
    const unsigned initial_serial=talk.learned_word_serial();
    auto lesson=[&](const std::string& spelling,const std::string& reading) {
        std::vector<unsigned> messages,answers;
        Check(talk.BeginTeaching(),"Open free lesson");
        FinishTeaching(talk,font,spelling,reading,{14},messages,answers);
        const auto row=talk.learned_word_row();
        Check(talk.finished() && talk.ordinary_source_return() && row && words.FindWritten(spelling)==int(*row) &&
              words.FieldText(*row,2)==reading,"Completed wearable lesson must publish its own word");
        return *row;
    };
    const unsigned first=lesson("Ribbon","\x82\xe8\x82\xda\x82\xf1");
    Check(words.FieldInt(first,4)==0,"New first word starts with no conversation selections");
    const std::array<std::string_view,2> important_beautiful{"\x91\xe5\x90\xd8","\x83\x4c\x83\x8c\x83\x43"};
    AskWearableTraits(talk,font,19,{10,15},important_beautiful);
    Check(words.FieldInt(first,4)==1 && words.FieldInt(first,14)==0x82000 && words.FieldInt(first,15)==0x82000 &&
          talk.learned_word_serial()==initial_serial+1,"First word owns both positive traits");

    gameplay::RoomNotebook memo;
    auto press=[&](unsigned key) { memo.Step(0); memo.Step(key); };
    auto open_date=[&] {
        memo.Open(words,0);
        memo.Step(0);
        press(0x80);
        Check(memo.list_ready() && memo.tab()==13,"Open the date list");
    };
    open_date();
    Check(memo.count()==1 && memo.selected_item()->row==first,"Delete the first lesson's word");
    press(0x2000);
    for(unsigned i=0;i<30 && !memo.actions_ready();++i) memo.Step(0);
    Check(memo.actions_ready(),"Word actions open");
    press(0x40);
    press(0x2000);
    for(unsigned i=0;i<60 && memo.active_delete_choice()<0;++i) memo.Step(0);
    Check(memo.active_delete_choice()==0,"Confirm deletion with Yes");
    press(0x2000);
    for(unsigned i=0;i<60 && !memo.deletion_pending();++i) memo.Step(0);
    Check(memo.deletion_pending(),"Notebook issues dictionary deletion");
    memo.FinishDeletion(words);
    Check(memo.list_ready() && !memo.count() && words.FindWritten("Ribbon")==-1 &&
          talk.learned_word_serial()==initial_serial+1,"Empty notebook keeps the completed lesson serial");
    for(unsigned field=0;field<19;++field)
        Check(!words.HasField(first,field),"Deleted row retained a dictionary cell");
    press(0x4000);
    memo.Step(0);
    Check(!memo.visible(),"Return to the room before the next lesson");

    const unsigned second=lesson("Cap","\x82\xda\x82\xa4\x82\xb5");
    Check(second==first && words.FindWritten("Ribbon")==-1 && words.FieldInt(second,14)==0 &&
          words.FieldInt(second,15)==0 && talk.learned_word_serial()==initial_serial+2,
          "First available slot reused with fresh traits and a new lesson serial");
    open_date();
    Check(memo.count()==1 && memo.selected_item()->row==second && memo.selected_item()->written=="Cap",
          "Date list has one live new word after two accepted lessons");
    press(0x4000);
    memo.Step(0);
    // The same questions can be asked about the new spelling. Answer No to
    // the first one to separate its new polarity from the old Ribbon answer.
    AskWearableTraits(talk,font,19,{10,15},important_beautiful,false);
    Check(words.FieldInt(second,4)==1 && words.FieldInt(second,14)==0x80000 && words.FieldInt(second,15)==0x82000 &&
          talk.learned_word_serial()==initial_serial+2,"Replacement remembers its own two answers");
}

void ImportantBelonging(gameplay::RoomTalkState talk,const recovered::PackageView& font,bool important,unsigned response) {
    std::vector<unsigned> messages,answers;
    Check(talk.BeginTeaching(),"Open wearable lesson before the trait conversation");
    FinishTeaching(talk,font,"Ribbon","\x82\xe8\x82\xda\x82\xf1",{14},messages,answers);
    auto& words=talk.vocabulary();
    Check(talk.learned_word_row()==163 && words.FieldInt(163,7)==8192 &&
          (words.FieldInt(163,8)&8) && !words.FieldInt(163,15),"Expected newly taught wearable");
    if(important && response==0) {
        auto history=talk;
        const auto& vocabulary=history.vocabulary();
        // Actual window questions: work (16) and moving (1).
        AskWearableTraits(history,font,19,{13,1},{"\x8e\x64\x8e\x96\x82\xc9\x83\x4a\x83\x93\x83\x50\x81\x5b\x82\xa0\x82\xe9",
                                                 "\x83\x57\x81\x5b\x83\x93\x82\xc6\x20\x82\xb5\x82\xbf\x82\xe1\x82\xa4"});
        Check(vocabulary.FieldInt(163,14)==0x10002 && vocabulary.FieldInt(163,15)==0x10002,
              "Both affirmative window traits belong to the same wearable row");
        // In the next contact, removing 1 and 16 puts index 17 at rank 12
        // and index 2 at rank 1. Both previous answers survive this contact.
        AskWearableTraits(history,font,17,{12,1},{"\x83\x47\x83\x62\x83\x60\x82\xc1\x82\xdb\x82\xa2","\x90\xd8\x82\xc8\x82\xa2"});
        Check(vocabulary.FieldInt(163,14)==0x30006 && vocabulary.FieldInt(163,15)==0x30006,
              "Later contact lost answered traits or repeated a previous question");
    }
    AskWearableTraits(talk,font,19,{10,15},{"\x91\xe5\x90\xd8","\x83\x4c\x83\x8c\x83\x43"},important);
    Check((words.FieldInt(163,15)&0x82000)==0x82000 &&
          (words.FieldInt(163,14)&0x82000)==(important?0x82000:0x80000),"Trait history lost the actual answers");
    words.BeginConversation();
    Check(talk.BeginOrdinaryTopic(0x7d43a),"Begin the ordinary topic dispatcher");
    unsigned held=0;
    bool sent=false;
    unsigned dispatches=0,replies=0;
    bool used=false,prompt=false;
    for(unsigned tick=0;tick<14000 && !talk.finished();++tick) {
        if(auto bound=talk.random_requested()) {
            unsigned pick=0;
            if(*bound==1057) { pick=436; ++dispatches; }
            if(*bound==3) pick=1; // Allow the existing learned row through 9113.
            talk.SupplyRandom(pick);
        }
        unsigned next=0;
        if(!talk.answer_visible()) sent=false;
        if(talk.answer_interactive() && !held && !sent) {
            if(talk.answer_cursor()!=(important?response:1u)) next=0x40;
            else { next=0x2000; sent=true; ++replies; }
        }
        held=next; talk.Step(next,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Important belonging frontier "+std::to_string(*pc));
        if(talk.message_pc()==0x9389) prompt=true;
        if(talk.message_pc()==0x50a89) {
            const auto& ops=talk.message_word_operands();
            Check(talk.message_indirect_topic()==0x5090b && ops.size()==1 && ops[0].row==163 &&
                  ops[0].field==1 && ops[0].local==1 && talk.text().substr(ops[0].offset,ops[0].length)=="Ribbon",
                  "Earthquake dialogue lost the previously confirmed important word");
            used=true;
        }
    }
    Check(dispatches==1 && replies==1 && used==important && prompt!=important &&
          talk.finished() && talk.ordinary_source_return() && talk.learned_word_serial()==1,
          "Wrong outcome of the earlier important/not-important answer");
}

void GarmentConversation(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                         const std::filesystem::path& game,unsigned answer,unsigned expected_row) {
    gameplay::ColdRoomActor actor;
    std::string error;
    if(!actor.Load(game,0,2,error)) throw std::runtime_error(error);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(720),"Garment actor entrance");
    Check(actor.talk_ready() && actor.Request(87),"Garment component starting pose");
    Check(talk.BeginOrdinaryTopic(0x7d43a),"Start ordinary topic dispatcher for garment conversation");
    const auto& words=talk.vocabulary();
    const int initial_uses=words.FieldInt(expected_row,4);
    const std::string garment(words.FieldText(expected_row,1));
    const std::string side=answer==0?"\x92\x6a\x95\xa8":"\x8f\x97\x95\xa8"; // 男物 / 女物
    std::vector<unsigned> lines;
    unsigned held=0,last=0,dispatches=0,questions=0,insertions=0;
    bool sent=false;
    for(unsigned i=0;i<16000 && !talk.finished();++i) {
        if(auto n=talk.random_requested()) {
            unsigned pick=0;
            if(*n==1057) { pick=187; ++dispatches; }
            else if(*n==100) pick=99; // Retain the secondary preference in 128C.
            talk.SupplyRandom(pick);
        }
        unsigned next=0;
        if(talk.answer_interactive() && !sent && !held) {
            Check(talk.message_pc()==0x66daf && talk.answer_options()==std::vector<std::string>{
                "\x92\x6a\x95\xa8\x82\xaa\x83\x43\x83\x43","\x8f\x97\x95\xa8\x82\xaa\x83\x43\x83\x43"},
                "Wrong garment question or options");
            if(talk.answer_cursor()!=answer) next=0x40;
            else { next=0x2000; ++questions; sent=true; }
        }
        Check(actor.Advance(720),"Advance garment actor");
        talk.SetActorField50(int(actor.source_field50()));
        held=next; talk.Step(next,actor.talk_ready(),actor.command_ready(),&font);
        actor.SetMood(talk.mood());
        if(auto command=talk.actor_command()) Check(actor.Request(*command),"Garment actor command");
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        Check(!talk.ordinary_frontier(),"Unresolved garment conversation");
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(pc<0x66d82 || pc>0x66e94) continue;
        Check(talk.message_indirect_topic()==0x66d5f,"Garment message lost its owning topic");
        lines.push_back(pc);
        if(pc==0x66d82 || pc==0x66d95 || pc==0x66e5d) {
            const auto& ops=talk.message_word_operands();
            Check(ops.size()==1 && ops[0].row==expected_row && ops[0].local==0 && ops[0].field==1 &&
                  talk.text().substr(ops[0].offset,ops[0].length)==garment,"Garment spelling changed within story");
            ++insertions;
        }
        if(pc==0x66dff || pc==0x66e5d)
            Check(talk.last_answer()==int(answer+1) && talk.text().substr(0,side.size())==side,
                  "Lost confirmed men's/women's choice");
    }
    Check(lines==std::vector<unsigned>{0x66d82,0x66d95,0x66daf,0x66dff,0x66e14,0x66e30,0x66e5d,0x66e94} &&
          dispatches==1 && questions==1 && insertions==3 && talk.finished() && talk.ordinary_source_return() &&
          !talk.ordinary_rejected() && words.FieldInt(expected_row,4)==initial_uses+1,
          "Incomplete garment conversation or repeated word selection");
    std::cout<<"66D5F answer "<<answer+1<<", word row "<<expected_row<<", three inserts, source return\n";
}

void OutingWithLearnedWord(gameplay::RoomTalkState talk,const recovered::PackageView& font,const std::filesystem::path& game) {
    const std::string written="\x96\xbc\x82\xa9",reading="\x82\xc8\x82\xa9";
    std::vector<unsigned> messages,answers;
    Check(talk.BeginTeaching(),"Start ordinary wearable lesson for the outing");
    FinishTeaching(talk,font,written,reading,{14},messages,answers);
    const auto learned=talk.learned_word_row();
    auto& words=talk.vocabulary();
    Check(learned && words.FieldText(*learned,1)==written && words.FieldText(*learned,2)==reading &&
          words.FieldInt(*learned,7)==8192 && (words.FieldInt(*learned,12)&6),
          "Lesson did not produce an eligible word");
    const std::vector<int> predicate{0,6,4,11,7,2,7,8,12672,12,8,6};
    for(unsigned ct:{0xddc8u,0xddd7u})
        Check(words.PredicateGroup(ct,0)==predicate,"Outing lookup predicate changed");
    Check(words.FieldInt(*learned,4)==0,"Usage after learning");
    GarmentConversation(talk,font,game,0,*learned);
    const unsigned first_imported=23;
    const std::vector<unsigned> imported{23,24,25,26,27,28,29,30,31,32,43,44,45,46,
                                        146,147,148,149,150,151,152,153,154,155,156,157};
    for(int contact=0;contact<=5;++contact) {
        std::vector<unsigned> eligible;
        for(unsigned row=0;row<words.row_count();++row)
            if((words.FieldInt(row,0)&4) && !(words.FieldInt(row,11)&2) &&
               (words.FieldInt(row,7)&12672) && (words.FieldInt(row,12)&6)) eligible.push_back(row);
        auto expected=imported;
        expected.push_back(*learned);
        Check(eligible==expected && words.FieldInt(*learned,4)==contact,"Outing eligible set and history");
        // 1113 prefers usage <5. Each imported match starts at 5.
        const unsigned first_bound=contact<5?1u:unsigned(eligible.size());
        const unsigned first=contact<5?*learned:first_imported;
        const unsigned second=contact<5?first_imported:*learned;
        const int first_usage=words.FieldInt(first,4),second_usage=words.FieldInt(second,4);
        Check(talk.BeginOrdinaryTopic(0x84b63),"Start outing topic");
        unsigned draws=0,replies=0,held=0;
        bool sent=false,shown=false,continued=false;
        for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
            if(auto bound=talk.random_requested()) {
                unsigned pick=0;
                if(replies==2 && draws==0) {
                    Check(*bound==first_bound,"Wrong usage tier in DDC8");
                    ++draws;
                } else if(replies==2 && draws==1) {
                    Check(*bound==imported.size() && (words.FieldInt(first,11)&2) && !(words.FieldInt(second,11)&2) &&
                          words.FieldInt(first,4)==first_usage+1,"Second lookup must see first row consumed");
                    pick=contact<5?0:*bound-1;
                    ++draws;
                }
                talk.SupplyRandom(pick);
            }
            unsigned next=0;
            if(!talk.answer_visible()) sent=false;
            if(talk.answer_interactive() && !sent && !held) {
                next=0x2000; sent=true; ++replies;
            }
            held=next; talk.Step(next,true,true,&font);
            if(!talk.error().empty()) throw std::runtime_error(talk.error());
            Check(!talk.ordinary_frontier(),"Unexpected source frontier in outing");
            if(talk.message_pc()==0x84ca6) {
                const auto& ops=talk.message_word_operands();
                Check(ops.size()==2 && ops[0].row==first && ops[1].row==second,"Wrong outing rows");
                Check((words.FieldInt(first,11)&2) && (words.FieldInt(second,11)&2),
                      "Both outing rows must be marked when shown");
                for(unsigned i=0;i<2;++i) {
                    const auto& op=ops[i];
                    Check(op.local==i+1 && op.control_pc==(i==0?0x84ca8u:0x84cb5u) && op.field==1 &&
                          talk.text().substr(op.offset,op.length)==words.FieldText(op.row,1),
                          "Outing text lost the selected row's spelling");
                }
                shown=true;
            }
            if(talk.message_pc()==0x84ccb) continued=true;
        }
        Check(talk.finished() && talk.ordinary_source_return(),"Outing did not return");
        Check(replies==2 && draws==2,"Missing outing answer or lookup");
        Check(shown && continued,"Missing outing words or subsequent dialogue");
        Check(words.FieldInt(second,4)==second_usage+1 && words.FieldInt(first,4)==first_usage+1,
              "Outing usage after both draws");
        std::cout<<"84B63 contact "<<contact+1<<": DDC8 row "<<first<<"/"<<first_bound
                 <<", DDD7 row "<<second<<"/26, 84CA6 -> 84CCB -> return\n";
    }
}

void CrowdedPlace(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                  const std::filesystem::path& game,bool teach,unsigned answer) {
    const std::string spelling="\x82\xa8\x82\xb5\x82\xe1\x82\xd7\x82\xe8"; // おしゃべり
    if(!teach) {
        std::vector<unsigned> lines,answers;
        Check(talk.BeginTeaching(),"Wearable lesson before the crowded-place request");
        FinishTeaching(talk,font,"\x96\xbc\x82\xa9","\x82\xc8\x82\xa9",{14},lines,answers);
        const auto row=talk.learned_word_row();
        Check(row && talk.vocabulary().FieldInt(*row,7)==8192,"Missing wearable prerequisite");
    }
    gameplay::ColdRoomActor actor;
    std::string error;
    if(!actor.Load(game,0,2,error)) throw std::runtime_error(error);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(720),"Crowded place actor entrance");
    Check(actor.talk_ready() && actor.Request(87),"Crowded place component starting pose");
    std::optional<unsigned> queued;
    for(unsigned contact=0;contact<(teach?2u:1u);++contact) {
        const bool story=contact==1;
        Check(talk.BeginOrdinaryTopic(queued.value_or(0x7d43a),queued.has_value()),"Open crowded-place conversation");
        unsigned held=0,dispatches=0,editors=0,questions=0,last=0;
        bool sent=false,prompt=false,subtype=false;
        int place=-1;
        std::vector<unsigned> lines;
        for(unsigned i=0;i<20000 && !talk.finished();++i) {
            if(auto n=talk.random_requested()) {
                unsigned pick=0;
                if(*n==1057) { pick=965; ++dispatches; } // Real 202B8 slot in D090.
                else if(*n==3 && story) pick=1;
                talk.SupplyRandom(pick);
            }
            if(talk.word_entry_requested()) {
                Check(teach && !story && editors++==0,"Unexpected crowded-place word editor");
                talk.FinishWordEntry(spelling,spelling);
            }
            unsigned next=0;
            if(!talk.answer_visible()) sent=false;
            if(talk.answer_interactive() && !sent && !held) {
                const auto pc=talk.message_pc();
                unsigned choice=0;
                if(pc==0x9389 && !teach) choice=1;
                if(pc==0x5978) choice=answer; // Doing something to others, or having it done to oneself.
                if(pc==0x5a0c) {
                    const std::string prefix=answer?"\x82\xb3\x82\xea":"\x82\xb5";
                    Check(talk.answer_options().front().compare(0,prefix.size(),prefix)==0,
                          "Action subtype used the wrong direction");
                    subtype=true;
                }
                if(pc==0x203d6) { choice=answer; Check(story,"Premature crowded-place story"); }
                if(talk.answer_cursor()!=choice) next=0x40;
                else { next=0x2000; sent=true; if(pc==0x203d6) ++questions; }
            }
            Check(actor.Advance(720),"Crowded place actor update");
            talk.SetActorField50(int(actor.source_field50()));
            held=next; talk.Step(next,actor.talk_ready(),actor.command_ready(),&font);
            actor.SetMood(talk.mood());
            if(auto cmd=talk.actor_command(); cmd && !actor.Request(*cmd))
                throw std::runtime_error("Missing crowded-place actor command "+std::to_string(*cmd));
            if(!talk.error().empty()) throw std::runtime_error(talk.error());
            if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Crowded-place source frontier "+std::to_string(*pc));
            const auto pc=talk.message_pc();
            if(pc==last) continue;
            last=pc;
            if(pc==0x9389) prompt=true;
            if(pc<0x20342 || pc>0x2044c) continue;
            lines.push_back(pc);
            if(pc==0x2035f || pc==0x203a7 || pc==0x2044c) {
                const auto& ops=talk.message_word_operands();
                Check(ops.size()==1 && ops[0].field==1,"Missing crowded-place dictionary field");
                const auto& op=ops[0];
                if(pc==0x2035f) {
                    Check(op.local==2 && op.row>=134 && op.row<=136 &&
                          talk.text().substr(op.offset,op.length)==talk.vocabulary().FieldText(op.row,1),
                          "Story lost its place");
                    place=op.row;
                } else Check(op.local==1 && talk.learned_word_row()==op.row &&
                             talk.text().substr(op.offset,op.length)==spelling,"Story lost the learned action");
            }
        }
        Check(talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected() &&
              dispatches==unsigned(!story) && editors==unsigned(teach && !story) &&
              prompt==!story && questions==unsigned(story) && subtype==(teach && !story),"Wrong crowded-place return");
        const auto scheduled=talk.TakeTopicEnqueues();
        if(teach && !story) {
            const auto row=talk.learned_word_row();
            Check(row && (talk.vocabulary().FieldInt(*row,7)&48) && scheduled==std::vector<std::optional<unsigned>>{0x202b8},
                  "Teaching the activity did not schedule its owning conversation");
            queued=scheduled.front();
        } else Check(scheduled.empty(),"Spurious crowded-place queue entry");
        std::vector<unsigned> expected;
        if(story) {
            expected={0x20342,0x2035f,0x20384,0x203a7,0x203d6};
            if(answer) expected.push_back(0x2044c);
            else expected.insert(expected.end(),{0x20413,0x20429});
        }
        Check(lines==expected,"Wrong crowded-place story or reaction");
        if(story) std::cout<<"202B8 queued story: place row "<<place<<", action row "<<*talk.learned_word_row()
                           <<", answer "<<answer+1<<", final line "<<std::hex<<lines.back()<<std::dec<<", returned\n";
        else std::cout<<"202B8 D090[965]: prompt 9389, editors "<<editors<<", enqueued "<<scheduled.size()<<", returned\n";
    }
}

void MorningConversation(gameplay::RoomTalkState talk,const recovered::PackageView& font,const std::filesystem::path& game,
                  unsigned greeting_answer,std::optional<unsigned> activity_answer) {
    gameplay::ColdRoomActor actor;
    gameplay::RoomActivityPhase phase;
    gameplay::RoomActivitySelector selector;
    recovered::RngNativeOwner rng;
    std::string error;
    if(!actor.Load(game,0,2,error) || !selector.Load(game/"assets/game/post-profile",error))
        throw std::runtime_error(error);
    rng.ApplyInitializerTimeT32(168,1,"Morning plans component");
    phase.Enter(720,rng);
    phase.ReturnToRoom(720,talk.mood(),rng);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(720),"Morning plans actor entrance");
    Check(actor.talk_ready() && actor.Request(87),"Start morning conversation pose");
    talk.vocabulary().BeginConversation();
    const unsigned topic=activity_answer?0x82bfa:0x8304e;
    Check(phase.BeginOrdinary() && talk.BeginOrdinaryTopic(topic),"Start selected morning conversation");
    std::vector<std::string> activities;
    std::string adjective;
    std::vector<unsigned> alarm_lines;
    unsigned questions=0,held=0,last_pc=0,mentions=0;
    int selected_row=-1;
    bool sent=false;
    for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
        if(auto n=talk.random_requested()) talk.SupplyRandom(greeting_answer ? *n-1 : 0);
        unsigned next=0;
        if(!talk.answer_visible()) sent=false;
        if(talk.answer_interactive() && !sent && !held) {
            const auto& options=talk.answer_options();
            Check(questions<(activity_answer?2u:1u) && options.size()==2,"Unexpected morning question");
            const auto answer=questions ? activity_answer.value() : greeting_answer;
            if(!questions) {
                Check(!adjective.empty() && options[1].find(adjective)!=std::string::npos,
                      "Morning reply lost the phrase drawn for the prelude");
            } else {
                Check(options[0]!=options[1],"Expected separate activity draws on these component inputs");
                activities=options;
            }
            if(talk.answer_cursor()!=answer) next=0x40;
            else { next=0x2000; sent=true; ++questions; }
        }
        Check(actor.Advance(720),"Morning conversation actor advance");
        talk.SetActorField50(int(actor.source_field50()));
        held=next; talk.Step(next,actor.talk_ready(),actor.command_ready(),&font);
        actor.SetMood(talk.mood());
        if(auto command=talk.actor_command()) Check(actor.Request(*command),"Play morning conversation pose");
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        Check(!talk.ordinary_frontier(),"Morning conversation reached an unsupported procedure");
        auto pc=talk.message_pc();
        if(pc==last_pc) continue;
        last_pc=pc;
        if(pc==0x823d2) {
            const auto end=talk.text().find("\x82\xc8 "); // な before 朝
            Check(end!=std::string_view::npos,"Missing morning adjective");
            adjective=talk.text().substr(0,end);
            Check(adjective==(greeting_answer?"\x83\x45\x83\x62\x83\x67\x83\x8a":
                              "\x83\x8d\x83\x7d\x83\x93\x83\x60\x83\x62\x83\x4e"),
                  "Morning phrase did not match the selected CT2469 entry");
        }
        if(pc==0x8305a || pc==0x83086) {
            const auto prefix=pc==0x8305a?"\x96\xda\x8a\x6f\x82\xdc\x82\xb5\x20\x8e\x9e\x8c\x76\x82\xe6\x82\xe8\x20\x91\x81\x82\xad\x8b\x4e\x82\xab\x82\xe9\x82\xc6\x81\x63":"\x82\xc8\x82\xf1\x82\xa9\x20\x8f\x9f\x82\xc1\x82\xbd\x8b\x43\x82\xaa\x20\x82\xb7\x82\xe9";
            Check(!activity_answer && questions==1 && talk.last_answer()==int(greeting_answer+1) &&
                  talk.text().find(prefix)==0 && talk.message_word_operands().empty(),"Wrong alarm clock line");
            alarm_lines.push_back(pc);
        }
        if(pc==0x82c67 || pc==0x82c8a) {
            Check(questions==2 && talk.last_answer()==int(activity_answer.value()+1) &&
                  talk.text().find(activities.at(activity_answer.value()))!=std::string_view::npos,
                  "2945 did not carry the selected activity into both lines");
            const auto& ops=talk.message_word_operands();
            Check(ops.size()==1 && ops[0].local==2 && talk.text().substr(ops[0].offset,ops[0].length)==
                  talk.vocabulary().FieldText(ops[0].row,ops[0].field),"Wrong morning activity word operand");
            if(mentions) Check(ops[0].row==selected_row,"Morning activity changed between the two lines");
            selected_row=ops[0].row;
            ++mentions;
        }
    }
    Check(talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected(),"Morning conversation return");
    const auto resumed=ResumeActivity(phase,actor,talk,selector,rng,2,720);
    if(activity_answer) {
        Check(questions==2 && mentions==2 && alarm_lines.empty(),"Missing activity or repeated word after morning question");
        std::cout<<"82BFA prelude "<<greeting_answer+1<<", activity "<<*activity_answer+1
                 <<", word row "<<selected_row<<" at 82C67 and 82C8A, return -> ASC "<<resumed<<"\n";
    } else {
        Check(questions==1 && mentions==0 && alarm_lines==std::vector<unsigned>{0x8305a,0x83086},"Alarm clock sequence");
        std::cout<<"8304E prelude "<<greeting_answer+1<<", lines 8305A/83086, return -> ASC "<<resumed<<"\n";
    }
}

void AutumnVoice(gameplay::RoomTalkState talk,const recovered::PackageView& font,const std::filesystem::path& game,
                 unsigned month_answer,unsigned autumn_answer) {
    auto& words=talk.vocabulary();
    int sound_row=-1;
    const std::string custom_sound="RinRin"; // A component-only populated D9F0 row.
    if(month_answer) {
        sound_row=words.ReserveWord();
        Check(sound_row>=0,"Reserve an autumn call for the component");
        words.InitializeReserved(sound_row,custom_sound,custom_sound);
        words.WriteNumber(sound_row,7,32768);
        words.WriteNumber(sound_row,8,0);
        Check(words.AcceptReserved(sound_row),"Register autumn call fixture");
    }
    const auto minute=recovered::CalendarScalar7C660(recovered::CalendarRecord689B8({2026,11,19,12,0}));
    gameplay::RoomActivityPhase phase;
    gameplay::RoomActivitySelector selector;
    gameplay::ColdRoomActor actor;
    std::string error;
    if(!actor.Load(game,0,2,error) || !selector.Load(game/"assets/game/post-profile",error))
        throw std::runtime_error(error);
    recovered::RngNativeOwner rng;
    rng.ApplyInitializerTimeT32(168,1,"Autumn voice component");
    phase.Enter(minute,rng);
    phase.ReturnToRoom(minute,talk.mood(),rng);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(minute),"Autumn actor entrance");
    Check(actor.talk_ready() && actor.Request(87),"Start autumn conversation pose");
    words.BeginConversation();
    talk.SetCalendarMinute(minute);
    Check(phase.BeginOrdinary() && talk.BeginOrdinaryTopic(0x810a0),"Start ordinary November topic");
    std::vector<unsigned> lines;
    unsigned questions=0,held=0,last=0;
    int voice_row=-1;
    bool sent=false;
    for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
        if(auto n=talk.random_requested()) talk.SupplyRandom(autumn_answer ? *n-1 : 0);
        unsigned next=0;
        if(!talk.answer_visible()) sent=false;
        if(talk.answer_interactive() && !held && !sent) {
            const auto& options=talk.answer_options();
            Check(questions<2 && options.size()==2,"Autumn question count");
            if(!questions) Check(options[1].find("11\x8c\x8e")!=std::string::npos,"Month missing in second reply");
            const unsigned answer=questions?autumn_answer:month_answer;
            if(talk.answer_cursor()!=answer) next=0x40;
            else { next=0x2000; sent=true; ++questions; }
        }
        Check(actor.Advance(minute),"Advance autumn actor");
        talk.SetActorField50(int(actor.source_field50()));
        held=next; talk.Step(next,actor.talk_ready(),actor.command_ready(),&font);
        actor.SetMood(talk.mood());
        if(auto command=talk.actor_command()) Check(actor.Request(*command),"Play autumn conversation ASC");
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        Check(!talk.ordinary_frontier(),"Unresolved autumn conversation");
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(pc<0x810a0 || pc>=0x8121c) continue;
        lines.push_back(pc);
        const auto& ops=talk.message_word_operands();
        if(pc==0x810df) Check(talk.text().find("11\x8c\x8e")!=std::string::npos && ops.empty(),"Autumn civil month");
        if(pc==0x8111c || pc==0x811f6) {
            Check(ops.size()==1 && ops[0].local==1 && ops[0].field==1 &&
                  talk.text().substr(ops[0].offset,ops[0].length)==words.FieldText(ops[0].row,1),
                  "Autumn creature display word");
            if(pc==0x8111c) voice_row=ops[0].row;
            else Check(voice_row==ops[0].row,"Creature changed after the answer");
        }
        if(pc==0x81197 || pc==0x811ec) {
            if(sound_row<0) Check(ops.empty() && talk.text().find("\x83\x60\x83\x93\x83\x60\x83\x8d\x83\x8a\x83\x93")!=std::string::npos,
                                  "Missing original autumn sound fallback");
            else Check(ops.size()==1 && ops[0].row==sound_row && ops[0].local==2 &&
                       talk.text().substr(ops[0].offset,ops[0].length)==custom_sound,"Autumn sound lookup");
        }
    }
    std::vector<unsigned> expected{0x810df,0x81109,0x8111c,0x81144};
    if(autumn_answer) expected.insert(expected.end(),{0x811c0,0x811ec,0x811f6});
    else expected.insert(expected.end(),{0x8117c,0x81197,0x811a1});
    Check(questions==2 && lines==expected && talk.finished() && talk.ordinary_source_return() &&
          !talk.ordinary_rejected(),"Autumn branch or owning return");
    const auto resumed=ResumeActivity(phase,actor,talk,selector,rng,2,minute);
    std::cout<<"810A0 replies "<<month_answer+1<<"/"<<autumn_answer+1<<", voice row "<<voice_row
             <<", sound row "<<sound_row<<", return -> ASC "<<resumed<<"\n";
}

void IntroduceFriends(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                      const std::filesystem::path& game,unsigned answer) {
    gameplay::ColdRoomActor actor;
    gameplay::RoomActivityPhase phase;
    gameplay::RoomActivitySelector selector;
    recovered::RngNativeOwner rng;
    std::string error;
    if(!actor.Load(game,0,2,error) || !selector.Load(game/"assets/game/post-profile",error))
        throw std::runtime_error(error);
    rng.ApplyInitializerTimeT32(168,1,"Introduction request component");
    constexpr unsigned minute=720;
    phase.Enter(minute,rng);
    phase.ReturnToRoom(minute,talk.mood(),rng);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(minute),"Introduction actor entrance");
    Check(actor.talk_ready() && actor.Request(87),"Start conversation pose");
    talk.vocabulary().BeginConversation();
    talk.SetCalendarMinute(minute);
    Check(phase.BeginOrdinary() && talk.BeginOrdinaryTopic(0x85439),"Start request for an introduction");
    const auto initial_mood=talk.mood();
    std::vector<unsigned> lines;
    std::vector<int> word_rows;
    unsigned held=0,last=0;
    bool sent=false;
    for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
        if(auto n=talk.random_requested()) talk.SupplyRandom(rng.NextC3234()%*n);
        unsigned buttons=0;
        if(talk.answer_interactive() && !sent && !held) {
            Check(talk.message_pc()==0x854ec && talk.answer_options().size()==2,"Wrong introduction question");
            if(talk.answer_cursor()!=answer) buttons=0x40;
            else { buttons=0x2000; sent=true; }
        }
        Check(actor.Advance(minute),"Advance introduction actor");
        talk.SetActorField50(int(actor.source_field50()));
        talk.Step(buttons,actor.talk_ready(),actor.command_ready(),&font);
        held=buttons;
        actor.SetMood(talk.mood());
        if(auto command=talk.actor_command()) Check(actor.Request(*command),"Play introduction pose");
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        Check(!talk.ordinary_frontier(),"Missing continuation of introduction request");
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(pc<0x85439 || pc>=0x8566e) continue;
        lines.push_back(pc);
        if(pc==0x8555e || pc==0x8557c || pc==0x8563a) {
            const auto& ops=talk.message_word_operands();
            Check(ops.size()==(pc==0x8563a?2u:1u),"Missing occupation in spoken line");
            for(const auto& op:ops) {
                Check(op.local==int(word_rows.size()) && op.field==1 &&
                      talk.text().substr(op.offset,op.length)==talk.vocabulary().FieldText(op.row,1) &&
                      talk.vocabulary().FieldInt(op.row,7)==8 && (talk.vocabulary().FieldInt(op.row,12)&6),
                      "Occupation did not come from the selected dictionary row");
                word_rows.push_back(op.row);
            }
        }
    }
    std::vector<unsigned> expected{0x85477,0x85485,0x854ab,0x854c5,0x854ec};
    if(answer) expected.insert(expected.end(),{0x855c0,0x855de,0x855f6,0x85623,0x8563a});
    else expected.insert(expected.end(),{0x8552a,0x8555e,0x8557c,0x8559d});
    Check(sent && talk.last_answer()==int(answer+1) && lines==expected && word_rows.size()==2 &&
          talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected(),"Incomplete introduction branch");
    Check(talk.mood()==(answer?1:initial_mood),"Refusal mood from 2151(1)");
    const auto command=ResumeActivity(phase,actor,talk,selector,rng,2,minute);
    std::cout<<"85439 answer "<<answer+1<<", occupation rows "<<word_rows[0]<<"/"<<word_rows[1]
             <<", mood "<<talk.mood()<<", return -> ASC "<<command<<"\n";
}

void PracticeAtPlace(gameplay::RoomTalkState talk,const recovered::PackageView& font,const std::filesystem::path& game,
                     bool teach,unsigned place_type,unsigned story_answer) {
    const std::string place="Plaza";
    auto& words=talk.vocabulary();
    const auto prior=talk.learned_word_serial();
    gameplay::ColdRoomActor actor;
    gameplay::RoomActivitySelector selector;
    gameplay::RoomActivityPhase phase;
    recovered::RngNativeOwner rng;
    std::string error;
    if(!actor.Load(game,0,2,error) || !selector.Load(game/"assets/game/post-profile",error))
        throw std::runtime_error(error);
    rng.ApplyInitializerTimeT32(168,1,"Practice-place component");
    phase.Enter(720,rng);
    phase.ReturnToRoom(720,talk.mood(),rng);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(720),"Practice-place actor entrance");
    Check(actor.talk_ready() && actor.Request(87),"Practice-place initial pose");
    std::optional<unsigned> queued;
    for(unsigned visit=0;visit<(teach?2u:1u);++visit) {
        words.BeginConversation();
        Check(phase.BeginOrdinary() && talk.BeginOrdinaryTopic(queued.value_or(0x7d43a),queued.has_value()),
              "Start practice-place contact");
        unsigned held=0,last=0,editors=0,prompts=0,story_questions=0,draws=0;
        bool sent=false;
        std::string activity;
        std::vector<unsigned> lines;
        for(unsigned tick=0;tick<18000 && !talk.finished();++tick) {
            if(auto n=talk.random_requested()) {
                if(*n==1057) ++draws;
                talk.SupplyRandom(*n==1057?361:0); // Component draw of the original D090 slot.
            }
            if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Practice-place frontier "+std::to_string(*pc));
            if(talk.word_entry_requested()) {
                Check(teach && !visit && !editors,"Unexpected practice-place editor");
                ++editors;
                talk.FinishWordEntry(place,place);
            }
            if(!talk.answer_visible()) sent=false;
            unsigned buttons=0;
            if(talk.answer_interactive() && !sent && !held) {
                const auto pc=talk.message_pc();
                unsigned choice=0;
                if(pc==0x9389) choice=unsigned(!teach);
                if(pc==0x5f89) choice=place_type;
                if(pc==0x58026) choice=story_answer;
                if(talk.answer_cursor()!=choice) buttons=0x40;
                else {
                    buttons=0x2000; sent=true;
                    if(pc==0x9389) ++prompts;
                    if(pc==0x58026) {
                        const auto& labels=talk.answer_options();
                        Check(labels.size()==2 && !activity.empty() && labels.front().find(activity)!=std::string::npos,
                              "Practice question lost its selected action");
                        ++story_questions;
                    }
                }
            }
            held=buttons;
            Check(actor.Advance(720),"Practice-place actor update");
            talk.SetActorField50(int(actor.source_field50()));
            talk.Step(buttons,actor.talk_ready(),actor.command_ready(),&font);
            actor.SetMood(talk.mood());
            if(auto cmd=talk.actor_command()) Check(actor.Request(*cmd),"Play practice-place pose");
            if(!talk.error().empty()) throw std::runtime_error(talk.error());
            const auto pc=talk.message_pc();
            if(pc==last) continue;
            last=pc;
            if(pc<0x58011 || pc>0x580db) continue;
            lines.push_back(pc);
            if(pc==0x58011 || pc==0x58026 || pc==0x58070) {
                const auto& ops=talk.message_word_operands();
                Check(ops.size()==1 && ops.front().field==1,"Missing place or activity word in the story");
                const auto& op=ops.front();
                const auto text=words.FieldText(op.row,1);
                Check(talk.text().substr(op.offset,op.length)==text,"Practice-place spelling mismatch");
                if(pc==0x58011) Check(op.local==1 && talk.learned_word_row()==op.row && text==place,"Wrong taught place");
                else {
                    Check(op.local==3 && (words.FieldInt(op.row,7)&48) && (words.FieldInt(op.row,12)&6),
                          "Wrong kind of action in the practice-place story");
                    if(pc==0x58026) activity=text;
                    else Check(text==activity,"Practice action changed after the answer");
                }
            }
        }
        Check(talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected() &&
              draws==unsigned(!visit) && prompts==unsigned(!visit) && editors==unsigned(teach && !visit) &&
              story_questions==visit,"Wrong practice-place lesson or source return");
        const auto pending=talk.TakeTopicEnqueues();
        if(!visit && teach) {
            const auto row=talk.learned_word_row();
            Check(row && words.FieldInt(*row,7)==64 && (words.FieldInt(*row,0)&32) &&
                  pending==std::vector<std::optional<unsigned>>{0x57f95},"Place registration or scheduled topic missing");
            const std::array<int,6> subtype_masks{32,0x5000,6,0x400,0x800,1};
            Check(words.FieldInt(*row,8)==subtype_masks.at(place_type),"Incorrect place subtype after teaching");
            queued=pending.front();
        } else Check(pending.empty(),"Unexpected queued practice-place contact");
        Check(talk.learned_word_serial()==prior+unsigned(teach),"Practice-place registration count");
        if(visit) Check(lines==std::vector<unsigned>{0x58011,0x58026,0x58070,0x580b3,0x580db} &&
                        talk.last_answer()==int(story_answer+1),"Practice-place reaction or final line");
        else Check(lines.empty(),"Practice-place story shown during the lesson");
        const auto resumed=ResumeActivity(phase,actor,talk,selector,rng,2,720);
        std::cout<<"57F95 editors "<<editors<<", queued "<<pending.size()<<", place type "<<place_type
                 <<", story answer "<<(visit?story_answer+1:0)<<", return -> ASC "<<resumed<<"\n";
    }
}

void NightAtLearnedPlace(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                       const std::filesystem::path& game,unsigned answer) {
    gameplay::ColdRoomActor actor;
    gameplay::RoomActivityPhase phase;
    gameplay::RoomActivitySelector selector;
    recovered::RngNativeOwner rng;
    std::string error;
    if(!actor.Load(game,0,2,error) || !selector.Load(game/"assets/game/post-profile",error))
        throw std::runtime_error(error);
    rng.ApplyInitializerTimeT32(168,1,"Night at the taught place component");
    const auto place=talk.learned_word_row();
    const auto learned=talk.learned_word_serial();
    talk.vocabulary().BeginConversation();
    phase.Enter(720,rng);
    phase.ReturnToRoom(720,talk.mood(),rng);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(720),"Night story actor entrance");
    Check(actor.talk_ready() && actor.Request(87) && phase.BeginOrdinary() && talk.BeginOrdinaryTopic(0x7d43a),
          "Start ordinary dispatcher for the night story");
    std::vector<unsigned> lines;
    std::vector<int> rows;
    unsigned held=0,last=0,draws=0;
    bool sent=false;
    for(unsigned i=0;i<16000 && !talk.finished();++i) {
        if(auto n=talk.random_requested()) {
            if(*n==1057) ++draws;
            talk.SupplyRandom(*n==1057?206:rng.NextC3234()%*n);
        }
        Check(!talk.ordinary_frontier() && !talk.word_entry_requested(),"Night story did not use the existing place");
        unsigned buttons=0;
        if(talk.answer_interactive() && !held && !sent) {
            const auto& labels=talk.answer_options();
            Check(talk.message_pc()==0x653fa && rows.size()==2 && labels.size()==2 &&
                  labels[0].find(talk.vocabulary().FieldText(rows[1],1))!=std::string::npos,
                  "Night question did not offer its selected vehicle");
            if(talk.answer_cursor()!=answer) buttons=0x40;
            else { buttons=0x2000; sent=true; }
        }
        Check(actor.Advance(720),"Advance night story actor");
        talk.SetActorField50(int(actor.source_field50()));
        talk.Step(buttons,actor.talk_ready(),actor.command_ready(),&font);
        held=buttons;
        actor.SetMood(talk.mood());
        if(auto command=talk.actor_command()) Check(actor.Request(*command),"Play night story pose");
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(pc!=0x653d7 && pc!=0x653fa && pc!=0x65436) continue;
        lines.push_back(pc);
        const auto& ops=talk.message_word_operands();
        Check(ops.size()==1 && ops.front().field==1,"Night story lost a dictionary spelling");
        const auto& op=ops.front();
        const auto& words=talk.vocabulary();
        Check(talk.text().substr(op.offset,op.length)==words.FieldText(op.row,1),"Night story displayed a different word");
        const auto kind=words.FieldInt(op.row,7);
        if(pc==0x653d7) Check(op.local==1 && place==op.row,"Wrong place after free restaurant lesson");
        if(pc==0x653fa) Check(op.local==3 && (kind==16384 || kind==2),"Wrong vehicle lookup");
        if(pc==0x65436) Check(op.local==4 && (kind&48) && (words.FieldInt(op.row,12)&6),"Wrong couples' activity");
        rows.push_back(op.row);
    }
    Check(sent && draws==1 && talk.last_answer()==int(answer+1) && lines==std::vector<unsigned>{0x653d7,0x653fa,0x65436}
          && talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected() && talk.TakeTopicEnqueues().empty()
          && talk.learned_word_serial()==learned,"Incomplete night story after the free lesson");
    const auto resumed=ResumeActivity(phase,actor,talk,selector,rng,2,720);
    std::cout<<"65345 answer "<<answer+1<<", place/vehicle/action rows "<<rows[0]<<"/"<<rows[1]<<"/"<<rows[2]
             <<", return -> ASC "<<resumed<<"\n";
}

int AutumnColours(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                  const std::filesystem::path& game,unsigned answer,bool empty_food,bool last_draw=false) {
    auto& words=talk.vocabulary();
    if(empty_food) // Component-only copy with the ten starter foods unavailable.
        for(unsigned row=23;row<=32;++row) words.WriteNumber(row,0,words.FieldInt(row,0)&~4);
    const auto minute=recovered::CalendarScalar7C660(recovered::CalendarRecord689B8({2026,11,19,12,0}));
    gameplay::RoomActivityPhase phase;
    gameplay::RoomActivitySelector selector;
    gameplay::ColdRoomActor actor;
    recovered::RngNativeOwner rng;
    std::string error;
    if(!actor.Load(game,0,2,error) || !selector.Load(game/"assets/game/post-profile",error))
        throw std::runtime_error(error);
    rng.ApplyInitializerTimeT32(168,1,"Autumn colours component");
    phase.Enter(minute,rng);
    phase.ReturnToRoom(minute,talk.mood(),rng);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(minute),"Autumn actor entrance");
    words.BeginConversation();
    talk.SetCalendarMinute(minute);
    Check(actor.talk_ready() && actor.Request(87) && phase.BeginOrdinary() && talk.BeginOrdinaryTopic(0x8121c),
          "Start November colour conversation");
    std::vector<unsigned> lines;
    unsigned held=0,last=0;
    bool sent=false;
    int food_row=-1;
    for(unsigned i=0;i<16000 && !talk.finished();++i) {
        if(auto n=talk.random_requested()) talk.SupplyRandom(last_draw?*n-1:rng.NextC3234()%*n);
        unsigned buttons=0;
        if(talk.answer_interactive() && !held && !sent) {
            const auto& labels=talk.answer_options();
            Check(talk.message_pc()==0x81246 && labels.size()==2 && labels[1].find("11\x8c\x8e")!=std::string::npos,
                  "Monthly question or month in the second answer");
            if(talk.answer_cursor()!=answer) buttons=0x40;
            else { buttons=0x2000; sent=true; }
        }
        Check(actor.Advance(minute),"Advance monthly conversation actor");
        talk.SetActorField50(int(actor.source_field50()));
        talk.Step(buttons,actor.talk_ready(),actor.command_ready(),&font);
        held=buttons;
        actor.SetMood(talk.mood());
        if(auto cmd=talk.actor_command()) Check(actor.Request(*cmd),"Play autumn colour pose");
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        Check(!talk.ordinary_frontier(),"Autumn colours reached an unsupported procedure");
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(pc<0x8121c || pc>=0x812e3) continue;
        lines.push_back(pc);
        if(pc==0x81246 || pc==0x8126e) Check(talk.text().find("11\x8c\x8e")!=std::string::npos,"Wrong spoken month");
        if(pc==0x8128a) {
            const auto& ops=talk.message_word_operands();
            if(empty_food) {
                Check(ops.empty() && talk.text().find("\x82\xc8\x82\xc1\x82\xc6\x82\xa4\x82\xdc\x82\xab")!=std::string::npos,
                      "Missing source food fallback");
            } else {
                Check(ops.size()==1 && ops.front().field==1 && ops.front().local==1,"Missing food spelling");
                const auto& op=ops.front();
                food_row=op.row;
                Check(words.FieldInt(food_row,7)==128 && (words.FieldInt(food_row,12)&6) &&
                      talk.text().substr(op.offset,op.length)==words.FieldText(food_row,1),"Wrong food from D9FF");
            }
        }
    }
    Check(sent && talk.last_answer()==int(answer+1) && lines==std::vector<unsigned>{0x81246,0x8126e,0x8128a,0x812c0}
          && talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected(),"Incomplete autumn colour story");
    const auto command=ResumeActivity(phase,actor,talk,selector,rng,2,minute);
    std::cout<<"8121C month 11, answer "<<answer+1<<", food row "<<food_row<<", return -> ASC "<<command<<"\n";
    return food_row;
}

void StatusStory(gameplay::RoomTalkState talk,const recovered::PackageView& font,bool teach,unsigned story_answer,
                 std::vector<int> people={}) {
    std::optional<unsigned> queued;
    for(unsigned visit=0;visit<3;++visit) {
        talk.vocabulary().BeginConversation();
        Check(talk.BeginOrdinaryTopic(queued.value_or(0x7d43a),queued.has_value()),"Start status conversation");
        bool sent=false,edited=false,answered=false;
        unsigned held=0,last=0;
        int action=-1;
        std::vector<unsigned> lines;
        for(unsigned tick=0;tick<20000 && !talk.finished();++tick) {
            if(auto n=talk.random_requested()) talk.SupplyRandom(*n==1057?106:0);
            if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Status story frontier "+std::to_string(*pc));
            if(talk.word_entry_requested()) {
                Check(teach && !edited && people.size()<2,"Unexpected status-story editor");
                const std::string name=people.empty()?"Aki":"Mio";
                talk.FinishWordEntry(name,name);
                edited=true;
            }
            unsigned buttons=0;
            if(!talk.answer_visible()) sent=false;
            if(talk.answer_interactive() && !sent && !held) {
                const auto pc=talk.message_pc();
                unsigned pick=0;
                if(pc==0x9389 && !teach) pick=1;
                if(pc==0x6dc79) pick=story_answer;
                if(talk.answer_cursor()!=pick) buttons=0x40;
                else { buttons=0x2000; sent=true; if(pc==0x6dc79) answered=true; }
            }
            held=buttons;
            talk.Step(buttons,true,true,&font);
            if(!talk.error().empty()) throw std::runtime_error(talk.error());
            const auto pc=talk.message_pc();
            if(pc==last) continue;
            last=pc;
            if(pc<0x6dc5d || pc>=0x6dd95) continue;
            lines.push_back(pc);
            const auto& ops=talk.message_word_operands();
            if(pc==0x6dc5d) Check(ops.size()==1 && ops[0].local==1,"Missing first person in status story");
            if(pc==0x6dc79) Check(ops.size()==2 && ops[0].local==4 && ops[1].local==5,"Missing second person or action");
            for(const auto& op:ops) {
                std::cout<<"6DB73 pc "<<std::hex<<pc<<std::dec<<" local "<<op.local<<" row "<<op.row<<" field "<<op.field<<" length "<<op.length<<"\n";
                const unsigned field=op.local==5?1:13;
                Check(op.field==field && talk.text().substr(op.offset,op.length)==talk.vocabulary().FieldText(op.row,field),
                      "Status story lost the selected spelling");
                if(op.local==1 || op.local==4)
                    Check(people.size()==2 && op.row==people[op.local==4],"Unexpected person in status story");
                if(op.local==5) {
                    if(action<0) action=op.row;
                    Check(op.row==action && talk.vocabulary().FieldInt(op.row,7)==32,"Status story action changed");
                }
            }
        }
        Check(talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected(),"Status conversation return");
        const auto pending=talk.TakeTopicEnqueues();
        if(edited) {
            Check(pending==std::vector<std::optional<unsigned>>{0x6db73} && talk.learned_word_row() && lines.empty(),
                  "Teaching did not reschedule its status story");
            people.push_back(int(*talk.learned_word_row()));
            queued=pending.front();
            std::cout<<"6DB73 lesson "<<people.size()<<", row "<<people.back()<<", queued\n";
            continue;
        }
        Check(pending.empty() && answered==teach,"Wrong status story finish");
        if(!teach) { Check(lines.empty(),"Declined lesson reached status story"); return; }
        std::vector<unsigned> expected{0x6dc5d,0x6dc79};
        if(story_answer) expected.insert(expected.end(),{0x6dd35,0x6dd52,0x6dd72});
        else expected.insert(expected.end(),{0x6dcb7,0x6dcd2,0x6dcf1,0x6dd0b});
        Check(lines==expected && action>=0 && talk.last_answer()==int(story_answer+1),"Wrong status story reaction");
        std::cout<<"6DB73 people "<<people[0]<<"/"<<people[1]<<", action "<<action
                 <<", answer "<<story_answer+1<<", returned\n";
        return;
    }
    throw std::runtime_error("Missing final status story after two lessons");
}

int MailOrderStory(gameplay::RoomTalkState talk,const recovered::PackageView& font,const std::filesystem::path& game,
                    unsigned answer,int friend_row) {
    gameplay::RoomActivityPhase phase;
    gameplay::RoomActivitySelector selector;
    gameplay::ColdRoomActor actor;
    recovered::RngNativeOwner rng;
    std::string error;
    if(!actor.Load(game,0,2,error) || !selector.Load(game/"assets/game/post-profile",error))
        throw std::runtime_error(error);
    rng.ApplyInitializerTimeT32(168,1,"Mail order component");
    phase.Enter(720,rng);
    phase.ReturnToRoom(720,talk.mood(),rng);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(720),"Mail order entrance");
    auto& words=talk.vocabulary();
    words.BeginConversation();
    Check(actor.talk_ready() && actor.Request(87) && phase.BeginOrdinary() && talk.BeginOrdinaryTopic(0x7d43a),
          "Start mail order conversation");
    std::vector<unsigned> lines;
    unsigned last=0,held=0,draws=0;
    bool sent=false;
    int item=-1;
    for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
        if(auto n=talk.random_requested()) {
            if(*n==1057) ++draws;
            talk.SupplyRandom(*n==1057?330:0);
        }
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Mail order frontier "+std::to_string(*pc));
        Check(!talk.word_entry_requested(),"Existing friend should satisfy the mail order lesson");
        unsigned buttons=0;
        if(talk.answer_interactive() && !sent && !held) {
            Check(talk.message_pc()==0x5a96b && talk.answer_options()==std::vector<std::string>{"\x82\xd6\x82\xa5","\x92\x6d\x82\xc1\x82\xc4\x82\xe9"},
                  "Wrong mail order answers");
            if(talk.answer_cursor()!=answer) buttons=0x40;
            else {buttons=0x2000; sent=true;}
        }
        Check(actor.Advance(720),"Mail order actor advance");
        talk.SetActorField50(int(actor.source_field50()));
        held=buttons;
        talk.Step(buttons,actor.talk_ready(),actor.command_ready(),&font);
        actor.SetMood(talk.mood());
        if(auto command=talk.actor_command()) Check(actor.Request(*command),"Play mail order pose");
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(pc<0x5a94a || pc>=0x5aaa4) continue;
        lines.push_back(pc);
        const auto& ops=talk.message_word_operands();
        if(pc==0x5a94a) {
            Check(ops.size()==2 && ops[0].local==1 && ops[0].row==friend_row && ops[0].field==13 &&
                  ops[1].local==3 && ops[1].field==1,"Wrong mail order friend or purchase");
            item=ops[1].row;
            Check((words.FieldInt(item,7)&29570)!=0,"Purchase outside 9D40");
        }
        if(pc==0x5a9c6 || pc==0x5aa22)
            Check(ops.size()==1 && ops[0].local==3 && ops[0].row==item && ops[0].field==1,"Purchase changed in reply");
        for(const auto& op:ops) Check(talk.text().substr(op.offset,op.length)==words.FieldText(op.row,op.field),
                                      "Mail order word lost its display form");
    }
    std::vector<unsigned> expected{0x5a94a,0x5a96b};
    if(answer) expected.insert(expected.end(),{0x5a9f5,0x5aa03,0x5aa22,0x5aa39});
    else expected.insert(expected.end(),{0x5a9a2,0x5a9c6});
    Check(draws==1 && sent && lines==expected && item>=0 && talk.last_answer()==int(answer+1) &&
          talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected(),"Incomplete mail order story");
    const auto resumed=ResumeActivity(phase,actor,talk,selector,rng,2,720);
    std::cout<<"5A8C7 answer "<<answer+1<<", friend "<<friend_row<<", item "<<item<<", return -> ASC "<<resumed<<"\n";
    return item;
}

std::pair<int,int> RestConversation(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                      const std::filesystem::path& game,unsigned answer) {
    gameplay::RoomActivityPhase phase;
    gameplay::RoomActivitySelector selector;
    gameplay::ColdRoomActor actor;
    recovered::RngNativeOwner rng;
    std::string error;
    if(!actor.Load(game,0,2,error) || !selector.Load(game/"assets/game/post-profile",error))
        throw std::runtime_error(error);
    rng.ApplyInitializerTimeT32(168,1,"Rest conversation component");
    phase.Enter(720,rng);
    phase.ReturnToRoom(720,talk.mood(),rng);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(720),"Rest conversation entrance");
    auto& words=talk.vocabulary();
    words.BeginConversation();
    Check(actor.talk_ready() && actor.Request(87) && phase.BeginOrdinary() && talk.BeginOrdinaryTopic(0x7d43a),
          "Start rest conversation");
    std::vector<unsigned> lines;
    unsigned last=0,held=0,draws=0;
    bool answered=false;
    int activity=-1,part=-1;
    for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
        if(auto n=talk.random_requested()) {
            if(*n==1057) ++draws;
            talk.SupplyRandom(*n==1057?292:0);
        }
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Rest conversation frontier "+std::to_string(*pc));
        unsigned next=0;
        if(talk.answer_interactive() && !answered && !held) {
            Check(talk.message_pc()==0x5db68 && talk.answer_options()==std::vector<std::string>{"\x82\xc7\x82\xa4\x82\xbe\x82\xeb\x82\xa4\x81\x48","\x83\x58\x83\x65\x83\x4c\x85\x40"},
                  "Unexpected rest conversation choices");
            if(talk.answer_cursor()!=answer) next=0x40;
            else {next=0x2000; answered=true;}
        }
        Check(actor.Advance(720),"Rest conversation actor");
        talk.SetActorField50(int(actor.source_field50()));
        held=next;
        talk.Step(next,actor.talk_ready(),actor.command_ready(),&font);
        actor.SetMood(talk.mood());
        if(auto command=talk.actor_command()) Check(actor.Request(*command),"Rest conversation pose");
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(pc<0x5db55 || pc>=0x5dc31) continue;
        lines.push_back(pc);
        const auto& ops=talk.message_word_operands();
        if(pc==0x5db55 || pc==0x5db68 || pc==0x5dc03) {
            Check(ops.size()==1 && ops[0].field==1 && ops[0].local==(pc==0x5db55?0u:1u),"Missing rest topic word");
            const auto& op=ops[0];
            Check(talk.text().substr(op.offset,op.length)==words.FieldText(op.row,1),"Wrong rest topic spelling");
            if(pc==0x5db55) {
                activity=op.row;
                Check((words.FieldInt(activity,7)&48) && (words.FieldInt(activity,12)&6),"Activity outside A0D6");
            } else if(pc==0x5db68) {
                part=op.row;
                Check(words.FieldInt(part,7)==65536,"Body part outside A0E2");
            } else Check(op.row==part,"Rest topic changed body part at the final invitation");
        }
        if(pc==0x5dbb8 || pc==0x5dc03)
            Check(talk.text().find("Toro")!=std::string_view::npos,"Rest topic lost the current nickname");
    }
    Check(answered && draws==1 && talk.last_answer()==int(answer+1) && talk.ordinary_source_return() &&
          !talk.ordinary_rejected() && lines==std::vector<unsigned>{0x5db55,0x5db68,0x5dbb8,0x5dbe1,0x5dc03},
          "Incomplete rest conversation");
    const auto command=ResumeActivity(phase,actor,talk,selector,rng,2,720);
    std::cout<<"5DB1F answer "<<answer+1<<", activity "<<activity<<", body part "<<part<<", return -> ASC "<<command<<"\n";
    return {activity,part};
}

// No person in the player's dictionary (or a source-excluded spelling) offers
// the original lesson prompt. A matching person continues to the two answers.
void NightGhost(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                const std::filesystem::path& game,int person,unsigned answer,bool queued=false) {
    gameplay::RoomActivityPhase phase;
    gameplay::RoomActivitySelector selector;
    gameplay::ColdRoomActor actor;
    recovered::RngNativeOwner rng;
    std::string error;
    if(!actor.Load(game,0,2,error) || !selector.Load(game/"assets/game/post-profile",error))
        throw std::runtime_error(error);
    rng.ApplyInitializerTimeT32(168,1,"Night ghost component");
    phase.Enter(720,rng);
    phase.ReturnToRoom(720,talk.mood(),rng);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(720),"Night ghost entrance");
    talk.vocabulary().BeginConversation();
    Check(actor.talk_ready() && actor.Request(87) && phase.BeginOrdinary() && talk.BeginOrdinaryTopic(queued?0x462b7:0x7d43a,queued),
          "Start night ghost topic");
    bool answered=false;
    unsigned held=0,last=0;
    std::vector<unsigned> lines;
    for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
        if(auto n=talk.random_requested()) talk.SupplyRandom(*n==1057?555:0);
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Night ghost frontier "+std::to_string(*pc));
        unsigned next=0;
        if(talk.answer_interactive() && !answered && !held) {
            Check(talk.message_pc()==(person<0?0x9389:0x46335),"Unexpected ghost question");
            const auto labels=person<0?std::vector<std::string>{"\x82\xa6\x81\x5b\x82\xc1\x82\xc6\x82\xcb","\x82\xa8\x82\xb5\x82\xa6\x82\xc8\x82\xa2","\x82\xa6\x81\x48\x20\x89\xbd\x81\x48"}:
                std::vector<std::string>{"\x8a\xeb\x8c\xaf\x81\x48","\x95\xbd\x8b\x43\x82\xbe\x82\xe6"};
            Check(talk.answer_options()==labels,"Wrong ghost choices");
            if(talk.answer_cursor()!=(person<0?1u:answer)) next=0x40;
            else { next=0x2000; answered=true; }
        }
        Check(actor.Advance(720),"Night ghost actor");
        talk.SetActorField50(int(actor.source_field50()));
        held=next;
        talk.Step(next,actor.talk_ready(),actor.command_ready(),&font);
        actor.SetMood(talk.mood());
        if(auto command=talk.actor_command()) Check(actor.Request(*command),"Night ghost pose");
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(pc<0x4631a || pc>=0x46483) continue;
        lines.push_back(pc);
        if(pc==0x46385 || pc==0x4641b) {
            const auto& ops=talk.message_word_operands();
            Check(ops.size()==1 && ops[0].local==1 && ops[0].row==person &&
                  talk.text().substr(ops[0].offset,ops[0].length)==talk.vocabulary().FieldText(person,ops[0].field),
                  "Ghost did not use the selected person");
        }
    }
    std::vector<unsigned> expected;
    if(person>=0) {
        expected={0x4631a,0x46335};
        if(answer) expected.insert(expected.end(),{0x463f2,0x463ff,0x4641b,0x46445,0x46457});
        else expected.insert(expected.end(),{0x4636b,0x46385,0x463bc});
    }
    Check(answered && talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected() &&
          lines==expected && talk.TakeTopicEnqueues().empty(),"Incomplete night ghost conversation");
    const auto command=ResumeActivity(phase,actor,talk,selector,rng,2,720);
    std::cout<<"462B7 person "<<person<<", answer "<<talk.last_answer()<<", return -> ASC "<<command<<"\n";
}

void SeasonalClothes(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                const std::filesystem::path& game,int garment,int month,unsigned season,unsigned answer,bool queued=false) {
    gameplay::RoomActivityPhase phase;
    gameplay::RoomActivitySelector selector;
    gameplay::ColdRoomActor actor;
    recovered::RngNativeOwner rng;
    std::string error;
    if(!actor.Load(game,0,2,error) || !selector.Load(game/"assets/game/post-profile",error))
        throw std::runtime_error(error);
    rng.ApplyInitializerTimeT32(168,1,"Seasonal clothes component");
    phase.Enter(720,rng);
    phase.ReturnToRoom(720,talk.mood(),rng);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(720),"Seasonal clothes entrance");
    talk.SetCalendarMinute(recovered::CalendarScalar7C660(recovered::CalendarRecord689B8({2026,month,1,12,0})));
    talk.vocabulary().BeginConversation();
    Check(actor.talk_ready() && actor.Request(87) && phase.BeginOrdinary() && talk.BeginOrdinaryTopic(queued?0x348d6:0x7d43a,queued),
          "Start seasonal clothes topic");
    const std::string seasons[]{"\x93\x7e","\x8f\x74","\x89\xc4","\x8f\x48"}; // Winter through autumn.
    const unsigned reactions[][3]={{0x34b1c,0x34b3d,0x34b5d},{0x349ba,0x349dd,0x349fd},
                                   {0x34a36,0x34a57,0x34a77},{0x34ab5,0x34ad8,0x34af8}};
    const auto& branch=reactions[season];
    int part=-1;
    unsigned held=0,last=0;
    bool answered=false;
    std::vector<unsigned> lines;
    for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
        if(auto n=talk.random_requested()) talk.SupplyRandom(*n==1057?753:0);
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Seasonal clothes frontier "+std::to_string(*pc));
        unsigned next=0;
        if(talk.answer_interactive() && !answered && !held) {
            Check(talk.message_pc()==(garment<0?0x9389:0x34966),"Unexpected seasonal clothes question");
            if(garment>=0) {
                const std::string name(talk.vocabulary().FieldText(garment,1));
                Check(talk.answer_options()==std::vector<std::string>{seasons[season]+"\x95\xa8\x82\xcc\x81\x48",
                      "\x16"+name+"\x16\x81\x48"},"Wrong season or item in clothing choices");
            }
            if(talk.answer_cursor()!=(garment<0?1u:answer)) next=0x40;
            else { next=0x2000; answered=true; }
        }
        Check(actor.Advance(720),"Seasonal clothes actor");
        talk.SetActorField50(int(actor.source_field50()));
        held=next;
        talk.Step(next,actor.talk_ready(),actor.command_ready(),&font);
        actor.SetMood(talk.mood());
        if(auto command=talk.actor_command()) Check(actor.Request(*command),"Seasonal clothes pose");
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(pc<0x34966 || pc>=0x34b85) continue;
        lines.push_back(pc);
        if(pc==0x34966 || pc==branch[1] || pc==branch[2]) {
            const auto& ops=talk.message_word_operands();
            Check(ops.size()==1,"Missing seasonal clothes word");
            const auto& op=ops.front();
            if(pc==branch[2]) {
                Check(op.local==4 && op.field==1 && talk.vocabulary().FieldInt(op.row,7)==65536,"Wrong body part");
                part=op.row;
            } else Check(op.local==1 && op.field==1 && op.row==garment,"Wrong item of seasonal clothing");
            Check(talk.text().substr(op.offset,op.length)==talk.vocabulary().FieldText(op.row,op.field),"Wrong seasonal spelling");
        }
    }
    const auto expected=garment<0?std::vector<unsigned>{}:std::vector<unsigned>{0x34966,branch[0],branch[1],branch[2]};
    Check(answered && talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected() &&
          lines==expected && talk.TakeTopicEnqueues().empty(),"Incomplete seasonal clothing conversation");
    const auto command=ResumeActivity(phase,actor,talk,selector,rng,2,720);
    std::cout<<"348D6 month "<<month<<", clothing "<<garment<<", body part "<<part<<", answer "<<talk.last_answer()
             <<", return -> ASC "<<command<<"\n";
}

int Sleepover(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                const std::filesystem::path& game,int person,unsigned answer,bool queued=false) {
    gameplay::RoomActivityPhase phase;
    gameplay::RoomActivitySelector selector;
    gameplay::ColdRoomActor actor;
    recovered::RngNativeOwner rng;
    std::string error;
    if(!actor.Load(game,0,2,error) || !selector.Load(game/"assets/game/post-profile",error))
        throw std::runtime_error(error);
    rng.ApplyInitializerTimeT32(168,1,"Sleepover component");
    phase.Enter(720,rng);
    phase.ReturnToRoom(720,talk.mood(),rng);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(720),"Sleepover entrance");
    talk.vocabulary().BeginConversation();
    Check(actor.talk_ready() && actor.Request(87) && phase.BeginOrdinary() && talk.BeginOrdinaryTopic(queued?0x5f2a5:0x7d43a,queued),
          "Start sleepover topic");
    bool answered=false;
    int activity=-1;
    unsigned held=0,last=0;
    std::vector<unsigned> lines;
    for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
        if(auto n=talk.random_requested()) talk.SupplyRandom(*n==1057?273:0);
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Sleepover frontier "+std::to_string(*pc));
        unsigned next=0;
        if(talk.answer_interactive() && !answered && !held) {
            Check(talk.message_pc()==(person<0?0x9389:0x5f31f),"Unexpected sleepover question");
            if(person>=0) {
                const std::string name(talk.vocabulary().FieldText(person,13));
                const auto labels=std::vector<std::string>{std::string("\x16")+name+"\x16"+"\x82\xcc\x81\x48",
                                                          "\x95\x94\x89\xae\x81\x48"};
                Check(talk.answer_options()==labels,"Missing selected person's name in sleepover answers");
            } else {
                Check(talk.answer_options().size()==3 && talk.text().find("Nana")!=std::string_view::npos,
                      "Missing player's prompted lesson");
            }
            if(talk.answer_cursor()!=(person<0?1u:answer)) next=0x40;
            else { next=0x2000; answered=true; }
        }
        Check(actor.Advance(720),"Sleepover actor");
        talk.SetActorField50(int(actor.source_field50()));
        held=next;
        talk.Step(next,actor.talk_ready(),actor.command_ready(),&font);
        actor.SetMood(talk.mood());
        if(auto command=talk.actor_command()) Check(actor.Request(*command),"Sleepover pose");
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(pc<0x5f31f || pc>=0x5f3ad) continue;
        lines.push_back(pc);
        if(pc==0x5f31f || pc==0x5f372) {
            const auto& ops=talk.message_word_operands();
            Check(ops.size()==1,"Missing sleepover word");
            const auto& op=ops.front();
            const auto& words=talk.vocabulary();
            if(pc==0x5f31f) Check(op.row==person && op.local==1 && op.field==13,"Wrong sleepover person");
            else {
                Check(op.local==3 && op.field==1 && (words.FieldInt(op.row,7)&48) && (words.FieldInt(op.row,12)&6),
                      "Wrong sleepover activity");
                activity=op.row;
            }
            Check(talk.text().substr(op.offset,op.length)==words.FieldText(op.row,op.field),"Wrong sleepover spelling");
        }
    }
    const auto expected=person<0?std::vector<unsigned>{}:std::vector<unsigned>{0x5f31f,0x5f353,0x5f372,0x5f393};
    Check(answered && talk.finished() && talk.ordinary_source_return() && !talk.ordinary_rejected() &&
          lines==expected && talk.TakeTopicEnqueues().empty(),"Incomplete sleepover conversation");
    const auto command=ResumeActivity(phase,actor,talk,selector,rng,2,720);
    std::cout<<"5F2A5 person "<<person<<", activity "<<activity<<", answer "<<talk.last_answer()
             <<", return -> ASC "<<command<<"\n";
    return activity;
}

unsigned TeachPromptedWord(gameplay::RoomTalkState& talk,const recovered::PackageView& font,
                            unsigned slot,unsigned topic,const char* spelling="Sora") {
    talk.vocabulary().BeginConversation();
    Check(talk.BeginOrdinaryTopic(0x7d43a),"Start prompted word lesson");
    unsigned held=0,editors=0;
    bool sent=false;
    for(unsigned tick=0;tick<20000 && !talk.finished();++tick) {
        if(auto n=talk.random_requested()) talk.SupplyRandom(*n==1057?slot:0);
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Word lesson frontier "+std::to_string(*pc));
        if(!talk.answer_visible()) sent=false;
        unsigned next=0;
        if(talk.answer_interactive() && !sent && !held) { next=0x2000; sent=true; }
        held=next;
        talk.Step(next,true,true,&font);
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        if(talk.word_entry_requested()) {
            Check(++editors==1,"Repeated word lesson keyboard");
            talk.FinishWordEntry(spelling,spelling);
        }
    }
    const auto learned=talk.learned_word_row();
    Check(talk.finished() && talk.ordinary_source_return() && editors==1 && learned &&
          talk.vocabulary().FieldText(*learned,1)==spelling &&
          talk.TakeTopicEnqueues()==std::vector<std::optional<unsigned>>{topic},"Word lesson did not reschedule");
    return *learned;
}

// Take the state immediately after the free name lesson; the player's second word is an ingredient.
void TeachMailOrderItem(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                       const std::filesystem::path& game,int friend_row) {
    const std::string written="\x8d\xd8",reading="\x82\xc8"; // 菜 / な
    std::vector<unsigned> messages,answers;
    Check(talk.BeginTeaching(),"Start free purchase lesson");
    FinishTeaching(talk,font,written,reading,{8,1,2},messages,answers);
    const auto item=talk.learned_word_row();
    auto words=talk.vocabulary();
    Check(item && int(*item)!=friend_row && talk.ordinary_source_return() && words.FieldText(*item,1)==written &&
          words.FieldText(*item,2)==reading && words.FieldInt(*item,7)==128 && words.FieldInt(*item,8)==4,
          "Ingredient was not independently registered");
    words.BeginConversation();
    const auto friends=words.Candidates(0x9d33,0,true);
    Check(friends==1 && words.FinishLookup(0)==friend_row,"Unexpected friend draw after both free lessons");
    const auto purchases=words.Candidates(0x9d40,0,true);
    Check(purchases && words.FinishLookup(0)==int(*item),"Ingredient missing at the observed purchase draw");
    std::cout<<"9D33 candidates "<<friends<<"; 9D40 candidates "<<purchases<<", taught row "<<*item<<"\n";
    for(unsigned answer:{0u,1u})
        Check(MailOrderStory(talk,font,game,answer,friend_row)==int(*item),
              "5A8C7 did not use the taught ingredient");
    for(unsigned answer:{0u,1u}) RestConversation(talk,font,game,answer);
}

// Exercise a recorded room contact's two questions and both source fall-throughs.
int TimeFlies(gameplay::RoomTalkState talk,const recovered::PackageView& font,
               const std::filesystem::path& game,int hour,int minute,unsigned clock_answer,unsigned activity_answer) {
    gameplay::RoomActivityPhase phase;
    gameplay::RoomActivitySelector selector;
    gameplay::ColdRoomActor actor;
    recovered::RngNativeOwner rng;
    std::string error;
    if(!actor.Load(game,0,2,error) || !selector.Load(game/"assets/game/post-profile",error))
        throw std::runtime_error(error);
    rng.ApplyInitializerTimeT32(168,1,"Time flies component");
    phase.Enter(720,rng);
    phase.ReturnToRoom(720,talk.mood(),rng);
    for(unsigned i=0;i<3000 && !actor.talk_ready();++i) Check(actor.Advance(720),"Time flies entrance");
    const auto stamp=recovered::CalendarScalar7C660(recovered::CalendarRecord689B8({2026,11,19,hour,minute}));
    talk.SetCalendarMinute(stamp);
    auto& words=talk.vocabulary();
    words.BeginConversation();
    Check(actor.talk_ready() && actor.Request(87) && phase.BeginOrdinary() && talk.BeginOrdinaryTopic(0x7d43a),
          "Start time flies conversation");
    unsigned held=0,last=0,answers=0;
    int part=-1;
    bool sent=false;
    std::vector<unsigned> lines;
    for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
        if(auto bound=talk.random_requested()) talk.SupplyRandom(*bound==1057?4:0);
        if(auto pc=talk.ordinary_frontier()) throw std::runtime_error("Time flies frontier "+std::to_string(*pc));
        unsigned next=0;
        if(!talk.answer_visible()) sent=false;
        if(talk.answer_interactive() && !sent && !held) {
            const auto pc=talk.message_pc();
            Check((answers==0 && pc==0x756f9) || (answers==1 && pc==0x778ae),"Unexpected time flies question");
            const auto options=answers==0?std::vector<std::string>{"\x82\xa6\x81\x5b\x82\xc1\x82\xc6\x81\x63",
                std::string("\x16")+std::to_string(hour)+"\x16"+"\x8e\x9e"+"\x16"+std::to_string(minute)+"\x16"+"\x95\xaa\x82\xbe\x82\xe6"}:std::vector<std::string>{"\x8a\x79\x82\xb5\x82\xa2\x83\x52\x83\x67\x81\x48","\x89\xbd\x82\xb5\x82\xc4\x82\xbd\x82\xcc\x81\x48"};
            Check(talk.answer_options()==options,"Time flies answer labels or captured time changed");
            const auto choice=answers==0?clock_answer:activity_answer;
            if(talk.answer_cursor()!=choice) next=0x40;
            else {next=0x2000;sent=true;++answers;}
        }
        Check(actor.Advance(720),"Time flies actor");
        talk.SetActorField50(int(actor.source_field50()));
        held=next;
        talk.Step(next,actor.talk_ready(),actor.command_ready(),&font);
        actor.SetMood(talk.mood());
        if(auto command=talk.actor_command()) Check(actor.Request(*command),"Time flies pose");
        if(!talk.error().empty()) throw std::runtime_error(talk.error());
        const auto pc=talk.message_pc();
        if(pc==last) continue;
        last=pc;
        if(pc==0x756f9) talk.SetCalendarMinute(stamp+60); // Both later strings must use the sampled local array.
        else if(pc<0x7787a || pc>=0x77920) continue;
        lines.push_back(pc);
        if(pc==0x7787a) Check(talk.text().find(std::to_string(hour)+"\x8e\x9e")!=std::string_view::npos,"Changed hour after ASK");
        if(pc==0x778f0) {
            const auto& ops=talk.message_word_operands();
            Check(ops.size()==1 && ops[0].field==1 && ops[0].local==0 && words.FieldInt(ops[0].row,7)==65536 &&
                  talk.text().substr(ops[0].offset,ops[0].length)==words.FieldText(ops[0].row,1) &&
                  talk.text().find("Nana")!=std::string_view::npos,"Missing body part or player in last line");
            part=ops[0].row;
        }
    }
    Check(answers==2 && talk.last_answer()==int(activity_answer+1) && talk.ordinary_source_return() &&
          !talk.ordinary_rejected() && lines==std::vector<unsigned>{0x756f9,0x7787a,0x77896,0x778ae,0x778dd,0x778f0},
          "Time flies did not return through both questions");
    const auto command=ResumeActivity(phase,actor,talk,selector,rng,2,720);
    std::cout<<"7783F hour "<<hour<<", minute "<<minute<<", answers "<<clock_answer+1<<"/"<<activity_answer+1
             <<", return -> ASC "<<command<<"\n";
    return part;
}

unsigned TeachFreeAction(gameplay::RoomTalkState& talk,const recovered::PackageView& font) {
    const std::string written="\x8b\x83\x82\xab",reading="\x82\xc8\x82\xab"; // 泣き / なき
    std::vector<unsigned> messages,answers;
    Check(talk.BeginTeaching(),"Start free action lesson");
    FinishTeaching(talk,font,written,reading,{6,1,3,5},messages,answers);
    const auto row=talk.learned_word_row();
    const auto& words=talk.vocabulary();
    Check(talk.finished() && talk.ordinary_source_return() && talk.counted_contact() && row &&
          words.FieldText(*row,1)==written && words.FieldText(*row,2)==reading,
          "Free action lesson did not register the entered word");
    Check(words.FieldInt(*row,7)==32 && words.FieldInt(*row,8)==(16|32|1),
          "Wrong direction or subtype after choosing both and none of these");
    const std::vector<unsigned> questions{0x780c,0x5b48,0x5978,0x5a0c};
    Check(answers.size()>=questions.size() && std::equal(questions.begin(),questions.end(),answers.begin()),
          "Wrong action question sequence");
    std::cout<<"Free action lesson, row "<<*row<<", category 32, direction and subtype bits 49, source return\n";
    return *row;
}

void FreePersonLesson(gameplay::RoomTalkState talk,const recovered::PackageView& font,const std::filesystem::path& game) {
    const std::string written="\x89\x9b",reading="\x82\xc8\x82\xa9";
    std::vector<unsigned> messages,answers;
    Check(talk.BeginTeaching(),"Start free person lesson");
    FinishTeaching(talk,font,written,reading,{2,1,3,1,1},messages,answers);
    const auto row=talk.learned_word_row();
    Check(row && talk.finished() && talk.ordinary_source_return(),"Free person lesson did not return");
    auto& words=talk.vocabulary();
    std::cout<<"Free person asks";
    for(auto pc:answers) std::cout<<" "<<std::hex<<pc;
    std::cout<<std::dec<<"; row "<<*row<<", fields 0/7/8/9/11 "<<words.FieldInt(*row,0)<<"/"
             <<words.FieldInt(*row,7)<<"/"<<words.FieldInt(*row,8)<<"/"<<words.FieldInt(*row,9)<<"/"
             <<words.FieldInt(*row,11)<<"\n";
    Check(words.FieldText(*row,1)==written && words.FieldText(*row,2)==reading &&
          words.FieldInt(*row,7)==4 && (words.FieldInt(*row,8)&16),"Friend lesson lost person fields");
    const std::vector<unsigned> expected{0x780c,0x42e7,0x51d2,0x7436,0x43ee,0x7755};
    Check(answers.size()>=expected.size() && std::equal(expected.begin(),expected.end(),answers.begin()),
          "Unexpected free person questions");
    for(unsigned answer:{0u,1u}) NightGhost(talk,font,game,int(*row),answer);
    // Isolate 7DC5 from 7DC3: keep the eligible row and change only its spelling.
    for(const char* spelling : {"\x82\xa8\x82\xce\x82\xaf","\x83\x49\x83\x6f\x83\x50","\x82\xa8\x89\xbb\x82\xaf"}) {
        auto excluded=talk;
        excluded.vocabulary().WriteText(*row,1,spelling);
        NightGhost(excluded,font,game,-1,1);
    }
    // Identical readings on an otherwise eligible person. Only the spelling
    // changes between these two runs of the actual topic.
    auto homophone=talk;
    const std::string ghost_reading="\x82\xa8\x82\xce\x82\xaf"; // おばけ
    homophone.vocabulary().WriteText(*row,2,ghost_reading);
    for(bool listed:{false,true}) {
        auto& dictionary=homophone.vocabulary();
        const auto& spelling=listed?ghost_reading:written;
        dictionary.WriteText(*row,1,spelling);
        Check(dictionary.FieldText(*row,2)==ghost_reading && dictionary.FieldText(*row,1)==spelling,
              "Ghost exclusion pair lost its reading or spelling");
        NightGhost(homophone,font,game,listed?-1:int(*row),0);
        std::cout<<"462B7 exclusion: same reading, listed spelling "<<listed<<", row "<<*row<<"\n";
    }
    Check(recovered::CalendarBcdTime6912C(1440)==0 && recovered::CalendarBcdTime6912C(-1)==0x0fffffff &&
          recovered::CalendarBcdTime6912C(-60)==0x0fffff00,"Signed script time or day rollover changed");
    for(unsigned clock:{0u,1u}) for(unsigned activity:{0u,1u})
        TimeFlies(talk,font,game,clock?23:4,clock?59:7,clock,activity);
    for(unsigned answer:{0u,1u}) Sleepover(talk,font,game,int(*row),answer);
    auto sleepover=talk;
    const auto activity=TeachFreeAction(sleepover,font);
    Check(activity!=*row && Sleepover(sleepover,font,game,int(*row),0)==int(activity),
          "Sleepover did not use both free lessons");
    std::cout<<"5F2A5 free lessons, person "<<*row<<", activity "<<activity<<"\n";
    TeachMailOrderItem(talk,font,game,int(*row));
    auto single=words;
    single.BeginConversation();
    const auto high=single.Candidates(0xba4d,0xba58,true);
    Check(high==1 && single.FinishLookup(0)==int(*row),"6DB73 failed to select the learned person");
    const auto low=single.Candidates(0xba75,0,true);
    Check(!low && single.FinishLookup(0)==-1,"6DB73 reused the first person for the second role");
    std::cout<<"6DB73 higher lookup "<<high<<", lower lookup "<<low<<"\n";
    messages.clear();
    answers.clear();
    const std::string lower_name="\x93\xde", lower_reading="\x82\xc8";
    Check(talk.BeginTeaching(),"Start second free person lesson");
    FinishTeaching(talk,font,lower_name,lower_reading,{2,1,3,1,4},messages,answers);
    const auto lower=talk.learned_word_row();
    Check(lower && lower!=row && talk.ordinary_source_return() && words.FieldText(*lower,1)==lower_name &&
          words.FieldText(*lower,2)==lower_reading && (words.FieldInt(*lower,9)&(64|128)) &&
          words.FieldText(*row,1)==written,"Lower person was not registered independently");
    std::cout<<"6DB73 free people fields 9 "<<words.FieldInt(*row,9)<<"/"<<words.FieldInt(*lower,9)<<"\n";
    auto candidates=words;
    candidates.BeginConversation();
    Check(candidates.Candidates(0xba4d,0,false)==2,"Both named friends should match the primary person filter");
    candidates.FinishLookup(0);
    Check(candidates.Candidates(0xba4d,0xba58,true)==1 && candidates.FinishLookup(0)==int(*row),
          "Preferred position did not select the higher friend");
    Check(candidates.Candidates(0xba75,0,true)==1 && candidates.FinishLookup(0)==int(*lower),
          "Lower position did not select the second friend");
    for(unsigned answer:{0u,1u}) StatusStory(talk,font,true,answer,{int(*row),int(*lower)});
    for(unsigned answer:{0u,1u}) MailOrderStory(talk,font,game,answer,int(*row));
}

void FreeFoodLesson(gameplay::RoomTalkState talk,const recovered::PackageView& font,const std::filesystem::path& game,
                    unsigned subtype) {
    const std::string written="\x8d\xd8",reading="\x82\xc8"; // 菜 / な
    std::vector<unsigned> messages,answers;
    Check(talk.BeginTeaching(),"Start free food lesson");
    FinishTeaching(talk,font,written,reading,{8,1,subtype},messages,answers);
    Check(answers.size()>=3 && answers[0]==0x780c && answers[1]==0x612c && answers[2]==0x608f,
          "Missing category, food/drink or food subtype question");
    const auto row=talk.learned_word_row();
    const auto& words=talk.vocabulary();
    Check(talk.finished() && talk.ordinary_source_return() && row && words.FieldText(*row,1)==written &&
          words.FieldText(*row,2)==reading && words.FieldInt(*row,7)==128 &&
          words.FieldInt(*row,8)==std::array<int,3>{2,4,1}.at(subtype-1),"Food registration");
    std::cout<<"Free food row "<<*row<<", fields 0/7/11/12 "<<words.FieldInt(*row,0)<<"/"
             <<words.FieldInt(*row,7)<<"/"<<words.FieldInt(*row,11)<<"/"<<words.FieldInt(*row,12)<<"\n";
    Check(AutumnColours(talk,font,game,0,false,true)==int(*row),"D9FF did not select the eligible taught food");
    if(subtype==2) {
        StatusStory(talk,font,false,0);
        for(unsigned answer:{0u,1u}) StatusStory(talk,font,true,answer);
    }
}

void FreePlaceLesson(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                     const std::filesystem::path& game,bool school) {
    const std::string written=school?"\x92\x86":"\x92\x87",reading="\x82\xc8\x82\xa9"; // 中 or 仲 / なか
    std::vector<unsigned> messages,answers;
    Check(talk.BeginTeaching(),"Start free place lesson");
    FinishTeaching(talk,font,written,reading,{7,school?2u:3u,1},messages,answers);
    const std::vector<unsigned> place_questions{0x780c,0x5f89,school?0x5c90u:0x5d90u};
    Check(answers.size()>=place_questions.size() && std::equal(place_questions.begin(),place_questions.end(),answers.begin()),
          "Wrong category, place type or shop subtype question");
    const auto& words=talk.vocabulary();
    const auto row=talk.learned_word_row();
    Check(talk.finished() && talk.ordinary_source_return() && talk.counted_contact() && row &&
          words.FieldInt(*row,7)==64 && words.FieldInt(*row,8)==(school?0x5000:6) &&
          words.FieldText(*row,1)==written && words.FieldText(*row,2)==reading,
          "Free place lesson did not register the spelling, reading and subtype");
    std::cout<<"Free "<<(school?"school":"restaurant")<<" lesson row "<<*row<<", category 64, subtype "
             <<words.FieldInt(*row,8)<<", returned\n";
    for(unsigned answer:{0u,1u}) NightAtLearnedPlace(talk,font,game,answer);
    if(school)
        for(unsigned answer:{0u,1u})
            for(bool empty_food:{false,true}) AutumnColours(talk,font,game,answer,empty_food);
}

void SoloActionLesson(gameplay::RoomTalkState talk,const recovered::PackageView& font,const std::filesystem::path& game) {
    const std::string written="\x8b\x83\x82\xab",reading="\x82\xc8\x82\xab";
    std::vector<unsigned> messages,answers;
    Check(talk.BeginTeaching(),"Start lesson for an action not done to others");
    FinishTeaching(talk,font,written,reading,{6,2,1,1},messages,answers);
    const std::vector<unsigned> prefix{0x780c,0x5b48,0x5734,0x57d4};
    Check(answers.size()>prefix.size() && std::equal(prefix.begin(),prefix.end(),answers.begin()),
          "Wrong questions after selecting an action not done to others");
    const auto row=talk.learned_word_row();
    const auto& words=talk.vocabulary();
    Check(talk.finished() && talk.ordinary_source_return() && talk.counted_contact() && row &&
          words.FieldInt(*row,7)==16 && words.FieldInt(*row,8)==(1|32) &&
          words.FieldText(*row,1)==written && words.FieldText(*row,2)==reading,
          "Wrong saved category, subtype, group size or word");
    std::cout<<"Solo action row "<<*row<<", category 16, subtype bits 33, returned\n";
    for(unsigned answer:{0u,1u}) IntroduceFriends(talk,font,game,answer);
    PracticeAtPlace(talk,font,game,false,0,0);
    for(unsigned type=0;type<6;++type) PracticeAtPlace(talk,font,game,true,type,type%2);
    for(unsigned month:{0u,1u})
        for(unsigned autumn:{0u,1u}) AutumnVoice(talk,font,game,month,autumn);
    for(unsigned greeting:{0u,1u}) {
        for(unsigned activity:{0u,1u}) MorningConversation(talk,font,game,greeting,activity);
        MorningConversation(talk,font,game,greeting,std::nullopt);
    }
    gameplay::RoomWordEntry keyboard;
    std::string error;
    if(!keyboard.Load(game/"assets/game/title",error)) throw std::runtime_error(error);
    ReadingQuestion(talk,font,written,reading,ReadingAnswer::append_kana,&keyboard);
}

void TeachBodyPart(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                   const std::filesystem::path& game,int activity) {
    const std::string name="\x82\xcd\x82\xc8"; // はな
    std::vector<unsigned> messages,answers;
    Check(talk.BeginTeaching(),"Open free body part lesson");
    FinishTeaching(talk,font,name,name,{17},messages,answers);
    auto words=talk.vocabulary();
    const auto part=talk.learned_word_row();
    Check(part && int(*part)!=activity && talk.ordinary_source_return() && words.FieldText(*part,1)==name &&
          words.FieldText(*part,2)==name && words.FieldInt(*part,7)==65536,"Incorrect free body part registration");
    words.BeginConversation();
    const auto actions=words.Candidates(0xa0d6,0,true);
    Check(actions && words.FinishLookup(0)==activity,"Taught activity missing at component draw");
    const auto parts=words.Candidates(0xa0e2,0,true);
    Check(parts && words.FinishLookup(0)==int(*part),"Taught body part missing at component draw");
    std::cout<<"5DB1F free candidates "<<actions<<"/"<<parts<<", rows "<<activity<<"/"<<*part<<"\n";
    for(unsigned answer:{0u,1u})
        Check(RestConversation(talk,font,game,answer)==std::make_pair(activity,int(*part)),
              "Rest conversation lost a taught word");
    const int shown=TimeFlies(talk,font,game,12,34,1,0);
    Check(shown==int(*part),"Time flies did not display the taught body part");
    std::cout<<"7783F taught body part, row "<<shown<<"\n";
    NightGhost(talk,font,game,-1,1);
    SeasonalClothes(talk,font,game,-1,11,3,1);
    auto prompted=talk;
    const int garment=TeachPromptedWord(prompted,font,753,0x348d6,"Scarf");
    for(unsigned season=0;season<4;++season)
        SeasonalClothes(prompted,font,game,garment,season==0?12:int(season)*3,season,season%2,true);
    // Each source exclusion must reject even an otherwise matching item.
    for(const char* pocket:{"\x82\xdb\x82\xaf\x82\xc1\x82\xc6","\x83\x7c\x83\x50\x83\x62\x83\x67"}) {
        auto excluded=prompted;
        excluded.vocabulary().WriteText(garment,1,pocket);
        SeasonalClothes(excluded,font,game,-1,11,3,1,true);
    }
    auto ghost=talk;
    const auto ghost_person=TeachPromptedWord(ghost,font,555,0x462b7);
    NightGhost(ghost,font,game,int(ghost_person),0,true);
    Sleepover(talk,font,game,-1,1);
    const auto person=TeachPromptedWord(talk,font,273,0x5f2a5);
    for(unsigned answer:{0u,1u}) Sleepover(talk,font,game,int(person),answer,true);
}

void FreeSeasonalClothing(gameplay::RoomTalkState talk,const recovered::PackageView& font,
                          const std::filesystem::path& game) {
    const std::string hakama="\x82\xcd\x82\xa9\x82\xdc";
    std::vector<unsigned> lines,answers;
    Check(talk.BeginTeaching(),"Start hakama lesson");
    FinishTeaching(talk,font,hakama,hakama,{14},lines,answers);
    const auto row=talk.learned_word_row();
    const auto& words=talk.vocabulary();
    Check(row && talk.ordinary_source_return() && words.FieldText(*row,1)==hakama &&
          words.FieldText(*row,2)==hakama && (words.FieldInt(*row,0)&36)==36 &&
          !(words.FieldInt(*row,11)&2) && words.FieldInt(*row,7)==8192 && (words.FieldInt(*row,8)&8),
          "Hakama was not registered as eligible clothing");
    std::cout<<"348D6 free hakama, row "<<*row<<", fields 14/15 "<<words.FieldInt(*row,14)
             <<"/"<<words.FieldInt(*row,15)<<"\n";
    SeasonalClothes(talk,font,game,int(*row),11,3,1);
}

void FreeActionLesson(gameplay::RoomTalkState talk,const recovered::PackageView& font,const std::filesystem::path& game) {
    const auto row=TeachFreeAction(talk,font);
    TeachBodyPart(talk,font,game,int(row));
    GarmentConversation(talk,font,game,0,150);
    GarmentConversation(talk,font,game,1,150);
}

void DuplicateLesson(const std::filesystem::path& dir,const recovered::PackageView& font,bool change_reading) {
    gameplay::AcceptedProfile profile{};
    profile.nickname.Assign("Toro"); profile.player.Assign("Nana");
    gameplay::RoomTalkState talk;
    std::string error;
    if(!talk.Load(dir,profile,error)) throw std::runtime_error(error);
    talk.Step(0,true,true,&font,true);
    for(unsigned tick=0;tick<8000 && !talk.finished();++tick) {
        if(talk.gift_requested()) talk.SupplyGift(-1);
        talk.Step(0,true,true,&font);
    }
    const std::string kanji="\x90\xb6",nama="\x82\xc8\x82\xdc";
    if(change_reading) {
        const auto game=dir.parent_path().parent_path().parent_path();
        OutingWithLearnedWord(talk,font,game);
        FreeSeasonalClothing(talk,font,game);
        FreeActionLesson(talk,font,game);
        SoloActionLesson(talk,font,game);
        for(bool school:{false,true}) FreePlaceLesson(talk,font,game,school);
        FreePersonLesson(talk,font,game);
        for(unsigned subtype:{1u,2u,3u}) FreeFoodLesson(talk,font,game,subtype);
        CrowdedPlace(talk,font,game,false,0);
        CrowdedPlace(talk,font,game,true,0);
        CrowdedPlace(talk,font,game,true,1);
        RelearnAfterDeletion(talk,font);
        ImportantBelonging(talk,font,true,0);
        ImportantBelonging(talk,font,true,1);
        ImportantBelonging(talk,font,false,0);
    }
    std::vector<unsigned> messages,answers;
    Check(talk.BeginTeaching(),"Start initial word lesson");
    FinishTeaching(talk,font,kanji,nama,{5,1,3,2,1,1},messages,answers);
    Check(talk.learned_word_row()==163 && talk.learned_word_serial()==1 && talk.counted_contact(),"Missing accepted first word");
    if(change_reading) {
        ReadingQuestion(talk,font,kanji,nama,ReadingAnswer::cancel_edit);
        ReadingQuestion(talk,font,kanji,nama,ReadingAnswer::retry_edit);
        PromptedLookup(talk,font,PromptedCase::robot_refuses_lesson);
        PromptedLookup(talk,font,PromptedCase::new_lesson);
    } else {
        ReadingQuestion(talk,font,kanji,nama,ReadingAnswer::confirm);
        ReadingQuestion(talk,font,kanji,nama,ReadingAnswer::defer);
        PromptedLookup(talk,font,PromptedCase::eligible_word);
        PromptedLookup(talk,font,PromptedCase::starter_body_part);
        PromptedLookup(talk,font,PromptedCase::known_television);
        PromptedLookup(talk,font,PromptedCase::missing_television);
        PromptedLookup(talk,font,PromptedCase::known_friend);
        PromptedLookup(talk,font,PromptedCase::starter_occupation);
        for(unsigned rank=0; rank<4; ++rank)
            for(unsigned answer=0; answer<2; ++answer) PainPhrase(talk,font,rank,answer);
        RelatedRoomQuestion(talk,font,true);
        RelatedRoomQuestion(talk,font,false);
        CheckFaceQuestions(dir,font,talk);
        DrinkLessonAndContacts(talk,font);
        FutureRobotQuestions(talk,font);
        for(unsigned row=146;row<=162;++row) PrankCall(talk,font,row,row%2,1);
        PrankCall(talk,font,{},0,1);
        PrankCall(talk,font,{},1,2); // 2151 still refreshes CT2872 when the mood is already 2.
        MagazineDates(talk,font);
        PromptedLookup(talk,font,PromptedCase::new_drink);
        PromptedLookup(talk,font,PromptedCase::refuse_clothing);
        PromptedLookup(talk,font,PromptedCase::new_clothing);
        FreeClothingLesson(talk,font);
        QuarrelQuestions(talk,font);
    }
    auto& vocabulary=talk.vocabulary();
    const int relation=vocabulary.FieldInt(163,12);
    messages.clear(); answers.clear();
    Check(talk.BeginTeaching(),"Start existing word lesson");
    const unsigned answer=change_reading?2u:1u;
    FinishTeaching(talk,font,kanji,change_reading?"\x82\xb9\x82\xa2":nama,{answer},messages,answers,!change_reading);
    Check(messages==std::vector<unsigned>({0x3e2d,0x3fcf,0x3fee,0xabe9,answer==1?0xa86au:0xa88bu,0x41aa,0x7d5fa}) &&
          answers==std::vector<unsigned>({0xabe9}) && talk.last_answer()==int(answer),
          "Wrong repeated robot word acknowledgment, trait question or return");
    const int trait_mask=change_reading?1:(1<<21);
    Check(relation==0x42 && vocabulary.FieldInt(163,12)==relation && !talk.counted_contact() &&
          bool(vocabulary.FieldInt(163,14)&trait_mask)==(answer==1) && (vocabulary.FieldInt(163,15)&trait_mask),
          "Duplicate lesson did not store the selected trait on the existing word");
    Check(talk.finished() && talk.ordinary_source_return() && talk.learned_word_serial()==1 &&
          vocabulary.FindWritten(kanji)==163 && vocabulary.FieldText(163,2)==nama && !vocabulary.HasField(164,0),
          "Existing spelling changed the learned row or registered another word");
    // Same reading, different written form: the 0F02 lookup misses and the
    // word goes through the normal new-word classification at 3D05.
    messages.clear(); answers.clear();
    const std::string second_spelling=kanji+kanji;
    Check(talk.BeginTeaching(),"Start distinct homophone lesson");
    FinishTeaching(talk,font,second_spelling,nama,{5,1,3,2},messages,answers);
    Check(talk.counted_contact() && talk.learned_word_serial()==2 &&
          talk.learned_word_row()==164 && vocabulary.FindWritten(kanji)==163 &&
          vocabulary.FieldText(163,2)==nama && vocabulary.FieldText(164,2)==nama &&
          vocabulary.FieldText(164,1)==second_spelling && answers.size()==4,
          "Reading equality incorrectly replaced an existing word");

}


void CheckOutingCommands(const std::vector<unsigned>& actual,const std::vector<unsigned>& expected,
                         const char* direction) {
    if(actual==expected) return;
    auto sequence=[](const std::vector<unsigned>& commands) {
        std::string values;
        for(auto command:commands) values+=" "+std::to_string(command);
        return values;
    };
    throw std::runtime_error(std::string(direction)+" ASC expected"+sequence(expected)+", observed"+sequence(actual));
}

void CheckOutingSceneWait(gameplay::RoomTalkState& talk,unsigned selector,const recovered::PackageView& font) {
    // A component supplies the scene owner's completion. Neither game time,
    // an actor-ready level, buttons nor another scene's completion is that receipt.
    const auto& sites=talk.outing_sites();
    const auto request=std::find(sites.begin(),sites.end(),0x1269f);
    Check(request!=sites.end() && std::find(request,sites.end(),0x1c64)!=sites.end(),
          "GAME12 must be followed by registration of the source completion callback");
    Check(!talk.FinishOutingScene(selector==12?0:12),"Different scene acknowledged the outing");
    Check(!talk.BeginOutingReturn(),"Return started before the pending scene completed");
    for(unsigned i=0;i<120;++i) {
        talk.Step(i%2?0x2000:0,true,true,&font);
        Check(talk.outing_scene_request()==selector && !talk.actor_command() && !talk.finished() &&
              !talk.ordinary_source_return() && talk.outing_sites().empty(),"Pending scene advanced the outing script");
    }
    Check(talk.FinishOutingScene(selector) && !talk.FinishOutingScene(selector),
          "Missing or repeated outing scene receipt");
}

void CheckOutingActorWait(gameplay::RoomTalkState& talk,const recovered::PackageView& font) {
    for(unsigned i=0;i<20;++i) {
        talk.Step(0,true,false,&font);
        Check(!talk.actor_command() && !talk.ordinary_frontier() && !talk.finished() &&
              !talk.outing_scene_request(),"Outing advanced before the actor callback was ready");
    }
}

void HomeOuting(const std::filesystem::path& dir,const recovered::PackageView& font) {
    gameplay::AcceptedProfile profile{};
    profile.nickname.Assign("Toro");
    profile.player.Assign("Nana");
    gameplay::RoomTalkState talk;
    std::string error;
    Check(talk.Load(dir,profile,error),error.c_str());
    talk.Step(0,true,true,&font,true);
    for(unsigned i=0;i<8000 && !talk.finished();++i) {
        if(talk.gift_requested()) talk.SupplyGift(-1);
        talk.Step(0,true,true,&font);
    }
    Check(talk.finished(),"Missing outing introduction");
    {
        // Independent projection of G59DA for the cold G59AE bit 12 at period 0.
        const unsigned locked[]={1,2,7,8,13,14,20,21,26,27,32,33,44,45,51,52,57,58,63,64,75,76,100,101,106,107};
        const unsigned unlocked[]={69,70,71,72,73};
        const unsigned late[]={7,8,13,14,20,21,32,33,38,39,44,45,51,52,63,64,100,101};
        recovered::RngNativeOwner rng;
        rng.ApplyInitializerTimeT32(168,1,"home invitation selector component");
        auto expected_rng=rng;
        for(unsigned i=0;i<52;++i) {
            const unsigned mode=i%3==1,period=i%3==2?5:0;
            const auto draw=expected_rng.NextC3234();
            const unsigned id=mode?unlocked[draw%std::size(unlocked)] :
                (period?late[draw%std::size(late)]:locked[draw%std::size(locked)]);
            Check(talk.SelectHomeInvitation(mode,period,rng)==id &&
                  rng.Snapshot().seed==expected_rng.Snapshot().seed,"12F39 source-mode selection/draw");
        }
        const auto flags=talk.ProgressFlagWords();
        auto altered=flags;
        altered[1].word=0x20000000;
        talk.RestoreContactHistory(talk.ContactHistoryTables(),altered);
        Check(!talk.SelectHomeInvitation(1,0,rng) && !talk.BeginHomeInvitation() &&
              rng.Snapshot().seed==expected_rng.Snapshot().seed,"Empty enabled offer consumed RNG");
        altered[1].word=0x2fffffff;
        talk.RestoreContactHistory(talk.ContactHistoryTables(),altered);
        Check(!talk.SelectHomeInvitation(0,0,rng) && !talk.BeginHomeInvitation() &&
              rng.Snapshot().seed==expected_rng.Snapshot().seed,"Empty disabled offer consumed RNG");
        talk.RestoreContactHistory(talk.ContactHistoryTables(),flags);
        // Seed zero selects the first numerical row in the explicit component case.
        // A cold dictionary has no learned field-0 mask 32 word for CT1C04.
        gameplay::RoomTalkState offer=talk;
        unsigned returns=0,editors=0;
        bool saw_question=false;
        offer.BindHomeInvitationReturn([&] { ++returns; });
        rng.ApplyInitializerTimeT32(0,2,"component first outing invitation");
        Check(offer.SelectHomeInvitation(0,0,rng)==1 && offer.BeginHomeInvitation() &&
              !offer.BeginHomeInvitation(),"Begin selected home invitation");
        for(unsigned tick=0;tick<16000 && !offer.finished();++tick) {
            if(offer.random_requested()) offer.SupplyRandom(0);
            if(offer.message_pc()==0x9389 && offer.message_revealed()) saw_question=!offer.text().empty();
            if(offer.word_entry_requested()) {
                ++editors;
                offer.FinishWordEntry("","");
            }
            const unsigned keys=tick%3==1 && (offer.answer_interactive() || offer.message_revealed())?0x2000:0;
            offer.Step(keys,true,true,&font);
            if(!offer.error().empty()) throw std::runtime_error("12F39 invitation: "+offer.error());
            if(offer.ordinary_frontier()) throw std::runtime_error("12F39 invitation frontier "+std::to_string(*offer.ordinary_frontier()));
        }
        Check(saw_question && editors==1 && returns==1 && offer.ordinary_source_return() &&
              !offer.ordinary_rejected() && !offer.outing_descriptor(),"Home offer question, cancelled editor or farewell");
    }
    const std::string roof="\x89\xae\x8d\xaa\x82\xcc\x8f\xe3", cancel="\x82\xe2\x82\xc1\x82\xcf\x83\x84\x83\x81";
    recovered::RngNativeOwner outing_rng;
    outing_rng.ApplyInitializerTimeT32(168,1,"Outing shuffle component");
    for(unsigned route=0;route<=6;++route) {
        const bool depart=route==6;
        const unsigned period=route%6;
        Check(talk.BeginOuting(period),"Outing did not start");
        Check(!talk.BeginOuting(period),"Nested outing accepted");
        unsigned buttons=0, menus=0, draws=0;
        bool invitation=false, accepted=false, departure_line=false;
        std::optional<gameplay::RoomOutingDescriptor> destination;
        unsigned scene_count=0,close_at=0,leave_at=0,opens=0;
        std::vector<unsigned> actors;
        bool arrival_title=false,arrival_description=false;
        if(depart) {
            talk.SetOutingEnvironment(false,false,2,1);
            talk.SetCalendarMinute(12345678);
            Check(!talk.FinishOutingScene(12) && !talk.FinishOutingInventoryRestore(),
                  "Outing accepted an unsolicited owner completion");
        }
        for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
            if(auto bound=talk.random_requested()) {
                talk.SupplyRandom((draws++ * 17u + period) % *bound);
            }
            unsigned next=0;
            if(talk.message_pc()==0x1319d && talk.message_revealed()) invitation=true;
            if(talk.message_pc()==0x131bc && talk.message_revealed()) {
                departure_line=talk.text().find(roof)!=std::string::npos;
                destination=talk.outing_descriptor();
                Check(destination && destination->kind==12 && destination->label==roof &&
                      destination->destination>=118 && destination->destination<=120 &&
                      destination->written==talk.vocabulary().FieldText(destination->word,1) &&
                      !destination->written.empty() && destination->title.find(destination->written)!=std::string::npos &&
                      destination->eligibility_word==unsigned(talk.vocabulary().FirstOutingMatch(destination->primary_filter)),
                      "Outing descriptor does not bind the chosen roof and word");
            }
            if(talk.answer_interactive() && !accepted && !buttons) {
                const auto& labels=talk.answer_options();
                Check(labels==std::vector<std::string>({roof,cancel}),"Wrong first-day roof or Cancel label");
                if(!depart && talk.answer_cursor()==0) next=0x40;
                else { next=0x2000; accepted=true; ++menus; }
            }
            buttons=next;
            talk.Step(buttons,true,true,&font,false,{},route==1 || depart ? &outing_rng : nullptr);
            if(!talk.error().empty()) throw std::runtime_error(talk.error());
            if(auto gap=talk.ordinary_frontier()) throw std::runtime_error("Unexpected outing frontier "+std::to_string(*gap));
            if(!depart) continue;
            if(auto command=talk.actor_command(); command && departure_line) {
                actors.push_back(*command);
                if(*command==175) leave_at=tick;
                if(*command==174 || *command==186 || *command==188 || *command==190 || *command==191)
                    CheckOutingActorWait(talk,font);
                if(*command==273) Check(close_at && tick>=close_at+40,"ASC273 preceded the source wipe delay");
            }
            if(auto wipe=talk.TakeOutingWipe()) {
                Check(*wipe==bool(scene_count),"Departure must close the wipe and arrival open it");
                if(!scene_count) {
                    Check(leave_at && tick>=leave_at+75,"Roof wipe preceded the source departure delay");
                    close_at=tick;
                } else ++opens;
            }
            if(auto selector=talk.outing_scene_request()) {
                Check(*selector==12 && destination && !scene_count && !actors.empty() && actors.back()==273,
                      "Wrong scene requested after outward actor path");
                ++scene_count;
                CheckOutingSceneWait(talk,*selector,font);
            }
            if(destination && talk.message_revealed()) {
                if(talk.message_pc()==0x11d7e) arrival_title=talk.text().find(destination->title)!=std::string::npos;
                if(talk.message_pc()==0x11da7) arrival_description=talk.text().find(destination->description)!=std::string::npos;
            }
        }
        if(depart) {
            Check(invitation && menus==1 && talk.last_answer()==1 && departure_line && scene_count==1 &&
                  opens==1 && arrival_title && arrival_description && talk.finished() && talk.ordinary_source_return(),
                  "Outing lost its descriptor, arrival text or owning return");
            CheckOutingCommands(actors,{174,175,273,186,188,190,191,178,79,40,74},"Roof arrival");
            const auto place=talk.contact_location();
            Check(destination && place.scene==12 && place.descriptor_id==destination->destination &&
                  place.kind==12 && place.transfer_minute==12345678,
                  "Ordinary contact lost the selected roof or its transfer time");
            Check(talk.BeginOutingReturn() && !talk.BeginOutingReturn(),"Roof return entry or nested entry");
            actors.clear(); unsigned home=0,restores=0,home_opens=0; bool home_message=false;
            for(unsigned tick=0;tick<16000 && !talk.finished();++tick) {
                if(auto n=talk.random_requested()) talk.SupplyRandom(0);
                talk.Step(0,true,true,&font,false,{},&outing_rng);
                if(!talk.error().empty()) throw std::runtime_error(talk.error());
                if(auto gap=talk.ordinary_frontier()) throw std::runtime_error("Outing return frontier "+std::to_string(*gap));
                if(auto selector=talk.outing_scene_request()) {
                    Check(*selector==0 && !home++,"Wrong return selector");
                    CheckOutingSceneWait(talk,0,font);
                }
                if(talk.outing_inventory_restore_requested()) {
                    Check(home==1 && !restores++,"Restore requested before entering home");
                    for(unsigned i=0;i<12;++i) {
                        talk.Step(0,true,true,&font);
                        Check(talk.outing_inventory_restore_requested() && !talk.finished() && !talk.actor_command(),
                              "Home script advanced before inventory restoration");
                    }
                    Check(talk.FinishOutingInventoryRestore() && !talk.FinishOutingInventoryRestore(),
                          "Inventory restore receipt must belong to one invocation");
                }
                if(auto cmd=talk.actor_command()) {
                    actors.push_back(*cmd);
                    if(*cmd==186 || *cmd==188 || *cmd==190 || *cmd==191 || *cmd==157 || *cmd==178)
                        CheckOutingActorWait(talk,font);
                }
                if(auto wipe=talk.TakeOutingWipe()) {
                    Check(*wipe,"Home return must open the wipe");
                    ++home_opens;
                }
                if(talk.message_pc()==0x11d64 && talk.message_revealed()) home_message=true;
            }
            const auto home_place=talk.contact_location();
            Check(home_place.scene==0 && home_place.descriptor_id==0 && home_place.kind==0,
                  "Contact still uses the roof descriptor after return");
            CheckOutingCommands(actors,{186,188,190,191,273,176,157,178,35,74},"Home arrival");
            Check(home==1 && restores==1 && home_opens==1 && home_message && talk.finished() &&
                  talk.ordinary_source_return(),"Incomplete returning-home script");
            continue;
        }
        Check(invitation && menus==1 && (route==1 || draws) && talk.last_answer()==2 && talk.finished() &&
              talk.ordinary_source_return() && !talk.ordinary_rejected(),"Missing real outing Cancel return");
        Check(talk.TakeOutingRoomRequest() && !talk.TakeOutingRoomRequest(),"GAME12(0) not single-use");
    }
}


void CheckFallbackRoster(const std::filesystem::path& dir,const recovered::PackageView& font,unsigned species) {
    const auto game=dir.parent_path().parent_path().parent_path();
    gameplay::AcceptedProfile profile{};
    profile.actor=species;
    profile.adopted_at={0,0,0,12,19,11,2026};
    profile.nickname.Assign("Pokepi");
    profile.player.Assign("Nana");
    gameplay::RoomTalkState base;
    gameplay::ColdRoomActor base_actor;
    std::string error;
    if (!base.Load(dir,profile,error) || !base_actor.Load(game,species,2,error))
        throw std::runtime_error(error);
    recovered::RngNativeOwner rng;
    rng.ApplyInitializerTimeT32(168,1,"Room fallback roster fixture");
    base.vocabulary().StartActorScript(rng.NextC3234(),&rng);
    base.EnterWithoutIntroduction();
    base.SetRoomDay(1);
    base.SetCalendarMinute(recovered::CalendarScalar7C660(recovered::CalendarRecord689B8({2026,11,19,12,0})));
    base_actor.EnterWithoutInvitation();
    for (unsigned i=0;i<512 && !base_actor.entrance_finished();++i) {
        if (!base_actor.Advance()) throw std::runtime_error(base_actor.error());
    }
    Check(base_actor.entrance_finished(),"No actor entrance before fallback suite");
    gameplay::RoomFallbackList roster;
    if (!roster.Load(dir,error,species)) throw std::runtime_error(error);
    const auto count=roster.CandidateCount();
    constexpr unsigned counts[]{1057,512,490,512,547};
    Check(species<std::size(counts) && count==counts[species],"Missing full source fallback roster");
    unsigned first_return=0, retry_return=0;
    for (unsigned rank=0;rank<count;++rank) {
        auto talk=base;
        auto actor=base_actor;
        auto reference=roster;
        const unsigned selected=reference.Consume(rank);
        unsigned last_message=0,questions=0,lines=0,editors=0,action=0;
        bool first_draw=true,answered=false;
        unsigned previous_buttons=0;
        std::string outcome;
        try {
            Check(species>=2 ? talk.BeginNamedContact() : talk.BeginOrdinaryTopic(talk.named_contact_entry()),
                  "Cannot enter actual fallback dispatcher");
            for (unsigned tick=0;tick<12000 && !talk.finished();++tick) {
                if (const auto request=talk.random_requested()) {
                    const auto pick=first_draw?rank:0u;
                    Check(!first_draw || *request==count,"Fallback selected from unexpected roster");
                    first_draw=false;
                    talk.SupplyRandom(pick);
                }
                if (const auto boundary=talk.ordinary_frontier()) {
                    outcome="source boundary at "+std::to_string(*boundary);
                    break;
                }
                if(talk.word_entry_requested()) {
                    ++editors;
                    talk.FinishWordEntry("","");
                }
                if(talk.answer_interactive() && !answered && !previous_buttons) {
                    previous_buttons=0x2000;
                    answered=true;
                    ++questions;
                } else previous_buttons=0;
                if (!talk.answer_visible()) answered=false;
                if (const auto requested=talk.actor_command()) {
                    action=*requested;
                    if (!actor.Request(action))
                        throw std::runtime_error("Missing actor command "+std::to_string(action));
                }
                if (!actor.Advance()) throw std::runtime_error("Actor update "+std::to_string(action)+": "+actor.error());
                talk.Step(previous_buttons,true,actor.command_ready(),&font);
                if (!talk.error().empty()) throw std::runtime_error(talk.error());
                if (talk.message_pc()!=last_message) {
                    last_message=talk.message_pc();
                    ++lines;
                }
            }
            if (outcome.empty()) {
                if (talk.ordinary_source_return()) outcome="returned";
                else if (talk.ordinary_rejected()) outcome="original refusal";
                else outcome="unfinished";
            }
        } catch(const std::exception& failure) {
            outcome=failure.what();
        }
        const auto history=talk.fallback_list().SaveTable();
        const auto& entries=history.children.front().children;
        const auto attempts=std::count_if(entries.begin(),entries.end(),[](const auto& cell){return cell.word!=0;});
        Check(attempts && (entries.front().word&0x0fffffffu)==selected,
              "The actual fallback selector did not draw the expected source entry");
        if(outcome=="returned" && attempts==1) ++first_return;
        else if(outcome=="returned") ++retry_return;
        std::cout << rank << '\t' << std::hex << selected << '\t' << last_message << std::dec
                  << '\t' << lines << '\t' << questions << '\t' << editors << '\t' << attempts
                  << '\t' << outcome << '\n';
    }
    std::cerr << "Actor " << species << ": " << first_return << " direct, " << retry_return
              << " after source retry, " << count << " entries\n";
    Check(first_return+retry_return==count,"Incomplete source fallback roster");
}

}
void CheckFriendsIntroduction(const std::filesystem::path&, const recovered::PackageView&);
void CheckFriendsExchange(const std::filesystem::path&, const recovered::PackageView&);

int main(int argc,char** argv) {
    try {
        gameplay::RoomTalkOpcodeTests::Run();
        if(argc==2 && std::string_view(argv[1])=="--integer-opcodes") {
            std::cout<<"KSC integer, array and builtin tests passed\n";
            return 0;
        }
        if(argc==3 && std::string_view(argv[1])=="--script-module-archive") {
            gameplay::RoomTalkOpcodeTests::ScriptModuleArchive(argv[2]);
            std::cout<<"KSC module loading passed for five locally supplied archive members\n";
            return 0;
        }
        if(argc==3 && std::string_view(argv[1])=="--ksc-locals") {
            gameplay::RoomTalkOpcodeTests::LocalTables(argv[2]);
            std::cout<<"KSC local tables passed for five packed actors\n";
            return 0;
        }
        Check(argc==3 || argc==4 || argc==5,"Expected post-profile dir and FONT.XFT");
        gameplay::RoomTalkOpcodeTests::LocalTables(argv[1]);
        auto lease=std::make_shared<Font>();
        std::ifstream file(argv[2],std::ios::binary);
        lease->data.assign(std::istreambuf_iterator<char>(file),{});
        Check(lease->data.size()>0x203c,"Missing font geometry");
        lease->known.resize(lease->data.size(),1);
        auto font=recovered::PackageStorage::Attach(lease,lease->data.size());
        if (argc==5) {
            Check(std::string_view(argv[3])=="--fallback-roster","Unknown room text test");
            CheckFallbackRoster(argv[1],font,std::stoul(argv[4]));
            return 0;
        }
        if (argc==4) {
            Check(std::string_view(argv[3])=="--fallback-all","Unknown room text test");
            for(unsigned species=0;species<5;++species) CheckFallbackRoster(argv[1],font,species);
            return 0;
        }
        CheckFriendsIntroduction(argv[1],font);
        CheckFriendsExchange(argv[1],font);
        gameplay::RoomTalkState idle;
        gameplay::AcceptedProfile fresh{};
        std::string error;
        if(!idle.Load(argv[1],fresh,error)) throw std::runtime_error(error);
        idle.Step(0,true,false,&font);
        idle.Step(0,false,false,&font,true);
        Check(!idle.consumed() && idle.message_pc()==0x7bc93 && idle.actor_command()==42,
              "Natural invitation expiry must invoke its callback without reporting an input");
        HomeOuting(argv[1],font);
        DuplicateLesson(argv[1],font,false); DuplicateLesson(argv[1],font,true);
        MemoLesson(argv[1],font,1); MemoLesson(argv[1],font,2);
        Ordinary(argv[1],font,0x8dba,0);
        Ordinary(argv[1],font,0x8dba,0,true);
        Ordinary(argv[1],font,0x8dba,18);
        Ordinary(argv[1],font,0x8dba,0,false,40,false,true);
        Ordinary(argv[1],font,0x8dba,600);
        Ordinary(argv[1],font,0x8dba,600,false,40,false,true);
        Ordinary(argv[1],font,0x848f9,1,false,40,true);
        Ordinary(argv[1],font,0x846ab,1,false,40,true);
        Run(argv[1],font,0,false,0); Run(argv[1],font,1,false,1);
        Run(argv[1],font,1,true,0); Run(argv[1],font,-1,false,1);
        const auto first_visit_reply=VisitPlaces(argv[1],font,0);
        Check(VisitPlaces(argv[1],font,1)==first_visit_reply,"Visit replies gave different follow-up lines");
        for(unsigned pc:{0x8528bu,0x8506du}) {
            Ordinary(argv[1],font,pc,0); Ordinary(argv[1],font,pc,1);
        }
        for(unsigned pc:{0x846abu,0x848f9u}) {
            Ordinary(argv[1],font,pc,0); Ordinary(argv[1],font,pc,1,true);
        }
        Ordinary(argv[1],font,0x84b63,0); Ordinary(argv[1],font,0x84b63,1);
        Ordinary(argv[1],font,0x84d7a,0); Ordinary(argv[1],font,0x84d7a,4);
        for(unsigned answer:{0u,1u,2u}) Ordinary(argv[1],font,0x84e93,answer);
        Ordinary(argv[1],font,0x4e27,0); Ordinary(argv[1],font,0x89acc,0);
        Ordinary(argv[1],font,0x7d43a,0); Ordinary(argv[1],font,0x7d43a,0,true,0);
        std::cout<<"Cold Toro introduction, source-selected ordinary topics, ASK, typed teaching, existing words, cancellation,"
                   " relationship answers, fallback selection and farewell passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
