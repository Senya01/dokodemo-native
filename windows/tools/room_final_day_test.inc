void PrepareExtendedStayFixture(dokodemo::gameplay::RetailUtilityContents& saved) {
    using namespace dokodemo::gameplay;
    auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    owners.contact_count.children.at(0).word=0x20000000u|581u;
    auto& table=owners.dictionary.children.at(0).children;
    for (auto& reference : table) {
        auto& flags=reference.children.at(0).children.at(0).word;
        if ((flags>>28)==2) flags&=~0x20u;
    }
    constexpr unsigned count=100;
    const auto& first_lesson=table.at(163);
    const auto lesson=first_lesson.children.at(0).children.at(1).text.empty()?table.at(1):first_lesson;
    for (unsigned i=0;i<count;++i) {
        auto row=lesson;
        auto& fields=row.children.at(0).children;
        fields[0].word=0x20000020;
        if (i) fields[1].text+=std::to_string(i);
        table.at(163+i)=std::move(row);
    }
    saved.actor=owners.ToActorGraph();

    const auto clock=saved.profile.SavedAt();
    Require(clock[4]>14,"Need a controlled adoption date earlier in the fixture month");
    auto profile=saved.profile.ReadProfile();
    profile.adopted_at=clock;
    profile.adopted_at[4]-=14;
    saved.profile.Adopt(profile);
    auto request=BuildRetailUtilityRequest(saved.profile,saved.actor);
    // 3A314 considers a row occupied when its illustration differs from -1.
    // Keep one real diary row from the input and provide three occupied rows.
    constexpr unsigned diary_base=0x114, row_size=0xdc;
    Require(saved.profile.MainDiaryCount()==1,"Expected a single real diary row in the input");
    for (unsigned row=1;row<=3;++row)
        std::copy_n(request.begin()+diary_base,row_size,request.begin()+diary_base+row_size*row);
    const auto extended=DecodeRetailUtilityRequest(request,RetailSaveProfile{});
    Require(extended.profile.MainDiaryCount()==4,"Extended fixture diary bank projection failed");
    saved=extended;
}

