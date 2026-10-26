#include "game_scene_ui_native.h"
#include "game_boot_native.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <new>
#include <stdexcept>

namespace dokodemo::recovered {
bool GameDialogController::Begin(uint32_t mode, uint32_t selection) noexcept {
    if (state_.state != 3 || mode > 2 || selection > INT32_MAX - 1)
        return false;
    const auto confirmations = state_.confirmations;
    state_ = {};
    state_.state = 0;
    state_.mode = mode;
    state_.selection = selection;
    state_.confirmations = confirmations;
    gate_.Begin();
    cancel_gate_.Begin();
    return true;
}
bool GameDialogController::Input(const GameInputFrame &frame) noexcept {
    if (state_.state != 1 || state_.mode == 2)
        return false;
    const bool circle=gate_.Observe(frame, Circle);
    if(state_.mode==0&&cancel_gate_.Observe(frame,Cross)){
        state_.result=2;state_.state=2;return true;
    }
    if (!circle)
        return false;
    state_.result = int32_t(state_.selection + 1);
    ++state_.confirmations;
    state_.state = 2;
    return true;
}
void GameDialogController::Tick() noexcept {
    if (state_.state == 0) {
        state_.progress = (std::min)(1.0f, state_.progress + 0.2f);
        if (state_.progress >= 1)
            state_.state = 1;
    } else if (state_.state == 2) {
        state_.progress -= 0.2f;
        if (state_.progress <= 0) {
            state_.progress = 0;
            state_.state = 3;
            gate_.Stop();
        }
    }
}
bool GameTransition::Initialize(const std::array<float, 2> &center) {
    if (state_.live)
        return false;
    state_ = {};
    if (!SetCenter(center))
        return false;
    state_.live = true;
    return true;
}
bool GameTransition::SetCenter(const std::array<float, 2> &center) {
    if (!std::isfinite(center[0]) || !std::isfinite(center[1]))
        return false;
    state_.x = (std::clamp)(center[0], -240.0f, 240.0f);
    state_.y = (std::clamp)(center[1], -136.0f, 136.0f);
    state_.maximum_radius = 0;
    for (float x : {-240.5f, 240.5f})
        for (float y : {-136.5f, 136.5f})
            state_.maximum_radius = (std::max)(state_.maximum_radius, std::hypot(x - state_.x, y - state_.y));
    SetDuration(state_.duration);
    return true;
}
void GameTransition::SetDuration(uint32_t ticks) {
    state_.duration = ticks;
    state_.increment = ticks ? state_.maximum_radius / float(ticks) : 0;
}
void GameTransition::Open(bool immediate) {
    if (!state_.live || state_.state == 1 || state_.state == 2)
        return;
    state_.state = immediate ? 2u : 1u;
    if (immediate)
        state_.radius = state_.maximum_radius;
}
bool GameTransition::StartGameOpen() {
    if(!state_.live||!SetCenter({{0,0}}))return false;
    state_.group=5;Open(false);return true;
}
void GameTransition::CloseWipe(bool immediate) {
    if (!state_.live)
        return;
    state_.state = immediate ? 0u : 3u;
    if (immediate)
        state_.radius = -50;
}
void GameTransition::Update() {
    if (!state_.live)
        return;
    if (state_.state == 1) {
        state_.radius += state_.increment;
        if (state_.radius >= state_.maximum_radius) {
            state_.radius = state_.maximum_radius;
            state_.state = 2;
        }
    } else if (state_.state == 3) {
        state_.radius -= state_.increment;
        if (state_.radius <= -50) {
            state_.radius = -50;
            state_.state = 0;
        }
    }
}
bool GameTransition::Draw(GameGraphics &graphics) const {
    if (!state_.live)
        return false;
    if (state_.state == 2)
        return true; // actual D754 branch skips drawing
    const float inner = (std::max)(state_.radius, 0.0f);
    const float outer = (std::max)(state_.radius + 50, inner);
    return graphics.DrawRadialFade(state_.x + 240, state_.y + 136, inner, outer);
}

GameSceneUiBootstrap::~GameSceneUiBootstrap() {
    (void)Close();
}
GameFsStatus GameSceneUiBootstrap::Initialize(GameResourceSource &source, GameArena &arena,
                                              GameVmBoot &manager, const GameFontResource &font) {
    if (arena_ || !font.Snapshot().ready)
        return {GameFsCode::Busy, 0, "scene/font ownership precondition"};
    arena_ = &arena;
    manager_ = &manager;
    font_=&font;
    state_ = {};
    state_.live = true;
    try {
        constexpr uint32_t record_counts[]{2, 1, 1, 2, 2};
        const auto owner = [&](uint32_t index) {
            auto *p = arena.Allocate(sizeof(Owner), alignof(Owner));
            if (!p)
                throw std::runtime_error("native scene owner allocation failed");
            owners_[index] = new (p) Owner{};
            ++state_.owners;
            tasks_[index] = manager.AcquireTask();
            if (tasks_[index] == UINT32_MAX)
                throw std::runtime_error("native scene task slots exhausted");
            state_.records += record_counts[index];
        };
        owner(0);
        owners_[0]->state = 0;
        owners_[0]->group = 5;
        // Static ctor17B0->15F8 really produced this point; not BSS inference.
        if (!transition_.Initialize({{0, 0}}))
            throw std::runtime_error("fade initialization");
        constexpr const char *prefixes[]{"data\\gui\\select\\", "data\\gui\\softkey\\", "data\\gui\\trc\\"};
        constexpr const char *names[]{"SELECT.CB2A", "SOFTKEY.CB2A", "TRC.CB2A"};
        constexpr uint32_t capacities[]{1024, 41, 1024};
        for (uint32_t i = 0; i < 3; ++i) {
            owner(i + 1);
            auto resource = std::make_shared<GameUiResource>();
            auto status = resource->Load(source, std::string(prefixes[i]) + names[i], prefixes[i]);
            if (!status)
                return status;
            resources_[i] = std::move(resource);
            if (!clones_[i].Clone(resources_[i]))
                throw std::runtime_error("UI clone construction failed");
            const auto bind = [&](uint32_t node, uint32_t clip, uint32_t frame = 0) {
                if (!clones_[i].Bind(node, clip, frame))
                    throw std::runtime_error("source-selected UI base pose is invalid");
            };
            const auto control = [&](uint32_t clip, uint32_t frame = 0, uint8_t mode = 0,
                                     uint8_t active = 0) {
                auto *c = clones_[i].AddControl(clip);
                if (!c)
                    throw std::runtime_error("source-selected UI control unavailable");
                c->frame = frame;
                c->mode = mode;
                c->active = active;
            };
            if (i == 0) {
                bind(0, 0);
                bind(1, 1);
                bind(2, 13);
                bind(3, 25, 1);
                control(1);
                control(0, 1, 2, 1);
                control(25, 2, 2, 1);
            } else if (i == 1) {
                bind(0, 0);
                bind(151, 1);
                bind(1, 7);
                bind(2, 8);
                bind(3, 9);
                bind(4, 10);
                bind(7, 13);
                bind(6, 12);
                bind(5, 11);
                bind(8, 14);
                bind(9, 15);
                control(15);
                control(0, 1, 2, 1);
                control(2, 1, 2, 1);
                control(6, 1, 2, 1);
                control(11, 1, 2, 1);
                control(5, 13, 2, 1);
                control(14, 1, 2, 1);
                for (uint32_t clip : {4u, 7u, 8u, 9u, 10u, 13u, 12u})
                    control(clip);
            } else {
                bind(0, 0);
                bind(1, 1);
                control(0, 1);
                control(1, 1, 2, 1);
            }
            ++state_.ui_resources;
            state_.ui_nodes += uint32_t(resources_[i]->InitialNodes().size());
            state_.ui_clips += uint32_t(resources_[i]->Clips().size());
            // E4A0 capacities count44-byte draw records, not individual bytes.
            const auto bytes = capacities[i] * 44;
            text_buffers_[i] = arena.Allocate(bytes);
            if (!text_buffers_[i])
                throw std::runtime_error("glyph record allocation failed");
            state_.text_capacity_bytes += bytes;
            if (i == 1) {
                constexpr const char *dictionaries[]{"NJCON.A",  "NJBASE.A",  "NJFZK.A", "NJTAN.A",
                                                     "NJYOMI.A", "NJLEARN.A", "CUSTOM.A"};
                for (uint32_t d = 0; d < 7; ++d) {
                    PackageView data;
                    status = source.OpenResource(std::string("data\\gui\\DIC\\") + dictionaries[d], data);
                    if (!status)
                        return status;
                    status = dictionaries_[d].Attach(std::move(data), d ? 3 : 0);
                    if (!status) {
                        status.detail = std::string(dictionaries[d]) + ": " + status.detail;
                        return status;
                    }
                    ++state_.borrowed_dictionaries;
                }
                if (!dictionaries_[7].InitializeMutable(arena, 1) ||
                    !dictionaries_[8].InitializeMutable(arena, 2))
                    throw std::runtime_error("mutable NJ dictionaries unavailable");
                state_.mutable_dictionaries = 2;
            }
        }
        owner(4);
        auto status = source.TextureResource("data\\gui\\moving\\moving.GIM", moving_);
        if (!status)
            return status;
        state_.textures = source.TextureCacheSize();
        state_.texture_pixels = source.TextureCachePixels();
        // All cloned roots remain hidden by97A54/964F8. We do not call the
        // unimplemented visible96F20/callback renderer or advertise text draw.
        state_.constructors_returned = true;
        state_.scene1_returned = true;
        return {};
    } catch (const std::exception &e) {
        return {GameFsCode::Unavailable, 0, e.what()};
    }
}
bool GameSceneUiBootstrap::DrawColdFrame(GameGraphics &graphics) {
    if (!state_.constructors_returned)
        return false;
    for (size_t i = 0; i < clones_.size(); ++i) {
        auto &clone = clones_[i];
        if (clone.VisibleRoots() && !(i == 2 && state_.storage_dialog))
            return false;
        std::string error;
        if (!clone.Update(error))
            return false;
    }
    // Source cold phase6 owners are inert; phase9 D708 really draws its fade.
    if (!graphics.Clear(0xFF000000, 0) || !transition_.Draw(graphics))
        return false;
    if (state_.storage_dialog) {
        // Native tick quanta2 /10, using the retained backed initial value.
        // PSP runtime timing producer/matrix matching remain unrecovered.
        dialog_.Tick();
        const auto dialog = dialog_.Snapshot();
        state_.dialog_progress = dialog.progress;
        state_.dialog_state = dialog.state;
        state_.dialog_result = dialog.result;
        state_.confirmations = dialog.confirmations;
        state_.awaiting_confirmation = dialog.state == 1;
        if (dialog.state == 3) {
            state_.storage_dialog = false;
            clones_[2].ShowRoot(0, false);
            if (auto *control = clones_[2].Control(0)) {
                control->mode = 0;
                control->active = 0;
            }
            if (!graphics.SwapBuffers())
                return false;
            ++state_.native_frames;
            return true;
        }
        const auto scale = state_.dialog_progress;
        const auto w = (std::max)(1u, uint32_t(float(dialog_width_) * scale));
        const auto h = (std::max)(1u, uint32_t(float(dialog_height_) * scale));
        std::vector<uint32_t> pixels(size_t(w) * h);
        for (uint32_t y = 0; y < h; ++y)
            for (uint32_t x = 0; x < w; ++x)
                pixels[size_t(y) * w + x] =
                    dialog_pixels_[size_t(uint64_t(y) * dialog_height_ / h) * dialog_width_ +
                                   uint64_t(x) * dialog_width_ / w];
        const auto x = (480 - int32_t(w)) / 2, y = (272 - int32_t(h)) / 2;
        // Explicit functional TRC panel adapter. Original FONT glyphs/text and
        // state progression are used; this rectangle is not claimed CB2A/VFPU
        // pixel matching or the general96F20 callback renderer.
        if (!graphics.FillRectangle(x - 16, y - 16, x + int32_t(w) + 16, y + int32_t(h) + 16, 0xFFE9E3CE) ||
            !graphics.DrawImage(pixels, w, h, x, y))
            return false;
        state_.text_drawn = true;
    }
    if (!graphics.SwapBuffers())
        return false;
    ++state_.native_frames;
    return true;
}
bool GameChoiceController::Begin(std::vector<std::string> options,uint64_t context,uint64_t owner,uint32_t timeout,uint32_t confirm_only_row){
    if((state_.live&&state_.state!=3)||options.empty()||options.size()>32||!context||!owner)return false;
    for(const auto& text:options)if(text.size()>1023||text.find('\0')!=std::string::npos)return false;
    if(confirm_only_row!=UINT32_MAX&&confirm_only_row>=options.size())return false;
    state_={};state_.live=true;state_.state=0;state_.context=context;state_.owner_generation=owner;
    state_.remaining=timeout;state_.confirm_only_row=confirm_only_row;state_.options=std::move(options);input_.BeginContext();return true;
}
bool GameChoiceController::Input(const GameInputFrame& frame){
    if(frame.overflow_resync){input_.BeginContext();++state_.rejected_inputs;return false;}
    if(frame.sequence<=sequence_){++state_.rejected_inputs;return false;}sequence_=frame.sequence;
    if(!state_.live||state_.state!=1||frame.message_context!=state_.context||frame.message_owner_generation!=state_.owner_generation){
        ++state_.rejected_inputs;return false;
    }
    input_.Observe(frame);return true;
}
void GameChoiceController::Tick(uint8_t mode,bool trc_idle){
    if(!state_.live)return;
    if(state_.state==0){if(mode!=1){state_.state=1;input_.BeginContext();}return;}
    if(state_.state==2){if(mode!=1){state_.state=3;input_.BeginContext();}return;}
    if(state_.state!=1)return;
    input_.Tick();if(!trc_idle)return;
    ++state_.active_updates;
    const auto last=uint32_t(state_.options.size()-1);
    const auto down=input_.Repeat(Down,10,2),up=input_.Repeat(Up,10,2);
    if(down){if(state_.row<last)++state_.row;else if(down==1)state_.row=0;}
    else if(up){if(state_.row)--state_.row;else if(up==1)state_.row=last;}
    state_.result=state_.row+1;
    //10844 positive deadline policy1: expiry wins over same-tick Circle.
    if(state_.remaining&&!--state_.remaining){state_.result=0;state_.timed_out=true;state_.state=2;return;}
    if(input_.First(Circle)){
        if(state_.confirm_only_row!=UINT32_MAX&&state_.row!=state_.confirm_only_row){++state_.unsupported;input_.BeginContext();return;}
        ++state_.confirmations;state_.state=2;
    }
    //FFFC(0): Cross/Escape is deliberately not a result producer.
}
GameFsStatus GameSceneUiBootstrap::BeginChoice(std::vector<std::string> options,uint64_t context,uint64_t owner,uint32_t confirm_only_row){
    if(!state_.live||!font_||(editor_.Snapshot().live&&editor_.Snapshot().source_state!=3))return {GameFsCode::Busy,0,"SELECT owner busy/retired"};
    if(!choice_.Begin(std::move(options),context,owner,5400,confirm_only_row))return {GameFsCode::Busy,0,"SELECT request/options/context invalid"};
    auto& gui=clones_[0];auto* main=gui.Control(0);auto* auxiliary=gui.Control(1);auto* cursor=gui.Control(2);
    if(!main||!auxiliary||!cursor||!gui.Bind(2,13,0)||!gui.ShowRoot(2))return {GameFsCode::Unavailable,0,"SELECT mode0 root2/clip13 unavailable"};
    main->clip=13;main->frame=0;main->mode=main->active=1;
    auxiliary->clip=0;auxiliary->frame=uint32_t(choice_.Snapshot().options.size());auxiliary->mode=2;auxiliary->active=1;
    cursor->clip=25;cursor->frame=1;cursor->mode=2;cursor->active=1;return {};
}
GameFsStatus GameSceneUiBootstrap::TickChoice(){
    const auto before=choice_.Snapshot();if(!before.live||before.state==3)return {};
    auto& gui=clones_[0];std::string error;if(!gui.Update(error))return {GameFsCode::Unavailable,0,error};
    auto* main=gui.Control(0);if(!main)return {GameFsCode::Unavailable,0,"SELECT main control retired"};
    choice_.Tick(main->mode,dialog_.Snapshot().state==3);const auto after=choice_.Snapshot();
    if(after.unsupported)return {GameFsCode::Unavailable,0,"Outing destination confirmation unsupported before SELECT close/result"};
    if(before.state==1&&after.state==2){
        if(!gui.Bind(2,14,0))return {GameFsCode::Unavailable,0,"SELECT close clip14 unavailable"};
        main->clip=14;main->frame=0;main->mode=main->active=1;
    }
    if(after.state==3){gui.ShowRoot(2,false);main->mode=main->active=0;if(auto* auxiliary=gui.Control(1))auxiliary->mode=auxiliary->active=0;}
    return {};
}
bool GameSceneUiBootstrap::DrawChoice(GameGraphics& graphics){
    choice_draw_={};
    const auto fail=[&](GameChoiceDrawCode code){choice_draw_.code=code;return false;};
    const auto choice=choice_.Snapshot();if(!choice.live||choice.state==3)return true;
    if(!font_||!font_->Snapshot().ready)return fail(GameChoiceDrawCode::Owner);
    // Mode0 already owns SELECT.CB2A root2/clip13, auxiliary control0 and
    // cursor control25. Draw those actual source textures instead of painting
    // full-width rectangles over them. FONT remains a native display adapter.
    std::vector<GameFontBitmap> rows;uint32_t height=0;
    for(uint32_t i=0;i<choice.options.size();++i){
        //Same finite NativeNoHighlight display projection as KSC messages.
        //The owned option, its ordering and SELECT result remain unchanged.
        std::string visible;
        for(unsigned char c:choice.options[i]){
            if(c==0x16)continue;
            if(c<32&&c!=10)return fail(GameChoiceDrawCode::TextControl);
            visible.push_back(char(c));
        }
        GameFontBitmap text;const auto drawn=font_->RasterizeCp932({visible},160,text,nullptr,0,0xFF552F0D,21);
        if(!drawn)return fail(GameChoiceDrawCode::Font);
        if(height>UINT32_MAX-text.height)return fail(GameChoiceDrawCode::Overflow);
        height+=text.height;rows.push_back(std::move(text));
    }
    choice_draw_.total_height=height;
    const auto config=graphics.Snapshot().config;
    // Named Windows NONMATCHING render-only window. Whole measured rows,
    // unchanged21px line advance/160px width; no source scrolling claim.
    if(config.height<=40||choice.row>=rows.size())return fail(GameChoiceDrawCode::Viewport);
    const auto budget=(std::min)(11u*21u,config.height-40u);
    for(const auto& row:rows)
        if(!row.height||row.height>budget)return fail(GameChoiceDrawCode::RowTooTall);
    uint32_t first=0,end=0,visible_height=0;
    for(uint32_t i=0;i<=choice.row;++i){
        while(visible_height>budget-rows[i].height)visible_height-=rows[first++].height;
        visible_height+=rows[i].height;
    }
    end=choice.row+1;
    while(end<rows.size()&&rows[end].height<=budget-visible_height)visible_height+=rows[end++].height;
    choice_draw_.first=first;choice_draw_.end=end;choice_draw_.visible_height=visible_height;
    choice_draw_.viewport=first!=0||end!=rows.size();
    choice_draw_.selected_height=rows[choice.row].height;
    auto& gui=clones_[0];auto* auxiliary=gui.Control(1);auto* cursor=gui.Control(2);
    if(!auxiliary||!cursor)return fail(GameChoiceDrawCode::Ui);
    const auto units=visible_height/21;auxiliary->frame=units>=12?14:units;
    std::string error;
    if(!gui.SampleControl(0,*auxiliary,error)||!gui.SampleControl(3,*cursor,error))return fail(GameChoiceDrawCode::Ui);
    // WorldMatrix follows the visible root2 -> node43 -> root0 reference.
    // It already includes the ancestor transform; never compose it twice.
    GameUiMatrix anchor;if(!gui.WorldMatrix(0,anchor))return fail(GameChoiceDrawCode::Ui);
    // Render-only viewport fit; no change to SELECT's logical/source position.
    GameUiMatrix base;const auto bottom=float(config.height)*.5f+anchor.ty+float(visible_height)+24;
    base.ty=(std::min)(0.f,float(config.height)-8-bottom);
    if(choice_draw_.viewport&&float(config.height)*.5f+anchor.ty+base.ty<8)base.ty=8-float(config.height)*.5f-anchor.ty;
    GameUiDrawStats stats;if(!gui.Draw(graphics,stats,error,nullptr,base,0,UINT32_MAX,2))return fail(GameChoiceDrawCode::Ui);
    const auto x=int32_t(std::lround(float(config.width)*.5f+anchor.tx));
    auto y=int32_t(std::lround(float(config.height)*.5f+anchor.ty+base.ty));
    choice_draw_.text_x=x;choice_draw_.text_y=y;
    for(uint32_t i=first;i<end;++i){
        const auto& text=rows[i];
        if(!graphics.DrawImage(text.pixels,text.width,text.height,x,y,0xFFFFFFFF,65535))return fail(GameChoiceDrawCode::Ui);
        if(i==choice.row){
            choice_draw_.selected_y=choice_draw_.cursor_y=y;
            GameUiMatrix selection;selection.tx=anchor.tx;selection.ty=float(y)-float(config.height)*.5f;
            if(!gui.Draw(graphics,stats,error,nullptr,selection,0,UINT32_MAX,3))return fail(GameChoiceDrawCode::Ui);
        }
        y+=int32_t(text.height);
    }
    choice_draw_.code=GameChoiceDrawCode::Drawn;return true;
}
GameFsStatus GameSceneUiBootstrap::ShowStorageDialog(const GameFontResource &font, uint32_t error) {
    if (!state_.constructors_returned || state_.storage_dialog)
        return {GameFsCode::Busy, 0, "TRC native dialog ownership/state"};
    // Exact source CP932 bytes from D8BA8/D8B2C/D8B64, retained in
    // functional-first/scene-source-v1/LITERALS.json. No translated placeholder.
    const std::vector<std::string> messages{
        "\x22\x83\x81\x83\x82\x83\x8a\x81\x5b\x83\x58\x83\x65\x83\x42\x83\x62\x83\x4e\x22\x82\xaa\x90\xb3\x82"
        "\xb5\x82\xad\x8d\xb7\x82\xb5\x8d\x9e\x82\xdc\x82\xea\x82\xc4\x82\xa2\x82\xdc\x82\xb9\x82\xf1\x81"
        "\x42",
        "\x82\xb1\x82\xcc\x83\x51\x81\x5b\x83\x80\x82\xcc\x83\x66\x81\x5b\x83\x5e\x82\xf0\x83\x5a\x81\x5b\x83"
        "\x75\x82\xb7\x82\xe9\x82\xc9\x82\xcd\x38\x30\x30\x4b\x42\x88\xc8\x8f\xe3\x95\x4b\x97\x76\x82\xc5\x82"
        "\xb7\x81\x42",
        "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20"
        "\x20\x20\x20\x20\x20\x20\x20\x81\x9b\x81\x46\x8a\x6d\x94\x46"};
    GameFontBitmap bitmap;
    auto status =
        font.RasterizeCp932(messages, 320, bitmap, static_cast<GameFontDrawRecord *>(text_buffers_[2]), 1024);
    if (!status)
        return status;
    if (!clones_[2].ShowRoot(0))
        return {GameFsCode::Unavailable, 0, "TRC root0 unavailable"};
    if (!dialog_.Begin(1, 0))
        return {GameFsCode::Busy, 0, "TRC dialog already active"};
    dialog_pixels_ = std::move(bitmap.pixels);
    dialog_width_ = bitmap.width;
    dialog_height_ = bitmap.height;
    state_.storage_dialog = true;
    state_.dialog_glyphs = bitmap.glyphs;
    state_.storage_error = error;
    state_.dialog_progress = 0;
    state_.dialog_state = 0;
    state_.dialog_result = 0;
    state_.awaiting_confirmation = false;
    return {};
}
bool GameSceneUiBootstrap::HandleDialogInput(const GameInputFrame &frame) {
    if (!state_.live || !state_.storage_dialog)
        return false;
    const auto accepted = dialog_.Input(frame);
    const auto dialog = dialog_.Snapshot();
    state_.dialog_state = dialog.state;
    state_.dialog_result = dialog.result;
    state_.confirmations = dialog.confirmations;
    state_.awaiting_confirmation = dialog.state == 1;
    return accepted;
}
GameSceneUiSnapshot GameSceneUiBootstrap::Snapshot() const {
    auto out = state_;
    out.transition = transition_.Snapshot();
    out.editor=editor_.Snapshot();
    out.visible_roots = 0;
    for (const auto &clone : clones_)
        out.visible_roots += clone.VisibleRoots();
    return out;
}
GameFsStatus GameSceneUiBootstrap::PrepareOptionsPrompt(){
    // Attach a typed borrow to the existing47784 resources; no new clone or
    // source idle/readiness publication is produced by this operation.
    if(!state_.live||!owners_[3]||!font_||dialog_.Snapshot().state!=3||state_.storage_dialog||profile_confirmation_||clones_[2].VisibleRoots())
        return {GameFsCode::Unavailable,0,"Options requires idle actual global TRC owner"};
    if(options_prompt_.Snapshot().live)return {};
    return options_prompt_.Initialize(clones_[2],resources_[2],*font_,text_buffers_[2],1024);
}
bool GameSceneUiBootstrap::Close() {
    options_prompt_.Close(); //borrowed global before clone/template/text/FONT
    choice_.Retire();
    choice_draw_={};
    editor_.Retire();font_=nullptr;
    profile_confirmation_=false;
    std::vector<uint32_t>().swap(dialog_pixels_);
    dialog_width_ = dialog_height_ = 0;
    moving_.reset();
    for (auto &c : clones_)
        c.Close();
    for (auto &r : resources_)
        r.reset();
    for (auto d = dictionaries_.rbegin(); d != dictionaries_.rend(); ++d)
        if (!d->Close())
            return false;
    if (arena_) {
        for (auto b = text_buffers_.rbegin(); b != text_buffers_.rend(); ++b)
            if (*b) {
                if (!arena_->Release(*b))
                    return false;
                *b = nullptr;
            }
        for (size_t i = owners_.size(); i--;) {
            if (tasks_[i] != UINT32_MAX) {
                if (!manager_->ReleaseTask(tasks_[i]))
                    return false;
                tasks_[i] = UINT32_MAX;
            }
            if (owners_[i]) {
                owners_[i]->~Owner();
                if (!arena_->Release(owners_[i]))
                    return false;
                owners_[i] = nullptr;
            }
        }
    }
    arena_ = nullptr;
    manager_ = nullptr;
    transition_.Retire();
    dialog_.Retire();
    state_.live = false;
    state_.storage_dialog = state_.awaiting_confirmation = false;
    state_.owners = state_.records = state_.ui_resources = state_.ui_nodes = state_.ui_clips =
        state_.textures = state_.texture_pixels = 0;
    state_.borrowed_dictionaries = state_.mutable_dictionaries = state_.text_capacity_bytes = 0;
    return true;
}
GameFsStatus GameSceneUiBootstrap::BeginProfileConfirmation(){
    if(!font_||!dialog_.Begin(0,0))return {GameFsCode::Busy,0,"TRC confirmation active/retired"};
    //DA234 exact CP932:これでよろしいですか？; native panel adapter ofTRC.
    const std::string question="\x82\xb1\x82\xea\x82\xc5\x82\xe6\x82\xeb\x82\xb5\x82\xa2\x82\xc5\x82\xb7\x82\xa9\x81\x48";
    GameFontBitmap bitmap;auto status=font_->RasterizeCp932({question},320,bitmap,
        static_cast<GameFontDrawRecord*>(text_buffers_[2]),1024);
    if(!status)return status;
    dialog_pixels_=std::move(bitmap.pixels);dialog_width_=bitmap.width;dialog_height_=bitmap.height;
    profile_confirmation_=true;state_.storage_dialog=true;state_.dialog_state=0;state_.dialog_result=0;
    return {};
}
bool GameSceneUiBootstrap::DrawProfileConfirmation(GameGraphics& graphics){
    if(!profile_confirmation_)return true;
    dialog_.Tick();const auto d=dialog_.Snapshot();
    state_.dialog_state=d.state;state_.dialog_result=d.result;state_.dialog_progress=d.progress;
    state_.awaiting_confirmation=d.state==1;
    if(d.state==3){profile_confirmation_=false;state_.storage_dialog=false;return true;}
    const auto w=(std::max)(1u,uint32_t(float(dialog_width_)*d.progress));
    const auto h=(std::max)(1u,uint32_t(float(dialog_height_)*d.progress));
    std::vector<uint32_t> pixels(size_t(w)*h);
    for(uint32_t y=0;y<h;++y)for(uint32_t x=0;x<w;++x)
        pixels[size_t(y)*w+x]=dialog_pixels_[size_t(uint64_t(y)*dialog_height_/h)*dialog_width_+uint64_t(x)*dialog_width_/w];
    return graphics.FillRectangle(112,16,448,64,0xFFE9E3CE)&&graphics.DrawImage(pixels,w,h,128,24);
}
GameFsStatus GameSceneUiBootstrap::BeginEditor(std::string_view initial,uint32_t maximum,std::string label,
                                               uint32_t select_mode,int32_t minimum,int32_t maximum_value,
                                               bool allow_cancel,bool input32_presentation){
    if(!state_.live||select_mode>5||(choice_.Snapshot().live&&choice_.Snapshot().state!=3))return {GameFsCode::Busy,0,"editor ownership/mode"};
    const uint32_t resource=select_mode?0u:1u;
    constexpr uint32_t opens[]{17,3,11,5,7,9};
    const uint32_t open=input32_presentation&&select_mode==0?15u:opens[select_mode],close=open+1;
    auto next=editor_;
    if(!next.Begin(initial,maximum))return {GameFsCode::Busy,0,"invalid/active editor"};
    editor_resource_=resource;editor_open_clip_=open;editor_close_clip_=close;
    editor_min_=minimum;editor_max_=maximum_value;editor_cancel_=allow_cancel;
    state_.editor_label=std::move(label);state_.select_adapter=select_mode!=0;
    state_.input32_text_panel=input32_presentation;
    const auto root=select_mode?0u:9u;
    auto& clone=clones_[resource];auto* c=clone.Control(0);
    if(!c||!clone.Bind(root,open,0)||!clone.ShowRoot(root)){
        editor_failed_=true;return {GameFsCode::Unavailable,0,"actual editor open clip/root unavailable"};}
    c->clip=open;c->frame=0;c->mode=c->active=1;
    if(resource==1&&!clone.SetSourceCallback(42,0x222E0)){
        editor_failed_=true;return {GameFsCode::Unavailable,0,"SOFTKEY glyph callback node42 missing"};}
    editor_=std::move(next);editor_failed_=false;
    return {};
}
bool GameSceneUiBootstrap::HandleText(const GameTextEvent& event){
    if(editor_failed_)return false;
    const auto before=editor_.Snapshot();
    if(event.context!=before.context||!before.live||before.source_state==3)return false;
    if(event.kind==GameTextKind::Cancel&&!editor_cancel_)return editor_.Reject("Source guided year cannot cancel");
    if(state_.select_adapter&&before.source_state==1&&before.focused&&!before.composing){
        if(event.kind==GameTextKind::Left||event.kind==GameTextKind::Right){
            int32_t n=editor_min_;std::from_chars(before.staged.data(),before.staged.data()+before.staged.size(),n);
            n+=(event.kind==GameTextKind::Right?1:-1);
            if(n>editor_max_)n=editor_min_;if(n<editor_min_)n=editor_max_;
            return editor_.SetStaged(std::to_string(n));
        }
        if(event.kind==GameTextKind::Confirm){
            int32_t n=0;const auto parsed=std::from_chars(before.staged.data(),before.staged.data()+before.staged.size(),n);
            if(parsed.ec!=std::errc{}||parsed.ptr!=before.staged.data()+before.staged.size()||n<editor_min_||n>editor_max_)
                return editor_.Reject("Selection outside source range");
        }
    }
    auto next=editor_;const auto accepted=next.Apply(event);
    if(accepted&&before.source_state==1&&next.Snapshot().source_state==2){
        auto& clone=clones_[editor_resource_];auto* c=clone.Control(0);
        if(!c||!clone.Bind(editor_resource_?9u:0u,editor_close_clip_,0)){editor_failed_=true;return false;}
        c->clip=editor_close_clip_;c->frame=0;c->mode=c->active=1;
    }
    editor_=std::move(next);
    return accepted;
}
bool GameSceneUiBootstrap::ExpireEditor(){
    if(editor_failed_)return false;
    auto next=editor_;if(!next.Expire())return false;
    auto& clone=clones_[editor_resource_];auto* c=clone.Control(0);
    if(!c||!clone.Bind(editor_resource_?9u:0u,editor_close_clip_,0)){editor_failed_=true;return false;}
    c->clip=editor_close_clip_;c->frame=0;c->mode=c->active=1;
    editor_=std::move(next);return true;
}
bool GameSceneUiBootstrap::TickEditor(){
    if(editor_failed_)return false;
    const auto before=editor_.Snapshot();
    if(!before.live||before.source_state==3)return true;
    auto& clone=clones_[editor_resource_];std::string error;
    if(!clone.Update(error))return false;
    auto* c=clone.Control(0);if(!c)return false;
    editor_.ControlCompleted(c->mode);
    if(editor_.Snapshot().source_state==3){
        clone.ShowRoot(editor_resource_?9u:0u,false);c->active=c->mode=0;
    }
    return true;
}
bool GameSceneUiBootstrap::DrawUiCallback(uint32_t pc,uint32_t,const GameUiMatrix& world,uint32_t,
                                          GameGraphics& graphics,std::string& error){
    if(pc!=0x222E0||!font_){error="Unknown/retired SOFTKEY callback";return false;}
    const auto entry=editor_.Snapshot();if(entry.source_state==3)return true;
    //222E0/221FC: original font,one16px advance per941C8 step,128x16 viewport.
    const auto x=int32_t(240+world.tx),y=int32_t(136+world.ty);
    const auto& value=entry.source_state==1?entry.staged:entry.source120;
    std::vector<uint32_t> pixels(128*16,0);const auto* at=value.c_str();int32_t column=-(int32_t(entry.source_state==1?entry.units:0)>7?int32_t(entry.units-7)*16:0);
    while(*at&&column<128){
        const auto length=uint8_t(*at)<0x80?1u:2u;
        GameFontBitmap glyph;const auto status=font_->RasterizeCp932({std::string(at,length)},32,glyph,
              static_cast<GameFontDrawRecord*>(text_buffers_[1]),41,0xFF552F0D);
        if(!status){error=status.detail;return false;}
        for(uint32_t gy=0;gy<(std::min)(16u,glyph.height);++gy)
            for(uint32_t gx=0;gx<glyph.width&&column+int32_t(gx)<128;++gx)
                if(column+int32_t(gx)>=0)pixels[gy*128+uint32_t(column+int32_t(gx))]=glyph.pixels[gy*glyph.width+gx];
        column+=16;at+=length;
    }
    return graphics.DrawImage(pixels,128,16,x,y);
}
bool GameSceneUiBootstrap::DrawEditor(GameGraphics& graphics){
    const auto e=editor_.Snapshot();if(!e.live||e.source_state==3)return true;
    std::string error;GameUiDrawStats stats;
    if(!clones_[editor_resource_].Draw(graphics,stats,error,this))return false;
    if(state_.input32_text_panel){
        //WindowsInput32TextPanel:actual committed bytes and original FONT.
        //Original SOFTKEY dynamic glyph placement/IME remains unclosed;
        //this explicit panel makes editing visible without claiming it.
        const auto& visible=e.source_state==1?e.staged:e.source120;
        GameFontBitmap text;const auto status=font_->RasterizeCp932({visible},440,text,nullptr,0,0xFF552F0D);
        if(!status||!graphics.FillRectangle(12,8,468,36,0xFFF8F2D9,65535))return false;
        return text.width==0||text.height==0||graphics.DrawImage(text.pixels,text.width,text.height,20,12,0xFFFFFFFF,65535);
    }
    if(!state_.select_adapter)return true; //native instructions/errors live outside the game framebuffer
    //Explicit Windows committed-text/choice adapter caption. This is not NJ
    //conversion or the original dynamic SELECT row renderer.
    std::vector<std::string> lines{state_.editor_label};
    if(state_.select_adapter)lines.push_back(e.staged);
    if(!e.error.empty())lines.push_back(e.error);
    GameFontBitmap text;const auto status=font_->RasterizeCp932(lines,440,text,nullptr,0,0xFF202020);
    if(!status)return false;
    const int32_t y=state_.select_adapter?76:8;
    if(!graphics.FillRectangle(12,y-4,468,y+int32_t(text.height)+4,0xFFF8F2D9))return false;
    return graphics.DrawImage(text.pixels,text.width,text.height,20,y);
}
GameFsStatus GameSoundBank::Request73(int32_t sound,int32_t channel,int32_t volume){
    if(!state_.metadata_ready)return {GameFsCode::Unavailable,0,"GAME73 requires live SE_SYS bank lease"};
    // Cached registration descriptor only: actor revalidates the lease. No
    // source acquisition/decoder work belongs on the game dispatch thread.
    if(sound<0||channel<0||volume<0||volume>128)return {GameFsCode::Unavailable,0,"GAME73 finite sound/channel/volume domain"};
    ++state_.playback_requests;state_.last_sound=sound;state_.last_channel=channel;state_.last_volume=volume;
    audio::Game73SelectedBankRequest request;
    request.request_id=next_request_++;request.bank=image_;request.identity=identity_;
    request.group=sound;request.entry=channel;request.volume=volume;request.profile_gain=state_.profile_gain;
    request.master_left=state_.master_left;request.master_right=state_.master_right;
    state_.native_request_id=request.request_id;
    state_.native_queue_code=int32_t(sink_?sink_->TryQueueSelected(request):audio::Game73QueueCode::Closed);
    // Queue/decode/device results are separate native evidence. Source1D064
    // ignores the playback return; do not synthesize a source playback handle.
    state_.last_playback_result=-1;return {};
}
GameFsStatus GameSoundBank::Register(PackageView image) {
    if(registration_failed_)return registration_failure_;
    if (state_.metadata_ready)
        return {GameFsCode::Busy, 0, "sound bank already registered"};
    try {
        const auto description=image.Describe();const auto size = description.size;
        if (size < 16 || size > UINT32_MAX || image.ReadLE32(0) != 0x44584753)
            return {GameFsCode::MalformedDirectory, 0, "SGXD signature/extent"};
        const auto end = image.ReadLE32(8);
        if (end < 16 || end > size)
            return {GameFsCode::MalformedDirectory, 0, "SGXD chunk end outside file"};
        GameSoundBankSnapshot next;
        next.image_bytes = uint32_t(size);
        for (uint64_t at = 16; at < end;) {
            if (end - at < 8)
                return {GameFsCode::MalformedDirectory, 0, "SGXD truncated chunk header"};
            const auto tag = image.ReadLE32(at), bytes = image.ReadLE32(at + 4);
            if (bytes > end - at - 8)
                return {GameFsCode::MalformedDirectory, 0, "SGXD chunk extent"};
            ++next.chunks;
            next.regions |= tag == 0x444E4752;
            next.waves |= tag == 0x45564157;
            next.sequences |= tag == 0x44514553;
            next.names |= tag == 0x454D414E;
            at += uint64_t(bytes) + 8;
        }
        int32_t slot=-1;const auto allocated=registry_.Allocate(image,slot);if(!allocated)return allocated;
        // Publish the actual allocated image/identity before the source field.
        // A numeric metadata slot is only an output of this owning allocation.
        slots_[size_t(slot)]=true;identity_=description;image_=image;
        if(source_control_.Snapshot().live){
            auto bound=source_control_.BindSe5(image);
            if(bound)bound=source_control_.StoreRegisteredSe5(registry_,slot);
            if(!bound){registration_failed_=true;registration_failure_=bound;registry_.Close();return bound;}
        }
        next.native_metadata_slot = slot;
        next.metadata_ready = true;
        next.profile_gain=state_.profile_gain;next.finite_windows_output=sink_!=nullptr;
        identity_=description;
        image_ = std::move(image);
        state_ = next;
        return {};
    } catch (const std::exception &e) {
        return {GameFsCode::MalformedDirectory, 0, e.what()};
    }
}
} // namespace dokodemo::recovered
