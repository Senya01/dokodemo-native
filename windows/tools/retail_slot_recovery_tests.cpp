#include "gameplay/retail_save_slot.h"
#include "gameplay/retail_slot_lock.h"
#include "gameplay/retail_slot_transaction.h"
#include <windows.h>
#include <array>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
namespace fs=std::filesystem;
using namespace dokodemo::gameplay;
using detail::SlotWritePhase;

void Require(bool ok,const char* message) {
    if (!ok) throw std::runtime_error(message);
}

template<class Action>
void ExpectFailure(Action action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected the slot operation to fail");
}

class ShareLock {
public:
    ShareLock(const fs::path& path,DWORD share) : handle_(CreateFileW(path.c_str(),GENERIC_READ,share,
        nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr)) {
        Require(handle_!=INVALID_HANDLE_VALUE,"Could not arrange Windows sharing denial");
    }
    ~ShareLock() { CloseHandle(handle_); }
    ShareLock(const ShareLock&)=delete;
    ShareLock& operator=(const ShareLock&)=delete;
private:
    HANDLE handle_;
};

struct InjectedFailure : std::runtime_error {
    InjectedFailure() : std::runtime_error("Injected savedata write failure") {}
};

bool IsCheckpoint(SlotWritePhase phase,const fs::path& location,std::string_view point) {
    switch (phase) {
    case SlotWritePhase::create_stage: return point=="staging";
    case SlotWritePhase::write_file: return point==location.filename().string();
    case SlotWritePhase::validate_stage: return point=="validation";
    case SlotWritePhase::preserve_previous: return point=="backup";
    case SlotWritePhase::publish: return point=="publish";
    case SlotWritePhase::retire: return point=="retire";
    }
    return false;
}

void WriteFault(const fs::path& slot,const fs::path& game,const std::vector<std::uint8_t>& next,
                std::string_view point,bool crash) {
    bool hit=false, failed=false;
    try {
        detail::CommitRetailSlot(slot,game,next,[&](SlotWritePhase phase,const fs::path& location) {
            if (!IsCheckpoint(phase,location,point)) return;
            hit=true;
            if (crash) std::_Exit(73);
            throw InjectedFailure();
        });
    } catch (const InjectedFailure&) { failed=true; }
    Require(hit && failed==(point!="retire") && !crash,"Incorrect write fault outcome");
}

void WriteDenied(const fs::path& slot,const fs::path& game,const std::vector<std::uint8_t>& next,
                 std::string_view point) {
    std::optional<ShareLock> held;
    if (point=="live") {
        held.emplace(slot,FILE_SHARE_READ|FILE_SHARE_WRITE);
        ExpectFailure([&]{ WriteRetailSlot(slot,game,next); });
        return;
    }
    if (point=="data") {
        held.emplace(slot/"DATA.BIN",0);
        ExpectFailure([&]{ ReadRetailSlot(slot); });
        ExpectFailure([&]{ WriteRetailSlot(slot,game,next); });
        return;
    }
    if (point=="owner") {
        const RetailSlotLock lock(slot);
        ExpectFailure([&]{ LoadRetailSlot(slot); });
        ExpectFailure([&]{ WriteRetailSlot(slot,game,next); });
        return;
    }
    Require(point=="staged" || point=="rollback" || point=="retire","Unknown sharing denial");
    auto backup=slot;
    backup+=".previous";
    bool hit=false;
    const auto write=[&] {
        detail::CommitRetailSlot(slot,game,next,[&](SlotWritePhase phase,const fs::path& location) {
            if (phase!=SlotWritePhase::publish) return;
            held.emplace(point=="staged" ? location : backup,FILE_SHARE_READ|FILE_SHARE_WRITE);
            hit=true;
            if (point=="rollback") throw InjectedFailure();
        });
    };
    if (point=="retire") {
        write();
        Require(fs::exists(slot) && fs::exists(backup),"Publication did not keep the locked retirement copy");
    }
    else if (point=="staged") ExpectFailure(write);
    else {
        bool original_error=false;
        try { write(); } catch (const InjectedFailure&) { original_error=true; }
        Require(original_error && fs::exists(backup) && !fs::exists(slot),
                "Deferred rollback lost its recovery copy or original error");
    }
    Require(hit,"The sharing conflict never reached the transaction");
}
}

int main(int argc,char** argv) try {
    Require(argc==5 || argc==6,"Expected <operation> <game package> <reference slot> <temporary slot> [checkpoint]");
    const std::string_view operation=argv[1],point=argc==6 ? argv[5] : "";
    const fs::path game=argv[2],reference=argv[3],slot=argv[4];
    Require(!fs::exists(slot) || !fs::equivalent(reference,slot),"The probe needs a separate temporary slot");
    const auto old=ReadRetailSlot(reference);
    auto changed=DecodeRetailUtilityRequest(old,RetailSaveProfile{});
    const std::array<int,3> a{1,1,1}, b{2,2,2};
    changed.profile.SetOptions(changed.profile.Options()==a ? b : a);
    const auto next=BuildRetailUtilityRequest(changed.profile,changed.actor,changed.word_history);
    Require(next!=old,"The saved generations need distinct contents");

    if (operation=="write-old" || operation=="write-new")
        WriteRetailSlot(slot,game,operation=="write-old" ? old : next);
    else if (operation=="read-old" || operation=="read-new")
        Require(ReadRetailSlot(slot)==(operation=="read-old" ? old : next),"Cold load returned the wrong generation");
    else if (operation=="refuse") {
        ExpectFailure([&]{ LoadRetailSlot(slot); });
        ExpectFailure([&]{ WriteRetailSlot(slot,game,next); });
    } else if (operation=="absent")
        Require(!LoadRetailSlot(slot),"Uncommitted staging directory became authoritative");
    else if (operation=="fault" || operation=="crash")
        WriteFault(slot,game,next,point,operation=="crash");
    else if (operation=="sharing")
        WriteDenied(slot,game,next,point);
    else if (operation=="damage-stage") {
        bool damaged=false;
        ExpectFailure([&] {
            detail::CommitRetailSlot(slot,game,next,[&](SlotWritePhase phase,const fs::path& location) {
                if (phase==SlotWritePhase::validate_stage) {
                    fs::resize_file(location/point,1);
                    damaged=true;
                }
            });
        });
        Require(damaged,"The staged validation case never damaged its file");
    } else throw std::runtime_error("Unknown recovery probe operation");
    std::cout<<operation<<' '<<point<<" passed\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<error.what()<<'\n';
    return 1;
}
