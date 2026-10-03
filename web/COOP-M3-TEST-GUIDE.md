# Co-op M3 — Play Test Guide (targeting, enemy sounds/effects, horde night)

Builds on the M2 + Gohma guide (`COOP-TEST-GUIDE.md`); same two-instance rig, same Anchor relay.
**None of this has been play-tested yet** — it compiles and passed a code review, that's all.
Every feature has a kill switch, so if something misbehaves you can isolate it in seconds.

## Kill switches and settings

All under `CVars.gRemote.Anchor` in each instance's `shipofharkinian.json`:

| CVar | Default | What it does |
|---|---|---|
| `EnemyTargeting` | 1 | Enemies chase/attack the nearest living player. 0 = old behavior (perceive nearest, attack host). |
| `EnemyFxSync` | 1 | Enemy AI sounds and particles play on non-host screens. |
| `HordeNight` | 0 | Horde-night game mode (opt-in). |
| `HordeInterval` | 3 | Every Nth night is a horde night. |
| `HordeNightForce` | 0 | Treat right now as a horde night (testing). |
| `HordeMaxAlive` | 10 | Base enemy cap; +2 per horde night, max 24. |
| `HordeSpawnFrames` | 30 | Frames between spawns (30 = 1.5 s). |
| `EnemySyncVerbose` | 0 | Verbose log lines (`[EnemySync]`, `[HordeNight]`). |

Horde night only runs in Hyrule Field and Lon Lon Ranch, and only on the instance that owns enemy AI (the "authority": lowest client id in the scene).

## Fastest way to test everything at once

1. Both instances: `HordeNight = 1`, `HordeNightForce = 1`, `EnemySyncVerbose = 1`.
2. Both players walk into Hyrule Field together.
3. Expected: a "Horde night" notification and a ReDead scream on **both** screens; enemies start appearing 250–450 units from a random player every ~1.5 s.
4. Note: forced during the day, Stalchildren burrow immediately (vanilla daytime behavior). ReDeads, Gibdos and Wolfos stay. For the full mix, test at night (Sun's Song, or wait).

## Test script

1. **Nearest-player targeting.** B (not the host) walks toward a ReDead while A stays far away. Expected: the ReDead screams at **B**, B freezes on B's screen, the ReDead walks to B and grabs B. A's screen shows the same. A must **not** freeze, take damage, hear a lock-on chime, get a camera jerk, or feel rumble.
2. **Grab and escape.** B mashes free. Expected: the ReDead lets go on both screens. Bites while grabbed cost B hearts, never A.
3. **Refused grab.** B pulls out the hookshot and fires it as the ReDead lunges. Expected: no grab; the ReDead doesn't keep biting the air. Log on the host: no runaway bites.
4. **Stickiness.** A and B stand roughly equidistant from a Stalchild. Expected: it commits to one player and doesn't ping-pong. It switches only when the other is clearly (~20%) closer.
5. **Downed player.** B dies/goes down. Expected: enemies drop B and go after A.
6. **Sounds and particles on B's screen.** Stand B near enemies the host's game is running: Stalchild footsteps, ReDead moans and scream, Wolfos growls should be audible on B's screen. Dodongos (Dodongo's Cavern) breathing fire: the flame particles should show on B's screen.
7. **No doubled sounds.** Hit sounds, death sounds, death puffs should play exactly once on each screen (they were already local).
8. **Shambling.** During a horde, ReDeads should drift toward the nearest player instead of standing where they spawned.
9. **Dawn.** At dawn (or flip `HordeNightForce` to 0): "Dawn" notification on both screens; ReDeads/Gibdos/Wolfos vanish on both; Stalchildren burrow.
10. **Authority handover mid-horde.** The host leaves Hyrule Field (or dies) during a horde. Expected: B becomes authority, spawning continues, **no** second "Horde night" announcement, and the existing enemies do **not** vanish.
11. **Wolfos/ReDead spawn parity.** Kill a horde ReDead on B's screen. Expected: no permanent switch flag set. Previously mirrors respawned these enemies with already-mangled params, which made a mirror's ReDead set switch flag 0 permanently on death and made Wolfos invisible on mirrors.

## Known limitations (this round)

- **Bosses** still attack only the host (all bosses drive cameras and player cutscenes off "the player"). King Dodongo specifically needs bomb sync before co-op works; see the project notes.
- **Excluded from remote targeting** (they still chase only the host): Wallmaster, Like-Like, Gerudo fighters, Poes, Poe Sisters, Skull Kid, Floormaster, Skulltula, Shabom, Dead Hand + hands, Moblin. Either their attacks touch the host's save/camera/scene, or they release grabs in a way that doesn't reach the victim yet.
- ReDead lock-on is skipped for remote victims (it would have moved the host's camera).
- Sounds replay at default pitch/volume; the few enemies that pitch-shift their sounds will sound slightly flat on mirrors.
- A ReDead holding someone when it's removed at dawn leaves them in the grabbed pose until they mash out.
- Horde composition, caps, and spawn distance are first guesses. Tune with the CVars.

## If something looks wrong

Turn the relevant CVar off first to confirm which feature it is. Logs: `test-a\logs\Ship of Harkinian.log` / `test-b`. New canaries:
- `[EnemyTargeting] swap left active across frames` — should never appear; means a swapped update didn't restore.
- `[EnemyTargeting] PLAYER list head changed` — an enemy inserted into the player list mid-update; report which actor id.
- `[HordeNight] spawned id=… params=…` — verbose only; one line per spawn.
