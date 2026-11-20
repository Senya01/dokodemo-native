struct ResumeDayRoom {
    using Calendar = dokodemo::gameplay::RetailSaveProfile::Calendar;
    Calendar calendar;
    PostProfile room;
    dokodemo::recovered::PackageView font;
    dokodemo::recovered::RngNativeOwner rng;
    std::vector<std::uint8_t> frame = std::vector<std::uint8_t>(2*1024*1024);
    unsigned frames=0, saves=0, clears=0, restored=0, scheduled=0;
    int previous=-1, day=-1;

    ResumeDayRoom(const std::filesystem::path& install, const dokodemo::gameplay::RetailUtilityContents& saved,
                  const Calendar& test_calendar)
        : calendar(test_calendar), font(RoomFont(install)) {
        std::string error;
        rng.ApplyInitializerTimeT32(168,1,"component calendar room");
        if (!room.Load(install,saved,calendar,error)) throw std::runtime_error(error);
    }
    void Step(unsigned buttons=0) {
        if (!room.Draw(frame,(frames++%2)*surface,buttons,&font,&rng,&calendar))
            throw std::runtime_error(room.last_error());
        for (const auto& event:room.departure_events()) {
            const std::string_view name=event.name;
            if (name=="room-load-restored") {
                ++restored;
                for (const auto& [field,value]:event.values) {
                    if (std::string_view(field)=="day") day=value;
                    if (std::string_view(field)=="previous_day") previous=value;
                }
            }
            if (name=="diary-staging-clear") ++clears;
            if (name=="scheduled-room-event") ++scheduled;
            if (name=="system-save-written") ++saves;
            if (name=="system-save-unavailable")
                throw std::runtime_error(event.text.empty()?"System save failed":event.text.front());
            Require(name!="gift-received" && name!="diary-created","Later room replayed introductory events");
        }
    }
    void Rest(unsigned ticks) { while (ticks--) Step(); }
    void Key(unsigned key) { Step(); Step(key); Rest(16); }
    template<class F> void Until(F predicate,const char* error) {
        for (unsigned i=0;i<1800 && !predicate();++i) Step();
        if (!predicate()) throw std::runtime_error(error);
    }
    void Start(int source_day, int expected_day) {
        Until([&]{return room.first_activity().has_value();},"Restored room never started activity");
        Require(room.introduction_finished() && day==expected_day && previous==source_day && restored==1 &&
                clears==unsigned(source_day!=expected_day),"Room calendar entry or daily reset differs");
    }
    void OpenMenu() {
        for (unsigned i=0;i<80 && !room.teaching_snapshot().menu_open;++i) Key(0x20);
        Require(room.teaching_snapshot().menu_open && room.teaching_snapshot().menu_cursor==0,"Home menu did not open");
        Rest(30);
    }
    void OpenSection(int col,int resource) {
        OpenMenu();
        Key(0x40); Key(0x40);
        Require(room.teaching_snapshot().menu_cursor==3,"Notebook menu row");
        Key(0x2000); Rest(30);
        for (int i=0;i<col;++i) Key(0x40);
        Require(room.notebook_snapshot().menu_column==col,"Notebook submenu row");
        Key(0x2000);
        Until([&]{const auto state=room.notebook_snapshot();return state.resource==resource && state.list_ready;},
              "Notebook did not finish transition");
    }
    void CloseSection() {
        Key(0x4000);
        Until([&]{return !room.notebook_snapshot().visible;},"Notebook did not close");
        Rest(40);
    }
    void CheckWord() {
        OpenSection(1,8);
        for (int tab=2;tab<=5;++tab) {
            Key(0x20);
            Require(room.notebook_snapshot().tab==tab,"Memo tab did not change");
        }
        const auto row=room.notebook_snapshot();
        const std::string natsume="\x82\xc8\x82\xc2\x82\xdf";
        Require(row.row==163 && row.written==natsume && row.reading==natsume,"Restored lesson differs in notebook");
        CloseSection();
    }
    void RevealDiary() {
        OpenSection(2,12);
        auto page=room.notebook_snapshot();
        Require(page.count==2 && page.selection==1 && !page.detail,"Expected today's empty diary candidate");
        Key(0x80);
        Until([&]{const auto p=room.notebook_snapshot();return p.list_ready && p.detail && p.selection==0;},
              "Previous day's diary entry stayed hidden");
        CloseSection();
    }
    void Save() {
        OpenMenu(); Key(0x10);
        Require(room.teaching_snapshot().menu_cursor==6,"System menu row");
        Key(0x2000); Rest(30); Key(0x40); Key(0x2000);
        Until([&]{return saves==1;},"System did not write a slot");
    }
};



