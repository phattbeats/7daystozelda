// PHA-4155 lobby test: server.js + a fake Anchor relay + ws clients.
const net = require("net"), { spawn } = require("child_process"), assert = require("assert");
const WebSocket = require(require("path").join(__dirname, "../../../web/node_modules/ws"));
const AP = 46155, WP = 48155, KEY = "k4155";
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
const srv = spawn("node", [require("path").join(__dirname, "../../../web/server.js")], { env: { ...process.env, PORT: WP, ANCHOR_HOST: "127.0.0.1", ANCHOR_PORT: AP, ACCESS_KEY: KEY }, stdio: ["ignore", "pipe", "inherit"] });
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
  let r = await get("/api/rooms", {}); assert.equal(r.code, 403, "api needs the key"); console.log("ok  api 403 without key");
  // A hand-named private room is never listed.
  const priv = await player("boys", "Tyler"); priv.send(JSON.stringify(teamState(4, 6, 3, 20))); await sleep(150);
  r = await get("/api/rooms"); assert.equal(r.j.rooms.length, 0); console.log("ok  private room 'boys' not listed");
  r = await post("/api/rooms/publish", { room: "boys", stage: stage(0, 0, 0, 0) }); assert.equal(r.code, 409); console.log("ok  can't list a private room others are in (409)");
  // Quick match with nothing open: a new looking room with my stage.
  const late = stage(5, 8, 4, 30); // gs 15+8+4 = 27
  r = await post("/api/quickplay", { stage: late }); assert.equal(r.j.created, true); const roomA = r.j.room; console.log("ok  quick match opened", roomA, "gs", r.j.stage.gs);
  const a1 = await player(roomA, "Sam"); await sleep(100);
  // A save from inside updates the room's stage from the real team state.
  a1.send(JSON.stringify(teamState(5, 9, 4, 31))); await sleep(150);
  r = await get("/api/rooms?gs=0&dn=0&h=0&r=0&d=0&i=3"); const ra = r.j.rooms.find((x) => x.room === roomA);
  assert.deepEqual(ra.players, ["Sam"]); assert.equal(ra.stage.gs, 28); assert.equal(ra.free, 3); console.log("ok  roster + stage from team state:", JSON.stringify(ra));
  // A fresh player is far from it: gets their own room.
  r = await post("/api/quickplay", { stage: stage(0, 0, 0, 0) }); assert.equal(r.j.created, true); const roomB = r.j.room; console.log("ok  fresh player not sent to the late room; opened", roomB);
  // A similar player lands in room A, a second fresh player in room B.
  r = await post("/api/quickplay", { stage: stage(5, 7, 3, 25) }); assert.equal(r.j.room, roomA); console.log("ok  similar player matched to", roomA, "distance", r.j.distance);
  r = await post("/api/quickplay", { stage: stage(0, 1, 0, 2) }); assert.equal(r.j.room, roomB); console.log("ok  second fresh player matched to", roomB, "distance", r.j.distance);
  // Holds count as seats; the joining player consumes one.
  r = await get("/api/rooms"); assert.equal(r.j.rooms.find((x) => x.room === roomA).free, 2);
  const a2 = await player(roomA, "Kim"); await sleep(100);
  r = await get("/api/rooms"); const ra2 = r.j.rooms.find((x) => x.room === roomA); assert.equal(ra2.free, 2); assert.deepEqual(ra2.players.sort(), ["Kim", "Sam"]); console.log("ok  hold consumed on join:", ra2.players, "free", ra2.free);
  // Fill room A: the next similar player gets a new room.
  await post("/api/quickplay", { stage: late }); await post("/api/quickplay", { stage: late });
  r = await post("/api/quickplay", { stage: late }); assert.notEqual(r.j.room, roomA); console.log("ok  full room skipped; opened", r.j.room);
  // Listing a named room that is empty works, then shows its players.
  r = await post("/api/rooms/publish", { room: "my room", stage: stage(2, 3, 1, 9) }); assert.equal(r.code, 200);
  const m1 = await player("my room", "Lee"); await sleep(100);
  r = await get("/api/rooms?gs=9&dn=2&h=3&r=1&d=9&i=3"); const mr = r.j.rooms.find((x) => x.room === "my room"); assert.deepEqual(mr.players, ["Lee"]); assert.equal(mr.distance, 0); console.log("ok  listed named room:", JSON.stringify(mr));
  // Leaving empties the roster.
  a1.close(); a2.close(); await sleep(200);
  r = await get("/api/rooms"); const ra3 = r.j.rooms.find((x) => x.room === roomA); console.log("ok  after leaving, room A:", ra3 ? JSON.stringify(ra3.players) + " free " + ra3.free : "unlisted");
  // Bad input.
  r = await fetch(B + "/api/quickplay", { method: "POST", headers: H, body: "nope" }); assert.equal(r.status, 400); console.log("ok  bad json 400");
  // Bridge still relays: priv got its ALL_CLIENT_STATE.
  console.log("server log:\n" + out.split("\n").filter((l) => /lobby|bridge #1 /.test(l)).join("\n"));
  console.log("ALL PASS"); priv.close(); m1.close(); srv.kill(); anchor.close(); process.exit(0);
})().catch((e) => { console.error("FAIL", e); console.log(out); srv.kill(); process.exit(1); });
