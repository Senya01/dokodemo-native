template<class Step>
void OpenLinkedStayMenu(dokodemo::gameplay::BootToDisplay& boot, const Step& step) {
    for (unsigned i=0;i<60 && !boot.RoomTeaching().menu_open;++i) {
        step(0x20);
        for (unsigned tick=0;tick<9;++tick) step();
    }
    Require(boot.RoomTeaching().menu_open && boot.RoomTeaching().menu_cursor==0,
            "Missing active room menu");
}

template<class Step>
void SaveLinkedStayRoom(dokodemo::gameplay::BootToDisplay& boot, const Step& step, const unsigned& saves) {
    OpenLinkedStayMenu(boot,step);
    const auto rest=[&](unsigned ticks) { while (ticks--) step(); };
    const auto press=[&](unsigned key) { step(key); step(); };
    rest(14);
    press(0x10);
    Require(boot.RoomTeaching().menu_cursor==6,"The active room cannot select System");
    press(0x2000); rest(30); press(0x40); press(0x2000);
    for (unsigned i=0;i<200 && !saves;++i) step();
    Require(saves==1,"System must publish exactly one save");
}

void SaveLinkedFirstDay(const std::filesystem::path& install, std::uint8_t day=3) {
    using namespace dokodemo::gameplay;
    const auto slot=install/"savedata"/"UCJS10002000";
    Require(!std::filesystem::exists(slot.parent_path()),"A new stay requires an empty installation");
    const std::array<std::uint8_t,16> date{0xea,0x07,11,0,day,0,12};
    auto boot=std::make_unique<BootToDisplay>();
    Require(boot->Initialize() && boot->InitializeClock(date,168) &&
            boot->MountDataArchive(install/"assets/iso/PSP_GAME/USRDIR/data/DATA.BP") && boot->PrepareTitle(),
            "Could not start the new Toro stay");
    VerifyStartupRoom(*boot);
    unsigned saves=0;
    const auto step=[&](unsigned buttons=0) {
        if (!boot->AdvanceTitle(buttons))
            throw std::runtime_error(boot->SaveReadBoundary()?boot->SaveReadBoundary():"First room update failed");
        for (const auto& entry : boot->DepartureEvents()) {
            const std::string_view name=entry.name;
            if (name=="system-save-written") ++saves;
            if (name=="system-save-unavailable")
                throw std::runtime_error(entry.text.empty()?"New stay save failed":entry.text.front());
        }
    };
    SaveLinkedStayRoom(*boot,step,saves);
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

void CrossLinkedMidnight(const std::filesystem::path& install) {
    using namespace dokodemo::gameplay;
    const auto slot=install/"savedata"/"UCJS10002000";
    Require(!std::filesystem::exists(slot),"Cold startup should restore the missing slot");
    const std::array<std::uint8_t,16> date{0xea,0x07,11,0,30,0,23,0,59,0,59};
    auto boot=std::make_unique<BootToDisplay>();
    Require(boot->Initialize() && boot->InitializeClock(date,168) &&
            boot->MountDataArchive(install/"assets/iso/PSP_GAME/USRDIR/data/DATA.BP") && boot->PrepareTitle(),
            "Could not cold-start the recovered protected slot");
    Require(boot->Title().title_complete() && std::filesystem::is_directory(slot),
            "Recovered slot did not load directly into the room");
    const auto backup=install/"recovered-before-midnight";
    std::filesystem::copy(slot,backup,std::filesystem::copy_options::recursive);
    const auto request=ReadRetailSlot(slot);
    const auto before=DecodeRetailUtilityRequest(request,RetailSaveProfile{});
    const auto original=RoomSaveOwners::FromActorGraph(before.actor);
    Require(before.profile.SavedAt()==RetailSaveProfile::Calendar{0,0,0,12,30,11,2026} &&
            original.day_words[0].children.at(0).word==0x20000001u,
            "Recovered the wrong generation before midnight");

    unsigned restores=0, clears=0, saves=0;
    const auto step=[&](unsigned key=0) {
        if (!boot->AdvanceTitle(key))
            throw std::runtime_error(boot->SaveReadBoundary()?boot->SaveReadBoundary():"Midnight crossing update failed");
        for (const auto& event:boot->DepartureEvents()) {
            const std::string_view name=event.name;
            if (name=="room-load-restored") {
                Require(event.values.at(0).second==1 && event.values.at(1).second==1,
                        "Cold recovery did not restore day one before midnight");
                ++restores;
            }
            if (name=="diary-staging-clear") ++clears;
            if (name=="system-save-written") ++saves;
            if (name=="system-save-unavailable")
                throw std::runtime_error(event.text.empty()?"Midnight save failed":event.text.front());
            Require(name!="scheduled-room-event" && name!="gift-received",
                    "The rollover replayed an introduction or started a scheduled scene");
        }
    };
    for (unsigned i=0;i<1800 && !boot->RoomActivity().started_command;++i) step();
    Require(restores==1 && clears==0 && boot->RoomActivity().started_command,
            "Restored pre-midnight room did not start an activity");
    const auto serial=boot->RoomActivity().serial;
    auto next=boot->Calendar();
    AdvanceTestDate(next);
    next[1]=next[2]=next[3]=0;
    Require(next==RetailSaveProfile::Calendar{0,0,0,0,1,12,2026},"Incorrect test midnight");
    boot->SetUpdateCalendar(next);
    for (unsigned i=0;i<1800 && !(clears && boot->RoomActivity().serial>serial &&
             boot->RoomActivity().started_command);++i) step();
    Require(restores==1 && clears==1 && boot->RoomActivity().serial>serial &&
            boot->RoomActivity().started_command && !saves && ReadRetailSlot(slot)==request,
            "The active room failed to cross midnight without writing a slot");

    OpenLinkedStayMenu(*boot,step);
    const auto rest=[&](unsigned ticks) { while (ticks--) step(); };
    const auto press=[&](unsigned key) { step(); step(key); rest(16); };
    rest(30);
    press(0x40); press(0x40);
    Require(boot->RoomTeaching().menu_cursor==3,"Restored game cannot select the notebook");
    press(0x2000); rest(30); press(0x40); press(0x40);
    Require(boot->Notebook().menu_column==2,"Restored game cannot select the diary");
    press(0x2000);
    for (unsigned i=0;i<1800 && !boot->Notebook().list_ready;++i) step();
    Require(boot->Notebook().resource==12 && boot->Notebook().list_ready,"Diary did not open after midnight");
    if (boot->Notebook().selection==1) press(0x80);
    for (unsigned i=0;i<1800 && !boot->Notebook().detail;++i) step();
    Require(boot->Notebook().detail && boot->Notebook().selection==0,
            "The first day's diary did not reveal after midnight");
    press(0x4000);
    for (unsigned i=0;i<1800 && boot->Notebook().visible;++i) step();
    Require(!boot->Notebook().visible,"Diary did not return to the room");
    rest(40);
    SaveLinkedStayRoom(*boot,step,saves);
    const auto saved=DecodeRetailUtilityRequest(ReadRetailSlot(slot),RetailSaveProfile{});
    const auto after=RoomSaveOwners::FromActorGraph(saved.actor);
    const auto previous_page=before.profile.FirstDayDiary();
    const auto page=saved.profile.FirstDayDiary();
    Require(saved.profile.SavedAt()==next && after.day_words[0].children.at(0).word==0x20000002u &&
            after.day_words[1].children.at(0).word==original.day_words[1].children.at(0).word+1440u &&
            after.progress_flags[0].children.at(0).word==original.progress_flags[0].children.at(0).word &&
            previous_page && page && page->revealed && page->date==previous_page->date && page->text==previous_page->text,
            "System save did not preserve the diary and the new registered day");
    std::cout<<"Cold recovery reached the room on November 30; the running room crossed\n"
                "December 1, opened the previous diary page and saved day 2 through System\n";
}

int CheckCalendarSeam(const std::filesystem::path& install, std::string_view phase) {
    namespace fs=std::filesystem;
    if (phase=="seed") {
        SaveLinkedFirstDay(install,30);
    } else if (phase=="midnight") {
        CrossLinkedMidnight(install);
    } else if (phase=="finish") {
        AdvanceLinkedToroDays(install,2,12);
        const auto slot=install/"savedata"/"UCJS10002000";
        CheckPenultimateSlot(install,slot,false,true);
        const auto baseline=install/"pre-farewell"/slot.filename();
        Require(fs::create_directory(baseline.parent_path()),"Farewell baseline requires a new directory");
        fs::copy(slot,baseline,fs::copy_options::recursive);
        return CheckFinalDaySlot(install,baseline);
    } else if (phase=="continue") {
        return CheckDepartedRoom(install,false,true);
    } else {
        throw std::invalid_argument("Unknown calendar seam phase");
    }
    return 0;
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
