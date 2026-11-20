int CheckReplacementSlot(const std::filesystem::path& install,const std::filesystem::path& source) {
    using namespace dokodemo::gameplay;
    namespace fs=std::filesystem;
    constexpr unsigned circle=0x2000, cross=0x4000, right=0x20, down=0x40;
    const auto slot=install/"savedata"/"UCJS10002000";
    Require(fs::canonical(install)!=fs::canonical(source.parent_path().parent_path()) &&
            !fs::exists(slot.parent_path()),"Replacement test needs its own installation");
    const auto original_bytes=ReadRetailSlot(source);
    auto original=DecodeRetailUtilityRequest(original_bytes,RetailSaveProfile{});
    auto old=RoomSaveOwners::FromActorGraph(original.actor);
    Require(old.progression_words[0].children.at(0).word==0x20000000u &&
            (old.progress_flags[0].children.at(0).word&7)==7u,"Expected a completed stay");
    auto& old_rows=old.dictionary.children.at(0).children;
    const auto row_fields=[](const KscSaveValue& row) -> const std::vector<KscSaveValue>& {
        return row.children.at(0).children;
    };
    const auto& word=row_fields(old_rows.at(163));
    Require(original.profile.ReadProfile().actor==0 && !word.at(1).text.empty() && !word.at(2).text.empty() &&
            original.profile.MainDiaryCount()>0,"Expected a departed Toro fixture with a real lesson and diary");
    // This controlled record exercises all 30 fields of the inherited friend row.
    // No exchange session is implied by how the fixture was prepared.
    auto friend_record=old_rows.at(600);
    auto& card=friend_record.children.at(0).children;
    // The receipt path clears the sender's own-card bit.
    card.at(0).word&=~2u;
    card.at(1)={0x50000000u,"FRIEND",{}};
    card.at(2)={0x50000000u,"\x82\xd3\x82\xea\x82\xf1\x82\xc7",{}};
    card.at(11).word=0x20000003u;
    card.at(21)={0x50000000u,"POKEPI",{}};
    card.at(25)={0x50000000u,"456",{}};
    old_rows.at(601)=friend_record;
    original.profile.SetOptions({3,2,1});
    const auto staged=BuildRetailUtilityRequest(original.profile,old.ToActorGraph());
    WriteRetailSlot(slot,install,staged);
    CheckDepartedRoom(install);
    const auto prior=ReadRetailSlot(slot);
    const auto old_profile=original.profile.ReadProfile();
    auto calendar=DecodeRetailUtilityRequest(prior,RetailSaveProfile{}).profile.SavedAt();
    AdvanceTestDate(calendar);
    calendar[3]=14;
    calendar[2]=5;
    const auto prepare=[&](BootToDisplay& boot) {
        std::array<std::uint8_t,16> date{};
        for (unsigned i=0;i<6;++i) {
            date[2*i]=std::uint8_t(calendar[6-i]);
            date[2*i+1]=std::uint8_t(calendar[6-i]>>8);
        }
        Require(boot.Initialize() && boot.InitializeClock(date,168) &&
                boot.MountDataArchive(install/"assets/iso/PSP_GAME/USRDIR/data/DATA.BP") && boot.PrepareTitle(),
                boot.SaveReadBoundary()?boot.SaveReadBoundary():"Replacement slot could not start");
    };
    const auto step=[](BootToDisplay& boot,unsigned buttons=0) {
        if (!boot.AdvanceTitle(buttons))
            throw std::runtime_error(boot.SaveReadBoundary()?boot.SaveReadBoundary():"Replacement update failed");
    };
    auto boot_owner=std::make_unique<BootToDisplay>();
    auto& boot=*boot_owner;
    prepare(boot);
    const auto rest=[&](unsigned ticks) { while (ticks--) step(boot); };
    const auto key=[&](unsigned buttons) { step(boot,buttons); step(boot); };
    const auto until=[&](auto predicate,const char* error,unsigned ticks=750) {
        while (ticks-- && !predicate()) step(boot);
        Require(predicate(),error);
    };
    const auto on_panel=[&](unsigned panel) { return boot.Title().interactive() && boot.Title().panel()==panel; };
    const auto select_menu=[&] { return on_panel(0) && boot.Title().menu_variant()==2; };
    until(select_menu,"Existing slot did not appear in the title menu");
    rest(4);
    key(down);
    const auto open_prompt=[&] {
        Require(boot.Title().menu_variant()==3,"New adoption row did not open");
        key(circle);
        rest(20);
        Require(boot.Title().overwrite_requested() && !boot.Title().replacing_slot(),"Replacement dialog did not own input");
    };
    open_prompt();
    key(right);
    key(circle);
    until([&]{return !boot.Title().overwrite_requested();},"No did not close the replacement confirmation");
    Require(on_panel(0) && boot.Title().menu_variant()==3 && ReadRetailSlot(slot)==prior,
            "Declining replacement changed the selected row or old slot");
    open_prompt();
    key(circle);
    until([&]{return on_panel(2);},"Yes did not open character selection");
    key(cross);
    until([&]{return on_panel(0) && boot.Title().menu_variant()!=1;},
          "Cancel did not restore the two title choices");
    rest(4);
    Require(!boot.Title().replacing_slot() && ReadRetailSlot(slot)==prior,
            "Cancel in character selection modified the slot");
    std::cout<<"Replacement No and carousel cancel preserved the authenticated slot"<<std::endl;
    if (boot.Title().menu_variant()==2) key(down);
    open_prompt();
    key(circle);
    until([&]{return on_panel(2);},"Cannot reopen replacement character selection");
    key(right);
    until([&]{return boot.Title().selected_actor()==1 && boot.Title().actor_x(1)==0;},
          "Cannot select Robo after Toro");
    key(circle);
    until([&]{return on_panel(3);},"Cannot open the new nickname editor");
    const auto finish_name=[&] { rest(12); key(0x8000); key(circle); rest(12); };
    finish_name(); // Keep the character's original nickname.
    until([&]{return on_panel(4) && boot.Title().profile_present_mask()==1;},"Cannot open the new profile");
    rest(12);
    Require(boot.CommitNickname("NEW"),"Cannot enter the replacement player name");
    finish_name();
    until([&]{return boot.Title().gender_list_visible();},"Missing new profile gender list");
    rest(12); key(circle); rest(34);
    for (unsigned i=0;i<3;++i) { key(circle); rest(24); }
    until([&]{return boot.Title().blood_list_visible();},"Missing new profile blood list");
    rest(12); key(circle); rest(26);
    Require(boot.CommitNickname("123"),"Cannot enter the new telephone number");
    finish_name();
    until([&]{return boot.Title().profile_present_mask()==127;},"New profile did not complete");
    rest(24); key(circle);
    until([&]{return boot.Title().title_complete();},"New profile did not enter the room");
    unsigned pressed=0;
    bool contacted=false, closed_bag=false;
    for (unsigned i=0;i<6300 && !boot.RoomActivity().started_command;++i) {
        const auto state=boot.RoomConversation();
        const auto bag=boot.RoomBag();
        unsigned buttons=0;
        if (!pressed && state.invitation_ready && !contacted) { buttons=circle; contacted=true; }
        else if (!pressed && state.option_count) buttons=circle;
        else if (!pressed && bag.list_ready && !closed_bag) { buttons=cross; closed_bag=true; }
        step(boot,buttons);
        pressed=buttons;
    }
    Require(contacted && closed_bag && boot.RoomActivity().started_command && ReadRetailSlot(slot)==prior,
            "New Robo did not finish its introduction or it saved without a request");
    const auto open_home_menu=[&](BootToDisplay& current) {
        for (unsigned i=0;i<90 && !current.RoomTeaching().menu_open;++i) {
            step(current,right);
            for (unsigned j=0;j<11;++j) step(current);
        }
        Require(current.RoomTeaching().menu_open && current.RoomTeaching().menu_cursor==0,
                "New room did not open the main menu");
        for (unsigned j=0;j<24;++j) step(current);
    };
    const auto open_notebook=[&](BootToDisplay& current,bool memo) {
        open_home_menu(current);
        const auto press=[&](unsigned buttons) { step(current,buttons); step(current); };
        press(down); press(down);
        Require(current.RoomTeaching().menu_cursor==3,"New room did not select the notebook category");
        press(circle);
        for (unsigned j=0;j<30;++j) step(current);
        if (memo) press(down);
        press(circle);
        const auto ready=[&]{return current.Notebook().list_ready && current.Notebook().resource==(memo?8:9);};
        for (unsigned j=0;j<750 && !ready();++j) step(current);
        Require(ready(),"New room did not open the requested notebook section");
    };
    open_notebook(boot,true);
    key(0x80);
    const auto memo=boot.Notebook();
    Require(memo.row==163 && memo.written==word.at(1).text && memo.reading==word.at(2).text,
            "Replacement lost the old learned word in the live notebook");
    key(cross); rest(70);

    const auto save=[&] {
        open_home_menu(boot); key(0x10); key(circle); rest(30); key(down); key(circle);
        bool written=false;
        for (unsigned i=0;i<250 && !written;++i) {
            for (const auto& event:boot.DepartureEvents()) {
                if (std::string_view(event.name)=="system-save-written") written=true;
                if (std::string_view(event.name)=="system-save-unavailable")
                    throw std::runtime_error(event.text.empty()?"Replacement save failed":event.text.front());
            }
            if (!written) step(boot);
        }
        Require(written,"System menu did not write replacement slot");
    };
    save();
    const auto replacement_bytes=ReadRetailSlot(slot);
    const auto replacement=DecodeRetailUtilityRequest(replacement_bytes,RetailSaveProfile{});
    const auto profile=replacement.profile.ReadProfile();
    const auto new_owners=RoomSaveOwners::FromActorGraph(replacement.actor);
    const auto& new_rows=new_owners.dictionary.children.at(0).children;
    const auto& new_word=row_fields(new_rows.at(163));
    card.at(11).word=0x20000001u;
    Require(profile.actor==1 && profile.player.text()=="NEW" && profile.packed_birthday!=0 &&
            profile.nickname.text()!=old_profile.nickname.text() && replacement.profile.Options()==std::array<int,3>{3,2,1} &&
            replacement.profile.MainDiaryCount()==1 && new_owners.day_words[0].children.at(0).word==0x20000001 &&
            new_owners.progression_words[0].children.at(0).word==0x2000000e,
            "Slot did not store new profile, day one, or retained options");
    Require(new_word.at(1).text==word.at(1).text && new_word.at(2).text==word.at(2).text &&
            EncodeKscSaveGraph(new_rows.at(601).children.at(0))==EncodeKscSaveGraph(friend_record.children.at(0)) &&
            row_fields(new_rows.at(600)).at(1).text=="NEW",
            "Learned word, inherited friend card, or fresh own card differs");
    std::cout<<"Toro to Robo, row 163, friend row 601 (30 fields), options, fresh diary and day one saved"<<std::endl;

    ++calendar[2];
    boot_owner.reset();
    auto cold_owner=std::make_unique<BootToDisplay>();
    auto& cold=*cold_owner;
    prepare(cold);
    Require(cold.Title().title_complete(),"Replacement save did not select a cold room");
    unsigned restarts=0;
    for (unsigned i=0;i<1800 && !cold.RoomActivity().started_command;++i) {
        step(cold);
        for (const auto& e:cold.DepartureEvents())
            restarts+=std::string_view(e.name)=="room-load-restored";
    }
    Require(restarts==1 && cold.RoomActivity().started_command && ReadRetailSlot(source)==original_bytes,
            "New slot did not boot independently, or the input fixture was modified");
    std::cout<<"Independent cold room started ASC "<<*cold.RoomActivity().started_command<<'\n';
    const auto cold_key=[&](unsigned buttons) { step(cold,buttons); step(cold); };
    open_notebook(cold,true);
    cold_key(0x80);
    const auto cold_word=cold.Notebook();
    Require(cold_word.row==163 && cold_word.written==word.at(1).text && cold_word.reading==word.at(2).text,
            "Cold notebook did not restore the inherited word");
    cold_key(cross);
    for (unsigned i=0;i<70;++i) step(cold);
    open_notebook(cold,false);
    const auto cold_friend=cold.Notebook();
    Require(cold_friend.count==1 && cold_friend.row==601 && cold_friend.written==card.at(1).text,
            "Cold notebook did not restore the inherited friend");
    cold_key(circle);
    for (unsigned i=0;i<150 && !cold.Notebook().detail;++i) step(cold);
    Require(cold.Notebook().detail && cold.Notebook().written==card.at(1).text &&
            ReadRetailSlot(slot)==replacement_bytes,
            "Cold notebook did not open the inherited card or changed the slot");
    std::cout<<"Cold notebook reopened both the inherited word and friend card\n";
    return 0;
}
