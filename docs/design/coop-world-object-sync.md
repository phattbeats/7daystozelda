# Co-op world-object sync (PHA-4044)

Every client runs its own copy of every actor. What a partner sees depends on whether the
object has a flag (switch, chest, collectible, Gold Skulltula), which Anchor syncs, and on
whether the partner's copy looks at that flag again after it spawns.

Rules:
- **duplicate**: each player has their own copy and uses it on their own. Fine when
  everyone can see and use it.
- **share outcome**: one player breaks or opens it, and it is broken or open for everyone in
  the room.
- **mirror**: one client drives it and the others follow a position stream. This applies to
  enemies, cuccos and push blocks, not to anything here.

"Before" means main at 5209aac (PHA-4042); "after" means PHA-4044.

| Actor class | Rule | What the partner saw before | Now |
|---|---|---|---|
| Pots (Obj_Tsubo), small/large crates (Obj_Kibako/2), grass and bushes (En_Kusa), small rocks (En_Ishi) | duplicate | Their copy is still whole and they can break it themselves; each copy rolls its own drop | Same, by design. 7DtZ materials from them go to the shared pool through the owner, deduped per pot for 10 minutes (SevenDays/Loot.cpp). |
| Random drops: hearts, ammo, magic (En_Item00, no flag) | duplicate, personal | Each player has their own drop, and every pickup was **also** given to the partner (GIVE_ITEM), so a pot or enemy paid the partner twice | **Fixed**: a pickup stays with the player who grabbed it (Packets/WorldObject.cpp) |
| Random rupee drops | duplicate | Each player's own drop pays the shared wallet | Unchanged. The wallet is shared on purpose, so two players picking up their own copies get it twice, just as two players farming the same grass would. |
| Flagged collectibles: heart pieces, freestanding rupees, small keys, Item_B_Heart | share outcome | First pickup sets the flag and the partner's copy vanishes | Unchanged (already worked) |
| Chests (En_Box) | share outcome | Treasure flag syncs and the partner's chest swings open in place (EnBox_WaitOpen polls the flag) | Unchanged (already worked) |
| Gold Skulltula tokens (En_Si) | share outcome | The flag was only set after the textbox closed, so the partner could still collect their own token and the count went up by two | **Fixed**: the flag is set and sent the moment the token is collected; the collector's token finishes its textbox |
| Gold Skulltulas (En_Sw, gold) | duplicate until killed | Their spider stays alive until the token flag arrives | Unchanged ("everyone can see and fight them") |
| Floor switch, one-shot | share outcome | The door opens but their switch stays up | **Fixed**: their copy goes down (HookHandlers.cpp FollowSwitchFlag) |
| Floor switch, toggle; eye switch, toggle; crystal, toggle | share outcome | Their copy never moves, and the next press on their side did nothing (out of step) | **Fixed**: their copy follows the flag both ways |
| Floor switch, hold | share outcome | Their copy stays up. Worse: with both players standing on their own copies, whoever stepped off first closed the door on the other | **Fixed**: their copy shows the weight; a player still standing on their copy re-asserts the flag |
| Eye switch, one-shot; crystal, one-shot | share outcome | Stays open/red | **Fixed** |
| Torches, single | share outcome | Unlit until the room reloads (the door still opens) | **Fixed**: lights when the flag arrives |
| Torches, timed group | share outcome | Group lights for both once the flag is set | Unchanged. Two players can't light one group together: each client counts only its own torches. |
| Any switch flag 8+ being cleared | share outcome | `Flags_UnsetSwitch` truncated the flag bit to a u8, so the unset hook never fired: a hold switch released or a toggle switched off **only on the presser's screen**. Upstream SoH bug. | **Fixed** in z_actor.c (`Flags_UnsetSwitch`, `Flags_UnsetClear`) |
| Bombable walls/boulders already hooked (Bg_Bombwall, Bg_Breakwall, Obj_Bombiwa, Obj_Hamishi, Spot08/11/17 walls, webs, ice, doors...) | share outcome | Break on the partner's screen when the flag arrives | Unchanged (already worked) |
| Dodongo's Cavern stairs (Bg_Ddan_Kd), DC skull jaw (Bg_Dodoago), DMT boulder (Bg_Spot16_Bombstone), Spirit goddess face (Bg_Jya_Megami), Spirit Trial web (Bg_Gnd_Soulmeiro), silver-gauntlet boulders (En_Ishi large), golden-gauntlet pillars (Bg_Heavy_Block) | share outcome | Stayed whole for the partner until the room reloaded, which left them stuck behind something the other player opened | **Fixed**: each breaks or opens when its flag arrives |
| Hidden grottos, bombed or hammered (Door_Ana) | share outcome | No flag, so the partner saw no hole | **Fixed**: WORLD_OBJECT "grotto" opens the partner's copy at the same spot |
| Hidden grottos, Song of Storms | duplicate | The player who plays the song opens their own | Unchanged |
| Liftables (pots, rocks, bushes held by a player) | duplicate | The partner sees the puppet's carry pose and the object still on the ground | Unchanged (cosmetic) |
| 7DtZ supply caches (SevenDays/Loot.cpp) | share outcome | Owner records the cache as opened; the partner's lid swings open; no double loot | Unchanged (already worked) |

Not covered (left as is):
- Two players grabbing the same flagged item, or opening the same chest, within network
  latency can both get it.
- Event flags with the same u8 truncation (`Flags_UnsetEventChkInf`, `ItemGetInf`,
  `InfTable`, `EventInf` in z_actor.c) still don't send their unsets. Changing them would
  start syncing minigame and event unsets that have never synced, so that needs its own pass.
- Team-state sync on load or save fills an empty ammo slot from the partner's save, so the
  first nuts or sticks one player picks up also show up for a partner who has none yet.

Test hooks (Packets/WorldObject.cpp):
- `anchor_test_world(cmd, a, b, c)`: spawn an actor, set or clear a switch flag, and dump
  switches, tokens, rocks and ammo.
- `anchor_test_grottos(open)`
