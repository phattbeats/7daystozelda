// Ship of Harkinian (True Co-op) web server + Anchor WebSocket bridge.
//
// Serves the WebAssembly build from ./public and bridges WebSocket clients at
// /anchor to a stock Anchor server over TCP. Anchor's native protocol is JSON
// packets separated by a NUL byte; the browser build sends one JSON packet per
// WebSocket text message. This bridge converts between the two, one TCP
// connection per browser player, so browser and desktop players share rooms.
//
// Env:
//   PORT         HTTP/WebSocket listen port (default 8080)
//   ANCHOR_HOST  Anchor server host (default "anchor")
//   ANCHOR_PORT  Anchor server port (default 43383)
//   PUBLIC_DIR   static files (default ./public)
//   PING_MS      WebSocket keepalive interval (default 25000)
//   ACCESS_KEY   optional. When set, the site and the relay need it: open
//                https://host/?key=ACCESS_KEY once and a cookie remembers it.
//   ROOM_SEATS   players per public room for matchmaking (default 4)
//   MATCH_RANGE  how far apart (in gamestage points) quick match may pair you
//                with a room before it opens a new one instead (default 6)
//   LOBBY_URL    optional. The zelda-lobby Cloudflare Worker (web/lobby-worker).
//   LOBBY_SECRET When both are set, lobby codes, the public list and quick match
//                live in the Worker and this server reports who is in each lobby.
//                Without them the same API runs from this process's memory.

const http = require("http");
const net = require("net");
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");
const { WebSocketServer } = require("ws");

const PORT = parseInt(process.env.PORT || "8080", 10);
const ANCHOR_HOST = process.env.ANCHOR_HOST || "anchor";
const ANCHOR_PORT = parseInt(process.env.ANCHOR_PORT || "43383", 10);
const PUBLIC_DIR = path.resolve(process.env.PUBLIC_DIR || path.join(__dirname, "public"));
const ACCESS_KEY = process.env.ACCESS_KEY || "";
const MAX_PACKET = 4 * 1024 * 1024; // sanity cap on one Anchor packet
// Cloudflare drops WebSockets idle for 100 s and SWAG's proxy.conf times out
// at 240 s. A menu-idle player sends nothing, so ping well inside both.
const PING_MS = parseInt(process.env.PING_MS || "25000", 10);
const COOKIE = "soh_key";

const MIME = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".wasm": "application/wasm",
  ".data": "application/octet-stream",
  ".o2r": "application/octet-stream",
  ".json": "application/json",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".ico": "image/x-icon",
  ".svg": "image/svg+xml",
  ".woff2": "font/woff2",
  ".webmanifest": "application/manifest+json",
};

// Fetched by the OS/browser outside the page (home-screen install, tab icon,
// link-preview bots), often without cookies, and nothing secret: served
// without the invite key.
const PUBLIC_PATHS = new Set([
  "/manifest.webmanifest",
  "/icon.svg",
  "/icon-180.png",
  "/icon-192.png",
  "/icon-512.png",
  "/favicon.ico",
  "/favicon-16.png",
  "/favicon-32.png",
  "/favicon-96.png",
  "/og.jpg",
  // PHA-4242: the invite-only page uses the lobby's fonts and art
  "/fonts/cinzel-700.woff2",
  "/fonts/nunito-600.woff2",
  "/fonts/nunito-800.woff2",
  "/home-logo.png",
  // PHA-4335: the Clubhouse party drop-in and its glue; a party link lands here before the invite key is typed
  "/party/phatt-party.js",
  "/party.js",
  "/home-bg.png",
]);

// The link preview (Open Graph / Twitter card) for the "Invite only" page, so a
// link shared without ?key= still unfurls. index.html carries the same tags.
// PUBLIC_URL is the canonical https origin the preview image is served from.
const PUBLIC_URL = (process.env.PUBLIC_URL || "https://zelda.phatt.vip").replace(/\/+$/, "");
function fileVersion(name) {
  try {
    return crypto.createHash("md5").update(fs.readFileSync(path.join(PUBLIC_DIR, name))).digest("hex").slice(0, 8);
  } catch {
    return "0";
  }
}
const SHARE_DESC =
  "Co-op survival Ocarina of Time in your browser. Gather, craft, build walls, and hold Hyrule through seven nights of raids with your friends.";
const SHARE_IMAGE = `${PUBLIC_URL}/og.jpg?v=${fileVersion("og.jpg")}`;
const ICON_V = fileVersion("favicon-32.png");
const SHARE_HEAD = `<meta name="description" content="${SHARE_DESC}">
<meta property="og:type" content="website"><meta property="og:site_name" content="7 Days to Zelda">
<meta property="og:title" content="7 Days to Zelda"><meta property="og:description" content="${SHARE_DESC}">
<meta property="og:url" content="${PUBLIC_URL}/"><meta property="og:image" content="${SHARE_IMAGE}">
<meta property="og:image:type" content="image/jpeg"><meta property="og:image:width" content="1200"><meta property="og:image:height" content="630">
<meta name="twitter:card" content="summary_large_image"><meta name="twitter:image" content="${SHARE_IMAGE}">
<meta name="theme-color" content="#0a0c13">
<link rel="icon" href="/favicon.ico?v=${ICON_V}" sizes="48x48"><link rel="icon" href="/favicon-32.png?v=${ICON_V}" type="image/png" sizes="32x32">
<link rel="apple-touch-icon" href="/icon-180.png?v=${ICON_V}">`;

function log(...args) {
  console.log(new Date().toISOString(), ...args);
}

// ---- optional access key (same idea as CS Party's PARTY_KEY) ----
function keyEquals(given) {
  if (!ACCESS_KEY || typeof given !== "string") return false;
  const a = Buffer.from(given);
  const b = Buffer.from(ACCESS_KEY);
  return a.length === b.length && crypto.timingSafeEqual(a, b);
}

