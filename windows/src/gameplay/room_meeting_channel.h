#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace dokodemo::gameplay {

enum class MeetingMode : std::uint8_t { cards, shiritori, gifts };
enum class MeetingConnection { looking, connected, disconnected, unavailable };

struct MeetingPlace {
    MeetingMode mode;
    std::uint8_t venue;
};

class RoomMeetingChannel {
public:
    static constexpr std::size_t max_message = 2048;

    virtual ~RoomMeetingChannel() = default;
    virtual MeetingConnection Poll() = 0;
    virtual bool FirstParticipant() const = 0;
    virtual bool Send(const std::vector<std::uint8_t>& message) = 0;
    virtual bool HasPendingOutput() const = 0;
    virtual std::optional<std::vector<std::uint8_t>> Receive() = 0;
};

using OpenRoomMeeting = std::function<std::unique_ptr<RoomMeetingChannel>(MeetingPlace)>;

} // namespace dokodemo::gameplay
