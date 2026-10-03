# PHA-3873: 7DtZ M6: raids on the base

Status at export (2026-10-03): done

Gamestage budget, ring spawns + workbench routing + stuck check, barricade damage, raid clock for frozen outdoor scenes and prologue scripted nights, raids on empty base, losing-a-night penalty, Navi staged raid lines. First playable: Kokiri village holds its first raid after Gohma. Spec: parent PHA-3870 (description is the source of truth). Code in soh/soh/SevenDays/ behind its own setting under gSevenDays.Enabled; COOP-M3-TEST-GUIDE.md must keep passing; verify with tools/webtest/ two-profile run.

## Live-game gate (Brandon, 2026-10-02)
Not done until this stage is tested in the live game: run the built web bundle in a real browser and play the stage end to end. Headless tools/webtest/ runs alone do not count, and neither does a compile. Post screenshots or a clip plus what was played as evidence on this issue.
