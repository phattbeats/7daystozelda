# Authority-loss rig (#4111)

Reuses a boss rig (copy of `/tmp/z4052/t`: relay, `web/server.js`, two GPU Chromes via `playd-gpu.py`; ports 18452/43411/19871/19872 —
the web origin must stay on the port the profiles' IndexedDB was made on). Needs `drv.py`/`join.py` from the other `pha40xx` dirs.

- `boss.py LABEL WARP` (env `HIT='kill:%d'` optional): both clients warp into the boss room; the host (lowest client id) is found; its JS thread is
  frozen 8 s (stale stream) and the other client's authority/enemy list sampled; the host then leaves (`about:blank`), the other client's
  takeover time and enemy count are sampled; the host rejoins, re-warps and both enemy lists are compared; desync canaries are grepped.
- `frz.py PROBE HOST SECS`: freeze only, with the boss probe's position/tick on the mirror. `leave.py PROBE HOST ROOM`: leave + rejoin only.
- Warps: KD 40B, Gohma 40F, Barinade 301, Morpha 0x417, Bongo 413, Twinrova 8D (adult), Ganondorf 0x41F, Ganon 0x517.
