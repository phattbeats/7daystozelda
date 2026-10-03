title: 7 Days to Zelda, Day 7: How a pile of agents built a Zelda mod (and what's next)
slug: 7dtz-devlog-07-how-agents-built-it
excerpt: The last day of the week: how Paperclip issues, a live-game gate, a two-browser test rig and a GPU-backed headless Chrome turned AI agents into a Zelda mod team, and what's still on the list.
tags: 7 Days to Zelda, devlog, AI agents, Paperclip, process
feature_image: img/home-logo.png
publish: Day 7 (see schedule)

---

Day 7. In *7 Days to Die* this is horde night. Here it's the night I explain how this got built, because "an Ocarina of Time co-op horde mod in a browser in about three days" needs an asterisk.

The asterisk: I didn't write most of the code. AI agents did, Claude running inside Paperclip, mostly one agent called Vision Quest. I directed, I play-tested, I complained a lot, and I fixed a few of the nastiest bugs myself (the ROM extractor, the texture table and the audio buffer, all back in [Day 4](/7dtz-devlog-04-the-terrifying-kokiri/)).

## 1. Issues as milestones

Everything lived as Paperclip issues. The epic was PHA-3856. The game design spec was PHA-3870, and when it was done the agent split it into a chain:

> Split into milestones, chained in order: PHA-3871 M4 (todo, starts now) → PHA-3872 M5 → PHA-3873 M6 (first playable raid) → PHA-3874 M7 → PHA-3875 M8.

Every milestone sat behind its own setting (`gSevenDays.Enabled`, then `gSevenDays.Raids`, `gSevenDays.Loot`, `gSevenDays.Nights` and so on) and had to leave the old `COOP-M3-TEST-GUIDE.md` passing. Side quests got their own issues and ran in parallel: PHA-3901 for fairy and tunic colors, PHA-3904 for real OoT art, PHA-3906 for the logo, PHA-3902 for the GPU.

The single most useful thing I typed all week was one line on the spec, at 4 AM:

> test with the live game for each stage

The agent turned that into a gate in every milestone description:

> Not done until this stage is tested in the live game: run the built web bundle in a real browser and play the stage end to end. Headless tools/webtest/ runs alone do not count, and neither does a compile.

![Milestone pipeline with the live-game gate](diagrams/devlog-07-milestone-gate.svg)
*FIG 7-1 — The milestone chain and the four steps each one had to clear. Step 3 is the gate I added.*

After that, every close-out came with screenshots and a list of what wasn't actually proven.

## 2. The test rig

**Layer one: `tools/webtest/`.** This is the harness that found and verified the Day 4 bugs. Its README describes it as real Chromium (Playwright, software WebGL) running against the bundle behind a SWAG-equivalent nginx and a local Anchor server. Each milestone got its own script (`m4-test.py` to `m7-test.py`). Two browser profiles, A and B, join one room and the script checks that both sides agree: a shared bush pays once, a cache opened by B stays opened for A, a barricade placed by B comes back after B reloads. Passing `0` runs the same thing with 7 Days to Zelda switched off, as a regression check.

Saves are created by scripted input. This is straight from the webtest README, trimmed:

```sh
# tools/webtest/README.md: creating a save for profile A
python3 drive.py A "http://.../?key=testkey" o2r cfg solo wait:35 \
  key:space wait:6 key:x wait:5 key:x wait:8 key:x ... wait:30 shot:game
```

A robot pressing X through the intro on a timer. It works.

**Layer two: the live-game gate.** For that the agent drove a *headed* Chrome inside browserless and played the build. Here's the part I want to be really clear about: an agent with scripted keyboard input is not a person with a controller. The agents were good about saying so. Almost every close-out had a line like this one from M6:

> An agent played it with scripted input, not a person, and used the test shortcuts listed below.

And then the actual shortcut list. From M6: "The Kokiri Sword was granted and the Deku Tree flags were set instead of playing Gohma. Barricade kits were granted. I retried nights by reloading." From the M7 review: "The raid wave was ended with a test command, not fought to the end."

> **NOTE** — Those gap lists are the most valuable thing the agents wrote. They tell me exactly which parts of the game nobody has played yet. Spoiler: a lot of it. That's what M8 is for.

![Build and test rig](diagrams/devlog-07-test-rig.svg)
*FIG 7-2 — Build tree to bundle, then the two test layers. The gold box is the live-game gate.*

## 3. A build tree in a tarball

The full web build needs 12 GB of RAM because of the randomizer tables, which never change. So we froze the whole compiled tree into `zelda-buildtree.tar.gz` and let agents rebuild incrementally from it. From the buildtree Dockerfile in the bundle:

```dockerfile
# The web build tree, frozen at the exact paths it was built in, so builds are
# incremental (a 30-60 s relink) instead of a from-scratch 12 GB build.
FROM ubuntu:24.04
...
# ADD unpacks a local .tar.gz straight into the image: one layer, no second copy.
ADD zelda-buildtree.tar.gz /
COPY build-web.sh /usr/local/bin/build-web
WORKDIR /root/OOT-True-Co-op
CMD ["build-web"]
```

The paths have to stay identical because the ninja files are absolute; move the tree and it rebuilds from zero. The design doc says a one-file change rebuilds "in about 26 s on any Docker host." The agent's own container had no Docker, so it relocated the tree and got "about 30 seconds" per incremental build.

Parallel agents did step on each other. The colors run built in its own copy of the tree "because another run was using" the main one, and at one point the art run's cleanup "accidentally killed the `node server.js`" the M7 review was testing against and dropped two player connections. It restarted the server and apologized on the issue.

## 4. The commit identity rule

Every 7 Days to Zelda patch (0009 to 0019) is authored as `phattbeats`, with no AI trailers. When I shipped my audio fix, the agent flagged it:

> One note: the 0007 patch has `Co-Authored-By` and `Claude-Session` trailers. Strip them before committing it to any `phattbeats/*` repo.

The patch headers show the rule held. Here's the top of patch 0019:

```
From 76965437270509fde7fc56ae8a236be3f12e0dfc Mon Sep 17 00:00:00 2001
From: phattbeats <...>
Date: Fri, 2 Oct 2026 17:10:20 -0400
Subject: [PATCH 19/19] 7 Days to Zelda: custom fairy gradient and tunic colors
 that others see (PHA-3901)
```

Patches 0001 to 0008 (the netcode and web port) carry my own name. Either way, the identity on a patch is repo hygiene, not a claim about who typed it. This series exists to say who did what.

## 5. The GPU that didn't help (and the bug that did)

The agents' browser was browserless, rendering with SwiftShader on the CPU. I asked for GPU passthrough (PHA-3902). The research verdict was "yes, this is possible," and after a couple rounds of me clicking around in Unraid ("done. ch-ch-check it.") Chrome was on the Quadro K2200. Only one path worked: ANGLE on Vulkan. Plain `--use-gl=egl` stayed on SwiftShader. The final container setting:

```
DEFAULT_LAUNCH_ARGS=["--use-angle=vulkan","--enable-features=Vulkan","--ignore-gpu-blocklist","--enable-gpu"]
```

chrome://gpu then reported `ANGLE (NVIDIA, Vulkan 1.3.278 (NVIDIA Quadro K2200 ...))` as active, with WebGL hardware accelerated.

Then I looked at the CPU graph:

> i was really hoping that woudl lower CPU usage for browserlss.

The agent found browserless at **~420% CPU**. Three sessions were leftover 7 Days to Zelda playtest tabs, game loops still running nonstop. It closed them over CDP and the container dropped to **3–15%**.

> **WARNING** — A GPU moves drawing off the CPU. It does nothing for JavaScript, WASM game logic, audio or networking, which is what a running SoH tab actually burns. If your headless box is pegged, count your tabs first. The prevention (always close the browser, one-shot calls, session timeouts) is filed as PHA-3909.

## 6. The art pass: OoT models only

Through M5 and M6 the base pieces were placeholder shapes. My rule for PHA-3904: models from OoT or Majora's Mask only, never online assets, and Majora only with my approval. The agent swapped in:

| Placeable | Model (from OoT) | Collision box | Menu icon |
| --- | --- | --- | --- |
| Barricade | Iron horse-jump fence with brick posts | 120 x 20 x 48 | Deku Shield (unchanged) |
| Workbench | Dungeon shop shelves, half size | 110 x 32 x 52 | Poacher's Saw (was Megaton Hammer) |
| Storage chest | The real treasure chest, lid closed | 52 x 40 x 44 | Dungeon map chest mark (was Bomb Bag) |
| Spike strip, sign | Already OoT models | unchanged | Skull Mask (spikes, unchanged) |

Below half HP the fence "sags and leans, and it still darkens."

![Kokiri village with a chest, fence, sign and spike strips](img/V17-pieces.png)
*FIG 7-3 — Placed pieces in the village: the chest on the left, the fence and sign behind Link, spike strips by Saria.*

![Workbench Base tab with the new icons, iron fence behind the menu](img/V5-base.png)
*FIG 7-4 — The Workbench Base tab with the saw and chest icons. The iron fence barricade is visible behind the menu.*

I asked whether anything from Majora's Mask could apply. The agent's answer was the right one. Players only extract Ocarina of Time from their own ROM, so MM models would mean either every player brings a second ROM, or "we pull the models out ourselves and ship them in our game files. That is the 'online assets' problem you ruled out: we'd be distributing Nintendo's data." I said: "barricade can be the fence, resized as needed." Done.

This was also, in the agent's words, "the first time 7 Days to Zelda itself is live." There's now a **7 Days to Zelda** checkbox in the lobby, off by default.

## 7. N64-pilled

My entire brief for PHA-3906 was "make the homepage more nostalgic and n64 pilled." The agent added the logo, the blood-moon fort concept art with CRT scanlines, a four-color controller stripe, and N64 buttons: Join is the blue A button, Solo is the green B.

Then I asked "replace it in game?" and it did, without touching the game binary:

![Override layering for the title logo](diagrams/devlog-07-o2r-override.svg)
*FIG 7-5 — The title textures live in soh.o2r, which overrides the player's own oot.o2r.*

![In-game title screen with the 7 Days to Zelda logo and PRESS START](img/ingame-title-live.png)
*FIG 7-6 — The OoT title attract loop with the 7 Days to Zelda logo in place of the shield, flames traced around it.*

It's rendered at native 160×160 "because the 4× high-res version came out scrambled in this engine." The catch, flagged right in the issue: "a new bundle deploy ships a stock soh.o2r," so the title pack has to be re-copied after every deploy.

> **NOTE** — The SoH model still holds: players supply their own legally dumped ROM, and no Nintendo assets are distributed. That rule is exactly why the art pass stayed OoT-only.

## 8. b-mech's black fairy

Late on October 1, b-mech wanted a black fairy. So I asked: "can you make a gradient fairy picker so that b-mech can make his black." Eight minutes later the lobby had a Custom option starting at black, plus an honest caveat: "If pure black shows as no glow, pick a near-black like `1A1A1A`."

That grew into PHA-3901: separate fairy core, fairy aura and tunic colors that *other players* see. The protocol change is small and backwards compatible. From patch 0019, the client-state parser:

```cpp
// Older clients only send `color`: it is their aura and their tunic, with a white core.
j.contains("fairyInner") ? j.at("fairyInner").get_to(client.fairyInner) : client.fairyInner = { 255, 255, 255 };
j.contains("fairyOuter") ? j.at("fairyOuter").get_to(client.fairyOuter) : client.fairyOuter = client.color;
j.contains("tunic") ? j.at("tunic").get_to(client.tunic) : client.tunic = client.color;
```

![Purple-tunic Link with a blue fairy, seen from another player's screen](img/n4-A-sees-other-zoom.png)
*FIG 7-7 — Player A's view of player B: purple tunic, blue fairy. Two headed Chrome profiles in one room.*

My reaction on the issue: "woooooooooooo!" Still open: the agent measured live color updates at 1.1–2.8 s, but the test browsers ran at 2–4 fps, so the under-one-second target needs checking on real hardware.

## 9. What's next

Here's the board as of Day 7.

| Item | State | Done when |
| --- | --- | --- |
| M8: tuning (PHA-3875) | Blocked on us | Costs, HP, unlocks, loot and wave curves set by real game nights |
| M9: the world reacts (PHA-3913) | Backlog | Towns outside Kokiri get lines that change with raids, some get boarded up, townsfolk hide on raid nights, Gossip Stones and Navi give base tips |
| King Dodongo boss sync | Backlog | Two players beat him together; either player's bomb stuns him |
| Windows desktop build | Backlog | A Windows player joins a browser player's room |
| Real-phone pass | Backlog | One full horde night on iPhone Safari and Android Chrome without a crash |
| Real Bluetooth controller pass | Backlog | Link moves, Z-targets and uses C-items from a controller |
| M7 review findings 1–4 | Waiting on me | Low-severity; "Say the word and I'll fix 1-4" |

M9 exists because I asked the obvious question: "right now we dont have anything happening in any towns or NPC talking about it outside of kokiri villaige, correct?" Correct. Kokiri Forest is the only place NPCs react. Raids, the clock and loot work everywhere else, but nobody in Kakariko has noticed the moon is red.

And my two open asks, which nobody has answered yet: "places to purchase materials?" and "any other crafting or base items we could add? like maybe village houses or something?"

M8 is the one an agent can't do. Every number in this game was picked on paper and checked by a bot with cheats on. The fix is the boys, a room link, and a real night in Kokiri Forest. See you at dusk.

**Play it:** [https://zelda.phatt.vip/?key=<ACCESS_KEY>](https://zelda.phatt.vip/?key=<ACCESS_KEY>). Bring your own legally dumped ROM. The link carries the invite key, so keep it among subscribers.
