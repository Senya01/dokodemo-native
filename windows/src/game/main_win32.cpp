#include "startup_runtime_win32.h"
#include "core_hash.h"
#include "gameplay/boot_to_display.h"
#include "gameplay/display_win32.h"
#include "gameplay/controller_input_win32.h"
#include "gameplay/title_bgm.h"
#include "gameplay/title_effects.h"
#include "clock_backend_win32.h"
#include "platform/audio_ffmpeg_win32.h"
#include "verify_startup_room.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <tuple>

namespace {
namespace fs = std::filesystem;
using namespace dokodemo;
using gameplay::BootToDisplay;
using gameplay::Display;
enum ExitCode { Success, StartupFailed, BadArguments, TimedOut, CleanupFailed, MissingMedia };
enum class VerifyMode { startup, audio, power_cycle, startup_to_room };
constexpr wchar_t title[] = L"Doko Demo Issyo Portable";

fs::path PackageRoot() {
    std::wstring path(256, L'\0');
    for (;;) {
        auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!size) throw std::runtime_error("Cannot locate executable");
        if (size < path.size()) {
            path.resize(size);
            return fs::path(path).parent_path();
        }
        path.resize(path.size() * 2);
    }
}

bool Failed(const StartupObservation& state) {
    return !state.status || state.worker_state == "Exited" || state.worker_state == "Blocked";
}

bool VerifyFrame(const Display& display, const fs::path& capture) {
    std::vector<std::uint32_t> pixels;
    if (!display.CopyPixels(pixels)) return false;
    std::vector<std::uint8_t> rgb;
    rgb.reserve(pixels.size() * 3);
    unsigned nonblack = 0;
    for (auto pixel : pixels) {
        rgb.push_back(static_cast<uint8_t>(pixel >> 16));
        rgb.push_back(static_cast<uint8_t>(pixel >> 8));
        rgb.push_back(static_cast<uint8_t>(pixel));
        if (pixel & 0xffffff) ++nonblack;
    }
    auto hash = core::sha256_hex(rgb.data(), rgb.size());
    std::printf("title_frame=2 pixels=%zu nonblack=%u rgb_sha256=%s\n", pixels.size(), nonblack, hash.c_str());
    if (!capture.empty()) {
        std::ofstream out(capture, std::ios::binary);
        out << "P6\n" << Display::width << ' ' << Display::height << "\n255\n";
        out.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
        out.close();
        if (!out) return false;
    }
    return hash == "01ddd50aab29f683dca5e973cbb20cc943debed16fe3359601984182b08e42c6";
}

bool VerifyAudio(StartupCoordinator& session, const fs::path& file) {
    gameplay::TitleBgm music(session, file.parent_path());
    if (!music.Prepare() || music.Playing()) return false;
    const auto& clip = *music.PreparedClip();
    const auto samples = clip.FrameCount();
    const auto digest = core::sha256_hex(reinterpret_cast<const uint8_t*>(clip.samples.data()),
                                          clip.samples.size() * sizeof(int16_t));
    std::printf("pcm_frames=%llu pcm_sha256=%s\n", static_cast<unsigned long long>(samples), digest.c_str());
    return samples == 8921024 &&
        digest == "ac72953c2b7a3232f9d44dd322763d25a60644d288c8c4ee0965ca98b548f805";
}

bool VerifyPowerCycle(StartupCoordinator& session, const fs::path& audio_directory,
                      const std::shared_ptr<BootToDisplay>& boot);

