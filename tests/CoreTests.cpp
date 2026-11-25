#include "Tekken8FpsUnlocker.hpp"
#include <cstdio>
#include <limits>

namespace {

int failures = 0;

void check(bool condition, const char* description) {
    if (!condition) {
        std::fprintf(stderr, "Failed: %s\n", description);
        ++failures;
    }
}

int drain(tekken8::GameplayClock& clock) {
    int ticks = 0;
    while (clock.takeStep())
        ++ticks;
    return ticks;
}

void testClock() {
    for (int rate : {30, 60, 120, 144, 160, 240}) {
        tekken8::GameplayClock clock;
        int ticks = 0;
        for (int frame = 0; frame < rate * 10; ++frame) {
            clock.addFrame(1.0 / rate);
            ticks += drain(clock);
        }
        check(ticks == static_cast<int>(10.0 / tekken8::gameplayStep), "combat cadence across render rates");
    }
    tekken8::GameplayClock clock;
    clock.addFrame(tekken8::gameplayStep * 0.5);
    clock.addFrame(std::numeric_limits<double>::infinity());
    clock.addFrame(-std::numeric_limits<double>::infinity());
    clock.addFrame(std::numeric_limits<double>::quiet_NaN());
    clock.addFrame(-1);
    check(drain(clock) == 0, "invalid deltas cannot advance combat");
    clock.addFrame(tekken8::gameplayStep * 0.5);
    check(drain(clock) == 1, "fractional time survives invalid deltas");
    clock.addFrame(20);
    check(drain(clock) == static_cast<int>(10.0 / tekken8::gameplayStep), "bounded catch-up");
}

void testFrameRate() {
    std::uint8_t flags = 0x47;
    float fixed = 60, cap = 90;
    tekken8::FrameRateFields fields{&flags, &fixed, &cap};
    tekken8::FrameRateOverride rate;
    rate.apply(fields, 160);
    check(flags == 7 && fixed == 160 && cap == 160, "configured cap and fixed-step flag");
    rate.apply(fields, 144);
    flags |= 0x10;
    cap = 120;
    rate.restore(fields);
    check(flags == 0x57 && fixed == 60 && cap == 120, "restore owned settings and preserve engine changes");

    rate.apply(fields, 0);
    check(flags == 0x17 && fixed == 0 && cap == 0, "zero target removes the render limit");
    rate.apply(fields, 0);
    rate.restore(fields);
    check(flags == 0x57 && fixed == 60 && cap == 120, "restore native settings after uncapped rendering");

    flags = 3;
    rate.apply(fields, 144);
    cap = 45;
    rate.apply(fields, 160);
    rate.apply(fields, -1);
    check(flags == 3 && fixed == 60 && cap == 45, "invalid target restores latest native cap");

    rate.apply(fields, 160);
    std::uint8_t replacementFlags = 1;
    float replacementFixed = 50;
    rate.restore({&replacementFlags, &replacementFixed, &cap});
    check(replacementFlags == 1 && replacementFixed == 50 && cap == 45, "restore only surviving field owners");
    check(flags == 3 && fixed == 160, "previous engine storage stays untouched after replacement");
}

} // namespace

int main() {
    testClock();
    testFrameRate();
    check(!tekken8::isPlayableMap<char>("Boot", "Boot"), "short boot map name");
    check(!tekken8::isPlayableMap<wchar_t>(L"/Game/Maps/Boot", L"Boot"), "qualified wide boot map name");
    check(!tekken8::isPlayableMap<char>("", "Boot"), "missing map name");
    check(tekken8::isPlayableMap<char>("/Game/Boot/Battle", "Boot"), "playable map under a boot directory");
    return failures ? 1 : 0;
}
