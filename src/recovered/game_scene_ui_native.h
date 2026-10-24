#pragma once
#include "game_graphics_native.h"
#include "game_ui_resource_native.h"
#include "game_dictionary_native.h"
#include "game_input_native.h"
#include "game_text_native.h"
#include "game_sound_control_native.h"
#include "game_trc_prompt_native.h"
#include <array>
#include "../game73_audio_request.h"

namespace dokodemo::recovered {
struct GameTransitionSnapshot {
    bool live = false;
    uint32_t state = 0, group = 5, duration = 30;
    float x = 0, y = 0, radius = -50, maximum_radius = 0, increment = 0;
};
// D54C/D838/D9BC/D67C/D708, with typed native ownership and host math.
class GameTransition final {
  public:
    bool Initialize(const std::array<float, 2> &produced_center);
    bool SetCenter(const std::array<float, 2> &center);
    void SetDuration(uint32_t ticks);
    void Open(bool immediate = false);      // D9E4; source state1 expands to2
    bool StartGameOpen(); // source1C258 -> DA64(0,5,produced zero point)
    void CloseWipe(bool immediate = false); // contracting state3 to0
    void Update();
    bool Draw(GameGraphics &graphics) const;
    GameTransitionSnapshot Snapshot() const {
        return state_;
    }
    void Retire() noexcept {
        state_.live = false;
    }

  private:
    GameTransitionSnapshot state_;
};

class GameArena;
class GameVmBoot;
class GameFontResource;
struct GameChoiceSnapshot {
    bool live=false,timed_out=false;
    uint32_t state=3,row=0,result=0,remaining=0,active_updates=0,confirmations=0,rejected_inputs=0;
    uint64_t context=0,owner_generation=0;
    std::vector<std::string> options;
};
// Mode0 SELECT10884/10BCC: source counters,deadline priority and real clip
// completion;dynamic row drawing uses the named NativeSelectTextPanel.
class GameChoiceController {
  public:
    bool Begin(std::vector<std::string>,uint64_t context,uint64_t owner,uint32_t timeout=5400);
    bool Input(const GameInputFrame&);
    void Tick(uint8_t main_mode,bool trc_idle=true);
    void Retire(){input_.BeginContext();state_.live=false;}
    GameChoiceSnapshot Snapshot()const{return state_;}
  private:
    GameMenuInput input_;
    GameChoiceSnapshot state_;
    uint64_t sequence_=0;
};
struct GameDialogSnapshot {
    int32_t state = 3, result = 0;
    uint32_t mode = 1, selection = 0, confirmations = 0;
    float progress = 0;
};
// 47E0C/48224/47EA4: first press produces selection+1, then state2 really
// contracts to0/hides the root before state3. No forced completion/autoack.
class GameDialogController {
  public:
    bool Begin(uint32_t mode = 1, uint32_t selection = 0) noexcept;
    bool Input(const GameInputFrame &frame) noexcept;
    void Tick() noexcept;
    void Retire() noexcept {
        gate_.Stop();
        cancel_gate_.Stop();
        state_.state = 3;
    }
    GameDialogSnapshot Snapshot() const noexcept {
        return state_;
    }

