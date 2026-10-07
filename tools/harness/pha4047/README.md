# Boss test rig (PHA-4047)

Two local clients fight a boss together: a stock Anchor relay, `web/server.js`
and two GPU headless Chromes (`playd-gpu.py`), each driven over HTTP. Built for
King Dodongo; the boss adapters after it (PHA-4048 to 4054) reuse it. The
per-boss checklist is `docs/design/coop-boss-checklist.md`.

The scripts use `/tmp/z4047/t` as the rig directory and `/tmp/mOOT-True-Co-op`
as the build tree. Change those paths (sed) for a new run.

## Setup

1. Relay: copy the stock Anchor binary (see PHA-3934 in the harness notes) to
   `anchor/anchor` and byte-replace its port with yours (`up.sh` uses :43447).
2. `serve/`: soh.data, soh.o2r, index.html and the art from a previous rig;
   `install.sh` copies soh.js/soh.wasm from the build tree and bumps `soh.js?v=`.
3. Profiles `prof-A`/`prof-B` with a save past the intro, made on the same
   origin (:18422). IndexedDB saves are per port.
4. `./up.sh`, then `join.py` on both (`./pc.sh A < join.py`, then B).
   `down.py` kills only this rig's processes.

## Driving a fight

- `kd.py` (loaded on the client): `KD(cmd, arg)` wraps `anchor_test_kd`,
  `kit()` gives Infinite Health, the Deku Shield, the Kokiri Sword on B and a
  bomb bag, and turns on verbose sync logs. `W(x,y,z,yaw)` warps Link.
- `drv.py` (runs locally): `run('A'|'B', code)`, `both(codeA, codeB)`,
  `kd(client, cmd, arg)`.
- `raid('warp:40B')` enters King Dodongo's room. A test warp doesn't trigger
  boss co-entry, so warp both clients. Drop each Link into the pit with
  `W(-890,-1300,-2804,0x8000)`; the intro starts when that Link falls below
  y -1223.
- `cycle.py`: `cycle(swallower, hitter)` puts the swallower in his lane, feeds
  a bomb into his mouth during the inhale (cmd 1), then walks the hitter up to
  his head and swings the sword. If no swing lands, it falls back to cmd 2.
- `t_aim2.py` (fire at the mirror player), `t_roll.py` (both in the roll path),
  `t_death.py` (follows the death on both), `t_defer.py` + `t_defer2.py` (the
  partner wins while your own intro plays).

Logs: verbose sync lines land in `playd-A.log`/`playd-B.log` (Chrome stderr
CONSOLE lines). Grep `KingDodongoSync`, `HITREQ`, `EVENT`, `DIED`, and the
desync canaries `Fuzzy|No match|limb mismatch|overflow|split brain|parse error`.
