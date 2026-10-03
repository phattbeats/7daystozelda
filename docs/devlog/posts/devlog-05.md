title: 7 Days to Zelda, Day 5: What you actually do
slug: 7dtz-devlog-05-the-features
excerpt: A full tour of the game side of 7 Days to Zelda, from the boarded-up Kokiri village to blood-red raid nights and the dawn card.
tags: 7 Days to Zelda, devlog, game design, Ocarina of Time, co-op
feature_image: img/live-G46-redmoon-final.png
publish: Day 5 (see schedule)

---

Four days in, and so far this series has been netcode, Emscripten and crash dumps. Today is the post I actually wanted to write: what you *do* in 7 Days to Zelda.

Quick recap: 7DtZ is browser Ocarina co-op with a 7 Days to Die layer on top. You play the actual story and the raids come to you. Players bring their own legally dumped ROM; nothing from Nintendo is shipped.

Same disclaimer as always: an AI agent (Claude, running through Paperclip) wrote nearly all of this code. I wrote the design calls, played what I could, and kept saying "send me screenshots". A lot of the "play-testing" below was an agent driving headed Chrome with scripted input and test shortcuts. I'll flag those as we go.

![The loop](diagrams/devlog-05-loop.svg)
*FIG 5-1 — The whole game on one page. Every system below plugs into one of these boxes.*

## 5.1 The first ten minutes

The design rule was: Kokiri Forest has to prove the game is different without bending the story. So a new save drops you into a village that's already been boarded up.

Step out of Link's house and there's a workbench by your ladder, a sign that reads "Day 1", barricades across both Lost Woods paths, and one fence on the green that's already half broken. None of that is built by you. It's seeded straight into the new save's base state. This is the actual table from patch 0010 (`Base.cpp`):

```cpp
static const Seed sVillageSeeds[] = {
    { PLACEABLE_WORKBENCH, -170.0f, -80.0f,  960.0f, 0x4000, 100 }, // by Link's ladder
    { PLACEABLE_SIGN,      -165.0f, -80.0f,  880.0f, 0x4000, 100 }, // "Day 1"
    { PLACEABLE_BARRICADE, -1240.0f, -80.0f, -200.0f, 0x4000, 100 }, // Lost Woods bridge path, north side
    ...
    { PLACEABLE_BARRICADE,  300.0f,   0.0f,  500.0f, 0x1C72, 35  }, // the broken fence on the village green
};
```

That last number is HP percent. The broken fence starts at 35, so the very first thing the village asks of you is a repair.

![Workbench and Day 1 sign](img/L10-village-workbench-sign.png)
*FIG 5-2 — The village workbench and the "Day 1" sign by Link's ladder. Note the on-screen Craft button on the right edge.*

A handful of Kokiri got new lines (six between Fado, Saria and a Kokiri boy). Fado's is my favorite because it sets the tone in one breath:

![Fado's new line](img/L43-fado-things-in-grass.png)
*FIG 5-3 — Fado: "Link, have you heard them? There are things in…"*

The full line in the patch is "Link, have you heard them? There are things in the grass after dark. They scratch at the boards all night. Mido says the barricades will hold... I hope he's right."

> **NOTE** In the M5 play run, the first 2 Fiber came from a test hook because the scripted steering couldn't line Link up with a bush, and Link got teleported twice. The village, the dialogue and the reload persistence were seen for real.

## 5.2 Materials and gathering

Five materials, one shared pool for the whole room, kept outside OoT's inventory (no spare save bytes, and ammo caps would fight the economy).

The gather sources are one data table in `SevenDaysData.cpp` (patch 0009):

```cpp
static const GatherSource sGatherSources[] = {
    // actor              material   amt unlock            dedupe s  perDay
    { ACTOR_EN_KUSA,      MAT_FIBER, 2,  UNLOCK_START,     60,       false }, // grass/bushes cut or thrown; regrows
    { ACTOR_EN_ISHI,      MAT_STONE, 1,  UNLOCK_START,     120,      false }, // small rocks lifted and broken
    { ACTOR_OBJ_BOMBIWA,  MAT_STONE, 3,  UNLOCK_BOMB_BAG,  120,      false }, // bombable boulders
    { ACTOR_EN_WOOD02,    MAT_WOOD,  2,  UNLOCK_START,     1800,     true  }, // rolling into a tree, once a day
    { ACTOR_EN_SKB,       MAT_BONE,  1,  UNLOCK_DEKU_TREE, 60,       false }, // Stalchildren
    { ACTOR_EN_RD,        MAT_ROT,   2,  UNLOCK_DEKU_TREE, 60,       false }, // ReDeads and Gibdos
};
```

| Material | Source | Payout | Opens at |
|---|---|---|---|
| Fiber | Cut or thrown grass and bushes (regrows) | 2 | Start |
| Stone | Small rocks / bombable boulders | 1 / 3 | Start / Bomb Bag |
| Wood | Rolling into a tree, once per tree per in-game day | 2 | Start |
| Bone | Stalchildren | 1 | Deku Tree |
| Rot | ReDeads and Gibdos | 2 | Deku Tree |

*TABLE 5-1 — Gathering, straight from the data table. Every number is a placeholder to tune on game night.*

The gatherer sends GATHER to the room owner, who throws away repeats from the same source (so two players hacking the same bush get paid once) and broadcasts new totals. That plumbing gets its own post tomorrow.

One honest wrinkle: Kokiri Forest has no trees you can roll into, so new saves start with 8 Wood.

The first time you gather each material, Navi says one line and then never again. These are in the same table:

![Navi's first-Fiber line](img/L18-navi-first-fiber.png)
*FIG 5-4 — "Hey! Grass fiber! The workbench can use that!" Shown once, flag saved.*

The Rot line is the best one: "Eww, rot! Hold your nose, Link. It makes Deku Nuts, believe it or not!"

## 5.3 The workbench, now a pause-menu page

Crafting v0 was a floating ImGui window. My feedback:

> "I want to add it to the existing UI so it's controller friendly."

So the agent did the bigger job and made the Workbench a fifth page of the actual pause menu, sitting between Equipment and Select Item. R from Equipment or Z from Select Item turns to it, and the neighboring pages show its edge like any other page.

![Pause page ring](diagrams/devlog-05-pause-pages.svg)
*FIG 5-5 — The pause ring with the new page. With 7 Days off, the menu is vanilla: four pages, original labels.*

![Workbench page, Craft tab](img/W41-row0.png)
*FIG 5-6 — The Workbench page. Pool totals across the top, the game's own cursor on Deku Sticks (3 Wood), and the day counters on the pause line.*

The stick or D-pad moves the game cursor. The top row is the Craft / Trade / Base tabs. A crafts, sells or places. A greyed-out row tells you *why* in the bottom panel ("Save the Deku Tree", for example). You can get there with Tab, the on-screen Craft button, or by pressing A on a placed workbench. My review of this one was a single word: "beautiful."

> **WARNING** Controller support here was tested with keyboard keys mapped to N64 buttons, solo. A real gamepad goes through the same inputs but nobody had plugged one in at the time of writing. The page title and labels use the dialogue font, not stylised title art.

### Recipes

| Output | Cost | Gate |
|---|---|---|
| Deku Sticks ×5 | 3 Wood | Start |
| Deku Seeds ×30 | 4 Fiber | Start (needs Slingshot) |
| Deku Nuts ×5 | 3 Fiber, 1 Rot | Deku Tree |
| Arrows ×5 | 2 Wood, 1 Bone | Deku Tree + blueprint |
| Bombs ×5 | 3 Stone, 1 Rot | Bomb Bag + blueprint |
| Workbench | 4 Wood, 4 Stone | Start |
| Barricade | 6 Wood, 2 Fiber | Start |
| Torch | 2 Wood, 1 Fiber | Start |
| Spike strip | 4 Wood, 3 Bone | Deku Tree + blueprint |
| Storage chest | 8 Wood | Deku Tree |
| Stone wall | 6 Stone, 2 Wood | Bomb Bag + blueprint |
| Bomb-flower trap | 4 Stone, 2 Rot | Bomb Bag + blueprint |
| Player gate | 8 Wood, 4 Stone | Hookshot + blueprint |

*TABLE 5-2 — Every recipe in `sRecipes` as of patch 0012. Unlocks come from tools and bosses, not from nights.*

The Trade tab turns materials into rupees: 5 Fiber, 3 Wood or 3 Stone for 5 rupees, 2 Bone or 2 Rot for 10. That exists so gathering helps you buy the 40-rupee Deku Shield Mido demands before raids even matter. In the live run, crafting Deku Sticks took 3 Wood and gave 5 sticks, and selling 5 Fiber paid 5 rupees.

## 5.4 Placing things

Pick "Place" on the Base tab and the menu closes and you get a see-through ghost in front of Link. It snaps to a 30-unit grid. C-Left/C-Right rotate, A builds, B cancels. The kit isn't spent until the piece actually exists, so cancelling is free.

![Placement ghost](img/L24-placement-ghost.png)
*FIG 5-7 — A green barricade ghost in Kokiri Forest. Bottom hint: "Placing Barricade C-Left/C-Right rotate, A place, B cancel".*

The ghost box is green when the spot is fine and red when it isn't. A on red builds nothing and tells you the reason. Here's the check order from `Placeables.cpp` (patch 0010), trimmed:

```cpp
if (COLPOLY_GET_NORMAL(poly->normal.y) < cosf(30.0f * (float)M_PI / 180.0f)) {
    sPlace.reason = "Too steep (over 30 degrees)";
    return;
}
...
if (NearExitOrDoor(play, ground, floorY)) {
    sPlace.reason = "Too close to a door or exit";
    return;
}
...
if (dx * dx + dz * dz > BASE_RADIUS * BASE_RADIUS) {
    sPlace.reason = "Too far from the base's workbench";
    return;
}
```

![Placement check flow](diagrams/devlog-05-placement.svg)
*FIG 5-8 — Every reason the ghost can go red, in the order the code checks them.*

### Anywhere-base rules

- **Outdoors only.** Bases go where the raid clock runs. Dungeons and interiors stay frozen, so a base there would never see a raid.
- **The workbench is the base.** Everything sits within 800 units of it.
- **One base per era**, at most 24 pieces. Packing up refunds every kit.

The room owner makes the final call, so a green spot can still get refused if somebody else built there a split second earlier. You get the same message and keep your kit.

> **NOTE** The four M5 pieces (barricade, spike strip, workbench, chest) started life drawn as crates and spikes. When I asked about real models, my rule was "not online. only from in game or majoras mask." The OoT-only art pass is a Day 7 story.

## 5.5 Raids

The first night isn't a raid, it's a scare. Kokiri Forest's clock is frozen in vanilla, so the prologue's nights are scripted to story beats. Open the Kokiri Sword chest and dusk falls, Navi pipes up, and two Stalchildren claw up by the bridge.

![Dusk after the Kokiri Sword](img/live-R04-dusk.png)
*FIG 5-9 — "Link, it's getting dark... Something is moving down by the bridge!"*

Gohma's death brings the first real raid. In the first M6 build that night was budget 7 with Keese, and the agent only "survived" it by healing Link 20 times. My note:

> "it needs to be easy-ish the first time. option to select days when yoiu start a game."

So now Gohma's night is 2 Stalchildren plus 1 per player, Stalchildren only, never more than 2 alive, barricades take half damage, and dawn comes the moment you kill them all. Raid 2 brings Keese and Wolfos. ReDeads come from raid 3, Gibdos from raid 4.

![Stalchild at the village](img/live-S17-night-village.png)
*FIG 5-10 — Night in the village: a Stalchild by the Day 1 sign and Link's ladder.*

And the "select days" part became a picker the room owner sees the first time Link can move on a new save:

![Raid interval picker](img/R04-picker.png)
*FIG 5-11 — "How often do raids come?" Every night, 2, 3 (recommended), 5 or 7 days. Changeable later on the Base tab.*

In the solo run (no heals this time), the agent picked 2 days, won the dusk with the sword, then cleared Gohma's night of 3 Stalchildren in 66 seconds, bottoming out at 2 of 4 hearts. The Kokiri Sword and Gohma were granted by test shortcuts rather than played. The *how* of raids (budgets, spawn rings, routing) is tomorrow's post.

## 5.6 Loot

Loot is added around the story, never swapped into it. Vanilla chests keep their items.

- **Supply caches:** 32 across 11 dungeons plus 3 grottos, opened once per save.
- **Pots and crates** roll a small drop weighted by the area's tier: forest-tier pots pay Fiber and Wood, deeper dungeons pay Stone, Bone and Rot.
- **Blueprints** gate the spikes, stone wall, gate, bomb trap, bombs and arrows.
- **Bosses:** Gohma gives the spike strip blueprint, King Dodongo the stone wall, Barinade the gate.
- **Skulltulas:** every 10 tokens gives a blueprint.

![Supply cache in the Deku Tree](img/live-G29-cache-opened.png)
*FIG 5-12 — A Deku Tree supply cache: +2 Fiber, +3 Stone, +2 Wood and the Bomb-flower trap blueprint.*

Boss blueprints are a tiny switch in the data file (patch 0012):

```cpp
const char* BossBlueprint(int16_t bossActorId) {
    switch (bossActorId) {
        case ACTOR_BOSS_GOMA:
            return "spikes"; // Gohma: the spike strip
        case ACTOR_BOSS_DODONGO:
            return "stonewall"; // King Dodongo: the stone wall
        case ACTOR_BOSS_VA:
            return "gate"; // Barinade: the gate that opens for players
    }
    return nullptr;
}
```

> **WARNING** The boss rewards and the 10-token blueprint were fired by test hooks, not by actually beating Gohma or collecting tokens. A real crate was slashed in Jabu-Jabu during the review pass, but that evidence is from an earlier build than the final one.

## 5.7 Majora-style nights

This is the part that makes it feel like a different game. Raid nights borrow Majora's Mask's dread with OoT's own parts.

**The final-hours clock.** A small clock fades in before a raid, the hour marks turning red as it closes in, then it hides again. A later fix hides it during cutscenes and text boxes (seen hiding during a sign read; a scripted cutscene was never tested).

![Final-hours clock](img/clockzoom.png)
*FIG 5-13 — The clock at 5:15 and 5:52. Watch the marks go red.*

**Red sky and red moon.** The scene's ambient, light and fog colors ease toward red, and the moon grows and turns red. The moon change is two globals read inside vanilla's `Environment_DrawSunAndMoon` (patch 0012, `z_kankyo.c` and `Nights.cpp`):

```cpp
gSevenDaysMoonScale = 1.0f + 1.6f * sRed;
gSevenDaysMoonRed = (u8)(255.0f * sRed);
...
scale *= gSevenDaysMoonScale; // 7 Days to Zelda: a raid night's moon looms
```

![Red moon over Hyrule Field](img/live-G46-redmoon-final.png)
*FIG 5-14 — A raid night in Hyrule Field. That is the moon.*

**Music.** Majora's Final Hours isn't in the OoT ROM and custom streamed music is silent in the browser build, so raids use OoT's Mini-Boss theme (sequence 0x38). After review, the track stops when the wave is cleared.

**The dawn card.** At each dawn a centered card in the game's font tells you where you stand:

![Dawn card](img/live-G41-dawncard-1.png)
*FIG 5-15 — "Dawn of Day 4 · 1 night until the raid", with "3 days survived · 1 raid survived" under it.*

**Counters, never on the HUD.** Days survived and raids survived show on the dawn card, on the pause line (you can see it at the top of FIG 5-6), and in file select:

![File select counters](img/live-G45-fileselect-moreinfo.png)
*FIG 5-16 — File select reads "Days 3 · Raids 1". The More Info view was switched on by a test hook for this shot.*

Lose a raid (everyone down) and it costs you: each placeable loses half its remaining HP and the pool loses 25% of each material.

## 5.8 Telling Links apart

Four Links in green is a mess on game night. Every player now picks their own fairy core color, fairy aura color and tunic color in the web lobby (color wheel, hex field, 9 swatches) or in game under Settings, Network, Anchor. Everyone else sees them, and your own Navi wears your gradient too. Colors ride in the invite link, and you get a warning if your tunic is too close to someone else's.

![Another player's Link in red](img/n4-B-sees-other-zoom.png)
*FIG 5-17 — Player B's view of player A: red tunic, gold fairy.*

My reaction, verbatim: "woooooooooooo!"

> **NOTE** Color changes reached the other player in 1.1 to 2.8 seconds in testing, but those test browsers rendered at 2 to 4 fps in software WebGL. The under-one-second target still needs a check on real hardware.

## 5.9 What's not real yet

Straight list:

- Real grass-cutting and tree-rolling were never driven by a human in these runs.
- The survived M6 raid was healed 20 times. Only the M6.1 solo run won by fighting.
- Wolfos, ReDeads and Gibdos weren't seen in a live raid.
- The child base doesn't turn to ruins at the seven-year jump yet.
- Nobody outside Kokiri Forest reacts to the nights. That's M9.

Tomorrow (Day 6) we open up the raid code: budgets, spawn rings, barricade damage, and who in the room gets to decide what.