int CheckFinalDaySlot(const std::filesystem::path& install, const std::filesystem::path& source,
                      bool extended=false) {
    using namespace dokodemo::gameplay;
    const auto slot=install/"savedata"/"UCJS10002000";
    Require(!std::filesystem::equivalent(slot,source),
            "Install must contain a private copy of the penultimate slot");
    const auto original=ReadRetailSlot(source);
    Require(ReadRetailSlot(slot)==original,"Private slot does not match the fixture");
    auto saved=DecodeRetailUtilityRequest(original,RetailSaveProfile{});
    const auto species=saved.profile.ReadProfile().actor;
    const bool robo=species==1, named=species>=2;
    struct FarewellScene {
        unsigned first, last, alternate_last, choices, game75, game46;
    };
    constexpr FarewellScene named_scenes[]{
        {0x605de,0x60851,0x6082b,1,0x67779,0x67128},
        {0x63fbc,0x6459e,0x6458a,1,0x6b94e,0x6b2ff},
        {0x62f96,0x63250,0x63225,0,0x6b761,0x6b110},
    };
    const auto scene=named?named_scenes[species-2]:FarewellScene{0x5b447,0x5b6a8,0,2,0x6b896,0x6b247};
    const auto original_owners=RoomSaveOwners::FromActorGraph(saved.actor);
    Require(original_owners.day_words[0].children.at(0).word==0x2000000d &&
            original_owners.progression_words[0].children.at(0).word==0x2000000e &&
            (original_owners.progress_flags[0].children.at(0).word&7)==3,
            "Expected a completed 13th-day slot");
    if (extended) PrepareExtendedStayFixture(saved);
    auto calendar=saved.profile.SavedAt();
    Require(calendar[4]<28,"Use a fixture with the following day in the same month");
    ++calendar[4];
    calendar[3]=14;
    calendar[2]=53;
    ResumeDayRoom run(install,saved,calendar);
    const std::array<std::string_view,4> sequence{
        "farewell-cover-closed","farewell-save-written",
        "farewell-save-acknowledged","scheduled-farewell-complete"};
    unsigned buttons=0, choices=0, effects=0, effects_finished=0, stage=0;
    bool first_message=false, last_message=false, farewell_pose=false, departure_pose=false;
    bool invitation=false, last_day_cleared=false;
    unsigned return_day=0, extended_limit=0;
    std::vector<std::uint8_t> staged_diary;
    for (unsigned update=0;update<(named?14000u:4800u) && !run.room.return_to_title_requested();++update) {
        run.Step(buttons);
        buttons=0;
        if (robo || named) {
            const auto talk=run.room.conversation_snapshot();
            first_message |= talk.message_pc==scene.first && talk.message_revealed;
            last_message |= (talk.message_pc==scene.last || (scene.alternate_last && talk.message_pc==scene.alternate_last))
                            && talk.message_revealed;
        }
        for (const auto& event:run.room.departure_events()) {
            const std::string_view name=event.name;
            if (name=="scheduled-room-event") {
                Require(event.values.at(0).second==3 && event.values.at(1).second==14,
                        "Expected the farewell event on day 14");
            } else if (name=="scheduled-room-choice") {
                Require(event.values.at(0).second==2,"Expected two source replies");
                ++choices;
                buttons=0x2000;
            } else if (name=="scheduled-actor-command") {
                const auto command=event.values.at(0).second;
                farewell_pose |= command==283;
                departure_pose |= command==284;
                if (command==48 && !invitation) {
                    run.calendar[2]=59;
                    invitation=true;
                }
            } else if (name=="room-motion-effect") {
                Require(event.values.at(0).second==9,"Unexpected motion effect");
                ++effects;
            } else if (name=="room-motion-effect-finished") {
                Require(event.values.at(0).second==9,"Unexpected motion effect completion");
                ++effects_finished;
            } else if (name=="scheduled-stay-written") {
                const auto last=event.values.at(0).second, next=event.values.at(1).second;
                last_day_cleared |= last==0;
                if (next>=3 && next<=10) return_day=unsigned(next);
                if (last>14) extended_limit=unsigned(last);
            } else if (name=="farewell-diary-staged") {
                Require(staged_diary.empty(),"Repeated farewell diary staging");
                const auto& row=event.text.at(0);
                staged_diary.assign(row.begin(),row.end());
            } else if (std::find(sequence.begin(),sequence.end(),name)!=sequence.end()) {
                Require(stage<sequence.size() && name==sequence[stage],"Farewell host actions out of order");
                ++stage;
                std::cout<<name<<" at update "<<update<<'\n';
                if (stage==1) {
                    Require(event.values.at(2).second==int(extended),"Unexpected GAME75 mode");
                    Require(ReadRetailSlot(slot)==original,"Save changed before the closing transition");
                    Require(Visible(run.frame,((run.frames-1)%2)*surface).visible==0,
                            "Closing transition did not cover the room");
                }
                if ((robo || named) && stage<3)
                    Require(event.values.at(1).second==int(stage==1?scene.game75:scene.game46),
                            "Unexpected GAME75 or GAME46 call site");
            }
        }
    }
    std::cout<<"Farewell choices "<<choices<<", effects "<<effects<<"/"<<effects_finished
             <<", stages "<<stage<<", first/last messages "<<first_message<<"/"<<last_message
             <<", poses "<<farewell_pose<<"/"<<departure_pose<<std::endl;
    Require(run.previous==13 && run.day==14 && run.scheduled==1 && run.restored==1 &&
            ((robo || named) ? choices==scene.choices && first_message && last_message && farewell_pose && departure_pose :
                                choices==7 && effects==2 && effects_finished==2) &&
            (extended ? extended_limit==24 && !last_day_cleared && !return_day && staged_diary.empty() :
                        last_day_cleared && return_day && !staged_diary.empty()) &&
            stage==sequence.size() && run.room.return_to_title_requested(),
            "The farewell scene did not finish");
    const auto request=ReadRetailSlot(slot);
    Require(request!=original && ReadRetailSlot(source)==original,"Incorrect save replacement");
    const auto finished=DecodeRetailUtilityRequest(request,RetailSaveProfile{});
    const auto owners=RoomSaveOwners::FromActorGraph(finished.actor);
    if (extended) {
        Require(finished.profile.SavedAt()==run.calendar && finished.profile.MainDiaryCount()==4 &&
                owners.day_words[0].children.at(0).word==0x2000000e &&
                (owners.progress_flags[0].children.at(0).word&7)==7 &&
                owners.progression_words[0].children.at(0).word==(0x20000000|extended_limit) &&
                owners.progression_words[1].children.at(0).word==original_owners.progression_words[1].children.at(0).word &&
                SelectLoadedBootRoute(finished.profile,run.calendar)==LoadedBootRoute::resume_15,
                "Extended slot did not preserve the new limit and room continuation");
        std::cout<<"GAME75 mode 1 wrote day 14 with a 24-day limit and cold room continuation\n";
        return 0;
    }
    const auto diary=owners.diary_staging.children.at(0);
    const auto player=saved.profile.ReadProfile();
    Require(finished.profile.SavedAt()==run.calendar &&
            owners.day_words[0].children.at(0).word==0x2000000e &&
            (owners.progress_flags[0].children.at(0).word&7)==7 &&
            owners.progression_words[0].children.at(0).word==0x20000000 &&
            owners.progression_words[1].children.at(0).word==(0x20000000|return_day) &&
            diary.children.size()==7 && diary.children.at(1).word==0x20000008 &&
            diary.children.at(3).word==0x20700000 &&
            (species==4 || diary.children.at(2).text.find(player.player.text())!=std::string::npos) &&
            ((species!=1 && species!=2) || diary.children.at(2).text.find(player.nickname.text())!=std::string::npos) &&
            EncodeKscSaveGraph(diary)==staged_diary,"Protected farewell state differs from the live scene");

    auto next_boot=run.calendar;
    next_boot[3]=15;
    next_boot[2]=0;
    Require(SelectLoadedBootRoute(finished.profile,next_boot)==LoadedBootRoute::title_11,
            "Farewell save was routed to automatic room continuation");
    std::array<std::uint8_t,16> date{};
    for (unsigned i=0;i<6;++i) {
        date[2*i]=static_cast<std::uint8_t>(next_boot[6-i]);
        date[2*i+1]=static_cast<std::uint8_t>(next_boot[6-i]>>8);
    }
    BootToDisplay cold;
    Require(cold.Initialize() && cold.InitializeClock(date,168) &&
            cold.MountDataArchive(install/"assets/iso/PSP_GAME/USRDIR/data/DATA.BP") &&
            cold.PrepareTitle(),"Could not cold boot the protected farewell slot");
    for (unsigned frame=0;frame<670 && !(cold.Title().interactive() && cold.Title().menu_variant()==2);++frame)
        if (!cold.AdvanceTitle())
            throw std::runtime_error(cold.SaveReadBoundary()?cold.SaveReadBoundary():"Cold title update failed");
    Require(cold.Title().panel()==0 && cold.Title().interactive() && cold.Title().menu_variant()==2 &&
            !cold.Title().title_complete() && ReadRetailSlot(slot)==request,
            "Cold boot did not open the title with the farewell slot intact");
    std::cout<<"Verified day 13 -> 14, "<<choices<<" choices, "<<effects<<" EF9 effects, a protected farewell save"
             <<" with return day "<<return_day<<", and a cold boot to the title\n";
    return 0;
}


