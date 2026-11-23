#pragma once

namespace tekken8 {

// The overlay computes the step in single precision, then accumulates in double.
inline constexpr double gameplayStep = static_cast<double>(1.0f / 60.0f);

class GameplayClock {
public:
    void addFrame(double seconds) noexcept {
        if (seconds > 0.0)
            remaining_ += seconds < 10.0 ? seconds : 10.0;
    }

    bool takeStep() noexcept {
        if (remaining_ < gameplayStep)
            return false;
        remaining_ -= gameplayStep;
        return true;
    }

private:
    double remaining_ = 0.0;
};

}
