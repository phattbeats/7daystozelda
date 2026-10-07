# #3872: 7DtZ M5: placeables, save section, boarded-up Kokiri village

Status at export (2026-10-03): done

ActorDB placeables (barricade, spike strip, workbench, chest) with DynaPoly box collision, placement mode, anywhere-base rules (800u, one per era, 24 cap), sevenDays SaveManager section, BASE_DELTA/BASE_STATE + late-join via UPDATE_TEAM_STATE, seeded Kokiri village, new Kokiri lines. Spec: parent #3870 (description is the source of truth). Code in soh/soh/SevenDays/ behind its own setting under gSevenDays.Enabled; COOP-M3-TEST-GUIDE.md must keep passing; verify with tools/webtest/ two-profile run.

## Live-game gate (Brandon, 2026-10-02)
Not done until this stage is tested in the live game: run the built web bundle in a real browser and play the stage end to end. Headless tools/webtest/ runs alone do not count, and neither does a compile. Post screenshots or a clip plus what was played as evidence on this issue.
