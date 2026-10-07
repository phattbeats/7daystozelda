# PHA-4060 trailer capture kit

How the first 7 Days to Zelda trailer and short-form clips were shot and cut. Finished
videos, thumbnails and the raw takes live on Nextcloud at
`PHATT-TECH/Projects/7daystozelda/trailer/` (not in this public repo).

## capture-only.patch — never deploy

Applied on top of the live build (main 42e8dc0, `soh-web:pha4046`) in a throwaway tree:

- `sevendays_test_cam(mode, eye0[3], at0[3], eye1[3], at1[3], frames, fov0, fov1, hud)` (z_camera.c):
  scripted main camera. Mode 1 flies eye/at in world space, 2 is relative to Link, 3 orbits
  `at` with eye = (angle°, height, radius). Smoothstep over `frames` game frames. 0 = off.
- `hud`: 1 hides Interface_Draw, name tags, ImGui toasts and the 7DtZ notices/clock/card;
  2 keeps the day card; 3 keeps the 7DtZ notices ("You got 4 Fiber!") for gather/build shots.
- Placeables draw without culling, so wide shots show the whole base.
- 4x display-list pools in graph.c. A full 94-piece base plus a raid overran the stock
  POLY_OPA buffer (`Unhandled OP code: 0x8` in interpreter.cpp, then a renderer SEGV).
- `gSevenDays.TrailerBudget` (wave budget override), `raid('story:12')` (skip the prologue),
  `gSevenDays.TrailerQuiet` (no Navi lines over shots; the saves can't keep "seen" flags).

## rig/

A 4-client co-op room on one box: Anchor relay, `web/server.js`, the `sink.py` recorder
endpoint and four `playd-gpu.py` daemons (`up.sh`). Paths are hardcoded to `/tmp/z4060`.

- A = room owner and raid director (always owner when online: lowest client id).
  B = camera (1280x720, `TAP=1` audio tap). C, D = extra Links (shrink them to 320x180).
- `h.py` helpers: `CAM()`, `REC_START()/REC_STOP()` (CDP screencast JPEGs plus a
  MediaRecorder on a tapped AudioDestinationNode, synced by wall clock), `POST()`, `CLR()`.
  `mk.py <take>` turns a take into a 30 fps CFR mp4 with aligned audio.
- `fort2.py` is the radius-600 octagon (gatehouse, rampart decks, 2-storey corner towers).
  `repair.py` re-places whatever the last raid broke.
- `shot.py "<dict>"` runs `act.py` on all four clients: zones, post/idle/delay roles, Z-target
  plus sword AI, director upkeep on A, and recording on B.
- `dir_night.py` + `cam_night.py`: bring dusk, force a raid, and film the red-sky turn.

Gotchas learned the hard way:
- `captureStream` + MediaRecorder only reached 5-18 fps (GPU readback). The CDP screencast at
  720p q80 gets 20-35 fps, as long as the box isn't CPU-bound by other runs' rigs.
- Copied profiles share Anchor `LastClientId`. Zero it in `/persist/shipofharkinian.json`
  (`cid.py`) or the clients kick each other.
- The camera client's own movement follows the cinematic camera's direction: keep B still or
  Z-targeted. B's Z-target also adds letterbox bars (crop them in the edit).
- Each forced raid lasts about 3-4 min before the director scripts dawn. Spawn extra
  ReDeads/Gibdos with `anchor_test_mini('spawn:144,16129,x,0,z,0')` on A for staged fights.
- `sevendays_test_save` crashed every tab once (adult in Hyrule Field). Don't save mid-shoot.

## edit/

`segs.py trailer.json` renders 1080p segments; `assemble.py` cuts the 16:9 trailer
(field-day music → nightfall sting → raid bed → OoT title theme, loudnorm -14 LUFS).
`vsegs.py` + `vassemble.py <plan> <out>` make the 9:16 versions (4:3 crop over a blurred
fill, caption PNG on top, logo + URL end card). This ffmpeg has no drawtext, so
`textpng.py` renders the Cinzel captions with PIL.
