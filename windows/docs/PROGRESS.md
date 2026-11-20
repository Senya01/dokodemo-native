# Windows port progress

The current inventory has **67 basic entries and one exact entry**. The exact entry covers the 319 RGB frames of the opening under the recorded reference inputs. Basic coverage means a working native implementation exercised along a representative path. The inventory can be rendered from the Windows source directory with `python -B tools/render_native_coverage.py build/native-coverage.html`. [Coverage inventory](native-coverage.json)

| Area | Verified scope |
| --- | --- |
| Core play | Native startup, character selection, room activities, ordinary contacts, language lessons, inventory, notebooks, options and diary |
| Character stories | First-day entry for all five, ordinary and extended endings on prepared saves, and re-adoption with preserved vocabulary |
| Persistence | Protected Windows save and resume, natural day-one to day-two transition and another cold load. The original game also loaded a Windows-created first-day slot in an isolated test |
| Travel | All 15 destinations in Toro's ordinary menu reached the destination and returned. The component renderer exercised 54 scene registrations |
| Local meetings | Independent Windows processes exercised discovery, visit cancellation, card and gift exchange, and shiritori on loopback |
| Platform | Native rendering and audio, keyboard input, XInput mapping and recovery, and software suspend and resume checks |

## Acceptance and code map

The KSC badge measures how many active opcode and built-in IDs have a branch in the native room dispatcher. It is regenerated from the same inventory as the tiles. The Windows acceptance badge measures verified requirements in the acceptance matrix. The current total is **40 of 60** requirements verified and 20 open. The baseline recorded 62 of 62 Windows Release tests. The scheduled letter adds a separate 10-route Release check. Both ran without windows or sound. The [acceptance records](native-acceptance.json) identify both runs.

The [interactive map](native-coverage.html) includes the acceptance requirements and code inventory. Download the HTML and open it locally to select cells. The code inventory compares active IDs in the original BOOT v1.01 dispatch tables with explicit branches in the native room handler. A green cell records a branch or ID check in that handler. The file map comes from the Windows CMake source lists. These counts measure their respective inventories.

Run from the `windows` directory to rebuild the map and badges from the checked-in snapshots:

```powershell
python -B tools/render_native_coverage.py docs/native-coverage.html --badge docs/native-progress.svg --code-badge docs/native-code-progress.svg --overview docs/native-code-map.svg
```

With a local copy of the original executable, pass `--boot private/BOOT.BIN` to also show original dispatch target addresses. The published map records ID membership and source references.

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

## Scheduled letters

Event 7 uses the stored return day and completion bit `0x80`. GAME75 copies the sender's actor and names into the protected profile. The letter scene runs the recipient's own callback and stages the corresponding diary descriptor. Saving preserves the completion bit, the descriptor and the formatted text with the previous names.

Regenerate the local game media and build `post-profile-tests` with local-media tests enabled. Run from the `windows` directory with five local protected slot directories in character order.

```powershell
$env:DOKODEMO_TEST_AUDIO = 'null'
python -B tools/test_room_return_letters.py <post-profile-tests.exe> <package> <toro-slot> <robo-slot> <usagi-slot> <inu-slot> <kaeru-slot> --report build/return-letter-check
```

The test prepares day-three checkpoints in temporary slots. It advances the test calendar into day four on cold entry and across midnight in a running room, chooses both letter responses across the two routes per character, and saves through the System menu. A second boot checks that the room resumes without replaying the event. Game data and input slots are supplied locally.

## Open coverage

A continuous playthrough from a new game to the ending remains unverified. Some special branches remain open. Physical controller input, peer compatibility with a PSP and byte-accurate matching outside individually recorded checks remain unverified. The [Windows build guide](../README.md) describes the checks available with locally prepared media.
