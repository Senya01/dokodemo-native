# dokodemo-native

An in-progress native Windows source port of **Doko Demo Issyo Portable** (Japanese v1.01, UCJS10002). The game runs reconstructed C++ and its own KSC game scripting interpreter, without a PSP CPU emulator.

The verified route currently covers title/new-game menus, profile entry, room and actor rendering/animation, introductory dialogue, mode0 inventory browsing, action cancellation and close, and an owned in-memory diary entry followed by goodbye/camera-out. The later main-flow transition is still incomplete. Full gameplay, item actions, original save import, audio mixing and the complete text engine remain unfinished. Linux is not verified. This is not a complete game or a matching PSP rebuild.

This source export retains the full sequence of 104 development commits. See [HISTORY.md](HISTORY.md) for the preserved metadata and deliberately excluded private artifacts.

## Your game data

No ISO, PSP executable, PRX, decoded module, artwork, fonts, models, scripts, sound, save files or research dumps are distributed. Use your own copy. Do not commit extracted or generated data. Local assets go under ignored `assets/`; local inputs under ignored `private/`.

Two diagnostic/generated headers are deliberately excluded: `src/recovered/ksc_dispatch_addresses.h` and `src/audio_catalog.h`. Generate them from your local BOOT and existing audio metadata:

```powershell
python tools/generate-user-headers.py --boot private/BOOT.BIN --audio-manifest private/audio-manifest.json
```

This reads your files locally and does not download game data. The audio metadata must come from your own preparation with the included `tools/audio_prepare.py`. Full first-time extraction/preparation and the offline PRX preparation recipe still require publication packaging; the private research paths are not part of this source export. Without prepared local game inputs the complete runtime cannot yet be reproduced from this candidate alone.

## Build

Windows x64, Visual Studio 2022 C++ tools and its CMake/Ninja integration are required. Python 3 is used for local data tools. The optional codec owner backend expects a separately supplied FFmpeg 7.1.1 Windows shared development package; no third-party DLLs are bundled here.

```powershell
scripts/build-functional.ps1 -OutputDirectory build/functional -PreparedCodecRoot <your-ffmpeg-root> -Test
```

The focused `game-*` tests use small synthetic fixtures unless explicit user game data is selected. No private Git/research history is required to compile the exported C++ after user-generated headers are available. A separate `DOKODEMO_USER_TABLES_DIR` CMake option can point to an ignored directory containing those two headers, with the dispatch header under `recovered/`.

Run the resulting executable with `--assets <your-prepared-native-assets>` and `--startup-media-root <your-extracted-PSP-root>`. The original game files and prepared data remain your local inputs.

## License

New original project code is available under the [MIT License](LICENSE).
Existing component copyright and license notices under `LICENSES/` remain in effect. Third-party dependencies, including FFmpeg, retain their own licenses and distribution requirements.

This license grants no rights to the original game, its data, assets or trademarks. Original game material is not relicensed. The source export does not claim legal clearance or a completed third-party provenance review.
