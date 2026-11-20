#pragma once

#include "gameplay/room_meeting_channel.h"

namespace dokodemo::platform {

// Local-subnet discovery on the operating system's default multicast interface.
// A test can select its own port and constrain traffic to the loopback interface.
gameplay::OpenRoomMeeting MakeLanMeetingOpener(std::uint16_t port = 41749, bool loopback = false);

} // namespace dokodemo::platform