int Verify(StartupCoordinator& session, const fs::path& root, const fs::path& capture,
           VerifyMode mode) {
    const auto deadline = GetTickCount64() + 20000;
    while (GetTickCount64() < deadline) {
        auto state = session.Observe(UINT64_MAX);
        if (auto boot = session.BootDisplay()) {
            if (!state.root_archive_mounted) continue; // publication raced the observation
            if (!state.module_gates_completed || !state.display_initialized || state.module_records.size() != 9 ||
                !std::all_of(state.module_observations.begin(), state.module_observations.end(),
                    [](const auto& module) { return module.staging_hash_matches && module.source_close_returned; }))
                return StartupFailed;
            while (boot->TitleFrame() < 2) if (!boot->AdvanceTitle()) return StartupFailed;
            if (!VerifyFrame(boot->Screen(), capture)) return StartupFailed;
            if (mode == VerifyMode::power_cycle)
                return VerifyPowerCycle(session, root / L"assets/audio", boot) ? Success : StartupFailed;
            if (mode == VerifyMode::startup_to_room) {
                try {
                    gameplay::VerifyStartupRoom(*boot);
                } catch (const std::exception& error) {
                    std::fprintf(stderr, "Startup-to-room verification: %s\n", error.what());
                    return StartupFailed;
                }
            }
            return mode != VerifyMode::audio || VerifyAudio(session, root / L"assets/audio/BGM_TITLE.at3") ?
                Success : StartupFailed;
        }
        if (Failed(state)) {
            std::fprintf(stderr, "Startup stopped at %x: %s\n", state.barrier_source_pc, state.barrier.c_str());
            return StartupFailed;
        }
        Sleep(2);
    }
    return TimedOut;
}

struct WindowState {
    StartupCoordinator& startup;
    gameplay::TitleBgm music;
    gameplay::TitleEffects effects;
    gameplay::XInputController pad{0};
    platform::WindowsClockCache clock;
    uint32_t keys = 0, pressed = 0;
    std::shared_ptr<BootToDisplay> boot;
    using Clock = std::chrono::steady_clock;
    using FrameTicks = std::chrono::duration<std::int64_t, std::ratio<1001,30000>>;
    Clock::time_point started;
    bool suspended = false;
    gameplay::ControllerButtonLatch pad_latch;

    bool HandlePower(WPARAM event, Clock::time_point now) {
        switch (event) {
        case PBT_APMSUSPEND:
            if (suspended) return true;
            suspended=true;
            keys=pressed=0;
            pad_latch.Reset();
            break;
        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMESUSPEND:
        case PBT_APMRESUMECRITICAL:
            if (!suspended) return true;
            // Restart the pacing clock from the next unexecuted frame.
            if (boot) started=now-std::chrono::ceil<Clock::duration>(FrameTicks(boot->TitleFrame()-std::int64_t{1}));
            suspended=false;
            break;
        default:
            return false;
        }
        if (!music.SetSuspended(suspended))
            std::fprintf(stderr, "BGM power transition: %s\n", music.Error().c_str());
        if (!effects.SetSuspended(suspended))
            std::fprintf(stderr, "Effect power transition: %s\n", effects.Error().c_str());
        return true;
    }

    std::optional<std::int64_t> TargetFrame(Clock::time_point now) const {
        if (suspended || !boot) return {};
        return 1 + std::chrono::duration_cast<FrameTicks>(now-started).count();
    }

    std::uint32_t FrameButtons(std::optional<std::uint32_t> pad_buttons) {
        const auto buttons=keys | pressed | pad_latch.Read(pad_buttons);
        pressed=0;
        return buttons;
    }
    int result = Success;
    DWORD output_type = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
    unsigned activity_serial = 0;
    bool activity_started = false, activity_ready = false, activity_paused = false;
    gameplay::RoomConversationSnapshot last_conversation{};
    gameplay::RoomTeachingSnapshot last_teaching{};
    gameplay::RoomOptionsSnapshot last_options{};
    gameplay::RoomInventorySnapshot last_inventory{};
    gameplay::RoomNotebookSnapshot last_notebook{};
};