void AdvanceTestDate(dokodemo::gameplay::RetailSaveProfile::Calendar& calendar) {
    using dokodemo::gameplay::ProfileMonthDays;
    if (++calendar[4]<=unsigned(ProfileMonthDays(calendar[6],calendar[5]))) return;
    calendar[4]=1;
    if (++calendar[5]<=12) return;
    calendar[5]=1;
    ++calendar[6];
}

int CheckDepartedRoom(const std::filesystem::path& install, bool expired=false, bool linked=false) {
    using namespace dokodemo::gameplay;
    const auto slot=install/"savedata"/"UCJS10002000";
    const auto request=ReadRetailSlot(slot);
    const auto saved=DecodeRetailUtilityRequest(request,RetailSaveProfile{});
    const auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    Require(owners.progression_words[0].children.at(0).word==0x20000000 &&
            (owners.progress_flags[0].children.at(0).word&7)==(expired?3u:7u),"Expected a completed stay slot");
    auto now=saved.profile.SavedAt();
    now[3]=15;
    now[2]=1;
    if (!IsLoadedTitleClockCurrent(saved.profile,now)) AdvanceTestDate(now);
    std::array<std::uint8_t,16> date{};
    for (unsigned i=0;i<6;++i) {
        date[2*i]=static_cast<std::uint8_t>(now[6-i]);
        date[2*i+1]=static_cast<std::uint8_t>(now[6-i]>>8);
    }
    BootToDisplay boot;
    Require(boot.Initialize() && boot.InitializeClock(date,168) &&
            boot.MountDataArchive(install/"assets/iso/PSP_GAME/USRDIR/data/DATA.BP") &&
            boot.PrepareTitle(),"Could not boot the protected farewell slot");
    const auto step=[&](unsigned buttons=0) {
        if (!boot.AdvanceTitle(buttons))
            throw std::runtime_error(boot.SaveReadBoundary()?boot.SaveReadBoundary():"Departed room update failed");
    };
    for (unsigned i=0;i<670 && boot.Title().menu_variant()!=2;++i) step();
    Require(boot.Title().interactive() && boot.Title().menu_variant()==2,"Missing slot selection");
    for (unsigned i=0;i<12;++i) step();
    step(0x2000);
    bool departed=false, drawn=false;
    for (unsigned i=0;i<160 && !drawn;++i) {
        step();
        for (const auto& e:boot.DepartureEvents()) {
            if (std::string_view(e.name)=="departed-room") departed=true;
            if (std::string_view(e.name)=="view" && e.values.at(0).second==0) drawn=true;
        }
    }
    Require(departed && drawn && boot.Title().title_complete(),"Saved stay did not open the empty room");
    step(0x2000);
    step();
    Require(!boot.RoomConversation().running && !boot.RoomActivity().serial,
            "The empty room started a new contact or activity");
    std::vector<std::uint32_t> before, after;
    Require(boot.Screen().CopyPixels(before),"Cannot copy the empty room");
    const auto wait=[&](unsigned ticks) { while (ticks--) step(); };
    const auto press=[&](unsigned buttons) { step(buttons); step(); };
    press(0x20);
    wait(14);
    Require(boot.RoomTeaching().menu_open && boot.RoomTeaching().menu_cursor==0 &&
            boot.Screen().CopyPixels(after) && before!=after,"Restricted menu did not draw");
    press(0x40);
    Require(boot.RoomTeaching().menu_cursor==1,"Restricted menu did not select the notebook");
    press(0x2000);
    wait(25);
    press(0x40);
    press(0x2000);
    for (unsigned i=0;i<145 && !boot.Notebook().list_ready;++i) step();
    Require(boot.Notebook().list_ready && boot.Notebook().resource==8,"The empty room did not open the word memo");
    const auto actor=saved.profile.ReadProfile().actor;
    const auto& word=owners.dictionary.children.at(0).children.at(163).children.at(0).children;
    if (actor>=1 || linked) press(0x80);
    else for (int tab=2;tab<=5;++tab) press(0x20);
    auto note=boot.Notebook();
    const auto rows=note.count;
    for (unsigned i=1;i<rows && note.row!=163;++i) {
        press(0x40);
        note=boot.Notebook();
    }
    if ((actor>=2 || linked) && word.at(1).text.empty()) {
        Require(note.tab==13 && note.count==0,"The empty room gained a new word");
    } else {
        Require(!word.at(1).text.empty() && note.row==163 && note.written==word.at(1).text &&
                note.reading==word.at(2).text,"The notebook in the empty room lost the learned word");
    }
    press(0x4000);
    wait(80);
    press(0x20);
    wait(14);
    press(0x10);
    Require(boot.RoomTeaching().menu_cursor==3,"Restricted menu does not have the source four rows");
    press(0x2000);
    wait(25);
    press(0x40);
    press(0x40);
    press(0x2000);
    for (unsigned i=0;i<200 && boot.Title().title_complete();++i) step();
    Require(!boot.Title().title_complete() && !boot.RoomTeaching().menu_open,
            "The empty room did not return to the title");
    const auto updated=ReadRetailSlot(slot);
    const auto after_save=DecodeRetailUtilityRequest(updated,RetailSaveProfile{});
    const auto after_owners=RoomSaveOwners::FromActorGraph(after_save.actor);
    Require(SelectLoadedBootRoute(after_save.profile,now)==LoadedBootRoute::title_11 &&
            after_owners.dictionary.word==owners.dictionary.word &&
            EncodeKscSaveGraph(after_owners.dictionary.children.at(0))==EncodeKscSaveGraph(owners.dictionary.children.at(0)) &&
            after_owners.progression_words[0].children.at(0).word==0x20000000,
            "Returning from the empty room changed its slot mode, word data or stay");
    std::cout<<"Continue opened the departed room and word memo, then returned to the title\n";
    return 0;
}

