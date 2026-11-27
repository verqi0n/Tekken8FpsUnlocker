#include "Tekken8FpsUnlocker.hpp"
#include "T8MatchActivity.hpp"
#include <cstdio>
#include <initializer_list>
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

void testClockPhase() {
    // At 144 FPS a tick spans 2.4 frames. The rendered position must advance by
    // exactly one frame's worth every frame, including frames that run a tick.
    for (int rate : {75, 144, 165, 240, 300, 500}) {
        tekken8::GameplayClock clock;
        const double expected = 1.0 / rate / tekken8::gameplayStep;
        double previous = 0.0;
        int ticks = 0;
        bool steady = true;
        for (int frame = 0; frame < rate * 5; ++frame) {
            clock.addFrame(1.0 / rate);
            check(clock.ready() == (clock.phase() >= 1.0), "ready matches a full tick of unspent time");
            const int stepped = drain(clock);
            ticks += stepped;
            const double position = ticks + clock.phase();
            if (frame > 0 && std::fabs(position - previous - expected) > 1e-9)
                steady = false;
            previous = position;
            check(clock.phase() >= 0.0 && clock.phase() < 1.0, "phase stays within a tick after stepping");
        }
        check(steady, "render position advances evenly across tick boundaries");
    }
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

void testFighterSide() {
    alignas(int) std::uint8_t human[0x90 + 0x3BBC]{};
    alignas(int) std::uint8_t cpu[sizeof human]{};
    human[0] = cpu[0] = 1;
    human[0x90 + 0x3AC4] = 1; // P2 occupies the first storage slot.
    cpu[0x90 + 0x3BB8] = 1;
    const void* fighters[]{human, cpu};
    check(tekken8::hasManualFighterOnSide(fighters, 2, 1), "P2 human in reordered roster");
    check(!tekken8::hasManualFighterOnSide(fighters, 2, 0), "the human is on the other side");
    check(!tekken8::hasManualFighterOnSide(fighters, 0, 1), "empty roster during scene change");
    fighters[0] = nullptr;
    fighters[1] = human;
    check(!tekken8::hasManualFighterOnSide(fighters, 1, 1), "ignore slots beyond the current roster");
    human[0] = 0;
    check(!tekken8::hasManualFighterOnSide(fighters, 2, 1), "ignore an inactive fighter from the previous match");
}

void testFightPhases() {
    for (int phase : {1, 3, 4, 5, 21, 32})
        check(tekken8::isFightPhase(phase), "intro, round call, fight or round end");
    for (int phase : {-1, 0, 2, 8, 35, 37, 38})
        check(!tekken8::isFightPhase(phase), "loading, winner pose, results, menus or an unknown phase");
}

void testMatchActivity() {
    tekken8::FightActivity fight;
    check(!fight.allowsUnlock(), "no current fight");
    fight.matchMode = 1; // Practice
    fight.side = 0;
    fight.sideRole = 1;
    fight.playerKind = 1;
    fight.manualFighter = true;
    check(!fight.allowsUnlock(), "a player record can exist in menus and during loading");
    fight.battlePhase = 21;
    check(fight.allowsUnlock(), "local practice P1");
    fight.side = 1;
    check(fight.allowsUnlock(), "local practice P2");
    fight.manualFighter = false;
    check(!fight.allowsUnlock(), "local profile present while the fighter is absent or under automatic control");
    fight.manualFighter = true;
    fight.menuOpen = true;
    check(!fight.allowsUnlock(), "opening the practice menu in the same world");
    fight.menuOpen = false;
    fight.replay = true;
    check(!fight.allowsUnlock(), "replay even with the original player's record or during takeover");
    fight.replay = false;

    fight.matchMode = 4;
    fight.playerKind = 4;
    for (int subtype : {0, 1, 2, 3, 4, 6}) {
        fight.networkMode = subtype;
        check(fight.allowsUnlock(), "local player in a network battle");
    }
    fight.networkMode = 4; // Player Match
    fight.playerKind = 2;
    check(!fight.allowsUnlock(), "a non-network player record during a room transition");
    fight.playerKind = 4;
    fight.spectating = true;
    check(!fight.allowsUnlock(), "watching in the room with a persistent local record");
    fight.spectating = false;
    for (int role : {-1, 0, 3, 4}) {
        fight.sideRole = role;
        check(!fight.allowsUnlock(), "unassigned, placeholder or another player's side");
    }
    fight.sideRole = 1;
    check(fight.allowsUnlock(), "local turn in the same room");
    fight.spectating = true;
    check(!fight.allowsUnlock(), "back to watching after a local turn");
    fight.spectating = false;
    for (int subtype : {-1, 5, 7, 100}) {
        fight.networkMode = subtype;
        check(!fight.allowsUnlock(), "unrecognized or ghost network battle");
    }
    for (int mode : {-1, 0, 3, 7, 14, 20, 21, 99}) {
        fight.matchMode = mode;
        check(!fight.allowsUnlock(), "missing, unknown or ghost-vs-ghost match");
    }
    for (int mode : {6, 12}) {
        fight.matchMode = mode;
        fight.playerKind = 2;
        check(!fight.allowsUnlock(), "CPU versus CPU with a persistent local profile");
        fight.playerKind = mode;
        check(fight.allowsUnlock(), "human versus CPU or another local player");
    }
    fight.playerKind = 4;
    fight.networkMode = 6;
    check(!fight.allowsUnlock(), "special match is still configuring its temporary offline descriptor");
    fight.matchMode = 4;
    check(fight.allowsUnlock(), "online special match has entered network play");
    fight.networkMode = 7;
    check(!fight.allowsUnlock(), "online ghost match");
    fight.matchMode = 16;
    fight.playerKind = 16;
    check(fight.allowsUnlock(), "a local human fighting a ghost opponent");
    fight.ghostFighter = true;
    check(!fight.allowsUnlock(), "own side controlled by a ghost");
    fight.matchMode = 1;
    fight.side = 0xff;
    check(!fight.allowsUnlock(), "invalid local side");
}

void testFightFrameRateTransitions() {
    std::uint8_t flags = 0x40;
    float fixed = 60, cap = 60;
    tekken8::FrameRateFields fields{&flags, &fixed, &cap};
    tekken8::FrameRateOverride rate;
    tekken8::FightActivity fight;
    fight.matchMode = 1;
    fight.side = 0;
    fight.sideRole = 1;
    fight.playerKind = 1;
    fight.manualFighter = true;
    const auto update = [&] {
        if (fight.allowsUnlock())
            rate.apply(fields, 0);
        else
            rate.restore(fields);
    };

    update();
    check(flags == 0x40 && fixed == 60 && cap == 60, "menu keeps the native frame rate");
    fight.battlePhase = 0;
    update();
    check(flags == 0x40 && fixed == 60 && cap == 60, "the battle is still starting");
    for (int phase : {1, 3, 4, 5, 3, 4, 5}) {
        fight.battlePhase = phase;
        update();
        check(flags == 0 && fixed == 0 && cap == 0, "intros, round calls, rounds and round ends stay uncapped");
    }
    fight.battlePhase = 8;
    update();
    check(flags == 0x40 && fixed == 60 && cap == 60, "the winner pose restores the native frame rate");
    fight.battlePhase = 3;
    fight.menuOpen = true;
    update();
    check(flags == 0x40 && fixed == 60 && cap == 60, "pausing restores the native frame rate");
    cap = 90; // The game changes its own cap while paused.
    fight.menuOpen = false;
    update();
    check(flags == 0 && fixed == 0 && cap == 0, "resuming combat reapplies the configured rate");
    fight.battlePhase = 38;
    update();
    check(flags == 0x40 && fixed == 60 && cap == 90, "leaving combat restores the latest native cap");
}

} // namespace

int main() {
    testClock();
    testClockPhase();
    testFrameRate();
    testFighterSide();
    testFightPhases();
    testMatchActivity();
    testFightFrameRateTransitions();
    return failures ? 1 : 0;
}
