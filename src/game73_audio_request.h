#pragma once
#include "recovered/package_storage_native.h"
#include <cstdint>
namespace dokodemo::audio {
enum class Game73QueueCode { Queued, Busy, Closed, InvalidPcm, StaleRequest, NativeFailure };
struct Game73SelectedBankRequest {
    std::uint64_t request_id=0;
    recovered::PackageView bank;
    recovered::PackageViewDescription identity;
    std::int32_t group=-1,entry=-1,volume=0,profile_gain=0,master_left=128,master_right=128;
};
class Game73SelectedSink {
public:
    virtual ~Game73SelectedSink()=default;
    virtual Game73QueueCode TryQueueSelected(Game73SelectedBankRequest&) noexcept=0;
};
} // namespace dokodemo::audio
