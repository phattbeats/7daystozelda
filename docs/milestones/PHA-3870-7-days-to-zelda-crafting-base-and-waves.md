# PHA-3870: 7 Days to Zelda: crafting, base and waves

Status at export (2026-10-03): blocked

Oct 2, 2026 · @Brandon Kelly

Build four systems on top of the shipped co-op layer, in this order: materials, crafting, placeables, waves. Every change to shared state is decided by one machine in the room. The first playable milestone is Kokiri Forest's boarded-up village holding its first raid.

## Design decisions

Decided by Brandon on 2026-10-02. Sections below follow these; where they differ, this section wins.

**Mode: the story, with raids added.** Players gather materials while exploring, solving puzzles and running dungeons; there is no separate survival map. In vanilla the clock runs in only four scenes: Hyrule Field, Lake Hylia, Gerudo Valley and Hyrule Castle grounds. It is frozen everywhere else, Kokiri Forest included. The mod's raid clock (Waves, part 5) changes that for outdoor scenes. Dungeons and interiors stay frozen, so a dungeon is always a safe time-out.

**Progression: unlocks come from tools and bosses, not from nights.** Recipe unlocks and gathering sources check save state: items owned, upgrades, boss flags (`INV_CONTENT`, `CUR_UPG_VALUE`, `Flags_GetEventChkInf`). The mapping below is a first pass to tune:

| Unlocked by          | Gathering it opens                  | Recipes it opens                                          |
| -------------------- | ----------------------------------- | --------------------------------------------------------- |
| Start (Kokiri Sword) | Fiber, Wood                         | Workbench, barricade, torch                               |
| Deku Tree beaten     | Bone, Rot                           | Spike strip, storage chest                                |
| Bomb Bag             | Stone from bombable rocks and walls | Stone wall, bomb-flower trap                              |
| Hookshot             | Materials on ledges out of reach    | Gate that opens for players                               |
| Megaton Hammer       | Ore from boulders                   | Upgrade walls in place (wood → stone → iron); fast repair |
| Silver Gauntlets     | Silver rocks become an iron source  | Iron wall                                                 |

Wave difficulty scales with progression too. Replace the gamestage formula with:

```
\text{gamestage} = 3 \cdot \text{dungeons cleared} + \text{heart containers} + 2 \cdot \text{players} + \text{horde nights survived}
```

**Losing a horde night costs the base and the pool.** The co-op build already shares game-overs (`CoopLifeSync`). When every player is down during a horde:

* Each placeable loses half its remaining HP.
* The pool loses 25% of each material, rounded down.
* The night counts as failed.

The normal game-over respawn still applies.

**Days survived, shown on the night's own terms.** Track two counters in the save section: days survived and horde nights survived. They appear on the dawn card, in a pause-screen line, and in SoH's file-select details (`FileSelectMoreInfo`). They are never on the HUD all the time.

**Majora's Mask-style nights.**

* **Dawn card.** At each dawn, a centered card in the game's own font: "Dawn of Day 7 · 2 nights until the raid".
* **Warning.** The evening before a horde, Navi warns the players through SoH's `CustomMessageManager`.
* **Clock.** In the last in-game hour before a horde, a small clock fades in, like Majora's three-day clock, then fades out once the horde starts.
* **Horde night look.** Ease the scene's ambient, light and fog colors (`play->envCtx`) toward red. Scale up the moon and tint it red where `Environment_DrawSunAndMoon` draws `gMoonDL`.
* **Music.** Pick a track already in OoT's ROM. Majora's Final Hours isn't in it, and custom streamed music is silent in the browser build.

**Players recolor the whole tunic.** OoT's cap is part of the tunic's material, so one color covers tunic and cap. The lobby's color already travels as Anchor's per-player `color`, which today only tints the fairy. Use it for the tunic too: set the player's own tunic cosmetic (`Link.KokiriTunic` and siblings), and when drawing other players' Links (`DummyPlayer`), draw them in their owner's color, not your local cosmetics. Replacement Link models remain an option on desktop, but the browser build doesn't load mods yet.

**Bases go anywhere outdoors, one per era.** Three rules:

