title: 7 Days to Zelda, Day 2: Teaching Hyrule's monsters to share
slug: 7dtz-devlog-02-teaching-monsters-to-share
excerpt: How 7 Days to Zelda got Ocarina of Time's enemies to chase, grab and scream at whichever co-op player is closest, not just the host.
tags: 7 Days to Zelda, devlog, netcode, Ship of Harkinian, co-op
feature_image: img/concept-bg-original.png
publish: Day 2

![Concept art of four Links in a wooden fort in Hyrule Field at night under a red moon, with ReDeads, Gibdos and Stalchildren closing in](img/hyrule-third-night.png)
*FIG 2-0 — The fantasy: the boys in a fort, the moon going red, and the dead walking in from every side. (Concept art from the homepage art drop.)*

Day 2 of seven. Yesterday was the pitch: 7 Days to Die, but it's Hyrule. Today is the first real engineering problem, and it's the one the whole idea rests on. A horde mode is pointless if the horde only wants one of you.

## 1. The problem: every monster loves the host

We're building on [bghill95/OOT-True-Co-op](https://github.com/bghill95/OOT-True-Co-op), a fork of Ship of Harkinian's Anchor co-op that adds shared enemies. One machine (the "authority") runs the real AI, and everyone else gets mirrored copies. That's a great start. The feasibility pass found the catch in the fork's own test guide, though:

> **NOTE**
> "enemies still target only the authority player's Link; enemy SFX/particles only on authority screen; Floormasters don't mirror."

So when my buddy walks up to a ReDead, the ReDead turns around and walks over to me. On his screen it's silent. That's not a horde game.

M3 was the milestone to fix that. It shipped as three commits (the Linux build fix from Day 1 plus two feature commits), **+1,633 lines across 19 files**. To be clear about who did what: the Claude agent wrote all of this code. I set the goal and reviewed what came back. The agent's own summary is honest about the status at the time:

> **WARNING**
> "Compile-verified on Linux; **not play-tested**. An independent code review found 7 real bugs, all fixed before commit."

Nobody had actually played it yet. Keep that in mind for the rest of this post.

## 2. Why "nearest player" was harder than it sounds

Here's the funny part. The fork already made host enemies *perceive* the nearest player. Distance and yaw fields were already being filled in correctly. But Ocarina's enemy code doesn't act on those fields. It acts on `GET_PLAYER(play)`, which reads the head of the PLAYER actor list. On the host, that's always the host's Link.

Every enemy in the game has its own AI that leans on `GET_PLAYER`, and rewriting all of them was never going to happen. The agent went with a trick instead: **for the length of a single enemy's update, lie to it about who the player is.**

### 2.1 The swap

The hook goes right around the one line in `Actor_UpdateAll` that runs an actor. From patch 0002, `soh/src/code/z_actor.c`:

```c
if (GameInteractor_ShouldActorUpdate(actor)) {
#ifdef ENABLE_REMOTE_CONTROL
    // Co-op nearest-player targeting: for this one update only, an
    // authority-run enemy may see a remote player's puppet as
    // GET_PLAYER (see soh/Network/Anchor/EnemyTargeting.h).
    Anchor_EnemyTargetBegin(play, actor);
#endif
    actor->update(actor, play);
#ifdef ENABLE_REMOTE_CONTROL
    Anchor_EnemyTargetEnd(play, actor);
#endif
```

`Begin` saves three things and points them somewhere else. Here it is from patch 0002, `EnemyTargeting.cpp`:

```cpp
sActive.savedHead = play->actorCtx.actorLists[ACTORCAT_PLAYER].head;
sActive.savedGrab = play->grabPlayer;
sActive.savedDamage = play->damagePlayer;
sActive.freezeSnapshot = puppet->actor.freezeTimer;

play->actorCtx.actorLists[ACTORCAT_PLAYER].head = &puppet->actor;
play->grabPlayer = RouteGrab;
play->damagePlayer = RouteDamage;
```