function cookieKey(req) {
  const raw = req.headers.cookie || "";
  for (const part of raw.split(";")) {
    const eq = part.indexOf("=");
    if (eq > 0 && part.slice(0, eq).trim() === COOKIE) {
      try {
        return decodeURIComponent(part.slice(eq + 1).trim());
      } catch {
        return "";
      }
    }
  }
  return "";
}

// Returns { ok, setCookie } for this request.
function checkAccess(req) {
  if (!ACCESS_KEY) return { ok: true };
  if (keyEquals(cookieKey(req))) return { ok: true };
  const q = new URL(req.url || "/", "http://x").searchParams.get("key");
  if (keyEquals(q)) {
    const secure = (req.headers["x-forwarded-proto"] || "").includes("https") ? "; Secure" : "";
    return {
      ok: true,
      setCookie: `${COOKIE}=${encodeURIComponent(ACCESS_KEY)}; Path=/; Max-Age=31536000; HttpOnly; SameSite=Lax${secure}`,
    };
  }
  return { ok: false };
}

const DENIED_PAGE = `<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>7 Days to Zelda - invite only</title>
${SHARE_HEAD}
<style>
@font-face{font-family:Cinzel;src:url(/fonts/cinzel-700.woff2) format("woff2");font-weight:700;font-display:swap}
@font-face{font-family:Nunito;src:url(/fonts/nunito-600.woff2) format("woff2");font-weight:400 650;font-display:swap}
*{box-sizing:border-box;margin:0}
body{min-height:100vh;display:grid;place-items:center;padding:24px 16px;color:#f1e9d6;font:500 16px/1.5 Nunito,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
background:linear-gradient(180deg,rgba(10,12,19,.35),rgba(10,12,19,.92)),url(/home-bg.png) center 30%/cover no-repeat,#0a0c13}
main{width:min(100%,30rem);text-align:center}
img{display:block;width:min(100%,24rem);height:auto;margin:0 auto 20px;filter:drop-shadow(0 8px 22px rgba(0,0,0,.75))}
section{background:rgba(14,17,29,.9);border:1px solid rgba(232,201,128,.2);border-radius:14px;padding:24px;box-shadow:0 14px 36px rgba(0,0,0,.45)}
h1{font:700 1.12rem/1.2 Cinzel,Georgia,serif;letter-spacing:.06em;color:#e8c980;margin-bottom:10px}
p{color:#aba493}
code{font:700 .9em ui-monospace,SFMono-Regular,Menlo,monospace;color:#f7e0a6}
</style>
<main><img src="/home-logo.png" alt="7 Days to Zelda" width="960" height="398">
<section><h1>Invite only</h1><p>This server needs the invite link. Ask whoever runs it for the full link (it has <code>?key=</code> in it).</p></section></main>`;

// ---- static files ----
// Every asset revalidates (ETag/304), so a redeploy can never mix a new page
// with an old soh.js/soh.wasm out of a browser or Cloudflare cache. A 304 costs
// a round trip, not a download. Precompressed .gz siblings are preferred.
const statCache = new Map(); // path -> {mtimeMs, size, etag}
function etagFor(p, st) {
  const hit = statCache.get(p);
  if (hit && hit.mtimeMs === st.mtimeMs && hit.size === st.size) return hit.etag;
  const etag = `"${st.size.toString(16)}-${Math.floor(st.mtimeMs).toString(16)}"`;
  statCache.set(p, { mtimeMs: st.mtimeMs, size: st.size, etag });
  return etag;
}

function serveStatic(req, res, extraHeaders) {
  let urlPath;
  try {
    urlPath = decodeURIComponent((req.url || "/").split("?")[0]);
  } catch {
    res.writeHead(400).end("Bad request");
    return;
  }
  if (urlPath === "/" || urlPath === "") urlPath = "/index.html";
  const filePath = path.normalize(path.join(PUBLIC_DIR, urlPath));
  if (!filePath.startsWith(PUBLIC_DIR + path.sep)) {
    res.writeHead(403).end("Forbidden");
    return;
  }
  const ext = path.extname(filePath).toLowerCase();
  const type = MIME[ext] || "application/octet-stream";
  const acceptsGzip = /\bgzip\b/.test(req.headers["accept-encoding"] || "");
  const gzPath = filePath + ".gz";

  const send = (p, encoding) => {
    fs.stat(p, (err, st) => {
      if (err || !st.isFile()) {
        res.writeHead(404, extraHeaders).end("Not found");
        return;
      }
      const base = etagFor(p, st);
      const etag = encoding ? base.slice(0, -1) + '-gz"' : base;
      const headers = {
        ...extraHeaders,
        "Content-Type": type,
        "Cache-Control": "no-cache",
        ETag: etag,
        Vary: "Accept-Encoding",
        "X-Content-Type-Options": "nosniff",
      };
      if (encoding) headers["Content-Encoding"] = encoding;
      if (req.headers["if-none-match"] === etag) {
        res.writeHead(304, headers).end();
        return;
      }
      headers["Content-Length"] = st.size;
      res.writeHead(200, headers);
      if (req.method === "HEAD") return res.end();
      const stream = fs.createReadStream(p);
      stream.on("error", () => res.destroy());
      stream.pipe(res);
    });
  };

  if (acceptsGzip) {
    fs.access(gzPath, fs.constants.R_OK, (err) => (err ? send(filePath, null) : send(gzPath, "gzip")));
  } else {
    send(filePath, null);
  }
}

