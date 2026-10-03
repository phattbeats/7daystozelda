# 7 Days to Zelda: deployment runbook (PHA-3856)

Live at **https://zelda.phatt.vip** (Ship of Harkinian True Co-op, browser build). Deployed 2026-10-01 on PHATT-RAID.

## Steps taken

1. **Source.** `D:\Nextcloud\PHATT-TECH\Projects\7daystozelda\soh-coop-web.tar.gz`, which is `/mnt/user/nextcloud-data/phatt/files/PHATT-TECH/Projects/7daystozelda/` on the host. Extracted to `/mnt/disks/docker-drive-phatt/appdata/7daystozelda/soh-coop-web/`.
2. **Image.** `docker build -t soh-web:latest .` in that directory (node:22-alpine, has a healthcheck).
3. **Containers.** The host has no `docker compose`, so these are the plain-docker equivalent of the bundled compose file:
   ```
   docker network create soh-coop
   docker run -d --name soh-anchor --restart unless-stopped --network soh-coop ghcr.io/garrettjoecox/anchor:latest
   docker run -d --name soh-web --restart unless-stopped --network soh-coop \
     -e ANCHOR_HOST=soh-anchor -e ANCHOR_PORT=43383 -e ACCESS_KEY= soh-web:latest
   docker network connect phattvip soh-web      # SWAG's network
   ```
   Both containers have log rotation set (10m x 3). Anchor is not exposed to the internet; port 43383 is not published.
4. **SWAG proxy conf.** Copied `swag/zelda.subdomain.conf` to `appdata/swag/nginx/proxy-confs/`. It proxies `zelda.*` to `soh-web:8080` using lazy resolve.
5. **SWAG docker config.** Added `zelda` to `SUBDOMAINS` in `/boot/config/plugins/dockerMan/templates-user/my-swag.xml` (backup: `my-swag.xml.pre-zelda-20261001`), then recreated SWAG with Unraid's `update_container swag`. SWAG issued a new Let's Encrypt cert through Cloudflare DNS validation. The cert covers `zelda.phatt.vip` and expires 2026-12-30. Note: the bundled README says a wildcard cert covers the subdomain. That is wrong; the cert lists each subdomain by name.
6. **Cloudflare.** Created a proxied CNAME `zelda` → `phatt.vip` in zone phatt.vip (record id `93f2836f052400796d690da99002cd7a`), matching the existing records.

## Verification
- `https://zelda.phatt.vip/` returns 200 and `/healthz` returns 200. `soh-web` is healthy.
- A WebSocket upgrade on `/anchor` returns `101 Switching Protocols`, so the co-op relay is reachable through Cloudflare and SWAG.
- Other sites came back after the SWAG restart: nextcloud returns 302 and plex returns 401, as before.

## Open items
- **No invite key is set.** Anyone with the URL can load the game. To lock it, recreate `soh-web` with `-e ACCESS_KEY=$(openssl rand -hex 12)` and share `https://zelda.phatt.vip/?key=<key>`.
- Not yet tested with a real ROM, per the build's README.
- These containers are not Unraid templates, so they won't appear with editable settings in the Docker tab. They do restart on their own (`unless-stopped`).
- Redeploy a new build: extract it, run `docker build -t soh-web:latest .`, then `docker rm -f soh-web` and repeat the `soh-web` run and network connect commands from step 3.


## Change 2026-10-01: custom fairy color picker
Requested so b-mech can make his fairy black.
- Added a **Custom** option next to the four presets in the lobby. It opens a gradient picker with a saturation/brightness square, a hue bar, a hex box, and **Black** / **White** shortcuts. Custom starts at black.
- The picked color goes out as the same `color=RRGGBB` value the presets use. It is remembered per browser, carried in invite links, and a link such as `#color=000000` reopens the picker with that color.
- Edited `public/index.html` only (and regenerated `index.html.gz`), then rebuilt `soh-web` and recreated the container with the same run commands as step 3. Backups: `public/index.html.pre-fairypicker` and `index.html.gz.pre-fairypicker`. The lobby source patch under `src/OOT-True-Co-op/0005-*` was **not** updated, so a rebuild from source would drop the picker.
- Verified in headless Chromium against the live site: presets still work, dragging and hex entry set the color, Black gives 000000, saved/linked custom colors restore with the picker open, and there are no page errors.
- **Not verified in-game.** If the fairy glow is drawn additively, pure black (000000) may show as no glow. If that happens, use a near-black like 1A1A1A.


## Incident 2026-10-01: game crashes when a ROM is uploaded
- **Symptom:** the page shows "Unpacking finished without producing game data", then "The game crashed".
- **Cause:** the browser build's ROM extractor aborts (`Aborted()` in wasm) right after "ROM validated". I reproduced it in headless Chromium with Brandon's ROM, `Legend of Zelda, The - Ocarina of Time (USA) (Rev 2).z64` (NTSC 1.2, sha1 41b3bd…57c2). The ROM is fine and is a supported version, and the extractor tables for it are bundled. The bug is in the wasm build, which was never tested with a real ROM. Follow-up issue: PHA-3860.
- **Fixed (PHA-3860, live 2026-10-02):** the wasm extractor now unpacks the .z64 in the browser. Players drop the ROM itself in the ROM box; the pre-extracted `oot.o2r` workaround is retired. A spare `oot.o2r` remains in the private Nextcloud folder `PHATT-TECH/Projects/7daystozelda/` but is not needed.