* **Outdoors only.** Any outdoor scene the raid clock runs in: Kokiri Forest, Hyrule Field, Lon Lon Ranch, Kakariko, Lake Hylia, Gerudo Valley and the like. Dungeons, interiors and the Market stay frozen, so a base there would never see a raid.
* **The workbench is the base.** The first workbench marks the base center. Every other piece must sit within 800 units of it, which keeps a base under the 50-object collision cap and tells the waves where to go.
* **One base per era.** Moving means packing up, with every kit refunded. This settles the seven-year jump: the child base and the adult base are separate. The child base becomes ruins at the jump and refunds half its materials.

If the game should pick the spot instead, don't: choosing and fortifying the spot is half the fun.

## First ten minutes

Kokiri Forest proves the game is different and teaches gathering, without bending the story. Every beat reuses a system below:

1. **The village is boarded up.** Stepping out of Link's house, the player sees crude barricades across the Hyrule Field bridge and the Lost Woods entrance, a broken fence, and a workbench by Link's ladder. These are placeables seeded into a new save's `BaseState` by an init hook, so the village is the player's first base without them building it.
2. **A few Kokiri say something new.** Five or six lines change through `CustomMessageManager`. Fado talks about "things in the grass after dark", Saria's greeting mentions the barricades, and a sign by the workbench reads "Day 1". The rest of the dialogue stays vanilla.
3. **Firsts teach themselves.** The first time a player cuts grass: "+2 Fiber" and one Navi line, "Hey! Grass fiber! The workbench can use that!" The first rock, tree bump and bone each get one line, then never again. One flag per first goes in the save section. No tutorial screens.
4. **Gathering pays for the first story goal.** The workbench gets a Trade tab that turns materials into rupees (`Rupees_ChangeBy`). That helps buy the 40-rupee Deku Shield Mido demands, so gathering matters before raids do. The Kokiri Shop actor stays untouched.
5. **The Kokiri Sword brings the first dusk.** Kokiri Forest's clock is frozen in vanilla, so the prologue's nights are scripted to story beats. The mod sets the time directly, the way the Sun's Song does. When a player opens the Kokiri Sword's chest, dusk falls: two or three Stalchildren claw up near the Hyrule Field bridge while the Kokiri stay indoors. At dawn the Majora-style card reads "Dawn of Day 2". The player is armed, so it's a scare, not a death screen.
6. **Gohma's death brings the first raid.** With the forest's guardian gone, the dead come for the village that night. The raiders are Stalchildren and Keese only: Deku Babas are rooted plants and can't march. Defending the boarded-up village is the first real raid, at the moment players have the most reason to care about it.

## Woven into the game

Crafting and loot ride on what players already do in Ocarina, so nobody stops the story to grind. Players call hordes "raids"; the code keeps the name `HordeNight`.

**Where loot comes from.**

| Source                                                                                                   | Gives                                                                                        | Hook                                                                    |
| -------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| Grass, rocks, trees, enemies                                                                             | Base materials (see Materials and gathering)                                                 | As in that section                                                      |
| Pots and crates (ObjTsubo, ObjKibako)                                                                    | Small material rolls, weighted to the area's tier                                            | Actor-destroy hook                                                      |
| Supply caches: a new chest-like actor (ActorDB), 2–4 per dungeon plus some grottos, opened once per save | Bigger material bundles and a chance at a blueprint                                          | Placed from a per-scene table; an opened flag lives in the save section |
| Gold Skulltula tokens                                                                                    | A blueprint every 10 tokens, on top of the House of Skulltula's own rewards                  | OnItemReceive                                                           |
| Boss defeats                                                                                             | The next tier's unlock plus its key blueprint (Gohma: spike strip; King Dodongo: stone wall) | OnBossDefeat                                                            |
| Raids survived                                                                                           | Materials scaled by gamestage                                                                | At dawn                                                                 |

Vanilla chests keep their story items. Loot is added around the story, never swapped into it.

**Tiers and blueprints.** Tools and bosses open a tier (the progression table above). Blueprints found inside a tier unlock its individual recipes. Each tier opens with its basics; the strong pieces, like the iron gate and upgraded spikes, come from caches and bosses, so exploring a dungeon fully pays off at the base.

**Navi is the guide.** She says one short line the first time each thing happens, through `CustomMessageManager`, then never repeats it; a flag per line lives in the save section.