The "puppet" is the stand-in Link that represents a remote player on the host's screen. While the swap is active, the ReDead's vanilla AI walks toward the puppet, screams at the puppet, and tries to grab the puppet. Damage and grabs don't land on the puppet, though. They go through `RouteDamage` and `RouteGrab`, which turn them into network packets. `End` puts all three fields back **in the same call stack**, so no other actor ever sees the swapped state. As a safety net, a per-frame tick restores everything and logs `swap left active across frames` if a swap ever leaks. The test guide says that line "should never appear".

![Flow of one enemy update: select target, swap in the puppet, run vanilla update, restore and forward effects](diagrams/devlog-02-swap.svg)
*FIG 2-1 — One enemy update on the authority. Only the gold step is vanilla code. Everything around it is the wrapper.*

### 2.2 Picking a target without ping-pong

If you just take "closest player this frame", an enemy standing between two players flips back and forth every frame. So the selection is sticky (`TARGET_HOLD_FRAMES = 40`, `SWITCH_RATIO_SQ = 0.64f` in patch 0002):

| Rule | Value | Why |
|---|---|---|
| Hold after picking a target | 40 frames | stops frame-to-frame flipping |
| Challenger must be closer by | 20% (0.64 squared) | equidistant players don't steal aggro back and forth |
| Grabbed target | never switched off | a ReDead doesn't drop you mid-bite for someone 1 unit closer |
| Dead / downed players | skipped | enemies drop the downed player and go after whoever is still standing |
| Nobody alive in scene | engine default | leave the local Link alone |

*FIG 2-2 (table) — Target selection rules in `SelectAndArm`.*

### 2.3 What the remote player actually feels

Every effect is re-applied on the victim's own machine through their own engine, using a new `ENEMY_PLAYER_EFFECT` packet. Invincibility frames and cutscene guards behave exactly like single-player because they run on the victim's real Link and not on a copy. The packet kinds:

| Kind | Direction | Meaning |
|---|---|---|
| `HEALTH` (0) | authority to victim | `play->damagePlayer(play, amount)` |
| `GRAB` (1) | authority to victim | `play->grabPlayer(play, self)` |
| `FREEZE` (2) | authority to victim | ReDead scream: `freezeTimer = max(current, amount)` |
| `KNOCKBACK` (3) | authority to victim | knockback, skipped during i-frames |
| `GRAB_REFUSED` (4) | victim to authority | "you can't grab me", so the latch is dropped |
| `RELEASE` (5) | authority to victim | the enemy let go (added in patch 0018) |

*FIG 2-3 (table) — `EnemyEffectKind` values from `Anchor.h`.*

Two things were deliberately left out while an enemy is swapped: lock-on camera and rumble. A ReDead locking onto my buddy shouldn't jerk *my* camera or buzz *my* controller.

## 3. The grab latch

Grabs are where network lag actually hurts. On the host, the ReDead grabs the puppet. But the puppet's state comes from the remote player's stream, and that stream hasn't heard about the grab yet. On the next frame the stream overwrites the puppet's flags back to "not grabbed", and the ReDead lets go immediately.

The fix is a latch. From patch 0002:

```cpp
// How long the host keeps a remote player "grabbed" before its stream confirms.
// OoT logic runs at 20 Hz, so 45 frames ~= 2.25 s: well past any sane round trip.
constexpr uint16_t GRAB_LATCH_FRAMES = 45;
...
    Anchor::Instance->SendPacket_EnemyPlayerEffect(sActive.clientId, ENEMY_EFFECT_GRAB, 0, 0, 0.0f, 0.0f, 0);
    sGrabLatch[sActive.clientId] = GRAB_LATCH_FRAMES;
    // The rest of this update (and later frames, via the latch) sees the grab.
    player->stateFlags2 |= PLAYER_STATE2_GRABBED_BY_ENEMY;
```

Until the remote player's stream reports the grab (or 45 frames pass), the host keeps re-asserting "grabbed" on the puppet. Once the stream agrees, the stream is in charge again. If my buddy mashes free, his flag clears, the puppet's flag clears, and the ReDead lets go naturally on the host.