// Home-screen install. When the request carries a valid invite cookie, the app's
// start URL carries the key too: an iPhone home-screen app gets its own cookie
// jar, so without it the installed app would open on "Invite only".
function serveManifest(req, res) {
  const keyed = ACCESS_KEY && keyEquals(cookieKey(req));
  const manifest = {
    name: "7 Days to Zelda",
    short_name: "7 Days",
    description: SHARE_DESC,
    start_url: keyed ? `/?key=${encodeURIComponent(ACCESS_KEY)}` : "/",
    scope: "/",
    display: "fullscreen",
    display_override: ["fullscreen", "standalone"],
    orientation: "landscape",
    background_color: "#0a0c13",
    theme_color: "#0a0c13",
    icons: [
      { src: "/icon-192.png", sizes: "192x192", type: "image/png", purpose: "any maskable" },
      { src: "/icon-512.png", sizes: "512x512", type: "image/png", purpose: "any maskable" },
    ],
  };
  res.writeHead(200, { "Content-Type": MIME[".webmanifest"], "Cache-Control": "no-cache", Vary: "Cookie" });
  res.end(req.method === "HEAD" ? undefined : JSON.stringify(manifest));
}

// ---- lobby: public rooms + matchmaking (#4155) ----
// The same idea as CS Party's quick play, fitted to a drop-in co-op world. A room
// is public only when someone made or listed it through this API; rooms named by
// hand stay unlisted. The bridge watches the packets it already relays: the
// HANDSHAKE says which room a browser joined, ALL_CLIENT_STATE says who is
// online there, and UPDATE_TEAM_STATE (sent on every save) carries the room's
// progress. Joining a room hands you its progress (UpdateTeamState.cpp), so quick
// match looks for the room whose progress is nearest yours.
const ROOM_SEATS = Math.max(2, parseInt(process.env.ROOM_SEATS || "4", 10) || 4);
const MATCH_RANGE = Math.max(0, parseFloat(process.env.MATCH_RANGE || "6") || 0);
const HOLD_MS = 120 * 1000; // a quick-match seat waits this long for its player to load in
const FRESH_MS = 3 * 60 * 1000; // an empty room just made by quick match stays listed this long
const FORGET_MS = 6 * 60 * 60 * 1000; // an empty room's record is dropped after this
const CODE_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
const rooms = new Map(); // roomId -> room

// Progress summary: the game's own raid gamestage (Raids.cpp Gamestage()) without
// its player-count term, plus the counters that tell two saves apart.
const DUNGEON_BITS = [0x12, 0x13, 0x14, 0x00, 0x01, 0x02, 0x03, 0x04]; // stones, then medallions
function int(v, max) {
  const n = Math.floor(Number(v));
  return Number.isFinite(n) ? Math.max(0, Math.min(max, n)) : 0;
}
function cleanStage(s) {
  if (!s || typeof s !== "object") return null;
  const st = {
    dungeons: int(s.dungeons, 8),
    hearts: int(s.hearts, 17),
    raids: int(s.raids, 9999),
    days: int(s.days, 99999),
    interval: int(s.interval, 7),
  };
  st.gs = 3 * st.dungeons + st.hearts + st.raids;
  return st;
}
// A SaveContext as UPDATE_TEAM_STATE sends it (JsonConversions.hpp).
function stageFromTeamState(state) {
  if (!state || typeof state !== "object") return null;
  const quest = Number(state.inventory && state.inventory.questItems) >>> 0;
  let dungeons = DUNGEON_BITS.filter((b) => quest & (1 << b)).length;
  const deku = Array.isArray(state.eventChkInf) ? Number(state.eventChkInf[0]) & 0x80 : 0;
  if (!(quest & (1 << 0x12)) && deku) dungeons++; // Deku Tree dead, emerald still in flight
  const c = (state.sevenDays && state.sevenDays.base && state.sevenDays.base.counters) || {};
  return cleanStage({
    dungeons,
    hearts: Number(state.healthCapacity) / 16 - 3,
    raids: c.hordeNightsSurvived,
    days: c.daysSurvived,
    interval: c.raidInterval,
  });
}
// Lower is closer. Gamestage drives the raids, so it dominates; days and a
// different raid interval break ties between rooms at the same stage.
function stageDistance(a, b) {
  if (!a || !b) return null;
  return Math.abs(a.gs - b.gs) + 0.25 * Math.min(Math.abs(a.days - b.days), 20) + (a.interval && b.interval && a.interval !== b.interval ? 2 : 0);
}

function getRoom(id, create) {
  let r = rooms.get(id);
  if (!r && create) {
    r = { id, isPublic: false, looking: false, stage: null, stageAt: 0, roster: null, bridges: new Map(), holds: new Map(), createdAt: Date.now(), emptySince: Date.now() };
    rooms.set(id, r);
  }
  return r;
}
function pruneHolds(r, now) {
  for (const [token, exp] of r.holds) if (exp <= now) r.holds.delete(token);
}
// Names of players online in the room: the relay's own roster (it also counts
// desktop players), else the browsers this bridge carries.
function roomPlayers(r) {
  if (r.roster) return r.roster;
  return [...new Set(r.bridges.values())];
}
function roomFree(r, now) {
  pruneHolds(r, now);
  return ROOM_SEATS - roomPlayers(r).length - r.holds.size;
}
function listed(r, now) {
  if (!r.isPublic) return false;
  return roomPlayers(r).length > 0 || r.holds.size > 0 || now - r.createdAt < FRESH_MS;
}
function newCode() {
  for (;;) {
    let c = "mm-";
    for (let i = 0; i < 5; i++) c += CODE_ALPHABET[crypto.randomInt(CODE_ALPHABET.length)];
    if (!rooms.has(c)) return c;
  }
}
function roomView(r, me, now) {
  return {
    room: r.id,
    players: roomPlayers(r).slice(0, ROOM_SEATS),
    seats: ROOM_SEATS,
    free: Math.max(0, roomFree(r, now)),
    looking: r.looking,
    stage: r.stage,
    distance: stageDistance(me, r.stage),
  };
}
setInterval(() => {
  const now = Date.now();
  for (const [id, r] of rooms) {
    pruneHolds(r, now);
    if (roomPlayers(r).length || r.holds.size) r.emptySince = now;
    else if (now - r.emptySince > FORGET_MS) rooms.delete(id);
  }
}, 60 * 1000).unref();

