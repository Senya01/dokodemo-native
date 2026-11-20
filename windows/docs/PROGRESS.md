# Windows port progress

The current inventory has **67 basic entries and one exact entry**. The exact entry covers the 319 RGB frames of the opening under the recorded reference inputs. Basic coverage means a working native implementation exercised along a representative path. The inventory can be rendered from the Windows source directory with `python -B tools/render_native_coverage.py build/native-coverage.html`. [Coverage inventory](native-coverage.json)

| Area | Verified scope |
| --- | --- |
| Core play | Native startup, character selection, room activities, ordinary contacts, language lessons, inventory, notebooks, options and diary |
| Character stories | First-day entry for all five, Toro's linked ordinary and extended endings, and focused endings for the other Pokepi, and re-adoption with preserved vocabulary |
| Persistence | Protected Windows save and resume, natural day-one to day-two transition and another cold load. The original game also loaded a Windows-created first-day slot in an isolated test |
| Travel | All 15 destinations in Toro's ordinary menu reached the destination and returned. The component renderer exercised 54 scene registrations |
| Local meetings | Independent Windows processes exercised discovery, visit cancellation, card and gift exchange, and shiritori on loopback |
| Platform | Native rendering and audio, keyboard input, XInput mapping and recovery, and software suspend and resume checks |

## Acceptance and code map

The KSC badge measures how many active opcode and built-in IDs have a branch in the native room dispatcher. It is regenerated from the same inventory as the tiles. The Windows acceptance badge measures verified requirements in the acceptance matrix. The current total is **50 of 60** requirements verified and 10 open. The baseline recorded 62 of 62 Windows Release tests. Separate checks cover startup to Toro's room, Robo's lesson and cold load, the 10 scheduled-letter routes, the new-adoption route, the 15 outing routes, card and gift exchange with cold loads for both players, multiplayer shiritori, and protected slot recovery. All ran without windows or sound. The [acceptance records](native-acceptance.json) identify the runs.

The [interactive map](native-coverage.html) includes the acceptance requirements and code inventory. Download the HTML and open it locally to select cells. The code inventory compares active IDs in the original BOOT v1.01 dispatch tables with explicit branches in the native room handler. A green cell records a branch or ID check in that handler. The file map comes from the Windows CMake source lists. These counts measure their respective inventories.

Run from the `windows` directory to rebuild the map and badges from the checked-in snapshots:

```powershell
python -B tools/render_native_coverage.py docs/native-coverage.html --badge docs/native-progress.svg --code-badge docs/native-code-progress.svg --overview docs/native-code-map.svg
```

With a local copy of the original executable, pass `--boot private/BOOT.BIN` to also show original dispatch target addresses. The published map records ID membership and source references.

## Startup to Toro's room

Run `cmake --build <build> --config Release --target dokodemo-game` and `ctest --test-dir <build> -C Release -R '^game-startup-to-room$' --output-on-failure`. This test is registered when the local media directory also contains the prepared decoder. It copies the newly built executable to an isolated installation with links to the local media, sends input to the title and room controllers, and renders only to memory. The game's worker opens all nine modules, then the verifier checks the first visible RGB frame. The route enters all seven profile fields, accepts Toro, closes the gift list and verifies that his first independent room activity finishes and schedules its pause. The room clock is held at midday inside the test.

## Ordinary conversation roster

The test selects each original fallback entry with a fresh controlled state. It picks the first answer and cancels word entry if offered. Each run verifies that the actual selector chose the expected entry before checking for a room return. A return after a retry means that the script declined the selected topic and the dispatcher chose another.

| Poképi | Entries | Direct returns | Returns after retry |
| --- | ---: | ---: | ---: |
| Toro | 1,057 | 1,052 | 5 |
| Robo | 512 | 503 | 9 |
| Usagi | 490 | 490 | 0 |
| Inu | 512 | 511 | 1 |
| Kaeru | 547 | 538 | 9 |
| **Total** | **3,118** | **3,094** | **24** |

