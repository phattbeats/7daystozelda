// #4155: lobby + matchmaking, CS Party style. Quick match puts you in an open
// room whose progress is near yours (joining a room hands you its progress, see
// UpdateTeamState.cpp), the list shows the open public rooms, and the box lists
// your own room so players near your progress can join it. server.js keeps the
// room directory.
!function () {
  var $ = function (i) { return document.getElementById(i); };
  if (!$("mm")) return;
  var DUNGEON_BITS = [0x12, 0x13, 0x14, 0x00, 0x01, 0x02, 0x03, 0x04];
  var FRESH = { slot: "new", stage: { dungeons: 0, hearts: 0, raids: 0, days: 0, interval: 0, gs: 0 } };
  var saves = null, range = 6, busy = false, timer = 0;

  function started() { return typeof gameStarted !== "undefined" && gameStarted; }
  function obj(v) {
    if (typeof v === "string") try { return JSON.parse(v); } catch (e) { return null; }
    return v && typeof v === "object" ? v : null;
  }
  // The same summary server.js reads from a room's UPDATE_TEAM_STATE.
  function stageOf(json) {
    var sec = json && json.sections, b = sec && sec.base && sec.base.data;
    if (!b) return null;
    var q = (b.inventory && b.inventory.questItems) >>> 0, dn = 0;
    DUNGEON_BITS.forEach(function (bit) { if (q & (1 << bit)) dn++; });
    if (!(q & (1 << 0x12)) && b.eventChkInf && b.eventChkInf[0] & 0x80) dn++;
    var sd = sec.sevenDays && sec.sevenDays.data, base = obj(sd && sd.base) || {}, c = base.counters || {};
    var st = {
      dungeons: dn,
      hearts: Math.max(0, Math.floor((b.healthCapacity || 48) / 16) - 3),
      raids: c.hordeNightsSurvived >>> 0,
      days: c.daysSurvived >>> 0,
      interval: c.raidInterval >>> 0,
    };
    st.gs = 3 * st.dungeons + st.hearts + st.raids;
    return st;
  }
  function readSaves() {
    if (!window._sohIdbReady || typeof FS === "undefined") return false;
    var list = [];
    try {
      FS.readdir("/Save").forEach(function (f) {
        var m = /^file(\d+)\.sav$/.exec(f);
        if (!m) return;
        try {
          var st = stageOf(JSON.parse(new TextDecoder().decode(FS.readFile("/Save/" + f))));
          st && list.push({ slot: m[1], stage: st, t: +FS.stat("/Save/" + f).mtime || 0 });
        } catch (e) {}
      });
    } catch (e) { return false; }
    list.sort(function (a, b) { return b.t - a.t; });
    saves = list;
    return true;
  }
  function current() {
    if (!saves || !saves.length) return FRESH;
    var v = $("mm-file").value;
    if (v === "new") return FRESH;
    for (var i = 0; i < saves.length; i++) if (saves[i].slot === v) return saves[i];
    return saves[0];
  }
  function plural(n, w) { return n + " " + w + (n === 1 ? "" : "s"); }
  function describe(st) {
    if (!st) return "progress not saved yet";
    if (!st.gs && !st.days) return "just starting";
    return [plural(st.dungeons, "dungeon"), st.hearts + 3 + "♥", plural(st.raids, "raid") + " survived", "day " + (st.days + 1)].join(" · ");
  }
  function showMine() {
    var sel = $("mm-file");
    if (saves && saves.length) {
      if (!sel.options.length) {
        var want = recall("mm.file");
        saves.forEach(function (s) { sel.add(new Option("File " + s.slot, s.slot)); });
        sel.add(new Option("New game", "new"));
        sel.value = want && [].some.call(sel.options, function (o) { return o.value === want; }) ? want : saves[0].slot;
      }
      sel.hidden = saves.length < 2 && sel.value !== "new";
    }
    if (saves === null) return;
    var cur = current();
    $("mm-stage").textContent = cur === FRESH ? (saves && saves.length ? "a new game" : "no save yet, so new games") : describe(cur.stage);
  }
  function note(t, bad) { var n = $("mm-note"); n.textContent = t || ""; n.className = bad ? "bad" : ""; }
  function api(path, body) {
    return fetch(path, body ? { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) } : { cache: "no-store" })
      .then(function (r) { return r.json().then(function (j) { if (!r.ok) throw new Error(j.error || "HTTP " + r.status); return j; }); });
  }
  function closeness(d) {
    if (d === null || d === undefined) return null;
    if (d <= 2) return ["Close to you", ""];
    if (d <= range) return ["A bit off", "mid"];
    return ["Far from you", "far"];
  }
  function canPlay() { return !$("btn-solo").disabled && !started(); }
  function refreshQuick() {
    $("btn-quick").disabled = busy || !canPlay();
    $("mm-rooms").querySelectorAll("button").forEach(function (b) { b.disabled = !canPlay(); });
  }
  function join(room) {
    if (!$("f-name").value.trim()) {
      note("Type your name first.", true);
      $("f-name").focus();
      return;
    }
    $("f-room").value = room;
    $("f-room").dispatchEvent(new Event("input"));
    refreshButtons();
    if ($("btn-join").disabled) return note("The game is still loading. Try again in a moment.", true);
    $("join").requestSubmit ? $("join").requestSubmit($("btn-join")) : $("btn-join").click();
  }
  function render(list) {
    var ul = $("mm-rooms");
    ul.textContent = "";
    if (!list.length) {
      var li = document.createElement("li");
      li.className = "muted small";
      li.textContent = "No open rooms right now. Quick match opens one for you.";
      ul.appendChild(li);
      return;
    }
    list.forEach(function (r) {
      var li = document.createElement("li"), who = document.createElement("span"), st = document.createElement("span"), b = document.createElement("button");
      li.className = "room";
      who.className = "who";
      who.textContent = (r.players.length ? r.players.join(", ") : r.room) + " · " + (r.seats - r.free) + "/" + r.seats;
      var c = closeness(r.distance);
      if (c) {
        var tag = document.createElement("span");
        tag.className = "tag " + c[1];
        tag.textContent = c[0];
        who.appendChild(tag);
      }
      st.className = "st";
      st.textContent = describe(r.stage) + (r.looking ? "" : " · not looking");
      b.type = "button";
      b.className = "btn";
      b.textContent = r.free > 0 ? "Join" : "Full";
      b.disabled = r.free <= 0 || !canPlay();
      b.addEventListener("click", function () { join(r.room); });
      li.append(who, b, st);
      ul.appendChild(li);
    });
  }
  function load() {
    if (started()) return clearInterval(timer);
    var s = current().stage;
    var q = "?gs=" + s.gs + "&dn=" + s.dungeons + "&h=" + s.hearts + "&r=" + s.raids + "&d=" + s.days + "&i=" + s.interval;
    api("api/rooms" + q).then(function (j) {
      range = j.range;
      render(j.rooms.filter(function (r) { return r.free > 0; }).concat(j.rooms.filter(function (r) { return r.free <= 0; })));
    }, function () {
      $("mm-rooms").innerHTML = '<li class="muted small">Could not load the room list.</li>';
    });
  }

  $("btn-quick").addEventListener("click", function () {
    if (!$("f-name").value.trim()) {
      note("Type your name first.", true);
      return $("f-name").focus();
    }
    busy = true;
    refreshQuick();
    note("Finding a game near your progress…");
    api("api/quickplay", { stage: current().stage }).then(function (j) {
      busy = false;
      note(j.created
        ? "No close game was open, so you're opening room " + j.room + ". Players near your progress get sent to you."
        : "Matched: " + (j.players.join(", ") || "room " + j.room) + " (" + describe(j.stage) + ").");
      join(j.room);
    }, function (e) {
      busy = false;
      refreshQuick();
      note("Quick match failed: " + e.message, true);
    });
  });
  $("mm-refresh").addEventListener("click", load);
  $("mm-file").addEventListener("change", function () {
    remember("mm.file", $("mm-file").value);
    showMine();
    load();
  });
  $("f-public").checked = recall("mm.public") === "1";
  $("f-public").addEventListener("change", function () { remember("mm.public", $("f-public").checked ? "1" : "0"); });
  // Listing your own room: done as the form submits, before the game connects.
  $("join").addEventListener("submit", function () {
    var room = $("f-room").value.trim();
    if (!room || !$("f-public").checked || !$("f-name").value.trim()) return;
    api("api/rooms/publish", { room: room, stage: current().stage }).catch(function (e) { console.warn("[lobby] could not list the room:", e.message); });
  });
  ["f-name", "f-room"].forEach(function (i) { $(i).addEventListener("input", refreshQuick); });
  new MutationObserver(refreshQuick).observe($("btn-solo"), { attributes: true, attributeFilter: ["disabled"] });

  // Saves come out of IndexedDB a moment after the engine starts.
  var tries = 0, poll = setInterval(function () {
    if (readSaves() || ++tries > 120) {
      clearInterval(poll);
      showMine();
      load();
    }
  }, 500);
  showMine();
  load();
  timer = setInterval(load, 10000);
  refreshQuick();
}();
