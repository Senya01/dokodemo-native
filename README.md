# dokodemo-native

An **in-progress native Windows source port of Doko Demo Issyo Portable** (Japanese v1.01, UCJS10002). Reconstructed C++ and a KSC interpreter run on Windows without a PSP CPU emulator. **The full game is not complete.** Linux and a matching PSP rebuild are unverified.

The current source includes title/new-game/profile entry, room and actor rendering, repeated owned conversations, choices and finite Latin/kana word input. Implemented finite operations include inventory marking, decoration, putaway and confirmed discard; main-menu inventory; empty friend/word notebooks, self-details, the current diary record; and selected Options edit/No/Yes RAM paths. Coverage is bounded by each supported operation, data shape and lifecycle. Original save import, durable state, networking, full text/IME and broad scene progression remain open.

Visual work now takes priority. Toro uses actor-only textured color modulation and soft diffuse shading from authored normals; these are explicit Windows approximations, not proved PSP combiner/light equations. Large room/camera/material defects and readable UI are the next visible priorities.

<!-- source-status:start -->
Ordinary Windows key events and owned translated text input; actual title X reached character selection. Player-entry and room continuation remain unverified; full game incomplete.
<!-- source-status:end -->

Finite selected GAME73 sound output exists. Broader SGX/SAS work currently reaches control-state effects and stops at an unknown Port field. Control-state writes are not DSP/audio completion. See [audio references and limits](docs/AUDIO_REFERENCE.md).

## Your game data

No ISO/CSO, PSP executable, PRX, artwork, fonts, models, original scripts, sound, saves or raw research dumps are distributed. Use your own copy. Keep local game data under ignored `assets/` or `private/`, or outside this checkout.

Two game-derived headers are intentionally external: `audio_catalog.h` and `recovered/ksc_dispatch_addresses.h`. Generate them locally:

```powershell
python tools/generate-user-headers.py --boot private/BOOT.BIN --audio-manifest private/audio-manifest.json
```

The audio manifest comes from your own preparation with `tools/audio_prepare.py`. Full first-time asset/offline-PRX preparation still requires work; this source snapshot does not promise an end-to-end clean import of an arbitrary ISO. Prepared game inputs remain local.

## Build

Windows x64, Visual Studio 2022 C++ tools and its CMake/Ninja integration are required. Python3 supports local data tools. The optional codec backend uses a separately supplied FFmpeg7.1.1 Windows shared development package; vendor binaries are not bundled.

```powershell
scripts/build-functional.ps1 -OutputDirectory build/functional -PreparedCodecRoot <ffmpeg-root> -UserTablesDirectory <generated-header-root> -Test
```

The user-table root contains `audio_catalog.h` and `recovered/ksc_dispatch_addresses.h`; alternatively generate them at the ignored default locations. Public `game-*` tests exercise authored synthetic data. Seventeen original-byte fixture groups/dependent headers are excluded from publication and explicitly omitted by the default actor runner. Their absence is not reported as original-script equivalence. A developer's own local fixture package may be enabled with `-PrivateSourceFixturesDirectory`; it is optional and is never distributed. See [development and publication](docs/DEVELOPMENT.md).

## Run the native game window

Use your own prepared native assets and extracted media directory (not an ISO filename). Example paths below are placeholders:

```powershell
.\build\functional\dokodemo-native.exe --game-window `
  --assets "C:\DokoData\native" `
  --startup-media-root "C:\DokoData\iso" `
  --native-save-root ".\private\user-save" `
  --evidence ".\private\captures"
```

This is normal startup: no `--visual-capture` or automatic fixture confirmations. Follow the title/profile prompts with arrows, X (Circle), Z (Cross), text and Enter. Text must round-trip through CP932; comprehensive IME/prediction support remains incomplete. F12 saves the actual window and native framebuffer plus local diagnostics. Esc cancels an active text entry; outside text entry it closes the window. The close button also uses normal joined shutdown. The presentation preserves 480:272 aspect, prefers integer scaling and letterboxes on resize. Omit `--game-window` for the diagnostic shell.

The developer's local capture launcher is different: `--visual-capture room --keep-scene-open` performs the existing bounded onboarding inputs for a screenshot observation. It is not normal play or a full-game pass, and the launcher/game data is not bundled. The normal window already stays open without that flag. Live calendar/time-dependent waiting is not accelerated by `--game-window`.

Verification is bounded: normal title confirmation and character selection have been observed; a complete ordinary profile-to-room session remains under verification. Capture fixtures and focused synthetic tests are separate evidence, not a full-game pass.

## History and license

[HISTORY.md](HISTORY.md) explains the source-only development history, preserved metadata and excluded inputs. New original project code is [MIT](LICENSE); existing notices in `LICENSES/` and external dependency licenses remain in force. Original game data/trademarks are not relicensed. Publication does not claim complete game, SDK, audio or third-party equivalence.
