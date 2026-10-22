#pragma once
#include "game_filesystem_native.h"
#include "game_scene_ui_native.h"
#include "game_storage_native.h"
#include "game_title_native.h"
#include "game_actor_script_native.h"
#include "game_main_flow_native.h"
#include "game_main_menu_native.h"
#include "game_returned_interaction_native.h"
#include <array>
#include <vector>

namespace dokodemo::recovered {
class GameArena;
class GameVmBoot;
struct GameFontTexture {
    PackageView pixels;
    uint8_t format = 0;
    uint16_t width = 0, height = 0;
};
struct GameFontSnapshot {
    bool ready = false;
    uint32_t image_bytes = 0, texture_count = 0, texture_bytes = 0;
    uint32_t auxiliary_div8 = 0, auxiliary_offset = 0;
    int32_t glyph_width = 0, glyph_height = 0;
    float scale_x = 1, scale_y = 1, spacing_x = 2, spacing_y = 2;
};
struct GameFontBitmap {
    uint32_t width = 0, height = 0, glyphs = 0,advance_width=0;
    std::vector<uint32_t> pixels;
};
struct GameFontDrawRecord {
    uint32_t page = 0;
    uint8_t clut_mode = 0, zero = 0, field6 = 255, plane = 0;
    float x = 0, y = 0;
    uint32_t field10 = 0;
    int16_t width = 0, height = 0;
    float u = 0, v = 0, scale_x = 1, scale_y = 1;
    uint32_t color = 0x8007182B;
};
static_assert(sizeof(GameFontDrawRecord) == 44, "source-sized native glyph record");
// E364/937FC/9435C: live borrowed XFT image and typed native texture records.
// No rendered text, GPU upload or invented title is implied by initialization.
class GameFontResource final {
  public:
    GameFsStatus Initialize(PackageView image);
    const GameFontTexture *Texture(uint32_t index) const;
    PackageView AuxiliaryData() const {
        return auxiliary_;
    }
    GameFontSnapshot Snapshot() const {
        return state_;
    }
    // Finite native93CFC/E4D0 atlas consumer. Original XFT pixels/palette and
    // 941C8 CP932 codes, with explicit native character wrapping at max_width.
    GameFsStatus RasterizeCp932(const std::vector<std::string> &lines, uint32_t max_width,
                                GameFontBitmap &out, GameFontDrawRecord *records = nullptr,
                                uint32_t capacity = 0,uint32_t native_modulation=0xFF552F0D) const;
    void Close();

  private:
    PackageView image_, auxiliary_;
    std::vector<GameFontTexture> textures_;
    GameFontSnapshot state_;
};
struct GameContentSnapshot {
    bool vsync_owner = false, loadman_owner = false, bb8_returned = false;
    bool state_reset = false, import_callback_configured = false, import_callback_registered = false;
    bool font_ready = false, package_mounted = false, blocked = false, closed = false;
    uint32_t registered_records = 0, loading_ticks = 0, load_state = 1, mount_state = 0;
    int32_t previous_scene = -1, current_scene = -1, next_scene = -1;
    uint32_t requested_bits = 0, owned_bits = 0, completed_bits = 0;
    uint32_t package_bytes = 0, package_slots = 0, frontier_pc = 0;
    uint32_t texture_cache_entries=0,texture_cache_pixels=0;
    uint64_t texture_cache_evictions=0;
    GameFontSnapshot font;
    GameSceneUiSnapshot scene_ui;
    GameSoundBankSnapshot sound_bank;
    uint32_t scene_substate = 0, opened_resources = 0;
    GameAutoloadSnapshot autoload;
    bool title_mounted = false, title_ui_ready = false, title_model_read = false,
         title_constructor_returned = false;
    uint32_t title_bytes = 0, title_slots = 0, title_nodes = 0, title_clips = 0, title_textures = 0;
    uint32_t title_model_bytes = 0, title_model_nodes = 0, title_model_materials = 0;
    std::vector<std::string> title_material_resources;
    GameTitleSnapshot title;
    bool actor_dictionary_requested=false,actor_dictionary_ready=false,actor_dictionary_live=false;
    uint32_t actor_package_bytes=0,actor_dictionary_bytes=0;
    std::string actor_dictionary_path;
    std::array<uint8_t,16> actor_dictionary_prefix{};
    GameActorScriptSnapshot actor_script;
    GameMainFlowSnapshot main_flow;
    GameMainMenuSnapshot main_menu;
    uint32_t main_menu_phase=0,main_menu_returns=0;
    uint64_t main_menu_input_epoch=0;
    bool main_menu_available=false,main_menu_source_right_blocked=false;
    GamePostModalSnapshot post_modal;
    GameReturnedInteractionSnapshot returned_interaction;
    bool source_loader469=false; //17910 cold0;stable inventory poll1 persists
    GameInventorySnapshot inventory;
    uint32_t actor_message_contexts=0;
    uint32_t actor_message_stale_inputs=0;
    GameChoiceSnapshot actor_choice;
    uint32_t actor_input_remaining=0,actor_input_active_updates=0;
    bool actor_input_timed_out=false;
    bool actor_message_input_armed=false;
    uint32_t actor_message_draw_frames=0,actor_message_glyphs=0,actor_message_ink_pixels=0;
    std::string next_resource;
    GameFsStatus status;
};
// Nonmatching cold-loading driver. It runs the recovered phase0 state machines
// in source order; synchronous native I/O replaces the PSP async file pump.
// The finite fade/hidden-control/TRC text adapter is connected explicitly;
// general CVSync rendering, PSP interrupt delivery and the full loop remain
// unsupported. No source-PC descriptor is treated as a successful callback.
class GameContentBoot final : public GameResourceSource {
  public:
    GameContentBoot() = default;
    GameContentBoot(const GameContentBoot &) = delete;
    GameContentBoot &operator=(const GameContentBoot &) = delete;
    ~GameContentBoot();
    bool InitializeBB8(GameArena &arena, GameVmBoot &manager);
    bool Reset24994();
    bool ConfigureImportCallback5334C(); // unavailable native interrupt/sound policy, no fake registration
    uint32_t StateWord(uint32_t byte_offset) const;
    int32_t ResetLookupWord(uint32_t index) const;
    // One native loading tick; false means a real I/O/format/lifetime failure.
    // A successful font consumer stops BEFORE the next source constructor.
    bool TickLoading(GameFilesystem &filesystem);
    // Continue the same live font/package through all five reached scene1
    // constructors. A platform source is only used for actual absolute files.
    GameFsStatus InitializeSceneResources(GameResourceSource &platform);
    GameFsStatus ShowUnavailableMemoryStick(uint32_t native_error);
    bool ProcessDialogInput(const GameInputFrame &frame);
    bool ProcessText(const GameTextEvent& event);
    GameFsStatus BeginStorage(GameStorage &storage, bool test_missing_device_prompt = false);
    bool TickInteractive(GameFilesystem &filesystem, GameStorage &storage, GameGraphics &graphics);
    void SetNativeTitleLayout(NativeTitleLayout value){title_layout_=value;}
    void SetTitleRng(RngNativeOwner& rng){title_rng_=&rng;}
    void SetCalendar(const std::array<uint32_t,7>& value){calendar_=value;calendar_year_=int32_t(value[6]);}
    void SetActorClock(GameActorScript::ClockWord clock){actor_clock_=std::move(clock);}
    void SetSoundOutput(audio::Game73SelectedSink* sink){sound_bank_.SetSink(sink);}
    GameFsStatus OpenResource(const std::string &path, PackageView &out) override;
    bool DrawColdFrame(GameGraphics &graphics);
    GameContentSnapshot Snapshot() const {
        auto out=state_;out.texture_cache_entries=TextureCacheSize();out.texture_cache_pixels=TextureCachePixels();
        out.texture_cache_evictions=TextureCacheEvictions();return out;
    }
    const GameFontResource &Font() const {
        return font_;
    }
    bool Close();

