// The Clubhouse party on the 7 Days to Zelda lobby (PHA-4335; contract: clubhouse-games/docs/CROSSOVER.md).
// party/phatt-party.js (window.PhattParty, deferred) draws the Party pill, drawer and follow banner and moves people
// between the arcade's games. This file is what 7DTZ does with the venue:
//   arrival   the party is here with a room -> join it (once the ROM is in and the engine is ready: the same gate as
//             the Join button); no room and I am the host -> make a lobby (api/lobby/create) and tell the party its
//             room, so the others, who see "<host> is setting up the camp…", join it on the next snapshot.
//   where     room + lobby/playing: 'lb-CODE' once joined, 'playing' once the game started.
//   name      a nameless browser takes the party's name; a named one tells the party.
//   guard     a started game is not left without asking (the arcade selector gets the same guard).
// Globals from index.html's classic script: startGame, formValues, gameStarted, recall, remember, refreshButtons.
// Rooms: 'lb-' + 5 chars (lobbies) or 'mm-' + 5 (quick-match camps); anything else the page drops (index.html's room rule).
// ?camp=ROOM joins that camp (the arcade's universal browser links here), party or not.
(function () {
  "use strict";
  var GAME = "7dtz", ROOM_RE = /^lb-[ABCDEFGHJKMNPQRSTUVWXYZ23456789]{5}$/, ROOM_RE_ANY = /^(lb|mm)-[ABCDEFGHJKMNPQRSTUVWXYZ23456789]{5}$/;
  var $ = function (i) { return document.getElementById(i); };
  var party = null, accepted, joined, whereKey = "", making = false, waiting = null, waitTimer = 0;

  function started() { return typeof gameStarted !== "undefined" && !!gameStarted; }
  function myName() { var n = $("f-name"); return n ? n.value.trim().slice(0, 24) : ""; }
  function roomNow() { var r = started() && window._anchorConfig ? window._anchorConfig.room : ($("f-room") || {}).value; return ROOM_RE_ANY.test(r || "") ? r : null; }
  function note(t, bad) { var n = $("mm-note"); if (!n) return; n.textContent = t || ""; n.className = bad ? "bad" : ""; }
  function hostName() { var h = null; (party.members || []).some(function (m) { if (m.id === party.host) { h = m; return true; } return false; }); return (h && h.name) || "The host"; }
  function pid() {
    var p = typeof recall === "function" ? recall("mm.pid") : null;
    return /^[0-9a-f]{16,64}$/.test(p || "") ? p : null;
  }
  function api(path, body) {
    return fetch(path, body ? { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) } : undefined)
      .then(function (r) { return r.json().then(function (j) { if (!r.ok) throw new Error(j.error || "HTTP " + r.status); return j; }); });
  }

  // report room + phase; a no-op when nothing changed
  function where() {
    if (!party) return;
    var w = { room: roomNow(), status: started() ? "playing" : "lobby" };
    var k = (w.room || "") + ":" + w.status;
    if (k === whereKey) return;
    whereKey = k; party.setWhere(w);
  }

  // the Join button's own gate: ROM in, engine up, a name typed
  function canJoin() { var b = $("btn-join"); return !!b && !b.disabled && !started() && !!myName(); }

  // join a room now if the game is ready, else keep the room in the box and join when it is
  function joinRoom(room) {
    if (started()) return;
    var f = $("f-room"); if (!f) return;
    if (f.value !== room) { f.value = room; f.dispatchEvent(new Event("input")); }
    if (typeof refreshButtons === "function") refreshButtons();
    if (!canJoin()) {
      waiting = room;
      note(myName() ? "The party is at camp " + room.slice(3) + ". Joining once the game has loaded…" : "The party is at camp " + room.slice(3) + ". Type your name to join them.");
      if (!waitTimer) waitTimer = setInterval(function () { if (!waiting) { clearInterval(waitTimer); waitTimer = 0; return; } if (canJoin()) { var r = waiting; waiting = null; joinRoom(r); } }, 500);
      return;
    }
    waiting = null; joined = room;
    note("Joining the party at camp " + room.slice(3) + "…");
    var v = typeof formValues === "function" ? formValues() : null;
    if (v) { v.room = room; startGame(v); } else { var form = $("join"); if (form && form.requestSubmit) form.requestSubmit($("btn-join")); }
  }

  // act on a venue that is here: join its room, or (as host) make a lobby and tell the party
  function act(venue) {
    if (!party) return;
    if (venue.room) {
      if (!ROOM_RE_ANY.test(venue.room)) return;
      if (roomNow() === venue.room || joined === venue.room) return;
      joined = venue.room; joinRoom(venue.room);
      return;
    }
    if (party.isHost) {
      var cur = roomNow();
      if (cur) { whereKey = ""; where(); return; }   // already in a camp: it becomes the party's
      if (making) return;
      making = true;
      note("Making a camp for the party…");
      api("api/lobby/create", { pid: pid(), name: myName() || "Host", public: false, stage: {} }).then(function (j) {
        making = false;
        if (typeof remember === "function") remember("mm.lobby", j.code);
        party.setWhere({ room: j.room, status: "lobby" }); whereKey = j.room + ":lobby";
        joined = j.room; joinRoom(j.room);
      }, function (e) { making = false; note("Could not make a camp for the party: " + e.message, true); });
      return;
    }
    note(hostName() + " is setting up the camp…");
  }
  // a snapshot: the host's room landed on a venue I accepted (arrived with it, or said yes to the banner)
  function watch() {
    if (!party) return;
    var v = party.venue;
    if (!v || v.game !== GAME || v.at !== accepted) return;
    if (v.room && v.room !== joined && v.room !== roomNow()) act(v);
  }

  function init() {
    var guard = function () { return started() ? "You are in a game. Leaving takes you out of the camp." : ""; };
    if (window.PhattArcade) PhattArcade.setGuard(guard);
    // ?camp=lb-CODE|mm-CODE: a Join from the arcade's universal browser (or a party venue URL): join it once the game is ready
    var camp = new URLSearchParams(location.search).get("camp") || "";
    if (ROOM_RE_ANY.test(camp)) { joined = camp; joinRoom(camp); }
    party = window.PhattParty || null;
    if (!party) return;
    party.setGuard(guard);
    party.on("party", watch);
    party.on("follow", function (v) { accepted = v.at; joined = undefined; act(v); });
    var nameBox = $("f-name");
    if (nameBox) nameBox.addEventListener("change", function () { if (party.code && myName()) party.setProfile({ name: myName() }); });
    // the game started (the form submit or a deep link): say so, and drop a pending join
    var tick = setInterval(function () { if (started()) { waiting = null; where(); } }, 1000);
    party.ready.then(function () {
      var carried = String(party.me.name || "").trim().slice(0, 24);
      if (nameBox && !myName() && carried && carried.toLowerCase() !== "guest") { nameBox.value = carried; nameBox.dispatchEvent(new Event("input")); if (typeof remember === "function") remember("name", carried); }
      else if (myName() && party.code && (!party.me.name || party.me.name === "Guest")) party.setProfile({ name: myName() });
      whereKey = ""; where();
      var v = party.venue;
      if (party.code && v && v.game === GAME) { accepted = v.at; act(v); }
    });
    void tick;
  }
  if (document.readyState === "loading") addEventListener("DOMContentLoaded", init); else init();
})();
