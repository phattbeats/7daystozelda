# 2026 09 30 Oot Coop Horde Feasibility And First Build

---
title: OoT co-op horde mode — feasibility, first build, M3
date: 2026-09-30
updated: 2026-10-01
tags: [gamedev, zelda, soh, anchor, coop, feasibility]
status: web-build-swag-mobile-ready-untested-with-rom
---

# OoT co-op + zombies + base building

Pitch (with the boys, via text): Ocarina of Time world, co-op, zombies attacking on a 3-day cycle, building/crafting from world materials (Deku sticks, bark, plants). Private server + TeamSpeak only.

## Findings
- **Co-op exists.** Anchor (client/server co-op) merged into mainline Ship of Harkinian (SoH) Jan 2026. Syncs saves, flags, items. **Does not sync enemies** — each player fights their own copies.
- **Enemy sync exists as a fork:** [bghill95/OOT-True-Co-op](https://github.com/bghill95/OOT-True-Co-op). Host-authority enemy mirroring on top of Anchor; Gohma is the only boss adapter. Per its test guide: dynamic spawns sync (incl. Stalchildren at night — relevant for hordes); enemies still target only the authority player's Link; enemy SFX/particles only on authority screen; Floormasters don't mirror. (Targeting and SFX/particles addressed in M3 below.)
- **The 3-day cycle is Majora's Mask, not OoT.** OoT save data already counts days (`totalDays`, increments at dawn), so a horde-night counter is a small hook. Majora (2 Ship 2 Harkinian) co-op is alpha/outdated. Design hook for a Majora sequel: the base is the only thing that survives the Song of Time.
- **3DS versions as base: no.** Not on ZeldaRET's project list, no decomp, no PC port. Their look can be approximated later with SoH custom models/HD textures.
- **Dusklight** (friend's find) = Twilight Princess native port, May 2026. No co-op.
- **7 Days to Die:** closed-source Unity C#; code can't be taken. Steal the design from its XML: gamestage wave scaling, block upgrade tiers, noise heat map.
- **Walls, hardest first:** horde-scale enemy sync → building (static per-scene collision; placeables must be dynamic-collision actors; actors die on scene unload, so base needs a sidecar save; raise actor caps) → wave AI (OoT enemies don't pathfind or target structures) → crafting (easy; ImGui menu, separate materials store).
- **Proposed base map:** Lon Lon Ranch — walled, one gate, in Hyrule Field where Stalchildren already spawn at night.
- **Identity:** players distinguished by worn masks (Bunny Hood, Mask of Truth, Goron, Gerudo) as roles; Anchor syncs tunic but not worn mask yet (small add). Colored Four Swords caps need model edits (cap shares the tunic color).
- **Legal:** SoH model — no Nintendo assets distributed, user supplies ROM. Nintendo won a $4.5M default judgment against an r/SwitchPirates mod (Sept 2026; piracy case, uncontested). Keep ROMs out of shared channels.
- **Community note:** decomp/recomp scene is split on AI-coded work; some servers won't promote AI-heavy projects. Keep changes out of shared libraries (libultraship, RT64), disclose AI use.

## First build (2026-09-30)
- Cloned OOT-True-Co-op @ `b738c76` (2026-07-11), built Linux Release with `-DBUILD_REMOTE_CONTROL=1` on Ubuntu 24.04 / GCC 13. **Builds and links** (`soh.elf` + `soh.o2r`). Not runtime-tested (headless box, no ROM).
- Fixes needed:
  1. Environment: libultraship's `file(DOWNLOAD)` of `stb_image.h` came back empty through the sandbox proxy → fetched from raw.githubusercontent. Not a repo bug. Every CMake reconfigure re-downloads it; restore with `cp -p` to keep the old mtime or the whole library rebuilds.
  2. **Real fork bug:** `CoopWarp.h` / `EnemySync.h` include `z64.h` inside `extern "C"`; z64.h pulls `<memory>` under C++. MSVC tolerates it, GCC fails ("template with C linkage"). Fix: `#include <memory>` before the extern block. Upstreamable as-is.
  3. Ubuntu libzip package needs `zipcmp zipmerge ziptool` installed or CMake config fails.
- Fork is Windows-first (`launch-coop-test.bat`, two-Xbox-pad test flow). Getting a Windows build: fork the repo on GitHub, push; `generate-builds.yml` runs on push and produces Windows artifacts.
- Anchor relay: `docker run -p 43383:43383 ghcr.io/garrettjoecox/anchor:latest` → candidate for PHATT-RAID.

## M3: targeting, enemy sounds/effects, horde night (2026-09-30 → 10-01)

Patch series `oot-coop-m3-patches.tar.gz` (Linux fix + 2 feature commits, +1,633 lines across 19 files). Compile-verified on Linux; **not play-tested**. An independent code review found 7 real bugs, all fixed before commit. Test script and kill switches: `COOP-M3-TEST-GUIDE.md` in the repo.

- **Nearest-player targeting** (`EnemyTargeting.*`). The fork already made host enemies *perceive* the nearest player (distance/yaw fields) but they *acted* on `GET_PLAYER`, which is always the host. Fix: sticky nearest-living-player choice per enemy; for that one enemy's `update()`, the PLAYER list head points at the remote puppet and `play->damagePlayer` / `grabPlayer` are swapped for network routers, restored in the same call stack. Freezes and knockbacks written to the puppet are forwarded. A grab latch bridges the round trip so the host's ReDead doesn't release instantly; the victim replies GRAB_REFUSED when it can't be grabbed. New `ENEMY_PLAYER_EFFECT` packet. Lock-on camera and rumble skipped while swapped. Bosses and 13 enemies with save/camera/scene side effects (Wallmaster, Like-Like, Gerudo fighters, Poes, Poe Sisters, Skull Kid, Floormaster, Skulltula, Shabom, Dead Hand + hands, Moblin) stay host-only.
- **Enemy sounds + particles on mirror screens** (`EnemyFxSync.*`). Captured during the host enemy's update (sounds at its position, deferred `actor->sfx`, 18 pointer-free particle types), carried on the existing per-enemy stream, replayed on mirrors. Hit and death effects deliberately left local.
- **Horde night** (`HordeNight.*`, opt-in CVar). Every 3rd night in Hyrule Field / Lon Lon Ranch: escalating Stalchild/ReDead/Gibdo/Wolfos waves around living players; ReDeads' leash point drifts toward the nearest player so they shamble in; dawn clears them; authority handover continues silently. `HORDE_EVENT` packet announces start/end. Force/interval/cap/rate CVars.
- **Pre-existing fork bug fixed:** dynamic-spawn broadcasts sent post-init params, so mirrors spawned mismatched copies (mirror ReDeads set permanent switch flag 0 on death; Wolfos from the vanilla field spawner self-destructed on mirrors). Fixed by capturing params in `ShouldActorInit`. Upstreamable on its own.

### King Dodongo adapter — spec, not built
Not attempted blind; Gohma's adapter was clearly built by play-testing each phase, and KD has two co-op blockers Gohma doesn't:
1. **Bomb inhale.** `BossDodongo_AteExplosive` scans the *host's* explosive actor list near `mouthPos`. Bombs are local actors, so only the host can ever stun him. Needs a "bomb near mouth" request from the thrower's machine → host spawns a stand-in explosive or forces the inhale transition.
2. **Private particle system.** Fire breath uses `play->specialEffects = &this->effects` (boss-owned array updated in Update), not `EffectSs`, so EnemyFxSync can't carry it. Stream the effect array in adapter extras, or run the effect update locally on mirrors.
3. Phases to gate like Gohma: intro cutscene (local everywhere) → fight (mirror) → vulnerable/explode (streamed phase edges) → death cutscene (local handoff via `BossDodongo_SetupDeathCutscene`).
Priority: low for the horde mode; matters only for campaign co-op.

## Web build (2026-10-01)

Playable-in-a-browser bundle: `soh-coop-web.tar.gz` (21 MB; game + Docker compose + source patches). Players get ~10 MB gzipped on first load.

- **Base:** ported zalo/shipwright-64's Emscripten work (single-threaded wasm, in-browser ROM extraction via ZAPD, IndexedDB saves, touch gamepad, Anchor over WebSocket) onto the True Co-op fork + M3, minus its libsm64/Mario mode. Project516/Shipwright (SoH 9.2.3, merged Sept 26 2026) is the other web port; it has no Anchor.
- **Networking:** browsers can't open raw TCP. `server.js` serves the game and bridges `wss://<host>/anchor` to a stock Anchor server (adds/strips the NUL packet framing), one TCP connection per player. Browser + desktop players share rooms. zalo's PartyKit relay was not used: it broadcasts unknown packet types to everyone, so targeted enemy effects would hit every player. Bridge verified end-to-end against a real Anchor server (targeted packet reached only its target).
- **Share link:** `https://zelda.phatt.vip/#room=boys&name=Tyler&horde=1` (`hordeforce=1` for testing, `ws=` relay override). Relay defaults to the page's own host.
- **Build gotchas:** emsdk 3.1.64; GitHub archive downloads blocked in sandbox → port sources via git + `.emscripten_url` marker. Randomizer table sources need 5+ GB each under emcc at -O1 (OOM at -j2 on 7 GB; needed 12 GB swap + -j1); -O0 is not an option (functions exceed wasm's 50,000-local limit → `too many locals`). zalo's OTRExporter desktop branch redirected tinyxml2 to ZAPD's bundled copy, breaking the desktop ZAPD link on this base; reverted. Desktop build re-verified after the port. `shell.html` isn't a link dependency: after shell-only edits, `rm build-web/soh/soh.html` to relink (~30 s).
- **Not in browser build:** Opus custom music (silent), Crowd Control, Sail.

### Bug, UI and stability pass + SWAG hosting (2026-10-01, patch 5)
Bundle re-sent as `soh-coop-web.tar.gz`. Hosted like the other subdomains: `swag/zelda.subdomain.conf` (stock lazy-resolve, `$upstream_app soh-web`), compose joins SWAG's Docker network (`SWAG_NETWORK` in `.env`), proxied Cloudflare CNAME `zelda`, wildcard cert. Optional `ACCESS_KEY` (invite link `?key=`, year-long cookie), same idea as CS Party's `PARTY_KEY`.

Bugs found and fixed:
- **ROM extraction always hung** (inherited from zalo's shell): it freed strings with `Module._free`, which this build doesn't export, so the result was never handled. Found by feeding a fake byte-swapped ROM through the real extractor. Now uses `ccall`. The first web bundle could never have gotten past the ROM step.
- **SWAG conf trap:** adding `proxy_read_timeout` after `include proxy.conf` is a duplicate directive → nginx refuses to start → every SWAG site down. Caught by testing the conf against a SWAG-equivalent nginx before shipping. The shipped conf uses proxy.conf as-is; the relay pings every 25 s instead (inside SWAG's 240 s and Cloudflare's 100 s idle limits).
- shell.html had a JS syntax error (unescaped apostrophe) that would have broken the next build's page.
- `.n64`/`.v64` ROMs were offered by the picker but rejected; now byte-order normalized.
- soh.o2r cached in IndexedDB forever → stale assets after a redeploy. Now ETag-revalidated from the server; all assets `no-cache` + ETag so a redeploy can't mix old and new files.
- Settings and controller bindings were lost on every reload (only `/Save` persisted). Mirrored through an IDBFS `/persist` mount; fullscreen flag stripped on restore. Solo boots reset Anchor `Enabled` so a persisted co-op setting doesn't auto-connect.
- WebSocket reconnect leaked a socket handle per attempt; stale close events could flip the new connection's state.

UI: lobby (ROM status + "use a different ROM", name, room, fairy color in the Four Swords palette, horde toggle, copy invite link, play solo); links with room+name skip it. In-game connection pill; crash/load-failure card; controls reference. Anchor's color only tints the player's fairy on other screens, so it's labeled "fairy color", not tunic.

Stability: background tabs switch the main loop to timers (rAF stops when hidden, which froze the room authority's enemies for everyone; silent tabs still throttle to ~1 Hz); bridge ping/pong drops dead peers in ~2 intervals; NUL-in-packet guard; graceful SIGTERM (close 1012) so clients reconnect after redeploys; periodic + on-hide IndexedDB sync; screen wake lock; leave-page confirm; non-root container, log rotation.

Verified headless through the SWAG-equivalent proxy (timeouts cut to 10 s): relay 6/6 (key enforcement, idle survival, 70 KB packet, NUL drop, dead-peer drop), lobby 17/17 (stale-cache cleanup, invite link, ROM checks, extractor refusal path, settings persistence across reload, loop switch, status pill, mobile layout, no page errors). Desktop build still links. **Still not verified: a real ROM extraction and gameplay.**

### Mobile + controllers (2026-10-01, patch 6)
- **Controllers:** SDL 2.28's Emscripten driver ships a default "Standard Gamepad" mapping, and LUS hot-plugs SDL controllers, so any browser-exposed controller (Bluetooth Xbox/PS/most others, Chrome Android + Safari iOS) maps like desktop SoH: LT = Z, RT = R, right stick = C. Browsers only expose a pad after a button press. Touch pad hides on `gamepadconnected`.
- **iPhone ROM picker:** the `accept=".z64"` filter greys ROMs out on iOS (unknown UTI); removed on iOS. `.zip` import via `DecompressionStream`.
- **Memory:** after extraction the page stores the archive and reloads, since wasm memory never shrinks and phones kill tabs early.
- **Install/fullscreen:** manifest (fullscreen, landscape; key-aware start_url because iOS home-screen apps get their own cookies and storage), icons, Android fullscreen + orientation lock on Join, portrait hint, safe-area insets. Lobby/icon mark changed from the three-triangle emblem to an original fairy glyph (that emblem is Nintendo's).
- **App-switch handoff:** authority is the lowest same-scene clientId. A backgrounded phone is frozen but stayed "online" until the relay's ping timeout (~50 s), freezing every enemy for the room. Phones now `web_anchor_suspend` on hide (leave the room) and rejoin on show, so authority moves within a tick.
- **Deliberately not done:** forcing iOS `audioSession = playback` to beat the silent switch; it would compete with TeamSpeak for the audio session.
- Verified in iPhone 13 / Pixel 7 emulation through the proxy (14/14) plus the earlier lobby and relay suites. Not verified: a physical controller driving Link, phone memory during a real unpack.

## Next steps
1. Deploy behind SWAG per the bundle README and drop a real OoT ROM in. A successful extraction is the one boot path no test here could reach.
2. (Desktop players) Fork `bghill95/OOT-True-Co-op` on GitHub, apply the patches, push → Windows build from Actions. The session's GitHub token can't create forks (proxy blocks API writes).
3. Run `COOP-M3-TEST-GUIDE.md`; fastest path: horde night ticked + `hordeforce=1` in the link, both players in Hyrule Field.
4. Then: RELEASE effect (unblocks Dead Hand / Moblin targeting), worn-mask sync, then building.

## First milestone
Lon Lon Ranch base, day counter, ReDead waves every third night, five placeables, two players seeing the same zombies. (Day counter + waves + shared zombies now exist in code; base and placeables remain.)

