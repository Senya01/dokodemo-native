int CheckRecordExchange(const std::filesystem::path& install,const std::filesystem::path& source,
                        unsigned port,unsigned side,dokodemo::gameplay::MeetingMode mode) {
    using namespace dokodemo::gameplay;
    const bool gifts=mode==MeetingMode::gifts, words=mode==MeetingMode::shiritori;
    namespace fs=std::filesystem;
    Require(port && port<=65535 && side<=2,"Invalid private meeting port or participant");
    const auto input=ReadRetailSlot(source);
    auto saved=DecodeRetailUtilityRequest(input,RetailSaveProfile{});
    auto owners=RoomSaveOwners::FromActorGraph(saved.actor);
    auto profile=saved.profile.ReadProfile();
    Require(profile.actor==0,"A Toro slot is required for the Toro meeting menu");
    auto& rows=owners.dictionary.children.at(0).children;
    auto& card=rows.at(600).children.at(0).children;
    const char* player=side==1 ? "HANA" : "SORA";
    const char* peer=side==1 ? "SORA" : "HANA";
    Require(profile.player.Assign(player),"Could not prepare the test identity");
    card.at(1)={0x50000000u,player,{}};
    card.at(2)=card.at(1);
    saved.profile.Adopt(profile);
    int local_id=-1, peer_id=-1;
    if (gifts) {
        RoomInventory definitions;
        std::string error;
        if (!definitions.Load(install/"assets/game",error)) throw std::runtime_error(error);
        const auto date=saved.profile.SavedAt();
        const auto own_round=side==1 ? 0u : 2u;
        Require(definitions.AwardGift(0,own_round,"TORO",int(date[6]),int(date[5]),int(date[4]))==0,
                "Cannot prepare the private offered gift");
        local_id=definitions.slot_item(0)->id;
        owners.inventory=definitions.SaveTable();
        Require(definitions.AwardGift(0,2-own_round,"TORO",int(date[6]),int(date[5]),int(date[4]))==1,
                "Cannot verify the recipient's original gift");
        peer_id=definitions.slot_item(1)->id;
        Require(local_id!=peer_id,"Gift fixture needs two distinct items");
    }
    constexpr std::array<std::string_view,3> chain_a{
        "\x82\xe8\x82\xb7", "\x82\xdf\x82\xbe\x82\xa9", "\x82\xa2\x82\xca"};
    constexpr std::array<std::string_view,3> chain_b{
        "\x82\xb7\x82\xb8\x82\xdf", "\x82\xa9\x82\xa2", "\x82\xca\x82\xe8\x82\xa6"};
    const auto& learned=side==1 ? chain_a : chain_b;
    if (words) {
        const auto exemplar=rows.at(163);
        for (unsigned index=0;index<learned.size();++index) {
            auto row=exemplar;
            auto& fields=row.children.at(0).children;
            fields[0].word=0x20005024u;
            fields[1]={0x50000000u,std::string(learned[index]),{}};
            fields[2]=fields[1];
            fields[11].word=0x20000000u;
            const auto k=ShiritoriKana(learned[index]);
            Require(k.has_value(),"Invalid shiritori test word");
            fields[18].word=0x20000082u|((*k)[0]<<8);
            rows.at(163+index)=std::move(row);
        }
    }
    saved.actor=owners.ToActorGraph();
    const auto slot=install/"savedata"/"UCJS10002000";
    Require(!fs::exists(slot.parent_path()),"The LAN test needs a fresh private slot");
    WriteRetailSlot(slot,install,BuildRetailUtilityRequest(saved.profile,saved.actor,saved.word_history));
    const auto baseline=ReadRetailSlot(slot);
    auto calendar=saved.profile.SavedAt();
    const auto previous_day=int(owners.day_words[0].children.at(0).word&0x0fffffffu);
    ResumeDayRoom run(install,saved,calendar);
    run.room.BindMeetingOpener(dokodemo::platform::MakeLanMeetingOpener(std::uint16_t(port),true));
    run.Start(previous_day,previous_day);
    const auto activity=run.room.activity_snapshot().serial;
    run.OpenMenu();
    run.Key(0x40); run.Key(0x40); run.Key(0x40);
    Require(run.room.teaching_snapshot().menu_cursor==4,"Room did not select the friends menu");
    run.Key(0x2000); run.Rest(30); run.Key(0x40);
    if (gifts || words) run.Key(0x40);
    if (gifts) run.Key(0x40);
    run.Key(0x2000);
    bool choosing_gift=false, meeting=false, closed=false, received=false;
    std::vector<std::string> played;
    unsigned own_turns=0;
    for (unsigned i=0;i<5500 && !(closed && run.room.activity_snapshot().serial>activity &&
                                run.room.activity_snapshot().started_command);++i) {
        const auto conversation=run.room.conversation_snapshot();
        // Choose the first of the five actual venue options. Once GAME49
        // is active, the no-partner scenario sends a fresh Cross edge.
        const bool select=!meeting && (choosing_gift || conversation.option_count==5) && i%12==0;
        const bool cancel=meeting && !side && !closed && i%23==0;
        run.Step(select ? 0x2000 : cancel ? 0x4000 : 0);
        for (const auto& event:run.room.departure_events()) {
            const std::string_view name=event.name;
            if (name=="meeting-open") {
                Require(!meeting && event.values.at(0).second==int(mode) && event.values.at(1).second==0,
                        "Source meeting command supplied the wrong mode or venue");
                meeting=true;
                std::cout<<"GAME49 selected "<<(words?"shiritori":gifts?"gift exchange":"card exchange")
                         <<" and venue 0"<<std::endl;
            } else if (name=="meeting-gift-choice") {
                Require(!choosing_gift && gifts && event.values.front().second==1,
                        "Owned gift did not reach the offer prompt");
                choosing_gift=true;
            } else if (name=="meeting-gift-received") {
                Require(side && gifts && event.values.at(0).second==0 && event.values.at(1).second==peer_id,
                        "Gift was not replaced by the peer's item");
                received=closed=true;
                std::cout<<"Received gift "<<peer_id<<" in exchange for "<<local_id<<std::endl;
            } else if (name=="meeting-shiritori-word") {
                Require(side && words && event.values.at(0).second==int(played.size()+1) &&
                        event.text.size()==2 && event.text[0]==event.text[1],
                        "Shiritori did not consume the test word and reading");
                const bool own=event.values.at(1).second!=0;
                const auto& expected=own ? learned : side==1 ? chain_b : chain_a;
                Require(std::find(expected.begin(),expected.end(),event.text[0])!=expected.end() &&
                        std::find(played.begin(),played.end(),event.text[0])==played.end(),"Word repeated or belongs to the wrong player");
                const auto kana=ShiritoriKana(event.text[1]);
                Require(kana && (played.empty() || (*ShiritoriKana(played.back()))[1]==(*kana)[0]),
                        "Shiritori did not match the previous final kana");
                played.push_back(event.text[0]);
                own_turns+=own;
            } else if (name=="meeting-shiritori-finished") {
                Require(side && words && event.values.at(0).second==(side==1?-1:1) &&
                        event.values.at(1).second==int(played.size()) && played.size()>=5 && played.size()<=6,
                        "Shiritori did not finish on the unmatched tail");
                received=closed=true;
                std::cout<<"Shiritori finished, result "<<event.values.at(0).second<<", "<<played.size()
                         <<" accepted words, "<<own_turns<<" local"<<std::endl;
            } else if (name=="meeting-card-received") {
                Require(side && event.values.at(0).second==601 && event.values.at(1).second==1,
                        "Card was not mutually acknowledged in row 601");
                received=closed=true;
                std::cout<<"Received and acknowledged friend row 601"<<std::endl;
            } else if (name=="meeting-closed") {
                Require(!side && event.values.at(0).second==1,"Unexpected connection failure or close");
                closed=true;
            }
        }
    }
    Require(meeting && closed && received==bool(side) && run.room.activity_snapshot().serial>activity &&
            ReadRetailSlot(slot)==baseline,"Exchange did not return to activity or saved without a request");
    if (words) {
        if (side) {
            run.Save();
            const auto stored=DecodeRetailUtilityRequest(ReadRetailSlot(slot),RetailSaveProfile{});
            const auto after=RoomSaveOwners::FromActorGraph(stored.actor);
            const auto& dictionary=after.dictionary.children.front().children;
            unsigned used=0;
            for (unsigned row=163;row<166;++row)
                used+=bool(dictionary.at(row).children.front().children.at(11).word&8);
            Require(used==own_turns,"Saved shiritori word usage differs from the local turns");
        } else Require(played.empty(),"A cancelled match without a partner played a word");
        Require(ReadRetailSlot(source)==input,"Shiritori changed the source test slot");
        return 0;
    }
    if (gifts) {
        const auto check_inventory=[](ResumeDayRoom& current,int expected) {
            current.OpenMenu(); current.Key(0x10); current.Key(0x10);
            Require(current.room.teaching_snapshot().menu_cursor==5,"The inventory menu did not select");
            current.Key(0x2000);
            current.Until([&]{return current.room.inventory_snapshot().list_ready;},"Could not open the bag");
            const auto bag=current.room.inventory_snapshot();
            Require(bag.count==1 && bag.slot==0 && bag.id==expected && bag.category==1,
                    "Inventory did not retain one stored gift");
            current.Key(0x4000);
            current.Until([&]{return !current.room.inventory_snapshot().visible;},"The inventory did not close");
            current.Rest(40);
        };
        check_inventory(run,side?peer_id:local_id);
        if (side) {
            run.Save();
            const auto replacement=DecodeRetailUtilityRequest(ReadRetailSlot(slot),RetailSaveProfile{});
            ResumeDayRoom reboot(install,replacement,calendar);
            reboot.Start(previous_day,previous_day);
            check_inventory(reboot,peer_id);
        }
        Require(ReadRetailSlot(source)==input,"The source slot changed while exchanging gifts");
        std::cout<<(side ? "Protected slot and cold bag contain the gift" : "Cancelled offer preserved the original gift")<<'\n';
        return 0;
    }
    run.OpenSection(0,9);
    auto page=run.room.notebook_snapshot();
    if (!side) {
        Require(page.count==0 && page.row==-1,"Waiting/cancel created a friend");
        Require(ReadRetailSlot(source)==input,"The source fixture changed");
        std::cout<<"Source cancellation returned to the room with an empty notebook\n";
        return 0;
    }
    Require(page.count==1 && page.row==601 && page.written==peer,"Peer entry did not reach the notebook list");
    run.Key(0x2000);
    Require(run.room.notebook_snapshot().detail && run.room.notebook_snapshot().written==peer,
            "Peer's card did not open");
    run.Key(0x4000);
    run.CloseSection();
    run.Save();
    const auto replacement=DecodeRetailUtilityRequest(ReadRetailSlot(slot),RetailSaveProfile{});
    auto state=RoomSaveOwners::FromActorGraph(replacement.actor);
    const auto& taken=state.dictionary.children.at(0).children.at(601).children.at(0).children;
    Require(taken.size()==30 && taken.at(1).text==peer && !(taken.at(0).word&2) &&
            replacement.profile.ReadProfile().player.text()==player,"Own identity or peer receipt did not persist");
    ResumeDayRoom reboot(install,replacement,calendar);
    reboot.Start(previous_day,previous_day);
    reboot.OpenSection(0,9);
    Require(reboot.room.notebook_snapshot().row==601 && reboot.room.notebook_snapshot().written==peer &&
            ReadRetailSlot(source)==input,"Cold notebook lost its saved friend or changed the source");
    std::cout<<"Protected slot and cold notebook contain "<<peer<<'\n';
    return 0;
}
