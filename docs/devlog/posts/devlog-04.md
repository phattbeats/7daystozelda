title: 7 Days to Zelda, Day 4: The terrifying Kokiri
slug: 7dtz-devlog-04-the-terrifying-kokiri
excerpt: Five browser bugs in one night, from a ROM extractor that could never finish to a Kokiri girl with a solid green face, and the one overflowing table behind the scariest two.
tags: 7 Days to Zelda, devlog, debugging, WebAssembly, WebGL, Ship of Harkinian
feature_image: img/green-kokiri.png
publish: Day 4 (see schedule)

---

Day 3 ended with Ocarina of Time running in a browser tab. Day 4 is about what happened when I actually tried to play it with the boys. Between about half past midnight and four in the morning on October 2nd, we hit five separate bugs. Two of them turned out to be the same bug wearing different masks.

Quick reminder of who did what: the AI agent (running as "Vision Quest" through Paperclip) wrote most of this port. This night was different. Two of the three worst fixes, the extractor abort and the texture table, plus the audio rebuild, landed in commits under my name from my own PC, because that's where the build tree lived. The agent diagnosed, filed, deployed and nagged. I'll be clear about who did which part as we go.

And as always with this project: you bring your own legally dumped ROM. The page unpacks it inside your browser and never uploads it. No Nintendo assets are distributed.

## 4.1 The bug board

| # | Symptom | Where | Root cause | Fix | Who |
|---|---------|-------|-----------|-----|-----|
| B1 | ROM step never finishes | page shell | `Module._free` not exported; JS threw before handling the result | `Module.ccall` | agent (patch 0005, Oct 1) |
| B2 | `Aborted()` right after "ROM validated" | `Extract.cpp` | a `std::thread` for an "extracting" popup in a build with no threads | `__EMSCRIPTEN__` guards restored | Brandon (patch 0007) |
| B3a | Kokiri girl with a solid green face, static eyes and hair | libultraship OpenGL backend | fixed 1,024-slot texture table, WebGL IDs past 2,400 | vector + bounds checks | Brandon (lus 0005 upstream + 0006) |
| B3b | `memory access out of bounds` when a second player joins | same | same overflow, now into the shader map | same | Brandon |
| B4 | Stuck on "Connecting to boys..." | Cloudflare | `.js` cached 4 h, new `.wasm` paired with old `soh.js` | `soh.js?v=<md5 prefix>` | agent |
| B5 | Choppy audio | SDL audio, web | buffer too small, main-thread underruns | 2048/4096 samples, ~100/130 ms queue | Brandon (lus 0007, OOT 0008) |

![Boot timeline with five bug markers](diagrams/devlog-04-bug-timeline.svg)
*FIG 4-1 — Where each bug bit, in the order a player hits them. B4 sits at the very front but was the last one found.*

## 4.2 B1: an extractor that could never have worked

This one was caught before I ever touched the site, which is the only reason it isn't a war story. During the Oct 1 "bug, UI and stability" pass, the agent fed a fake byte-swapped ROM through the real extractor and noticed the page never handled the result. The shell (inherited from zalo's shipwright-64 port) passed strings into wasm and then freed them with `Module._free`, which this build doesn't export. So the JS threw right after the extractor returned, and the page just sat there. In the agent's words: "The first web bundle could never have gotten past the ROM step."

The fix in OOT-True-Co-op patch 0005 (`shell.html`) lets Emscripten handle the strings:

```js
-        var result = Module._web_extract_rom(romPathPtr, outPathPtr);
-        Module._free(romPathPtr);
-        Module._free(outPathPtr);
+        try {
+          // ccall puts the strings on the wasm stack and frees them itself. The
+          // old allocateUTF8 + Module._free path threw (_free isn't exported),
+          // so every extraction, good ROM or bad, hung at this step.
+          result = Module.ccall('web_extract_rom', 'number', ['string', 'string'], ['/rom.z64', '/oot.o2r']);
+        } catch (e) {
+          console.error('[Web] extractor crashed', e);
+          result = -4;
+        }
```

Note that `-4`. It comes back in B2.

## 4.3 B2: the popup that killed the page

With B1 fixed, the extractor got further and died differently. The agent reproduced it on 2026-10-01 in headless Chromium with a clean USA 1.2 dump: the log says `ROM validated. Version: Vanilla`, then `Aborted()` with no message, and the page shows the -4 error.

![Lobby with crash card over it](img/rom-crash.png)
*FIG 4-3 — The lobby after a failed unpack: "Unpacking finished without producing game data," then THE GAME CRASHED card on top.*