int CheckExpiredStay(const std::filesystem::path& game, const std::filesystem::path& source) {
    using namespace dokodemo::gameplay;
    const auto slot=game/"savedata"/"UCJS10002000";
    const auto before=ReadRetailSlot(source);
    Require(!std::filesystem::equivalent(source,slot) && ReadRetailSlot(slot)==before,
            "Expired-stay test requires a private copy of the input slot");
    const auto input=DecodeRetailUtilityRequest(before,RetailSaveProfile{});
    const auto species=input.profile.ReadProfile().actor;
    const auto owners=RoomSaveOwners::FromActorGraph(input.actor);
    Require(owners.day_words[0].children.at(0).word==0x2000000d &&
            owners.progression_words[0].children.at(0).word==0x2000000e &&
            (owners.progress_flags[0].children.at(0).word&7)==3,"Expected a completed day 13 with one day left");
    auto now=input.profile.SavedAt();
    Require(now[4]+2<=unsigned(ProfileMonthDays(now[6],now[5])),"Expected a same-month expiration fixture");
    now[4]+=2;
    ResumeDayRoom run(game,input,now);
    run.Step();
    bool expired=false;
    std::vector<std::uint8_t> staged;
    for (const auto& e:run.room.departure_events()) {
        if (std::string_view(e.name)=="expired-stay-closed") {
            constexpr std::array sites{0x8e158,0x6c019,0x67efc,0x6c0d1,0x6bee4};
            Require(e.values.at(0).second==sites.at(species),"Wrong expiration branch");
            expired=true;
        } else if (std::string_view(e.name)=="expired-stay-diary") {
            const auto& text=e.text.at(0);
            staged.assign(text.begin(),text.end());
        }
    }
    Require(expired && !staged.empty() && !run.scheduled &&
            !run.room.first_activity() && ReadRetailSlot(slot)==before,"Expiration did not stage the restricted empty room");
    run.OpenMenu();
    run.Key(0x10);
    Require(run.room.teaching_snapshot().menu_cursor==3,"Expired room did not open the restricted System menu");
    run.Key(0x2000);
    run.Rest(30);
    run.Key(0x40);
    run.Key(0x2000);
    run.Until([&]{return run.saves==1;},"Expired room could not write the slot");
    const auto after=DecodeRetailUtilityRequest(ReadRetailSlot(slot),RetailSaveProfile{});
    const auto retained=RoomSaveOwners::FromActorGraph(after.actor);
    const auto& record=retained.diary_staging.children.at(0);
    Require(retained.day_words[0].children.at(0).word==0x2fffffffu &&
            retained.progression_words[0].children.at(0).word==0x20000000u &&
            retained.progress_flags[0].children.at(0).word==owners.progress_flags[0].children.at(0).word &&
            record.children.at(1).word==0x20000009u && record.children.at(3).word==0x20a00000u &&
            EncodeKscSaveGraph(record)==staged && ReadRetailSlot(source)==before,
            "Expired stay did not save its own diary and unchanged prior scene flags");
    return CheckDepartedRoom(game,true);
}

