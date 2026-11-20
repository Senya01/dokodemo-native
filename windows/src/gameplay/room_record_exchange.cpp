#include "room_record_exchange.h"
#include <stdexcept>
#include <utility>

namespace dokodemo::gameplay {
namespace {
constexpr std::uint8_t offer=1, receipt=2, ready=3;
}

RoomRecordExchange::RoomRecordExchange(std::unique_ptr<RoomMeetingChannel> channel,const KscSaveValue& record,
                                       Prepare prepare,Commit commit)
    : channel_(std::move(channel)), prepare_(std::move(prepare)), commit_(std::move(commit)),
      offer_(EncodeKscSaveGraph(record)) {
    if (!channel_ || offer_.size()>=RoomMeetingChannel::max_message)
        state_=RecordExchangeState::failed;
    else
        offer_.insert(offer_.begin(),offer);
}

RecordExchangeState RoomRecordExchange::Poll() try {
    if (state_==RecordExchangeState::complete || state_==RecordExchangeState::failed) return state_;
    const auto connection=channel_->Poll();
    if (connection==MeetingConnection::looking) return state_;
    if (connection!=MeetingConnection::connected) return state_=RecordExchangeState::failed;
    state_=RecordExchangeState::exchanging;

    if (!offer_.empty() && channel_->Send(offer_)) offer_.clear();
    if (const auto message=channel_->Receive()) {
        if (message->size()>1 && message->front()==offer && !received_) {
            std::size_t used=0;
            auto peer=DecodeKscSaveGraph(message->data()+1,message->size()-1,used);
            if (used!=message->size()-1 || !prepare_(peer)) return state_=RecordExchangeState::failed;
            received_=std::move(peer);
        } else if (message->size()==1 && message->front()==receipt && received_ && !received_receipt_) {
            received_receipt_=true;
        } else if (message->size()==1 && message->front()==ready && received_receipt_ && !received_ready_) {
            received_ready_=true;
        } else {
            return state_=RecordExchangeState::failed;
        }
    }
    if (offer_.empty() && received_ && !sent_receipt_) sent_receipt_=channel_->Send({receipt});
    if (received_receipt_ && sent_receipt_ && !sent_ready_) sent_ready_=channel_->Send({ready});
    // Exchange approval follows validation in both directions. The live
    // owner changes only once, after both readiness messages have crossed.
    if (received_ready_ && sent_ready_ && !channel_->HasPendingOutput()) {
        row_=commit_(*received_);
        return state_=RecordExchangeState::complete;
    }
    return state_;
} catch (const std::invalid_argument&) {
    return state_=RecordExchangeState::failed;
} catch (const std::runtime_error&) {
    return state_=RecordExchangeState::failed;
}
} // namespace dokodemo::gameplay
