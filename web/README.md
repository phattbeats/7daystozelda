# Ocarina Co-op: Ship of Harkinian True Co-op in a browser

Ocarina of Time co-op (shared enemies, nearest-player targeting, enemy sounds and effects, horde night) as a web page at `https://zelda.phatt.vip`. Each player brings their own ROM. It's unpacked inside their browser and never uploaded.

## What's in here

| Path | What |
|---|---|
| `public/` | The game: `index.html` (lobby + game), `soh.js`, `soh.wasm`, `soh.data` (extractor tables), `soh.o2r` (SoH's own assets, no Nintendo data). `.gz` siblings are served automatically. |
| `server.js` | Serves `public/` and bridges `wss://<host>/anchor` to the Anchor relay over TCP. Optional invite key. |
| `docker-compose.yml` | `soh-web` (the above, on SWAG's network) + `soh-anchor` (stock Anchor relay, private). |
| `swag/zelda.subdomain.conf` | SWAG proxy conf. |
| `.env.example` | `SWAG_NETWORK`, `ACCESS_KEY`. |

## Deploy on PHATT-RAID behind SWAG

1. **Find SWAG's network.** Unraid Docker tab → SWAG → Network Type, or `docker inspect swag --format '{{json .NetworkSettings.Networks}}'`. Copy `.env.example` to `.env` and set `SWAG_NETWORK` to it.
2. **Start the stack:** `docker compose up -d --build`. Check it with `docker logs soh-web` (it prints `serving ... bridging /anchor`).
3. **DNS:** Cloudflare CNAME `zelda` → `phatt.vip`, proxied. The `*.phatt.vip` wildcard cert already covers it.
4. **SWAG:** copy `swag/zelda.subdomain.conf` into SWAG's `/config/nginx/proxy-confs/` and restart SWAG. It's the stock lazy-resolve pattern (`$upstream_app soh-web`), so SWAG still starts if this stack is down.
5. Open `https://zelda.phatt.vip`.

Rename the subdomain by changing `server_name zelda.*;` and the CNAME. The container name `soh-web` must stay lowercase; SWAG resolves it literally.

**Don't add `proxy_read_timeout` to the conf.** SWAG's `proxy.conf` already sets it, and nginx refuses a duplicate, which stops SWAG and takes every site down with it. It isn't needed: the relay pings every 25 s, inside both SWAG's 240 s timeout and Cloudflare's 100 s idle cutoff.

### Invite key (optional)

Set `ACCESS_KEY` in `.env` (e.g. `openssl rand -hex 12`) and `docker compose up -d`. The site and the relay then only answer browsers that opened `https://zelda.phatt.vip/?key=<key>` once; a cookie remembers it for a year. Without it, anyone who finds the URL can load the page and join a room whose name they guess.

### Desktop players (optional)

Desktop SoH players connect Anchor straight to the relay: uncomment `43383` under `anchor` in the compose file and forward the port. Browser and desktop players share rooms.

## Playing

The page is a lobby: drop your ROM (once per browser), type your name, pick a room and a fairy color, tick horde night, and **Join room**. **Copy invite link** gives friends a link with the room (and key, if you used one) filled in; they add their name.

Links skip the lobby when they carry both room and name:

```
https://zelda.phatt.vip/#room=boys&name=Tyler&horde=1
```

- `room`: everyone in the same room plays together.
- `name`: shown over your Link.
- `horde=1`: horde night (every third night in Hyrule Field / Lon Lon Ranch). The room's host decides.
- `hordeforce=1`: start a horde right now (testing).
- `color=D0312D`: fairy color. Optional: `team=default`, `ws=wss://other-host/anchor`.

Saves, settings and controller bindings stay in each browser. A small pill in the top-right shows the co-op connection; it reconnects on its own after drops or a server restart. Esc opens the settings menu.

## Phones, tablets and controllers

- **Touch:** an on-screen pad (stick, A/B/Z, C-buttons, L/R, Start, Esc) appears on touch screens and stays clear of the notch. Play sideways; the page nudges you if you don't.
- **Bluetooth controllers:** pair it in the phone's Bluetooth settings, open the game, and press any button (browsers only reveal a controller after a press). The touch pad hides itself, and comes back if the controller disconnects. Xbox, PlayStation and most "standard" controllers map like the desktop game: A/B, LT = Z, RT = R, right stick = C-buttons. Works in Chrome on Android and Safari on iPhone/iPad. Rebind in Esc → Settings → Controller; it's remembered.
- **ROM on a phone:** tap the picker and choose the file from Files/Downloads. A `.zip` is fine. Unpacking takes a minute or two on a phone; keep the screen on.
- **Fullscreen:** Android goes fullscreen and locks landscape when you tap Join. iPhone Safari can't fullscreen a web page: use Share → Add to Home Screen and launch from the icon. The home-screen app has its own storage, so add the ROM once more there (with an invite key, the icon carries it).
- **Switching apps:** a phone freezes a page in the background. So the moment you switch to TeamSpeak or lock the screen, the phone steps out of the co-op room, and it rejoins when you come back. Others keep playing; enemy control hands off to the next player instantly instead of freezing everyone.
- **iPhone silent switch** mutes game audio (Safari rule). Left that way on purpose: forcing game audio through it would also fight TeamSpeak for the audio session.

## Known limits

- Verified with a real ROM (USA Rev 2 / 1.2, SHA-1 41b3bdc4…): in-browser extraction (~40 s headless, matches the desktop archive), boot, a save created and loaded, two players in one room seeing each other, Kokiri Forest and the Kokiri Shop rendering correctly. Not verified: a physical Bluetooth controller driving Link, and phone memory headroom during an unpack.
- Unpacking the ROM takes about half a minute and freezes the page while it runs.
- A tab in the background keeps the game running only if it's playing sound. Silent background tabs get slowed to about once a second by the browser. That's enough to stay connected, but if the room's host tabs out with the music muted, enemies will crawl for everyone.
- Custom streamed music mods (Opus) are silent. The game's own music and sounds work.
- No Crowd Control or Sail (they need raw sockets).
- Clearing the site's data deletes saves and the unpacked ROM.
