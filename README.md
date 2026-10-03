# 7 Days to Zelda

*Ocarina of Time* as a co-op survival game: gather materials, craft, build a base in
Kokiri Forest, and hold it against raids that come with the night. It runs natively and
in the browser (WebAssembly) at https://zelda.phatt.vip.

The game is a fork of [OOT-True-Co-op](https://github.com/bghill95/OOT-True-Co-op), which
builds on [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) and
[Anchor](https://github.com/garrettjoecox/OOT). The co-op layer's original README is
[docs/OOT-TRUE-COOP-README.md](docs/OOT-TRUE-COOP-README.md).

**No ROM or game assets are in this repository.** Players supply their own legally
dumped *Ocarina of Time* ROM. The web build extracts it in the browser.

## Layout

| Path | What |
| --- | --- |
| `soh/soh/SevenDays/` | The 7 Days to Zelda mode: materials, crafting, the Workbench, placeables, raids, loot, nights, merchants, NPC lines |
| `soh/soh/Network/Anchor/` | Co-op sync (enemies, bosses, cutscenes, effects) |
| `libultraship/`, `ZAPDTR/`, `OTRExporter/` | Vendored at the `web-port` commits the build uses (they were submodules) |
| `web/` | The zelda.phatt.vip server (`server.js`: static site plus the Anchor relay over WebSocket), lobby page, Dockerfile, SWAG config |
| `web/deploy-history/` | Lobby page and Dockerfile for each live deploy |
| `web/tools/webtest/` | Browser tests for the lobby, joining, rendering and mobile |
| `web-build/` | How to build the web bundle (emsdk 3.1.64, the prebuilt build tree) |
| `tools/harness/` | Headed-Chrome live-play harness (playd, pc.py snippets) used for milestone tests |
| `docs/design/` | Feasibility study and the deployment runbook |
| `docs/milestones/` | The spec for each milestone (M4-M10) and follow-up issue |
| `docs/devlog/` | Devlog posts, images and diagrams |
| `art/` | Logo, crest, key art, homepage art sources |

Built web bundles and test evidence (screenshots, videos, reports) are attached to
[GitHub Releases](../../releases), not committed.

## Build

- Native: see the Ship of Harkinian build docs (`docs/BUILDING.md`).
- Web: [web-build/BUILD-WEB.md](web-build/BUILD-WEB.md). An incremental build needs the
  prebuilt build tree (a release asset); a cold build needs about 12 GB of RAM for the
  randomizer tables.

## Deploy

[docs/design/deployment.md](docs/design/deployment.md). The site's invite key is set with
`ACCESS_KEY` at deploy time and is not stored here.

## Not in this repository

These stay private on Nextcloud (`PHATT-TECH/Projects/7daystozelda/`) because they are
Nintendo-owned or third-party:

- `rom/`: the OoT ROM and the `oot.o2r` extracted from it
- `homepage-art/title-theme.mp3`: the lobby music
- `outside-asset-downloads/OOTStyleModels.zip`: third-party models
