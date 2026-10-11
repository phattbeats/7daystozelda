# 7 Days to Zelda: deployment runbook (#3856)

Live at **https://zelda.phatt.vip**. This is the browser build of Ship of Harkinian True Co-op plus the 7 Days mode. It runs on PHATT-RAID. First deployed 2026-10-01. This runbook was reviewed against the live server on 2026-10-11.

The top half describes how things are now and how to redeploy. The bottom half is the history of #3856's own changes. Every later deploy (40+ of them, one per issue) is recorded in the GitHub commit log as "live index for the PHA-xxxx deploy".

## 1. Current state (checked 2026-10-11)

| Item | Value |
| --- | --- |
| Live image | `soh-web:pha4155b` (PHA-4155 lobby codes, 2026-10-11: server.js + index.html on top of `pha4155`; game files unchanged. Main also carries the undeployed chat/cloud-save changes, so this image was built from the live files plus only the lobby delta, commit 51be76d) |
| Rollback | `soh-web:pre-pha4155b` (= pha4155). The old container is kept stopped as `soh-web-pre-pha4155b` |
| Lobby page | `soh.js?v=af74dd85` |
| Game files (md5, first 8) | soh.wasm 6dc097b2, soh.data eaa1dc66, soh.o2r 679138c5 |
| server.js | same as repo main `web/server.js`. Since PHA-4155 it also keeps the in-memory public-room directory behind `/api/rooms`, `/api/quickplay` and `/api/rooms/publish` (env `ROOM_SEATS`, `MATCH_RANGE`; see web/README.md). A restart forgets which rooms are public; players already in them keep playing | Lobby codes (`/api/lobby/create`, `/info`, `/visibility`): a lobby is the room `lb-CODE`, private (code only) or public (listed, quick match). With `LOBBY_URL` + `LOBBY_SECRET` it forwards the lobby API to the `zelda-lobby` Cloudflare Worker (`web/lobby-worker`) and reports each lobby's roster to it; **neither is set on the live container yet** (no Worker deployed: the Cloudflare token here is read-only for Workers), so codes run from memory and a restart forgets them.
| Relay | `soh-anchor` (ghcr.io/garrettjoecox/anchor:latest), only on the `soh-coop` network, not published |
| Networks | soh-web is on `soh-coop` (to reach the relay) and `phattvip` (so SWAG can reach it) |
| Logs | json-file, max-size 50m, max-file 1 (the old runbook said 10m x 3; copy whatever `docker inspect` shows) |
| Cert | SWAG Let's Encrypt cert for phatt.vip includes `zelda.phatt.vip`, expires 2026-12-31, auto-renews |
| Access | `ACCESS_KEY` is set. `/` answers 200 without the key (share-preview tags only). The game files, the mp3 and `/anchor` answer 403 without the key or its cookie |
| Health | `https://zelda.phatt.vip/healthz` returns 200 |

## 2. How it fits together

1. **Cloudflare** has a proxied CNAME `zelda` pointing to `phatt.vip` in zone phatt.vip.
2. **SWAG** has `zelda` in `SUBDOMAINS` in its Unraid template (backed up first). `appdata/swag/nginx/proxy-confs/zelda.subdomain.conf` proxies `zelda.*` to `soh-web:8080` (lazy resolve, `client_max_body_size 0`). The cert lists each subdomain by name. A wildcard does not cover it, whatever the old bundle README says.
3. **soh-web** is node:22-alpine running `web/server.js`. It serves `/app/public`, checks the invite key, and proxies the `/anchor` WebSocket to `soh-anchor:43383`.
4. **soh-anchor** is the co-op relay.
5. **Saves stay in each player's browser.** The game writes `/Save` and `/persist` (settings) to IndexedDB through Emscripten IDBFS, every 20 s and when the tab hides (`soh/soh/web/web_main.cpp`). The server keeps no saves. Clearing site data, or switching browser or device, starts over. Cloud saves are planned; see the `cloud-saves-plan` document.
6. **The ROM never leaves the player's machine.** Players drop their own .z64 into the page. It is unpacked in the browser (#3860 fixed the extractor). The private copy of the ROM and `oot.o2r` are in Nextcloud `PHATT-TECH/Projects/7daystozelda/rom/` and are never public.