bool VerifyPowerCycle(StartupCoordinator& session, const fs::path& audio_directory,
                      const std::shared_ptr<BootToDisplay>& boot) {
    using Clock = WindowState::Clock;
    using Tick = WindowState::FrameTicks;
    WindowState state{session, {session, audio_directory}, gameplay::TitleEffects(audio_directory)};
    // Exercise the notification order even if the startup worker has not handed off.
    const auto start=Clock::time_point{};
    if (!state.HandlePower(PBT_APMSUSPEND,start) || state.TargetFrame(start) ||
        !state.HandlePower(PBT_APMRESUMEAUTOMATIC,start) || state.TargetFrame(start)) return false;

    state.boot=boot;
    state.started=start;
    const auto frame=boot->TitleFrame();
    if (frame!=2 || !state.music.Prepare(0) || !state.music.Start(0) || !state.effects.Play(4)) return false;
    std::vector<std::uint32_t> before, during;
    if (!boot->Screen().CopyPixels(before)) return false;
    state.keys=state.pressed=0x2000;
    const auto stopped=start+std::chrono::duration_cast<Clock::duration>(Tick(1));
    const auto resumed=stopped+std::chrono::hours(1);
    if (!state.HandlePower(PBT_APMSUSPEND,stopped) ||
        !state.HandlePower(PBT_APMSUSPEND,stopped) || state.TargetFrame(resumed) ||
        state.keys || state.pressed || boot->TitleFrame()!=frame ||
        !boot->Screen().CopyPixels(during) || during!=before) return false;

    if (!state.HandlePower(PBT_APMRESUMEAUTOMATIC,resumed) || state.TargetFrame(resumed)!=frame ||
        state.FrameButtons(std::nullopt) || state.FrameButtons(0x2000) || state.FrameButtons(0) || state.FrameButtons(0x2000)!=0x2000) return false;
    const auto next=resumed+std::chrono::ceil<Clock::duration>(Tick(1));
    if (!state.HandlePower(PBT_APMRESUMESUSPEND,next) || state.TargetFrame(next)!=frame+1 ||
        !boot->AdvanceTitle() || boot->TitleFrame()!=frame+1) return false;
    // A resume without a matching suspend must not rebase the live clock.
    if (!state.HandlePower(PBT_APMRESUMECRITICAL,next) || state.TargetFrame(next)!=frame+1 ||
        state.HandlePower(PBT_APMPOWERSTATUSCHANGE,next)) return false;
    std::printf("power_cycle=pass frame_before=%u frame_paused=%u frame_after=%u skipped_updates=0 null_audio=%d\n",
                frame, frame, boot->TitleFrame(), int(audio::Win32Player::UsesNullOutput()));
    return audio::Win32Player::UsesNullOutput();
}

