title: 7 Days to Zelda, Day 3: Ocarina in a browser tab
slug: 7dtz-devlog-03-ocarina-in-a-browser-tab
excerpt: How a co-op Ocarina of Time mod ended up running in Chrome, talking to a stock Anchor server through a small Node relay.
tags: 7 Days to Zelda, devlog, WebAssembly, Emscripten, networking, self-hosting
feature_image: img/live-D00-lobby.png
publish: Day 3 (see schedule)

Day 3. On Day 2 the monsters learned to share. That was all desktop code, though, and the plan for game night was never "everybody install a Windows build from a fork of a fork." The plan was: I text the boys a link, they open it, they're in Hyrule.

So this one is about getting Ship of Harkinian, the True Co-op fork, and our M3 patches running inside a browser tab, and getting browsers to talk to an Anchor co-op server that only speaks raw TCP. Same deal as the whole series: the AI agent (Claude, running as an agent in Paperclip) wrote basically all of this. I pointed, it built, and I broke it with a real ROM.

![The 7 Days to Zelda lobby on the live site](img/live-D00-lobby.png)
*FIG 3-1 — The live lobby (after the later art pass): game files ready, name, room, fairy color, horde night toggle.*

## 3.1 Standing on zalo's shoulders

We didn't start from zero. zalo/shipwright-64 had already done the hard Emscripten work for SoH: single-threaded wasm, in-browser ROM extraction via ZAPD, IndexedDB saves, a touch gamepad, and Anchor over WebSocket. The agent ported that onto the True Co-op fork plus our M3 patches, minus its libsm64/Mario mode. (Project516/Shipwright is the other web port, on SoH 9.2.3, but it has no Anchor, and Anchor is the whole point.)

The commit message for patch 0004 is a decent summary of what "port" meant here:

From `OOT-True-Co-op/0004-Web-build-WebAssembly-port-with-Anchor-over-WebSocke.patch`:

```text
- Anchor connects over WebSocket on web (zalo's Network/WebSocket layer) to a
  relay that bridges to a stock Anchor TCP server, so browser and desktop
  players share rooms; polled from ProcessIncomingPacketQueue (no threads).
- Room/name/color/team/relay URL and horde switches come from the page link
  (#room=..&name=..&horde=1); relay defaults to wss://<page host>/anchor.
- Crowd Control and Sail are excluded on web (raw sockets).
...
Desktop builds unaffected (all changes behind __EMSCRIPTEN__/BUILD_FOR_WEB).
```

"No threads" is the big one. Desktop Anchor has a receive thread. The wasm build is single-threaded, so there is no receive thread; the WebSocket inbox gets polled on the game thread.

What didn't make it into the browser: Opus custom music (silent on web), Crowd Control, and Sail.

## 3.2 The build, or: 12 GB of swap for a randomizer table

![Web build pipeline diagram](diagrams/devlog-03-build.svg)
*FIG 3-2 — Web build pipeline and the two optimization traps.*

The toolchain is pinned to emsdk 3.1.64, CMake 3.26+, Ninja. Most of SoH compiles fine under emcc. The randomizer's table sources do not. Each one needs 5+ GB of memory to compile at `-O1`. At `-j2` on a 7 GB box it ran out of memory; the agent needed 12 GB of swap and `-j1` to get through.

The obvious fix is "just build those at `-O0`." Doesn't work. At `-O0` those functions blow past WebAssembly's 50,000-local limit, and the browser refuses to load the module with `too many locals`. So the build script compiles the heavy files one at a time, then everything else in parallel.

From `BUILD-WEB.md`:

```sh
cd build-web
ninja -t targets all | grep -oE "soh/CMakeFiles/soh.dir/soh/Enhancements/randomizer/(3drando/hint_list[^:]*|location_list|item_list|settings|ShufflePots|randomizer)\.cpp\.o" | sort -u | xargs ninja -j1
ninja -j4 soh
```

> **NOTE** After the first build, changes outside those files are an incremental compile plus a roughly 30 second relink, no swap needed. Keep the build tree around. One gotcha: `shell.html` is not a link dependency, so after a shell-only edit you have to `rm build-web/soh/soh.html` to force the relink.

What players actually download: about 10 MB gzipped on first load.

## 3.3 Browsers can't open TCP sockets

Anchor, the co-op server that ships with SoH, speaks JSON packets over TCP, separated by a NUL byte. Browsers can't open raw TCP. They can open WebSockets.

So `server.js` does two jobs: it serves the wasm build as static files, and at `/anchor` it bridges each browser WebSocket to its own TCP connection to a stock Anchor server. One packet per WebSocket text message on one side, NUL-terminated on the other. Because the Anchor server is unmodified, desktop SoH players and browser players can sit in the same room.

![Relay architecture diagram](diagrams/devlog-03-relay.svg)
*FIG 3-3 — Browser to Cloudflare to SWAG to server.js to Anchor. The relay is the only piece that knows about NUL framing.*