The victim can also say no. `RouteGrab` mirrors the vanilla refusal checks (dead, in a cutscene, on a horse, hanging off a ledge, already grabbed). If the victim's own engine refuses anyway, for example because he fired the hookshot as the ReDead lunged, he replies `GRAB_REFUSED` and the latch clears. The review also caught a nasty case where grab bites kept landing on someone who had already escaped. Bites are now tagged, and a victim who isn't grabbed drops them.

![Sequence: authority sends GRAB, holds the latch, victim confirms or refuses, RELEASE frees the victim](diagrams/devlog-02-grab.svg)
*FIG 2-4 — The grab latch between the authority and player B.*

### 3.1 The blocklist

Some enemies can't be pointed at a puppet safely, because what they do to "the player" reaches into the host's save, camera or scene. Those stay host-only:

| Enemy | Why it stays host-only |
|---|---|
| Wallmaster | grab triggers a respawn, which would warp the host |
| Floormaster | never mirrored at all |
| Like-Like | deletes equipment from the host's save |
| Gerudo fighters | jail transition + player cutscene |
| Poes, Poe Sisters | item / bottle checks against the host's save, one-point cutscenes |
| Skull Kid | items, rupees, player cutscenes |
| Skulltula, Shabom | contact damage only the host's Link can trigger |
| All bosses | cameras and cutscenes run off `GET_PLAYER` |

*FIG 2-5 (table) — From `IsSwapBlocked` and the M3 test guide.*

Dead Hand, its hands and Moblin were on that list in M3 too. They release a grab by clearing the flag on "the player" directly, and on a puppet that would leave the real victim stuck. Patch 0018 (Day 2) added the `RELEASE` effect: the authority remembers which enemy holds whom, and when that enemy lets go, or dies or unloads without a final update, it tells the victim. The victim's side then clears its own grabbed flag, doing the same writes a real enemy makes when it lets go.

With that in place, Dead Hand and Moblin came off the blocklist.

## 4. Making the monsters audible: EnemyFxSync

The second half of M3 deals with the silence on everyone else's screen. While the authority runs an enemy's update, `EnemyFxSync` records three things: sound calls at the enemy's position, the enemy's deferred `actor->sfx`, and particle spawns. Those ride along on the existing per-enemy `ENEMY_STATE` stream and get replayed on the mirrors.

Particles are only safe to send if their init struct means the same bytes on every machine. So it's a whitelist of **18 pointer-free effect types** (dust, sparkles, fire breath and so on). Anything holding an `Actor*` or `Gfx*` is excluded on purpose. Hit and death effects were also kept out, because they already played locally on every screen. Syncing them would have doubled them.

> **NOTE**
> Known limitation from the test guide: sounds replay at default pitch and volume, so "the few enemies that pitch-shift their sounds will sound slightly flat on mirrors."

## 5. Horde night

With targeting and sound working, the agent added the first actual horde: `HordeNight`, opt-in, every Nth night in Hyrule Field or Lon Lon Ranch. Only the authority spawns enemies, and they replicate through the fork's existing dynamic-spawn path.

| CVar | Default | Effect |
|---|---|---|
| `HordeNight` | 0 | opt-in switch |
| `HordeInterval` | 3 | every 3rd night |
| `HordeMaxAlive` | 10 | base cap, +2 per horde night, max 24 |
| `HordeSpawnFrames` | 30 | one spawn every 1.5 s |
| `HordeNightForce` | 0 | treat right now as a horde night (testing) |

*FIG 2-6 (table) — Horde night knobs from `COOP-M3-TEST-GUIDE.md`.*

The first horde is 70/30 Stalchildren to ReDeads. The second adds Gibdos and Wolfos. From the third on there are big Stalchildren too. Spawns land 250–450 units from a random living player.

