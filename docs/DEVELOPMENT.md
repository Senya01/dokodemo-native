# Development and publication

This public checkout is a normal Git repository with `origin` pointing at Senya01/dokodemo-native. Private evidence/game inputs are not part of its history. Windows is the current target; Linux is unverified.

The default build uses public synthetic tests. Original-byte fixture groups are conditional on `DOKODEMO_PRIVATE_SOURCE_FIXTURES_DIR` (the build script exposes `-PrivateSourceFixturesDirectory`). That directory is a local developer input containing the omitted header basenames; it is not supplied or downloaded. Headers must use this checkout's public source/include paths. Do not commit that package. The omitted groups are inventory exact-script/action/placement/discard, owned/common conversation, greeting, natural-loop, opcode55 exact-loop, Flow5 and dependent menu callback/placement/room fixtures. Other authored synthetic fixture groups remain enabled.

Source synchronization preserves code commits while excluding private raw data across every appended revision. It retains names/messages/dates, maps addresses to the GitHub noreply address and uses new public SHAs. Existing public history is preserved. Private branch parents, raw research, game data and generated tables must never be merged/pushed. The initial source-only snapshot update is validated with an independent composition review, a fresh public build/tests, object/path checks and a redacted secret scan.

User-facing runtime data is external: the prepared native asset tree, extracted PSP tree, generated diagnostic headers and optional FFmpeg development package. The first-time ISO/offline module preparation pipeline remains incomplete; avoid claiming an arbitrary clean clone can immediately play the full game.