// ---- lobby codes (#4155) ----
// A lobby is a room named lb-CODE. The code is what players share; the room is
// public (listed, quick match can send people to it) or private (code only).
// With LOBBY_URL the zelda-lobby Worker owns the codes and the public list.
const LOBBY_URL = (process.env.LOBBY_URL || "").replace(/\/$/, "");
const LOBBY_SECRET = process.env.LOBBY_SECRET || "";
const lobbyRemote = () => !!(LOBBY_URL && LOBBY_SECRET);
const LOBBY_CODE_RE = /^[ABCDEFGHJKMNPQRSTUVWXYZ23456789]{5}$/;
async function lobbyCall(method, p, body) {
  const r = await fetch(LOBBY_URL + p, {
    method,
    headers: { "x-lobby-secret": LOBBY_SECRET, "content-type": "application/json" },
    body: body ? JSON.stringify(body) : undefined,
    signal: AbortSignal.timeout(5000),
  });
  return { status: r.status, json: await r.json().catch(() => ({ error: "bad reply" })) };
}
// Tell the Worker who is in a lobby: on every join/leave/save, and every 30 s
// while anyone is there (it forgets players it has not heard about for 90 s).
const reportTimers = new Map();
function reportLobby(roomId) {
  if (!lobbyRemote() || !/^lb-/.test(roomId || "") || reportTimers.has(roomId)) return;
  reportTimers.set(
    roomId,
    setTimeout(() => {
      reportTimers.delete(roomId);
      const r = rooms.get(roomId);
      if (!r) return;
      lobbyCall("POST", "/v1/report", { room: roomId, players: roomPlayers(r), stage: r.stage }).catch((e) => log(`[lobby] report ${roomId} failed: ${e.message}`));
    }, 1000)
  );
}
setInterval(() => {
  for (const [id, r] of rooms) if (/^lb-/.test(id) && roomPlayers(r).length) reportLobby(id);
}, 30 * 1000).unref();
function localLobbyView(r, me, now) {
  const v = roomView(r, me, now);
  return { ...v, code: r.id.slice(3), public: r.isPublic };
}

// Bridge hooks.
function bridgeJoined(roomId, bridgeId, name) {
  if (typeof roomId !== "string" || !roomId) return;
  const r = getRoom(roomId.slice(0, 64), true);
  r.bridges.set(bridgeId, String(name || "Player").slice(0, 24));
  r.emptySince = Date.now();
  reportLobby(roomId);
  // The oldest seat held for this room is the one this player was given.
  const first = r.holds.keys().next();
  if (!first.done) r.holds.delete(first.value);
}
function bridgeLeft(roomId, bridgeId) {
  const r = roomId && rooms.get(roomId);
  if (!r) return;
  r.bridges.delete(bridgeId);
  if (!r.bridges.size) r.roster = null; // nobody left here to report the relay's roster
  reportLobby(roomId);
}
function bridgeRoster(roomId, state) {
  const r = roomId && rooms.get(roomId);
  if (!r || !Array.isArray(state)) return;
  r.roster = state.filter((c) => c && c.online !== false).map((c) => String(c.name || "Player").slice(0, 24));
  reportLobby(roomId);
}
function bridgeTeamState(roomId, state) {
  const r = roomId && rooms.get(roomId);
  const st = r && stageFromTeamState(state);
  if (!st) return;
  if (!r.stageAt || st.gs !== r.stage.gs) log(`[lobby] ${r.id}: progress gs ${st.gs} (${st.dungeons} dungeons, ${st.hearts} extra hearts, ${st.raids} raids, day ${st.days + 1})`);
  r.stage = st;
  r.stageAt = Date.now();
  reportLobby(roomId);
}

// API. Small JSON in, small JSON out; same invite key as the page.
const hits = new Map(); // ip -> {n, reset}
setInterval(() => hits.clear(), 10 * 60 * 1000).unref();
function rateLimited(req) {
  const ip = req.headers["cf-connecting-ip"] || req.socket.remoteAddress || "?";
  const now = Date.now();
  let h = hits.get(ip);
  if (!h || h.reset <= now) hits.set(ip, (h = { n: 0, reset: now + 60 * 1000 }));
  return ++h.n > 30;
}
function sendJson(res, code, body, extra) {
  res.writeHead(code, { ...extra, "Content-Type": MIME[".json"], "Cache-Control": "no-store" }).end(JSON.stringify(body));
}
function readJson(req, cb) {
  let size = 0;
  const parts = [];
  req.on("data", (c) => {
    size += c.length;
    if (size > 4096) return req.destroy();
    parts.push(c);
  });
  req.on("end", () => {
    try {
      cb(JSON.parse(Buffer.concat(parts).toString("utf8") || "{}"));
    } catch {
      cb(null);
    }
  });
}
function cleanRoomName(v) {
  return typeof v === "string" ? v.trim().slice(0, 32) : "";
}