// An ordinary window can inherit a redirected output stream. Report each
// decision, actual actor start and script pause at the update that owns it.
void ReportRoomActivity(WindowState& window) {
    if (window.output_type != FILE_TYPE_DISK && window.output_type != FILE_TYPE_PIPE) return;
    const unsigned frame = window.boot->TitleFrame();
    const auto notebook = window.boot->Notebook();
    const auto notebook_fields = [](const gameplay::RoomNotebookSnapshot& s) {
        return std::tie(s.menu_column, s.visible, s.list_ready, s.tab, s.selection, s.count,
                        s.row, s.action_cursor, s.written, s.reading, s.resource, s.detail, s.delete_choice);
    };
    const auto edit_fields = [](const gameplay::MemoReadingEditSnapshot& edit) {
        return std::tie(edit.word_row, edit.phase, edit.keyboard_row, edit.column, edit.page,
                        edit.picking, edit.written, edit.composition);
    };
    const bool notebook_changed = notebook_fields(notebook) != notebook_fields(window.last_notebook);
    const bool edit_changed = edit_fields(notebook.edit) != edit_fields(window.last_notebook.edit);
    if (notebook_changed) {
        std::fprintf(stdout, "notebook-state %u %d %d %d %d %d %u %d %d", frame, notebook.menu_column,
                     int(notebook.visible), int(notebook.list_ready), notebook.tab, notebook.selection,
                     notebook.count, notebook.row, notebook.action_cursor);
        for (const auto* text : {&notebook.written, &notebook.reading}) {
            std::fputc(' ', stdout);
            if (text->empty()) std::fputc('-', stdout);
            for (unsigned char byte : *text) std::fprintf(stdout, "%02x", unsigned(byte));
        }
        std::fprintf(stdout, " %d %d %d\n", notebook.resource, int(notebook.detail), notebook.delete_choice);
    }
    if (edit_changed) {
        const auto& edit = notebook.edit;
        std::fprintf(stdout,"memo-edit-state %u %d %u %u %u %u %d", frame, edit.word_row,
                     edit.phase, edit.keyboard_row, edit.column, edit.page, int(edit.picking));
        for (const auto* text : {&edit.written, &edit.composition}) {
            std::fputc(' ', stdout);
            if (text->empty()) std::fputc('-', stdout);
            for (unsigned char byte : *text) std::fprintf(stdout,"%02x",unsigned(byte));
        }
        std::fputc('\n',stdout);
    }
    if (notebook_changed || edit_changed) {
        std::fflush(stdout);
        window.last_notebook = notebook;
    }
    const auto options = window.boot->Options();
    const auto options_fields = [](const gameplay::RoomOptionsSnapshot& s) {
        return std::tie(s.visible, s.active, s.row, s.values, s.prompt_choice);
    };
    if (options_fields(options) != options_fields(window.last_options)) {
        std::fprintf(stdout, "options-state %u %d %d %d %d %d %d %d\n",
                     frame, int(options.visible), int(options.active), options.row,
                     options.values[0], options.values[1], options.values[2], options.prompt_choice);
        std::fflush(stdout);
        window.last_options = options;
    }
    const auto bag = window.boot->RoomBag();
    const auto inventory_fields = [](const gameplay::RoomInventorySnapshot& s) {
        return std::tie(s.visible, s.list_ready, s.filter, s.selection, s.count,
                        s.slot, s.id, s.category, s.actions_ready, s.action_cursor, s.discard_choice);
    };
    if (inventory_fields(bag) != inventory_fields(window.last_inventory)) {
        std::fprintf(stdout, "inventory-state %u %d %d %u %u %u %d %d %d %d %u %d\n",
                     frame, int(bag.visible), int(bag.list_ready), bag.filter, bag.selection,
                     bag.count, bag.slot, bag.id, bag.category, int(bag.actions_ready), bag.action_cursor,
                     bag.discard_choice);
        std::fflush(stdout);
        window.last_inventory = bag;
    }
    const auto teaching = window.boot->RoomTeaching();
    const auto word_fields = [](const gameplay::RoomTeachingSnapshot& s) {
        return std::tie(s.menu_open, s.menu_cursor, s.menu_result, s.word_open, s.word_phase, s.row,
                        s.column, s.page, s.picking, s.choosing_candidate, s.composition_bytes,
                        s.written_bytes, s.reading_bytes, s.learned_serial, s.learned_row);
    };
    const auto nj_fields = [](const gameplay::RoomTeachingSnapshot& s) {
        return std::tie(s.word_open, s.candidates_known, s.choosing_candidate,
                        s.candidate_index, s.candidates);
    };
    const bool list_changed = nj_fields(teaching) != nj_fields(window.last_teaching);
    const bool word_changed = word_fields(teaching) != word_fields(window.last_teaching);
    if (list_changed) {
        std::fprintf(stdout, "nj-state %u %d %d %d %zu", frame, int(teaching.candidates_known),
                     int(teaching.choosing_candidate), teaching.candidate_index, teaching.candidates.size());
        for (const auto& spelling : teaching.candidates) {
            std::fputc(' ', stdout);
            for (unsigned char byte : spelling) std::fprintf(stdout, "%02x", unsigned(byte));
        }
        std::fputc('\n', stdout);
    }
    if (word_changed) {
        std::fprintf(stdout, "word-state %u %d %d %d %d %u %u %u %u %d %d %u %u %u %u %d",
                     frame, int(teaching.menu_open), teaching.menu_cursor, teaching.menu_result,
                     int(teaching.word_open), teaching.word_phase, teaching.row, teaching.column, teaching.page,
                     int(teaching.picking), int(teaching.choosing_candidate), teaching.composition_bytes,
                     teaching.written_bytes, teaching.reading_bytes, teaching.learned_serial, teaching.learned_row);
        for (const auto* text : {&teaching.learned_written, &teaching.learned_reading}) {
            std::fputc(' ', stdout);
            for (unsigned char byte : *text) std::fprintf(stdout, "%02x", unsigned(byte));
        }
        std::fputc('\n', stdout);
    }
    if (word_changed || list_changed) {
        std::fflush(stdout);
        window.last_teaching = teaching;
    }
    for (const auto& event : window.boot->DepartureEvents()) {
        std::fprintf(stdout, "departure-event %u %u %s", frame, event.serial, event.name);
        for (const auto& [key,value] : event.values) std::fprintf(stdout, " %s=%d", key, value);
        std::fputc('\n', stdout);
        if (!event.text.empty()) {
            std::fprintf(stdout, "departure-text %u %u", frame, event.serial);
            for (const auto& field : event.text) {
                std::fputc(' ',stdout);
                if (field.empty()) std::fputc('-',stdout);
                for (const unsigned char byte : field) std::fprintf(stdout,"%02x",unsigned(byte));
            }
            std::fputc('\n',stdout);
        }
        std::fflush(stdout);
    }
    const auto talk = window.boot->RoomConversation();
    const auto fields = [](const gameplay::RoomConversationSnapshot& s) {
        return std::tie(s.serial, s.topic_pc, s.boundary_pc, s.message_pc, s.option_count,
                        s.chosen_option, s.running, s.completed, s.word_entry_interactive);
    };
    if (talk.serial && fields(talk) != fields(window.last_conversation)) {
        std::fprintf(stdout, "talk-state %u %u %u %u %u %u %d %d %d %d\n", frame, talk.serial,
                     talk.topic_pc, talk.boundary_pc, talk.message_pc, talk.option_count, talk.chosen_option,
                     int(talk.running), int(talk.completed), int(talk.word_entry_interactive));
        std::fflush(stdout);
    }
    const auto dialog_fields = [](const gameplay::RoomConversationSnapshot& s) {
        return std::tie(s.serial,s.topic_pc,s.indirect_topic,s.message_pc,s.message_revealed,
                        s.message,s.word_operands,s.displayed_lines);
    };
    if (talk.serial && talk.message_pc && dialog_fields(talk) != dialog_fields(window.last_conversation)) {
        const auto hex_text = [](std::string_view text) {
            if(text.empty()) std::fputc('-',stdout);
            for(unsigned char byte : text) std::fprintf(stdout,"%02x",unsigned(byte));
        };
        std::fprintf(stdout, "talk-message %u %u %u %u %u %d ",frame,talk.serial,talk.topic_pc,
                     talk.indirect_topic,talk.message_pc,int(talk.message_revealed));
        hex_text(talk.message);
        std::fprintf(stdout," %zu",talk.word_rows.size());
        for(int row : talk.word_rows) std::fprintf(stdout," %d",row);
        std::fputc('\n',stdout);
        if (talk.message_revealed) {
            for(const auto& operand : talk.word_operands) {
                std::fprintf(stdout,"talk-operand %u %u %u %d %u %u %u %zu %zu\n",
                             frame,talk.serial,talk.message_pc,operand.row,operand.field,operand.control_pc,
                             operand.local,operand.offset,operand.length);
            }
            std::fprintf(stdout,"talk-lines %u %u %u %zu",frame,talk.serial,talk.message_pc,
                         talk.displayed_lines.size());
            for (const auto& line : talk.displayed_lines) {
                std::fputc(' ',stdout); hex_text(line);
            }
            std::fputc('\n',stdout);
        }
        std::fflush(stdout);
    }
    window.last_conversation = talk;
    const auto activity = window.boot->RoomActivity();
    if (!activity.serial) return;
    bool wrote = false;
    if (window.activity_serial != activity.serial) {
        const auto& in = activity.inputs;
        std::fprintf(stdout, "room-select %u %u %d %d %d %d %d %d %d %d %d %d %u\n",
                     frame, activity.serial, activity.selected_row ? int(*activity.selected_row) : -1,
                     activity.selected_command ? int(*activity.selected_command) : -1,
                     int(in.first_after_cleanup), in.scene, in.actor_field58, in.actor_field50,
                     in.period, in.mood, int(in.game_flag28), activity.effective_field58,
                     activity.eligible_rows);
        window.activity_serial = activity.serial;
        window.activity_started = window.activity_ready = window.activity_paused = false;
        wrote = true;
    }
    if (activity.started_command && !window.activity_started) {
        std::fprintf(stdout, "room-start %u %u %u\n", frame, activity.serial, *activity.started_command);
        window.activity_started = true;
        wrote = true;
    }
    if (activity.actor_wait_released && !window.activity_ready) {
        std::fprintf(stdout, "room-ready %u %u\n", frame, activity.serial);
        window.activity_ready = true;
        wrote = true;
    }
    if (activity.pause_ticks && !window.activity_paused) {
        std::fprintf(stdout, "room-pause %u %u %u\n", frame, activity.serial, activity.pause_ticks);
        window.activity_paused = true;
        wrote = true;
    }
    if (wrote) std::fflush(stdout);
}