My favorite detail: vanilla ReDeads are leashed. They only chase within about 150 units of their home point and otherwise walk back to it. So a horde ReDead would just stand in a field forever. The fix doesn't touch the ReDead at all. It drags the ReDead's *home* toward the nearest player at 1.2 units per frame, from patch 0003:

```cpp
if (best > 1.0f) {
    f32 step = std::min(HORDE_SHAMBLE_SPEED, best);
    a->home.pos.x += (nearest->world.pos.x - a->home.pos.x) / best * step;
    a->home.pos.z += (nearest->world.pos.z - a->home.pos.z) / best * step;
    a->home.pos.y = a->world.pos.y; // stay on its own ground; only steer in XZ
}
```

They shamble in like zombies. At dawn, ReDeads, Gibdos and Wolfos are removed, Stalchildren burrow on their own, and a `HORDE_EVENT` packet tells everyone the night is over.

## 6. The bug that was already there: post-init params

While wiring up horde spawns, the review found a bug that came with the fork. When the authority spawns an enemy at runtime, it broadcasts `ENEMY_SPAWN` with the actor's params so mirrors can spawn the same thing. But it read the params *after* `Init`, and many enemies rewrite their own params in `Init`. So mirrors spawned a different enemy than the host did:

| Enemy | What went wrong on mirrors |
|---|---|
| ReDead | set permanent switch flag 0 on death |
| Gibdo | params shifted by `1<<95` |
| Wolfos (vanilla field spawner) | killed itself / invisible on mirrors |

*FIG 2-7 (table) — Symptoms of the post-init params bug.*

The fix captures params in the `ShouldActorInit` hook, before `Init` gets a chance to change them. From patch 0003:

```cpp
COND_HOOK(ShouldActorInit, isConnected, [](void* actorRef, bool* should) {
    Actor* actor = (Actor*)actorRef;
    if (SyncEnabled() && (IsTrackedCategory(actor) || IsSyncedProjectile(actor))) {
        preInitParams[actor] = (uint16_t)actor->params;
    }
});
```

The post-init value stays the enemy's identity for matching. Only the broadcast uses the pre-init copy. The agent flagged it as upstreamable on its own. The horde's own ReDead params also use a temp switch flag (`0x3F01`), so a horde kill can't flip a permanent, Anchor-synced flag in Hyrule Field.

## 7. Who's in charge? Lowest clientId

The authority rule is simple: **the lowest client id in the scene runs the enemies.** If the host leaves Hyrule Field or dies mid-horde, the next lowest takes over. Spawning continues, existing enemies stay, and there's no second "Horde night" banner. (Day 3 covers what this did to phones. A backgrounded phone with the lowest id froze every enemy in the room.)

## 8. King Dodongo: spec'd, not built

Bosses all stay host-only. King Dodongo got a written spec for a co-op adapter, but nobody built it. Two reasons:

1. **Bomb inhale.** `BossDodongo_AteExplosive` scans the *host's* explosive list near his mouth. Bombs are local actors, so only the host could ever stun him. Fixing that needs a "bomb near mouth" request from the thrower's machine.
2. **Private particles.** His fire breath uses his own effect array, not `EffectSs`, so EnemyFxSync can't carry it.

The plan was to gate the phases the way the fork already does for Gohma: local intro cutscene, mirrored fight, streamed phase edges, local death cutscene. It's still on the list for Day 7.

## 9. So did it work?

Honest answer: when M3 landed, it had compiled and passed a review, and that's all. The test guide said so in bold. It only got exercised for real once the browser build existed (Day 3) and the base-building milestones put enemies on top of it. Most of the "testing" in this project was an agent driving a browser with scripted input. The raid screenshots in later posts come from those runs, like this one of a Stalchild at the village workbench.

![A Stalchild at night in the Kokiri village, standing next to the green-clad Link with a Craft button and minimap on screen](img/live-S17-night-village.png)
*FIG 2-8 — A Stalchild at the workbench during a raid (M6 live run). It's still the same targeting code from this post doing the chasing.*

Tomorrow: getting all of this into a browser tab, which meant replacing TCP with a WebSocket.