// Lobby routes that the Worker answers when it is configured.
function proxyLobby(req, res, pathOnly, extra, body) {
  const q = new URL(req.url, "http://x").searchParams;
  const ip = req.headers["cf-connecting-ip"] || req.socket.remoteAddress || "?";
  let call;
  if (pathOnly === "/api/rooms") call = lobbyCall("GET", "/v1/rooms?" + q.toString());
  else if (pathOnly === "/api/quickplay") call = lobbyCall("POST", "/v1/quick", { stage: body.stage, ip });
  else if (pathOnly === "/api/lobby/create") call = lobbyCall("POST", "/v1/create", { pid: body.pid, name: body.name, public: !!body.public, stage: body.stage, ip });
  else if (pathOnly === "/api/lobby/visibility") call = lobbyCall("POST", "/v1/visibility", { code: String(body.code || ""), pid: body.pid, public: !!body.public });
  else if (pathOnly === "/api/lobby/info") call = lobbyCall("GET", "/v1/room/" + encodeURIComponent(String(q.get("code") || "").toUpperCase()));
  else return false;
  call.then(
    (r) => {
      const j = r.json;
      if (j && j.rooms) j.rooms.forEach((x) => (x.looking = true));
      if (j && j.room && !j.error) j.looking = true;
      sendJson(res, r.status, j, extra);
    },
    (e) => {
      log(`[lobby] worker call failed: ${e.message}`);
      sendJson(res, 502, { error: "lobby service unreachable" }, extra);
    }
  );
  return true;
}

function handleApi(req, res, pathOnly, extra) {
  const now = Date.now();
  if (lobbyRemote() && req.method === "GET" && proxyLobby(req, res, pathOnly, extra, {})) return;
  if (pathOnly === "/api/lobby/info" && req.method === "GET") {
    const code = String(new URL(req.url, "http://x").searchParams.get("code") || "").toUpperCase();
    const r = LOBBY_CODE_RE.test(code) && rooms.get("lb-" + code);
    return r ? sendJson(res, 200, localLobbyView(r, null, now), extra) : sendJson(res, 404, { error: "no such lobby" }, extra);
  }
  if (pathOnly === "/api/rooms" && req.method === "GET") {
    const q = new URL(req.url, "http://x").searchParams;
    const me = q.has("gs") ? cleanStage({ dungeons: q.get("dn"), hearts: q.get("h"), raids: q.get("r"), days: q.get("d"), interval: q.get("i") }) : null;
    const list = [...rooms.values()].filter((r) => listed(r, now)).map((r) => roomView(r, me, now));
    // Rooms looking for players first, then nearest progress, then fullest.
    list.sort((a, b) => b.looking - a.looking || (a.distance ?? 1e9) - (b.distance ?? 1e9) || a.free - b.free);
    return sendJson(res, 200, { seats: ROOM_SEATS, range: MATCH_RANGE, rooms: list.slice(0, 50) }, extra);
  }
  if (req.method !== "POST") return sendJson(res, 405, { error: "method" }, extra);
  if (rateLimited(req)) return sendJson(res, 429, { error: "slow down" }, extra);
  readJson(req, (body) => {
    if (!body || typeof body !== "object") return sendJson(res, 400, { error: "bad json" }, extra);
    const me = cleanStage(body.stage);
    if (lobbyRemote() && proxyLobby(req, res, pathOnly, extra, body)) return;
    if (pathOnly === "/api/lobby/create") {
      if (typeof body.pid !== "string" || body.pid.length < 8 || body.pid.length > 64) return sendJson(res, 400, { error: "bad player id" }, extra);
      let code = newCode().slice(3);
      while (rooms.has("lb-" + code)) code = newCode().slice(3);
      const r = getRoom("lb-" + code, true);
      r.isPublic = r.looking = !!body.public;
      r.host = body.pid;
      r.stage = me;
      return sendJson(res, 200, localLobbyView(r, me, now), extra);
    }
    if (pathOnly === "/api/lobby/visibility") {
      const r = rooms.get("lb-" + String(body.code || "").toUpperCase());
      if (!r) return sendJson(res, 404, { error: "no such lobby" }, extra);
      if (r.host !== body.pid) return sendJson(res, 403, { error: "only the host can change that" }, extra);
      r.isPublic = r.looking = !!body.public;
      r.createdAt = now;
      return sendJson(res, 200, localLobbyView(r, me, now), extra);
    }
    if (pathOnly === "/api/quickplay") {
      // The nearest public room that is looking and has a free seat, if it is
      // within MATCH_RANGE; ties go to the fuller room. Otherwise open a new one
      // that is looking, so the next player near this progress lands with you.
      let best = null;
      let bestD = Infinity;
      for (const r of rooms.values()) {
        if (!r.looking || !listed(r, now) || roomFree(r, now) <= 0) continue;
        const d = stageDistance(me, r.stage) ?? (me && me.gs === 0 && !r.stage ? 0 : null);
        if (d === null || d > MATCH_RANGE) continue;
        if (d < bestD || (d === bestD && roomFree(r, now) < roomFree(best, now))) {
          best = r;
          bestD = d;
        }
      }
      let created = false;
      if (!best) {
        best = getRoom(newCode(), true);
        best.isPublic = best.looking = true;
        best.stage = me; // until the first save from inside it arrives
        created = true;
      }
      best.holds.set(crypto.randomBytes(8).toString("hex"), now + HOLD_MS);
      log(`[lobby] quick match -> ${best.id} (${created ? "new" : "distance " + bestD}, gs ${me ? me.gs : "?"})`);
      return sendJson(res, 200, { room: best.id, created, distance: created ? 0 : bestD, ...roomView(best, me, now) }, extra);
    }
    if (pathOnly === "/api/rooms/publish") {
      // List a room by name (the "list my room" box). Only a room nobody is in
      // yet, or one that is already public, so nobody can expose a private room
      // that others are playing in.
      const id = cleanRoomName(body.room);
      if (!id) return sendJson(res, 400, { error: "room" }, extra);
      const r = getRoom(id, true);
      if (!r.isPublic && roomPlayers(r).length) return sendJson(res, 409, { error: "room in use" }, extra);
      r.isPublic = true;
      r.looking = body.looking !== false;
      r.createdAt = now;
      if (!r.stage && me) r.stage = me;
      return sendJson(res, 200, roomView(r, me, now), extra);
    }
    if (pathOnly === "/api/rooms/unpublish") {
      const r = rooms.get(cleanRoomName(body.room));
      if (r) r.isPublic = r.looking = false;
      return sendJson(res, 200, { ok: true }, extra);
    }
    sendJson(res, 404, { error: "not found" }, extra);
  });
}

