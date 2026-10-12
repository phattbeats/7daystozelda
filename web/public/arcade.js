// PHATT GAMING ARCADE selector (PHA-4215 / PHA-4216 / PHA-4217).
// One self-contained file, byte-identical in csparty, 7daystozelda and pokernight.
// Optional: data-current="<game id>" on the script tag overrides hostname detection.
// Edit GAMES here, then copy the file to the other two repos. Assets are read from an
// "arcade/" folder next to this script: phatt-gaming-arcade.webp plus one logo per game.
(function () {
  "use strict";
  if (window.__phattArcade) return;
  window.__phattArcade = true;

  var GAMES = [
    { id: "csparty", name: "CS Party", url: "https://csparty.phatt.vip/", logo: "csparty.webp", w: 640, h: 320,
      hosts: ["csparty.phatt.vip"], blurb: "Mario Party-style board game inside Counter-Strike 1.6. Roll, scheme and sabotage your friends." },
    { id: "7dtz", name: "7 Days to Zelda", url: "https://zelda.phatt.vip/", logo: "7-days-to-zelda.webp", w: 640, h: 265,
      hosts: ["zelda.phatt.vip"], blurb: "Co-op survival Ocarina of Time. Gather, craft, build walls and hold Hyrule for seven nights." },
    { id: "poker", name: "Poker Night Multiplayer", url: "https://poker.phatt.vip/", logo: "poker-night-multiplayer.webp", w: 640, h: 427,
      hosts: ["poker.phatt.vip", "pokernight.brandon-444.workers.dev"], blurb: "Pull up a chair at the Inventory table and play poker online with your friends." }
  ];

  var script = document.currentScript;
  var base = script && script.src ? script.src.replace(/[^\/?#]*([?#].*)?$/, "") : "/";
  var asset = function (f) { return base + "arcade/" + f; };
  var host = location.hostname;
  var current = (script && script.getAttribute("data-current")) || null;
  if (!current) GAMES.forEach(function (g) { if (g.hosts.indexOf(host) >= 0) current = g.id; });
  var reduce = window.matchMedia && matchMedia("(prefers-reduced-motion: reduce)").matches;

  var CSS = [
    ".pga-btn,.pga-modal,.pga-modal *{box-sizing:border-box}",
    ".pga-bar{display:flex;justify-content:center;padding:calc(env(safe-area-inset-top,0px) + 6px) 8px 6px;position:relative;z-index:50}",
    ".pga-bar.pga-float{position:fixed;top:0;left:0;right:0;pointer-events:none}",
    "body.playing .pga-bar{display:none}",
    ".pga-btn{pointer-events:auto;appearance:none;display:inline-flex;align-items:center;justify-content:center;min-height:44px;padding:4px 14px;border-radius:10px;cursor:pointer;color:#f6e6c4;font:inherit;",
    "background:linear-gradient(#1c1612,#0e0b09);border:2px solid #4a3a2a;box-shadow:0 0 0 1px #000,inset 0 1px 0 rgba(255,220,160,.18),0 3px 0 #000;-webkit-tap-highlight-color:transparent;touch-action:manipulation}",
    ".pga-btn img{display:block;height:36px;width:auto}",
    ".pga-btn:hover{border-color:#ff9a3c}.pga-btn:active{transform:translateY(2px);box-shadow:0 0 0 1px #000,0 1px 0 #000}",
    ".pga-btn:focus-visible,.pga-x:focus-visible,.pga-tile:focus-visible{outline:3px solid #35d6c6;outline-offset:2px}",
    ".pga-back{position:fixed;inset:0;z-index:2147483000;display:flex;align-items:center;justify-content:center;padding:max(12px,env(safe-area-inset-top)) 12px 12px;background:rgba(4,3,2,.82);overflow:auto;overscroll-behavior:contain}",
    ".pga-back[hidden]{display:none}",
    ".pga-modal{position:relative;width:min(920px,100%);max-height:100%;overflow:auto;color:#f6e6c4;font:16px/1.4 system-ui,sans-serif;border-radius:16px;padding:18px 18px 16px;",
    "background:radial-gradient(120% 80% at 50% 0,#2a1d14 0,#14100c 55%,#0b0907 100%);border:3px solid #5a4430;box-shadow:0 0 0 2px #000,0 0 0 5px #2a2018,0 20px 60px rgba(0,0,0,.7),inset 0 0 40px rgba(255,140,40,.08)}",
    ".pga-head{display:flex;flex-direction:column;align-items:center;gap:6px;padding:4px 36px 12px}",
    ".pga-head img{display:block;width:min(340px,70%);height:auto}",
    ".pga-sub{margin:0;font-size:.82rem;letter-spacing:.18em;text-transform:uppercase;color:#35d6c6;text-align:center}",
    ".pga-x{position:absolute;top:10px;right:10px;width:44px;height:44px;border-radius:10px;cursor:pointer;appearance:none;font:700 22px/1 system-ui;color:#f6e6c4;background:#1c1612;border:2px solid #4a3a2a;touch-action:manipulation}",
    ".pga-x:hover{border-color:#ff9a3c}",
    ".pga-grid{list-style:none;margin:0;padding:0;display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,240px),1fr));gap:14px}",
    ".pga-tile{display:flex;flex-direction:column;height:100%;text-decoration:none;color:inherit;border-radius:12px;overflow:hidden;position:relative;",
    "background:linear-gradient(#211811,#120d0a);border:2px solid #4a3a2a;box-shadow:0 0 0 1px #000,0 4px 0 #000;transition:transform .15s,border-color .15s,box-shadow .15s}",
    ".pga-tile:hover,.pga-tile:focus-visible{border-color:#ff9a3c;transform:translateY(-3px);box-shadow:0 0 0 1px #000,0 7px 0 #000,0 0 18px rgba(255,154,60,.35)}",
    ".pga-tile[aria-current=page]{border-color:#35d6c6;box-shadow:0 0 0 1px #000,0 4px 0 #000,0 0 16px rgba(53,214,198,.3)}",
    ".pga-shot{display:flex;align-items:center;justify-content:center;aspect-ratio:3/2;padding:10px;background:radial-gradient(circle at 50% 40%,#1e2a2a,#0a0e0e);border-bottom:2px solid #000}",
    ".pga-shot img{display:block;max-width:100%;max-height:100%;width:auto;height:auto;object-fit:contain}",
    ".pga-info{padding:10px 12px 14px;display:flex;flex-direction:column;gap:4px;flex:1}",
    ".pga-name{margin:0;font-size:1.1rem;font-weight:800;color:#ffd9a0}",
    ".pga-blurb{margin:0;font-size:.9rem;color:#d9c8a6}",
    ".pga-tag{margin-top:auto;padding-top:6px;font-size:.78rem;letter-spacing:.12em;text-transform:uppercase;color:#ff9a3c;font-weight:700}",
    ".pga-tile[aria-current=page] .pga-tag{color:#35d6c6}",
    ".pga-badge{position:absolute;top:8px;left:8px;padding:2px 8px;border-radius:6px;background:#35d6c6;color:#04201d;font:800 .72rem/1.5 system-ui;letter-spacing:.1em;text-transform:uppercase}",
    "@media (max-width:560px){.pga-modal{padding:14px 12px 12px}.pga-grid{gap:10px}.pga-tile{flex-direction:row}.pga-shot{aspect-ratio:auto;width:42%;flex:none;border-bottom:0;border-right:2px solid #000}.pga-name{font-size:1rem}.pga-blurb{font-size:.84rem}}",
    "@media (prefers-reduced-motion:no-preference){.pga-back{animation:pga-in .18s ease-out}.pga-modal{animation:pga-up .22s ease-out}}",
    "@keyframes pga-in{from{opacity:0}}@keyframes pga-up{from{transform:translateY(12px) scale(.98);opacity:.6}}",
    "@media (prefers-reduced-motion:reduce){.pga-tile,.pga-tile:hover{transition:none;transform:none}}"
  ].join("");

  function el(tag, cls, attrs) {
    var n = document.createElement(tag);
    if (cls) n.className = cls;
    for (var k in attrs || {}) { if (k === "text") n.textContent = attrs[k]; else n.setAttribute(k, attrs[k]); }
    return n;
  }
  function img(file, alt, w, h) {
    return el("img", "", { src: asset(file), alt: alt, width: w, height: h, decoding: "async" });
  }

  var back, modal, closeBtn, opener, bar;

  function focusables() {
    return [].slice.call(modal.querySelectorAll("a[href],button")).filter(function (n) { return !n.disabled; });
  }
  function close() {
    if (back.hidden) return;
    back.hidden = true;
    document.removeEventListener("keydown", onKey, true);
    if (opener) opener.setAttribute("aria-expanded", "false");
    if (opener && opener.focus) opener.focus();
  }
  function open() {
    back.hidden = false;
    opener.setAttribute("aria-expanded", "true");
    document.addEventListener("keydown", onKey, true);
    var cur = modal.querySelector("[aria-current=page]");
    (cur || closeBtn).focus();
  }
  // The host game listens for keys on window; stop them while the dialog is up.
  function onKey(e) {
    if (e.key === "Escape") { e.preventDefault(); e.stopImmediatePropagation(); close(); return; }
    if (e.key === "Tab") {
      var f = focusables(), i = f.indexOf(document.activeElement);
      if (!f.length) return;
      if (e.shiftKey && i <= 0) { e.preventDefault(); f[f.length - 1].focus(); }
      else if (!e.shiftKey && (i < 0 || i === f.length - 1)) { e.preventDefault(); f[0].focus(); }
    }
    e.stopImmediatePropagation();
  }

  function build() {
    var st = el("style"); st.textContent = CSS; document.head.appendChild(st);

    bar = el("div", "pga-bar");
    opener = el("button", "pga-btn", { type: "button", "aria-haspopup": "dialog", "aria-expanded": "false", "aria-label": "PHATT Gaming Arcade: open game list" });
    opener.appendChild(img("phatt-gaming-arcade.webp", "", 640, 238));
    opener.firstChild.alt = "";
    bar.appendChild(opener);
    var mount = document.querySelector("[data-arcade-mount]") || document.querySelector("#overlay .card");
    if (mount) mount.insertBefore(bar, mount.firstChild);
    else { bar.className += " pga-float"; document.body.appendChild(bar); }

    back = el("div", "pga-back"); back.hidden = true;
    modal = el("div", "pga-modal", { role: "dialog", "aria-modal": "true", "aria-labelledby": "pga-title" });
    var head = el("div", "pga-head");
    head.appendChild(img("phatt-gaming-arcade.webp", "PHATT Gaming Arcade", 640, 238));
    head.appendChild(el("h2", "pga-sub", { id: "pga-title", text: "Choose your cabinet" }));
    closeBtn = el("button", "pga-x", { type: "button", "aria-label": "Close arcade menu", text: "×" });
    var grid = el("ul", "pga-grid");
    GAMES.forEach(function (g) {
      var li = el("li");
      var here = g.id === current;
      var a = el("a", "pga-tile", { href: g.url });
      if (here) a.setAttribute("aria-current", "page");
      var shot = el("div", "pga-shot"); shot.appendChild(img(g.logo, "", g.w, g.h)); shot.firstChild.alt = "";
      var info = el("div", "pga-info");
      info.appendChild(el("h3", "pga-name", { text: g.name }));
      info.appendChild(el("p", "pga-blurb", { text: g.blurb }));
      info.appendChild(el("span", "pga-tag", { text: here ? "You are here" : "Play now" }));
      a.appendChild(shot); a.appendChild(info);
      if (here) a.appendChild(el("span", "pga-badge", { text: "Current game", "aria-hidden": "true" }));
      a.addEventListener("click", function (e) {
        if (here) { e.preventDefault(); close(); return; }
        // Never drop a player out of a running match by accident.
        if (document.body.classList.contains("playing") && !confirm("Leave this game and go to " + g.name + "?")) e.preventDefault();
      });
      li.appendChild(a); grid.appendChild(li);
    });
    modal.appendChild(closeBtn); modal.appendChild(head); modal.appendChild(grid);
    back.appendChild(modal); document.body.appendChild(back);

    opener.addEventListener("click", open);
    closeBtn.addEventListener("click", close);
    back.addEventListener("mousedown", function (e) { if (e.target === back) close(); });
  }

  if (document.body) build(); else document.addEventListener("DOMContentLoaded", build);
})();
