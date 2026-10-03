# PHA-3863: [Brandon/host] 7DtZ web: rebuild with a bigger audio buffer to fix laggy audio (PHA-3860)

Status at export (2026-10-03): done

Brandon reported laggy audio on zelda.phatt.vip. The fix needs a wasm rebuild, and the build tree is only on Brandon's PC. PHATT-RAID only has the patch files.

**Likely cause (from reading the patches, not measured):**
- The web build has no audio thread. `OTRAudio_ProcessInline()` (OOT-True-Co-op patch 0004) mixes one tick of audio (about 528–560 samples × 3 frames) on each game tick, which is driven by requestAnimationFrame.
- Output goes through stock libultraship SDL audio. None of our libultraship patches change it. On Emscripten that means a `ScriptProcessorNode` on the main thread (the boot log shows the deprecation warning), with the stock small buffer.
- So any slow frame, GC pause, texture upload or WebSocket burst on the main thread empties the buffer, and you hear stutter or crackle. On a slow device, game ticks fall below 20/s, so audio is starved all the time.

**Suggested fix, smallest first:**
1. Under `__EMSCRIPTEN__` in the SDL audio player: raise `want.samples` to 2048 or 4096, and raise `AudioPlayer_GetDesiredBuffered()` to match (about 100–150 ms). This trades a little latency for no underruns.
2. In `OTRAudio_ProcessInline`: when `AudioPlayer_Buffered()` is well below the target, mix an extra update so one slow frame doesn't cause a gap.
3. Later, if needed: SDL's AudioWorklet backend (needs pthreads/SharedArrayBuffer and COOP/COEP headers, which is a bigger change).

**Please also tell me which symptom you heard:** sound delayed behind the picture, or choppy/crackly sound? Option 1 fixes choppy. If it's a constant delay instead, the buffer is too *full*, and the fix goes the other way.

When you have the bundle: drop it in the Nextcloud 7daystozelda folder and mark this done. I'll deploy it, including the `soh.js?v=` cache-bust edit.