## Change 2026-10-03: menu music on the lobby
Requested: play the OoT title theme (youtube.com/watch?v=exQIatKQyQk) on the main screen at 15% by default, with a slider.
- A small card in the lobby's bottom-right corner shows a tiny YouTube player, a play/pause button and a volume slider (0-100%, default **15%**). The slider setting is saved per browser (`localStorage` key `soh.menuMusicVolume`). The track loops.
- Browsers block sound until the visitor interacts with the page, so the music starts on the **first tap, click or key press** on the lobby. The card says "tap anywhere to start" until then.
- When the game starts (the lobby overlay hides), the music fades out over about 1 second and the player is removed, so it never plays over the game's own music. If YouTube fails to load, the card removes itself.
- The overlay got 96px of bottom padding so the card doesn't cover the footer on phones.
- Deploy: tagged the old image `soh-web:pre-pha3856-music` (rollback), built `soh-web:latest` FROM it with only `public/index.html` and `.gz` changed (build dir `appdata/7daystozelda/deploy-pha3856-music/`), then recreated `soh-web` with the step 3 run command, log options, and `docker network connect phattvip`.
- Saved to Nextcloud `7daystozelda/homepage-art/`: `live-index-pha3856-music.html` (the full live page) and `lobby-music.snippet.html` (just the widget: it goes right before `<script async src="soh.js...`). **The older `homepage-art/index.html` does not have this change.** On the next bundle unpack, re-insert the snippet or the music disappears.
- Verified in headless Chromium (desktop and phone sizes) with the page swapped in: the player loads at 15% and starts playing after a click; moving the slider to 40% saves and survives a reload; hiding the overlay removes the player; no page errors. After the deploy, curl confirmed the live page serves the widget and /healthz returns 200. I couldn't rerun the browser test against the live URL because browserless was over capacity.

## Change 2026-10-03 (later): menu music is now a hosted MP3, not YouTube
Brandon asked for the MP3 instead of a YouTube player. This replaces the YouTube widget described above.
- `public/title-theme.mp3`: 90 s, about 128 kbps, 1.4 MB, downloaded from the same video with yt-dlp. The page plays it through the Web Audio API on a seamless loop. A gain node handles volume, because iPhones ignore volume on a normal audio player and server.js doesn't support byte ranges or know the mp3 type.
- The card has a play/pause button, a volume slider (default **15%**, remembered per browser under `soh.menuMusicVolume`), and the track name. No video. Music starts on the first tap, click or key press, and fades out over 1 s when the game starts.
- The page loads `title-theme.mp3?v=ab03a2ad`. Cloudflare caches .mp3 for 4 h, including a 404 I triggered during deploy, so the version tag is required. Change it if the file changes.
- Deploy: rollback image `soh-web:pre-pha3856-mp3`; build dir `appdata/7daystozelda/deploy-pha3856-mp3/` (FROM rollback + COPY public/); container recreated with the step 3 command, log options, and the phattvip network.
- Nextcloud `homepage-art/` now holds `title-theme.mp3`, the updated `lobby-music.snippet.html`, and `live-index-pha3856-music.html` (the full live page). On a bundle unpack, copy the mp3 into public/ and insert the snippet before `<script async src="soh.js`.
- Verified on the live site in headless Chromium: before a click the card says "tap anywhere to start"; a click starts audio at 15%; pause and resume work; a slider setting of 40 survives a reload; hiding the lobby closes the audio and removes the card; a phone-size tap also starts it; no page errors. /healthz 200, mp3 200, other sites unaffected.


## Change 2026-10-03: invite key is on (PHA-3914)
Brandon OK'd printing the invite key in the 7DtZ devlog, which is for subscribers only.
- Recreated `soh-web` from the same `soh-web:latest` image with the step 3 command, log options and the phattvip network. The only change is `-e ACCESS_KEY=<ACCESS_KEY>`.
- Invite link: `https://zelda.phatt.vip/?key=<ACCESS_KEY>`. Room links take the key in front of the hash, for example `/?key=<ACCESS_KEY>#room=boys`.
- Anyone who already opened the bare URL needs the keyed link once. After that, a cookie lasting a year remembers the key.
- Verified on the live site: the bare `/` returns 403 with the "needs the invite link" page; `/?key=` returns 200 and sets the cookie; `/anchor` returns 403 for a WebSocket upgrade without the cookie and 101 with it; `/healthz` returns 200; the container is healthy.
- **On every redeploy, keep `-e ACCESS_KEY=<ACCESS_KEY>`** in the run command. If you leave it out, the site is open again.