The agent's guesses were reasonable: a C++ throw in a no-exceptions build, a thread without pthreads, or an assert in ZAPD. It wanted a debug rebuild (`-sASSERTIONS=1 -g2 -sSAFE_HEAP=1 -fexceptions`) and figured that needed a 12 GB box. It filed a blocker on me for that.

Turned out I didn't need the box. The heavy parts were already compiled on my PC, so each fix was a small rebuild of about 30 seconds. And the cause was my own fault. From my comment on #3861:

> "That one was my mistake. When I ported the ROM extractor to the browser, I dropped zalo's browser-specific guards. So it tried to show an 'extracting…' popup on a background thread, and this build can't run threads, so it died silently right after 'ROM validated'."

Here's the guard that went missing, from OOT-True-Co-op patch 0007 (`soh/soh/Extractor/Extract.cpp`):

```cpp
-#else
+#elif !defined(__EMSCRIPTEN__)
     // Show extraction in background message until linux/mac can have visual progress
+    // (Not on web: there are no threads, and std::thread aborts the whole page.)
     std::thread mbThread(MessageboxWorker);
     mbThread.detach();
 #endif
```

The same patch makes every dialog either log to stderr or auto-answer "yes" on web, since the player already picked the file in the page. ZAPD had the same problem one layer down: zalo's ZAPDTR patch 0003 removes a `ctpl::thread_pool` whose constructor "calls pthread_create which aborts in Emscripten without -pthread." That one was already in the tree; mine just hadn't been.