int CheckNamedContactSlot(const std::filesystem::path& game,const std::filesystem::path& input,
                          unsigned fixture_seed) {
    using namespace dokodemo::gameplay;
    const auto saved=DecodeRetailUtilityRequest(ReadRetailSlot(input),RetailSaveProfile{});
    const auto actor=saved.profile.ReadProfile().actor;
    Require(actor>=2 && actor<=4,"Named contact needs an alternate actor slot");
    const auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    const auto day=owners.day_words[0].children.at(0).word&0x0fffffff;
    auto clock=saved.profile.SavedAt();
    clock[1]=clock[2]=0;
    clock[3]=12;
    ResumeDayRoom run(game,saved,clock);
    run.Start(day,day);
    run.Until([&]{return run.room.activity_snapshot().actor_wait_released;},"No idle actor state for contact");
    run.rng.ApplyInitializerTimeT32(fixture_seed,1,"Controlled named contact selection");
    run.Step();
    run.Step(0x2000);
    unsigned replies=0,letters=0,last_pc=0;
    bool answered=false,completed=false;
    for (unsigned i=0;i<4400 && !completed;++i) {
        const auto state=run.room.conversation_snapshot();
        if(state.boundary_pc) throw std::runtime_error("Named contact source boundary "+std::to_string(state.boundary_pc));
        if (state.message_pc!=last_pc && !state.message.empty()) {
            ++letters;
            last_pc=state.message_pc;
            std::cout << "Actor " << actor << " message " << std::hex << last_pc
                      << " topic " << state.indirect_topic << std::dec << '\n';
        }
        if(!state.option_count) answered=false;
        const bool press=state.option_count && !answered;
        if(press) { answered=true; ++replies; }
        run.Step(press?0x2000:0);
        completed=run.room.conversation_snapshot().completed;
    }
    const auto final=run.room.conversation_snapshot();
    if (!completed || !final.serial || letters<=1 || (actor==4 && !replies))
        throw std::runtime_error("Named contact incomplete: serial "+std::to_string(final.serial)+
            " messages "+std::to_string(letters)+" answers "+std::to_string(replies)+
            " return "+std::to_string(completed));
    const auto prev=run.room.activity_snapshot().serial;
    run.Until([&]{
        const auto state=run.room.activity_snapshot();
        return state.serial>prev && state.started_command.has_value();
    },"Named actor did not resume his activity");
    std::cout << "Actor " << actor << " day " << day << " messages " << letters << " answers " << replies
              << " home activity " << *run.room.activity_snapshot().started_command << '\n';
    return 0;
}

