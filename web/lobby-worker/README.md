# zelda-lobby (PHA-4155)

Lobby directory for 7 Days to Zelda, as a Cloudflare Worker with one SQLite Durable Object. Same shape as CS Party's
lobby Worker. It owns **lobby codes** (5 characters, no 0/O/1/I/L), **public/private**, and **matchmaking by game
progress**. The game itself does not move: players still connect to `zelda.phatt.vip`, and `/anchor` still bridges to the
Anchor relay. A lobby is the room `lb-CODE`.

Only soh-web calls the Worker (header `x-lobby-secret`), so the site's invite key still decides who can see lobbies.

```
browser --(same origin, invite key)--> soh-web /api/lobby/*, /api/rooms, /api/quickplay
                                          |  LOBBY_URL + LOBBY_SECRET set: forwards to the Worker
                                          '--> zelda-lobby Worker (Directory DO): codes, public list, quick match
soh-web bridge --(POST /v1/report, on every join/leave/save and every 30 s)--> Worker: who is in lobby lb-CODE, its progress
```

Without `LOBBY_URL`/`LOBBY_SECRET`, soh-web runs the same API from its own memory (lobbies are forgotten on restart).

## API (all but /healthz need `x-lobby-secret`)

- `POST /v1/create {pid, name, public, stage, ip}` -> `{code, room, public, ...}`. The creator's `pid` (random id in their browser) is the host.
- `GET /v1/room/:code` -> room view, 404 if there is no such lobby. Private lobbies are found by code.
- `POST /v1/visibility {code, pid, public}`: host only (403 otherwise).
- `GET /v1/rooms?gs=&dn=&h=&r=&d=&i=`: public lobbies with a free seat or players, nearest progress first.
- `POST /v1/quick {stage}`: nearest public lobby with a free seat within `MATCH_RANGE`, else a new public one. Holds the seat 2 minutes.
- `POST /v1/report {room, players, stage}`: from soh-web. Players not re-reported for 90 s count as gone.

Private lobbies never appear in the list and quick match never picks them. Creates are limited to 10 per IP per 10 minutes.

## Develop and test

```
cd web/lobby-worker && npm install && cp .dev.vars.example .dev.vars
rm -rf .wrangler/state && npm run dev        # :8788
npm test                                     # 22 checks against the running Worker
REMOTE=1 node ../../tools/harness/pha4155/lobby_codes_test.js   # soh-web + fake relay + this Worker, end to end
```

## Deploy (needs a Cloudflare API token with Workers Scripts: Edit, Durable Objects)

```
export CLOUDFLARE_API_TOKEN=... CLOUDFLARE_ACCOUNT_ID=...
npx wrangler secret put LOBBY_SECRET      # any long random string
npm run deploy                            # prints https://zelda-lobby.<subdomain>.workers.dev
```

Then give soh-web the same pair: `LOBBY_URL=<that URL>` and `LOBBY_SECRET=<that secret>` in the container env and recreate it.
The pair was not set on the live container when this was written; see `docs/design/deployment.md`.
