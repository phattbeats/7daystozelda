title: 7 Days to Zelda, Day 6: Raids under the hood
slug: 7dtz-devlog-06-raids-under-the-hood
excerpt: How a raid night in 7 Days to Zelda picks its enemies, sneaks them in out of view, routes them at your workbench, chews through barricades, and keeps two different "hosts" in agreement.
tags: 7 Days to Zelda, devlog, netcode, game design, Ship of Harkinian
feature_image: img/live-S17-night-village.png
publish: Day 6

---

Day 6. Yesterday was the player's tour. Today we open the hood on the night itself. Almost all of this lives in one new file, `soh/soh/SevenDays/Raids.cpp`. Patch 0011 added it at 1,434 lines, and patch 0014 tuned it after I played it and complained. As with the rest of the project, an AI agent (running through Paperclip) wrote the code. I wrote the spec's wish list and did the yelling.

![A Stalchild right next to Link by the ladder and sign in Kokiri Forest at night, Saria at the left edge](img/live-S17-night-village.png)
*FIG 6-1 — Night in the village. A Stalchild has made it to the workbench area by Link's ladder.*

## 1. The budget: gamestage buys enemies

7 Days to Die scales its hordes with a "game stage" number. We stole that. The formula is right in the code.

From `Raids.cpp` (patch 0011):

```cpp
// gamestage = 3 x dungeons cleared + heart containers + 2 x players + horde nights survived
int32_t Gamestage() {
    ...
    int hearts = std::max(0, gSaveContext.healthCapacity / 16 - 3);
    return 3 * dungeons + hearts + 2 * CountReadyPlayers() + (int32_t)GetBase().hordeNightsSurvived;
}
```

So the extra hearts count, not the three you start with, and every player in the room adds 2. The night's budget then buys enemies off a price list.

| Enemy | Cost | Barricade drain (HP/s) |
|---|---|---|
| Stalchild | 1 | 3.0 |
| Big Stalchild | 2 | 5.0 |
| Keese | 1 | 1.5 |
| Wolfos | 3 | 6.0 |
| ReDead | 4 | 8.0 |
| Gibdo | 5 | 10.0 |

*TABLE 6-1 — The `RaiderDef` table in patch 0011.*

Each wave rolls from a weighted list that depends on which raid number this is. The spawner only rolls among enemies it can still afford. When nothing is affordable, the budget counts as spent. HordeNight's live cap stays in force on top of that: 10 alive, plus 2 per horde, up to 24.

The first version used 1.5 × gamestage for every raid. Then I played it, and my whole review was:

> "it needs to be easy-ish the first time. option to select days when yoiu start a game."

The agent reopened M6, and patch 0014 added a ramp.

From `Raids.cpp` (patch 0014):

```cpp
static int32_t WaveBudget(int32_t gamestage, int32_t raidNo, bool prologue) {
    if (prologue) {
        return 2 + CountReadyPlayers();
    }
    return raidNo <= 2 ? gamestage : (int32_t)(gamestage * 1.5f);
}
```

Gohma's night, the first real raid, is now 2 Stalchildren plus 1 per player, with no Keese. At most 2 are alive at once, and barricades take half damage. Raid 2 buys 1× gamestage of Stalchildren, Keese and Wolfos. ReDeads join at raid 3 and Gibdos at raid 4. The Kokiri Sword dusk before all of that is always exactly 2 Stalchildren.

The "select days" half became a picker on new saves. It asks "How often do raids come?" and offers every night, or every 2, 3, 5 or 7 days. The choice is saved with the base, and you can change it later on the workbench's Base tab.

![The raid interval picker on a new save](img/R04-picker.png)
*FIG 6-2 — The interval picker. Only the room owner sees it.*

## 2. Where they come from, and how they find you

![Top-down diagram of the spawn ring around the workbench](diagrams/devlog-06-ring-routing.svg)
*FIG 6-3 — Spawn ring, retarget radius and the stuck check.*

OoT enemies don't pathfind, and a base can go anywhere. So the routing is deliberately dumb, with one safety net:

- **Spawn ring.** At raid start the authority samples up to 16 points on a ring 600–900 units from the workbench. Each point needs a floor raycast hit, can't be water, steep, void-out or damage floor, and sits within 300 units of the workbench's height. With no base in the scene, the ring goes around a random living player instead.
- **Out of view.** A raider only spawns on a sample nobody can see. For your own Link, "see" means a roughly 50° camera cone with line of sight. Other players' cameras don't exist on your machine, so for them it's a 60° facing cone from their head.
- **Walk at the bench.** Walking raiders (Stalchildren, Wolfos) are steered by overwriting their idea of where "the player" is.
- **300u override.** If a real player is within 300 units and roughly on the same level, the raider fights them instead.

