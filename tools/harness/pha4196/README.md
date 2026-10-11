# Boss entry / downed follow rig (#4196)

Same two-client rig as `../pha4167` (copied from /tmp/z4181/t), paths under `/tmp/z4196/t`, tree `/tmp/qOOT-True-Co-op`.
Ports: web 18496 (pages still load :18481; `playd-gpu.py` maps it with `--host-resolver-rules` so the saved
IndexedDB origin keeps working), relay 43496 (anchor binary byte-patched), daemons A 19896 / B 19897.

- `b.py` (host side): `st(c)`/`pr(tag)` print scene, `roomCtx.curRoom.num`, Link pos, health, gameOver state and the
  Barinade probe; `do(c, code)`, `shot(c, name)` (to /tmp/z4196/ev), `logs(c)` greps BossEntry/LifeSync/CoopWarp.
- `KILL`: kills the local Link at once by writing `gSaveContext.health` = 0 (HEAP16 byte address 8706600 in this
  wasm; find it again for another build by scanning HEAP16 for the `healthCapacity, health` pair from
  `sevendays_test_raid_state`, then `raid('heal')` and keep the candidate that changed). Bombs (`raid('bomb')`)
  also work but can blast Link out of the Jabu mouth.
- `playd-gpu.py` logs console lines containing BossEntry/LifeSync/CoopWarp/Barinade/Scene Init.
- Entrances: Jabu `warp:28`, Barinade `warp:301`, Deku `warp:0`, Gohma `warp:40F`, King Dodongo `warp:40B`.