Here's the core of it.

From `server.js`, Anchor to browser:

```js
// Anchor -> browser: split on NUL, one WebSocket text message per packet.
tcp.on("data", (chunk) => {
  let start = 0;
  let idx;
  while ((idx = chunk.indexOf(0, start)) !== -1) {
    const piece = chunk.subarray(start, idx);
    const packet = chunkBytes ? Buffer.concat([...chunks, piece]) : piece;
    chunks = [];
    chunkBytes = 0;
    start = idx + 1;
    if (packet.length && ws.readyState === ws.OPEN) ws.send(packet.toString("utf8"));
  }
  if (start < chunk.length) {
    chunks.push(chunk.subarray(start));
    chunkBytes += chunk.length - start;
    if (chunkBytes > MAX_PACKET) closeBoth(1009, "packet too large");
  }
});
```

And the other direction, from the same file:

```js
// Browser -> Anchor: append the NUL terminator Anchor expects.
ws.on("message", (data, isBinary) => {
  alive = true;
  const body = Buffer.isBuffer(data) ? data : Buffer.from(isBinary ? data : String(data));
  // A NUL inside a packet would split it into garbage on the Anchor side.
  if (body.indexOf(0) !== -1) return;
  const framed = Buffer.concat([body, Buffer.from([0])]);
  ...
});
```

TCP is a stream, so a packet can arrive split across `data` events; the `chunks` buffer holds the partial tail until the next NUL shows up. There's a 4 MB cap on any one packet, and packets sent before the TCP side connects get queued.

### Why not PartyKit?

zalo's port came with a PartyKit relay. We didn't use it. It broadcasts unknown packet types to everyone in the room. Day 2's netcode sends targeted packets: "this enemy grabbed you specifically," "this freeze is for you." Broadcast those and effects meant for one player land on everyone. The agent verified the bridge end to end against a real Anchor server: a targeted packet reached only its target.

## 3.4 Putting it on the internet (and nearly taking everything else off)

Hosting went the same way as my other subdomains: Docker containers for the web server and the Anchor server, the web one joined to SWAG's network, a SWAG proxy conf, a proxied Cloudflare CNAME. Anchor itself isn't exposed to the internet at all; only the relay talks to it. The site lives at `zelda.phatt.vip`, and an invite link looks like `https://zelda.phatt.vip/?key=<ACCESS_KEY>#room=boys&name=phaTT&horde=1`.

The fun part is idle timeouts. A player sitting in a menu sends nothing. Cloudflare drops WebSockets that are idle for 100 seconds, and SWAG's `proxy.conf` times out at 240 seconds. The obvious move is to bump `proxy_read_timeout` in the site conf.

> **WARNING** Adding `proxy_read_timeout` after `include proxy.conf` is a duplicate directive. nginx refuses to start, and SWAG takes every site on the box down with it. The agent caught this by testing the conf against a SWAG-equivalent nginx before shipping, not after.

So the shipped conf uses `proxy.conf` untouched, and the relay pings instead.

From `server.js`:

```js
// Cloudflare drops WebSockets idle for 100 s and SWAG's proxy.conf times out
// at 240 s. A menu-idle player sends nothing, so ping well inside both.
const PING_MS = parseInt(process.env.PING_MS || "25000", 10);
```

That same ping doubles as dead-peer detection. A sleeping laptop or a dropped phone never sends a close frame, so two missed pongs and the relay frees the slot. And on `docker stop` the server sends close code 1012 ("service restart") to every browser so they reconnect to the new container instead of waiting out a timeout.

| Limit | Value | Where | Handled by |
|---|---|---|---|
| Cloudflare WebSocket idle | 100 s | edge | 25 s ping |
| SWAG `proxy.conf` read timeout | 240 s | nginx | 25 s ping (conf untouched) |
| Dead peer | 2 missed pongs | `server.js` | terminate, free slot |
| One Anchor packet | 4 MB | `server.js` | close 1009 |
| Redeploy | SIGTERM | `server.js` | close 1012, clients reconnect |

*TABLE 3-1 — Every timeout between a browser and Anchor.*

There's also an optional invite key. Without it, the page and the relay both answer 403. Open the site once with `?key=` and a year-long cookie remembers it. The key is on now, and the links in this series carry it.

The legal model is SoH's: the site ships the engine and SoH's own assets, nothing from Nintendo. Every player drops in their own legally dumped ROM, it gets unpacked in their browser, and it stays in their browser's storage. My ROM was a clean USA 1.2 dump. (How that unpack step went the first time is a Day 4 story. Spoiler: badly.)

![The original lobby with colors and the horde night toggle](img/ui-lobby.png)
*FIG 3-4 — The lobby from a test run: ROM stored in this browser, name and room, fairy and tunic colors, horde night.*

## 3.5 Phones, controllers, and the iPhone file picker