From `Raids.cpp` (patch 0011), the steering hook:

```cpp
// Raiders that walk (Stalchildren, Wolfos) steer by yawTowardsPlayer: point it at
// the workbench unless a player is within 300.
static void OnRaiderPerception(void* actorRef, bool* should) {
    ...
    Vec3f goal = sDir.center;
    f32 xz = Actor_WorldDistXZToPoint(a, &goal);
    if (a->id == ACTOR_EN_WF) {
        xz = std::min(xz, 200.0f); // close enough to come out of the ground and run at it
    }
    a->yawTowardsPlayer = Actor_WorldYawTowardPoint(a, &goal);
    a->xzDistToPlayer = xz;
    ...
}
```

I love this trick. The Stalchild AI is untouched; we just lie to it about where Link is. ReDeads, Gibdos and Keese are leashed to a home point instead, so they get their home dragged toward the goal with HordeNight's `ShambleToward`, generalized from the old `ShambleTowardPlayers`.

**Stuck check.** Every 5 seconds the authority asks each raider: did you get 20 units closer to your goal? If not, and it isn't in melee range, pressing a barricade, or visible to anyone, it gets moved to another unseen ring sample.

> **NOTE** Keese are exempt. They circle a moving home point and fly over walls, so "not making progress" means nothing for them. Hand-made routes for spots like the Lon Lon Ranch gate are in the spec as a later upgrade, not something that exists.

## 3. Barricade damage

Barricades are 100 HP. Every tick, the authority checks each raider against each destructible piece's box. It rotates the raider into the piece's local frame and pads the box by 32 units for body radius.