* **Each tier unlock**, when the item or boss lands. Bomb Bag: "Link! Bombs crack stone. Stone walls hold twice as long as wood!" Megaton Hammer: "That hammer can rebuild walls stronger: wood to stone to iron!"
* **Each first**: first blueprint, first supply cache, first trade at the workbench.
* **Raids, explained in stages**, each the first time it matters:
  1. The evening before the first raid: "Every third night the dead rise and come for wherever we sleep. Build, Link!"
  2. The first raid starts: defend the workbench, and walls take damage.
  3. The first dawn after a raid: rewards, and that losing costs walls and materials.
  4. The first raid that hits the base while everyone is away.
  5. Each new enemy type's first raid. ReDead: "Their scream freezes you. Don't let them get close!"
* **Her C-Up hints** gain a crafting and raid tip pool, used when she has nothing story-urgent to say.

After the first explanation, the final-hours clock stays wordless.

## Starting point

The base is bghill95/OOT-True-Co-op at `b738c76` plus our patches 1–7. They live in the bundle's `src/`, with the rebuild steps in `src/BUILD-WEB.md`. The patches already add shared enemies, nearest-player targeting, enemy sound and particle sync, horde night, the browser build and mobile support. Put the new work in its own module, `soh/soh/SevenDays/`. Register it through SoH's ShipInit `COND_HOOK` pattern (as `Enhancements/BootToDebugWarpScreen.cpp` does), behind one master setting, `gSevenDays.Enabled`, so plain co-op still works with it off.

Two kinds of authority, both already exist:

* **Base-wide state** (materials, the base, crafting) is decided by the Anchor room owner, `roomState.ownerClientId`.
* **Anything living in a scene** (wave enemies, barricade damage) is decided by the existing enemy authority: the lowest client ID in that scene (`EnemySync::IsLocalAuthority`). Authority already passes to the next player when someone leaves or dies.

Rules the browser build imposes:

* **No `std::thread` and no blocking file I/O.** A stray thread is exactly what broke in-browser ROM extraction (PHA-3860).
* **Keep per-frame work small.** The browser build has a single thread.
* **Packets carry JSON, never raw structs.** Desktop is 64-bit and the browser is 32-bit, and players on both share rooms.

## Materials and gathering

Materials are one shared pool for the whole room, stored outside OoT's own inventory. That inventory has no spare save bytes, and its ammo caps would fight a crafting economy.

Start with five materials, each tied to a GameInteractor hook:

| Material | Source                                                                                | Hook                                     |
| -------- | ------------------------------------------------------------------------------------- | ---------------------------------------- |
| Fiber    | Grass and bushes cut or thrown (EnKusa); grass regrows, so it pays again once regrown | VB\_GRASS\_DROP\_ITEM                    |
| Stone    | Small rocks lifted and broken (EnIshi)                                                | OnActorKill                              |
| Wood     | Rolling into a tree (EnWood02); one payout per tree per in-game day                   | VB\_TREE\_DROP\_ITEM (fires on the bonk) |
| Bone     | Killing Stalchildren                                                                  | OnEnemyDefeat                            |
| Rot      | Killing ReDeads and Gibdos                                                            | OnEnemyDefeat                            |

Deku Sticks and Nuts keep their vanilla drops and work as crafting inputs too. A gather pays out instantly: a toast through `Notification::Emit` (the same path `HORDE_EVENT` uses) and the item-get sound. Nothing drops into the world, so nothing extra needs syncing.

The gathering client sends `GATHER {material, amount, sourceKey}`. The room owner does three things:

* Throws away repeats by `sourceKey`, built with EnemySync's `PackKey` (room, setup index, actor ID, params), so two players hitting the same bush pay once.
* Applies the amount to the pool.
* Broadcasts the new totals.

Keep the material list and payout amounts in one data table, so balancing never touches logic.

## Crafting

Crafting v0 is an ImGui window: a subclass of `Ship::GuiWindow`, the same way `AnchorRoomWindow` is built. It opens when a player presses A near a placed Workbench, or from a hotkey. Its buttons are big enough for the touch layout. A crafting page inside the pause menu is the nicer v1, but it means new textures and pause-screen layout code; ship the window first.

Recipes live in one data table: `{id, inputs[], output, unlock}`. Outputs come in two kinds:

* **Vanilla consumables** (Deku Sticks, Nuts, Seeds, arrows) are granted with `Inventory_ChangeAmmo` / `Item_Give`, within the player's current capacity upgrades.
* **Placeable kits** are counts in the shared pool.

The client sends `CRAFT_REQUEST`. The room owner checks the inputs, deducts them and replies `CRAFT_RESULT`, and the requester grants the item only on that reply. Two players spending the same wood at once can't both succeed.

Starter recipes (costs are placeholders to tune on game night):

| Output        | Cost            |
| ------------- | --------------- |
| Barricade     | 6 Wood, 2 Fiber |
| Spike strip   | 4 Wood, 3 Bone  |
| Stone wall    | 6 Stone, 2 Wood |
| Torch         | 2 Wood, 1 Fiber |
| Storage chest | 8 Wood          |
| Workbench     | 4 Wood, 4 Stone |
| Deku Nuts ×5  | 3 Fiber, 1 Rot  |

A new save starts with the village's workbench already placed (First ten minutes), so the first craft is a barricade.

## Placeables and the base

Each placeable is a new actor type registered through SoH's `ActorDB::AddEntry(name, desc, ActorInit)`. That avoids touching the vanilla actor table.

**Collision.** Each placeable is a `DynaPolyActor` with a box collision header built in code: 8 vertices and 12 triangles, registered with `DynaPoly_SetBgActor`, so Link and enemies collide with it like scenery.

**Art.** Draw with display lists the ROM already has: crates, fences, signs, the torch stand. Their objects must be loaded in the scene: check `field_keep` / `gameplay_keep` first, and otherwise request the object at scene init with `Object_Spawn`. SoH raised the loaded-object limit from 19 to 128 (`OBJECT_EXCHANGE_BANK_MAX`), so object slots aren't a constraint.

**Placement mode.**

* A translucent ghost of the placeable sits 60–80 units ahead of Link, dropped to the floor with `BgCheck_EntityRaycastFloor4` (the same call `HordeNight` uses).
* It snaps to a 30-unit grid. C-left and C-right rotate it 45°, A places it, B cancels.
* Placement is refused on water, on slopes steeper than 30°, and within 100 units of a door or a scene exit.

**Hard limits.** OoT allows 50 dynamic-collision actors per scene (`BG_ACTOR_MAX`) and shares a 512-polygon / 512-vertex dynamic budget between them in outdoor scenes (`z_bgcheck.c`). Lon Lon Ranch already spends part of both. Cap a base at about 24 placeables, and enforce the cap in the owner's placement check.

**Anywhere bases.** The owner's placement check enforces the base rules from Design decisions:

* the scene's clock must run;
* every piece sits within 800 units of the base's workbench;
* one base per era.

Spawn placeables as scene-wide actors (room −1), not as children of one room, so they survive walking between rooms of the same scene. Packing up turns each piece back into a kit.

**Behaviors.**

| Placeable              | What it does                                                         |
| ---------------------- | -------------------------------------------------------------------- |
| Barricade / stone wall | Has HP; the enemy authority drains it while enemies press against it |
| Spike strip            | An attack collider that deals half a heart to enemies crossing it    |
| Torch                  | No wave spawn point within 300 units; lights the base                |
| Storage chest          | Opens the material pool                                              |
| Workbench              | Opens crafting                                                       |

## Waves

Waves extend the shipped `HordeNight` module rather than replacing it. Today it:

* runs only on the enemy authority, in Hyrule Field and Lon Lon Ranch (Lon Lon Ranch's clock is frozen in vanilla, so it only sees a raid when players arrive already at night);
* fires every third night by `gSaveContext.totalDays` (`HordeInterval`);
* spawns Stalchildren, ReDeads, Gibdos and Wolfos 250–450 units from the players, raycast to the floor and never on water;
* caps the live count at 10, plus 2 per horde, up to 24;
* drags each ReDead's home point toward the nearest player, so they shamble in;
* clears every wave enemy at dawn.

Four changes turn that into raids on a base.

**1. A budget instead of a fixed cap.** Each night gets a score in the style of 7 Days to Die's game stage, driven by progression (formula in Design decisions).

The budget is 1.5 × gamestage. It buys enemies at fixed costs: Stalchild 1, Wolfos 3, ReDead 4, Gibdo 5.

**2. Routes that work anywhere.** OoT enemies don't find paths, and a base can sit anywhere, so routing has a general fallback:

* At raid start, sample spawn points on a ring 600–900 units from the workbench: floor found by raycast, not water, out of every player's view.
* Generalize `ShambleTowardPlayers` so each enemy's home point walks straight toward the workbench. When a player is within 300 units, it targets the player instead.
* Stuck check: an enemy that gains less than 20 units in 5 seconds is moved, out of sight, to another sampled point.

Favorite spots (the Lon Lon Ranch gate, Kakariko's stairs) can get hand-made JSON routes later, as an upgrade, not a requirement. Enemies then walk into whatever stands between them and the workbench.

**3. Damage to the base.** Each frame, the enemy authority checks every wave enemy's body collider against each barricade's box. Contact drains HP at a per-enemy-type rate, sent out as `BASE_DELTA`. At zero the barricade breaks, and the route opens.

**4. Raids on an empty base.** A raid night can arrive while everyone is far from the base, for example in Gerudo Valley; it can't arrive in a dungeon, since the clock stops there. In that case the room owner settles the raid on paper at dawn:

* wave budget minus the base's defense rating (the HP and damage of its pieces) gives the damage;
* the damage is spread over the pieces nearest the routes, and the pool takes a cut;
* the dawn card reports it: "The base was raided: 2 barricades lost".

At dawn, wave enemies still clear, and survivors earn materials by gamestage.

**5. The raid clock.** Read from the game data: vanilla's clock runs (time speed 10) only in Hyrule Field, Lake Hylia, Gerudo Valley and Hyrule Castle grounds. It is frozen (speed 0) in Kokiri Forest, the Lost Woods, Lon Lon Ranch, Kakariko, the Graveyard, the Market and every interior and dungeon. At speed 10 a full day lasts about 4 real minutes: about 2.6 minutes of day and 1.4 of night, because nights run at double speed.

* **Prologue.** Kokiri Forest's nights are scripted to story beats (First ten minutes), setting `gSaveContext.dayTime` directly like the Sun's Song. The mod's own day counter counts them, since `totalDays` only rises at a natural dawn.
* **After the prologue.** On scene load, the mod gives frozen outdoor scenes a time speed through `play->envCtx.timeIncrement` / `gTimeIncrement`, about half the field's, so a day there lasts about 8 minutes. Dungeons and interiors stay frozen. NPCs that differ by time of day only refresh on scene re-entry, the same as arriving at night in vanilla.
* **Raid nights hold the clock.** Night doesn't advance toward dawn until the wave is cleared or a minimum time passes (about 4 minutes), so a raid isn't over in 85 seconds.
* **Raid scenes.** `HordeNight`'s fixed two-scene list becomes any outdoor scene with a base or players in it.

## Netcode and persistence

New packets follow the pattern of `HORDE_EVENT` and `ENEMY_PLAYER_EFFECT`: a file under `Network/Anchor/Packets/`, a type constant, and a line in `Anchor::ProcessIncomingPacketQueue`. Each is sent to the room, or to one player with `targetClientId`.

| Packet                         | Sender → decider                | Carries                                   |
| ------------------------------ | ------------------------------- | ----------------------------------------- |
| GATHER                         | Gatherer → room owner           | material, amount, sourceKey               |
| CRAFT\_REQUEST / CRAFT\_RESULT | Crafter → owner → crafter       | recipe ID; ok or refused, with new totals |
| PLACE\_REQUEST                 | Builder → owner                 | type, scene, room, position, rotation     |
| BASE\_DELTA                    | Owner or enemy authority → room | rev, plus add / remove / HP change        |
| BASE\_STATE                    | Owner → a joiner                | the whole base, on join                   |
| HORDE\_EVENT (extended)        | Enemy authority → room          | night number, gamestage, wave status      |

**What gets stored.** The canonical record is `BaseState {rev, era, center, materials, placeables[{id, type, scene, pos, rot, hp}], counters, firsts, lootOpened}`. Store it as a SaveManager section (`AddSaveFunction("sevenDays", 1, …)`), with a matching AddLoadFunction, so it sits in every member's save file.

**Late joiners.** Anchor's team sync already ships the whole serialized `gSaveContext` as `payload["state"]` in `UPDATE_TEAM_STATE`, and the stock Anchor server holds that team state in memory for whoever joins later (until the relay restarts; the save files stay the durable copy). Add `state["sevenDays"]` to that same packet and players who join later get the base for free. When two copies disagree, the higher `rev` wins.

**Spawning.** Placeables are not enemy-synced. On `OnSceneSpawnActors`, every client spawns its own copy of each entry for that scene, keyed by the stable `id`. Only HP and add/remove events travel.

## Build order and testing

Ship in five milestones. Each one sits behind its own setting and leaves `COOP-M3-TEST-GUIDE.md` passing:

1. **M4: materials and crafting.** Gathering hooks, the shared pool, the crafting window with its Trade tab, consumable recipes, unlocks gated by tools and bosses, and Navi's first-time lines for gathering. Player tunic colors land here too: a small change and a big win on game night.
2. **M5: placeables, saving and the village.** Barricade, spike strip, workbench and chest; placement mode; anywhere-base rules; the `sevenDays` save section with counters and firsts; late-join sync; the boarded-up Kokiri village seeded into new saves.
3. **M6: raids.** Base damage, fallback routing, gamestage budgets, raids on an empty base, the losing-a-night penalty, the Night 1 preview, and Navi's staged raid explanations. This is the first night worth playing.
4. **M7: loot and Majora-style nights.** Supply caches, pot and crate rolls, blueprints, boss and Skulltula rewards; dawn cards, the final-hours clock, red sky and moon, and the raid track.
5. **M8: tuning.** Costs, HP, the unlock ladder, loot tables and wave curves, set by real game nights.

**Rebuilds.** Builds are incremental. The full 12 GB build is only for the randomizer tables, which never change, so after a source change it's a recompile plus a \~30 s link (`src/BUILD-WEB.md`). Browser crashes now print named stacks. Agents build from zelda-buildtree.tar.gz in the Nextcloud folder, using the Dockerfile in the bundle's src/buildtree/: the tree is frozen at its original paths, so a one-file change rebuilds in about 26 s on any Docker host.

**Testing.** The test harness used for PHA-3860 drives two headless browser profiles into one room through a SWAG-like proxy and a local Anchor server. It creates saves by scripted input and boots straight into a scene with the debug warp settings (`gDeveloperTools.DebugEnabled`, `BootToDebugWarpScreen`, `gGeneral.BetterDebugWarpScreenCurrentScene`). It now ships in the bundle as `tools/webtest/`, with a README (Nextcloud: `soh-coop-web-2026-10-02-pha3860.tar.gz`).

**Open questions.**

* Who keeps the base authoritative when the room owner is offline: the next-lowest client ID from its cached copy, or a read-only base?
* Does a materials pool shared by the whole room feel right, or should each player carry their own?

## Backlog

Co-op loose ends to file in the 7 Days to Zelda project as backlog issues. Each carries its own check.

| Issue                                                                                                                                                    | Done when                                                                               |
| -------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| Deploy the PHA-3860 build to zelda.phatt.vip; drop the oot.o2r workaround from the PHA-3856 deployment doc                                               | A fresh browser unpacks a ROM on the live site and loads a save                         |
| King Dodongo boss sync: bomb-inhale request from the thrower, sync of his private fire-breath effects, phase-gated like Gohma (spec in the Notebook doc) | Two players beat him together; either player's bomb stuns him                           |
| Release effect for grabbing enemies, so Dead Hand and Moblin can target players other than the host                                                      | A non-host player is grabbed and released correctly in Bottom of the Well               |
| Windows desktop build: fork bghill95/OOT-True-Co-op, apply patches 1–7, let generate-builds.yml produce artifacts                                        | A Windows player joins a browser player's room                                          |
| Real-phone pass: iPhone Safari and Android Chrome, ROM unpack memory, touch controls, home-screen install                                                | One full horde night played on each phone without a crash                               |
| Real Bluetooth controller pass on phone and desktop browser                                                                                              | Link moves, Z-targets and uses C-items from the controller; touch pad hides and returns |
| Move tools/webtest/ from the bundle into the repo and run it before each release                                                                         | lobby, mobile, relay and jointest pass on the release build                             |
