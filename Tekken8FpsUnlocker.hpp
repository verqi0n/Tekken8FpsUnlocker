#pragma once
#include <cmath>
#include <cstdint>

namespace tekken8 {

// The overlay computes the step in single precision, then accumulates in double.
inline constexpr double gameplayStep = static_cast<double>(1.0f / 60.0f);

class GameplayClock {
public:
    void addFrame(double seconds) noexcept {
        if (std::isfinite(seconds) && seconds > 0.0)
            remaining_ += seconds < 10.0 ? seconds : 10.0;
    }

    bool ready() const noexcept { return remaining_ >= gameplayStep; }

    bool takeStep() noexcept {
        if (remaining_ < gameplayStep)
            return false;
        remaining_ -= gameplayStep;
        return true;
    }

    // Unspent time as a fraction of a tick: the render position between the last two ticks.
    double phase() const noexcept {
        const double value = remaining_ / gameplayStep;
        return value < 1.0 ? value : 1.0;
    }

private:
    double remaining_ = 0.0;
};

struct FrameRateFields {
    std::uint8_t* flags;
    float* fixed;
    float* maximum;
};

class FrameRateOverride {
public:
    void apply(FrameRateFields fields, float target) noexcept {
        if (!fields.flags || !fields.fixed || !fields.maximum || target < 0 || !std::isfinite(target)) {
            restore(fields);
            return;
        }
        if (fields.flags != last_.flags || (*fields.flags & fixedBit))
            wasFixed_ = (*fields.flags & fixedBit) != 0;
        if (fields.fixed != last_.fixed || *fields.fixed != target_)
            nativeFixed_ = *fields.fixed;
        if (fields.maximum != last_.maximum || *fields.maximum != target_)
            nativeMaximum_ = *fields.maximum;
        last_ = fields;
        target_ = target;
        *fields.flags &= static_cast<std::uint8_t>(~fixedBit);
        *fields.fixed = target;
        *fields.maximum = target;
    }

    void restore(FrameRateFields fields) noexcept {
        // Only the current engine and console variable own valid storage.
        if (fields.flags && fields.flags == last_.flags && wasFixed_)
            *fields.flags |= fixedBit;
        if (fields.fixed && fields.fixed == last_.fixed && *fields.fixed == target_)
            *fields.fixed = nativeFixed_;
        if (fields.maximum && fields.maximum == last_.maximum && *fields.maximum == target_)
            *fields.maximum = nativeMaximum_;
        last_ = {};
    }

private:
    static constexpr std::uint8_t fixedBit = 0x40;
    FrameRateFields last_{};
    bool wasFixed_ = false;
    float nativeFixed_ = 0, nativeMaximum_ = 0, target_ = 0;
};

}