## Conversation, word memo and protected save

Run `cmake --build <build> --config Release --target post-profile-tests` and `ctest --test-dir <build> -C Release -R '^game-contact-to-save$' --output-on-failure` with local game media configured. In a temporary installation, the first headless process starts the first day with Robo and takes the controlled route to his ordinary lesson. It accepts a new word through the game editor, checks its spelling and reading in the memo, closes the notebook and saves from the System menu. A new process cold-loads that slot, returns to room activity and checks both strings in the live memo. The runner verifies that all six slot files remain byte-identical after loading.

## Scheduled letters

Event 7 uses the stored return day and completion bit `0x80`. GAME75 copies the sender's actor and names into the protected profile. The letter scene runs the recipient's own callback and stages the corresponding diary descriptor. Saving preserves the completion bit, the descriptor and the formatted text with the previous names.

Regenerate the local game media and build `post-profile-tests` with local-media tests enabled. Run from the `windows` directory with five local protected slot directories in character order.

```powershell
$env:DOKODEMO_TEST_AUDIO = 'null'
python -B tools/test_room_return_letters.py <post-profile-tests.exe> <package> <toro-slot> <robo-slot> <usagi-slot> <inu-slot> <kaeru-slot> --report build/return-letter-check
```

The test prepares day-three checkpoints in temporary slots. It advances the test calendar into day four on cold entry and across midnight in a running room, chooses both letter responses across the two routes per character, and saves through the System menu. A second boot checks that the room resumes without replaying the event. Game data and input slots are supplied locally.

## New adoption after departure

The headless test opens the empty room and checks the previous Pokepi's word memo. It returns to the title, checks the two cancellation paths, adopts Robo, and saves through the System menu. An independent cold loader opens both the inherited word and the friend card. The local Toro donor must be a completed stay with a lesson in row 163. The test adds a controlled friend card to a temporary copy.

Configure the Windows CMake build with local media and `-DOKODEMO_GAME_DEPARTED_TEST_SLOT=<UCJS10002000 directory>`, build `post-profile-tests`, then run `ctest --test-dir <build> -C Release -R '^game-readoption$' --output-on-failure`. CTest sets the null audio mode and the runner creates a temporary installation next to the prepared package.

## Outings and return to the room

The automated route opens the normal outing menu, enters and draws each of Toro's 15 ordinary destinations, starts a new activity there, and returns through the destination menu. It checks for another activity at home with a later serial. To exercise each destination, the test opens its eligibility flag in memory and uses a suitable learned-word classification where needed. The donor slot stays unchanged.

Configure the Windows CMake build with local media and `-DOKODEMO_GAME_OUTING_TEST_SLOT=<UCJS10002000 directory>` pointing to a Toro slot with a lesson in row 163. Build `post-profile-tests`, then run `ctest --test-dir <build> -C Release -R '^game-out-and-back$' --output-on-failure`. CTest sets null audio. The runner uses temporary installs beside the package and sends game frames to memory.

## Peer card exchange

Configure the Windows CMake build with local media and `-DOKODEMO_GAME_MEETING_TEST_SLOT=<UCJS10002000 directory>` pointing to a Toro slot with an empty friend notebook. Build `post-profile-tests` and run `ctest --test-dir <build> -C Release -R '^game-peer-card$' --output-on-failure`.

The runner copies the input into separate installations and exchanges two cards over loopback. It also cancels a separate wait without a partner. Both players save through the System menu, exit, and are relaunched as separate headless processes. Each opens the peer card from disk in the live notebook. The runner compares all 30 card fields across the two slots after applying the receive path's own-card flag change. All six files of each slot are unchanged by the cold load.

## Peer gift exchange

