# #4062 rig: display-list overflow and large-base tests

Scripts that reproduced the stock-build crash and measured the fix. Paths are hardcoded to `/tmp/z4062`
(copy the directory there as `t/`, plus the relay binary in `t/anchor/`, four Chrome profiles
`t/prof-A..E`, and a build served from `/tmp/z4062/<serve-dir>`). Chrome libs come from
`/tmp/vlibs4055/gpu-env.sh` (see `tools/harness/pha4055`). The profiles are copies of the #4060 saves
(`tools/trailer/pha4060/rig`): four adult Links in Hyrule Field, room `trailer`.

| Script | What it does |
| --- | --- |
| `up.sh`, `down.py`, `restart.sh <serve>` | start/stop relay, `web/server.js`, four `playd-gpu.py` daemons; `restart.sh` also clears Chrome's cache (a stale `soh.wasm` fooled the first 256 run) and joins A, then B/C/D |
| `x.py <A-D> 'code'` / `-f file` | run Python inside a client's page with the `h.py` helpers |
| `h.py`, `gfx.py` | helpers; `GFX('')`, `GFX('reset')`, `GFX('force:N')` read/clear/force the pool counters (`sevendays_test_gfx`) |
| `fortbuild.py` | the 94-piece #4060 fort (fort2) |
| `fortbuildn.py` (`N`, `KIND` = mixed/heavy, `CAP`) | N pieces in rings inside BASE_RADIUS; prints placed count, dyna lists and the owner's refusal reasons |
| `spawn24.py` | 24 ReDeads/Gibdos round the base (host side) |
| `pressure.sh <scale> <raiders/24> <drawdist> <shrink>` | raiders, draw distance, posts, 40 s crash watch, then 30 s of fps/pool/memory per client |
| `repro.sh <serve> [scale]` | restart + fort + 48 raiders + draw distance 4 + stats: the crash on stock main, the pass on the fix |
| `perf.sh <serve> N...` | fresh rig per N, B measured (others 320x180) |
| `typecost.py` | per piece type: 24 pieces in front of B, fps and pool bytes |
| `failsafe.py` | force N overrun frames: the tab must stay up |
| `churn.py` | scene transitions (Kakariko and back) and day/night/blood moon cycling |
| `joinE.sh` | fifth client late-joins the room |
| `final.sh <serve>` | the whole sequence on one build |

Reading the stats (`sevendays_test_gfx`): `opa.peakUsed` is bytes of POLY_OPA used at the end of a frame
(head = display list, tail = `Graph_Alloc` matrices), `minFree` the smallest room left, `overflowFrames`
frames replaced by an empty list, `actorSkips` / `placeableSkips` draws skipped for lack of room.
Run a build at stock size with `cv('gSevenDays.GfxPoolScale', 1)`.
