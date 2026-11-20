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
    card.at(1)={0x50000000u,"FRIEND",{}};
    card.at(2)={0x50000000u,"\x82\xd3\x82\xea\x82\xf1\x82\xc7",{}};
    card.at(11).word=0x20000003u;
    card.at(21)={0x50000000u,"POKEPI",{}};
    card.at(25)={0x50000000u,"456",{}};
    old_rows.at(601)=friend_record;
    original.profile.SetOptions({3,2,1});
    const auto staged=BuildRetailUtilityRequest(original.profile,old.ToActorGraph());
    WriteRetailSlot(slot,install,staged);
    const auto prior=ReadRetailSlot(slot);
    const auto old_profile=original.profile.ReadProfile();
    auto calendar=original.profile.SavedAt();
    Require(calendar[4]<28,"Replacement fixture needs tomorrow in the current month");
    ++calendar[4];
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
    BootToDisplay boot;
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
    const auto open_home_menu=[&] {
        for (unsigned i=0;i<90 && !boot.RoomTeaching().menu_open;++i) { key(right); rest(10); }
        Require(boot.RoomTeaching().menu_open && boot.RoomTeaching().menu_cursor==0,"New room did not open the main menu");
        rest(24);
    };
    open_home_menu(); key(down); key(down);
    Require(boot.RoomTeaching().menu_cursor==3,"New room did not select the notebook category");
    key(circle); rest(30); key(down); key(circle);
    until([&]{return boot.Notebook().list_ready && boot.Notebook().resource==8;},"New room did not open its word memo");
    key(0x80);
    const auto memo=boot.Notebook();
    Require(memo.row==163 && memo.written==word.at(1).text && memo.reading==word.at(2).text,
            "Replacement lost the old learned word in the live notebook");
    key(cross); rest(70);

    const auto save=[&] {
        open_home_menu(); key(0x10); key(circle); rest(30); key(down); key(circle);
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
    const auto replacement=DecodeRetailUtilityRequest(ReadRetailSlot(slot),RetailSaveProfile{});
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
    BootToDisplay cold;
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
    return 0;
}