With the same local fixture setting, build `post-profile-tests` and run `ctest --test-dir <build> -C Release -R '^game-peer-gift$' --output-on-failure`. The players exchange the first of two gifts and save through the System menu. The test compares the other 169 inventory rows with their original state. It also checks waiting without a partner and cancelling after both peers have prepared the exchange. In the paired cancellation case, the test holds the final readiness message at the channel boundary and one player cancels with Cross. Both inventories stay unchanged and both players return to ordinary room activity.

A fresh process opens each saved bag. The live gift rows, complete inventory hashes and all six slot files agree with the recorded save in every case. The network runs on loopback. Cancellation is exercised before either peer commits.

## Shiritori

With the same local fixture setting, build `post-profile-tests` and run `ctest --test-dir <build> -C Release -R '^game-peer-shiritori$' --output-on-failure`. Both players use the ordinary friends menu and the same venue. The loopback match has a winner and loser, returns control to both rooms and saves usage marks on the exact local words that were played. The no-partner cancellation returns control without marking any word.

Three additional runs use a test peer behind the meeting-channel interface. After one legal local turn it replies with a wrong starting kana, a duplicate whose first kana matches, or a word ending in ん. Each case confirms the bad move was delivered and never added to the word history, the room resumes, and the saved word usage marks remain unchanged. The component suite exercises the three-word condition for the meeting offer.

## Linked meetings on the same slots

Run `ctest --test-dir <build> -C Release -R '^game-seam-links$' --output-on-failure` with the same `DOKODEMO_GAME_MEETING_TEST_SLOT` and `post-profile-tests` build. One pair of temporary slots passes through four visits on loopback: exchanging cards, cancelling a prepared gift exchange, exchanging gifts, and playing shiritori. Both players save from the System menu after each visit. Subsequent visits start from the saved slots, with no new fixture injection. The test compares cards, gift rows and learned words across those visits.

Two final processes open their saved friend cards, bags and the three learned rows in their respective notebooks. The complete slot files stay unchanged across the cold reads. The cancellation uses the controlled pre-commit readiness boundary described above.

## Protected slot recovery

Configure the local-media build with `-DOKODEMO_GAME_RECOVERY_TEST_SLOT=<UCJS10002000 directory>`, build `retail-slot-recovery-tests` in Release and run `ctest --test-dir <build> -C Release -R '^game-save-recovery$' --output-on-failure`. The runner derives two distinct generations from the local protected fixture and works exclusively in temporary copies.

The matrix damages, removes and truncates a staged copy of each of the six files. Invalid DATA.BIN or PARAM.SFO prevents loading and replacement. Preview damage allows gameplay to load, and the next successful save rewrites the previews. The matrix also executes 11 injected exceptions, 11 process exits at the same transaction boundaries, and six real Windows sharing conflicts. Fresh processes check the selected generation and retained file bytes. A deferred rollback keeps the backup available for recovery and reports the original write error. Abandoned stages and invalid backups are checked independently. The interruption model is Windows process exit.

## Toro's linked stay and endings

With local media configured, build `post-profile-tests` in Release and run `ctest --test-dir <build> -C Release -R '^game-toro-ending$' --output-on-failure`. The test creates a new profile at the title and saves the first day through System. Each following date uses the prior day's protected save. Ordinary dates resume control, run an activity and write the next checkpoint. The day-13 scene exercises both actor records; the day-14 save branches into departure or the 24-day extension. Each final branch returns to the title and **Continue** opens the departed room from a cold load.

The test controls the game clock and scripted choices. The extended branch prepares the qualifying friendship counters, dictionary and diary at the day-13 fork; it then advances through the regular extended days and events 6, 4 and 5 in the game loop. This connects Toro's main stay and both endings on Release with local media.

## Open coverage

Continuous routes across all characters and optional branches remain unverified. Some special branches remain open. Physical controller input, peer compatibility with a PSP and byte-accurate matching outside individually recorded checks remain unverified. The [Windows build guide](../README.md) describes the checks available with locally prepared media.