int CheckExtendedReturn(const std::filesystem::path& install) {
    using namespace dokodemo::gameplay;
    const auto slot=install/"savedata"/"UCJS10002000";
    const auto request=ReadRetailSlot(slot);
    const auto saved=DecodeRetailUtilityRequest(request,RetailSaveProfile{});
    const auto species=saved.profile.ReadProfile().actor;
    const auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    Require(owners.progression_words[0].children.at(0).word==0x20000018 &&
            (owners.progress_flags[0].children.at(0).word&15)==7,"Expected the extended farewell slot");
    auto now=saved.profile.SavedAt();
    now[3]=15;
    now[2]=1;
    std::array<std::uint8_t,16> date{};
    for (unsigned i=0;i<6;++i) {
        date[2*i]=static_cast<std::uint8_t>(now[6-i]);
        date[2*i+1]=static_cast<std::uint8_t>(now[6-i]>>8);
    }
    BootToDisplay boot;
    Require(boot.Initialize() && boot.InitializeClock(date,168) &&
            boot.MountDataArchive(install/"assets/iso/PSP_GAME/USRDIR/data/DATA.BP") &&
            boot.PrepareTitle() && boot.Title().title_complete(),
            "Extended stay did not select cold room entry");
    const auto step=[&](unsigned buttons=0) {
        if (!boot.AdvanceTitle(buttons))
            throw std::runtime_error(boot.SaveReadBoundary()?boot.SaveReadBoundary():"Extended room update failed");
    };
    unsigned next=0, events=0, choices=0;
    bool deadline=false, returned=false, saved_again=false;
    std::vector<std::uint8_t> diary;
    for (unsigned i=0;i<(species>=2?8500u:3300u) && !(returned && boot.RoomActivity().started_command);++i) {
        step(next);
        next=0;
        for (const auto& entry : boot.DepartureEvents()) {
            const std::string_view name=entry.name;
            if (name=="scheduled-room-event") {
                Require(entry.values.at(0).second==6 && entry.values.at(1).second==14,"Wrong cold extended event");
                ++events;
            } else if (name=="scheduled-actor-command" && entry.values.at(0).second==85 && !deadline) {
                now[2]+=6;
                boot.SetUpdateCalendar(now);
                deadline=true;
            } else if (name=="scheduled-room-choice") {
                const auto replies=entry.values.at(0).second;
                Require(replies==2 || (species==2 && replies==4),"Unexpected extension answer menu");
                ++choices;
                next=0x2000;
            } else if (name=="return-diary-staged") {
                const auto& bytes=entry.text.at(0);
                diary.assign(bytes.begin(),bytes.end());
            } else if (name=="scheduled-room-return") {
                Require(entry.values.at(0).second==6 && entry.values.at(1).second==14,"Wrong extension callback returned");
                returned=true;
            }
        }
    }
    constexpr unsigned expected_answers[]{4,2,6,1,2};
    std::cout<<"Cold extended event 6 answers="<<choices<<" returned="<<returned<<'\n';
    Require(events==1 && choices==expected_answers[species] && returned && !diary.empty() && boot.RoomActivity().started_command &&
            ReadRetailSlot(slot)==request,"Incomplete event 6 or an unsolicited save");

    const auto wait=[&](unsigned ticks) { while(ticks--) step(); };
    const auto press=[&](unsigned key) { step(key); step(); };
    for (unsigned i=0;i<60 && !boot.RoomTeaching().menu_open;++i) {
        press(0x20);
        wait(8);
    }
    Require(boot.RoomTeaching().menu_open && boot.RoomTeaching().menu_cursor==0,"Missing full room menu after event 6");
    wait(12);
    press(0x10);
    Require(boot.RoomTeaching().menu_cursor==6,"Event 6 left the full menu unavailable");
    press(0x2000);
    wait(24);
    press(0x40);
    press(0x2000);
    for (unsigned i=0;i<150 && !saved_again;++i) {
        step();
        for (const auto& entry : boot.DepartureEvents())
            if (std::string_view(entry.name)=="system-save-written") saved_again=true;
    }
    Require(saved_again,"Event 6 returned to a room which did not save");
    const auto after=DecodeRetailUtilityRequest(ReadRetailSlot(slot),RetailSaveProfile{});
    const auto after_owners=RoomSaveOwners::FromActorGraph(after.actor);
    const auto& record=after_owners.diary_staging.children.at(0);
    Require((after_owners.progress_flags[0].children.at(0).word&15)==15 &&
            after_owners.progression_words[0].children.at(0).word==0x20000018 &&
            record.children.at(1).word==0x20000007 && record.children.at(3).word==0x20900000 &&
            EncodeKscSaveGraph(record)==diary,"Event 6 did not preserve its result in the protected slot");

    ResumeDayRoom restart(install,after,now);
    restart.Start(14,14);
    Require(!restart.scheduled,"Event 6 repeated after its committed save");
    std::cout<<"Cold extended stay ran event 6 and "<<choices
             <<" answers, saved the 24-day limit and resumed without replay\n";
    return 0;
}

