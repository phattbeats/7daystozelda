// 7 Days to Zelda lobby directory (PHA-4155). Lobby codes, public/private, and matchmaking by game progress.
// soh-web (web/server.js) is the only caller and sends x-lobby-secret; players never reach this Worker, so the
// site's invite key keeps gating who can look at lobbies. soh-web also reports who is in each lobby.
//
//   POST /v1/create      {pid, name, public, stage, ip}    -> {code, room}
//   GET  /v1/room/:code                                    -> room view, 404 if no such lobby
//   POST /v1/visibility  {code, pid, public}               host only
//   GET  /v1/rooms?gs=&dn=&h=&r=&d=&i=                     public lobbies, nearest progress first
//   POST /v1/quick       {stage}                           nearest open public lobby, else a new public one
//   POST /v1/report      {room, players, stage}            what soh-web sees in the lobby right now
//   GET  /healthz
import { DurableObject } from "cloudflare:workers";

// 5 characters, no 0/O/1/I/L: 31^5 is about 29M codes
const ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
const CODE_RE = /^[ABCDEFGHJKMNPQRSTUVWXYZ23456789]{5}$/;
const HOLD_MS = 120e3;           // a quick-match seat waits this long for its player to load in
const FRESH_MS = 3 * 60e3;       // a lobby nobody has entered yet stays listed this long
const STALE_MS = 90e3;           // players not re-reported for this long are gone (soh-web re-reports every 30 s)
const FORGET_MS = 6 * 3600e3;    // a lobby nobody has been in for this long is deleted
const CREATES_PER_10MIN = 10;    // per IP

const json = (body, status = 200) =>
  new Response(JSON.stringify(body), { status, headers: { "Content-Type": "application/json", "Cache-Control": "no-store" } });
const nick = (s) => String(s ?? "").replace(/[\u0000-\u001f\u007f]/g, "").trim().slice(0, 24) || "Player";
const int = (v, max) => {
  const n = Math.floor(Number(v));
  return Number.isFinite(n) ? Math.max(0, Math.min(max, n)) : 0;
};
const cleanStage = (s) => {
  if (!s || typeof s !== "object") return null;
  const st = { dungeons: int(s.dungeons, 8), hearts: int(s.hearts, 17), raids: int(s.raids, 9999), days: int(s.days, 99999), interval: int(s.interval, 7) };
  st.gs = 3 * st.dungeons + st.hearts + st.raids;
  return st;
};
// Same measure as web/server.js: gamestage dominates, days and a different raid interval break ties.
const distance = (a, b) =>
  !a || !b ? null : Math.abs(a.gs - b.gs) + 0.25 * Math.min(Math.abs(a.days - b.days), 20) + (a.interval && b.interval && a.interval !== b.interval ? 2 : 0);

const same = (a, b) => {
  if (typeof a !== "string" || typeof b !== "string") return false;
  const x = new TextEncoder().encode(a), y = new TextEncoder().encode(b);
  return x.length === y.length && crypto.subtle.timingSafeEqual(x, y);
};

export default {
  async fetch(req, env) {
    const url = new URL(req.url);
    if (url.pathname === "/healthz") return json({ ok: true });
    if (!env.LOBBY_SECRET || !same(req.headers.get("x-lobby-secret") || "", env.LOBBY_SECRET)) return json({ error: "forbidden" }, 403);
    const dir = env.DIRECTORY.get(env.DIRECTORY.idFromName("directory"));
    let body = {};
    if (req.method === "POST") {
      try { body = await req.json(); } catch { return json({ error: "bad json" }, 400); }
    }
    const p = url.pathname;
    let r;
    if (p === "/v1/create" && req.method === "POST") r = await dir.create(body);
    else if (p === "/v1/visibility" && req.method === "POST") r = await dir.setVisibility(body);
    else if (p === "/v1/quick" && req.method === "POST") r = await dir.quick(body);
    else if (p === "/v1/report" && req.method === "POST") r = await dir.report(body);
    else if (p === "/v1/rooms" && req.method === "GET") {
      const q = url.searchParams;
      const me = q.has("gs") ? cleanStage({ dungeons: q.get("dn"), hearts: q.get("h"), raids: q.get("r"), days: q.get("d"), interval: q.get("i") }) : null;
      r = await dir.list(me);
    } else {
      const m = /^\/v1\/room\/([^/]+)$/.exec(p);
      if (m && req.method === "GET") r = await dir.view(m[1].toUpperCase());
      else return json({ error: "not found" }, 404);
    }
    return json(r, r && r.error ? r.status || 400 : 200);
  },
};

