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
  ".ico": "image/x-icon",
  ".svg": "image/svg+xml",
  ".webmanifest": "application/manifest+json",
};

// Fetched by the OS/browser outside the page (home-screen install, tab icon),
// often without cookies, and nothing secret: served without the invite key.
const PUBLIC_PATHS = new Set(["/manifest.webmanifest", "/icon.svg", "/icon-180.png", "/icon-192.png", "/icon-512.png", "/favicon.ico"]);

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

const DENIED_PAGE = `<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Invite only</title><body style="background:#0b0d12;color:#d8dbe2;font:16px system-ui,sans-serif;display:grid;place-items:center;height:100vh;margin:0">
<div style="max-width:28rem;padding:1rem;text-align:center"><h1 style="font-size:1.3rem">Invite only</h1>
<p>This server needs the invite link. Ask whoever runs it for the full link (it has <code>?key=</code> in it).</p></div>`;

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
    name: "Ocarina Co-op",
    short_name: "Ocarina",
    description: "Ship of Harkinian co-op with shared enemies and horde night.",
    start_url: keyed ? `/?key=${encodeURIComponent(ACCESS_KEY)}` : "/",
    scope: "/",
    display: "fullscreen",
    display_override: ["fullscreen", "standalone"],
    orientation: "landscape",
    background_color: "#0b0f14",
    theme_color: "#0b0f14",
    icons: [
      { src: "/icon-192.png", sizes: "192x192", type: "image/png", purpose: "any maskable" },
      { src: "/icon-512.png", sizes: "512x512", type: "image/png", purpose: "any maskable" },
      { src: "/icon.svg", sizes: "any", type: "image/svg+xml" },
    ],
  };
  res.writeHead(200, { "Content-Type": MIME[".webmanifest"], "Cache-Control": "no-cache", Vary: "Cookie" });
  res.end(req.method === "HEAD" ? undefined : JSON.stringify(manifest));
}

const server = http.createServer((req, res) => {
  if (req.url === "/healthz") {
    res.writeHead(200, { "Content-Type": "text/plain", "Cache-Control": "no-store" }).end("ok");
    return;
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
    res.writeHead(403, { "Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store" }).end(DENIED_PAGE);
    return;
  }
  serveStatic(req, res, access.setCookie ? { "Set-Cookie": access.setCookie } : {});
});
server.keepAliveTimeout = 65 * 1000; // longer than typical proxy idle pools

// ---- /anchor WebSocket <-> Anchor TCP bridge ----
const wss = new WebSocketServer({ noServer: true, maxPayload: MAX_PACKET, perMessageDeflate: false });

server.on("upgrade", (req, socket, head) => {
  socket.on("error", () => {});
  if ((req.url || "").split("?")[0] !== "/anchor") {
    socket.destroy();
    return;
  }
  if (!checkAccess(req).ok) {
    socket.end("HTTP/1.1 403 Forbidden\r\nConnection: close\r\n\r\n");
    return;
  }
  wss.handleUpgrade(req, socket, head, (ws) => wss.emit("connection", ws, req));
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
      if (packet.length && ws.readyState === ws.OPEN) ws.send(packet.toString("utf8"));
    }
    if (start < chunk.length) {
      chunks.push(chunk.subarray(start));
      chunkBytes += chunk.length - start;
      if (chunkBytes > MAX_PACKET) closeBoth(1009, "packet too large");
    }
  });

  // Browser -> Anchor: append the NUL terminator Anchor expects.
  ws.on("message", (data, isBinary) => {
    alive = true;
    const body = Buffer.isBuffer(data) ? data : Buffer.from(isBinary ? data : String(data));
    // A NUL inside a packet would split it into garbage on the Anchor side.
    if (body.indexOf(0) !== -1) return;
    const framed = Buffer.concat([body, Buffer.from([0])]);
    if (tcpReady) {
      tcp.write(framed);
    } else {
      pendingBytes += framed.length;
      if (pendingBytes > MAX_PACKET) return closeBoth(1013, "anchor not reachable");
      pendingToTcp.push(framed);
    }
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