int CheckReturnLetter(const std::filesystem::path& install, const std::filesystem::path& donor,
                      unsigned answer, bool live) {
    using namespace dokodemo::gameplay;
    Require(answer<2,"Invalid return-letter answer");
    const auto slot=install/"savedata"/"UCJS10002000";
    Require(!std::filesystem::exists(slot),"Return-letter test requires an empty private slot");
    const auto original=ReadRetailSlot(donor);
    auto input=DecodeRetailUtilityRequest(original,RetailSaveProfile{});
    const auto reader=input.profile.ReadProfile();
    auto sender=reader;
    sender.actor=reader.actor==3 && live ? 1u : (reader.actor+(live?3u:2u))%5;
    Require(sender.nickname.Assign("PreviousActor") && sender.player.Assign("PreviousPlayer"),
            "Invalid test sender profile");
    input.profile.Adopt(sender);
    input.profile.RememberLetterSender();
    input.profile.Adopt(reader);
    input.profile.SetFullMenuMode();
    const auto sender_record=input.profile.ReadLetterSender();
    Require(sender_record && sender_record->actor==sender.actor && sender_record->nickname=="PreviousActor" &&
            sender_record->player=="PreviousPlayer","Farewell sender did not survive the next adoption");

    auto clock=input.profile.SavedAt();
    clock[1]=clock[2]=live?59:0;
    clock[3]=live?23:12;
    auto owners=RoomSaveOwners::FromActorGraph(input.actor);
    const auto midnight=[](const auto& calendar) {
        return dokodemo::recovered::CalendarScalar7C660(
            dokodemo::recovered::CalendarRecord689B8({int(calendar[6]),int(calendar[5]),int(calendar[4]),0,0}));
    };
    owners.day_words[0].children.at(0).word=0x20000003;
    owners.day_words[1].children.at(0).word=0x30000000|midnight(clock);
    owners.progress_flags[0].children.at(0).word=0x20000001;
    owners.progression_words[0].children.at(0).word=0x2000000e;
    owners.progression_words[1].children.at(0).word=0x20000004;
    const RoomMessages empty;
    owners.used_diaries=empty.UsedDescriptorsSaveTable();
    owners.diary_staging=empty.SaveTable();
    input.profile.StampForSave(clock);
    const auto baseline=BuildRetailUtilityRequest(input.profile,owners.ToActorGraph(),input.word_history);
    WriteRetailSlot(slot,install,baseline);
    const auto previous_date=clock;
    if (++clock[4]>static_cast<unsigned>(ProfileMonthDays(clock[6],clock[5]))) {
        clock[4]=1;
        if (++clock[5]>12) { clock[5]=1; ++clock[6]; }
    }
    if(live) clock[1]=clock[2]=clock[3]=0;
    const auto initialize=[&](BootToDisplay& boot,const auto& calendar) {
        std::array<std::uint8_t,16> date{};
        for (unsigned i=0;i<6;++i) {
            date[2*i]=static_cast<std::uint8_t>(calendar[6-i]);
            date[2*i+1]=static_cast<std::uint8_t>(calendar[6-i]>>8);
        }
        Require(boot.Initialize() && boot.InitializeClock(date,168) &&
                boot.MountDataArchive(install/"assets/iso/PSP_GAME/USRDIR/data/DATA.BP") &&
                boot.PrepareTitle() && boot.Title().title_complete(),"Could not cold-load the letter room");
    };
    std::vector<std::uint8_t> staged;
    {
        BootToDisplay boot;
        initialize(boot,live?previous_date:clock);
        const auto step=[&](unsigned buttons=0) {
            if (!boot.AdvanceTitle(buttons))
                throw std::runtime_error(boot.SaveReadBoundary()?boot.SaveReadBoundary():"Return letter update failed");
        };
        unsigned previous_activity=0;
        if(live) {
            for(unsigned i=0;i<1800 && !boot.RoomActivity().started_command;++i) {
                step();
                for(const auto& event:boot.DepartureEvents())
                    Require(std::string_view(event.name)!="scheduled-room-event","Letter started a day early");
            }
            Require(bool(boot.RoomActivity().started_command),"Could not start activity on the prior day");
            previous_activity=boot.RoomActivity().serial;
            boot.SetUpdateCalendar(clock);
        }
        const auto resumed=[&] {
            return boot.RoomActivity().serial>previous_activity && bool(boot.RoomActivity().started_command);
        };
        bool offered=false, navigation=false, moved=false;
        unsigned starts=0, returns=0, choices=0, accepted=0, buttons=0;
        for (unsigned i=0;i<5200 && !(returns && resumed());++i) {
            const auto held=std::exchange(buttons,0u);
            step(held);
            if (held==0x2000 && choices>accepted &&
                boot.RoomConversation().chosen_option==int(choices==2?answer+1:1))
                accepted=choices;
            for (const auto& event:boot.DepartureEvents()) {
                const std::string_view name=event.name;
                if (name=="scheduled-room-event" || name=="scheduled-room-return") {
                    Require(event.values[0].second==7 && event.values[1].second==4,"Wrong scheduled letter day");
                    name=="scheduled-room-event" ? ++starts : ++returns;
                } else if (name=="scheduled-actor-command" && event.values[0].second==79) {
                    offered=true;
                } else if (name=="scheduled-navigation") {
                    navigation=event.values[0].second!=0;
                } else if (name=="scheduled-room-choice") {
                    Require(event.values[0].second==2,"Expected two letter responses");
                    ++choices;
                } else if (name=="letter-diary-staged") {
                    const auto& bytes=event.text.at(0);
                    staged.assign(bytes.begin(),bytes.end());
                }
            }
            if (!held && choices>accepted && boot.RoomConversation().option_count) {
                if (choices==2 && answer && !moved) { buttons=0x40; moved=true; }
                else buttons=0x2000;
            } else if (!held && offered && navigation && choices==0 && i%4==0) {
                buttons=0x2000;
            }
        }
        const auto after_activity=boot.RoomActivity();
        const bool untouched=ReadRetailSlot(slot)==baseline;
        std::cout<<"Letter actor "<<reader.actor<<" "<<(live?"live":"cold")
                 <<" start="<<starts<<" offer="<<offered<<" options="<<choices
                 <<" accepted="<<accepted<<" returned="<<returns<<" activity="<<after_activity.started_command.value_or(0)
                 <<" serial="<<previous_activity<<"->"<<after_activity.serial<<" untouched="<<untouched
                 <<" staged="<<staged.size()<<std::endl;
        Require(starts==1 && offered && choices==2 && accepted==2 && returns==1 && !staged.empty() &&
                resumed() && untouched,"Incomplete letter room or premature save");
        const auto wait=[&](unsigned ticks) { while(ticks--) step(); };
        const auto press=[&](unsigned button) { step(button); step(); };
        for(unsigned i=0;i<80 && !boot.RoomTeaching().menu_open;++i) { press(0x20); wait(8); }
        Require(boot.RoomTeaching().menu_open && boot.RoomTeaching().menu_cursor==0,"Letter did not restore controls");
        wait(12);
        press(0x10);
        Require(boot.RoomTeaching().menu_cursor==6,"Letter room did not expose System");
        press(0x2000); wait(30); press(0x40); press(0x2000);
        bool wrote=false;
        for(unsigned i=0;i<170 && !wrote;++i) {
            step();
            for(const auto& event:boot.DepartureEvents())
                if(std::string_view(event.name)=="system-save-written") wrote=true;
        }
        Require(wrote,"Letter room did not save on request");
    }
    const auto written=ReadRetailSlot(slot);
    const auto readback=DecodeRetailUtilityRequest(written,RetailSaveProfile{});
    const auto restored_sender=readback.profile.ReadLetterSender();
    const auto saved=RoomSaveOwners::FromActorGraph(readback.actor);
    const auto& diary=saved.diary_staging.children.at(0);
    constexpr unsigned descriptors[5][5]{{130,134,131,132,133},{132,131,133,134,135},
        {132,135,131,133,134},{134,137,135,133,136},{131,134,132,133,130}};
    const auto descriptor=descriptors[reader.actor][sender.actor];
    Require(saved.progress_flags[0].children.at(0).word==0x20000081 &&
            saved.progression_words[1].children.at(0).word==0x20000004 &&
            diary.children.at(1).word==(0x20000000|descriptor) && diary.children.at(3).word==0x20500000 &&
            EncodeKscSaveGraph(diary)==staged && diary.children.at(2).text.find("PreviousPlayer")!=std::string::npos &&
            diary.children.at(2).text.find("$_O")==std::string::npos &&
            restored_sender && restored_sender->actor==sender.actor &&
            restored_sender->nickname=="PreviousActor" && restored_sender->player=="PreviousPlayer" &&
            (saved.used_diaries.children.at(0).children.at(descriptor/28).word&(1u<<(descriptor%28))),
            "Protected save lost the letter flags, selected descriptor or formatted text");
    BootToDisplay restarted;
    initialize(restarted,clock);
    for(unsigned i=0;i<1800 && !restarted.RoomActivity().started_command;++i) {
        if(!restarted.AdvanceTitle(0))
            throw std::runtime_error(restarted.SaveReadBoundary()?restarted.SaveReadBoundary():"Letter reload failed");
        for(const auto& event:restarted.DepartureEvents())
            Require(std::string_view(event.name)!="scheduled-room-event","Committed letter replayed after reload");
    }
    Require(restarted.RoomActivity().started_command && written==ReadRetailSlot(slot) && original==ReadRetailSlot(donor),
            "Read-only restart did not restore the room or changed a protected slot");
    std::cout<<"Letter actor "<<reader.actor<<" "<<(live?"live":"cold")
             <<" choice "<<answer+1<<" descriptor "<<descriptor
             <<" saved and cold-resumed without replay\n";
    return 0;
}