export class Directory extends DurableObject {
  constructor(ctx, env) {
    super(ctx, env);
    this.sql = ctx.storage.sql;
    this.sql.exec(`CREATE TABLE IF NOT EXISTS lobbies(code TEXT PRIMARY KEY, host TEXT, public INTEGER, players TEXT, stage TEXT,
        holds TEXT, created INTEGER, entered INTEGER, reported INTEGER, updated INTEGER);
      CREATE TABLE IF NOT EXISTS creates(ip TEXT, at INTEGER);`);
  }
  get seats() { return Math.max(2, parseInt(this.env.ROOM_SEATS || "4", 10) || 4); }
  get range() { return Math.max(0, parseFloat(this.env.MATCH_RANGE || "6") || 0); }

  // A row as a lobby: stale players dropped, expired seat holds dropped.
  load(row, now) {
    const holds = JSON.parse(row.holds).filter((t) => t > now);
    const players = row.reported && now - row.reported < STALE_MS ? JSON.parse(row.players) : [];
    return { code: row.code, host: row.host, public: !!row.public, players, stage: row.stage ? JSON.parse(row.stage) : null, holds,
      created: row.created, entered: row.entered, updated: row.updated };
  }
  get(code, now = Date.now()) {
    const row = this.sql.exec("SELECT * FROM lobbies WHERE code = ?", code).toArray()[0];
    return row ? this.load(row, now) : null;
  }
  put(l) {
    this.sql.exec("UPDATE lobbies SET host=?, public=?, stage=?, holds=?, entered=?, updated=? WHERE code=?",
      l.host, l.public ? 1 : 0, l.stage ? JSON.stringify(l.stage) : null, JSON.stringify(l.holds), l.entered, Date.now(), l.code);
  }
  free(l) { return this.seats - l.players.length - l.holds.length; }
  listed(l, now) { return l.public && (l.players.length > 0 || l.holds.length > 0 || now - l.created < FRESH_MS); }
  view(l, me, now = Date.now()) {
    if (typeof l === "string") { l = this.get(l, now); if (!l) return { error: "no such lobby", status: 404 }; }
    return { code: l.code, room: `lb-${l.code}`, public: l.public, players: l.players.slice(0, this.seats), seats: this.seats,
      free: Math.max(0, this.free(l)), stage: l.stage, distance: distance(me, l.stage) };
  }

  create({ pid, name, public: pub, stage, ip }) {
    const now = Date.now();
    if (typeof pid !== "string" || pid.length < 8 || pid.length > 64) return { error: "bad player id" };
    this.sql.exec("DELETE FROM creates WHERE at < ?", now - 600e3);
    if (this.sql.exec("SELECT COUNT(*) AS n FROM creates WHERE ip = ?", String(ip || "?")).one().n >= CREATES_PER_10MIN)
      return { error: "Too many lobbies from here. Try again in a few minutes.", status: 429 };
    this.sql.exec("DELETE FROM lobbies WHERE updated < ?", now - FORGET_MS);
    for (let tries = 0; tries < 20; tries++) {
      const code = [...crypto.getRandomValues(new Uint8Array(5))].map((b) => ALPHABET[b % ALPHABET.length]).join("");
      if (this.sql.exec("SELECT 1 FROM lobbies WHERE code = ?", code).toArray().length) continue;
      this.sql.exec("INSERT INTO lobbies (code, host, public, players, stage, holds, created, entered, reported, updated) VALUES (?,?,?,?,?,?,?,?,?,?)",
        code, pid, pub ? 1 : 0, "[]", JSON.stringify(cleanStage(stage)), "[]", now, 0, 0, now);
      this.sql.exec("INSERT INTO creates VALUES (?, ?)", String(ip || "?"), now);
      return { ...this.view(code), host: nick(name) };
    }
    return { error: "Couldn't make a code, try again.", status: 503 };
  }

