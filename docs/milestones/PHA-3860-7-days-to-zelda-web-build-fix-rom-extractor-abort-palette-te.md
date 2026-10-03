# PHA-3860: 7 Days to Zelda web build: fix ROM extractor abort + palette-texture corruption

Status at export (2026-10-03): done

The web build at zelda.phatt.vip aborts during ROM extraction. `Module.ccall("web_extract_rom")` logs `ROM validated. Version: Vanilla`, then `Aborted()` with no message, and the page shows the -4 error. This was reproduced on 2026-10-01 in headless Chromium with a clean NTSC 1.2 ROM (sha1 41b3bdc48d98c48529219919015a1af22f5057c2). Desktop SoH 9.1.1 extracts the same ROM fine, and the web build boots from that oot.o2r, so only the in-browser extractor is broken.

Likely causes: a C++ throw in a no-exceptions build, a std::thread start without pthreads, or an assert in ZAPDLib/OTRExporter.

Next step: rebuild the web target with `-sASSERTIONS=1 -g2` (and possibly `-fexceptions`) per src/BUILD-WEB.md to get a symbolized stack.

Constraint: the build needs about 12 GB of RAM plus swap. PHATT-RAID had only 6 GB free and a load average of about 18 on 12 cores, and /mnt/user is 100% full, so it should not be built there without planning. Build on Brandon's PC or another box.

Current workaround is in the PHA-3856 deployment doc: players load oot.o2r made by desktop SoH 9.1.1.

## Bug 2: palette-texture corruption in the web renderer (reported 2026-10-02)
Brandon booted the game with the desktop-made oot.o2r and played into Kokiri Forest. A Kokiri girl has a solid green face, and her eyes and hair show blue/green static. Link and the Kokiri's clothes render normally. Screenshot: PHA-3856 comment 428aba9a, attachment 81b46b1d-ddf4-4ac4-a634-d54803ba455c.

Hypothesis (unconfirmed): CI4/CI8 textures pick up the wrong TLUT, here the tunic-green palette. That would be a texture-cache key collision in the Fast3D/libultraship texture cache that only appears on wasm32 (32-bit pointers or size_t), since desktop 64-bit renders the same o2r correctly.

Where to look: the TextureCacheKey hash/equality and the palette address handling in libultraship `src/fast/interpreter.cpp` (import_texture_ci4/ci8, palette hashing).

Fix this in the same debug rebuild as the extractor abort.

