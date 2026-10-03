# PHA-3875: 7DtZ M8: tuning from real game nights

Status at export (2026-10-03): blocked

Tune costs, HP, unlock ladder, loot tables, wave curves after real sessions. Spec: parent PHA-3870 (description is the source of truth). Code in soh/soh/SevenDays/ behind its own setting under gSevenDays.Enabled; COOP-M3-TEST-GUIDE.md must keep passing; verify with tools/webtest/ two-profile run.

## Live-game gate (Brandon, 2026-10-02)
Not done until this stage is tested in the live game: run the built web bundle in a real browser and play the stage end to end. Headless tools/webtest/ runs alone do not count, and neither does a compile. Post screenshots or a clip plus what was played as evidence on this issue.