// ---- cloud saves (#3856) ----
// GET/PUT /api/save keeps one JSON bundle per signed-in account. SWAG's Authentik
// forward auth sets X-Authentik-Username; X-Soh-Auth must carry SAVE_AUTH_SECRET
// (SWAG adds it) so nothing else on the network can claim an identity.
// Layout: SAVE_DIR/<user>/current.json plus the last SAVE_VERSIONS older copies.
const SAVE_DIR = process.env.SAVE_DIR || "";
const SAVE_AUTH_SECRET = process.env.SAVE_AUTH_SECRET || "";
const SAVE_MAX = parseInt(process.env.SAVE_MAX_BYTES || String(6 * 1024 * 1024), 10);
const SAVE_VERSIONS = 10;

function saveUser(req) {
  if (!SAVE_DIR || !SAVE_AUTH_SECRET) return "";
  const got = Buffer.from(String(req.headers["x-soh-auth"] || ""));
  const want = Buffer.from(SAVE_AUTH_SECRET);
  if (got.length !== want.length || !crypto.timingSafeEqual(got, want)) return "";
  const name = String(req.headers["x-authentik-username"] || "").trim().toLowerCase();
  // A hash keeps the folder name safe whatever characters the username has.
  return name ? crypto.createHash("sha256").update(name).digest("hex").slice(0, 32) : "";
}

function handleSave(req, res) {
  const user = saveUser(req);
  const hdr = { "Cache-Control": "no-store" };
  if (!user) return sendJson(res, 401, { error: "sign in" }, hdr);
  const dir = path.join(SAVE_DIR, user);
  const cur = path.join(dir, "current.json");
  if (req.method === "GET") {
    fs.readFile(cur, (err, data) => {
      if (err) return sendJson(res, 404, { error: "no save" }, hdr);
      res.writeHead(200, { "Content-Type": "application/json", ...hdr }).end(data);
    });
    return;
  }
  if (req.method !== "PUT") return sendJson(res, 405, { error: "method" }, hdr);
  const chunks = [];
  let size = 0;
  let dead = false;
  req.on("data", (c) => {
    size += c.length;
    if (size > SAVE_MAX) {
      dead = true;
      sendJson(res, 413, { error: "too big" }, hdr);
      req.destroy();
    } else chunks.push(c);
  });
  req.on("end", () => {
    if (dead) return;
    let bundle;
    try {
      bundle = JSON.parse(Buffer.concat(chunks).toString("utf8"));
      if (!bundle || typeof bundle !== "object" || typeof bundle.files !== "object") throw 0;
    } catch {
      return sendJson(res, 400, { error: "bad bundle" }, hdr);
    }
    try {
      fs.mkdirSync(path.join(dir, "versions"), { recursive: true });
      if (fs.existsSync(cur)) fs.copyFileSync(cur, path.join(dir, "versions", `${Date.now()}.json`));
      const saved = { ...bundle, savedAt: Date.now() };
      const tmp = cur + ".tmp";
      fs.writeFileSync(tmp, JSON.stringify(saved));
      fs.renameSync(tmp, cur);
      const old = fs.readdirSync(path.join(dir, "versions")).sort().reverse().slice(SAVE_VERSIONS);
      for (const f of old) fs.unlinkSync(path.join(dir, "versions", f));
      sendJson(res, 200, { ok: true, savedAt: saved.savedAt }, hdr);
    } catch (e) {
      log(`[save] write failed: ${e.message}`);
      sendJson(res, 500, { error: "write failed" }, hdr);
    }
  });
}

const server = http.createServer((req, res) => {
  if (req.url === "/healthz") {
    res.writeHead(200, { "Content-Type": "text/plain", "Cache-Control": "no-store" }).end("ok");
    return;
  }
  const apiPath = (req.url || "/").split("?")[0];
  if (apiPath === "/api/save") return handleSave(req, res);
  if (apiPath.startsWith("/api/")) {
    const access = checkAccess(req);
    if (!access.ok) return sendJson(res, 403, { error: "invite only" });
    return handleApi(req, res, apiPath, access.setCookie ? { "Set-Cookie": access.setCookie } : {});
  }
  if (req.method !== "GET" && req.method !== "HEAD") {
    res.writeHead(405).end();
    return;
  }
  const pathOnly = (req.url || "/").split("?")[0];
  if (pathOnly === "/manifest.webmanifest") return serveManifest(req, res);
  if (PUBLIC_PATHS.has(pathOnly)) return serveStatic(req, res, {});
  const access = checkAccess(req);
  if (!access.ok) {
    // The front page answers 200 so link-preview bots (Discord, Slack,
    // iMessage) read its share tags; they skip error pages. Everything else 403s.
    const page = pathOnly === "/" || pathOnly === "/index.html";
    res.writeHead(page ? 200 : 403, { "Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store" }).end(DENIED_PAGE);
    return;
  }
  serveStatic(req, res, access.setCookie ? { "Set-Cookie": access.setCookie } : {});
});
server.keepAliveTimeout = 65 * 1000; // longer than typical proxy idle pools

// ---- /anchor WebSocket <-> Anchor TCP bridge ----
const wss = new WebSocketServer({ noServer: true, maxPayload: MAX_PACKET, perMessageDeflate: false });