uint32_t KeyButton(WPARAM key) {
    switch (key) {
    case VK_UP: return 0x10;
    case VK_RIGHT: return 0x20;
    case VK_DOWN: return 0x40;
    case VK_LEFT: return 0x80;
    case VK_RETURN: return 0x2000; // PSP Circle
    case VK_ESCAPE: return 0x4000; // PSP Cross
    case VK_SPACE: return 0x0008; // PSP Start
    case VK_TAB: return 0x8000; // PSP Square
    case 'T': return 0x1000; // PSP Triangle
    default: return 0;
    }
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    // DefWindowProc can ring the system bell for an unhandled menu mnemonic.
    if (message == WM_SYSCHAR && audio::Win32Player::UsesNullOutput()) return 0;
    if (message == WM_NCCREATE) {
        const auto create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto state = reinterpret_cast<WindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state && message == WM_POWERBROADCAST &&
        state->HandlePower(wparam, WindowState::Clock::now())) return TRUE;
    if (state && state->suspended &&
        (message == WM_TIMER || message == WM_KEYDOWN || message == WM_KEYUP || message == WM_CHAR)) return 0;
    if (state && (message == WM_KEYDOWN || message == WM_KEYUP)) {
        const auto bit = KeyButton(wparam);
        if (bit) {
            if (message == WM_KEYDOWN) {
                // A repeating key from before sleep or focus loss cannot start a new press.
                if ((lparam & (1L << 30)) && !(state->keys & bit)) return 0;
                state->pressed |= bit & ~state->keys;
                state->keys |= bit;
            } else state->keys &= ~bit;
            return 0;
        }
    }
    if (state && message == WM_KILLFOCUS) {
        state->keys=state->pressed=0;
        state->pad_latch.Reset();
    }
    if (state && state->boot && message == WM_CHAR) {
        if (wparam == VK_BACK) state->boot->BackspaceNickname();
        else if (wparam >= 0x20 && wparam != VK_SPACE && wparam <= 0xffff) {
            const wchar_t value = static_cast<wchar_t>(wparam);
            char bytes[2]{};
            BOOL substituted = FALSE;
            const auto count = WideCharToMultiByte(932, WC_NO_BEST_FIT_CHARS, &value, 1,
                bytes, sizeof(bytes), nullptr, &substituted);
            if (count && !substituted) state->boot->CommitNickname(std::string_view(bytes, count));
        }
        return 0;
    }
    if (message == WM_TIMER && state) {
        if (!state->boot) {
            auto observation = state->startup.Observe(UINT64_MAX);
            state->boot = state->startup.BootDisplay();
            if (!state->boot && !Failed(observation)) return 0;
            if (state->boot && (!state->music.Prepare(0) || !state->music.Prepare(1))) {
                std::fprintf(stderr, "Title bank: %s\n", state->music.Error().c_str());
                state->result = MissingMedia;
                DestroyWindow(window);
                return 0;
            }
            state->started = std::chrono::steady_clock::now();
        }
        if (!state->boot) {
            state->result = StartupFailed;
            DestroyWindow(window);
            return 0;
        }
        // Cold title submits at two PSP vblanks per frame. Use elapsed time
        // so Windows timer coalescing does not slow the authored animation.
        const auto frame = state->TargetFrame(WindowState::Clock::now());
        if (!frame || state->boot->TitleFrame() >= *frame) return 0;
        while (state->boot->TitleFrame() < *frame) {
            std::optional<std::uint32_t> pad_buttons;
            if (GetFocus() == window) {
                const auto sample = state->pad.ReadOne();
                if (sample && sample->sample) pad_buttons=sample->sample->buttons;
            }
            const auto buttons=state->FrameButtons(pad_buttons);
            if (!state->clock.RefreshFromWindows()) {
                state->result = StartupFailed;
                DestroyWindow(window);
                return 0;
            }
            state->boot->SetUpdateCalendar(state->clock.Snapshot().calendar_words);
            if (!state->boot->AdvanceTitle(buttons, state->effects.CharacterVoicePlaying())) {
                std::fprintf(stderr, "GuiTitle stopped at BOOT+%05x on title frame %u\n",
                             state->boot->FrontierPc(), state->boot->TitleFrame());
                if (const char* save_boundary = state->boot->SaveReadBoundary())
                    std::fprintf(stderr, "%s\n", save_boundary);
                state->result = StartupFailed;
                DestroyWindow(window);
                return 0;
            }
            ReportRoomActivity(*state);
            const int bank = state->boot->RequestedMusic();
            const int preload = state->boot->PreloadMusic();
            if (preload >= 0 && !state->music.Prepare(preload))
                std::fprintf(stderr, "Room audio: %s\n", state->music.Error().c_str());
            const auto settings = state->boot->Options().values;
            state->effects.SetVolumeSetting(settings[1]);
            if (!state->music.SetVolumeSetting(settings[2]))
                std::fprintf(stderr, "Room BGM gain: %s\n", state->music.Error().c_str());
            const auto effects = state->boot->RequestedEffects();
            // The confirm request precedes the panel-transition request.
            for (const int sequence : {4, 0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11}) {
                if ((effects & (1u << sequence)) && !state->effects.Play(sequence)) {
                    std::fprintf(stderr, "Title effect %d: %s\n", sequence, state->effects.Error().c_str());
                    if (sequence == 3) {
                        state->result = StartupFailed;
                        DestroyWindow(window);
                        return 0;
                    }
                }
            }
            if ((state->boot->Title().bgm_fade_requested() || state->boot->SceneMusicFade()) && !state->music.FadeOut())
                std::fprintf(stderr, "Title fade: %s\n", state->music.Error().c_str());
            for (const auto& sound : state->boot->RoomEffects())
                if (!state->effects.Play(sound))
                    std::fprintf(stderr, "Room effect %u/%u/%u: %s\n", sound.bank, sound.group,
                                 sound.sequence, state->effects.Error().c_str());
            if (bank >= 0 && !state->music.Start(bank)) {
                std::fprintf(stderr, "Title audio: %s\n", state->music.Error().c_str());
            }
            if (!state->music.DuckForCharacter(state->boot->Title().duck_bgm()))
                std::fprintf(stderr, "Character voice mix: %s\n", state->music.Error().c_str());
        }
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        auto dc = BeginPaint(window, &paint);
        if (!state || !state->boot || !gameplay::PresentDisplay(dc, state->boot->Screen()))
            FillRect(dc, &paint.rcPaint, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}

int RunWindow(HINSTANCE instance, int show, StartupCoordinator& session) {
    const auto audio_directory = PackageRoot() / L"assets/audio";
    WindowState state{session, {session, audio_directory}, gameplay::TitleEffects(audio_directory)};
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = WindowProc;
    wc.lpszClassName = L"DokodemoGame";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) return StartupFailed;
    constexpr DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT area{0, 0, Display::width, Display::height};
    if (!AdjustWindowRect(&area, style, FALSE)) return StartupFailed;
    auto window = CreateWindowExW(0, wc.lpszClassName, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
        area.right - area.left, area.bottom - area.top, nullptr, nullptr, instance, &state);
    if (!window) return StartupFailed;
    if (!SetTimer(window, 1, 10, nullptr)) { DestroyWindow(window); return StartupFailed; }
    ShowWindow(window, show);
    MSG msg{};
    BOOL received;
    while ((received = GetMessageW(&msg, nullptr, 0, 0)) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (IsWindow(window)) DestroyWindow(window);
    return received < 0 ? StartupFailed : state.result;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return BadArguments;
    const bool audio = argc == 2 && std::wstring_view(argv[1]) == L"--verify-audio";
    const bool capture = argc == 3 && std::wstring_view(argv[1]) == L"--capture-first-frame";
    const bool power_cycle = argc == 2 && std::wstring_view(argv[1]) == L"--verify-power-cycle";
    const bool startup_to_room = argc == 2 && std::wstring_view(argv[1]) == L"--verify-startup-to-room";
    const auto mode = startup_to_room ? VerifyMode::startup_to_room : power_cycle ? VerifyMode::power_cycle :
        audio ? VerifyMode::audio : VerifyMode::startup;
    const bool verify = startup_to_room || power_cycle || audio || capture ||
        (argc == 2 && std::wstring_view(argv[1]) == L"--verify-startup");
    const fs::path output = capture ? argv[2] : L"";
    LocalFree(argv);
    if (argc != 1 && !verify) return BadArguments;
    if (verify && !SetEnvironmentVariableW(L"DOKODEMO_TEST_AUDIO",L"null")) return StartupFailed;
    const bool null_audio = dokodemo::audio::Win32Player::UsesNullOutput();
    try {
        const auto root = PackageRoot();
        if (startup_to_room && fs::exists(root / L"savedata")) {
            std::fprintf(stderr, "Startup-to-room verification requires a fresh installation without savedata\n");
            return BadArguments;
        }
        if (!verify) SetProcessDPIAware(); // Present one native LCD pixel per client pixel.
        const auto media = root / L"assets/iso";
        const auto prx = media / L"PSP_GAME/USRDIR/prx";
        for (auto file : {L"audiocodec.prx", L"libatrac3plus.prx", L"sc_sascore.prx", L"ifhandle.prx",
                          L"pspnet.prx", L"memab.prx", L"pspnet_adhoc_auth.prx", L"pspnet_adhoc.prx", L"pspnet_adhocctl.prx"}) {
            if (!fs::is_regular_file(prx / file)) {
                if (null_audio) std::fprintf(stderr, "Missing game module: %ls\n", file);
                else if (!verify) MessageBoxW(nullptr, (L"Отсутствует игровой модуль " + (prx / file).wstring()).c_str(), title, MB_ICONERROR);
                return MissingMedia;
            }
        }
        StartupRuntimeOptions options{media.wstring()};
        const auto decoder = root / L"decoder";
        options.atrac_decoder_factory = audio::LoadFfmpegAtracDecoder(
            decoder / L"avcodec-63-2756d4fffb772585dcdece87427716a4.dll",
            decoder / L"avutil-61-e540ec7976bbd26a4a94dc737be3001c.dll");
        StartupRuntimeStatus status;
        auto session = StartupCoordinator::Create(options, status);
        if (session && status && session->Start()) {
            auto result = verify ? Verify(*session, root, output, mode) : RunWindow(instance, show, *session);
            if (!session->Close()) return CleanupFailed;
            const auto after = session->Observe(UINT64_MAX);
            if (!after.caller_joined || !after.worker_joined || !after.resources_closed ||
                !after.module_records.empty()) return CleanupFailed;
            if (verify || result == Success) return result;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
    }
    if (null_audio) std::fprintf(stderr, "Unable to load game resources or decoder\n");
    else if (!verify) MessageBoxW(nullptr, L"Не удалось загрузить ресурсы игры или нативный декодер.", title, MB_ICONERROR);
    return StartupFailed;
}