  private:
    struct Vsync {
        uint32_t interval = 160;
        bool render = true, finish = true, paused = false;
    };
    struct LoadMan {
        std::array<char, 35> configured_script{};
    };
    GameArena *arena_ = nullptr;
    GameVmBoot *manager_ = nullptr;
    Vsync *vsync_ = nullptr;
    LoadMan *loadman_ = nullptr;
    uint32_t vsync_task_ = UINT32_MAX, load_task_ = UINT32_MAX;
    std::array<uint8_t, 0x10B50> reset_bytes_{};
    std::array<int32_t, 28> reset_lookup_{};
    PackageView package_;
    BpDirectory directory_;
    GameFontResource font_;
    GameSceneUiBootstrap scene_ui_;
    GameSoundBank sound_bank_;
    GameAutoload autoload_;
    PackageView title_package_, title_model_image_;
    BpDirectory title_directory_;
    std::shared_ptr<GameUiResource> title_ui_;
    GameUiInstance title_clone_;
    GameTitleScene title_;
    PackageView actor_package_,actor_dictionary_;
    BpDirectory actor_directory_;
    GameActorScript actor_script_;
    GameMainFlow main_flow_;
    GameMainMenu main_menu_;
    GameButtonGate main_menu_entry_gate_;
    GameOwnerEventTarget main_menu_target_;
    GameMainMenuLease main_menu_lease_;
    bool main_menu_entry_ready_=false;
    uint64_t main_menu_entry_owner_=0,main_menu_entry_generation_=0;
    bool MainMenuEligible()const;
    bool MainMenuInput(const GameInputFrame&);
    GameFsStatus CheckMainMenuLease()const;
    bool TickMainMenu(GameGraphics&);
    GamePostModalInteraction post_modal_;
    GameReturnedConversationInteraction returned_interaction_;
    GameReturnedInteractionGate ReturnedGate()const;
    GameRecordStore records_;
    GameActorRuntime actor_runtime_;
    GameInventory inventory_;
    PackageView inventory_package_;
    BpDirectory inventory_directory_;
    bool inventory_mounted_=false;
    PackageView room_package_;
    BpDirectory room_directory_;
    bool room_mounted_=false;
    PackageView actor_model_package_;
    BpDirectory actor_model_directory_;
    bool actor_model_mounted_=false;
    GameFilesystem* game_filesystem_=nullptr;
    GameActorScript::ClockWord actor_clock_;
    GameMessageInput actor_message_gate_;
    GameMessageTarget actor_message_target_;
    GameChoiceTarget actor_choice_target_;
    GameChoiceTarget actor_input_target_;
    bool TickActorDictionary(GameFilesystem&,GameGraphics&);
    NativeTitleLayout title_layout_=NativeTitleLayout::CharacterEntrance;
    RngNativeOwner* title_rng_=nullptr;
    int32_t calendar_year_=0;
    std::array<uint32_t,7> calendar_{};
    GameResourceSource *platform_ = nullptr;
    GameContentSnapshot state_;
};
} // namespace dokodemo::recovered