  setVisibility({ code, pid, public: pub }) {
    const l = this.get(String(code || "").toUpperCase());
    if (!l) return { error: "no such lobby", status: 404 };
    if (l.host !== pid) return { error: "only the host can change that", status: 403 };
    l.public = !!pub;
    l.entered = l.entered || 0;
    this.put(l);
    return this.view(l);
  }

  // soh-web: who is in the lobby right now, and the room's progress (from the game's own save sync).
  report({ room, players, stage }) {
    const m = /^lb-([A-Z0-9]{5})$/.exec(String(room || ""));
    const row = m && this.sql.exec("SELECT * FROM lobbies WHERE code = ?", m[1]).toArray()[0];
    if (!row) return { ok: false };
    const now = Date.now();
    const names = (Array.isArray(players) ? players : []).slice(0, 16).map(nick);
    const st = cleanStage(stage);
    // A player who arrived takes the oldest seat hold with them.
    const old = JSON.parse(row.players).length;
    let holds = JSON.parse(row.holds).filter((t) => t > now).sort();
    holds = holds.slice(Math.max(0, names.length - (row.reported && now - row.reported < STALE_MS ? old : 0)));
    this.sql.exec("UPDATE lobbies SET players=?, stage=?, holds=?, reported=?, entered=?, updated=? WHERE code=?",
      JSON.stringify(names), st ? JSON.stringify(st) : row.stage, JSON.stringify(holds), now, names.length ? now : row.entered, now, m[1]);
    return { ok: true };
  }

  list(me) {
    const now = Date.now();
    const out = this.sql.exec("SELECT * FROM lobbies WHERE public = 1").toArray().map((r) => this.load(r, now)).filter((l) => this.listed(l, now))
      .map((l) => this.view(l, me, now));
    out.sort((a, b) => (a.distance ?? 1e9) - (b.distance ?? 1e9) || a.free - b.free);
    return { seats: this.seats, range: this.range, rooms: out.slice(0, 50) };
  }

  // The nearest public lobby with a free seat within range (ties go to the fuller one); none: a new public lobby.
  quick({ stage, pid, ip }) {
    const now = Date.now();
    const me = cleanStage(stage);
    let best = null, bestD = Infinity;
    for (const row of this.sql.exec("SELECT * FROM lobbies WHERE public = 1").toArray()) {
      const l = this.load(row, now);
      if (!this.listed(l, now) || this.free(l) <= 0) continue;
      const d = distance(me, l.stage) ?? (me && me.gs === 0 && !l.stage ? 0 : null);
      if (d === null || d > this.range) continue;
      if (d < bestD || (d === bestD && this.free(l) < this.free(best))) { best = l; bestD = d; }
    }
    if (!best) {
      const c = this.create({ pid: typeof pid === "string" && pid.length >= 8 ? pid : "quick-" + crypto.randomUUID(), public: true, stage, ip });
      if (c.error) return c;
      best = this.get(c.code);
      best.holds.push(now + HOLD_MS);
      this.put(best);
      return { ...this.view(best, me), created: true, distance: 0 };
    }
    best.holds.push(now + HOLD_MS);
    this.put(best);
    return { ...this.view(best, me), created: false, distance: bestD };
  }
}