From `Raids.cpp` (patch 0011, with 0014's first-raid scale):

```cpp
if (fabsf(lx) < info.halfX + r && fabsf(lz) < info.halfZ + r) {
    f32& owed = sPendingDrain[id];
    owed += DrainFor(a) * drainScale * dt;
    if (owed >= 4.0f) {
        int amount = (int)owed;
        owed -= amount;
        DamagePlaceable(id, amount);
    }
}
```

Damage builds up and only goes out in chunks of at least 4 HP. `DamagePlaceable` is where the network comes in. If you're the room owner, you apply it. If not, you send `BASE_HP {id, hp}` to the owner, which turns it into a numbered `BASE_DELTA` for everyone. In the live run one barricade went 100 → 80 → 68 → 48 → 28 → 4 → broken.

![Close-up of Stalchildren pressed against a wooden barricade, Link at two hearts](img/live-S10-hit-11-68.png)
*FIG 6-4 — Raiders pressed against a barricade during the M6 live run.*

## 4. Two authorities

![Diagram of enemy authority and room owner exchanging BASE_HP and BASE_DELTA](diagrams/devlog-06-authority.svg)
*FIG 6-5 — Who decides what during a raid.*

This was the design call that made the rest possible. The spec (#3870) says both kinds of authority "already exist":

- **Room owner** (`roomState.ownerClientId`) owns base-wide state: the base, the material pool, the night schedule, dawn, penalties.
- **Scene enemy authority** is the lowest client ID in that scene (`EnemySync::IsLocalAuthority`, from Day 2's netcode). It runs anything alive in the scene: the budget, spawns, routing, the stuck check, barricade drain.

Usually that's the same person. When it isn't, the authority reports and the owner decides. The authority also sends `HORDE_EVENT` to same-scene peers every 5 seconds, including its `dayTime`. Peers snap their clock to it if they've drifted, so everyone fighting the same wave sees the same night.

| Packet | Direction | Carries |
|---|---|---|
| HORDE_EVENT (extended) | authority → same-scene peers | raid, status, night, gamestage, budget, alive, dayTime, types |
| RAID_STORY | any client → owner | sword / gohma / duskCleared (+ firstRaidCleared in 0014) |
| RAID_SCRIPT | owner → room | `to` (dayTime), why: dusk / raid / dawn |
| RAID_REPORT | authority → owner | scene, base: a raid is being fought at the base |
| RAID_LOST | any client → owner | everyone went down |
| RAID_NOTICE | owner → room | dawn reports, penalties, Navi line IDs |
| BASE_HP | authority → owner | id, hp |
| BASE_DELTA | owner → room | rev + add / remove / hp |
| BASE_STATE / BASE_REQUEST | owner ↔ joiner | the whole base; higher rev wins |

*TABLE 6-2 — Raid and base packets, from the header comments in Raids.cpp and Base.cpp.*

> **WARNING** Every one of these is JSON. The spec's rule: "Packets carry JSON, never raw structs. Desktop is 64-bit and the browser is 32-bit, and players on both share rooms." A `memcpy`'d struct with a pointer or `size_t` in it would have a different layout on each side.

## 5. The clock

Vanilla OoT only runs the clock (time speed 10) in Hyrule Field, Lake Hylia, Gerudo Valley and the Castle grounds. At speed 10 a whole day is about 4 real minutes. Kokiri Forest, where your first base sits, is frozen at speed 0. A raid game where night never comes is a short game.

So `ClockTick` does three things. During the prologue, nights are scripted: `RAID_SCRIPT` fast-forwards at speed 400, the Sun's Song's own speed. After the prologue, frozen outdoor scenes get `RaidClockSpeed`, 5 by default, about half the field's, so a day there is roughly 8 minutes. Dungeons and interiors stay frozen. And on a raid night the clock is held at 0 until the wave is cleared or `RaidHoldSeconds` (240) runs out, so a raid can't end in a minute and a half.

![Timeline of a raid night](diagrams/devlog-06-raid-night.svg)
*FIG 6-6 — Wave states and the clock hold across one raid night.*

## 6. Nobody home, and losing

**Empty base.** If the raid night passes and nobody fought at the base (no `RAID_REPORT` with `base: true`), the owner settles it on paper at dawn. Damage is (budget − defense) × 10 HP. Defense is HP/25 per barricade plus 3 per spike strip. That damage is spread over the four pieces farthest from the workbench, and the pool loses 2% per 10 damage, capped at 25%. The toast reads "The base was raided: {n} barricades lost, {pct}% of the stores taken.", or "It held." if the defenses covered it.

**Losing a night.** If everyone goes down during a raid, `RAID_LOST` goes to the owner. Every piece loses half its remaining HP and every material loses 25%: "Walls at half strength, a quarter of the stores gone." Survive instead and the pool gets +gamestage/2 Wood and +gamestage/3 Bone.

![Dawn of Day 4 card with "Survived: +3 Wood, +2 Bone"](img/G2-text-970a.png)
*FIG 6-7 — The first raid won by fighting: Dawn of Day 4, one night until the next raid (2-day interval), +3 Wood, +2 Bone.*

## 7. Saving it and late joiners

All of this sits in one `BaseState` record: rev, base centers per era, placeables with HP, and counters (`nextRaidDay`, `story`, `nightsFailed`, and from 0014 `raidInterval`). It's saved as the `sevenDays` SaveManager section, version 2 (version 1 saves still load), in every member's save file. Late joiners get it for free by riding on Anchor's existing team-state packet.

From `UpdateTeamState.cpp` (patch 0010):

```cpp
// 7 Days to Zelda: the base and the pool ride along, so late joiners get them (higher rev wins).
if (SevenDays::Enabled()) {
    payload["state"]["sevenDays"] = SevenDays::TeamStateJson();
}
```

Deltas are strict. A client only applies `BASE_DELTA` when `rev == mine + 1`. A gap means it missed one, so it sends its own copy in a `BASE_REQUEST`, the owner keeps whichever rev is higher, and answers with `BASE_STATE`.

![The "Day 3" sign after save and reload](img/live-S12-sign-day3.png)
*FIG 6-8 — After a save and page reload, the sign still reads Day 3.*

## 8. The pieces themselves

Each barricade is an `ActorDB` actor and a `DynaPolyActor` with a box built in code: 8 vertices, 12 triangles, registered with `DynaPoly_SetBgActor`, so Link and enemies bump into it like scenery.

The ceiling here is the engine's. OoT allows 50 dynamic-collision actors per scene (`BG_ACTOR_MAX`), and they share a 512-polygon / 512-vertex budget outdoors. That's why a base caps at 24 pieces, enforced in the owner's placement check.

## 9. What was actually tested

The "play" here was an agent driving headed Chrome with scripted input, not me with a controller. In M6 it used shortcuts. The sword was granted, the Deku Tree flags were set instead of fighting Gohma, and barricade kits were granted. The "survived" M6 night only ended because the 240-second hold ran out, and Link was healed 20 times. The `sword` shortcut also wasn't equipping the sword, so that bot was effectively fighting without one.

The M6.1 rerun is the honest one. It was solo, on a new file, with no heals. The bot won the dusk with the sword, then cleared Gohma's night (3 Stalchildren, never more than 2 alive) in 66 s with Link at 2 of 4 hearts at worst. Dawn came 11 s later.

![Link with the Kokiri Sword fighting two Stalchildren by the sign](img/G2-t035.png)
*FIG 6-9 — The first raid, M6.1 rules: Stalchildren only, two at a time.*

Still unseen in live play: ReDeads and Gibdos in a real raid, and raid 2, which was only spawned and checked, never fought. The day counter follows the room owner's clock, so a split party can see a night end early. Those go on the list for real game nights, which is tomorrow's post.
