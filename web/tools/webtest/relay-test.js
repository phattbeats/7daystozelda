// Relay tests through the SWAG-equivalent nginx (proxy timeouts cut to 10 s).
const WebSocket = require("/root/web-bundle/node_modules/ws");
const VIA = "ws://127.0.0.1:18443/anchor", DIRECT = "ws://127.0.0.1:8080/anchor";
const COOKIE = { headers: { Cookie: "soh_key=testkey" } };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let fails = 0;
const check = (ok, what) => { console.log((ok ? "PASS " : "FAIL ") + what); if (!ok) fails++; };

function open(url, opts) {
  return new Promise((res) => {
    const ws = new WebSocket(url, opts);
    ws.msgs = [];
    ws.on("message", (m) => ws.msgs.push(String(m)));
    ws.on("open", () => res({ ws, ok: true }));
    ws.on("unexpected-response", (req, r) => res({ ws, ok: false, status: r.statusCode }));
    ws.on("error", () => res({ ws, ok: false }));
  });
}
const hs = (name) => JSON.stringify({ type: "HANDSHAKE", roomId: "relaytest", clientId: 0, clientState: { name, teamId: "default" } });

(async () => {
  const denied = await open(VIA, {});
  check(!denied.ok && denied.status === 403, "relay refuses a browser without the invite cookie");

  const a = await open(VIA, COOKIE), b = await open(VIA, COOKIE);
  check(a.ok && b.ok, "two players connect through the proxy with the cookie");
  a.ws.send(hs("A")); b.ws.send(hs("B"));
  await sleep(1000);
  let closedA = false; a.ws.on("close", () => (closedA = true));
  console.log("... idling 25 s (proxy timeout is 10 s, relay ping every 3 s)");
  await sleep(25000);
  check(!closedA && a.ws.readyState === 1, "idle connection survives past the proxy timeout");
  const before = b.ws.msgs.length;
  a.ws.send(JSON.stringify({ type: "RELAY_TEST", clientId: 0, payload: "x".repeat(70000) }));
  await sleep(800);
  const got = b.ws.msgs.slice(before).find((m) => m.includes("RELAY_TEST"));
  check(!!got && JSON.parse(got).payload.length === 70000, "a 70 KB packet arrives intact after idling");

  // NUL inside a message must not reach Anchor (would split into garbage).
  a.ws.send('{"type":"RELAY_TEST","clientId":0,"payload":"a\u0000b"}');
  await sleep(500);
  check(!b.ws.msgs.some((m) => m.includes('"a')), "a packet containing NUL is dropped, not split");

  // Dead peer: stop answering pings; relay should drop it within ~2 intervals.
  const z = await open(DIRECT, { ...COOKIE, autoPong: false });
  z.ws.send(hs("Zombie"));
  let zClosed = 0; const t0 = Date.now();
  z.ws.on("close", () => (zClosed = Date.now() - t0));
  await sleep(9000);
  check(zClosed > 0 && zClosed < 9000, `silent client dropped after ${zClosed} ms`);

  a.ws.close(); b.ws.close();
  await sleep(300);
  console.log(fails ? `${fails} FAILED` : "RELAY: ALL PASS");
  process.exit(fails ? 1 : 0);
})();
