// Optional HD textures (PHA-4156). The player downloads GhostlyDark's OoT Reloaded
// Ship of Harkinian pack from its own GitHub release and picks the .o2r here. We read
// the zip index straight from the File, keep only the entries that fit in browser
// memory, and store the result in this browser. Nothing is uploaded or hosted by us.
(function () {
  "use strict";
  var KEY = "hd.o2r";
  var PACK_URL = "https://github.com/GhostlyDark/OoT-Reloaded/releases/latest";
  var MAX_BYTES = 1300 * 1024 * 1024;
  // Objects hold Link, enemies and set pieces. object_mag is the title logo: the engine
  // loads it as a strip, so HD textures garble it. Skyboxes and scene textures are the
  // bulk of the pack and are skipped.
  var KEEP = [/^alt\/objects\/(?!object_mag\/)/, /^alt\/textures\/icon_item_(static|field_static)\//];

  function keep(name) {
    return KEEP.some(function (re) { return re.test(name); });
  }

  function read(file, start, len) {
    return file.slice(start, start + len).arrayBuffer().then(function (b) { return new DataView(b); });
  }

  // Zip central directory reader with zip64 support (the full pack is over 4 GB).
  function listEntries(file) {
    var tail = Math.min(file.size, 65557 + 20);
    return read(file, file.size - tail, tail).then(function (t) {
      var p = -1;
      for (var i = t.byteLength - 22; i >= 0; i--) {
        if (t.getUint32(i, true) === 0x06054b50) { p = i; break; }
      }
      if (p < 0) throw new Error("This doesn't look like an .o2r (no zip index).");
      var count = t.getUint16(p + 10, true), size = t.getUint32(p + 12, true), off = t.getUint32(p + 16, true);
      if (p >= 20 && t.getUint32(p - 20, true) === 0x07064b50) {
        var e64 = Number(t.getBigUint64(p - 20 + 8, true));
        return read(file, e64, 56).then(function (z) {
          if (z.getUint32(0, true) !== 0x06064b50) throw new Error("Bad zip64 index.");
          return [Number(z.getBigUint64(32, true)), Number(z.getBigUint64(40, true)), Number(z.getBigUint64(48, true))];
        });
      }
      return [count, size, off];
    }).then(function (cd) {
      return read(file, cd[2], cd[1]).then(function (v) { return parseCentral(v, cd[0]); });
    });
  }

  function parseCentral(v, count) {
    var dec = new TextDecoder(), out = [], p = 0;
    while (out.length < count && p + 46 <= v.byteLength && v.getUint32(p, true) === 0x02014b50) {
      var nl = v.getUint16(p + 28, true), xl = v.getUint16(p + 30, true), cl = v.getUint16(p + 32, true);
      var e = {
        flags: v.getUint16(p + 8, true), method: v.getUint16(p + 10, true),
        time: v.getUint16(p + 12, true), date: v.getUint16(p + 14, true), crc: v.getUint32(p + 16, true),
        csize: v.getUint32(p + 20, true), usize: v.getUint32(p + 24, true), lho: v.getUint32(p + 42, true),
        name: dec.decode(new Uint8Array(v.buffer, v.byteOffset + p + 46, nl))
      };
      var x = p + 46 + nl, xe = x + xl;
      while (x + 4 <= xe) {
        var id = v.getUint16(x, true), sz = v.getUint16(x + 2, true), y = x + 4;
        if (id === 1) {
          if (e.usize === 0xFFFFFFFF) { e.usize = Number(v.getBigUint64(y, true)); y += 8; }
          if (e.csize === 0xFFFFFFFF) { e.csize = Number(v.getBigUint64(y, true)); y += 8; }
          if (e.lho === 0xFFFFFFFF) { e.lho = Number(v.getBigUint64(y, true)); }
        }
        x += 4 + sz;
      }
      out.push(e);
      p += 46 + nl + xl + cl;
    }
    return out;
  }

  // Resolve where each kept entry's data starts (the local header has its own name/extra length).
  function locate(file, entries) {
    var i = 0;
    function batch() {
      if (i >= entries.length) return Promise.resolve();
      var chunk = entries.slice(i, i += 256);
      return Promise.all(chunk.map(function (e) {
        return read(file, e.lho, 30).then(function (h) {
          if (h.getUint32(0, true) !== 0x04034b50) throw new Error("Bad zip entry for " + e.name);
          e.data = e.lho + 30 + h.getUint16(26, true) + h.getUint16(28, true);
        });
      })).then(batch);
    }
    return batch();
  }

  function buildZip(file, entries) {
    var enc = new TextEncoder(), parts = [], cds = [], off = 0;
    entries.forEach(function (e) {
      var n = enc.encode(e.name), h = new DataView(new ArrayBuffer(30));
      h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint16(6, 0x0800, true);
      h.setUint16(8, e.method, true); h.setUint16(10, e.time, true); h.setUint16(12, e.date, true);
      h.setUint32(14, e.crc, true); h.setUint32(18, e.csize, true); h.setUint32(22, e.usize, true);
      h.setUint16(26, n.length, true); h.setUint16(28, 0, true);
      parts.push(h.buffer, n, file.slice(e.data, e.data + e.csize));
      var c = new DataView(new ArrayBuffer(46));
      c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true);
      c.setUint16(8, 0x0800, true); c.setUint16(10, e.method, true); c.setUint16(12, e.time, true);
      c.setUint16(14, e.date, true); c.setUint32(16, e.crc, true); c.setUint32(20, e.csize, true);
      c.setUint32(24, e.usize, true); c.setUint16(28, n.length, true); c.setUint32(42, off, true);
      cds.push(c.buffer, n);
      off += 30 + n.length + e.csize;
    });
    var cdSize = 0;
    cds.forEach(function (b) { cdSize += b.byteLength; });
    var end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true); end.setUint16(8, entries.length, true); end.setUint16(10, entries.length, true);
    end.setUint32(12, cdSize, true); end.setUint32(16, off, true);
    return new Blob(parts.concat(cds, [end.buffer]), { type: "application/octet-stream" });
  }

  function prepare(file) {
    return listEntries(file).then(function (all) {
      var sel = all.filter(function (e) { return keep(e.name); });
      if (!sel.length) throw new Error("No HD textures found in that file. Pick the Ship of Harkinian .o2r from the OoT Reloaded download.");
      var total = 0;
      sel.forEach(function (e) { total += e.csize; });
      if (total > MAX_BYTES || total > 0xF0000000) throw new Error("That pack is too big to load in a browser.");
      return locate(file, sel).then(function () { return { blob: buildZip(file, sel), count: sel.length }; });
    });
  }

  // Runs just before the game boots: the engine scans mods/ once at start-up.
  function install() {
    return getCachedFile(KEY).then(function (blob) {
      if (!blob) return;
      return new Response(blob).arrayBuffer().then(function (buf) {
        try { FS.mkdir("/mods"); } catch (e) {}
        FS.writeFile("/mods/" + KEY, new Uint8Array(buf));
      });
    }).catch(function (e) { console.warn("[HD] could not install the HD pack", e); });
  }

  function fmt(n) { return n >= 1e9 ? (n / 1e9).toFixed(1) + " GB" : Math.round(n / 1e6) + " MB"; }

  function ui() {
    if (typeof isTouch !== "undefined" && isTouch) return;
    var join = document.getElementById("join");
    if (!join) return;
    var sec = document.createElement("section");
    sec.className = "panel";
    sec.innerHTML = '<h2>Optional · HD textures</h2>' +
      '<p class="muted small" style="margin:.2rem 0 .6rem">Sharper Link, enemies and items from a fan-made pack. For desktop browsers with plenty of memory. ' +
      'Download the <b>soh-o2r-hd</b> file from <a id="hd-get" target="_blank" rel="noopener">the pack author\'s GitHub</a>, unzip it, then pick the .o2r here. It stays in this browser; we host nothing.</p>' +
      '<div class="ready-row"><button type="button" class="linkish" id="hd-pick">Choose HD pack…</button>' +
      '<span class="muted small" id="hd-msg" aria-live="polite"></span>' +
      '<button type="button" class="linkish" id="hd-off" hidden>Remove</button></div>' +
      '<input type="file" id="hd-file" accept=".o2r" style="display:none">';
    join.parentNode.insertBefore(sec, join);
    var get = sec.querySelector("#hd-get"), pick = sec.querySelector("#hd-pick"), msg = sec.querySelector("#hd-msg"),
      off = sec.querySelector("#hd-off"), inp = sec.querySelector("#hd-file");
    get.href = PACK_URL;
    function show(text, on) { msg.textContent = text; off.hidden = !on; }
    getCachedFile(KEY).then(function (b) { if (b) show("HD pack ready (" + fmt(b.size) + ").", true); });
    pick.onclick = function () { inp.click(); };
    off.onclick = function () { deleteCachedFile(KEY).then(function () { show("HD pack removed.", false); }); };
    inp.onchange = function () {
      var f = inp.files && inp.files[0];
      if (!f) return;
      show("Reading " + f.name + "…", false);
      prepare(f).then(function (r) {
        return cacheFile(KEY, r.blob).then(function () {
          show("HD pack ready: " + r.count + " files, " + fmt(r.blob.size) + ".", true);
        });
      }).catch(function (e) { show(e.message || String(e), false); });
      inp.value = "";
    };
  }

  var orig = window.startGame, started = false;
  if (typeof orig === "function") {
    window.startGame = function (a) {
      if (started) return;
      started = true;
      install().then(function () { orig(a); });
    };
  }
  ui();
})();
