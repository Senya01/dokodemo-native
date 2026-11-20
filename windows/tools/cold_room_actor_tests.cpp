#include "gameplay/cold_room_actor.h"
#include "gameplay/room_motion_effects.h"
#include "gameplay/room_activity_phase.h"
#include "gameplay/room_activity_selector.h"
#include "recovered/rng_native.h"
#include <set>
#include <deque>
#include <tuple>
#include <algorithm>

#include <cmath>
#include <iostream>
#include <stdexcept>

using dokodemo::gameplay::ColdRoomActor;

namespace {
void Check(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}

void Step(ColdRoomActor &actor) {
    Check(actor.Advance(), "ASC/DAK update failed");
}

// Fixed Z-up orthographic camera for an isolated raster check. All activity
// frames here use the same model, placement, viewport and lighting parameters.
uint64_t PosePixels(const ColdRoomActor &actor) {
    const std::array<float,16> camera{
        .0006f,0,0,0, 0,0,.0001f,0, 0,.0012f,0,0, -.36f,-.62f,0,1};
    constexpr size_t pixels = 512*272;
    std::vector<uint8_t> vram(10*pixels);
    Check(actor.Draw(camera,vram,0), "activity mesh render failed");
    uint64_t digest = 1469598103934665603ull;
    unsigned visible = 0;
    for (size_t i=0; i<4*pixels; ++i) {
        visible += vram[i] != 0;
        digest = (digest ^ vram[i]) * 1099511628211ull;
    }
    Check(visible > 0, "activity sample rendered no model");
    return digest;
}

void VerifyMotionEffects(const std::filesystem::path& dir, const std::filesystem::path& actors) {
    using namespace dokodemo::gameplay;
    struct Case { unsigned actor, command, resource; };
    const Case cases[] {
        {0,1063,1}, {0,1193,2}, {0,1263,4}, {0,1062,9},
        {1,1202,4}, {1,1205,4}, {2,1063,1}, {3,1263,4},
        {4,1203,3}, {4,1205,3}, {5,1264,3}, {5,1266,3}, {5,1267,3}
    };
    const auto media=dir/"assets/game/post-profile";
    RoomMotionEffects renderer;
    Check(!renderer.Spawn(media,{5,{}}),"An inactive source descriptor was accepted");
    constexpr std::size_t pixels=512*272;
    for (const auto& test : cases) {
        ColdRoomActor actor;
        std::string error;
        Check(actor.Load(actors,test.actor,1,error),error.c_str());
        Check(actor.Request(test.command),"Missing source effect ASC");
        for (unsigned i=0;i<130 && actor.effect_spawns().empty();++i) Step(actor);
        Check(!actor.effect_spawns().empty(),"Source actor did not emit the timed DAK key");
        for (const auto& spawn : actor.effect_spawns()) {
            Check(spawn.resource==test.resource,"Wrong source effect descriptor");
            Check(renderer.Spawn(media,spawn),"No effect instance allocated");
        }

        const auto& anchor=actor.effect_spawns().front().position;
        const float scale=.001f;
        const std::array<float,16> camera{scale,0,0,0, 0,0,.0001f,0, 0,scale,0,0,
                                          -scale*anchor[0],-scale*anchor[2],-.0001f*anchor[1],1};
        std::vector<std::uint8_t> vram(10*pixels);
        unsigned visible=0, retired=0;
        for (unsigned tick=0;tick<240 && retired<actor.effect_spawns().size();++tick) {
            std::fill(vram.begin(),vram.end(),0);
            const auto finished=renderer.Draw(camera,vram,0);
            retired+=finished[test.resource];
            for (std::size_t i=0;i<4*pixels;i+=4)
                if (vram[i] || vram[i+1] || vram[i+2]) ++visible;
        }
        std::cout<<"actor="<<test.actor<<" asc="<<test.command<<" effect="<<test.resource
                 <<" instances="<<actor.effect_spawns().size()<<" retired="<<retired
                 <<" visible_pixel_samples="<<visible<<'\n';
        Check(visible>0 && retired==actor.effect_spawns().size(),
              "Effect did not render and retire through the room pipeline");
    }
    for (unsigned i=0;i<20;++i)
        Check(renderer.Spawn(media,{9,{}}),"Source pool refused a vacant entry");
    Check(!renderer.Spawn(media,{9,{}}),"The source effect pool exceeded its capacity");
    renderer.Clear();
    Check(renderer.Spawn(media,{9,{}}),"Scene change did not clear the effect pool");
}

void VerifyActivities(const std::filesystem::path &dir, const ColdRoomActor &after_farewell) {
    using namespace dokodemo::gameplay;
    RoomActivitySelector selector;
    std::string error;
    if (!selector.Load(dir / "assets/game/post-profile", error)) throw std::runtime_error(error);
    Check(after_farewell.source_field50() == 1 && after_farewell.source_field58() == 0,
          "B194 actor posture or KSC location after farewell");
    ColdRoomActor quiet = after_farewell;
    quiet.SetMood(3);
    Check(!quiet.RequestActivity(13), "logical motion must not become a script activity");
    for (unsigned t = 0; t < 180; ++t)
        Step(quiet);  // Earlier ASC keeps evolving through the genuine quiet standing variant.
    std::set<unsigned> tested;
    std::set<uint64_t> stationary_poses;
    for (int period = 0; period < 6; ++period) for (int mood : {0,1}) {
        const RoomActivityInputs inputs{true,0,int(after_farewell.source_field58()),
                                       int(after_farewell.source_field50()),period,mood,false};
        dokodemo::recovered::RngNativeOwner rng;
        rng.ImportCurrentState64(1, 1, "cold room ASC selector test");
        for (unsigned trial = 0; trial < 500; ++trial) {
            const auto choice = selector.Select(inputs,rng);
            if (!choice.command || !tested.insert(*choice.command).second) continue;
            const auto id = *choice.command;
            ColdRoomActor actor = after_farewell;
            actor.SetMood(mood);
            Check(actor.Request(id), "selected job missing from actor bundle");
            unsigned tv_requests = 0, flag_writes = 0, samples = 0;
            bool released = false;
            for (unsigned tick = 0; tick < 8000 && actor.command(); ++tick) {
                if (!actor.Advance())
                    throw std::runtime_error("Room activity " + std::to_string(id) + " " + actor.error());
                released |= actor.command_ready();
                if (tick == 27 && (id == 341 || id == 344 || id == 346))
                    stationary_poses.insert(PosePixels(actor));
                if (actor.game28_write() == 1) ++flag_writes;
                if (actor.scene_pair_requested() == 0) ++tv_requests;
                if (tick % 13 == 0) {
                    std::array<float,3> node;
                    if (!actor.SpeechAnchor(node))
                        throw std::runtime_error("Room joint/DAK sampling failed in ASC " + std::to_string(id));
                    ++samples;
                }
            }
            Check(released && samples && actor.command() == 0, "selected finite ASC did not complete");
            const auto xy = actor.position();
            if (id == 444 || id == 445)
                Check(xy[0] == -1200 && xy[1] == -100 && flag_writes == 1 && tv_requests == 1,
                      "television walk or source scene effects");
            else if (id == 424)
                Check(xy[0] == -500 && xy[1] == -500, "ASC424 source coordinate");
        }
    }
    Check(stationary_poses.size() == 3, "stationary activities must render their distinct source poses");
    Check(tested.size() == 43 && tested.count(344) && tested.count(346),
          "cold first activity corpus changed");
}
void VerifyWait23(const ColdRoomActor &after_farewell) {
    auto actor = after_farewell;
    {
        auto seated = actor;
        Check(seated.RequestActivity(445), "Start the source posture-three activity");
        Step(seated);
        for (unsigned t = 0; t < 2000 && !seated.command_ready(); ++t) Step(seated);
        Check(seated.command_ready() && seated.source_field50() == 3,
              "Activity 445 did not leave the actor in posture three");
        Check(seated.RequestConversationControl(147), "Posture-three conversation action refused");
        Step(seated);
        Check(seated.started_activity() == 147, "Actor did not start the requested stand-up action");
        for (unsigned t = 0; t < 2000 && !seated.command_ready(); ++t) Step(seated);
        Check(seated.command_ready() && seated.source_field50() == 1 && !seated.source_flag25a(),
              "Conversation action did not release the actor in standing posture");
    }
    {
        auto event_actor = actor;
        Check(event_actor.RequestActivity(409), "neutral activity before an ordinary talk");
        Step(event_actor);
        Check(event_actor.started_activity() == 409 && !event_actor.command_ready() &&
              !event_actor.RequestActivity(374) && !event_actor.RequestRoomControl(157),
              "neutral VM must retain a busy motion");
        Check(!event_actor.RequestConversationControl(409) &&
              event_actor.RequestConversationControl(178) && event_actor.command() == 409,
              "ordinary VM request must use its own admitted command domain");
        Step(event_actor);
        Check(event_actor.started_activity() == 178, "conversation turn was not admitted");
        for (unsigned t=0;t<120;++t) {
            Check(!event_actor.zoom_requested() && !event_actor.talk_ready(),
                  "ordinary turn replayed the first-introduction script");
            Step(event_actor);
        }
        Check(event_actor.RequestConversationControl(156), "conversation TV-off request");
        bool tv_off = false;
        for (unsigned t=0;t<2000 && !event_actor.command_ready();++t) {
            Step(event_actor);
            tv_off |= event_actor.scene_pair_requested() == 1;
        }
        Check(event_actor.command_ready() && tv_off, "conversation TV-off did not own its scene effect");
    }
    {
        auto lamp = actor;
        Check(!lamp.RequestActivity(153) && !lamp.RequestRoomControl(344),
              "room lighting and activity domains must stay distinct");
        for (const unsigned command : {153u,153u,157u,154u}) {
            Check(lamp.RequestRoomControl(command) && !lamp.command_ready(), "lamp control request");
            unsigned writes = 0;
            for (unsigned t=0;t<2000 && !lamp.command_ready();++t) {
                Step(lamp);
                if (lamp.lamp_write()) {
                    ++writes;
                    Check(*lamp.lamp_write()==(command==153), "ASC8030 light argument");
                }
            }
            Check(lamp.command_ready() && writes==(command!=157),"fresh light write per admitted ASC");
        }
    }
    unsigned prior_ticks = 0;
    for (const unsigned command : {336u,409u,409u}) {
        Check(actor.RequestActivity(command) && !actor.command_ready(), "new wait23 registration");
        unsigned ticks = 0;
        for (; ticks < 500; ++ticks) {
            Step(actor);
            Check((ticks == 0) == actor.started_activity().has_value(), "B774 entry occurrence");
            if (actor.command_ready()) break;
        }
        // ASC409 has another 8010 and a four-pass loop AFTER the first
        // finite loop of three. That loop pop cannot wake 13848.
        Check(command == 336 ? ticks >= 80 && ticks < 100 : ticks >= 300 && ticks < 325,
              "wait23 resumed at an intermediate loop or missed the owning return");
        if (command == 409 && prior_ticks >= 300)
            Check(ticks == prior_ticks, "same ASC request did not restart its whole record");
        std::cout << "ASC " << command << " resumed after " << ticks << " actor updates from entry\n";
        prior_ticks = ticks;
        for (unsigned delay=0; delay<33; ++delay) Step(actor);
    }
}
void VerifyInterrupt(const ColdRoomActor& after_farewell) {
    auto actor = after_farewell;
    Check(actor.RequestActivity(395), "ASC395 admission");
    for (unsigned t=0;t<30 && !actor.source_field5c();++t) Step(actor);
    Check(actor.command()==395 && actor.source_field5c()==1 && actor.source_flag25a() &&
          actor.source_field50()==1 && !actor.command_ready(), "ASC395 source interrupt gate");
    auto normal = actor;
    Check(actor.InterruptCommand() && !actor.command_ready() &&
          actor.source_field5c()==1 && actor.command()==395, "GAME26 lookahead must preserve active 395");
    Step(actor);
    Check(actor.source_field50()==2 && actor.source_flag25a(), "locked ASC395 prefix skipped");
    bool cleanup = false;
    for (unsigned t=0;t<1500 && !actor.command_ready();++t) {
        Step(actor);
        cleanup |= !actor.source_flag25a() && actor.source_field5c()==1 && actor.source_field50()==2;
    }
    Check(cleanup && actor.command_ready() && !actor.command() && actor.source_field50()==1 &&
          !actor.source_field5c(), "8043 cleanup/owning 8000 return");

    // The normal record has seven loop passes. Interrupt once inside one
    // of those frames, then independently interrupt the final locked section.
    bool loop_interrupt = false;
    unsigned locked_pairs = 0, previous_posture = normal.source_field50();
    for (unsigned t=0;t<2000 && !normal.command_ready();++t) {
        Step(normal);
        if (!loop_interrupt && normal.source_field5c() && !normal.source_flag25a()) {
            auto loop = normal;
            Check(loop.InterruptCommand() && !loop.command_ready(), "interrupt in a live loop");
            Step(loop);
            Check(loop.source_field50()==2 && !loop.source_flag25a(), "interrupt skipped its own cleanup");
            for (unsigned n=0;n<600 && !loop.command_ready();++n) Step(loop);
            Check(loop.command_ready() && !loop.command() && !loop.source_field5c(), "interrupted loop leaked its frame");
            loop_interrupt = true;
        }
        if (normal.source_field50()==2 && previous_posture!=2 && normal.source_flag25a()) {
            if (++locked_pairs == 2) {
                auto locked = normal;
                Check(locked.InterruptCommand() && !locked.command_ready(), "late locked 395 interruption");
                for (unsigned n=0;n<600 && !locked.command_ready();++n) Step(locked);
                Check(locked.command_ready() && locked.command()==395 && locked.source_flag25a() &&
                      !locked.source_field5c() && locked.source_field50()==1, "8041 release before root return");
            }
        }
        previous_posture = normal.source_field50();
    }
    Check(loop_interrupt && locked_pairs==2 && normal.command_ready() && normal.command()==395 &&
          !normal.source_flag25a(), "normal 395 owns its 8031 rather than the interrupted exit");

    // Separate lookahead case: 336 has no 8043. Direct component call,
    // independent of KSC applicability, distinguishes immediate 25E from stop.
    auto simple = after_farewell;
    Check(simple.RequestActivity(336), "ASC336 admission");
    Step(simple);
    Check(!simple.command_ready() && simple.InterruptCommand() && simple.command_ready() &&
          simple.command()==336, "8000 lookahead without 8043");
}
void VerifyRepeated(const std::filesystem::path &dir, const ColdRoomActor &after_farewell) {
    using namespace dokodemo::gameplay;
    RoomActivitySelector selector;
    std::string error;
    if (!selector.Load(dir / "assets/game/post-profile", error)) throw std::runtime_error(error);
    using State = std::tuple<unsigned,unsigned,bool>;
    std::deque<std::pair<ColdRoomActor,bool>> pending{{after_farewell,false}};
    std::set<State> retained{{after_farewell.source_field58(), after_farewell.source_field50(), false}};
    std::set<unsigned> jobs;
    bool repeated = false, early_blocked = false, tv_off = false;
    while (!pending.empty()) {
        const auto [source, source_flag] = std::move(pending.front());
        pending.pop_front();
        std::set<unsigned> sampled;
        for (int mood : {0,1,3}) for (int period=0; period<6; ++period) {
            const RoomActivityInputs inputs{false,0,int(source.source_field58()),int(source.source_field50()),
                                            period,mood,source_flag};
            dokodemo::recovered::RngNativeOwner rng;
            rng.ImportCurrentState64(747,1,"Repeated room activity choices");
            for (unsigned trial=0; trial<180; ++trial) {
                const auto selection = selector.Select(inputs,rng);
                if (!selection.command || !sampled.insert(*selection.command).second) continue;
                const unsigned id = *selection.command;
                auto actor = source;
                bool flag = source_flag;
                actor.SetMood(mood);
                const auto before = actor.command();
                Check(actor.RequestActivity(id) && actor.command()==before && !actor.command_ready(),
                      "activity reservation must not enter or inherit readiness");
                Check(!actor.RequestActivity(id),"duplicate pending activity");
                unsigned delay = 0;
                bool awake = false;
                for (unsigned tick=0; tick<8000; ++tick) {
                    Step(actor);
                    Check((tick==0 && actor.started_activity()==id) || (tick>0 && !actor.started_activity()),
                          "activity start occurrence");
                    if (actor.game28_write()) flag = *actor.game28_write()!=0;
                    if (actor.scene_pair_requested() == 1) tv_off = true;
                    if (!awake && actor.command_ready()) {
                        awake = true;
                        delay = 30 + rng.NextC3234()%31;
                    } else if (awake && --delay==0) break;
                    else if (!awake) {
                        Check(!actor.RequestActivity(id),"wait23 was not ready for replacement");
                        early_blocked = true;
                    }
                }
                Check(awake && delay==0,"13848 wait and following delay did not finish");
                jobs.insert(id);
                const State state{actor.source_field58(),actor.source_field50(),flag};
                if (retained.insert(state).second)
                    pending.emplace_back(actor,flag);
                if (!repeated && id==344) {
                    // The same id is a new B1D4 request, even from a still
                    // resident record whose earlier 258 pulse released KSC.
                    Check(actor.RequestActivity(id) && !actor.command_ready(),"same-id new wait");
                    Step(actor);
                    repeated = actor.started_activity()==id && actor.command()==id && !actor.command_ready();
                }
            }
        }
    }
    if (!(early_blocked && repeated && tv_off)) {
        throw std::runtime_error("Incomplete repeated activity results: " + std::to_string(jobs.size()) +
                                 " records, blocked " + std::to_string(early_blocked) +
                                 ", replay " + std::to_string(repeated) + ", TV-off " + std::to_string(tv_off));
    }
    std::cout << "Repeated selector: " << jobs.size() << " command records, " << retained.size()
              << " distinct (location, posture, flag) tuples\n";
    if (jobs.size()!=70) throw std::runtime_error("Repeated activity records seen " + std::to_string(jobs.size()));
}
void VerifyOutsideConversation(const std::filesystem::path &dir, ColdRoomActor actor) {
    using namespace dokodemo::gameplay;
    RoomMesh room;
    std::string error;
    Check(room.Load(dir/"assets/game/post-profile/bg_room.bin",error,true),"room geometry for GAME17");
    Check(!room.DirectActorRoute({-1800,750},{-500,750}),"source route crossed the node 62 circle");
    Check(actor.RequestActivity(444),"television activity");
    do { Step(actor); } while (!actor.command_ready());
    const auto before=actor.position();
    const auto source=room.QueryActorRegion(before);
    Check(!source.inside && before==std::array<float,2>{-1200,-100} &&
          room.ContainsActor(source.target),"outside GAME19 captured an eligible center");
    Check(actor.RequestConversationControl(186),"outside preparation command");
    do { Step(actor); } while (!actor.command_ready());
    Check(actor.source_field58()==0 && actor.position()==before,"ASC186 only clears the location");
    Check(actor.BeginConversationApproach(room,source) && actor.position()==before &&
          !actor.ConversationApproachDone(),"GAME17 begins an actual route from the current pose");
    unsigned moved=0;
    for(unsigned i=0;i<2000 && !actor.ConversationApproachDone();++i) {
        const auto previous=actor.position();
        Step(actor);
        moved += actor.position()!=previous;
    }
    Check(moved>1 && actor.ConversationApproachDone() && actor.position()==source.target &&
          room.ContainsActor(actor.position()) && actor.source_field50()==1,
          "GAME18 returns true at the end of the source-selected route");
    std::cout<<"GAME17 ("<<before[0]<<","<<before[1]<<") -> nearest center ("
             <<source.target[0]<<","<<source.target[1]<<"), "<<moved<<" moving updates\n";
}
#include "room_departure_test.inc"

void Verify(const std::filesystem::path &dir, unsigned species, int period) {
    ColdRoomActor actor;
    std::string error;
    if (!actor.Load(dir, species, period, error)) throw std::runtime_error(error);
    unsigned doors = 0, zooms = 0;
    for (unsigned tick = 0; tick < 1600 && !actor.talk_ready(); ++tick) {
        Check(actor.Advance(100), "entrance update failed");
        doors += actor.door_close_requested();
        zooms += actor.zoom_requested();
    }
    Check(doors == 1 && zooms == 1 && actor.talk_ready(), "entrance, camera or greeting order");
    Check(actor.command() == (species ? 85u : 60u) && !actor.command_ready(), "first invitation row");
    const auto pos = actor.position();
    Check(pos[0] == 600 && pos[1] == -300 && actor.light_on() == (period == 0), "cold room endpoint");
    std::array<float,3> anchor;
    Check(actor.SpeechAnchor(anchor) && std::isfinite(anchor[2]), "character joint");
    if (species == 0)
        Check(std::abs(anchor[0]-600)<0.5 && std::abs(anchor[1]+300)<0.5 && std::abs(anchor[2]-400)<0.5,
              "Toro dialogue attachment");
    std::vector<unsigned> voice;
    for (unsigned tick = 0; tick < 1500 && !actor.command_ready(); ++tick) {
        Step(actor);
        if (!species)
            for (const auto &sound : actor.sounds()) {
                Check(sound.bank == 0 && sound.group == 3 && sound.sequence == 7 && sound.scaled,
                      "invitation DAK type-6 routing");
                voice.push_back(sound.volume);
            }
    }
    Check(actor.command_ready() && actor.talk_ready(), "builtin 23 must release on the appropriate ASC repeat");
    if (species) return;
    if (period == 0) {
        for (unsigned ticks = 0; ticks < 160 && actor.talk_ready(); ++ticks) {
            Check(actor.Advance(105), "invitation deadline update failed");
            if (actor.invitation_expired()) break;
        }
        Check(actor.invitation_expired() && !actor.talk_ready(), "1A56 calendar expiration");
        Step(actor);
        Check(!actor.invitation_expired(), "expired pulse must be consumed");
    }
    Check(voice == std::vector<unsigned>({50,30}), "natural Toro invitation keys");
    for (unsigned cmd : {42u,60u,39u,38u,43u,35u,74u,20u,48u,29u,36u,87u}) {
        Check(actor.Request(cmd) && !actor.talk_ready() && !actor.command_ready(), "new ASC owns readiness");
        for (unsigned tick = 0; tick < 2000 && !actor.command_ready(); ++tick) {
            Step(actor);
            Check(!actor.door_close_requested(), "unexpected conversation door operation");
        }
        Check(actor.command_ready(), "dialogue motion never released");
    }
    if (period == 2) {
        // The second line of 2E36E requests ASC 46. DAK 73 contains
        // the type-6 key 06046402 at source time 1600 of 1920.
        ColdRoomActor phone = actor;
        Check(phone.Request(46), "Missing nuisance-call animation");
        unsigned keys = 0;
        uint64_t first_pose = 0;
        bool pose_changed = false;
        for (unsigned tick=0; tick<160 && !phone.command_ready(); ++tick) {
            Step(phone);
            if (tick==2) first_pose=PosePixels(phone);
            for (const auto& sound : phone.sounds()) {
                Check(sound.bank==0 && sound.group==4 && sound.sequence==2 &&
                      sound.volume==100 && sound.scaled, "Wrong nuisance-call motion sound");
                ++keys;
                pose_changed = first_pose && first_pose!=PosePixels(phone);
            }
        }
        Check(phone.command_ready() && keys==1 && pose_changed, "Nuisance-call pose, sound or first ASC repeat missing");
        VerifyActivities(dir,actor); VerifyWait23(actor); VerifyRepeated(dir,actor);
        VerifyOutsideConversation(dir,actor); VerifyInterrupt(actor);
        VerifyDepartureActor(actor);
    }
    // Ordinary cold topics include the two answers to 8528B,
    // the 94D1 wait and the refusal/repeat teaching reactions.
    for (const unsigned id : {60u,42u,66u,43u,41u,26u,54u,49u,48u,31u,
                              51u,24u,38u,61u,52u,44u,40u,45u,30u,37u,50u,53u,
                              58u,63u,67u,69u,70u,79u,82u,89u,90u,99u}) {
        Check(actor.Request(id) && !actor.command_ready(), "conversation ASC replacement");
        for (unsigned tick=0; tick<1600 && !actor.command_ready(); ++tick) Step(actor);
        Check(actor.command_ready(), "conversation motion never allowed KSC to continue");
    }
    Check(!actor.Request(0xffff), "unknown ASC request");
}
}
int main(int argc,char** argv) try {
    if (argc==4 && std::string_view(argv[2])=="--motion-effects") {
        VerifyMotionEffects(argv[1],argv[3]);
        return 0;
    }
    if (argc != 2) return 2;
    dokodemo::gameplay::RoomMesh room;
    std::string error;
    const auto path = std::filesystem::path(argv[1]) / "assets/game/post-profile/bg_room.bin";
    if (!room.Load(path,error,true)) throw std::runtime_error(error);
    Check(room.ContainsActor({600,-300}) && !room.ContainsActor({9000,9000}),"GAME19 DVS region query");
    for (unsigned species = 0; species < 5; ++species)
        for (int period : {0,2}) Verify(argv[1], species, period);
    std::cout << "Cold-room entrance, invitation and Toro conversation motions verified\n";
    return 0;
} catch(const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