### Where things live

- **Source of truth:** GitHub `phattbeats/7daystozelda`, branch main (public). `web/server.js`, `web/Dockerfile`, `web/public/index.html` (the live lobby), `web/swag/`, `docs/`. Release `buildtree-2026-10-02` holds the emscripten build tree.
- **Nextcloud** `PHATT-TECH/Projects/7daystozelda/`: `homepage-art/` (lobby art, `title-theme.mp3`, title/MM/icon pack inputs), `rom/` (private), `devlog/`.
- **On the host:** build directories `<appdata>/7daystozelda/deploy-*`. The old `soh-coop-web/` folder there is stale; don't build from it.

## 3. Redeploying (the recipe every deploy since #3904 has used)

Run docker over `ssh raid`. phatt-claw can list and inspect the containers but can't build images or read their logs.

1. **Push first.** Commit the change to main as phattbeats (no co-author lines), `git fetch`, rebase, and push. Make sure the push really landed: a rejected non-fast-forward push is easy to miss. Several deploys can land in one evening, so check `docker inspect soh-web --format '{{.Config.Image}}'` right before you swap. Anything deployed but not on main gets silently reverted by the next deploy.
2. **Build the game** from the build tree. Use `tools/limited-build.sh` (`-j2`, nice). A full `ninja -j10` has crashed the server.
3. **Ship only what changed.** If `soh.data` matches the live md5, ship only soh.js and soh.wasm (and soh.o2r if it changed). Make a fresh `gzip -9 -k -f` of every file you change: server.js prefers a `.gz` next to the file, so a stale .gz serves the old build.
4. **Start the lobby page from the live one** (`docker exec soh-web cat /app/public/index.html`, the same as repo `web/public/index.html`). Bump `soh.js?v=<first 8 of the soh.js md5>`. Cloudflare has cached .js and .mp3 for 4 h before, which paired an old soh.js with a new soh.wasm and gave a black screen.
5. **Build the image** in a new `deploy-<tag>/` dir: `FROM soh-web:<current live tag>` plus `COPY public/ /app/public/` (and `server.js` if it changed). `docker tag` the current live image as `pre-<tag>`.
6. **Recreate the container:**
   ```
   docker inspect soh-web --format '{{range .Config.Env}}{{println .}}{{end}}' | grep -E '^(ACCESS_KEY|ANCHOR_HOST|ANCHOR_PORT|NODE_ENV|PORT)=' > env.list   # umask 077; delete after
   docker stop soh-web && docker rename soh-web soh-web-pre-<tag>
   docker run -d --name soh-web --restart unless-stopped --network soh-coop --env-file env.list \
     --log-opt max-size=50m --log-opt max-file=1 soh-web:<tag>
   docker network connect phattvip soh-web
   ```
   Never print the ACCESS_KEY. If it is left out, the site is open to anyone.
7. **Check it:** `/healthz` 200; the keyed page loads the new `soh.js?v=`; the live soh.js and soh.wasm md5 match the build; `/soh.wasm` without the key gives 403.
8. **Record it:** commit the live index to main ("live index for the PHA-xxxx deploy (soh.js?v=…, image soh-web:…)").

**Rollback:** `docker stop soh-web && docker rename soh-web soh-web-bad`, then `docker rename soh-web-pre-<tag> soh-web && docker start soh-web`. It keeps its networks and env.

### Things every deploy must keep

- **The lobby page:** the `og:`/`twitter:` share tags and `favicon*?v=` links; the inline `Object.defineProperty(document,"title",…)` tab-title lock right after `<title>`; the 7 Days-only form (no Horde/7 Days checkboxes); the `#f-raids` Days-between-raids select; the menu music card with `title-theme.mp3?v=…`; the fairy and tunic pickers.
- **soh.o2r layers:** title pack, then the MM model pack (`art/mm-pack/build_mm_pack.py`), then the Workbench icons (`art/icons/build_icon_pack.py`). Start from the live o2r and drop `objects/7dtz_mm/` before re-appending.
- **Rules from Brandon:** no ImGui toasts or popups for players (notices are OoT text boxes); no on-screen Craft button; pre-game choices go on the lobby, not in game; never change the existing mobile pad.

