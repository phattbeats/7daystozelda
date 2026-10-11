# Prop break sync rig (#4167)

Same two-client rig as `tools/harness/pha4053` (paths under `/tmp/z4167`, ports 18467/43467/19867/19877).

- `anchor_test_world(4, actorId, 0, 0)` hits the nearest pot/crate/bush/rock of that actor id within 150 units, as a weapon would.
  `prop.py` wraps it (`W(client, cmd, a, b, c)`).
- `t3.py` (Kakariko, `raid('warp:db')`): bush, crate, small crate and rock each break on both clients; the drop (En_Item00) appears only on the hitter.
- `t7.py` (Deku Tree, `raid('warp:0')`): pot breaks on both, drop only on the hitter; bush B->A direction.
- `t10.py`: a real sword slash (`key('c')` after `kit()`) on a pot spawned in front of Link.
