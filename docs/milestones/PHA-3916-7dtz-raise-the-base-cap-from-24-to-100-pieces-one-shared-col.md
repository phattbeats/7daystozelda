# PHA-3916: 7DtZ: raise the base cap from 24 to 100+ pieces (one shared collision actor, bigger dyna budget)

Status at export (2026-10-03): in_review

Brandon on PHA-3870 (2026-10-03): "can we up the dynamic budget? 24 pieces is not much at all".

**What limits the cap today** (tree 4cf67f7)
- `BASE_CAP = 24` (SevenDays.h:174) is our own safety margin. The engine does not impose it.
- `BG_ACTOR_MAX 50` (include/z64bgcheck.h:17) is a fixed array of dynamic-collision actors per scene. It is shared with vanilla movers: platforms, doors, the Kokiri shop's switch and so on. `BGCHECK_SCENE` is defined as `BG_ACTOR_MAX`, so the scene's own collision id moves with it.
- The dynamic poly/vertex lists are 512 each outdoors, and SoH already doubles them to 1024 (z_bgcheck.c:1613). A box piece costs 12 polys and 8 vertices, so 24 pieces use only about 290 polys.

**Plan, in order**
1. **One collision actor for the whole base.** Today each piece is its own DynaPolyActor (Placeables.cpp:285). Instead, build all pieces' boxes into one combined CollisionHeader on a single "base collision" actor, rebuilt only when a piece is added, removed or broken. The base then uses 1 of the 50 slots no matter how many pieces it has. Pieces keep drawing and running their behavior as separate actors, just without their own collision.
2. **Raise the dynamic poly/vertex lists when 7 Days is on.** Raise them to 4096/4096 in outdoor scenes where the raid clock runs. That costs about 64 KB of play-state arena. Check the THA headroom in the web build first, because the browser heap is tighter than desktop's.
3. **Raise `BASE_CAP`, measured rather than guessed.** Target 100 pieces, and higher if the web build holds frame time on a phone. Decorative pieces with no collision (flags, banners, signs) don't count toward the cap.
4. **Leave `BG_ACTOR_MAX` alone** unless step 1 turns out not to be enough. Changing it is possible, since it's a compile-time constant and desktop and web are built from the same source, but it touches every vanilla actor that compares against it.

**Watch for:** raid routing and damage checks loop over pieces every frame on the enemy authority. Make sure that stays cheap at 100+ pieces in the browser build, using spatial bucketing if needed. The base save section and the BASE_STATE packet grow with the piece count, so check join-time sync with a full base.

**Done when:** a 100-piece base in Kokiri Forest and one in Hyrule Field load, collide and survive a raid with two players, at least one of them in the browser build. It is verified in the live game with screenshots, and COOP-M3-TEST-GUIDE still passes.

