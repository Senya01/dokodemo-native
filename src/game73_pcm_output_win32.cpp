#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "game73_pcm_output_win32.h"
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <process.h>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <exception>
#include <utility>

namespace dokodemo::audio {
namespace {
struct Lock {
    SRWLOCK& value;
    explicit Lock(SRWLOCK& v) noexcept : value(v) { AcquireSRWLockExclusive(&value); }
    ~Lock() { ReleaseSRWLockExclusive(&value); }
};
struct TryLock {
    SRWLOCK& value;
    bool acquired;
    explicit TryLock(SRWLOCK& v) noexcept : value(v), acquired(TryAcquireSRWLockExclusive(&value) != FALSE) {}
    ~TryLock() { if (acquired) ReleaseSRWLockExclusive(&value); }
};
template<class T> struct Com {
    T* value = nullptr;
    ~Com() { Reset(); }
    void Reset() noexcept { if (value) { value->Release(); value = nullptr; } }
    T** Address() noexcept { Reset(); return &value; }
    T* operator->() const noexcept { return value; }
};
DWORD WaitWithMessages(HANDLE first, HANDLE second, DWORD timeout) noexcept {
    HANDLE handles[2] = {first, second};
    const auto start = GetTickCount64();
    for (;;) {
        const auto elapsed = GetTickCount64() - start;
        const DWORD remaining = timeout == INFINITE ? INFINITE : elapsed >= timeout ? 0 : timeout - static_cast<DWORD>(elapsed);
        const DWORD result = MsgWaitForMultipleObjectsEx(2,handles,remaining,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        if (result != WAIT_OBJECT_0 + 2) return result;
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
}
struct Device {
    Com<IMMDeviceEnumerator> enumerator;
    Com<IMMDevice> device;
    Com<IAudioClient> client;
    Com<IAudioRenderClient> render;
    UINT32 capacity = 0;
    WORD channels = 0;
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
    std::shared_ptr<Game73OutputTestControl> test;
    UINT32 test_padding = 0;
    HANDLE test_event = nullptr;
    void Observe(Game73OutputOperation op) noexcept {
        const auto i = static_cast<std::size_t>(op);
        test->operation_calls[i].fetch_add(1); test->operation_threads[i].store(GetCurrentThreadId());
    }
#endif
    HRESULT Open(const PcmClip& pcm, HANDLE event, Game73OutputOperation& operation) {
        channels = pcm.channels;
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
        if (test) {
            test_event = event;
            while (test->hold_open.load()) Sleep(1); // models a blocked driver call in this test binary only
            for (auto op : {Game73OutputOperation::CreateEnumerator,Game73OutputOperation::DefaultEndpoint,
                    Game73OutputOperation::Activate,Game73OutputOperation::Initialize,Game73OutputOperation::SetEventHandle,
                    Game73OutputOperation::GetBufferSize,Game73OutputOperation::GetRenderService}) {
                operation = op; if (test->fail == op) return test->failure;
                Observe(op);
            }
            capacity = test->capacity;
            return capacity ? S_OK : E_INVALIDARG;
        }
#endif
        operation = Game73OutputOperation::CreateEnumerator;
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,
            __uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(enumerator.Address()));
        if (FAILED(result)) return result;
        operation = Game73OutputOperation::DefaultEndpoint;
        result = enumerator->GetDefaultAudioEndpoint(eRender,eConsole,device.Address());
        if (FAILED(result)) return result;
        operation = Game73OutputOperation::Activate;
        result = device->Activate(__uuidof(IAudioClient),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(client.Address()));
        if (FAILED(result)) return result;
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = channels;
        format.nSamplesPerSec = pcm.sample_rate; format.wBitsPerSample = 16;
        format.nBlockAlign = WORD(channels * 2); format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        operation = Game73OutputOperation::Initialize;
        result = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            0,0,&format,nullptr);
        if (FAILED(result)) return result;
        operation = Game73OutputOperation::SetEventHandle;
        result = client->SetEventHandle(event); if (FAILED(result)) return result;
        operation = Game73OutputOperation::GetBufferSize;
        result = client->GetBufferSize(&capacity); if (FAILED(result)) return result;
        if (!capacity) return E_UNEXPECTED;
        operation = Game73OutputOperation::GetRenderService;
        return client->GetService(__uuidof(IAudioRenderClient),reinterpret_cast<void**>(render.Address()));
    }
    HRESULT Padding(UINT32& padding, Game73OutputOperation& operation) noexcept {
        operation = Game73OutputOperation::GetPadding;
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
        if (test) {
            Observe(operation);
            if (test->fail == operation) return test->failure;
            if (!test->hold_drain.load()) test_padding = 0;
            padding = test_padding; test->padding_return_entered.store(true);
            while (test->hold_padding_return.load()) Sleep(1);
            return S_OK;
        }
#endif
        return client->GetCurrentPadding(&padding);
    }
    HRESULT Submit(const std::int16_t* samples, UINT32 count, Game73OutputOperation& operation) noexcept {
        operation = Game73OutputOperation::GetBuffer;
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
        if (test) {
            Observe(operation);
            if (test->fail == operation) return test->failure;
            operation = Game73OutputOperation::ReleaseBuffer;
            Observe(operation);
            if (test->fail == operation) return test->failure;
            auto captured = test->captured_count.load();
            for (std::uint64_t i = 0; i < std::uint64_t(count)*channels && captured < test->captured_samples.size(); ++i)
                test->captured_samples[captured++] = samples[i];
            test->captured_count.store(captured);
            test_padding += count; test->copied_samples.fetch_add(std::uint64_t(count) * channels);
            SetEvent(test_event);
            test->submit_return_entered.store(true);
            while (test->hold_after_submit.load()) Sleep(1);
            return S_OK;
        }
#endif
        BYTE* destination = nullptr;
        HRESULT result = render->GetBuffer(count,&destination); if (FAILED(result)) return result;
        if (!destination) { render->ReleaseBuffer(0,0); return E_POINTER; }
        std::memcpy(destination,samples,std::size_t(count) * channels * sizeof(std::int16_t));
        operation = Game73OutputOperation::ReleaseBuffer;
        return render->ReleaseBuffer(count,0);
    }
    HRESULT Start(Game73OutputOperation& operation) noexcept {
        operation = Game73OutputOperation::Start;
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
        if (test) { Observe(operation); return test->fail == operation ? test->failure : S_OK; }
#endif
        return client->Start();
    }
    HRESULT Stop(Game73OutputOperation& operation) noexcept {
        operation = Game73OutputOperation::Stop;
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
        if (test) { Observe(operation); return test->fail == operation ? test->failure : S_OK; }
#endif
        return client.value ? client->Stop() : S_OK;
    }
    void Close() noexcept {
        // All COM releases occur on the render actor, never on the game thread.
        render.Reset(); client.Reset(); device.Reset(); enumerator.Reset();
    }
};
}

struct Game73PcmOutput::Impl {
    mutable SRWLOCK lock = SRWLOCK_INIT;
    SRWLOCK handle_lock = SRWLOCK_INIT;
    HANDLE stop = nullptr, work = nullptr, audio = nullptr, thread = nullptr;
    unsigned thread_id = 0;
    Game73PcmOutputSnapshot state;
    Game73DecodedPcmRequest pending;
    bool outstanding = false;
    std::atomic<bool> admission{false}, stop_requested{false};
    std::atomic<DWORD> stop_error{0};
    std::atomic_flag joining = ATOMIC_FLAG_INIT;
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
    std::shared_ptr<Game73OutputTestControl> test;
#endif
    ~Impl() {
        // Factory failure only; the public destructor joins a running actor.
        for (HANDLE h : {thread,audio,work,stop}) if (h) CloseHandle(h);
    }
    bool Cancelled() const noexcept { return stop_requested.load(); }
    void Failure(Game73OutputOperation operation, HRESULT result, DWORD error = 0) noexcept {
        Lock guard(lock); state.failed_operation = operation; state.hresult = result;
        state.native_error = error; state.stage = Game73OutputStage::Failed;
    }
    void Play(Game73DecodedPcmRequest request) {
        Device device;
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
        device.test = test;
#endif
        { Lock guard(lock); state.stage = Game73OutputStage::Opening;
          state.com_references_released = state.synthetic_device; }
        Game73OutputOperation operation = Game73OutputOperation::None;
        HRESULT result = device.Open(*request.clip,audio,operation);
        bool cancelled = false, failed = FAILED(result), started = false, full = false, drained = false;
        if (failed) Failure(operation,result);
        std::uint64_t position = 0;
        const auto frames = request.clip->FrameCount();
        const auto deadline = GetTickCount64() + (frames * 1000 + request.clip->sample_rate - 1) / request.clip->sample_rate + 5000;
        while (!failed) {
            if (Cancelled()) { cancelled = true; break; }
            UINT32 padding = 0;
            result = device.Padding(padding,operation);
            if (FAILED(result) || padding > device.capacity) { failed = true; Failure(operation,FAILED(result)?result:E_UNEXPECTED); break; }
            if (Cancelled()) { cancelled = true; break; }
            const auto count = static_cast<UINT32>((std::min)(frames-position,std::uint64_t(device.capacity-padding)));
            if (count) {
                result = device.Submit(request.clip->samples.data()+position*request.clip->channels,count,operation);
                if (FAILED(result)) { failed = true; Failure(operation,result); break; }
                position += count;
                { Lock guard(lock); state.submitted_frames = position; state.stage = Game73OutputStage::Submitting;
                  if (!full && position == frames) { full = true; ++state.fully_submitted_requests; } }
                if (Cancelled()) { cancelled = true; break; }
                if (!started) {
                    result = device.Start(operation);
                    if (FAILED(result)) { failed = true; Failure(operation,result); break; }
                    started = true; Lock guard(lock); ++state.started_requests;
                }
            } else if (position == frames && padding == 0) {
                Lock guard(lock);
                if (Cancelled()) cancelled = true;
                else { drained = true; ++state.completed_client_drain; }
                break;
            }
            { Lock guard(lock); if (full) state.stage = Game73OutputStage::Draining; }
            if (GetTickCount64() >= deadline) { failed = true; Failure(Game73OutputOperation::Wait,HRESULT_FROM_WIN32(ERROR_TIMEOUT)); break; }
            const DWORD wait = WaitWithMessages(stop,audio,100);
            if (wait == WAIT_OBJECT_0) { cancelled = true; break; }
            if (wait != WAIT_OBJECT_0 + 1 && wait != WAIT_TIMEOUT) {
                failed = true; const auto error = GetLastError(); Failure(Game73OutputOperation::Wait,HRESULT_FROM_WIN32(error),error); break;
            }
        }
        // Stop failure is recorded but cannot retain app buffers: WASAPI owns
        // every copied render buffer. Release all COM objects on THIS thread.
        const auto stopped = device.Stop(operation);
        if (FAILED(stopped)) { if (!failed) Failure(operation,stopped); failed = true; }
        device.Close(); request.clip.reset();
        { Lock guard(lock); state.com_references_released = true; outstanding = false;
          if (failed) { ++state.failed_requests; state.stage = Game73OutputStage::Failed; }
          else if (cancelled) { ++state.cancelled_requests; state.stage = Game73OutputStage::Cancelled; }
          else if (drained) state.stage = Game73OutputStage::Completed; }
    }
    static unsigned __stdcall Entry(void* raw) noexcept { static_cast<Impl*>(raw)->Run(); return 0; }
    void Run() noexcept {
        bool initialized = false, fatal = false;
        { Lock guard(lock); state.actor_thread = GetCurrentThreadId(); }
        try {
            HRESULT result = S_OK;
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
            if (test) {
                test->initialize_entered.store(true);
                while (test->hold_initialize.load()) Sleep(1);
                if (test->fail == Game73OutputOperation::ComInitialize) result = test->failure;
            } else
#endif
                result = CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            if (FAILED(result)) { fatal = true; admission.store(false); Failure(Game73OutputOperation::ComInitialize,result); Lock guard(lock); state.accepting = false; }
            else {
                initialized = true;
                for (;;) {
                    const DWORD wait = WaitWithMessages(stop,work,INFINITE);
                    if (wait == WAIT_OBJECT_0 || Cancelled()) break;
                    if (wait != WAIT_OBJECT_0+1) { fatal = true; Failure(Game73OutputOperation::Wait,HRESULT_FROM_WIN32(GetLastError()),GetLastError()); break; }
                    Game73DecodedPcmRequest request;
                    { Lock guard(lock); if (!pending.clip) continue; request = std::move(pending);
                      state.failed_operation = Game73OutputOperation::None; state.hresult = 0; state.native_error = 0; }
                    Play(std::move(request));
                }
            }
        } catch (...) { fatal = true; Failure(Game73OutputOperation::Unexpected,E_UNEXPECTED); }
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
        if (initialized && !test) CoUninitialize();
#else
        if (initialized) CoUninitialize();
#endif
        admission.store(false);
        { Lock guard(lock); state.accepting = false;
          if (pending.clip) { pending.clip.reset(); if (fatal) ++state.failed_requests; else ++state.cancelled_requests; }
          else if (fatal && outstanding) ++state.failed_requests;
          outstanding = false; state.com_references_released = true; state.actor_exited = true; }
    }
    bool Start(std::uint32_t& error) noexcept {
        stop = CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if (!stop) { error = GetLastError(); return false; }
        work = CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if (!work) { error = GetLastError(); return false; }
        audio = CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if (!audio) { error = GetLastError(); return false; }
        { Lock guard(lock); state.accepting = true; }
        admission.store(true);
        thread = reinterpret_cast<HANDLE>(_beginthreadex(nullptr,0,Entry,this,0,&thread_id));
        if (!thread) { error = ERROR_NOT_ENOUGH_MEMORY; admission.store(false); state.accepting = false; return false; }
        return true;
    }
};

Game73PcmOutput::Game73PcmOutput(std::unique_ptr<Impl> value) noexcept : impl_(std::move(value)) {}
std::unique_ptr<Game73PcmOutput> Game73PcmOutput::Create(std::uint32_t& error) {
    error = 0;
    auto result = std::unique_ptr<Game73PcmOutput>(new Game73PcmOutput(std::make_unique<Impl>()));
    if (!result->impl_->Start(error)) return {};
    return result;
}
#ifdef DOKODEMO_SOUND_OUTPUT_TESTS
std::unique_ptr<Game73PcmOutput> Game73PcmOutput::CreateForTest(std::shared_ptr<Game73OutputTestControl> control,std::uint32_t& error) {
    error = 0; if (!control) { error = ERROR_INVALID_PARAMETER; return {}; }
    auto result = std::unique_ptr<Game73PcmOutput>(new Game73PcmOutput(std::make_unique<Impl>()));
    result->impl_->test = std::move(control); result->impl_->state.synthetic_device = true;
    if (!result->impl_->Start(error)) return {};
    return result;
}
#endif
Game73PcmOutput::~Game73PcmOutput() {
    if (impl_ && impl_->thread && GetCurrentThreadId() == impl_->thread_id) std::terminate();
    if (impl_) { const auto closed = StopAndJoin(INFINITE); if (!closed) std::terminate(); }
}
Game73QueueCode Game73PcmOutput::TryQueueDecoded(Game73DecodedPcmRequest& request) noexcept {
    if (!impl_->admission.load()) return Game73QueueCode::Closed;
    TryLock guard(impl_->lock);
    if (!guard.acquired) return impl_->admission.load() ? Game73QueueCode::Busy : Game73QueueCode::Closed;
    if (!impl_->admission.load()) return Game73QueueCode::Closed;
    if (!request.request_id || request.request_id <= impl_->state.request_id) return Game73QueueCode::StaleRequest;
    const auto* pcm = request.clip.get();
    if (!pcm || (pcm->channels != 1 && pcm->channels != 2) || pcm->sample_rate < 8000 || pcm->sample_rate > 192000
        || pcm->samples.empty() || pcm->samples.size()%pcm->channels || pcm->FrameCount() > (1u<<22)) return Game73QueueCode::InvalidPcm;
    if (impl_->outstanding) return Game73QueueCode::Busy;
    if (!SetEvent(impl_->work)) { impl_->state.native_error = GetLastError(); impl_->admission.store(false); impl_->state.accepting = false; return Game73QueueCode::NativeFailure; }
    impl_->state.request_id = request.request_id; impl_->state.expected_frames = pcm->FrameCount();
    impl_->state.failed_operation = Game73OutputOperation::None; impl_->state.hresult = 0; impl_->state.native_error = 0;
    impl_->state.submitted_frames = 0; ++impl_->state.queued_requests; impl_->state.stage = Game73OutputStage::Queued;
    impl_->pending = std::move(request); impl_->outstanding = true;
    return Game73QueueCode::Queued;
}
Game73PcmOutputSnapshot Game73PcmOutput::Snapshot() const noexcept {
    Lock guard(impl_->lock); auto state = impl_->state;
    state.accepting = impl_->admission.load();
    if (impl_->stop_error.load()) state.native_error = impl_->stop_error.load();
    return state;
}
void Game73PcmOutput::RequestStop() noexcept {
    impl_->admission.store(false); // close admission before any cancellation/join
    impl_->stop_requested.store(true);
    Lock lifetime(impl_->handle_lock);
    if (impl_->stop && !SetEvent(impl_->stop)) {
        impl_->stop_error.store(GetLastError());
        if (impl_->work) SetEvent(impl_->work); // actor checks the atomic stop latch on wake
    }
}
Game73OutputCloseResult Game73PcmOutput::StopAndJoin(std::uint32_t timeout) noexcept {
    if (impl_->joining.test_and_set()) return {Game73OutputCloseCode::HostFailure,ERROR_BUSY};
    struct Clear { std::atomic_flag& flag; ~Clear() { flag.clear(); } } clear{impl_->joining};
    RequestStop();
    if (impl_->thread) {
        if (GetCurrentThreadId() == impl_->thread_id) return {Game73OutputCloseCode::WrongThread,ERROR_INVALID_THREAD_ID};
        const DWORD wait = WaitForSingleObject(impl_->thread,timeout);
        if (wait == WAIT_TIMEOUT) return {Game73OutputCloseCode::TimedOut,ERROR_TIMEOUT};
        if (wait != WAIT_OBJECT_0) return {Game73OutputCloseCode::HostFailure,GetLastError()};
        { Lock guard(impl_->lock); impl_->state.actor_joined = true; }
    }
    DWORD error = 0;
    Lock lifetime(impl_->handle_lock);
    for (HANDLE* handle : {&impl_->thread,&impl_->audio,&impl_->work,&impl_->stop}) {
        if (*handle) { if (CloseHandle(*handle)) *handle = nullptr; else if (!error) error = GetLastError(); }
    }
    if (error) return {Game73OutputCloseCode::HostFailure,error};
    { Lock guard(impl_->lock); impl_->state.handles_closed = true; impl_->state.stage = Game73OutputStage::Closed; }
    return {};
}
} // namespace dokodemo::audio
