#pragma once

#include "room_meeting_channel.h"
#include "ksc_save_graph.h"

namespace dokodemo::gameplay {

enum class RecordExchangeState { waiting, exchanging, complete, failed };

class RoomRecordExchange {
public:
    using Prepare = std::function<bool(const KscSaveValue&)>;
    using Commit = std::function<unsigned(const KscSaveValue&)>;

    RoomRecordExchange(std::unique_ptr<RoomMeetingChannel> channel,const KscSaveValue& record,
                       Prepare prepare,Commit commit);
    RecordExchangeState Poll();
    RecordExchangeState state() const { return state_; }
    std::optional<unsigned> received_row() const { return row_; }
private:
    std::unique_ptr<RoomMeetingChannel> channel_;
    Prepare prepare_;
    Commit commit_;
    RecordExchangeState state_=RecordExchangeState::waiting;
    std::vector<std::uint8_t> offer_;
    std::optional<KscSaveValue> received_;
    bool sent_receipt_=false, received_receipt_=false, sent_ready_=false, received_ready_=false;
    std::optional<unsigned> row_;
};

} // namespace dokodemo::gameplay
