# PHA-3935: 7DtZ spec gaps: dead-end kits, era-jump ruins, Hammer/Silver/Hookshot tiers, repair, torch, town hiding

Status (2026-10-03): items 1, 2, 6, 7, 8, 9, 10, 11 and 12 shipped and checked in the game. Items 3, 4 and 5 are in progress.

Gaps found in the 2026-10-03 review against the PHA-3870 spec and M9.

## Shipped

1. **Kits you can place.** Four new placeables (`PLACEABLE_TORCH`, `_STONEWALL`, `_BOMBTRAP`, `_GATE`, added at the end of the enum, so old saves load):
   - Torch: ObjSyokudai's wooden stand with its scrolling flame and a flickering point light. Indestructible.
   - Stone wall: two push blocks, 120 x 60 x 60, 200 HP (a barricade has 100). Raiders drain it like a barricade.
   - Bomb-flower trap: EnBombf's flower and bomb. A raider within 70 lights it, and 12 frames later it blasts for a bomb's damage (DMG_EXPLOSIVE) out to 110. The blast hits enemies only. The bomb grows back in 10 s. Every client plays it, but only the scene's enemy authority deals the damage.
   - Player gate: two leaves of Ingo's ranch gate, 150 HP. It swings open while a player is within 70 in front of or behind it, and its box drops out of the base collision. Raiders have to break it.
2. **Ruins at the seven-year jump.** The first time the owner is an adult (`STORY_RUINS`), every child-era piece becomes `ERA_RUINS`, half of each kit's materials go into the pool, and the child base center is cleared. Ruins stand in the adult era, tinted with moss and sunk at an angle. They have no collision and no behavior, raids ignore them, and packing one up just clears it.
6. **Repair.** `REPAIR_REQUEST` goes to the owner and restores full HP. It costs the kit's materials for the missing share of HP, rounded up, or half that with the Megaton Hammer (the spec's "fast repair"). It is on both the Workbench page and the ImGui Base tab. A damaged piece now packs up into only its HP's share of the kit's materials (rounded down), so packing up is no longer a free repair.
7. **Torch radius.** Raid ring samples and spawn picks within `TORCH_RADIUS` (300) of a torch are skipped.
8. **Navi's C-Up tips.** `ElfMessage_GetCUpText` reports her current hint. The first time she gives it, the story hint plays. After it has been heard, the same text id shows the next of 12 crafting and raid tips instead. The id stays vanilla because Player stores it in an s16.
12. **Warning before every raid.** From 16:30 on a raid day, after the first raid, Navi gives one of three warnings. The first raid keeps its staged line.

9. **Towns on raid nights.** On a raid night, Kakariko, Castle Town at night, the back alley and Lon Lon Ranch send their townsfolk indoors (EnHy, EnDaikuKakariko and EnNiwGirl; never a quest NPC or a guard), with a "has barred its doors until dawn" notice. Once the first raid is over, Kakariko's field gate is boarded on both sides with the path left open. These are town decoration pieces (ids from 0xF000) that are not in BaseState, so they are never packed up, damaged, counted or saved.
10. **More lines.** Gerudo's Fortress gets the Training Ground guard (0x6070 and 0x6072). The castle gets its gate guard (0x7006). Impa (0x708E, DemoIm), Talon (0x2055 and 0x5015) and Anju (0x503D and 0x5047) get their own lines. Every replaced text was checked against the ROM's message table for control codes.
    - Three M9 lines replaced texts that chain to another message (0x07). 0x5066 chained to the clock soldier's graveyard-song hint, so it is dropped. 0x5079 and 0x6019 move to the plain texts they chain to (0x507A and 0x601A), so the vanilla first part plays again.
11. **The owner going offline.** Tonight's record (fought at the base, lost, gamestage) is stored in BaseState counters as `night {day, fought, failed, gamestage}`, keyed by the day. It is saved and synced, so whoever is owner at dawn settles the night the same way. Checked: the record survived a save and reload in the middle of a raid night.

New text placeholder `[[when]]` ("tonight", "tomorrow night", "in 3 days"). The three M9 lines that said "in [[next]]" (which read "in tonight") use it now.

Test hooks: `sevendays_test_raid("tier:bomb|hookshot|hammer|silver")`, `("bp:<recipe>")`, `("age")`, `sevendays_test_repair`, `_repair_cost`, `_open_window`, `_trap_blasts`, `_cup_state`, and `spawnLog` / `eveWarnedDay` in raid state.

## Checked in the game (GPU Chrome, solo, Kokiri Forest)

- Crafted torch x5, stone wall, bomb trap x2 and gate x2 at the workbench (blueprints and tiers through the test hooks), placed them all, and each kit count dropped.
- Gate: Link walked through it (+110 to -69 along its normal). The stone wall stopped him at its face (-30). During a raid, raiders broke one gate and took the other to 46/150.
- Bomb trap: 5 blasts across two raids. Two Stalchildren by the workbench died, and a ReDead went from 8 to 4 HP.
- Torch: 5 spawns on night 3, each 313 to 666 from the nearest torch (none within 300).
- Repair: the gate went from 46 to 150 for 6 Wood and 3 Stone, as the page showed. Salvage: a stone wall at 100/200 packed up into 3 Stone and 1 Wood, with no kit back.
- Ruins: 16 pieces turned into ruins, and the pool gained +27 Wood, +11 Stone, +5 Fiber and +2 Rot, which is half the kits by hand count.
- Tips: after the 0x140 hint, C-Up text shows the tips in rotation ("The next raid comes tonight.").
- Warning: text 0x9716 at 16:37 on day 3 ("We've held off 2 raids so far").
- Towns: Kakariko on raid night 5. Three carpenters went indoors at dusk, the notice showed, and the boards stand at the field gate.
- Lines: 0x507A, 0x2055, 0x5015, 0x503D, 0x5047 and 0x708E shown, plus 0x7006 at the castle, 0x601A in Gerudo Valley and 0x6070/0x6072 at the fortress. All showed the after-raids variants.

## In progress

- Items 3 and 4: Ore and iron, in-place wall upgrades (wood to stone to iron), the iron wall, and the Navi lines in Loot.cpp that promise them.
- Item 5: Hookshot-tier materials on ledges.
