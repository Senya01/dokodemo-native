# Windows game

This source tree builds the standalone `dokodemo-game.exe`. It contains the Windows game target, room and roof gameplay, first-day save and resume, and the game-day transition. The root CMake project builds the earlier recovery application and its own tests.

## Build

Install Visual Studio 2022 with Desktop development with C++, a Windows SDK, CMake 3.21 or newer, and Python 3.10 or newer. Run these commands from this directory.

The C++ build needs two generated headers from your own game data. If you already have `audio_catalog.h` and `recovered/ksc_dispatch_addresses.h`, place them under `private/generated-tables`. To generate them from your locally prepared `BOOT.BIN` and audio manifest:

```powershell
python -B tools/generate-user-headers.py --boot private/BOOT.BIN --audio-manifest private/audio-manifest.json --output-dir private/generated-tables
```

The generator requires a fresh output directory. The headers can also live outside the checkout. Pass the containing directory to CMake.

```powershell
$tables = (Resolve-Path private/generated-tables).Path
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DDOKODEMO_USER_TABLES_DIR=$tables"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The six component tests use synthetic inputs and run with `DOKODEMO_TEST_AUDIO=null`.

## Local game data

For local preparation, copy `source-iso.example.json` to `source-iso.json`. Set `relative_path` to the location of your own Japanese v1.01 ISO within this directory. The media packer verifies the image size and SHA-256 before use. For the header generator, `tools/extract_game.py extract --iso-only` can extract BOOT locally, and `tools/audio_prepare.py` can create `assets/native/audio/manifest.json` with a local FFmpeg installation.

```powershell
python -B tools/package_game_media.py --output build/game
python -B tools/package_native_decoder.py --output build/game
./build/game/dokodemo-game.exe
```

The decoder installer also accepts `--wheel <path-to-pinned-wheel>` for offline preparation. Game data, generated headers, decoder binaries and save files belong in local directories ignored by Git.

## Verification scope

The first-day Windows route was verified across two ordinary processes with a saved learned word. A separate run of the original game in PPSSPP loaded the Windows-created slot. The day-two transition, prior diary entry, and second-day re-save and reload passed component testing. The ordinary Windows check at this checkpoint covered same-day reload.