int CheckExtendedPenultimate(const std::filesystem::path& install, const std::filesystem::path& donor,
                             bool linked=false) {
    using namespace dokodemo::gameplay;
    const auto destination=install/"savedata"/"UCJS10002000";
    Require(!std::filesystem::exists(destination.parent_path()),"Expected a fresh penultimate test install");
    const auto original=ReadRetailSlot(donor);
    auto saved=DecodeRetailUtilityRequest(original,RetailSaveProfile{});
    const auto species=saved.profile.ReadProfile().actor;
    auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    Require((owners.progress_flags[0].children.at(0).word&15)==15 &&
            owners.day_words[0].children.at(0).word==(linked?0x20000016u:0x2000000eu) &&
            owners.progression_words[0].children.at(0).word==0x20000018,
            "Expected a protected extended stay after event 6");
    auto now=saved.profile.SavedAt();
    if (linked) AdvanceTestDate(now);
    else {
        // The focused event probe stages the prior day. The linked stay
        // arrives with the day counter and midnight from the real loop.
        for (unsigned i=0;i<9;++i) AdvanceTestDate(now);
        owners.day_words[0].children.at(0).word=0x20000016;
        owners.day_words[1].children.at(0).word+=8*1440;
        saved.actor=owners.ToActorGraph();
    }
    now[3]=14;
    now[2]=53;
    ResumeDayRoom run(install,saved,now);
    unsigned next=0, answers=0;
    bool deadline=false, returned=false, secondary=false;
    for (unsigned i=0;i<(species>=2?10000u:3800u) && !(returned && run.room.first_activity());++i) {
        run.Step(next);
        next=0;
        for (const auto& entry:run.room.departure_events()) {
            const std::string_view name=entry.name;
            if (name=="scheduled-room-event" || name=="scheduled-room-return") {
                Require(entry.values.at(0).second==4 && entry.values.at(1).second==23,"Wrong extended penultimate callback");
                returned|=name=="scheduled-room-return";
            } else if (name=="scheduled-actor-command" && entry.values.at(0).second==79 && !deadline) {
                run.calendar[2]=59;
                deadline=true;
            } else if (name=="scheduled-actor-selection" && entry.values.at(0).second==1) {
                secondary=true;
            } else if (name=="scheduled-room-choice") {
                Require(entry.values.at(0).second==2,"Expected an extended penultimate answer");
                ++answers;
                next=0x2000;
            }
        }
    }
    std::cout<<"Extended penultimate event_count="<<run.scheduled<<" day="<<run.previous<<"->"<<run.day
             <<" second_actor="<<secondary<<" choices="<<answers<<" returned="<<returned
             <<" activity="<<run.room.first_activity().value_or(0)<<'\n';
    constexpr unsigned expected_answers[]{4,2,6,3,5};
    Require(run.scheduled==1 && run.previous==22 && run.day==23 && secondary &&
            answers==expected_answers[species] && returned && run.room.first_activity(),"Incomplete extended penultimate callback");
    run.Save();
    const auto restored=DecodeRetailUtilityRequest(ReadRetailSlot(destination),RetailSaveProfile{});
    const auto final_owners=RoomSaveOwners::FromActorGraph(restored.actor);
    Require((final_owners.progress_flags[0].children.at(0).word&31)==31 &&
            final_owners.day_words[0].children.at(0).word==0x20000017 &&
            ReadRetailSlot(donor)==original,"Extended day 23 did not commit independently");
    std::cout<<"Extended day 23 ran event 4 with "<<answers<<" answers, then saved\n";
    return 0;
}

