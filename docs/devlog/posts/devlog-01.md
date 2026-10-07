title: 7 Days to Zelda, Day 1: The pitch. 7 Days to Die, but it's Hyrule.
slug: 7dtz-devlog-01-the-pitch
excerpt: How a group-text joke about zombies in Ocarina of Time turned into a feasibility study, a first Linux build, and a design doc for a co-op horde mod.
tags: 7 Days to Zelda, devlog, Ship of Harkinian, game design, co-op
feature_image: img/hyrule-third-night.png
publish: Day 1

![7 Days to Zelda logo](img/7-days-to-zelda-logo.png)
*FIG 1-0 — The logo we ended up with. Blood moon, Master Sword, three sharpened stakes.*

This is the first of seven devlogs, one for each day of the week, about a thing my friends and I have been calling **7 Days to Zelda**: Ocarina of Time, co-op, with the dead coming for your base every few nights. Seven days, seven posts. It felt right.

Quick honesty up front, because it matters for the whole series: most of the code was written by AI agents (running through my Paperclip setup). I pitched it, made the design calls, played it, filed the angry bug reports, and fixed a few things myself (you'll get those stories on Day 4). Where "testing" meant an agent driving a browser with scripted inputs instead of a human with a controller, I'll say so.

## 1. The pitch

It started the way these things always start: texting the boys. The pitch, as the feasibility doc recorded it:

> Ocarina of Time world, co-op, zombies attacking on a 3-day cycle, building/crafting from world materials (Deku sticks, bark, plants). Private server + TeamSpeak only.

Basically 7 Days to Die, but it's Hyrule. Scavenge by day, build walls, survive the horde night. Except the horde is Stalchildren and ReDeads and the walls are around Link's treehouse.

![Concept art: a wooden stockade in Hyrule Field under a red moon, four Links inside, ReDeads and Stalchildren approaching](img/hyrule-third-night.png)
*FIG 1-1 — The blood-moon fort concept art that later became the homepage background. Four Links with different-colored fairies behind a palisade, Gibdo-looking mummies and Stalchildren closing in, Hyrule Castle behind. This is the vibe, not a screenshot.*

The question for Day 1 was simple: is this a weekend project, a year-long project, or impossible?

## 2. Feasibility: what already exists

I pointed an agent at the question on 2026-09-30 and it came back with a findings list. The short version is that a surprising amount of the hard part already existed, spread across three open-source layers.

![Layer diagram: Ship of Harkinian at the bottom, Anchor co-op, the True Co-op fork, and 7 Days to Zelda patches on top](diagrams/devlog-01-stack.svg)
*FIG 1-2 — The stack. We only wrote the top layer.*

### 2.1 Spec table: the findings

| Question | Finding |
| --- | --- |
| Does OoT co-op exist? | Yes. **Anchor** (client/server co-op) merged into mainline Ship of Harkinian in Jan 2026. Syncs saves, flags, items. **Does not sync enemies**: each player fights their own copies. |
| Shared enemies? | Yes, as a fork: **bghill95/OOT-True-Co-op**. Host-authority enemy mirroring on top of Anchor. Gohma is the only boss adapter. |
| Do night spawns sync? | Per the fork's test guide, dynamic spawns sync, including Stalchildren at night. Relevant for hordes. |
| What's broken in the fork? | Enemies still target only the authority player's Link. Enemy SFX/particles only play on the authority's screen. Floormasters don't mirror. |
| 3-day cycle? | That's Majora's Mask, not OoT. But OoT's save already counts days (`totalDays`, ticks at dawn), so a horde-night counter is a small hook. |
| Build on the 3DS versions? | No. No decomp, no PC port. |
| Steal 7 Days to Die's code? | Can't: closed-source Unity C#. Steal the *design* from its XML instead: gamestage wave scaling, block upgrade tiers, noise heat map. |

> **NOTE** Majora's Mask has its own PC port (2 Ship 2 Harkinian), but its co-op is alpha and outdated. The doc parked a fun idea for a Majora sequel anyway: the base is the only thing that survives the Song of Time.

### 2.2 Walls, hardest first

The most useful part of the doc was a ranking of what would actually hurt. Not features, walls. Hardest first:

![Four stacked bars ranking the hard problems: enemy sync, building, wave AI, crafting](diagrams/devlog-01-walls.svg)
*FIG 1-3 — The four walls, from the feasibility doc. Crafting, the thing that sounds like the whole game, is the easy one.*

1. **Horde-scale enemy sync.** The fork mirrors enemies, but everything still chases the host. A horde that ignores three of the four players isn't a horde.
2. **Building.** OoT's collision is static per scene. Anything you place has to be a dynamic-collision actor. Actors die when a scene unloads, so the base needs its own save data on the side. Actor caps need raising.
3. **Wave AI.** OoT enemies don't pathfind, and they definitely don't attack structures.
4. **Crafting.** Easy. A menu and a separate materials store.

That ordering ended up shaping the whole week. Day 2 is entirely about wall #1.

The first proposed base was Lon Lon Ranch: walled, one gate, in Hyrule Field where Stalchildren already spawn at night. That didn't survive the design pass (more below), but it was a good first guess.

## 3. The first build, and a one-line bug

Same day, the agent cloned OOT-True-Co-op at commit `b738c76` and did a Linux Release build on Ubuntu 24.04 with GCC 13. It built and linked (`soh.elf` plus `soh.o2r`). Not run, because it was a headless box with no ROM, but it linked.

Getting there took three fixes. Two were environment noise (a `stb_image.h` download that came back empty through the sandbox proxy, and Ubuntu's libzip package needing its tools installed for CMake to configure). The third was a real bug in the fork.

The fork is Windows-first, built with MSVC. Two Anchor headers include `z64.h` inside an `extern "C"` block, and `z64.h` pulls in `<memory>` when compiled as C++. MSVC shrugs. GCC refuses, with "template with C linkage". The fix is to include `<memory>` first so it's already parsed with C++ linkage by the time the extern block runs. This is patch 0001 in our series, `0001-Anchor-include-memory-before-extern-C-z64.h-in-CoopW.patch`:

```diff
--- a/soh/soh/Network/Anchor/CoopWarp.h
+++ b/soh/soh/Network/Anchor/CoopWarp.h
@@ -2,6 +2,7 @@
 #define NETWORK_ANCHOR_COOP_WARP_H
 #ifdef __cplusplus
 
+#include <memory> // must precede extern "C": z64.h pulls in <memory> under C++ (GCC rejects templates with C linkage)
 extern "C" {
 #include "z64.h"
 }
```

Same line goes into `EnemySync.h`. Two insertions, two files. The doc flagged it as upstreamable as-is. It's the least exciting patch in the series, and the first one.

The same doc found the other piece of plumbing: the Anchor relay server ships as a stock Docker image. So "private server" was one `docker run` away. Getting browsers to talk to it is a Day 3 story.

## 4. The design decisions

The agents started on netcode right away (Day 2), but by 2026-10-02 there was enough working that I had to decide what the game actually *is*. That became the spec in #3870, and its "Design decisions" section opens with a line I like: where anything below differs, this section wins.

### 4.1 The story, with raids added

No separate survival map. You play Ocarina: explore, solve puzzles, run dungeons, and gather materials while you do. The raids come to you.

This bumped into a fun fact about OoT. In vanilla, the day/night clock only runs in four scenes: Hyrule Field, Lake Hylia, Gerudo Valley and Hyrule Castle grounds. It's frozen everywhere else, Kokiri Forest included. The mod's raid clock gets outdoor scenes moving, but dungeons and interiors stay frozen. So **a dungeon is always a safe time-out.** I love that as a rule.

### 4.2 Progression comes from tools and bosses, not nights

In 7 Days to Die you unlock stuff by leveling. Here, unlocks check what's in your save: items owned, upgrades, boss flags. First pass:

| Unlocked by | Gathering it opens | Recipes it opens |
| --- | --- | --- |
| Start (Kokiri Sword) | Fiber, Wood | Workbench, barricade, torch |
| Deku Tree beaten | Bone, Rot | Spike strip, storage chest |
| Bomb Bag | Stone from bombable rocks and walls | Stone wall, bomb-flower trap |
| Hookshot | Materials on ledges out of reach | Gate that opens for players |
| Megaton Hammer | Ore from boulders | Upgrade walls in place (wood → stone → iron); fast repair |
| Silver Gauntlets | Silver rocks become an iron source | Iron wall |

The Megaton Hammer upgrading walls in place is my favorite row. Of course the hammer fixes walls.

### 4.3 The gamestage formula

This is the bit lifted most directly from 7 Days to Die. Their wave strength scales with a "gamestage"; ours is driven by story progress:

```
gamestage = 3 × dungeons cleared + heart containers + 2 × players + horde nights survived
```

Here's how it landed in code, from patch 0011 (M6 raids), trimmed:

```cpp
// gamestage = 3 x dungeons cleared + heart containers + 2 x players + horde nights survived
int32_t Gamestage() {
    static const uint8_t kDungeonRewards[] = { QUEST_KOKIRI_EMERALD, QUEST_GORON_RUBY, ...
                                               QUEST_MEDALLION_SPIRIT, QUEST_MEDALLION_SHADOW };
    int dungeons = 0;
    for (uint8_t q : kDungeonRewards) {
        dungeons += CHECK_QUEST_ITEM(q) ? 1 : 0;
    }
    ...
    int hearts = std::max(0, gSaveContext.healthCapacity / 16 - 3);
    return 3 * dungeons + hearts + 2 * CountReadyPlayers() + (int32_t)GetBase().hordeNightsSurvived;
}
```

"Heart containers" means hearts past your starting three (16 health units per heart). Each night's wave budget is 1.5 × gamestage, spent on enemies at fixed costs. Day 6 goes into that.

### 4.4 Bases go anywhere outdoors, one per era

Lon Lon Ranch lost. Three rules replaced it:

- **Outdoors only.** Any outdoor scene the raid clock runs in. A base in a dungeon would never see a raid.
- **The workbench is the base.** Your first workbench marks the center; every other piece sits within 800 units of it. That keeps the base under OoT's 50-object dynamic collision cap and tells the waves where to go.
- **One base per era.** Packing up refunds every kit. The child base and adult base are separate, and at the seven-year jump the child base becomes ruins and refunds half its materials.

The spec's last line on this is the design philosophy in one sentence: "If the game should pick the spot instead, don't: choosing and fortifying the spot is half the fun."

### 4.5 And the opening: Kokiri is boarded up

Instead of Lon Lon Ranch, the first base is the one you already live in. A new save starts with barricades across the Kokiri village exits and a workbench by Link's ladder, seeded into the save as your first base. A few Kokiri get new lines. Here's how it looks in the build:

![Saria talking to Link: "Hi, Link! Did you see the barricades? Everyone helped b" mid-typewriter](img/L11-saria-barricades-1.png)
*FIG 1-4 — Saria's new greeting, caught mid-line by the screenshot. Note the "Craft" button on the right edge of the browser build.*

![A wooden sign reading "Day 1", with Navi hovering near Link](img/L13-sign-day1.png)
*FIG 1-5 — The sign by the workbench. It just says "Day 1". That's the whole tutorial.*

Both of those shots came from an agent driving the browser build with scripted input, not from a game night. Still, it was the first time this looked like a game and not a design doc.

The milestone plan came out of the spec: M4 materials and crafting, M5 placeables and the village, M6 raids, M7 loot and Majora-style nights, M8 tuning on real game nights. My one note when the agent split it up:

> test with the live game for each stage

That one sentence changed how the agents worked for the rest of the week. A compile or a headless test stopped counting as done. More on that on Day 7.

## 5. The legal stance

This one isn't a joke, so plainly:

> **WARNING** 7 Days to Zelda follows the Ship of Harkinian model. **No Nintendo assets are distributed.** Each player supplies their own legally dumped ROM, and the game data is built from it locally. Our patches contain code, not game data.

The feasibility doc noted that in September 2026 Nintendo won a $4.5M default judgment against an r/SwitchPirates mod. That was an uncontested piracy case, not a decomp project, but it's a good reason to keep ROMs out of shared channels. We do. This is a private server for a handful of friends on TeamSpeak.

That rule held up later. During the mobile pass, the lobby's three-triangle emblem was swapped for an original fairy glyph, because that emblem is Nintendo's.

> **NOTE** The doc also flagged that the decomp/recomp community is split on AI-written code, and some servers won't promote AI-heavy projects. That's fair. So: changes stay out of shared libraries where possible, and the AI use is disclosed. Hi, this is the disclosure.

## 6. End of Day 1

Where things stood at the end of the first day:

- A pitch and a name.
- A findings table saying it was possible, mostly because Anchor and the True Co-op fork had done the hard groundwork.
- A ranked list of walls, with enemy sync at the top.
- One real bug fixed (a single `#include`) and a Linux build that linked.
- A design that turned "zombies in Hyrule" into "the story, with raids", plus a formula for how bad the nights get.

Tomorrow, Day 2: teaching Hyrule's monsters to share. Enemies that only ever chase the host, a ReDead that lets go of you the instant it grabs you, and why King Dodongo is still on the bench.

**Play it:** [https://zelda.phatt.vip/?key=<ACCESS_KEY>](https://zelda.phatt.vip/?key=<ACCESS_KEY>). Bring your own legally dumped ROM. That link has the invite key in it, so it's for subscribers only. Please don't pass it around.
