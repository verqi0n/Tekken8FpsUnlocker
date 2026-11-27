# Tekken 8 FPS Unlocker

A Windows x64 ASI plugin that lets you set Tekken 8's maximum render frame rate. Combat runs at 60 ticks per second. Between ticks, the plugin interpolates the battle camera and both fighters' movement and skeletal poses.

The plugin supports **Tekken 8 Steam build 26651490**. It checks the game executable before enabling its changes. Game updates may require a new plugin build.

## Install

1. Sign in to GitHub, open the latest successful build on `main` and download `Tekken8FpsUnlocker-win64` under **Artifacts**. Extract both files. [Builds](https://github.com/verqi0n/Tekken8FpsUnlocker/actions/workflows/build.yml?query=branch%3Amain+is%3Asuccess)
2. Close Tekken 8. In Steam, open the game's installation folder through **Manage → Browse local files**. If you need an ASI loader, place the x64 Ultimate ASI Loader's `dinput8.dll` in `Polaris\Binaries\Win64`, alongside `Polaris-Win64-Shipping.exe`. [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader#installation)
3. Copy `Tekken8FpsUnlocker.asi` and `Tekken8FpsUnlocker.ini` into `Polaris\Binaries\Win64\plugins`. Create the `plugins` folder if needed, then start the game.

To uninstall, close the game and remove both files from the `plugins` folder.

## Verify the download

Every build from `main` carries a signed GitHub artifact attestation. It proves that this repository's workflow built the exact file from a specific commit. With the [GitHub CLI](https://cli.github.com) installed, run the following in the folder where you extracted the files.

```powershell
gh attestation verify Tekken8FpsUnlocker.asi --repo verqi0n/Tekken8FpsUnlocker
```

The check succeeds only for an unmodified ASI built by this repository. The output names the commit and workflow run that produced it. You can also check the INI the same way, but only before you edit it. The repository's [Attestations](https://github.com/verqi0n/Tekken8FpsUnlocker/attestations) page lists every signed build.

## Configure the FPS limit

Edit the `[FPS]` section of `Tekken8FpsUnlocker.ini` next to the ASI.

```ini
[FPS]
TargetFPS=300
```

Set `TargetFPS` to a positive whole number for the maximum render FPS, or to `0` for uncapped rendering. If the file or setting is missing or the value is invalid, the plugin uses 300 FPS.

The plugin applies `TargetFPS` while you control a fighter in a match or in practice, including character intros, round calls and round ends. The main menu, character selection, loading screens, the winner pose, results screens, pause menus, replays and spectated matches use the game's own frame limit. Resuming play reapplies `TargetFPS` automatically.

Restart the game after editing the INI. Actual frame rates depend on rendering performance, VSync and external frame limiters.

## Build and test

Install Visual Studio 2022 or later with **Desktop development with C++** and the Windows SDK. Put CMake 3.24 or later on your PATH, using a CMake version that supports your installed Visual Studio release. The first configure downloads a pinned copy of MinHook and verifies its checksum. The build links MinHook statically.

From the repository root, run the following in PowerShell or Command Prompt.

```powershell
.\build.bat
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

The script writes the Release build to `Tekken8FpsUnlocker.asi` in the repository root, alongside the INI. GitHub Actions builds with Visual Studio 2026, runs the core tests, then attests and packages both files.
