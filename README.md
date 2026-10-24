# dokodemo-native

An **in-progress native Windows source port of Doko Demo Issyo Portable** (Japanese v1.01, UCJS10002). Reconstructed C++ and a KSC interpreter run on Windows without a PSP CPU emulator. **The full game is not complete.** Linux and a matching PSP rebuild are unverified.

The current source includes title/new-game/profile entry, room and actor rendering, repeated owned conversations, choices and finite Latin/kana word input. Implemented finite operations include inventory marking, decoration, putaway and confirmed discard; main-menu inventory; empty friend/word notebooks, self-details, the current diary record; and selected Options edit/No/Yes RAM paths. Coverage is bounded by each supported operation, data shape and lifecycle. Original save import, durable state, networking, full text/IME and broad scene progression remain open.

Visual work now takes priority. Toro uses actor-only textured color modulation and soft diffuse shading from authored normals; these are explicit Windows approximations, not proved PSP combiner/light equations. Large room/camera/material defects and readable UI are the next visible priorities.

<!-- source-status:start -->
Native renderer now derives a conservative soft contact decal from posed actor geometry and live carpet support. Contact, depth and synthetic alpha regressions were added; current Windows frames were checked locally. This remains a Windows approximation; the full game is incomplete.
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

Run `dokodemo-native.exe --assets <prepared-native-assets> --startup-media-root <extracted-PSP-root> --native-title-layout characters --native-save-root <local-save-root>`. Follow onboarding with arrows, X (Circle), Z (Cross), text/Enter. F12 captures the live window and a clean native framebuffer; Esc exits. `--keep-scene-open` keeps an existing observation window open; it does not advance scripts or alter game readiness. The ordinary introduction uses live local time and can take five real minutes.

## History and license

[HISTORY.md](HISTORY.md) explains the source-only development history, preserved metadata and excluded inputs. New original project code is [MIT](LICENSE); existing notices in `LICENSES/` and external dependency licenses remain in force. Original game data/trademarks are not relicensed. Publication does not claim complete game, SDK, audio or third-party equivalence.
