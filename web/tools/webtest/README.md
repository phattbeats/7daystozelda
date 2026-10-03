# webtest: headless browser tests for the web build

What found and verified PHA-3860. Runs real Chromium (Playwright, software WebGL) against the bundle
behind a SWAG-equivalent nginx and a local Anchor server.

## Setup (once)

- `pip install playwright` (Chromium preinstalled or `playwright install chromium`), Node 18+, nginx,
  and the Anchor server binary (`go build` of github.com/garrettjoecox/anchor) at `~/anchor-server/anchor`.
- `export WEBTEST_DIR=~/webtest-work` (default). Put inside it:
  - `zelda/oot-rev2.z64`: your own ROM (never commit it)
  - `o2rpick/oot.o2r`: an extracted archive (for render/join tests)
  - `swagtest/`: this folder's `nginx.conf` + `config/` (edit the absolute paths in `nginx.conf`
    to your `$WEBTEST_DIR`; it mirrors SWAG's `proxy.conf` with timeouts cut to 10 s)
- `up.sh` starts Anchor, the bundle server (`ACCESS_KEY=testkey`, `PING_MS=3000`) and nginx on :18443.
  `web.sh <public_dir>` restarts only the bundle server, e.g. to A/B an old build.

## Tests

| Script | What it checks |
| --- | --- |
| `relay-test.js` | invite key, idle survival past proxy timeout, 70 KB packets, NUL guard, dead-peer drop |
| `lobby-test.py` | lobby, stale-cache cleanup, invite link, ROM checks, extractor refusal, settings persistence |
| `mobile-test.py` | iPhone/Android emulation: picker, zip import, touch pad, controller hand-off, rotate hint |
| `extract-test.py` | real ROM → in-browser extraction (~40 s) |
| `render-test.py URL TAG` | boots via the debug warp screen (`SCENE`, `RIGHTS`, `WALK` env) and screenshots |
| `drive.py PROFILE URL steps…` | scripts one persistent profile (`o2r`, `cfg`, `solo`, `join`, `key:x*2`, `wait:5`, `shot:name`); used to create saves |
| `jointest.py URL TAG` | two saved profiles (A, B) join one room; logs + screenshots; flags page errors |

Creating a save for profile A (file select → quest → name "A" → start):
`python3 drive.py A "http://127.0.0.1:18443/?key=testkey" o2r cfg solo wait:35 key:space wait:6 key:x wait:5 key:x wait:8 key:x wait:2 key:x wait:2 key:space wait:3 key:x wait:6 key:x wait:4 key:x wait:30 shot:game`
Repeat for B with its own profile (a copied profile shares A's Anchor client id).

Screenshots and logs land in `$WEBTEST_DIR/render/`.
