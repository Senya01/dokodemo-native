#pragma once
#include <functional>
#include <string>
#include <string_view>

namespace dokodemo::gameplay {

// A processed NJ call can itself return an error; 20260 ignores that status.
// The callback reports whether the current native owner could execute the call.
using RegisterWordReading = std::function<bool(std::string_view reading, std::string_view written)>;

bool FinishWordReading(std::string_view written, std::string& reading,
                       const RegisterWordReading& register_word);

} // namespace dokodemo::gameplay