// ---- /chat: text chat + voice signalling (#4175) ----
// Browser players only (native clients are not bridged). Text goes through here;
// voice is peer-to-peer WebRTC and this socket only carries the offer/answer/ICE
// packets between two players in the same room. No audio touches the server.
const chatWss = new WebSocketServer({ noServer: true, maxPayload: 16 * 1024, perMessageDeflate: false });
const chatRooms = new Map(); // roomId -> { peers: Map(id -> peer), history: [] }
const CHAT_MAX_PEERS = 12;
const CHAT_HISTORY = 40;
const CHAT_MAX_TEXT = 240;
let nextChatId = 1;

const cleanText = (v, max) => String(v == null ? "" : v).replace(/[\u0000-\u001f\u007f]/g, " ").replace(/\s+/g, " ").trim().slice(0, max);
const chatSend = (peer, obj) => {
  if (peer.ws.readyState === peer.ws.OPEN) peer.ws.send(JSON.stringify(obj));
};

server.on("upgrade", (req, socket, head) => {
  socket.on("error", () => {});
  const path = (req.url || "").split("?")[0];
  if (path !== "/anchor" && path !== "/chat") {
    socket.destroy();
    return;
  }
  if (!checkAccess(req).ok) {
    socket.end("HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n");
    return;
  }
  if (path === "/chat") return chatWss.handleUpgrade(req, socket, head, (ws) => chatWss.emit("connection", ws, req));
  wss.handleUpgrade(req, socket, head, (ws) => wss.emit("connection", ws, req));
});

chatWss.on("connection", (ws) => {
  let me = null; // set by the hello packet
  let room = null;
  let alive = true;
  let stamps = [];
  ws.on("pong", () => (alive = true));
  const pinger = setInterval(() => {
    if (!alive) return ws.terminate();
    alive = false;
    try {
      ws.ping();
    } catch {}
  }, PING_MS);

  const leave = () => {
    clearInterval(pinger);
    if (!me || !room) return;
    room.peers.delete(me.id);
    for (const p of room.peers.values()) chatSend(p, { t: "leave", id: me.id });
    if (!room.peers.size) chatRooms.delete(room.id);
    me = null;
  };

  ws.on("message", (data) => {
    alive = true;
    let msg;
    try {
      msg = JSON.parse(String(data));
    } catch {
      return;
    }
    if (!msg || typeof msg !== "object") return;
    if (!me) {
      if (msg.t !== "hello") return;
      const id = cleanText(msg.room, 64);
      if (!id) return ws.close(1008, "room required");
      room = chatRooms.get(id) || { id, peers: new Map(), history: [] };
      if (room.peers.size >= CHAT_MAX_PEERS) return ws.close(1013, "room full");
      chatRooms.set(id, room);
      me = { id: nextChatId++, name: cleanText(msg.name, 24) || "Player", ws };
      chatSend(me, {
        t: "welcome",
        id: me.id,
        peers: [...room.peers.values()].map((p) => ({ id: p.id, name: p.name })),
        history: room.history,
      });
      for (const p of room.peers.values()) chatSend(p, { t: "join", id: me.id, name: me.name });
      room.peers.set(me.id, me);
      return;
    }
    if (msg.t === "msg") {
      const now = Date.now();
      stamps = stamps.filter((t) => now - t < 4000);
      if (stamps.length >= 5) return chatSend(me, { t: "slow" });
      stamps.push(now);
      const text = cleanText(msg.text, CHAT_MAX_TEXT);
      if (!text) return;
      const out = { t: "msg", id: me.id, from: me.name, text, ts: now };
      room.history.push({ id: me.id, from: me.name, text, ts: now });
      if (room.history.length > CHAT_HISTORY) room.history.shift();
      for (const p of room.peers.values()) chatSend(p, out);
    } else if (msg.t === "talk") {
      for (const p of room.peers.values()) if (p !== me) chatSend(p, { t: "talk", id: me.id, on: !!msg.on });
    } else if (msg.t === "sig") {
      const to = room.peers.get(msg.to);
      if (to && to !== me && msg.data && typeof msg.data === "object") chatSend(to, { t: "sig", from: me.id, data: msg.data });
    }
  });
  ws.on("close", leave);
  ws.on("error", leave);
});