Patch 0006 was the mobile pass. Some of it was free: SDL 2.28's Emscripten driver ships a default "Standard Gamepad" mapping and libultraship hot-plugs SDL controllers, so a Bluetooth Xbox or PlayStation pad in Chrome on Android or Safari on iOS maps like desktop SoH (LT = Z, RT = R, right stick = C). Browsers only expose a pad after you press a button on it. The touch pad hides on `gamepadconnected`.

Some of it was not free. On iPhone, the file input's `accept=".z64"` filter greys ROMs out entirely, because iOS doesn't know that file type. The fix is to just remove the filter on iOS.

From `OOT-True-Co-op/0006-Web-mobile-pass-ROM-import-install-touch-controller-.patch`:

```js
var isIOS = /iPad|iPhone|iPod/.test(navigator.userAgent) || (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);
...
if (isIOS) $('rom-file').removeAttribute('accept');
```

`.zip` ROMs get unpacked in the browser with `DecompressionStream`. After extraction the page stores the archive and reloads, since wasm memory never shrinks and phones kill fat tabs early. The web manifest's start URL carries the invite key, because an iPhone home-screen app gets its own cookie jar. The lobby icon also changed from the three-triangle emblem to an original fairy glyph, since that emblem is Nintendo's.

One thing deliberately not done: forcing iOS audio into `playback` mode to beat the silent switch. That would fight TeamSpeak for the audio session, and TeamSpeak wins.

> **NOTE** All of this was verified in iPhone 13 and Pixel 7 emulation (14/14 checks). Not verified at the time: a physical controller actually driving Link, or phone memory during a real ROM unpack.

## 3.6 Tab out, freeze the world

Here's the bug that only exists because of the co-op design. On Day 2 we made enemy authority belong to the lowest client ID in the scene. That machine runs the enemies; everyone else mirrors.

Browsers stop `requestAnimationFrame` in background tabs. The game loop runs on rAF. So if the authority player alt-tabbed to TeamSpeak, their game stopped, their Anchor traffic stopped, and every enemy froze for the whole room.

Fix one, for desktop browsers: when the tab is hidden, drive the loop from timers instead.

From `OOT-True-Co-op/0005-Web-lobby-UI-extraction-fix-settings-persistence-con.patch`:

```cpp
// Background tabs get no requestAnimationFrame, so the game (and its Anchor
// traffic) froze whenever someone tabbed out. For the room's authority that
// froze every enemy for everyone. While hidden, drive the loop from timers.
// Browsers keep ~60 Hz timers for tabs that are playing audio; a silent tab
// may drop to 1 Hz, which still keeps the connection and authority alive.
EMSCRIPTEN_KEEPALIVE
void web_set_hidden(int hidden) {
    if (hidden) {
        emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, 16);
    } else {
        emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
    }
}
```

Phones are worse. A hidden phone page is frozen outright, timers included. So the phone stayed "online" until the relay's ping timeout noticed, about 50 seconds, and the room's enemies sat there as statues the whole time. Fix two: phones just leave.

![Authority handoff timeline](diagrams/devlog-03-handoff.svg)
*FIG 3-5 — Before: a hidden phone holds authority hostage for ~50 s. After: it steps out and authority moves within a tick.*

From patch 0006:

```cpp
EMSCRIPTEN_KEEPALIVE
void web_anchor_suspend(int suspend) {
    ...
    if (suspend) {
        if (Anchor::Instance->isEnabled) {
            printf("[Web] Page hidden on a phone: leaving the co-op room until it's back.\n");
            Anchor::Instance->Disable();
            s_anchorSuspended = true;
        }
    } else if (s_anchorSuspended) {
        s_anchorSuspended = false;
        if (CVarGetInteger(CVAR_REMOTE_ANCHOR("Enabled"), 0)) {
            printf("[Web] Page visible again: rejoining the co-op room.\n");
            Anchor::Instance->Enable();
        }
    }
}
```

The page's `visibilitychange` handler calls it only on mobile and only once the game has started. Your Link blinks out of the room while you check TeamSpeak, and the next-lowest client picks up the monsters.

## 3.7 The honest scorecard

Before any of this touched a real ROM, the agent ran headless suites through a SWAG-equivalent proxy with timeouts cut to 10 seconds: relay 6/6 (key enforcement, idle survival, a 70 KB packet, NUL drop, dead-peer drop), lobby 17/17, mobile emulation 14/14. All green.

And the doc said it plainly in bold: **still not verified: a real ROM extraction and gameplay.** That turned out to be exactly where it broke. The agent had already found one extractor bug that meant the first web bundle could never have gotten past the ROM step. Then I dropped my own ROM in and the game crashed anyway, and then the Kokiri showed up with green faces and static for eyes. My reaction at the time: "oh god its terrifying."

That's tomorrow. Day 4 is the debugging war stories, including the fixes I made myself.
