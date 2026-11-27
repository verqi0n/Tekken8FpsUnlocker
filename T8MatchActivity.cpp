#include "T8MatchActivity.hpp"
#include "T8GameObject.hpp"

namespace tekken8 {
namespace {

// Polaris-Win64-Shipping, Steam build 26651490. These are game-thread queries.
constexpr unsigned battleGameModeClass = 0x09935400;
constexpr unsigned battleManager = 0x09BDC458;
constexpr unsigned battleRegistry = 0x0959A320;
constexpr unsigned fighterRegistryKey = 0x0959A088;
constexpr unsigned playerRecords = 0x09BDDB18;
constexpr unsigned watchingBattleScene = 0x09BE0A90;
constexpr unsigned lobbySession = 0x09BDD470;

template<class Function>
Function gameFunction(const std::uint8_t* image, unsigned rva) noexcept {
    return reinterpret_cast<Function>(image + rva);
}

bool hasManualFighter(const std::uint8_t* image, const void* registry, unsigned side) noexcept {
    using Lookup = const void* (*)(const void*, const void*);
    const auto* players = gameFunction<Lookup>(image, 0x018E6910)(registry, image + fighterRegistryKey);
    const auto* storage = players ? field<const void*>(players, 0) : nullptr;
    if (!storage)
        return false;
    // The game's indexed getters assume both slots already exist. During loading
    // the vector can still be empty or contain only the first fighter.
    const auto begin = field<std::uintptr_t>(storage, 8);
    const auto end = field<std::uintptr_t>(storage, 0x10);
    if (!begin || end < begin)
        return false;
    return hasManualFighterOnSide(reinterpret_cast<const void* const*>(begin), (end - begin) / sizeof(void*), side);
}

bool battleMenuOpen(const std::uint8_t* image, const void* world, const void* manager) noexcept {
    using Query = bool (*)();
    using WorldQuery = bool (*)(const void*);
    // APolarisBattleGameMode::IsPause reads the pause mask even when UE keeps ticking.
    if (field<std::uint32_t>(manager, 0x1558) || gameFunction<WorldQuery>(image, 0x05057A60)(world))
        return true;
    // IsPhotoMode dereferences the photo system at manager+0x10.
    const bool photoMode = field<const void*>(manager, 0x10) && gameFunction<Query>(image, 0x02E3C3B0)();
    return photoMode || !gameFunction<Query>(image, 0x02E8E340)() ||  // IsDialogClosed
        !gameFunction<Query>(image, 0x02E8E4F0)() ||  // IsHelpDialogClosed
        gameFunction<Query>(image, 0x02E03D00)();     // IsOpenMatchDialog
}

}

bool hasManualFighterOnSide(const void* const* entries, std::size_t count, unsigned side) noexcept {
    if (!entries || side > 1)
        return false;
    // Tekken compares each fighter's assigned side and control type (0x58CCDCF).
    // A live entry (0x191B4C0) embeds the fighter at +0x90 (0x191AD40).
    for (std::size_t i = 0; i < count && i < 2; ++i) {
        const auto* entry = entries[i];
        if (entry && field<std::uint8_t>(entry, 0) &&
            field<std::uint8_t>(entry, 0x90 + 0x3AC4) == side && field<int>(entry, 0x90 + 0x3BB8) == 0)
            return true;
    }
    return false;
}

bool FightActivity::allowsUnlock() const noexcept {
    if (!battleMain || !manualFighter || menuOpen || replay || spectating || side < 0 || side > 1 || sideRole != 1)
        return false;
    switch (matchMode) {
    case 4: // Network practice, quick, ranked, lounge, player room, special match
        return playerKind == 4 && ((networkMode >= 0 && networkMode <= 4) || networkMode == 6);
    case 6:
    case 12:
        // Versus setup (0x5D687A0 / 0x5CFFB70) assigns kind 2 to both CPUs.
        // The profile still occupies a side when the player chooses CPU vs CPU.
        return playerKind == 6 || playerKind == 12;
    case 16: // A player can fight a ghost on the other side.
        return playerKind == 16 && !ghostFighter;
    // The single-player and practice variants that initialize a player side in
    // the battle setup switch at 0x58C4C8A. Other descriptors include the demo,
    // character customization, replay and ghost-vs-ghost scenes.
    case 1:
    case 2:
    case 5:
    case 8:
    case 9:
    case 11:
    case 18:
    case 19:
        return true;
    default:
        return false;
    }
}

bool isLocalFight(const std::uint8_t* image, const void* world) noexcept {
    if (!image || !world ||
        !hasClass(field<const void*>(world, 0x150), field<const void*>(image, battleGameModeClass)))
        return false;
    const auto* manager = field<const void*>(image, battleManager);
    const auto* registry = field<const void*>(image, battleRegistry);
    // IsBattleMain checks the registered battle state, but assumes its registry exists.
    using Query = bool (*)();
    if (!manager || !registry || !field<const void*>(registry, 0x10) ||
        !gameFunction<Query>(image, 0x02EB1D50)())
        return false;

    using Descriptor = const void* (*)(const void*);
    const auto* match = gameFunction<Descriptor>(image, 0x05A1EFE0)(manager);
    const auto* records = field<const void*>(image, playerRecords);
    if (!match || !records)
        return false;

    FightActivity state;
    state.battleMain = true;
    state.matchMode = field<int>(match, 0);
    if (state.matchMode == 4) {
        using NetworkSettings = const int* (*)();
        state.networkMode = *gameFunction<NetworkSettings>(image, 0x058BDDC0)();
    }

    // UpdateGuideSprite gets the player's side from this descriptor (0x5EB6D30).
    // Resolve that side against the *current fight*. The spectator path excludes
    // the persistent local record even if the user is a member of the room.
    const auto side = field<std::uint8_t>(match, 4);
    state.side = side;
    if (side <= 1) {
        // The ghost battle setup at 0x5D13EC0 assigns ghost control per side.
        if (state.matchMode == 16)
            state.ghostFighter = field<std::uint8_t>(match, 0x30 + 0x84 * side + 2) != 0;
        using FighterRecord = const void* (*)(const void*, std::uint8_t);
        const auto* record = gameFunction<FighterRecord>(image, 0x05C37E20)(records, side);
        if (record) {
            state.sideRole = field<int>(record, 8);
            state.playerKind = field<int>(record, 0xC);
            state.manualFighter = state.sideRole == 1 && hasManualFighter(image, registry, side);
        }
    }
    const auto* lobby = field<const void*>(image, lobbySession);
    state.spectating = field<const void*>(image, watchingBattleScene) ||
        (lobby && field<std::uint8_t>(lobby, 0xF0)); // LobbyIsInWatchingBattleScene / LobbyIsWatching
    using IsReplayMode = void (*)(bool*, bool*);
    bool preCalculated = false;
    gameFunction<IsReplayMode>(image, 0x02EB2900)(&state.replay, &preCalculated);
    state.menuOpen = battleMenuOpen(image, world, manager);
    return state.allowsUnlock();
}

}
