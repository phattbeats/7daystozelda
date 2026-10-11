// Run with `npm run dev` going on :8788 (LOBBY_SECRET from .dev.vars). node test/lobby_test.mjs [base]
const base = process.argv[2] || "http://127.0.0.1:8788";
const secret = process.env.LOBBY_SECRET || "dev-lobby-secret";
let fails = 0;
const ok = (c, m) => { console.log((c ? "ok   " : "FAIL ") + m); if (!c) fails++; };
const call = async (path, body, key = secret) => {
  const r = await fetch(base + path, { method: body ? "POST" : "GET", headers: { "x-lobby-secret": key, "content-type": "application/json" }, body: body ? JSON.stringify(body) : undefined });
  return { s: r.status, j: await r.json().catch(() => null) };
};
const stage = (d, h, r, days = 0) => ({ dungeons: d, hearts: h, raids: r, days, interval: 3 });
const ip = "t" + Math.random().toString(36).slice(2, 8);

ok((await fetch(base + "/healthz")).status === 200, "healthz is open");
ok((await call("/v1/rooms", null, "wrong")).s === 403, "wrong secret is refused");
ok((await call("/v1/rooms", null, "")).s === 403, "no secret is refused");

const a = await call("/v1/create", { pid: "pid-host-0001", name: "A", public: false, stage: stage(3, 11, 2, 12), ip });
ok(a.s === 200 && /^[A-HJKMNP-Z2-9]{5}$/.test(a.j.code) && a.j.room === "lb-" + a.j.code && !a.j.public, "private lobby gets a 5-char code");
ok((await call("/v1/rooms")).j.rooms.every((r) => r.code !== a.j.code), "private lobby is not in the public list");
const look = await call("/v1/room/" + a.j.code.toLowerCase());
ok(look.s === 200 && look.j.free === 4 && look.j.stage.gs === 22, "code lookup works, any case, with the creator's progress");
ok((await call("/v1/room/ZZZZZ")).s === 404, "unknown code is 404");
ok((await call("/v1/visibility", { code: a.j.code, pid: "someone-else-1", public: true })).s === 403, "only the host can change visibility");
ok((await call("/v1/visibility", { code: a.j.code, pid: "pid-host-0001", public: true })).j.public === true, "host makes it public");
ok((await call("/v1/rooms?gs=1&dn=3&h=11&r=2&d=12&i=3")).j.rooms.some((r) => r.code === a.j.code && r.distance === 0), "public lobby listed with distance");
ok((await call("/v1/visibility", { code: a.j.code, pid: "pid-host-0001", public: false })).j.public === false, "host makes it private again");
ok((await call("/v1/rooms")).j.rooms.every((r) => r.code !== a.j.code), "private again: gone from the list");
const q0 = await call("/v1/quick", { stage: stage(3, 11, 2, 12), ip });
ok(q0.j.created && q0.j.code !== a.j.code, "quick match never lands in a private lobby");

// report players in and out
await call("/v1/visibility", { code: a.j.code, pid: "pid-host-0001", public: true });
ok((await call("/v1/report", { room: "lb-" + a.j.code, players: ["A"], stage: stage(3, 11, 2, 12) })).j.ok, "soh-web reports the roster");
const q1 = await call("/v1/quick", { stage: stage(3, 12, 1, 12), ip });
ok(!q1.j.created && q1.j.code === a.j.code && q1.j.distance <= 2.5, "similar progress quick-matches into it");
const q2 = await call("/v1/quick", { stage: stage(0, 0, 0), ip });
ok(q2.j.created && q2.j.code !== a.j.code, "a fresh save is kept apart (opens its own lobby)");
// hold: A + q1 hold + three more fills seats
for (let i = 0; i < 2; i++) await call("/v1/quick", { stage: stage(3, 11, 2, 12), ip });
const full = await call("/v1/room/" + a.j.code);
ok(full.j.free === 1 || full.j.free === 0, `seat holds count (${full.j.free} free)`);
await call("/v1/report", { room: "lb-" + a.j.code, players: ["A", "B", "C", "D"], stage: stage(3, 11, 2, 12) });
const f2 = await call("/v1/room/" + a.j.code);
ok(f2.j.free === 0 && f2.j.players.length === 4, "four players = full");
const q3 = await call("/v1/quick", { stage: stage(3, 11, 2, 12), ip });
ok(q3.s === 200 && q3.j.code !== a.j.code, "full lobby is skipped");
ok((await call("/v1/report", { room: "lb-NOPE0", players: [] })).j.ok === false, "report for an unknown lobby is ignored");
ok((await call("/v1/report", { room: "hand-named", players: ["x"] })).j.ok === false, "report for a hand-named room is ignored");
let limited = 0;
for (let i = 0; i < 12; i++) if ((await call("/v1/create", { pid: "pid-spam-00" + i, public: false, ip: "spam-" + ip })).s === 429) limited++;
ok(limited >= 1, "create is rate limited per IP");
console.log(fails ? `${fails} FAILED` : "all passed");
process.exit(fails ? 1 : 0);