int CheckDestinationSlot(const std::filesystem::path& install,const std::filesystem::path& input,
                         unsigned scene) {
    using namespace dokodemo::gameplay;
    Require((scene>=1 && scene<=13) || scene==17 || scene==18,
            "The ordinary destination roster does not include this scene");
    const auto original=ReadRetailSlot(input);
    auto saved=DecodeRetailUtilityRequest(original,RetailSaveProfile{});
    Require(saved.profile.ReadProfile().actor==0 && !std::filesystem::exists(install/"savedata"),
            "The outing test needs a Toro slot and an isolated installation without saves");
    auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    const auto day=owners.day_words[0].children.at(0).word&0x0fffffff;
    // Use a private copy of the saved learned word for destinations whose
    // NEKO predicates require an eligible word of a different category.
    auto& word=owners.dictionary.children.at(0).children.at(163).children.at(0).children;
    if (scene==5 || scene==6 || scene==10 || scene==13) {
        Require(!word.at(1).text.empty(),"Expected a learned word in the fixture");
        word.at(0).word=0x20000024u;
        word.at(7).word=0x20000008u;
        word.at(8).word=0x20000004u;
        word.at(11).word=0x20000000u;
        word.at(12).word=0x20000006u;
    }
    owners.progress_flags[1].children.at(0).word=0x20000000u|(1u<<scene);
    saved.actor=owners.ToActorGraph();
    auto clock=saved.profile.SavedAt();
    clock[1]=clock[2]=0;
    clock[3]=scene==7?23:12;
    ResumeDayRoom run(install,saved,clock);
    run.Start(day,day);
    const auto initial_serial=run.room.activity_snapshot().serial;
    auto field=[](const RoomDepartureEvent& event,std::string_view name,int fallback=-1) {
        for (const auto& [key,value]:event.values)
            if (name==key) return value;
        return fallback;
    };
    bool arrived=false,handed_off=false,returned=false,home_control=false;
    unsigned model=0,away=0,away_serial=0;
    auto step=[&](unsigned key=0) {
        run.Step(key);
        for (const auto& event:run.room.departure_events()) {
            const std::string_view name=event.name;
            if (name=="consumed" && field(event,"scene")==int(scene)) {
                model=unsigned(field(event,"resource",0));
                Require(model>=30+3*scene && model<=32+3*scene,"Wrong adopted scene model");
                arrived=true;
            }
            if (name=="control" && field(event,"scene")==int(scene)) handed_off=true;
            if (name=="consumed" && field(event,"scene")==0) returned=true;
            if (name=="return" && field(event,"scene")==0) home_control=true;
        }
        const auto status=run.room.conversation_snapshot();
        if (status.boundary_pc)
            throw std::runtime_error("Destination source boundary "+std::to_string(status.boundary_pc));
        const auto activity=run.room.activity_snapshot();
        if (handed_off && !returned && activity.inputs.scene==scene && activity.started_command &&
            activity.serial>initial_serial) {
            away=*activity.started_command;
            away_serial=activity.serial;
        }
    };
    auto key=[&](unsigned button) {
        step();step(button);
        for(unsigned i=0;i<16;++i) step();
    };
    auto until=[&](auto ready,const char* error) {
        for (unsigned i=0;i<1800 && !ready();++i) step();
        Require(ready(),error);
    };
    run.OpenMenu();
    key(0x40);
    Require(run.room.teaching_snapshot().menu_cursor==1,"Missing outing menu entry");
    key(0x2000);
    until([&]{return run.room.conversation_snapshot().option_count>=2;},"No eligible destination choice");
    key(0x2000);
    until([&]{return handed_off && away;},"No active destination handoff and ASC");
    const auto image=Visible(run.frame,((run.frames-1)%2)*surface);
    Require(arrived && image.visible>1000,"Destination was not drawn");
    for (unsigned i=0;i<80 && !run.room.teaching_snapshot().menu_open;++i) key(0x20);
    Require(run.room.teaching_snapshot().menu_cursor==0,"Away menu did not open");
    for(unsigned i=0;i<30;++i) step();
    key(0x40);
    Require(run.room.teaching_snapshot().menu_cursor==2,"The away menu did not select return home");
    key(0x2000);
    until([&]{return home_control;},"No source return from destination");
    until([&]{
        const auto activity=run.room.activity_snapshot();
        return activity.inputs.scene==0 && activity.started_command.has_value() && activity.serial>away_serial;
    },"Returned room did not resume its own activity");
    Require(returned && ReadRetailSlot(input)==original && !std::filesystem::exists(install/"savedata"),
            "No home scene adoption or the outing modified save data");
    const auto active=run.room.activity_snapshot();
    std::cout << "Destination " << scene << " model " << model << " RGB " << image.visible
              << " ASC " << away << " then home ASC " << *active.started_command
              << ", activity serials " << initial_serial << " -> " << away_serial << " -> " << active.serial
              << " at update " << run.frames << '\n';
    return 0;
}

