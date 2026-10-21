#include "game_title_native.h"
#include "game_boot_native.h"
#include "game_content_native.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace dokodemo::recovered {
namespace {
constexpr unsigned order[]{0, 4, 3, 2, 1};
constexpr const char *names[]{"toro", "suzuki", "jun", "pierre", "ricky", "kuro"};
constexpr char prefix[] = "title\\charmodel_title\\";
GameMatrix ActorRoot(const GameTitleActorSnapshot &actor, uint32_t layout = 0) {
    // 3B01C heading+pi=-pi/2, native column-vector equivalent.
    auto m = ModelIdentity();
    if (layout == 0) {
        m[0] = 0;
        m[1] = -1;
        m[4] = 1;
        m[5] = 0;
    } else if (layout == 3) {
        constexpr float angle = .0872664626f;
        m[0] = m[5] = std::cos(angle);
        m[1] = std::sin(angle);
        m[4] = -m[1];
    }
    m[12] = actor.x;
    m[13] = actor.y;
    m[14] = actor.z;
    return m;
}
} // namespace
uint32_t TitleMovementStep(uint32_t type, uint32_t counter, uint32_t &segment) {
    constexpr uint32_t speeds[]{25, 58, 0, 33, 25, 25};
    if (type >= 6 || segment >= 3)
        throw std::runtime_error("TITLE movement table index");
    if (type != 2)
        return speeds[type];
    constexpr uint32_t durations[]{3, 19, 24};
    const auto step = segment == 1 ? 52u : 0u;
    if (counter == 1)
        segment = 0;
    else if (counter > 1 && counter % durations[segment] == 1)
        segment = (segment + 1) % 3;
    return step; // advance is for the next08A88 invocation
}
void TitleArrivalCycle(GameTitleSnapshot &state, RngNativeOwner *rng, bool game_flag, bool both_triggers) {
    if (state.state != 1 || state.layout != 0)
        return;
    if (game_flag)
        state.arrival_countdown = 0;
    const auto denominator = game_flag && both_triggers ? 2u : 64u;
    for (unsigned i = 0; i < 5; ++i) {
        auto &primary = state.actors[i];
        auto &kuro = state.actors[i + 5];
        const auto arrived = [](const GameTitleActorSnapshot &actor) {
            return actor.target_produced && actor.x == actor.target_x && actor.y == actor.target_y;
        };
        if (kuro.visible) {
            if (arrived(kuro))
                kuro.desired_visible = false;
            continue;
        }
        if (!arrived(primary))
            continue;
        primary.desired_visible = false;
        if (state.media_state || !(state.selection == -1 || state.selection == 7))
            continue;
        ++state.primary_arrivals;
        if (i == 0 && state.arrival_countdown)
            --state.arrival_countdown;
        bool choose_kuro = false;
        if (!state.arrival_countdown) {
            if (!rng || !rng->Available())
                throw std::runtime_error("33B90 actual initialized RNG owner unavailable");
            choose_kuro = rng->NextC3234() % denominator == 0;
            ++state.rng_calls;
        }
        auto &chosen = choose_kuro ? kuro : primary;
        chosen.x = 2500 + 800.0f * order[i];
        chosen.y = 2000 + 500.0f * order[i];
        chosen.z = -400;
        chosen.target_x = -4000;
        chosen.target_y = chosen.y;
        chosen.moving = chosen.target_produced = chosen.desired_visible = true;
        chosen.movement_counter = 0;
        ++chosen.movement_epoch;
        if (choose_kuro)
            ++state.kuro_selections;
        else
            ++state.primary_restarts;
    }
    for (auto &actor : state.actors)
        actor.visible = actor.desired_visible;
}
GameTitleScene::~GameTitleScene() { (void)Close(); }
GameFsStatus GameTitleScene::Initialize(GameResourceSource &source, GameArena &arena, GameVmBoot &manager,
                                        NativeTitleLayout layout, uint32_t source_profile_date) {
    if (arena_)
        return {GameFsCode::Busy, 0, "TITLE owner already allocated"};
    arena_ = &arena;
    manager_ = &manager;
    state_ = {};
    state_.layout = layout == NativeTitleLayout::CharacterEntrance ? 0u : 5u;
    // 30E98->342D8->24D00 reads gamestate+4C. The actual249D0 reset
    // zeroed that range and no missing AUTOLOAD imported it. With0,342D8
    // returns0 immediately, before its profile/date/string consumers.
    if (source_profile_date != 0)
        return {GameFsCode::Unavailable, 0, "TITLE342D8 nonzero profile path unimplemented"};
    try {
        owner_ = arena.Allocate(376);
        text_ = arena.Allocate(1024 * 44);
        task_ = manager.AcquireTask();
        if (!owner_ || !text_ || task_ == UINT32_MAX)
            throw std::runtime_error("TITLE owner/text/task allocation failed");
        // Immutable arrays may be shared across the five independent kuro clones.
        // Actual constructors request these six identities and light, not a scan.
        constexpr char suffix[]{'T', 'S', 'J', 'P', 'R'};
        for (unsigned type = 0; type < 6; ++type) {
            auto asset = std::make_shared<GameModelAsset>();
            auto status = asset->Load(source, std::string(prefix) + names[type] + ".DVS",
                                      std::string(prefix) + "gim\\");
            if (!status)
                return status;
            assets_[type] = std::move(asset);
            ++state_.model_assets;
            const auto stats = assets_[type]->Statistics();
            state_.vertices += stats.vertices;
            state_.triangles += stats.triangles;
            std::vector<std::string> paths;
            if (type < 5)
                paths = {"_tati", "_run_l", "_dance", std::string("_") + suffix[type] + "_guruguru02"};
            else
                paths = {"_run_l"};
            for (const auto &path : paths) {
                GameModelMotion motion;
                status =
                    motion.Load(source, std::string(prefix) + names[type] + path + ".DAK", *assets_[type]);
                if (!status)
                    return status;
                state_.approximate_curves += motion.Statistics().approximate_curves;
                motions_[type].push_back(std::move(motion));
                ++state_.motions;
            }
        }
        assets_[6] = std::make_shared<GameModelAsset>();
        auto status =
            assets_[6]->Load(source, std::string(prefix) + "light.DVS", std::string(prefix) + "gim\\");
        if (!status)
            return status;
        ++state_.model_assets;
        for (unsigned i = 0; i < 10; ++i) {
            status = actors_[i].model.Initialize(assets_[i < 5 ? i : 5]);
            if (!status)
                return status;
            ++state_.clones;
        }
        // 3B264/3B344 position and lookpoint, FOV20deg. The file camera's
        // one-unit lookpoint is not used as a position. Host lookAt is nonmatching.
        auto view = ModelIdentity();
        view[0] = 1;
        view[5] = 0;
        view[9] = 1;
        view[6] = -1;
        view[10] = 0;
        view[13] = -560;
        view[14] = -4400;
        camera_ = ModelMultiply(ModelPerspective(state_.field_of_view, 480.0f / 272.0f, 100, 10400), view);
        state_.camera_ready = true;
        status = ResetEntrance();
        if (!status)
            return status;
        state_.live = state_.constructor_returned = true;
        state_.state = 2;
        state_.frontier_pc = 0x30D44;
        return {};
    } catch (const std::exception &e) {
        return {GameFsCode::Unavailable, 0, e.what()};
    }
}
GameFsStatus GameTitleScene::ResetEntrance() {
    nickname_.clear(); //30E70..30E9C clears cold profile text on FC0
    for (unsigned i = 0; i < 10; ++i) {
        auto &actor = actors_[i];
        auto &s = state_.actors[i];
        const auto position = order[i % 5];
        s = {};
        s.type = i < 5 ? i : 5;
        s.motion = i < 5 ? 1u : 0u;
        s.x = 2500.0f + (i >= 5 ? 800.0f * position : 0);
        s.y = 2000.0f + 500.0f * position;
        s.target_y = s.y;
        s.z = -400;
        s.moving = i < 5;
        s.target_produced = i < 5;
        s.movement_epoch = i < 5 ? 1 : 0;
        if (i >= 5) {
            s.target_x = 0;
            s.target_y = 0;
        } // no cold3B134 producer for kuro
        s.desired_visible = i < 5;
        s.visible = false;
        actor.time = actor.segment = 0;
        actor.movement_epoch = s.movement_epoch;
        actor.complete = false;
        actor.carousel_step = 0;
        actor.blend_left = 0;
        actor.blend_from.clear();
        auto status = motions_[s.type][s.type < 5 ? 1 : 0].Apply(actor.model, 0);
        if (!status)
            return status;
        status = actor.model.Update(ActorRoot(s));
        if (!status)
            return status;
    }
    return {};
}
GameFsStatus GameTitleScene::AttachMenu(GameUiInstance &ui, const GameFontResource &font,
                                        RngNativeOwner &rng,GameSceneUiBootstrap* scene_ui,int32_t calendar_year) {
    if (!state_.live || ui.Nodes().size() != 404 || !font.Snapshot().ready || !rng.Available())
        return {GameFsCode::Unavailable, 0, "TITLE menu requires actual UI,font and initialized RNG owners"};
    ui_ = &ui;
    font_ = &font;
    rng_ = &rng;
    scene_ui_=scene_ui;calendar_year_=calendar_year;
    controls_.fill(UINT32_MAX);
    const auto control = [&](uint32_t clip, uint32_t frame) {
        auto *c = ui.AddControl(clip);
        if (!c)
            throw std::runtime_error("TITLE UI control allocation");
        // AddControl appends; controls are indexed independently from node IDs.
        uint32_t index = 0;
        while (ui.Control(index) != c)
            ++index;
        c->frame = frame;
        c->mode = 2;
        c->active = 1;
        controls_[clip] = index;
    };
    try {
        control(6, 1);
        control(7, 1);
        control(8, 1);
        control(9, 1);
        control(17, 1);
        for (uint32_t i = 0; i < 7; ++i)
            control(10 + i, i == 0 ? 3 : 1);
        for (uint32_t i = 0; i < 10; ++i)
            control(19 + i, 1);
        for (uint32_t i = 0; i < 30; ++i)
            ui.ShowRoot(i, false);
        for (uint32_t i = 0; i < 5; ++i)
            ui.Node(i)->draw_group = 0;
        ui.Node(5)->draw_group = ui.Node(29)->draw_group = 26;
        for (uint32_t i = 6; i < 17; ++i)
            ui.Node(i)->draw_group = 20;
        ui.Node(17)->draw_group = 21;
        constexpr uint32_t callbacks[]{0x32D9C, 0x32E80, 0x32F68, 0x330B8, 0x331A0, 0x33288, 0x33370};
        for (uint32_t i = 0; i < 7; ++i)
            ui.SetSourceCallback(375 + i, callbacks[i]);
        ui.ShowRoot(state_.layout);
        if (!ui.Bind(state_.layout, state_.layout, 0))
            throw std::runtime_error("TITLE initial root bind");
        current_control_ = 0;
        while (ui.Control(current_control_))
            ++current_control_;
        auto *c = ui.AddControl(state_.layout);
        if (!c)
            throw std::runtime_error("TITLE current control");
        c->mode = c->active = 1;
        c->frame = 0;
        state_.menu_input_implemented = true;
        input_.BeginContext();
        return {};
    } catch (const std::exception &e) {
        return {GameFsCode::MalformedDirectory, 0, e.what()};
    }
}
GameFsStatus GameTitleScene::SetupLayout() {
    selection_motion_ = carousel_moving_ = false;
    state_.selection_moving = false;
    if (state_.layout != 3)
        state_.selection_motion_complete = false;
    if (state_.layout == 0)
        return ResetEntrance();
    if (state_.layout == 2 || state_.layout == 3) {
        constexpr float x[]{0, 2000, 4000, -4000, -2000};
        for (unsigned i = 0; i < 5; ++i) {
            auto &s = state_.actors[i];
            auto &a = actors_[i];
            const auto k = (5 - state_.selected_actor + i) % 5;
            s.x = state_.layout == 3 ? -600 : x[k];
            s.y = s.z = 0;
            s.moving = s.target_produced = false;
            s.target_x = s.target_y = 0;
            s.desired_visible = s.visible = i == state_.selected_actor;
            s.motion = 2;
            s.animation_time = s.movement_counter = 0;
            a.time = a.segment = 0;
            a.complete = false;
            a.carousel_step = 0;
            a.blend_left = 0;
            a.blend_from.clear();
            auto status = motions_[i][2].Apply(a.model, 0);
            if (!status)
                return status;
            status = a.model.Update(ActorRoot(s, state_.layout));
            if (!status)
                return status;
        }
        for (unsigned i = 5; i < 10; ++i)
            state_.actors[i].visible = state_.actors[i].desired_visible = false;
        return {};
    }
    if (state_.layout == 5||state_.layout==4) {
        for (unsigned i = 0; i < 5; ++i) {
            auto &s = state_.actors[i];
            auto &a = actors_[i];
            s.x = s.y = s.z = 0;
            s.moving = s.target_produced = s.visible = s.desired_visible = false;
            s.motion = 0;
            a.time = a.segment = 0;
            a.complete = false;
            a.carousel_step = 0;
            a.blend_left = 0;
            a.blend_from.clear();
            auto status = motions_[i][0].Apply(a.model, 0);
            if (!status)
                return status;
            status = a.model.Update(ActorRoot(s, state_.layout));
            if (!status)
                return status;
        }
        for (unsigned i = 5; i < 10; ++i)
            state_.actors[i].visible = state_.actors[i].desired_visible = false;
        return {};
    }
    return {GameFsCode::Unavailable, 0, "TITLE next layout constructor not yet supported"};
}
void GameTitleScene::BeginRoot(uint32_t root, GameTransition &fade) {
    state_.pending_root = int32_t(root);
    state_.state = 2;
    switching_root_ = true;
    fade.SetDuration(30);
    fade.CloseWipe();
    input_.BeginContext();
}
GameFsStatus GameTitleScene::MenuTick(GameTransition &fade) {
    input_.Tick();
    auto *current = ui_->Control(current_control_);
    if (!current)
        return {GameFsCode::Unavailable, 0, "TITLE current control retired"};
    if (state_.state != 1)
        return {};
    if (state_.layout == 5) {
        if (current->mode != 1)
            BeginRoot(0, fade);
        return {};
    }
    if (state_.layout == 0) {
        // Missing/default profile has124=-1.315E4 resetsF4=1 even after
        // navigation: cold Continue is not enabled by a native UI shortcut.
        const auto down = input_.Repeat(Down, 10, 2), up = input_.Repeat(Up, 10, 2);
        if (down || up) {
            ++state_.navigation_events;
            state_.menu_row = 1;
        }
        if (state_.selection == -1)
            state_.menu_row = 1;
        if (prior_row_ != state_.menu_row) {
            prior_row_ = state_.menu_row;
            auto *row = ui_->Control(controls_[6]);
            row->frame = state_.menu_row;
            row->mode = 2;
            row->active = 1;
            return {};
        }
        if (input_.First(Circle)) {
            if (state_.menu_row != 1)
                return {GameFsCode::Unavailable, 0, "unreached TITLE profile row"};
            ++state_.confirmations;
            BeginRoot(2, fade);
        }
    } else if (state_.layout == 2) {
        auto &selected = state_.actors[state_.selected_actor];
        if (selection_motion_) {
            if (actors_[state_.selected_actor].complete) {
                state_.selection_motion_complete = true;
                BeginRoot(3, fade);
            }
            return {};
        }
        if (carousel_moving_) {
            carousel_moving_ = selected.x != selected.target_x || selected.y != selected.target_y;
            state_.selection_moving = carousel_moving_;
            if (carousel_moving_)
                return {};
        }
        if (input_.First(Cross)) {
            BeginRoot(0, fade);
            return {};
        }
        const auto right = input_.Repeat(Right, 10, 3), left = input_.Repeat(Left, 10, 3);
        if (right || left) {
            state_.selected_actor = (state_.selected_actor + (right ? 1 : 4)) % 5;
            ++state_.navigation_events;
            constexpr float x[]{0, 2000, 4000, -4000, -2000};
            for (unsigned i = 0; i < 5; ++i) {
                const auto k = (5 - state_.selected_actor + i) % 5;
                auto &s = state_.actors[i];
                if (k == 0 || k == 1 || k == 4) {
                    s.visible = s.desired_visible = true;
                    s.target_x = x[k];
                    s.target_y = 0;
                    s.target_produced = s.moving = true;
                    actors_[i].carousel_step = right ? -200.0f : 200.0f;
                } else {
                    s.visible = s.desired_visible = false;
                    s.x = x[k];
                    s.y = s.z = 0;
                    s.moving = false;
                }
            }
            carousel_moving_ = state_.selection_moving = true;
            auto *side = ui_->Control(controls_[right ? 8 : 7]);
            side->frame = 2;
            side->mode = side->active = 1;
            return {};
        }
        if (input_.First(Circle)) {
            ++state_.confirmations;
            state_.pending_root = -1;
            constexpr const char *nicknames[]{"\x83\x67\x83\x8D", "\x83\x58\x83\x59\x83\x4C",
                                              "\x83\x57\x83\x85\x83\x93", "\x83\x73\x83\x47\x81\x5B\x83\x8B",
                                              "\x83\x8A\x83\x62\x83\x4C\x81\x5B"};
            nickname_ = nicknames[state_.selected_actor]; // 14490 exact CP932 bytes
            selected.motion = 3;
            auto &a = actors_[state_.selected_actor];
            a.blend_from = a.model.Pose();
            a.blend_left = 1280;
            a.time = 0;
            a.complete = false;
            selection_motion_ = true;
            // SGX playback unavailable: no native sound handle is created.
            // This is the explicit suppressed/no-mixer outcome174=-1.
        }
    } else if (state_.layout == 3) {
        state_.softkey_requested = true;
        if(!scene_ui_){state_.frontier_pc=0x1FD80;return {GameFsCode::Unavailable,0,"SOFTKEY owner absent from this session"};}
        if(!editor_waiting_){
            auto status=scene_ui_->BeginEditor(nickname_,4,"NICKNAME - Windows CP932 / Enter:OK Esc:Cancel");
            if(!status)return status;editor_waiting_=true;
        }else if(scene_ui_->Editor().source_state==3){
            editor_waiting_=false;
            if(const auto* name=scene_ui_->EditorResult()){
                nickname_=*name;profile_.Nickname(*name);state_.profile=profile_.Snapshot();BeginRoot(4,fade);
            }else BeginRoot(2,fade);
        }
    }else if(state_.layout==4){return ProfileTick(fade);
    }
    return {};
}
GameFsStatus GameTitleScene::ProfileTick(GameTransition& fade){
    if(confirmation_waiting_){
        const auto dialog=scene_ui_->Snapshot();
        if(dialog.dialog_state!=3)return {};
        confirmation_waiting_=false;
        const bool yes=dialog.dialog_result==1;
        if(!profile_.Confirm(yes,state_.selected_actor,calendar_))return {GameFsCode::Unavailable,0,"Incomplete profile confirmation"};
        state_.profile=profile_.Snapshot();
        if(yes){leaving_profile_=true;state_.state=2;fade.SetDuration(30);fade.CloseWipe();}
        else {confirmation_declined_=true;input_.BeginContext();}
        return {};
    }
    if(editor_waiting_){
        if(scene_ui_->Editor().source_state!=3)return {};
        std::string error;
        if(!profile_.Result(scene_ui_->EditorResult(),calendar_year_,error))
            return {GameFsCode::MalformedDirectory,0,error};
        editor_waiting_=false;state_.profile=profile_.Snapshot();return {};
    }
    const auto field=profile_.Next();const auto p=profile_.Snapshot();
    if(field==GameProfileField::Confirmation){
        if(confirmation_declined_&&!input_.First(Circle))return {};
        confirmation_declined_=false;auto status=scene_ui_->BeginProfileConfirmation();
        if(status)confirmation_waiting_=true;return status;
    }
    std::string initial,label;uint32_t maximum=4,mode=0;int32_t lo=0,hi=0;bool cancel=true;
    switch(field){
        case GameProfileField::Nickname:initial=p.nickname;label="NICKNAME / CP932 max4";break;
        case GameProfileField::Player:initial=p.player;label="PLAYER NAME / CP932 max4";break;
        case GameProfileField::Gender:mode=1;lo=1;hi=2;initial=p.gender==1?"2":"1";
            label="GENDER: 1=Female 2=Male / Left-Right Enter";cancel=p.present==7;break;
        case GameProfileField::Year:mode=3;lo=calendar_year_-100;hi=calendar_year_;initial=std::to_string(p.year>0?p.year:1980);
            label="BIRTH YEAR / digits or Left-Right Enter";cancel=p.zodiac!=-1;break;
        case GameProfileField::Month:mode=4;lo=1;hi=12;initial=std::to_string(p.month>0?p.month:1);label="BIRTH MONTH / Esc:back to year";break;
        case GameProfileField::Day:mode=5;lo=1;hi=GameProfile::Days(p.year,p.month);
            initial=std::to_string(p.day>0?(std::min)(p.day,hi):1);label="BIRTH DAY / Esc:back to month";break;
        case GameProfileField::Blood:mode=2;lo=1;hi=4;initial=std::to_string(p.blood>=0?p.blood+1:1);label="BLOOD: 1=A 2=B 3=AB 4=O";cancel=p.present==7;break;
        case GameProfileField::Comment:maximum=13;initial=p.comment;label="COMMENT / CP932 max13";break;
        default:return {GameFsCode::Unavailable,0,"Profile dispatcher field not implemented"};
    }
    profile_.Begin(field);state_.profile=profile_.Snapshot();
    const auto status=scene_ui_->BeginEditor(initial,maximum,label,mode,lo,hi,cancel);
    if(status)editor_waiting_=true;return status;
}
bool GameTitleScene::DrawUiCallback(uint32_t pc, uint32_t, const GameUiMatrix &world, uint32_t,
                                    GameGraphics &graphics, std::string &error) {
    constexpr uint32_t callbacks[]{0x32D9C, 0x32E80, 0x32F68, 0x330B8, 0x331A0, 0x33288, 0x33370};
    if (std::find(std::begin(callbacks), std::end(callbacks), pc) == std::end(callbacks)) {
        error = "unknown TITLE UI callback";
        return false;
    }
    const auto p=profile_.Snapshot();std::string text;
    if(pc==0x32D9C)text=nickname_;
    else if(pc==0x32E80)text=p.player;
    else if(pc==0x32F68&&p.gender!=-1)text=p.gender==2?"\x8f\x97":"\x92\x6a";
    else if(pc==0x330B8&&p.year!=-1)text=std::to_string(p.year)+"/"+std::to_string(p.month)+"/"+std::to_string(p.day);
    else if(pc==0x331A0&&p.zodiac!=-1){
        constexpr const char* zodiac_labels[]{"\x82\xa8\x82\xd0\x82\xc2\x82\xb6\x8d\xc0","\x82\xa8\x82\xa4\x82\xb5\x8d\xc0",
          "\x82\xd3\x82\xbd\x82\xb2\x8d\xc0","\x82\xa9\x82\xc9\x8d\xc0","\x82\xb5\x82\xb5\x8d\xc0",
          "\x82\xa8\x82\xc6\x82\xdf\x8d\xc0","\x82\xc4\x82\xf1\x82\xd1\x82\xf1\x8d\xc0","\x82\xb3\x82\xbb\x82\xe8\x8d\xc0",
          "\x82\xa2\x82\xc4\x8d\xc0","\x82\xe2\x82\xac\x8d\xc0","\x82\xdd\x82\xb8\x82\xaa\x82\xdf\x8d\xc0","\x82\xa4\x82\xa8\x8d\xc0"};
        text=zodiac_labels[p.zodiac];
    }
    else if(pc==0x33288&&p.blood!=-1){constexpr const char* blood[]{"A","B","AB","O"};text=blood[p.blood];}
    else if(pc==0x33370)text=p.comment;
    if(text.empty())return true;
    if (!font_) {
        error = "TITLE callback font retired";
        return false;
    }
    GameFontBitmap bitmap;
    const uint32_t source_row=pc==0x331A0?3u:uint32_t(std::find(std::begin(callbacks),std::end(callbacks),pc)-std::begin(callbacks));
    const uint32_t color=p.present==source_row?0xFFFDFDFD:0xFF552F0D;
    auto status = font_->RasterizeCp932({text}, 480, bitmap, static_cast<GameFontDrawRecord *>(text_),
                                        1024, color);
    if (!status) {
        error = status.detail;
        return false;
    }
    const auto config = graphics.Snapshot().config;
    float scale=1;
    if(pc==0x33370){
        scale=.75f;const auto width=bitmap.width*3/4;
        std::vector<uint32_t> scaled(size_t(width)*bitmap.height);
        for(uint32_t y=0;y<bitmap.height;++y)for(uint32_t x=0;x<width;++x)
            scaled[size_t(y)*width+x]=bitmap.pixels[size_t(y)*bitmap.width+uint64_t(x)*bitmap.width/width];
        bitmap.pixels=std::move(scaled);bitmap.width=width;
    }
    // EC68 alignment1 uses actual XFT advance,not CP932 byte count.
    return graphics.DrawImage(bitmap.pixels, bitmap.width, bitmap.height,
                              int32_t(float(config.width) / 2 + world.tx - float(bitmap.advance_width)*scale / 2),
                              int32_t(float(config.height) / 2 + world.ty));
}
GameFsStatus GameTitleScene::TickAndDraw(GameGraphics &graphics, GameTransition &fade) {
    if (!state_.live)
        return {GameFsCode::Unavailable, 0, "TITLE not live"};
    try {
        if(scene_ui_&&!scene_ui_->TickEditor())return {GameFsCode::Unavailable,0,"editor animation/source lease"};
        // 30C34 case2 waits on the actual D54C owner, resets335A4, then
        // 30E48 DA64 starts source fade1. No direct199DC fade call is invented.
        bool reset = false;
        if (state_.state == 2 && fade.Snapshot().state == 0) {
            if(leaving_profile_){state_.state=3;state_.scene3_completed=true;state_.frontier_pc=0x1A89C;
                return {};}
            if (ui_ && switching_root_) {
                ui_->ShowRoot(state_.layout, false);
                auto *current = ui_->Control(current_control_);
                current->mode = current->active = 0;
                state_.layout = uint32_t(state_.pending_root);
                if (!ui_->Bind(state_.layout, state_.layout, 0) || !ui_->ShowRoot(state_.layout))
                    return {GameFsCode::MalformedDirectory, 0, "TITLE root switch failed"};
                current->clip = state_.layout;
                current->frame = 0;
                current->mode = current->active = 1;
                input_.BeginContext();
                switching_root_ = false;
            }
            auto status = SetupLayout();
            if (!status)
                return status;
            reset = true;
            if (state_.layout == 5 || state_.layout == 29)
                state_.state = 1;
            else {
                fade.SetDuration(30);
                fade.Open();
                if (fade.Snapshot().state != 1)
                    return {GameFsCode::Unavailable, 0, "TITLE DA64 fade refused"};
                state_.state = 0;
            }
        } else if (state_.state == 0 && fade.Snapshot().state == 2)
            state_.state = 1;
        ++state_.updates;
        state_.visible_actors = 0;
        for (unsigned i = 0; i < 10; ++i) {
            auto &actor = actors_[i];
            auto &s = state_.actors[i];
            // 08A88 uses the previous animation counter;3B01C publishes the
            // new floor(time/160) after movement. Native80 units/tick at60Hz.
            if (s.moving && !reset) {
                const float step = actor.carousel_step != 0
                                       ? std::abs(actor.carousel_step)
                                       : float(TitleMovementStep(s.type, s.movement_counter, actor.segment));
                if (std::abs(s.x - s.target_x) <= step) {
                    s.x = s.target_x;
                    s.moving = false;
                } else
                    s.x += s.x > s.target_x ? -step : step;
            }
            auto &motion = motions_[s.type][s.motion];
            auto status = motion.Apply(actor.model, actor.time);
            if (!status)
                return status;
            if (actor.blend_left && !actor.blend_from.empty()) {
                const float t = 1.0f - float(actor.blend_left) / 1280.0f;
                auto &poses = actor.model.Pose();
                if (poses.size() != actor.blend_from.size())
                    return {GameFsCode::MalformedDirectory, 0, "TITLE blend owner mismatch"};
                for (size_t p = 0; p < poses.size(); ++p) {
                    auto &now = poses[p];
                    const auto &from = actor.blend_from[p];
                    float dot = 0, norm = 0;
                    for (unsigned j = 0; j < 4; ++j)
                        dot += from.rotation[j] * now.rotation[j];
                    for (unsigned j = 0; j < 4; ++j) {
                        now.position[j] = from.position[j] + (now.position[j] - from.position[j]) * t;
                        now.scale[j] = from.scale[j] + (now.scale[j] - from.scale[j]) * t;
                        now.rotation[j] =
                            from.rotation[j] +
                            ((dot < 0 ? -now.rotation[j] : now.rotation[j]) - from.rotation[j]) * t;
                        norm += now.rotation[j] * now.rotation[j];
                    }
                    if (norm > 0)
                        for (auto &v : now.rotation)
                            v /= std::sqrt(norm);
                }
                actor.blend_left = actor.blend_left > state_.native_tick_units
                                       ? actor.blend_left - state_.native_tick_units
                                       : 0;
            }
            s.animation_time = actor.time;
            s.movement_counter = actor.time / 160;
            // The reached run completion path seeks160 rather than0.
            if (s.motion == 3) {
                actor.complete = actor.time >= motion.Statistics().duration;
                actor.time = (std::min)(motion.Statistics().duration, actor.time + state_.native_tick_units);
            } else {
                actor.time += state_.native_tick_units;
                if (actor.time >= motion.Statistics().duration)
                    actor.time = (std::min)(160u, motion.Statistics().duration);
            }
            // Debug identity of local animated values, excluding the actor's
            // outer movement matrix. Used to verify real DAK changes in a run.
            s.local_pose_digest = 14695981039346656037ull;
            for (const auto &pose : actor.model.Pose())
                for (const auto &vector : {pose.position, pose.rotation})
                    for (auto value : vector) {
                        uint32_t word;
                        std::memcpy(&word, &value, 4);
                        s.local_pose_digest ^= word;
                        s.local_pose_digest *= 1099511628211ull;
                    }
            status = actor.model.Update(ActorRoot(s, state_.layout));
            if (!status)
                return status;
        }
        // Native device adapter: mscmhc0: is unavailable on Windows, so the
        // source4E67C numeric result is0. This does not describe the native save
        // directory or manufacture insertion/valid PSP savedata. raw2A950's
        // unclosed flag uses an explicit disabled host policy,not BSS inference.
        TitleArrivalCycle(state_, rng_, false, false);
        for (unsigned i = 0; i < 10; ++i)
            if (actors_[i].movement_epoch != state_.actors[i].movement_epoch) {
                actors_[i].segment = 0;
                actors_[i].carousel_step = 0;
                actors_[i].movement_epoch = state_.actors[i].movement_epoch;
            }
        if (ui_) {
            auto status = MenuTick(fade);
            if (!status)
                return status;
        }
        for (const auto &actor : state_.actors)
            if (actor.visible)
                ++state_.visible_actors;
        if (!graphics.Clear(0xFF000000, 0))
            return {GameFsCode::Unavailable, 0, "TITLE surface clear failed"};
        const auto before = graphics.Snapshot().mesh_pixels;
        if (ui_) {
            std::string error;
            GameUiDrawStats stats;
            if (!ui_->Update(error) || !ui_->Draw(graphics, stats, error, this, {}, 0, 0))
                return {GameFsCode::Unavailable, 0, error};
            state_.ui_quads += stats.quads;
            state_.ui_callbacks += stats.callbacks;
        }
        const auto model_before = graphics.Snapshot().mesh_pixels;
        for (unsigned i = 0; i < 10; ++i)
            if (state_.actors[i].visible) {
                auto status = actors_[i].model.Draw(graphics, camera_);
                if (!status)
                    return status;
            }
        state_.model_pixel_writes += graphics.Snapshot().mesh_pixels - model_before;
        if (ui_) {
            std::string error;
            GameUiDrawStats stats;
            if (!ui_->Draw(graphics, stats, error, this, {}, 3, 26))
                return {GameFsCode::Unavailable, 0, error};
            state_.ui_quads += stats.quads;
            state_.ui_callbacks += stats.callbacks;
            state_.visible_roots = ui_->VisibleRoots();
            if(state_.layout==4){
                //32B04 explicitly draws root18; it is not a visible external root.
                if(!ui_->Draw(graphics,stats,error,this,{},0,UINT32_MAX,18))
                    return {GameFsCode::Unavailable,0,error};
                state_.ui_quads+=stats.quads;state_.ui_callbacks+=stats.callbacks;
            }
        }
        if(scene_ui_&&(!scene_ui_->DrawEditor(graphics)||!scene_ui_->DrawProfileConfirmation(graphics)))
            return {GameFsCode::Unavailable,0,"editor/TRC glyph draw failed"};
        fade.Update();
        if (!fade.Draw(graphics) || !graphics.SwapBuffers())
            return {GameFsCode::Unavailable, 0, "TITLE fade/present failed"};
        state_.pixel_writes += graphics.Snapshot().mesh_pixels - before;
        ++state_.frames;
        state_.frame_drawn = state_.pixel_writes > 0;
        // Scene3's1A89C still waitsCC3. Unrecovered menu/random-restart/input
        // consumers are not turned into a completed scene or guessed KSC call.
        state_.frontier_pc = state_.layout>=3 ? (state_.layout==4?0x31D44:0x20E48) : (state_.state == 1 ? 0x33B90 : 0x30EB0);
        return {};
    } catch (const std::exception &e) {
        return {GameFsCode::Unavailable, 0, e.what()};
    }
}
bool GameTitleScene::Close() {
    scene_ui_=nullptr;
    ui_ = nullptr;
    font_ = nullptr;
    rng_ = nullptr;
    input_.BeginContext();
    nickname_.clear();
    for (auto &a : actors_) {
        a.model.Close();
        a.blend_from.clear();
        a.blend_left = 0;
    }
    for (auto &m : motions_)
        m.clear();
    for (auto &a : assets_)
        a.reset();
    if (manager_ && task_ != UINT32_MAX) {
        if (!manager_->ReleaseTask(task_))
            return false;
        task_ = UINT32_MAX;
    }
    if (arena_ && text_) {
        if (!arena_->Release(text_))
            return false;
        text_ = nullptr;
    }
    if (arena_ && owner_) {
        if (!arena_->Release(owner_))
            return false;
        owner_ = nullptr;
    }
    arena_ = nullptr;
    manager_ = nullptr;
    state_.live = false;
    state_.profile.stored.live=false;
    state_.clones = 0;
    return true;
}
} // namespace dokodemo::recovered
