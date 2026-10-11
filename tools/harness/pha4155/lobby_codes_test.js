// PHA-4155 lobby codes test: server.js + a fake Anchor relay + ws clients. REMOTE=1 also runs the zelda-lobby Worker (npm run dev in web/lobby-worker on :8788, fresh state) behind it.
const net = require("net"), { spawn } = require("child_process"), assert = require("assert");
const WebSocket = require(require("path").join(__dirname, "../../../web/node_modules/ws"));
const AP = 46156, WP = 48156, KEY = "k4155";
// Fake relay: rooms by HANDSHAKE roomId; ALL_CLIENT_STATE to the room on joins/leaves.
let nextId = 1; const conns = new Set();
const anchor = net.createServer((s) => {
  const c = { s, id: nextId++, room: null, name: "" }; conns.add(c);
  let buf = Buffer.alloc(0);
  const roster = (room) => { for (const o of conns) if (o.room === room) o.s.write(JSON.stringify({ type: "ALL_CLIENT_STATE", state: [...conns].filter((x) => x.room === room).map((x) => ({ clientId: x.id, name: x.name, online: true, self: x === o })) }) + "\0"); };
  s.on("data", (d) => { buf = Buffer.concat([buf, d]); let i; while ((i = buf.indexOf(0)) >= 0) { const m = JSON.parse(buf.subarray(0, i)); buf = buf.subarray(i + 1);
    if (m.type === "HANDSHAKE") { c.room = m.roomId; c.name = m.clientState.name; roster(c.room); } } });
  s.on("close", () => { conns.delete(c); c.room && roster(c.room); }); s.on("error", () => {});
}).listen(AP);
const srv = spawn("node", [require("path").join(__dirname, "../../../web/server.js")], { env: { ...process.env, PORT: WP, ANCHOR_HOST: "127.0.0.1", ANCHOR_PORT: AP, ACCESS_KEY: KEY, ...(process.env.REMOTE ? { LOBBY_URL: "http://127.0.0.1:8788", LOBBY_SECRET: "dev-lobby-secret" } : {}) }, stdio: ["ignore", "pipe", "inherit"] });
let out = ""; srv.stdout.on("data", (d) => (out += d));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const H = { Cookie: "soh_key=" + KEY, "Content-Type": "application/json" };
const B = `http://127.0.0.1:${WP}`;
const get = (p, h = H) => fetch(B + p, { headers: h }).then(async (r) => ({ code: r.status, j: await r.json() }));
const post = (p, body) => fetch(B + p, { method: "POST", headers: H, body: JSON.stringify(body) }).then(async (r) => ({ code: r.status, j: await r.json() }));
const stage = (dungeons, hearts, raids, days, interval = 3) => ({ dungeons, hearts, raids, days, interval });
function teamState(dn, hearts, raids, days) {
  const bits = [0x12, 0x13, 0x14, 0, 1, 2, 3, 4].slice(0, dn).reduce((a, b) => a | (1 << b), 0);
  return { type: "UPDATE_TEAM_STATE", targetTeamId: "default", queue: [], state: { healthCapacity: (hearts + 3) * 16, inventory: { questItems: bits >>> 0 }, eventChkInf: [0], sevenDays: { base: { counters: { daysSurvived: days, hordeNightsSurvived: raids, raidInterval: 3 } } } } };
}
function player(room, name) {
  return new Promise((res) => {
    const ws = new WebSocket(`ws://127.0.0.1:${WP}/anchor`, { headers: { Cookie: "soh_key=" + KEY } });
    ws.on("open", () => { ws.send(JSON.stringify({ type: "HANDSHAKE", roomId: room, roomState: {}, clientState: { name } })); setTimeout(() => res(ws), 150); });
  });
}
(async () => {
  await sleep(600);
  const mode = process.env.REMOTE ? "worker" : "local";
  const pid = "pid-host-0001";
  // Private lobby: a code, not listed, quick match never sends anyone there, but the code finds it.
  let r = await post("/api/lobby/create", { pid, name: "Host", public: false, stage: stage(3, 11, 2, 12) });
  assert.equal(r.code, 200); assert.match(r.j.code, /^[A-HJKMNP-Z2-9]{5}$/); assert.equal(r.j.room, "lb-" + r.j.code); assert.equal(r.j.public, false);
  const code = r.j.code, room = r.j.room; console.log(`ok  [${mode}] private lobby`, code);
  r = await get("/api/rooms"); assert.ok(!r.j.rooms.some((x) => x.room === room)); console.log("ok  private lobby not listed");
  r = await get("/api/lobby/info?code=" + code.toLowerCase()); assert.equal(r.code, 200); assert.equal(r.j.room, room); assert.equal(r.j.free, 4); console.log("ok  code lookup, any case");
  r = await get("/api/lobby/info?code=ZZZZZ"); assert.equal(r.code, 404); console.log("ok  unknown code 404");
  r = await post("/api/quickplay", { stage: stage(3, 11, 2, 12) }); assert.notEqual(r.j.room, room); console.log("ok  quick match skips the private lobby");
  // Only the host can flip it. Then it is listed with its progress.
  r = await post("/api/lobby/visibility", { code, pid: "someone-else-1", public: true }); assert.equal(r.code, 403); console.log("ok  visibility is host only");
  r = await post("/api/lobby/visibility", { code, pid, public: true }); assert.equal(r.j.public, true);
  r = await get("/api/rooms?gs=1&dn=3&h=11&r=2&d=12&i=3"); assert.ok(r.j.rooms.some((x) => x.room === room && x.distance === 0)); console.log("ok  public lobby listed");
  // Players inside: roster + progress come from the bridge, through to the directory.
  const h = await player(room, "Host"); h.send(JSON.stringify(teamState(3, 11, 2, 12))); await sleep(1800);
  r = await get("/api/lobby/info?code=" + code); assert.deepEqual(r.j.players, ["Host"]); assert.equal(r.j.free, 3); assert.equal(r.j.stage.gs, 22); console.log("ok  roster + progress from the game:", JSON.stringify(r.j.players), "gs", r.j.stage.gs);
  r = await post("/api/quickplay", { stage: stage(3, 12, 1, 12) }); assert.equal(r.j.room, room); console.log("ok  similar player quick-matched into the public lobby");
  const g = await player(room, "Guest"); await sleep(1800);
  r = await get("/api/lobby/info?code=" + code); assert.deepEqual(r.j.players.sort(), ["Guest", "Host"]); assert.equal(r.j.free, 2); console.log("ok  hold consumed by the arriving player (2 free)");
  r = await post("/api/lobby/visibility", { code, pid, public: false });
  r = await get("/api/rooms"); assert.ok(!r.j.rooms.some((x) => x.room === room)); console.log("ok  private again, players keep playing");
  r = await post("/api/lobby/create", { pid: "x", public: false }); assert.equal(r.code, 400); console.log("ok  bad player id 400");
  g.close(); await sleep(1800);
  r = await get("/api/lobby/info?code=" + code); assert.deepEqual(r.j.players, ["Host"]); console.log("ok  leaving is reported");
  console.log(`ALL PASS (${mode})`); h.close(); srv.kill(); anchor.close(); process.exit(0);
})().catch((e) => { console.error("FAIL", e); console.log(out); srv.kill(); process.exit(1); });