## 4. Review findings (2026-10-11)

- **All #3856 requests are live:** the fairy colour (now #3901's core, aura and tunic pickers, which replaced the first gradient picker), menu music MP3 at 15%, no Craft button, raid days on the lobby, OoT text-box notices, and the 1.2x workbench.
- **The repo's lobby page lagged one deploy behind.** `web/public/index.html` on main still loaded `soh.js?v=bcc2899c` (pha4125b). The live page loads `af74dd85` (pha4048). Fixed in the same commit as this runbook. Nothing else differs.
- **Docker disk is 96% full** (447G used, 19G free). The 7DtZ share of that is small: about 574 MB of deploy dirs, 9 `soh-web` tags, and 5 stopped `soh-web-*` containers. Docker reports 17.6 GB of reclaimable images across the whole host. Each deploy adds a build dir and a stopped container. Pruning down to the last two rollbacks needs Brandon's OK.
- **Not checked in-game on the live site:** pure-black fairy glow, a new save picking up the lobby's raid days, and the text-box notices. They were tested on the local rig.
- **Still drawn the modern way:** the co-op "Anchor Room" window and the old Tab-key Workbench window. They are menus, not popups, and were left as they are.

## 5. #3856 change history

- **2026-10-01, first deploy.** Steps 1 to 6 above. Containers were plain `docker run`, because the host has no `docker compose`. They aren't Unraid templates, so they don't appear with editable settings in the Docker tab.
- **2026-10-01, fairy colour picker.** Added a Custom gradient picker with a Black shortcut so b-mech could make his fairy black. #3901/3939 later replaced it with the core, aura and tunic pickers.
- **2026-10-01, ROM crash.** The in-browser extractor aborted right after "ROM validated". Brandon's USA 1.2 ROM was fine. The `oot.o2r` workaround was used until #3860 fixed the extractor (live 2026-10-02).
- **2026-10-02, palette bug.** Kokiri drew with garbled palette textures. Fixed in #3860.
- **2026-10-02, volume and graphics.** No change was needed: the in-game Esc menu already has separate music and SFX sliders and an Internal Resolution setting.
- **2026-10-03, "4 player shouldn't be hardcoded".** No 4-player limit was found. Rooms, raids and horde targeting already scale with the players connected.
- **2026-10-03, menu music.** The title theme plays on the lobby at 15%, with a slider (`localStorage` `soh.menuMusicVolume`). It was first a YouTube embed; Brandon asked for an MP3, so it is now `public/title-theme.mp3` (90 s loop) played through Web Audio, because iPhones ignore `<audio>` volume. It starts on the first tap and fades when the game starts. The mp3 is kept out of the public repo; its copy is in Nextcloud `homepage-art/`.
- **2026-10-03, invite key (#3914).** `ACCESS_KEY` is set. Players use the `?key=` link once, and then a cookie lasting a year remembers it.
- **2026-10-03, no Craft button.** The floating `##SevenDaysCraftButton` window was removed. Crafting opens from the pause menu's Workbench page, the Tab key, or a placed workbench. Repo 1781e0f.
- **2026-10-03, raid days on the lobby.** The in-game "How often do raids come?" popup grabbed controllers, so it was removed. The lobby `#f-raids` select (default 3) sets `window._raidInterval`. The host's pick (or a solo player's) applies once per session. Repo 09f29fc.
- **2026-10-06, notices and workbench.** `Notification::Emit` now draws OoT message boxes (`SevenDays::PushNotice` in Nights.cpp) instead of ImGui toasts. Repeated gains of the same item add up in one box. The boxes hide during text, cutscenes, the pause screen and Game Over. The workbench is now 1.2x (top at 35 units, about child Link's chest) instead of 1.8x. Repo 55f0e9f + 52eb0a1. #4028 later moved notices into their own overlay display list.
- **2026-10-11, runbook review.** This rewrite. The repo's lobby page is synced to the live one.
