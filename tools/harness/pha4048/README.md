# Barinade test scripts (PHA-4048)

Same rig as `../pha4047` (relay, `web/server.js`, two GPU Chromes, `drv.py`,
`up.sh`, `join.py`). These files replace the King Dodongo loader:

- `kd.py`: loaded on each client; `VA(cmd, arg)` wraps `anchor_test_va`
  (1 sword hit on part `arg`, 2 boomerang hit on part `arg`, 3 and 4 set
  `fightPhase` and `phase4HP` on the host, 5 un-clear the room).
- `enter.py`: kit, warp both clients to Barinade's room (`warp:0x301`).
- `fl.py`: `st()` and `pr()` report both clients' state.
- `full.py`: the whole fight, intro to heart container and blue warp.

Paths are `/tmp/z4048/t`; sed them for a new run. Un-clear the room
(`VA(5)`) before a second run.