let nextId = 1;
wss.on("connection", (ws, req) => {
  const id = nextId++;
  const who = req.headers["cf-connecting-ip"] || req.headers["x-forwarded-for"] || req.socket.remoteAddress;
  log(`[bridge #${id}] browser connected from ${who} (${wss.clients.size} online)`);

  const tcp = net.connect({ host: ANCHOR_HOST, port: ANCHOR_PORT });
  tcp.setNoDelay(true);
  tcp.setKeepAlive(true, 30 * 1000);
  const pendingToTcp = [];
  let pendingBytes = 0;
  let tcpReady = false;
  let closed = false;
  let chunks = [];
  let chunkBytes = 0;

  const closeBoth = (code, why) => {
    if (closed) return;
    closed = true;
    clearInterval(pinger);
    bridgeLeft(roomId, id);
    if (ws.readyState === ws.OPEN || ws.readyState === ws.CONNECTING) ws.close(code, why.slice(0, 120));
    tcp.destroy();
  };

  // Dead-peer detection: a sleeping laptop or a dropped phone never sends a
  // close frame. Two missed pongs and the slot is freed so the room sees them leave.
  let alive = true;
  ws.on("pong", () => (alive = true));
  const pinger = setInterval(() => {
    if (!alive) {
      log(`[bridge #${id}] no pong, dropping`);
      ws.terminate();
      closeBoth(1001, "timeout");
      return;
    }
    alive = false;
    try {
      ws.ping();
    } catch {}
  }, PING_MS);

  tcp.on("connect", () => {
    tcpReady = true;
    log(`[bridge #${id}] connected to Anchor ${ANCHOR_HOST}:${ANCHOR_PORT}`);
    for (const m of pendingToTcp) tcp.write(m);
    pendingToTcp.length = 0;
    pendingBytes = 0;
  });

  // Anchor -> browser: split on NUL, one WebSocket text message per packet.
  tcp.on("data", (chunk) => {
    let start = 0;
    let idx;
    while ((idx = chunk.indexOf(0, start)) !== -1) {
      const piece = chunk.subarray(start, idx);
      const packet = chunkBytes ? Buffer.concat([...chunks, piece]) : piece;
      chunks = [];
      chunkBytes = 0;
      start = idx + 1;
      if (packet.includes('"ALL_CLIENT_STATE"')) onClientState(packet);
      else if (roomId && packet.includes('"UPDATE_TEAM_STATE"')) watchTeamState(packet);
      if (packet.length && ws.readyState === ws.OPEN) ws.send(packet.toString("utf8"));
    }
    if (start < chunk.length) {
      chunks.push(chunk.subarray(start));
      chunkBytes += chunk.length - start;
      if (chunkBytes > MAX_PACKET) closeBoth(1009, "packet too large");
    }
  });

  // The relay trusts the clientId inside each payload, so a browser could pose as another
  // player (or as the enemy authority). The relay tells each connection its own id in
  // ALL_CLIENT_STATE (the entry flagged "self"); every later packet from this browser
  // is stamped with that id. Only the join packet goes out before the id is known;
  // anything sent after it waits in heldForId until the id arrives.
  let ownId = null;
  let sentJoin = false;
  const heldForId = [];
  let heldBytes = 0;

  const onClientState = (packet) => {
    try {
      const msg = JSON.parse(packet.toString("utf8"));
      if (msg.type !== "ALL_CLIENT_STATE") return;
      bridgeRoster(roomId, msg.state);
      if (ownId !== null) return;
      const me = Array.isArray(msg.state) ? msg.state.find((c) => c && c.self === true) : null;
      if (!me || !Number.isSafeInteger(me.clientId)) return;
      ownId = me.clientId;
      for (const held of heldForId.splice(0)) forward(held);
      heldBytes = 0;
    } catch {}
  };

  // Lobby (#4155): which room this browser is in, and that room's progress.
  let roomId = null;
  const watchTeamState = (packet) => {
    try {
      const msg = typeof packet === "object" && !Buffer.isBuffer(packet) ? packet : JSON.parse(packet.toString("utf8"));
      if (msg.type === "UPDATE_TEAM_STATE") bridgeTeamState(roomId, msg.state);
    } catch {}
  };

  const writeFramed = (body) => {
    const framed = Buffer.concat([body, Buffer.from([0])]);
    if (tcpReady) {
      tcp.write(framed);
    } else {
      pendingBytes += framed.length;
      if (pendingBytes > MAX_PACKET) return closeBoth(1013, "anchor not reachable");
      pendingToTcp.push(framed);
    }
  };

  const forward = (body) => {
    let msg;
    try {
      msg = JSON.parse(body.toString("utf8"));
    } catch {
      return; // not JSON: the relay couldn't use it either
    }
    if (msg === null || typeof msg !== "object" || Array.isArray(msg)) return;
    if (msg.type === "UPDATE_TEAM_STATE") watchTeamState(msg);
    if (msg.clientId === ownId) return writeFramed(body);
    msg.clientId = ownId;
    writeFramed(Buffer.from(JSON.stringify(msg)));
  };

  // Browser -> Anchor: append the NUL terminator Anchor expects.
  ws.on("message", (data, isBinary) => {
    alive = true;
    const body = Buffer.isBuffer(data) ? data : Buffer.from(isBinary ? data : String(data));
    // A NUL inside a packet would split it into garbage on the Anchor side.
    if (body.indexOf(0) !== -1) return;
    if (!sentJoin) {
      sentJoin = true;
      try {
        const hello = JSON.parse(body.toString("utf8"));
        if (hello && hello.type === "HANDSHAKE" && typeof hello.roomId === "string" && hello.roomId) {
          roomId = hello.roomId.slice(0, 64);
          bridgeJoined(roomId, id, hello.clientState && hello.clientState.name);
        }
      } catch {}
      return writeFramed(body);
    }
    if (ownId === null) {
      heldBytes += body.length;
      if (heldBytes > MAX_PACKET) return closeBoth(1013, "no client id from relay");
      heldForId.push(body);
      return;
    }
    forward(body);
  });

  tcp.on("error", (err) => {
    log(`[bridge #${id}] Anchor connection error: ${err.message}`);
    closeBoth(1011, "anchor unreachable");
  });
  tcp.on("close", () => closeBoth(1011, "anchor closed"));
  ws.on("close", () => {
    log(`[bridge #${id}] browser disconnected`);
    closeBoth(1000, "bye");
  });
  ws.on("error", () => closeBoth(1011, "ws error"));
});

// docker stop: tell browsers we're going away (1012 = service restart) so they
// reconnect to the new container instead of waiting for a timeout.
function shutdown(sig) {
  log(`${sig}: closing ${wss.clients.size} player connection(s)`);
  for (const ws of wss.clients) {
    try {
      ws.close(1012, "server restarting");
    } catch {}
  }
  for (const ws of chatWss.clients) {
    try {
      ws.close(1012, "server restarting");
    } catch {}
  }
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 3000).unref();
}
process.on("SIGTERM", () => shutdown("SIGTERM"));
process.on("SIGINT", () => shutdown("SIGINT"));
process.on("uncaughtException", (err) => log("uncaught:", err && err.stack ? err.stack : err));

server.listen(PORT, () => {
  log(
    `serving ${PUBLIC_DIR} on :${PORT}, bridging /anchor -> ${ANCHOR_HOST}:${ANCHOR_PORT}` +
      (ACCESS_KEY ? " (invite key required)" : "")
  );
});
