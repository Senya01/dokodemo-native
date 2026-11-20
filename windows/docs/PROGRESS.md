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

The badge measures verified requirements in the Windows acceptance matrix. This snapshot records **39 of 60** requirements verified and 21 open, with 62 of 62 tests passing in a local Windows Release run without sound or windows. The [snapshot](native-acceptance.json) records the executable hash, public source checkpoint and test results.

The [interactive map](native-coverage.html) includes the acceptance requirements and code inventory. Download the HTML and open it locally to select cells. The code inventory compares active IDs in the original BOOT v1.01 dispatch tables with explicit branches in the native room handler. A green cell records a branch or ID check in that handler. The file map comes from the Windows CMake source lists. These counts measure their respective inventories.

Run from the `windows` directory to rebuild all three assets from the checked-in snapshots:

```powershell
python -B tools/render_native_coverage.py docs/native-coverage.html --badge docs/native-progress.svg --overview docs/native-code-map.svg
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

## Open coverage

A continuous playthrough from a new game to the ending remains unverified. The scheduled letter event and some special branches are open. Physical controller input, peer compatibility with a PSP and byte-accurate matching outside individually recorded checks remain unverified. The [Windows build guide](../README.md) describes the checks available with locally prepared media.