int CheckNextDaySlot(const std::filesystem::path& install,const std::filesystem::path& original) {
    using namespace dokodemo::gameplay;
    namespace fs=std::filesystem;
    const auto input=fs::canonical(original);
    const auto directory=fs::canonical(install);
    const auto out=directory/"savedata"/"UCJS10002000";
    Require(input.filename()=="UCJS10002000" && !fs::exists(out.parent_path()) &&
            !fs::is_symlink(fs::symlink_status(out.parent_path())),"Supply a fresh separate installation without savedata");
    const auto original_bytes=ReadRetailSlot(input);
    const auto first=DecodeRetailUtilityRequest(original_bytes,RetailSaveProfile{});
    const auto date=first.profile.SavedAt();
    const auto before=RoomSaveOwners::FromActorGraph(first.actor);
    const auto day_word=before.day_words[0].children.at(0).word;
    const auto original_page=first.profile.FirstDayDiary();
    Require(date[5]==11 && date[6]==2026 && original_page &&
            ((date[4]==19 && day_word==0x20000001 && !original_page->revealed) ||
             (date[4]==20 && day_word==0x20000002 && original_page->revealed)),
            "Expected the private Nov 19 or 20 lesson slot");
    const int source_day=int(day_word & 0x0fffffff);
    const int next_day=source_day+1;
    const ResumeDayRoom::Calendar clock{0,22,53,14,date[4]+1,date[5],date[6]};
    Require(fs::create_directory(out.parent_path()) && fs::create_directory(out),"Cannot create exclusive test slot");
    for (const char* name:{"PARAM.SFO","DATA.BIN","ICON0.PNG","ICON1.PMF","PIC1.PNG","SND0.AT3"})
        fs::copy_file(input/name,out/name);
    const auto first_private=DecodeRetailUtilityRequest(ReadRetailSlot(out),RetailSaveProfile{});
    {
        ResumeDayRoom room(directory,first_private,clock);
        room.Start(source_day,next_day); room.CheckWord(); room.RevealDiary(); room.Save();
    }
    const auto restored=DecodeRetailUtilityRequest(ReadRetailSlot(out),RetailSaveProfile{});
    const auto after=RoomSaveOwners::FromActorGraph(restored.actor);
    const auto page=restored.profile.FirstDayDiary();
    Require(after.day_words[0].children.at(0).word==0x20000000u+unsigned(next_day) &&
            after.day_words[1].children.at(0).word==before.day_words[1].children.at(0).word+1440,
            "System did not persist the updated day counter and midnight");
    Require(page && page->revealed && page->date==original_page->date && page->text==original_page->text,
            "System did not preserve the revealed original diary entry");
    const auto& staging=after.diary_staging.children.at(0).children;
    const auto& counter=after.message_counter.children.at(0).children;
    Require(staging.size()==7 && std::all_of(staging.begin(),staging.end(),[](const auto& v){return v.word==0;}) &&
            counter.at(0).word==0x20000000 &&
            counter.at(3).word==after.day_words[1].children.at(0).word+clock[3]*60+clock[2],
            "System save lost the daily message reset or its actual minute");
    Require(EncodeKscSaveGraph(after.dictionary.children.at(0))==EncodeKscSaveGraph(before.dictionary.children.at(0)),
            "Day transition changed the saved dictionary");
    ResumeDayRoom independent(directory,restored,clock);
    Require(restored.profile.SavedAt()==independent.calendar,"Second slot did not contain component save time");
    independent.Start(next_day,next_day); independent.CheckWord(); independent.RevealDiary();
    Require(ReadRetailSlot(input)==original_bytes,"Source private slot changed");
    std::cout<<"component calendar Nov "<<date[4]<<"->"<<clock[4]<<", day "<<source_day<<"->"<<next_day
             <<", notebook row 163 and both strings, diary empty then revealed, System save, "
             <<"fresh room from protected bytes day "<<next_day<<"->"<<next_day<<", activity ASC "
             <<*independent.room.first_activity()<<"\n";
    return 0;
}

void RequirePenultimateInput(const dokodemo::gameplay::RoomSaveOwners& owners) {
    Require(owners.day_words[0].children.at(0).word==0x20000002 &&
            owners.progression_words[0].children.at(0).word==0x2000000e &&
            !(owners.progress_flags[0].children.at(0).word&2),"Expected the private day-two slot before the farewell");
}

int MakePenultimateWindowSlot(const std::filesystem::path& install, const std::filesystem::path& original) {
    using namespace dokodemo::gameplay;
    const auto output=std::filesystem::canonical(install)/"savedata"/"UCJS10002000";
    Require(original.filename()=="UCJS10002000" && !std::filesystem::exists(output.parent_path()) &&
            !std::filesystem::is_symlink(std::filesystem::symlink_status(output.parent_path())),
            "Supply a fresh separate installation without savedata");
    const auto saved=DecodeRetailUtilityRequest(ReadRetailSlot(original),RetailSaveProfile{});
    auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    RequirePenultimateInput(owners);
    // Keep the authenticated slot's real calendar and start the window at its event boundary.
    owners.day_words[0].children.at(0).word=0x2000000d;
    WriteRetailSlot(output,install,BuildRetailUtilityRequest(saved.profile,owners.ToActorGraph()));
    return 0;
}

