void SaveLinkedFirstDay(const std::filesystem::path& install) {
    using namespace dokodemo::gameplay;
    const auto slot=install/"savedata"/"UCJS10002000";
    Require(!std::filesystem::exists(slot.parent_path()),"A new stay requires an empty installation");
    constexpr std::array<std::uint8_t,16> date{0xea,0x07,11,0,3,0,12};
    auto boot=std::make_unique<BootToDisplay>();
    Require(boot->Initialize() && boot->InitializeClock(date,168) &&
            boot->MountDataArchive(install/"assets/iso/PSP_GAME/USRDIR/data/DATA.BP") && boot->PrepareTitle(),
            "Could not start the new Toro stay");
    VerifyStartupRoom(*boot);
    bool written=false;
    const auto step=[&](unsigned buttons=0) {
        if (!boot->AdvanceTitle(buttons))
            throw std::runtime_error(boot->SaveReadBoundary()?boot->SaveReadBoundary():"First room update failed");
        for (const auto& entry : boot->DepartureEvents()) {
            const std::string_view name=entry.name;
            if (name=="system-save-written") written=true;
            if (name=="system-save-unavailable")
                throw std::runtime_error(entry.text.empty()?"New stay save failed":entry.text.front());
        }
    };
    const auto rest=[&](unsigned ticks) { while (ticks--) step(); };
    const auto press=[&](unsigned key) { step(key); step(); };
    for (unsigned i=0;i<60 && !boot->RoomTeaching().menu_open;++i) {
        press(0x20);
        rest(8);
    }
    Require(boot->RoomTeaching().menu_open && boot->RoomTeaching().menu_cursor==0,"Missing new stay room menu");
    rest(14);
    press(0x10);
    Require(boot->RoomTeaching().menu_cursor==6,"The new stay cannot select System");
    press(0x2000); rest(30); press(0x40); press(0x2000);
    for (unsigned i=0;i<200 && !written;++i) step();
    Require(written,"The new Toro profile did not save through System");
    const auto saved=DecodeRetailUtilityRequest(ReadRetailSlot(slot),RetailSaveProfile{});
    const auto own=RoomSaveOwners::FromActorGraph(saved.actor);
    Require(saved.profile.ReadProfile().actor==0 && saved.profile.ReadProfile().player.text()=="AKI" &&
            saved.profile.MainDiaryCount()==1 && own.day_words[0].children.at(0).word==0x20000001u &&
            (own.progress_flags[0].children.at(0).word&7)==1,"The new protected slot has the wrong day-one state");
    std::cout<<"Linked stay adopted Toro, finished the introduction and saved day 1 through System\n";
}

void AdvanceLinkedToroDays(const std::filesystem::path& install,int from,int through) {
    using namespace dokodemo::gameplay;
    const auto slot=install/"savedata"/"UCJS10002000";
    for (int day=from+1;day<=through;++day) {
        const auto request=ReadRetailSlot(slot);
        const auto saved=DecodeRetailUtilityRequest(request,RetailSaveProfile{});
        const auto before=RoomSaveOwners::FromActorGraph(saved.actor);
        auto clock=saved.profile.SavedAt();
        AdvanceTestDate(clock);
        clock[1]=0;
        clock[2]=53;
        clock[3]=14;
        ResumeDayRoom room(install,saved,clock);
        room.Start(day-1,day);
        Require(!room.scheduled && ReadRetailSlot(slot)==request,"A regular stay day replayed a scene or wrote without input");
        if (day==2) room.RevealDiary();
        room.Save();
        const auto resumed=DecodeRetailUtilityRequest(ReadRetailSlot(slot),RetailSaveProfile{});
        const auto after=RoomSaveOwners::FromActorGraph(resumed.actor);
        Require(resumed.profile.SavedAt()==clock && after.day_words[0].children.at(0).word==0x20000000u+unsigned(day) &&
                after.day_words[1].children.at(0).word==before.day_words[1].children.at(0).word+1440u &&
                after.progress_flags[0].children.at(0).word==before.progress_flags[0].children.at(0).word,
                "Daily System save lost its midnight or replayed flags");
        std::cout<<"Linked stay day "<<day<<" resumed, started ASC "<<*room.room.first_activity()<<" and saved\n";
    }
}

int CheckToroLinkedStay(const std::filesystem::path& install) {
    namespace fs=std::filesystem;
    SaveLinkedFirstDay(install);
    AdvanceLinkedToroDays(install,1,12);
    const auto checkpoint=install/"savedata"/"UCJS10002000";
    CheckPenultimateSlot(install,checkpoint,false,true);

    const auto prepare_branch=[&](const char* name,const fs::path& previous={}) {
        const auto branch=install/name;
        Require(fs::create_directory(branch),"The ending branch needs a fresh directory");
        fs::copy(install/"assets",branch/"assets",fs::copy_options::recursive|fs::copy_options::create_hard_links);
        if (!previous.empty()) {
            fs::create_directory(branch/"savedata");
            fs::copy(previous,branch/"savedata"/"UCJS10002000",fs::copy_options::recursive);
        }
        return branch;
    };
    const auto ordinary=prepare_branch("ordinary",checkpoint);
    CheckFinalDaySlot(ordinary,checkpoint);
    CheckDepartedRoom(ordinary,false,true);
    std::cout<<"Linked normal ending completed and cold Continue returned to the empty room\n";

    const auto extension=prepare_branch("extension",checkpoint);
    CheckFinalDaySlot(extension,checkpoint,true);
    CheckExtendedReturn(extension);
    AdvanceLinkedToroDays(extension,14,22);
    const auto eve=prepare_branch("day-23");
    CheckExtendedPenultimate(eve,extension/"savedata"/"UCJS10002000",true);
    const auto last=prepare_branch("day-24");
    CheckExtendedLastDay(last,eve/"savedata"/"UCJS10002000");
    CheckDepartedRoom(last,false,true);
    std::cout<<"Linked extended ending completed and cold Continue returned to the empty room\n";
    return 0;
}
