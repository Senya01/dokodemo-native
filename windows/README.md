# Windows game

This source tree builds the standalone native Windows game, `dokodemo-game.exe`. The [progress report](docs/PROGRESS.md) covers all five Poképi, save and resume, outings, conversations, and Windows peer meetings. The root CMake project builds the earlier recovery application.

The room KSC component tests use the local-table projection generated from your own media. Regenerate older prepared packages to add the new projection.

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

The default CTest suite covers synthetic components, controller mapping and the null audio queue. CTest sets `DOKODEMO_TEST_AUDIO=null` for each test.

## Local game data

For local preparation, copy `source-iso.example.json` to `source-iso.json`. Set `relative_path` to the location of your own Japanese v1.01 ISO within this directory. The media packer verifies the image size and SHA-256 before use. For the header generator, `tools/extract_game.py extract --iso-only` can extract BOOT locally, and `tools/audio_prepare.py` can create `assets/native/audio/manifest.json` with a local FFmpeg installation.

```powershell
python -B tools/package_game_media.py --output build/game
python -B tools/package_native_decoder.py --output build/game
./build/game/dokodemo-game.exe
```

The decoder installer also accepts `--wheel <path-to-pinned-wheel>` for offline preparation. Game data, generated headers, decoder binaries and save files belong in local directories ignored by Git.

## Optional local-media tests

Prepare the game media into `build/game` using the command above. Point CMake at the prepared package and run the tests without opening a game window.

```powershell
$env:DOKODEMO_TEST_AUDIO = 'null'
$media = (Resolve-Path build/game).Path
cmake -S . -B build "-DDOKODEMO_GAME_TEST_MEDIA_DIR=$media"
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The conversation roster test covers all 3,118 entries under controlled inputs. The room tests draw to memory. Keep save files and any local test fixtures in ignored directories.

To check the Windows peer transport on loopback, run the following command with a fresh output directory and `DOKODEMO_TEST_AUDIO=null` already set.

```powershell
python -B tools/test_room_meetings.py channel build/Release/room-meeting-channel-tests.exe build/peer-test
```
