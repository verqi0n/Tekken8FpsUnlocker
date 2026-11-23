#pragma once

namespace tekken8 {

// Maximum supported refresh at the game monitor's current resolution. A zero
// result means that the asynchronous query has not found a usable mode.
float monitorRefreshLimit() noexcept;

} // namespace tekken8