  private:
    GameButtonGate gate_;
    GameButtonGate cancel_gate_;
    GameDialogSnapshot state_;
};
struct GameSceneUiSnapshot {
    bool live = false, constructors_returned = false, scene1_returned = false;
    bool ui_render_implemented = false, text_drawn = false, mixer_implemented = false;
    uint32_t owners = 0, records = 0, ui_resources = 0, ui_nodes = 0, ui_clips = 0;
    uint32_t textures = 0, texture_pixels = 0, borrowed_dictionaries = 0, mutable_dictionaries = 0;
    uint32_t text_capacity_bytes = 0, visible_roots = 0, native_frames = 0;
    bool storage_dialog = false, awaiting_confirmation = false;
    uint32_t storage_error = 0, dialog_glyphs = 0;
    float dialog_progress = 0;
    int32_t dialog_state = 3, dialog_result = 0;
    uint32_t confirmations = 0;
    GameTransitionSnapshot transition;
    GameNameEntrySnapshot editor;
    std::string editor_label;
    bool select_adapter=false;
    bool input32_text_panel=false; //named Windows committed-text display
};
// 18EE8..18F38: grouped typed native owners. Immutable views borrow DATA.BPM;
// mutable NJ/text/control state is owned and retired before that package/font.
class GameSceneUiBootstrap final : private GameUiCallbacks {
  public:
    GameFsStatus Initialize(GameResourceSource &source, GameArena &arena, GameVmBoot &manager,
                            const GameFontResource &font);
    bool DrawColdFrame(GameGraphics &graphics);
    GameTransition& TitleTransitionOwner()noexcept{return transition_;} //worker-owned,never exposed to UI thread
    const GameTransition& TitleTransitionOwner()const noexcept{return transition_;}
    GameTrcPrompt& OptionsPromptOwner()noexcept{return options_prompt_;}
    GameFsStatus PrepareOptionsPrompt();
    GameFsStatus ShowStorageDialog(const GameFontResource &font, uint32_t native_error);
    bool HandleDialogInput(const GameInputFrame &frame);
    GameFsStatus BeginEditor(std::string_view initial,uint32_t maximum,std::string label,
                            uint32_t select_mode=0,int32_t minimum=0,int32_t maximum_value=0,
                            bool allow_cancel=true,bool input32_presentation=false);
    bool HandleText(const GameTextEvent&);
    GameNameEntrySnapshot Editor()const{return editor_.Snapshot();}
    const std::string* EditorResult()const noexcept{return editor_.Result();}
    bool TickEditor();
    bool ExpireEditor();
    bool DrawEditor(GameGraphics&);
    GameFsStatus BeginProfileConfirmation();
    bool DrawProfileConfirmation(GameGraphics&);
    GameFsStatus BeginChoice(std::vector<std::string>,uint64_t context,uint64_t owner);
    GameFsStatus TickChoice();
    bool HandleChoiceInput(const GameInputFrame& frame){return choice_.Input(frame);}
    bool DrawChoice(GameGraphics&);
    GameChoiceSnapshot Choice()const{return choice_.Snapshot();}
    GameSceneUiSnapshot Snapshot() const;
    bool Close();
    ~GameSceneUiBootstrap();