int CheckPenultimateSlot(const std::filesystem::path& install, const std::filesystem::path& original,
                         bool press_circle) {
    using namespace dokodemo::gameplay;
    namespace fs=std::filesystem;
    const auto input=fs::canonical(original);
    const auto directory=fs::canonical(install);
    const auto output=directory/"savedata"/"UCJS10002000";
    Require(input.filename()=="UCJS10002000" && !fs::exists(output.parent_path()) &&
            !fs::is_symlink(fs::symlink_status(output.parent_path())),
            "Supply a fresh separate installation without savedata");
    const auto original_bytes=ReadRetailSlot(input);
    auto saved=DecodeRetailUtilityRequest(original_bytes,RetailSaveProfile{});
    auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    RequirePenultimateInput(owners);
    const auto original_page=saved.profile.FirstDayDiary();
    owners.day_words[0].children.at(0).word=0x2000000c;
    saved.actor=owners.ToActorGraph();
    const auto date=saved.profile.SavedAt();
    const auto choice=0x2000u;
    ResumeDayRoom run(directory,saved,{0,0,53,14,date[4]+1,date[5],date[6]});
    unsigned navigation_on=0, navigation_off=0, offers=0, answers=0;
    unsigned event_cues=0, circle_cues=0, returns=0;
    std::vector<int> selected, primary, secondary;
    unsigned buttons=0;
    for (unsigned update=0;update<4200 && !(returns && run.room.first_activity());++update) {
        run.Step(buttons);
        buttons=0;
        for (const auto& cue:run.room.requested_effects()) {
            if (cue.bank!=5 || !cue.scaled) continue;
            if (cue.group==1 && cue.sequence==8 && cue.volume==96) ++event_cues;
            if (!answers && cue.group==0 && cue.sequence==4 && cue.volume==128) ++circle_cues;
        }
        for (const auto& event:run.room.departure_events()) {
            const std::string_view name=event.name;
            if (name=="scheduled-room-event" || name=="scheduled-room-return") {
                Require(event.values[0].second==2 && event.values[1].second==13,"Wrong scheduled event");
                if (name=="scheduled-room-return") ++returns;
            } else if (name=="scheduled-navigation") {
                if (event.values[0].second) ++navigation_on;
                else ++navigation_off;
            } else if (name=="scheduled-actor-command") {
                const auto command=event.values[0].second;
                (event.values[1].second?secondary:primary).push_back(command);
                if (command==79 && !navigation_off && ++offers==1 && !press_circle) run.calendar[2]=59;
            } else if (name=="scheduled-actor-selection") {
                selected.push_back(event.values[0].second);
            } else if (name=="scheduled-room-choice") {
                Require(event.values[0].second==2,"Scheduled question did not have two options");
                ++answers;
                buttons=choice;
            }
        }
        if (press_circle && offers && !navigation_off && !circle_cues && update%4==0) buttons=choice;
    }
    std::cout<<"event "<<run.scheduled<<", day "<<run.previous<<"->"<<run.day<<", "
             <<(press_circle?"Circle request":"deadline")<<", navigation "<<navigation_on<<"/"<<navigation_off
             <<", offers "<<offers<<", choices "<<answers<<", returns "<<returns<<", event cues "<<event_cues
             <<", Circle cues "<<circle_cues<<", actor switches";
    for (const auto record:selected) std::cout<<" "<<record;
    std::cout<<", primary ASC";
    for (const auto command:primary) std::cout<<" "<<command;
    std::cout<<", secondary ASC";
    for (const auto command:secondary) std::cout<<" "<<command;
    std::cout<<", room ASC "<<run.room.first_activity().value_or(0)<<", update "<<run.frames<<std::endl;
    Require(run.day==13 && run.previous==12 && run.clears==1 && run.scheduled==1 &&
            navigation_on==1 && navigation_off==1 && offers==1 && answers==4 && returns==1 &&
            event_cues==1 && circle_cues==unsigned(press_circle) &&
            run.room.introduction_finished() && run.room.first_activity().has_value() &&
            selected==std::vector<int>{1,0,1,0,1,0,1,0} && secondary==std::vector<int>{280,39,281,282} &&
            !primary.empty() && primary.back()==74,"Incomplete scheduled event, actor commands, or room return");

    run.CheckWord();
    run.Save();
    Require(std::distance(fs::directory_iterator(output),fs::directory_iterator{})==6,
            "System did not produce the six expected slot members");
    const auto saved_again=DecodeRetailUtilityRequest(ReadRetailSlot(output),RetailSaveProfile{});
    const auto after=RoomSaveOwners::FromActorGraph(saved_again.actor);
    Require(after.day_words[0].children.at(0).word==0x2000000d &&
            after.progress_flags[0].children.at(0).word==(owners.progress_flags[0].children.at(0).word|2u) &&
            saved_again.profile.SavedAt()==run.calendar,"System save lost the event outcome");
    const auto page=saved_again.profile.FirstDayDiary();
    Require(original_page && page && page->revealed && page->date==original_page->date &&
            page->text==original_page->text,"System save lost the first diary page");
    ResumeDayRoom independent(directory,saved_again,run.calendar);
    independent.Start(13,13);
    independent.CheckWord();
    Require(independent.scheduled==0 && ReadRetailSlot(input)==original_bytes,
            "Completed event started again or input slot changed");
    std::cout<<"System save bit 2, six protected slot files, reload 13->13 and ASC "
             <<*independent.room.first_activity()<<", notebook row 163, no event replay\n";
    return 0;
}
