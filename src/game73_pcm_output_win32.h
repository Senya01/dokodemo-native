#pragma once
#include "audio_pcm.h"
#include "game73_audio_request.h"
#include "game73_selected.h"
#include <array>
#include <cstdint>
#include <memory>
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
#include <atomic>
#include <array>
#endif

namespace dokodemo::audio {
enum class Game73OutputStage { Idle, Queued, Opening, Submitting, Draining, Completed, Failed, Cancelled, Closed };
enum class Game73OutputOperation { None, ComInitialize, CreateEnumerator, DefaultEndpoint, Activate,
    Initialize, GetBufferSize, SetEventHandle, GetRenderService, GetPadding, GetBuffer, ReleaseBuffer,
    Start, Stop, Wait, CloseHandle, Unexpected };
struct Game73DecodedPcmRequest {
    std::uint64_t request_id = 0;
    std::int32_t group = -1, sequence = -1;
    // Exclusive immutable ownership, transferred ONLY on Queued. No PackageView
    // or source allocation can escape into the output device's render buffers.
    std::unique_ptr<const PcmClip> clip;
};
struct Game73RenderedWitness {
    std::uint64_t request_id=0;
    recovered::PackageViewDescription bank_identity;
    SelectedSoundTrace trace;
    std::array<std::uint8_t,32> wave_sha256{},pcm_sha256{};
};
struct Game73PcmOutputSnapshot {
    Game73OutputStage stage = Game73OutputStage::Idle;
    Game73OutputOperation failed_operation = Game73OutputOperation::None;
    std::int32_t hresult = 0;
    std::uint32_t native_error = 0, actor_thread = 0;
    std::uint64_t request_id = 0, expected_frames = 0, submitted_frames = 0;
    std::uint64_t queued_requests = 0, fully_submitted_requests = 0, started_requests = 0;
    std::uint64_t completed_client_drain = 0, failed_requests = 0, cancelled_requests = 0;
    bool accepting = false, actor_exited = false, actor_joined = false;
    bool handles_closed = false, com_references_released = true, synthetic_device = false;
    bool selected_request=false;
    std::uint64_t selected_requests=0,rendered_requests=0;
    std::int32_t group=-1,entry=-1,volume=0,profile_gain=0,master_left=128,master_right=128;
    recovered::PackageViewDescription bank_identity;
    SelectedSoundCode render_code=SelectedSoundCode::Ok;
    SelectedSoundTrace render_trace;
    std::array<std::uint8_t,32> wave_sha256{},pcm_sha256{};
    Game73RenderedWitness last_rendered;
    std::uint64_t last_started_request_id=0,last_drained_request_id=0,last_failed_request_id=0,last_cancelled_request_id=0;
};
enum class Game73OutputCloseCode { Closed, TimedOut, HostFailure, WrongThread };
struct Game73OutputCloseResult {
    Game73OutputCloseCode code = Game73OutputCloseCode::Closed;
    std::uint32_t native_error = 0;
    explicit operator bool() const noexcept { return code == Game73OutputCloseCode::Closed; }
};
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
// Test-binary-only transport controls; no production factory accepts this.
struct Game73OutputTestControl {
    Game73OutputOperation fail = Game73OutputOperation::None;
    std::int32_t failure = static_cast<std::int32_t>(0x80004005u);
    std::uint32_t capacity = 32;
    std::atomic<bool> hold_open{false}, hold_drain{false};
    std::atomic<bool> hold_initialize{false}, initialize_entered{false};
    std::atomic<bool> hold_padding_return{false}, hold_after_submit{false};
    std::atomic<bool> padding_return_entered{false}, submit_return_entered{false};
    std::atomic<std::uint64_t> copied_samples{0};
    // Actor writes; inspect after the request finishes or actor joins. Prefix
    // capture is bounded and never claims COM teardown or real device evidence.
    std::array<std::int16_t,4096> captured_samples{};
    std::atomic<std::uint32_t> captured_count{0};
    std::array<std::atomic<std::uint32_t>,20> operation_calls{}, operation_threads{};
};
#endif
// One finite PCM request at a time, one COM/render owner. Queue admission does
// not mean device submission, Start, drain, or audible success. No SGX ABI here.
class Game73PcmOutput final : public Game73SelectedSink {
public:
    static std::unique_ptr<Game73PcmOutput> Create(std::uint32_t& error);
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
    static std::unique_ptr<Game73PcmOutput> CreateForTest(
        std::shared_ptr<Game73OutputTestControl>, std::uint32_t& error);
#endif
    ~Game73PcmOutput();
    Game73PcmOutput(const Game73PcmOutput&) = delete;
    Game73PcmOutput& operator=(const Game73PcmOutput&) = delete;
    Game73QueueCode TryQueueDecoded(Game73DecodedPcmRequest&) noexcept;
    Game73QueueCode TryQueueSelected(Game73SelectedBankRequest&) noexcept override;
    Game73PcmOutputSnapshot Snapshot() const noexcept;
    // Close mailbox FIRST; late requests receive Closed. No COM or join here.
    void RequestStop() noexcept;
    // On timeout/error this SAME owner remains live. Never detach, free borrowed
    // buffers, or require a game tick. Call before source caller/worker joins.
    // One coordinator calls StopAndJoin; RequestStop may race with that join.
    Game73OutputCloseResult StopAndJoin(std::uint32_t timeout_ms) noexcept;
private:
    struct Impl;
    explicit Game73PcmOutput(std::unique_ptr<Impl>) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace dokodemo::audio