  private:
    struct Owner {
        uint32_t state = 3, group = 0;
    };
    GameArena *arena_ = nullptr;
    GameVmBoot *manager_ = nullptr;
    std::array<Owner *, 5> owners_{};
    std::array<uint32_t, 5> tasks_{{UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX}};
    std::array<void *, 3> text_buffers_{};
    std::array<std::shared_ptr<GameUiResource>, 3> resources_;
    std::array<GameUiInstance, 3> clones_;
    std::array<GameDictionary, 9> dictionaries_;
    std::shared_ptr<GameImage> moving_;
    GameTransition transition_;
    GameDialogController dialog_;
    GameTrcPrompt options_prompt_;
    std::vector<uint32_t> dialog_pixels_;
    uint32_t dialog_width_ = 0, dialog_height_ = 0;
    GameSceneUiSnapshot state_;
    const GameFontResource* font_=nullptr;
    GameNameEntry editor_;
    uint32_t editor_resource_=1,editor_open_clip_=17,editor_close_clip_=18;
    int32_t editor_min_=0,editor_max_=0;
    bool editor_cancel_=true;
    bool editor_failed_=false;
    bool profile_confirmation_=false;
    GameChoiceController choice_;
    bool DrawUiCallback(uint32_t,uint32_t,const GameUiMatrix&,uint32_t,GameGraphics&,std::string&)override;
};

struct GameSoundBankSnapshot {
    bool metadata_ready = false, mixer_ready = false;
    uint32_t image_bytes = 0, chunks = 0, pool_capacity = 64;
    int32_t native_metadata_slot = -1;
    bool regions = false, waves = false, sequences = false, names = false;
    uint32_t playback_requests=0;
    int32_t last_sound=-1,last_channel=-1,last_volume=0,last_playback_result=-1;
    uint64_t native_request_id=0;
    int32_t native_queue_code=int32_t(audio::Game73QueueCode::Closed),profile_gain=0,master_left=128,master_right=128;
    bool finite_windows_output=false;
};
// Native metadata pool adaptation of551E4/55208/5536C. This owns a live source
// image and a real bounded slot; it does not pretend to implement SGX playback.
class GameSoundBank final {
  public:
    GameFsStatus InitializeSource17F0(){return source_control_.Initialize17F0();}
    GameSourceSoundSnapshot SourceControl()const{return source_control_.Snapshot();}
    GameFsStatus QuerySourceGain(const GameSourceSoundSnapshot& expected,int32_t value,GameSourceSoundGainReceipt& out){return source_control_.Gain4AFBC(expected,value,out);}
    GameFsStatus Register(PackageView image);
    GameFsStatus ReadOptionsFeedback(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){return source_control_.Feedback4AC38(expected,out);}
    GameFsStatus ReadOptionsFeedback(const GameSourceSoundSnapshot& expected,uint32_t root14,GameSourceFeedbackRead& out){return source_control_.Feedback4AC38(expected,root14,out);}
    GameFsStatus ReadOptionsVoiceFeedback(const GameSourceSoundSnapshot& expected,uint32_t root14,GameSourceFeedbackRead& out){return source_control_.VoiceFeedback4AC38(expected,root14,out);}
    GameFsStatus ServiceOptionsVoice(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){return source_control_.ServiceVoice(expected,out);}
    GameFsStatus ServiceOptionsSequence(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){return source_control_.ServiceSequence(expected,out);}
    GameFsStatus ServiceOptionsPort(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){return source_control_.ServicePort(expected,out);}
    GameFsStatus ServiceOptionsProcessor(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){return source_control_.ServiceProcessor(expected,out);}
    GameFsStatus ServiceOptionsControl(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){return source_control_.ServiceControl(expected,out);}
    GameFsStatus ServiceOptionsWave(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){return source_control_.ServiceWave(expected,out);}
    GameFsStatus ServiceOptionsPort24(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){return source_control_.ServicePort24(expected,out);}
    GameFsStatus ServiceOptionsChild(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){return source_control_.ServiceChild(expected,out);}
    GameFsStatus ServiceOptionsSlot(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){return source_control_.ServiceSlot(expected,out);}
    GameFsStatus ServiceOptionsSfx(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out){return source_control_.ServiceSfx(expected,out);}
    void ObserveOptionsWorkerTick()noexcept{source_control_.ObserveWorkerTick();}
    GameFsStatus Request73(int32_t sound,int32_t channel,int32_t volume);
    void SetSink(audio::Game73SelectedSink* sink) noexcept {sink_=sink;state_.finite_windows_output=sink!=nullptr;}
    void SetProfileGain(int32_t value) noexcept {state_.profile_gain=value;}
    void SetMasterGain(int32_t left,int32_t right) noexcept {state_.master_left=left<-128?-128:left>128?128:left;state_.master_right=right<-128?-128:right>128?128:right;}
    GameSoundBankSnapshot Snapshot() const {
        return state_;
    }
    void Close() {
        source_control_.Close();
        registry_.Close();
        image_.Reset();
        slots_.fill(false);
        state_ = {};
        registration_failure_={};registration_failed_=false;
    }

  private:
    PackageView image_;
    std::array<bool, 64> slots_{};
    GameSoundBankSnapshot state_;
    PackageViewDescription identity_;
    audio::Game73SelectedSink* sink_=nullptr;
    uint64_t next_request_=1;
    friend class GameContentBoot;
    GameFsStatus ServiceOptionsChildScoped(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out,GameOptionsChildCallScope& scope){return source_control_.ServiceChildScoped(expected,out,scope);}
    GameFsStatus ServiceOptionsSlotScoped(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out,GameOptionsChildCallScope& scope){return source_control_.ServiceSlotScoped(expected,out,scope);}
    GameFsStatus ServiceOptionsSfxScoped(const GameSourceSoundSnapshot& expected,GameSourceFeedbackRead& out,GameOptionsChildCallScope& scope){return source_control_.ServiceSfxScoped(expected,out,scope);}
    GameSourceSoundControl source_control_;
    GameSourceBankRegistry registry_;
    bool registration_failed_=false;
    GameFsStatus registration_failure_;
};
} // namespace dokodemo::recovered
