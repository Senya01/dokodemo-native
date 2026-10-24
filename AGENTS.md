# Development

Build a readable functional native Windows game. Prioritize visible ordinary-game defects: whole Toro/materials/lighting, then major room/camera/visibility problems, readable UI, obvious poses/animation, then smaller details. Deep audio/SDK/matching work is lower priority unless it directly blocks the visible result. Validate visible changes with actual current-build before/after frames; label Windows approximations and preserve failures.

Keep original game files, generated game tables, exact-source KSC fixture headers, saves, build outputs and research captures local/ignored. Never add private Git history or copy excluded data to make a build pass. The default public tests are synthetic. Optional local private fixture inputs must not be committed or counted as public test coverage.

Use `scripts/build-functional.ps1` with external user-generated tables and the required SDK/dependencies. Run meaningful tests for the changed behavior. For renderer changes inspect the actual frame. Full PSP/source closure is not a prerequisite for a finite functional fix.

Public commits use `Toro Rebuilder <25263912+Senya01@users.noreply.github.com>`, unsigned, without Signed-off-by. Preserve third-party notices and the existing MIT license. Do not change global Git configuration. The user authorized normal publication to `Senya01/dokodemo-native` after relevant checks; use ordinary fast-forward pushes, never force-push or push the private development branch. Exclude all original data and secrets before publication.

During the staged workspace transition, private development and evidence remain separate. Publish only the reviewed source projection into this checkout; do not merge private parents. Once development moves here, assets, original-byte fixtures and evidence remain external/ignored. Do not alter another active checkout or interrupt another owner's build/render session.