int CheckExtendedLastDay(const std::filesystem::path& install, const std::filesystem::path& donor) {
    using namespace dokodemo::gameplay;
    const auto destination=install/"savedata"/"UCJS10002000";
    Require(!std::filesystem::exists(destination.parent_path()),"Expected a fresh final extension install");
    const auto original=ReadRetailSlot(donor);
    const auto saved=DecodeRetailUtilityRequest(original,RetailSaveProfile{});
    const auto species=saved.profile.ReadProfile().actor;
    const auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    Require((owners.progress_flags[0].children.at(0).word&31)==31 &&
            owners.day_words[0].children.at(0).word==0x20000017 &&
            owners.progression_words[0].children.at(0).word==0x20000018,"Expected completed extended day 23");
    auto now=saved.profile.SavedAt();
    if (++now[4]>static_cast<unsigned>(ProfileMonthDays(now[6],now[5]))) {
        now[4]=1;
        if (++now[5]>12) { now[5]=1; ++now[6]; }
    }
    now[3]=14;
    now[2]=53;
    ResumeDayRoom run(install,saved,now);
    const std::array<std::string_view,4> sequence{
        "farewell-cover-closed","farewell-save-written","farewell-save-acknowledged","scheduled-farewell-complete"};
    unsigned next=0, choices=0, stage=0, motions=0, return_day=0;
    bool deadline=false, cleared=false;
    const unsigned departure_motion=(species==0 || species==3)?283:285;
    std::vector<std::uint8_t> staged;
    for (unsigned i=0;i<(species>=2?17000u:7900u) && !run.room.return_to_title_requested();++i) {
        run.Step(next);
        next=0;
        for (const auto& e : run.room.departure_events()) {
            const std::string_view name=e.name;
            if (name=="scheduled-room-event") {
                Require(e.values.at(0).second==5 && e.values.at(1).second==24,"Wrong extended last-day event");
            } else if (name=="scheduled-actor-command") {
                const auto command=e.values.at(0).second;
                if (command==48 && !deadline) {
                    run.calendar[2]=59;
                    deadline=true;
                }
                if (command==departure_motion || command==departure_motion+1) ++motions;
            } else if (name=="scheduled-room-choice") {
                Require(e.values.at(0).second==2,"Unexpected final extension menu");
                ++choices;
                next=0x2000;
            } else if (name=="farewell-diary-staged") {
                const auto& bytes=e.text.at(0);
                staged.assign(bytes.begin(),bytes.end());
            } else if (name=="scheduled-stay-written") {
                cleared|=e.values.at(0).second==0;
                const auto value=e.values.at(1).second;
                if (value>=3 && value<=10) return_day=unsigned(value);
            } else if (std::find(sequence.begin(),sequence.end(),name)!=sequence.end()) {
                Require(stage<sequence.size() && name==sequence[stage],"Final extension save was out of order");
                if (stage==0)
                    Require(e.values.at(2).second==2 && !std::filesystem::exists(destination),
                            "Final extension used the wrong mode or wrote early");
                if (stage==3)
                    Require(e.values.at(0).second==5,"Wrong event finished the extension");
                std::cout<<name<<" update "<<i<<'\n';
                ++stage;
            }
        }
    }
    std::cout<<"Extended final choices="<<choices<<" farewell motions="<<motions<<'\n';
    constexpr unsigned expected_answers[]{8,1,2,5,1};
    Require(run.scheduled==1 && run.previous==23 && run.day==24 && choices==expected_answers[species] && motions==2 &&
            stage==sequence.size() && cleared && return_day && !staged.empty() &&
            run.room.return_to_title_requested(),"Incomplete final day of the extension");
    const auto final=DecodeRetailUtilityRequest(ReadRetailSlot(destination),RetailSaveProfile{});
    const auto final_owners=RoomSaveOwners::FromActorGraph(final.actor);
    const auto& diary=final_owners.diary_staging.children.at(0);
    const auto id=diary.children.at(1).word;
    Require((final_owners.progress_flags[0].children.at(0).word&63)==63 &&
            final_owners.day_words[0].children.at(0).word==0x20000018 &&
            final_owners.progression_words[0].children.at(0).word==0x20000000 &&
            final_owners.progression_words[1].children.at(0).word==(0x20000000|return_day) &&
            (id==0x2000000a || id==0x2000000b) && diary.children.at(3).word==0x20800000 &&
            diary.children.at(2).text.find("$_YOU")==std::string::npos &&
            EncodeKscSaveGraph(diary)==staged && ReadRetailSlot(donor)==original &&
            SelectLoadedBootRoute(final.profile,run.calendar)==LoadedBootRoute::title_11,
            "Final extended save failed its diary and title round trip");
    std::cout<<"Event 5 ended the extended stay, wrote a protected slot and selected the cold title\n";
    return 0;
}

int CheckExtendedDays(const std::filesystem::path& game, const std::filesystem::path& baseline) {
    CheckFinalDaySlot(game,baseline,true);
    CheckExtendedReturn(game);
    const auto later_install=[&](const char* name) {
        const auto path=game/name;
        std::filesystem::create_directory(path);
        std::filesystem::copy(game/"assets",path/"assets",
            std::filesystem::copy_options::recursive | std::filesystem::copy_options::create_hard_links);
        return path;
    };
    const auto eve=later_install("day-23");
    const auto last=later_install("day-24");
    CheckExtendedPenultimate(eve,game/"savedata"/"UCJS10002000");
    CheckExtendedLastDay(last,eve/"savedata"/"UCJS10002000");
    return CheckDepartedRoom(last);
}
