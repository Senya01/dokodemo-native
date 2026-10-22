#include "game_sound_component_regressions.h"
#include "../src/game73_pcm_output_win32.h"
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace {
using namespace dokodemo::audio;
void Check(bool ok,const char* message) { if (!ok) throw std::runtime_error(message); }
template<class F> void Until(F predicate,const char* message) {
    const auto end = GetTickCount64()+3000;
    while (!predicate()) { if (GetTickCount64()>=end) throw std::runtime_error(message); Sleep(1); }
}
Game73DecodedPcmRequest Request(std::uint64_t id,std::size_t frames=97,std::uint16_t channels=2) {
    auto pcm=std::make_unique<PcmClip>(); pcm->sample_rate=48000; pcm->channels=channels;
    for (std::size_t n=0;n<frames*channels;++n) pcm->samples.push_back(static_cast<std::int16_t>((n*137+id*23)%2001-1000));
    return {id,0,4,std::move(pcm)};
}
std::unique_ptr<Game73PcmOutput> Actor(const std::shared_ptr<Game73OutputTestControl>& c) {
    std::uint32_t error=0; auto a=Game73PcmOutput::CreateForTest(c,error); Check(a&&error==0,"create test actor"); return a;
}
void Queue(Game73PcmOutput& a,Game73DecodedPcmRequest& r) {
    Game73QueueCode q=Game73QueueCode::Busy;
    Until([&]{q=a.TryQueueDecoded(r);return q!=Game73QueueCode::Busy;},"queue contention");
    Check(q==Game73QueueCode::Queued&&!r.clip,"exclusive PCM transfer");
}
void Closed(Game73PcmOutput& a) {
    Check(bool(a.StopAndJoin(3000)),"audio actor stop/join"); const auto s=a.Snapshot();
    Check(!s.accepting&&s.actor_exited&&s.actor_joined&&s.handles_closed&&s.com_references_released,"joined output ownership");
    auto late=Request(999); Check(a.TryQueueDecoded(late)==Game73QueueCode::Closed&&bool(late.clip),"closed leaves late owner");
    Check(bool(a.StopAndJoin(0)),"idempotent joined close");
}
void OwnershipAndContent() {
    auto c=std::make_shared<Game73OutputTestControl>(); auto a=Actor(c);
    auto bad=Request(1,1); const_cast<PcmClip*>(bad.clip.get())->channels=3;
    Check(a->TryQueueDecoded(bad)==Game73QueueCode::InvalidPcm&&bool(bad.clip),"invalid keeps PCM");
    std::uint64_t priorFull=0,priorStart=0,priorDrain=0;
    for (std::uint64_t id=1;id<=2;++id) {
        c->hold_open.store(true); c->captured_count.store(0); c->copied_samples.store(0);
        auto request=Request(id); const auto expected=request.clip->samples;
        Queue(*a,request); Until([&]{return a->Snapshot().stage==Game73OutputStage::Opening;},"opening");
        auto busy=Request(id+20); Check(a->TryQueueDecoded(busy)==Game73QueueCode::Busy&&bool(busy.clip),"one outstanding owner");
        c->hold_open.store(false);
        Until([&]{return a->Snapshot().stage==Game73OutputStage::Completed;},"complete fake request");
        auto s=a->Snapshot();
        Check(s.request_id==id&&s.expected_frames==97&&s.submitted_frames==97,"per-request frame identity");
        Check(s.fully_submitted_requests==priorFull+1&&s.started_requests==priorStart+1&&s.completed_client_drain==priorDrain+1,"per-request counter deltas");
        priorFull=s.fully_submitted_requests;priorStart=s.started_requests;priorDrain=s.completed_client_drain;
        Check(s.failed_requests==0&&s.cancelled_requests==0&&s.synthetic_device,"fake success classification");
        Check(c->captured_count.load()==expected.size()&&c->copied_samples.load()==expected.size(),"captured sample extent");
        Check(std::equal(expected.begin(),expected.end(),c->captured_samples.begin()),"sample values/channel order retained");
        auto replay=Request(id); Check(a->TryQueueDecoded(replay)==Game73QueueCode::StaleRequest&&bool(replay.clip),"monotonic request identity");
        for (auto op:{Game73OutputOperation::GetPadding,Game73OutputOperation::GetBuffer,Game73OutputOperation::ReleaseBuffer,Game73OutputOperation::Start,Game73OutputOperation::Stop}) {
            const auto i=static_cast<std::size_t>(op); Check(c->operation_calls[i].load()>0&&c->operation_threads[i].load()==s.actor_thread,"observed transport op on actor");
        }
        Check(c->operation_calls[static_cast<std::size_t>(Game73OutputOperation::GetBuffer)].load()==c->operation_calls[static_cast<std::size_t>(Game73OutputOperation::ReleaseBuffer)].load(),"fake acquire/release call pairing");
    }
    Closed(*a);
}
void Errors() {
    for (auto op:{Game73OutputOperation::DefaultEndpoint,Game73OutputOperation::Initialize,Game73OutputOperation::GetPadding,
            Game73OutputOperation::GetBuffer,Game73OutputOperation::ReleaseBuffer,Game73OutputOperation::Start,Game73OutputOperation::Stop}) {
        auto c=std::make_shared<Game73OutputTestControl>();c->fail=op;auto a=Actor(c);auto r=Request(1);Queue(*a,r);
        Until([&]{return a->Snapshot().failed_requests==1;},"failed output request");auto s=a->Snapshot();
        Check(s.failed_operation==op&&s.hresult==c->failure&&s.cancelled_requests==0,"typed device error not cancellation");
        // Actor has finished its request before the control is changed.
        c->fail=Game73OutputOperation::None;c->hold_open.store(true);auto retry=Request(2);Queue(*a,retry);
        s=a->Snapshot();Check(s.request_id==2&&s.failed_operation==Game73OutputOperation::None&&s.hresult==0,"new queued request clears prior error");
        c->hold_open.store(false);Until([&]{return a->Snapshot().stage==Game73OutputStage::Completed;},"retry after device error");
        s=a->Snapshot();Check(s.failed_requests==1&&s.started_requests==(op==Game73OutputOperation::Stop?2u:1u),"lifetime counts independent of current error");Closed(*a);
    }
    auto c=std::make_shared<Game73OutputTestControl>();c->hold_initialize.store(true);c->fail=Game73OutputOperation::ComInitialize;
    auto a=Actor(c);Until([&]{return c->initialize_entered.load();},"initialization held");auto r=Request(1);Queue(*a,r);c->hold_initialize.store(false);
    Until([&]{return a->Snapshot().actor_exited;},"fatal initialization exit");auto s=a->Snapshot();
    Check(s.failed_requests==1&&s.cancelled_requests==0&&s.failed_operation==Game73OutputOperation::ComInitialize&&!s.accepting,"pending fatal classified failed");Closed(*a);
}
void CancellationAndTimeout() {
    for (bool afterSubmit:{false,true}) {
        auto c=std::make_shared<Game73OutputTestControl>();c->hold_padding_return.store(!afterSubmit);c->hold_after_submit.store(afterSubmit);
        auto a=Actor(c);auto r=Request(1);Queue(*a,r);
        Until([&]{return afterSubmit?c->submit_return_entered.load():c->padding_return_entered.load();},"fresh first render boundary");
        a->RequestStop();c->hold_padding_return.store(false);c->hold_after_submit.store(false);
        Closed(*a);auto s=a->Snapshot();Check(s.started_requests==0&&s.completed_client_drain==0&&s.cancelled_requests==1,"stop before Start refuses completion");
    }
    // A new Padding entry at the drain boundary, never the sticky first entry.
    auto c=std::make_shared<Game73OutputTestControl>();c->hold_after_submit.store(true);auto a=Actor(c);auto r=Request(1,16);Queue(*a,r);
    Until([&]{return c->submit_return_entered.load();},"submitted boundary");c->padding_return_entered.store(false);c->hold_padding_return.store(true);c->hold_after_submit.store(false);
    Until([&]{return c->padding_return_entered.load();},"NEW drain padding boundary");a->RequestStop();c->hold_padding_return.store(false);Closed(*a);
    Check(a->Snapshot().started_requests==1&&a->Snapshot().completed_client_drain==0&&a->Snapshot().cancelled_requests==1,"stop during zero-padding return not drain");
    auto t=std::make_shared<Game73OutputTestControl>();t->hold_open.store(true);auto timeout=Actor(t);auto pending=Request(1);Queue(*timeout,pending);
    Until([&]{return timeout->Snapshot().stage==Game73OutputStage::Opening;},"held Open");
    Check(timeout->StopAndJoin(1).code==Game73OutputCloseCode::TimedOut,"timeout retains actor");auto s=timeout->Snapshot();
    Check(!s.accepting&&!s.actor_joined&&!s.handles_closed,"timeout no detach/free");t->hold_open.store(false);Closed(*timeout);
    auto race=Actor(std::make_shared<Game73OutputTestControl>());
    std::thread stopper([&]{for (int i=0;i<2000;++i)race->RequestStop();});const auto joined=race->StopAndJoin(3000);stopper.join();Check(bool(joined),"join while stop signals");Closed(*race);
}
void WaveReference(const char* encoded,const char* reference) {
    std::ifstream in(encoded,std::ios::binary);Check(bool(in),"encoded candidate path");
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
    auto view=dokodemo::game_sound_codec_checks_detail::View(std::move(bytes));PcmClip pcm;
    Check(bool(DecodePsAdpcmMono(view,44100,4928,pcm))&&pcm.FrameCount()==4928,"candidate bounded native decode");
    std::ifstream ref(reference,std::ios::binary);Check(bool(ref),"reference path");std::vector<char> raw((std::istreambuf_iterator<char>(ref)),{});
    Check(raw.size()==pcm.samples.size()*2,"reference extent");
    for (std::size_t i=0;i<pcm.samples.size();++i) Check(static_cast<std::uint16_t>(pcm.samples[i])==(std::uint16_t(static_cast<unsigned char>(raw[2*i]))|(std::uint16_t(static_cast<unsigned char>(raw[2*i+1]))<<8)),"native storage decode byte equality");
    std::cout<<"CANDIDATE_STORAGE_PCM_MATCH frames="<<pcm.FrameCount()<<"; no SESQ/GAME73 output credit\n";
}
int RealDevice(bool stopDuringPlayback) {
    std::uint32_t error=0;auto a=Game73PcmOutput::Create(error);if (!a) {std::cout<<"DEVICE_CREATE_ERROR "<<error<<'\n';return 2;}
    auto r=Request(1,stopDuringPlayback?48000:4800,1);Queue(*a,r);
    if (stopDuringPlayback) {
        Until([&]{auto s=a->Snapshot();return s.started_requests==1||s.stage==Game73OutputStage::Failed;},"device Start before cancellation");
        a->RequestStop();
        Until([&]{auto s=a->Snapshot();return s.cancelled_requests==1||s.stage==Game73OutputStage::Failed;},"device cancellation terminal");
    } else
    Until([&]{auto s=a->Snapshot();return s.stage==Game73OutputStage::Completed||s.stage==Game73OutputStage::Failed;},"device request terminal");
    const auto s=a->Snapshot();Closed(*a);
    std::cout<<"REAL_DEVICE stage="<<int(s.stage)<<" operation="<<int(s.failed_operation)<<" hr="<<s.hresult<<" frames="<<s.submitted_frames<<" started="<<s.started_requests<<" drain="<<s.completed_client_drain<<" cancelled="<<s.cancelled_requests<<" synthetic="<<s.synthetic_device<<" joined=1 COM_released=1 handles_closed=1; audible/listened/loopback=UNVERIFIED\n";
    return s.failed_requests==0&&(stopDuringPlayback?(s.cancelled_requests==1&&s.completed_client_drain==0):(s.completed_client_drain==1))?0:2;
}
}
int main(int argc,char** argv) {
    try {
        if (argc==2&&std::string(argv[1])=="--device")return RealDevice(false);
        if (argc==2&&std::string(argv[1])=="--device-stop")return RealDevice(true);
        if (argc==4&&std::string(argv[1])=="--wave-reference") {WaveReference(argv[2],argv[3]);return 0;}
        Check(argc==1,"unknown sound test arguments");
        dokodemo::GameSoundCodecRegressionChecks();std::cout<<"CODEC 4/4 groups\n";
        OwnershipAndContent();Errors();CancellationAndTimeout();
        std::cout<<"OUTPUT 3/3 groups; synthetic transport, no real COM or audible credit\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
