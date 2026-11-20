void VerifyDepartureActor(ColdRoomActor actor) {
    Check(actor.Request(174), "Missing departure door ASC");
    std::vector<int> doors;
    for (unsigned tick = 0; tick < 1800 && !actor.command_ready(); ++tick) {
        Step(actor);
        const int pair = actor.scene_pair_requested();
        if (pair < 0) continue;
        doors.push_back(pair);
        const std::array<float,2> expected = pair == 2 ? std::array<float,2>{1830,-27}
                                                       : std::array<float,2>{2820,-27};
        Check(actor.position() == expected, "Door pair started at the wrong source coordinate");
    }
    Check(actor.command_ready() && doors == std::vector<int>({2,3}), "Door sequence never released wait23");

    Check(actor.Request(175), "Missing departure walk");
    for (unsigned t = 0; t < 75; ++t) Step(actor);
    Check(actor.position()[0] > 2820, "ASC175 did not walk away from the closed door");
    for (unsigned t = 0; t < 40; ++t) Step(actor);
    Check(actor.Request(273), "Missing transition pose");
    Step(actor);
    const auto in_transit = actor.position();
    Check(!actor.AdoptScene(13) && actor.position() == in_transit, "Foreign selector changed placement");
    Check(actor.AdoptScene(12) && actor.command() == 273 && actor.position() == std::array<float,2>{0,0},
          "Roof adoption reset the active ASC or missed its source position");

    for (const unsigned id : {186u,188u,190u,191u,178u}) {
        Check(actor.Request(id), "Missing target-scene control ASC");
        for (unsigned t = 0; t < 300 && !actor.command_ready(); ++t) Step(actor);
        Check(actor.command_ready(), "Target-scene control never released wait23");
    }
    Check(actor.AdoptScene(0) && actor.Request(176), "Room return did not admit ASC176");
    for (unsigned t = 0; t < 500 && !actor.command_ready(); ++t) Step(actor);
    Check(actor.command_ready() && actor.position() == std::array<float,2>{1830,-27},
          "Returned actor did not reach the room entrance");
    Step(actor);
    Check(actor.command() == 0 && !actor.talk_ready() && !actor.zoom_requested(),
          "Returning through the same door restarted the introduction");

    using namespace dokodemo::gameplay;
    dokodemo::recovered::RngNativeOwner rng;
    rng.ApplyInitializerTimeT32(168, 1, "roof ambient deadline component");
    constexpr unsigned start = 900;
    RoomActivityPhase arrived;
    arrived.Enter(start, rng);
    arrived.ReturnToRoom(start, 0, rng, false);
    // The scene's level filter is supplied by the live descriptor. Only a
    // strictly expired timer transfers the first post-cleanup turn to (2,4).
    for (unsigned now : {start + 5, start + 6}) {
        auto phase = arrived;
        auto local_rng = rng;
        const RoomActivityFrame frame{5, false, 0, 0, 1, 0, false, {}, false, 12, start + 5};
        for (unsigned t = 0; t < 60; ++t) phase.Advance(now, frame, local_rng);
        Check(!phase.lamp_command(), "Roof turn requested the scene-zero lamp");
        if (now == start + 5) {
            Check(phase.state() == RoomActivityPhase::State::select, "Returned home exactly at deadline");
        } else {
            Check(phase.state() == RoomActivityPhase::State::return_home && phase.BeginOrdinary(),
                  "Expired roof stay did not transfer to an owned room action");
        }
    }
}