After the fix, the build unpacked the ROM in about 42 seconds headless, and the result matched the desktop-made `oot.o2r` to within 2 KB. While I was in there I also raised the memory ceiling to `-sMAXIMUM_MEMORY=4GB` (extraction holds the ROM, ZAPD's state and the archive at once) and added `--profiling-funcs` so crashes print function names instead of `wasm-function[12345]`.

> **NOTE** Until this landed, the workaround was making `oot.o2r` with desktop SoH 9.1.1 and dropping that into the page. That's how I got far enough to meet the next bug.

## 4.4 B3: "oh god its terrifying"

At 00:47 I walked into Kokiri Forest and posted this, with one comment: "oh god its terrifying".

![Kokiri girl with a solid green face and static eyes and hair](img/green-kokiri.png)
*FIG 4-4 — The terrifying Kokiri. Her face is solid green, her eyes and hair are blue-green static, while Link and her clothes render fine.*

The agent's first theory was a palette mixup: the face, eyes and hair use palette (CI4/CI8) textures, and it looked like they were pulling the tunic-green palette. It guessed a texture-cache key collision that only shows up on 32-bit wasm. Plausible, wrong.

Eight minutes later, a different problem: "memory access out of bounds when someone joins."

![Crash card reading Uncaught RuntimeError: memory access out of bounds](img/oob-join.png)
*FIG 4-5 — The crash card when a second player joined the room: `Uncaught RuntimeError: memory access out of bounds`.*

The agent filed it as Bug 3 and again suspected 32-vs-64-bit struct sizing in the netcode. It even noted "one root cause may explain both." It was right about that part, wrong about which cause.

Here's what it actually was. The libultraship OpenGL backend keeps a small record per texture (width, height, filtering) in a fixed array indexed by the GL texture name:

From libultraship patch 0005 (upstream #1006, `gfx_opengl.h`), the "before" side:

```cpp
-    struct TextureInfo {
-        uint16_t width;
-        uint16_t height;
-        uint16_t filtering;
-    } textures[1024];
-
+    std::vector<TextureInfo> textures;
     GLuint mCurrentTextureIds[SHADER_MAX_TEXTURES];
```

On desktop, drivers hand out small texture names and reuse them, so 1,024 is plenty. WebGL under Emscripten hands every object name out of one counter that never recycles. By Kokiri Forest it had given out 2,400 to 2,900. Every texture past 1,024 wrote off the end of the array into whatever came next in the object:

- **First** `mCurrentTextureIds`, the record of which texture is bound. Wrong texture, wrong size, static. Green face.
- **Then** the shader program map. The next new shader follows a garbage pointer and traps. A second player's Link brings new shaders, which is why it died on join.

![Memory layout showing textures array overflowing into neighbors](diagrams/devlog-04-texture-overflow.svg)
*FIG 4-2 — The 1,024-entry table and the two neighbors that got stomped, in the order they got hit.*

The fix is upstream libultraship's own #1006 (array to vector, grown in `NewTexture()`), plus a bounds-checked accessor I added on top. From libultraship patch 0006 (`gfx_opengl.cpp`):

```cpp
const TextureInfo& GfxRenderingAPIOGL::TexInfo(GLuint id) const {
    static const TextureInfo kEmpty = {};
    return id < textures.size() ? textures[id] : kEmpty;
}

TextureInfo& GfxRenderingAPIOGL::TexInfoMut(GLuint id) {
    if (id >= textures.size()) {
        textures.resize((size_t)id + 1);
    }
    return textures[id];
}
```

Every read and write in the backend now goes through those, and `mCurrentTextureIds` is zero-initialized.

How sure are we? Honestly: the old build crashed in one of three scripted runs, and the fixed build ran four times with no crashes, including two players joining one room and seeing each other's Link, name tag and fairy. Those were scripted headless browsers, not people. The green face is random memory damage and never showed up on demand in the old build, so it was never directly re-observed as fixed. It just stopped happening.

> **WARNING** The "32-bit vs 64-bit" theory was a good guess that pointed at the wrong code. The real bug was a platform assumption (names get recycled) that only one platform breaks. If a desktop-correct renderer corrupts memory in the browser, check every table indexed by a GL name.

## 4.5 B4: stuck on "Connecting to boys..."

The fixed bundle went live at about 02:09. Two minutes later I sent this:

![Black page with a "Connecting to boys..." status pill](img/stuck-connecting.png)
*FIG 4-6 — Black screen, a yellow "Connecting to boys..." pill, and nothing else.*

"doesnt get past connecting?"

The server sent `no-cache` on every file. But Cloudflare rewrites `.js` responses to `cache-control: max-age=14400`, which is four hours, and doesn't do that for `.wasm`. So my browser paired the old `soh.js` from cache with the new `soh.wasm`, and the game never finished starting. My attempt never even reached the relay.

The agent's fix: `index.html` loads `soh.js?v=f2369db2`, the first 8 hex digits of the file's md5, so every new build gets a new URL. It verified in headless Chromium that the game booted and logged `[WebSocket] Connected!` about a second later, then told me straight that it couldn't reproduce my exact browser state, so the diagnosis was "likely, not proven." I reloaded, and joining worked.

> **NOTE** The cache-bust was hand-edited into the deployed page, not the build template, so every new bundle drops it. The agent re-applied it on each deploy (the audio build went out as `soh.js?v=83aea724`). A Cloudflare cache-bypass rule would be the real fix and still needs me in the dashboard.

## 4.6 B5: "choppy"

02:25, me: "audio seemed laggy?" The agent asked the right question back: delayed, or choppy? Delayed means the buffer is too full. Choppy means it's running dry. I said "choppy".

The web build has no audio thread. Each game tick mixes one tick of audio inline and pushes it to SDL, which on Emscripten plays through a `ScriptProcessorNode` on the main thread. The stock buffer was 1024 samples, about 21 ms. Any slow frame, GC pause or texture upload starves it.

The fix needed a wasm rebuild, so it came back to my PC. From libultraship patch 0007 (`SDLAudioPlayer.cpp`):

```cpp
#ifdef __EMSCRIPTEN__
    const bool mobile = lus_web_is_mobile() != 0;
    want.samples = mobile ? 4096 : 2048;
    // Keep ~100 ms (phones ~130 ms) of mixed audio queued so a slow frame or a
    // GC pause drains the queue instead of the speakers.
    this->SetDesiredBuffered(this->GetSampleRate() * (mobile ? 130 : 100) / 1000);
#else
```

The same patch raises the queue cap on web to 3x that target, so catch-up audio after a hitch isn't thrown away. OOT-True-Co-op patch 0008 adds the second half: when the queue drops below a third of the target, `OTRAudio_ProcessInline` mixes an extra update so it refills in one tick, and a `web_audio_queued()` export lets the test harness watch the queue. The headless A/B at the same 13 fps title screen: median queue went from 1184 to about 2720 samples, and empty samples from 21–23 to 9–10 out of 150.

The agent deployed it at 03:47 and was upfront that headless Chromium has no audio output, so "I need your ears for that." Five minutes later I confirmed it was smooth, and #3860 closed.

## 4.7 What I took away

- **Ports lose guards.** B1 and B2 were both browser-specific code that zalo's port already had, dropped while hand-porting. Diff against the reference port before you debug.
- **The agent's hypotheses were wrong twice and it said so.** Both of its theories (palette hashing, 32/64-bit structs) were reasonable and both missed. What it did well was write precise symptoms, file blockers on me instead of guessing, and verify each deploy (`/healthz`, the WebSocket upgrade, the md5 of the live `soh.wasm`).
- **Headless isn't hands.** Audio, a real phone and a real Bluetooth controller were all still listed as "not verified" at the end of the night.

Tomorrow, Day 5, is the fun one: what you actually do in 7 Days to Zelda once it stops crashing.
