# #3871: 7DtZ M4: materials, crafting window, tunic colors

Status at export (2026-10-03): done

Gathering hooks (Fiber/Stone/Wood/Bone/Rot data table), GATHER packet with sourceKey dedupe on room owner, shared pool, CRAFT_REQUEST/RESULT, ImGui crafting window with Trade tab, consumable recipes, tool/boss-gated unlocks, Navi first-gather lines, per-player tunic color incl. DummyPlayer. Spec: parent #3870 (description is the source of truth). Code in soh/soh/SevenDays/ behind its own setting under gSevenDays.Enabled; COOP-M3-TEST-GUIDE.md must keep passing; verify with tools/webtest/ two-profile run.
