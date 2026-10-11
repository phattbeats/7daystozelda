// Text + voice chat for browser players (PHA-4175). Self-contained: builds its own UI,
// talks to the server's /chat socket, and never touches the touch gamepad.
// Voice is push-to-talk peer-to-peer WebRTC (mesh); /chat only carries signalling.
(function () {
  "use strict";
  var KEYS_BLOCKED = ["keydown", "keyup", "keypress"];
  var isTouch = "ontouchstart" in window || navigator.maxTouchPoints > 0;
  var cfg = null, ws = null, myId = 0, retry = 0, closedForGood = false;
  var peers = {}; // id -> {id, name, pc, audio, talking, muted, polite}
  var muted = {}; // name -> true (remembered)
  try { muted = JSON.parse(localStorage.getItem("soh.chatMuted") || "{}") || {}; } catch (e) {}
  var unread = 0, open = false, mic = null, micTrack = null, talking = false, micDenied = false;
  var ui = {};

  // ---- keep the game from eating typed keys ----
  // SDL listens on window in the capture phase; this listener is registered first, so it
  // can stop the event before SDL sees it while the chat box has focus.
  KEYS_BLOCKED.forEach(function (type) {
    window.addEventListener(type, function (e) {
      var t = e.target;
      if (t && t.id === "chat-input") {
        e.stopImmediatePropagation();
        if (type === "keydown") {
          if (e.key === "Enter") { e.preventDefault(); send(); }
          else if (e.key === "Escape") { e.preventDefault(); setOpen(false); }
        }
        return;
      }
      if (!cfg) return;
      if (type === "keydown" && !e.repeat && !e.ctrlKey && !e.metaKey && !e.altKey) {
        if (e.code === "KeyT" && !open) { e.preventDefault(); e.stopImmediatePropagation(); setOpen(true); return; }
        if (e.code === "KeyV") { e.stopImmediatePropagation(); ptt(true); return; }
      }
      if (type === "keyup" && e.code === "KeyV") { e.stopImmediatePropagation(); ptt(false); }
    }, true);
  });

  // ---- UI ----
  function el(tag, attrs, kids) {
    var n = document.createElement(tag);
    for (var k in attrs || {}) {
      if (k === "text") n.textContent = attrs[k];
      else n.setAttribute(k, attrs[k]);
    }
    (kids || []).forEach(function (c) { n.appendChild(c); });
    return n;
  }

  function buildUI() {
    var css = document.createElement("style");
    css.textContent = [
      "#chat-root{position:fixed;z-index:60;font:16px/1.35 system-ui,sans-serif;color:#f0ede4;display:none}",
      "body.playing #chat-root,#chat-root.on{display:block}",
      "#chat-dock{position:fixed;top:calc(env(safe-area-inset-top,0px) + 92px);right:calc(env(safe-area-inset-right,0px) + 8px);display:flex;flex-direction:column;gap:8px}",
      "#chat-dock button,#chat-sheet button{appearance:none;font:inherit;color:#f0ede4;background:rgba(10,14,20,.72);border:1px solid rgba(255,255,255,.2);border-radius:999px;min-width:48px;min-height:48px;padding:0 12px;touch-action:manipulation;-webkit-tap-highlight-color:transparent;position:relative}",
      "#chat-dock button{font-size:22px;opacity:.75}",
      "#chat-dock button.live,#chat-dock button:active{opacity:1;background:rgba(201,162,74,.75);border-color:#c9a24a}",
      "#chat-badge{position:absolute;top:-4px;right:-4px;min-width:20px;height:20px;border-radius:10px;background:#d9534f;font:700 12px/20px system-ui;text-align:center;padding:0 5px;display:none}",
      "#chat-toasts{position:fixed;top:calc(env(safe-area-inset-top,0px) + 8px);left:50%;transform:translateX(-50%);width:min(92vw,560px);display:flex;flex-direction:column;gap:4px;pointer-events:none}",
      "#chat-toasts div{background:rgba(10,14,20,.78);border-radius:10px;padding:6px 12px;font-size:18px;word-break:break-word;transition:opacity .5s}",
      "#chat-talk{position:fixed;top:calc(env(safe-area-inset-top,0px) + 8px);left:calc(env(safe-area-inset-left,0px) + 8px);display:flex;flex-direction:column;gap:4px;pointer-events:none}",
      "#chat-talk span{background:rgba(201,162,74,.85);color:#0b0f14;border-radius:999px;padding:4px 12px;font:600 15px system-ui}",
      "#chat-sheet{position:fixed;left:0;right:0;bottom:0;display:none;flex-direction:column;background:rgba(10,14,20,.94);border-top:1px solid rgba(201,162,74,.5);padding:8px calc(env(safe-area-inset-right,0px) + 10px) calc(env(safe-area-inset-bottom,0px) + 8px) calc(env(safe-area-inset-left,0px) + 10px);max-height:60vh;box-sizing:border-box}",
      "#chat-root.open #chat-sheet{display:flex}",
      "#chat-root.open #chat-toasts{display:none}",
      "#chat-head{display:flex;align-items:center;gap:8px;margin-bottom:6px}",
      "#chat-head b{flex:1;font-size:16px}",
      "#chat-log{flex:1;overflow-y:auto;overscroll-behavior:contain;-webkit-overflow-scrolling:touch;font-size:18px;min-height:90px}",
      "#chat-log p{margin:0 0 4px;word-break:break-word}#chat-log p b{color:#e6c46a}",
      "#chat-who{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:6px}",
      "#chat-who button{font-size:15px;min-height:44px;border-radius:10px}#chat-who button.m{opacity:.55;text-decoration:line-through}#chat-who button.t{background:rgba(201,162,74,.75);color:#0b0f14}",
      "#chat-form{display:flex;gap:8px;margin-top:6px}",
      "#chat-input{flex:1;min-width:0;min-height:48px;font-size:18px;color:#f0ede4;background:#161c25;border:1px solid rgba(255,255,255,.25);border-radius:10px;padding:0 12px}",
      "@media (max-height:420px){#chat-sheet{max-height:80vh;left:auto;width:min(55vw,440px)}}"
    ].join("\n");
    document.head.appendChild(css);

    ui.badge = el("span", { id: "chat-badge" });
    ui.chatBtn = el("button", { id: "chat-btn", type: "button", "aria-label": "Open chat", text: "💬" }, [ui.badge]);
    ui.micBtn = el("button", { id: "chat-mic", type: "button", "aria-label": "Hold to talk", text: "🎙" });
    ui.toasts = el("div", { id: "chat-toasts", "aria-live": "polite" });
    ui.talk = el("div", { id: "chat-talk" });
    ui.log = el("div", { id: "chat-log", role: "log" });
    ui.who = el("div", { id: "chat-who" });
    ui.input = el("input", { id: "chat-input", type: "text", maxlength: "240", autocomplete: "off", autocapitalize: "sentences", enterkeyhint: "send", placeholder: "Message" });
    var sendBtn = el("button", { type: "submit", text: "Send" });
    var closeBtn = el("button", { type: "button", "aria-label": "Close chat", text: "✕" });
    ui.form = el("form", { id: "chat-form" }, [ui.input, sendBtn]);
    ui.sheet = el("div", { id: "chat-sheet" }, [
      el("div", { id: "chat-head" }, [el("b", { text: "Chat" }), closeBtn]),
      ui.who, ui.log, ui.form
    ]);
    ui.root = el("div", { id: "chat-root" }, [
      el("div", { id: "chat-dock" }, [ui.chatBtn, ui.micBtn]), ui.toasts, ui.talk, ui.sheet
    ]);
    document.body.appendChild(ui.root);

    // Touches on the chat UI must never reach the gamepad underneath.
    ["touchstart", "touchmove", "touchend", "pointerdown", "mousedown"].forEach(function (t) {
      ui.root.addEventListener(t, function (e) { e.stopPropagation(); }, { passive: true });
    });
    ui.chatBtn.addEventListener("click", function () { setOpen(!open); });
    closeBtn.addEventListener("click", function () { setOpen(false); });
    ui.form.addEventListener("submit", function (e) { e.preventDefault(); send(); });
    ui.micBtn.addEventListener("contextmenu", function (e) { e.preventDefault(); });
    ui.micBtn.addEventListener("pointerdown", function (e) { e.preventDefault(); try { ui.micBtn.setPointerCapture(e.pointerId); } catch (x) {} ptt(true); });
    ["pointerup", "pointercancel", "lostpointercapture"].forEach(function (t) {
      ui.micBtn.addEventListener(t, function () { ptt(false); });
    });
    // Keep the sheet above the on-screen keyboard.
    if (window.visualViewport) {
      var place = function () {
        var vv = window.visualViewport;
        ui.sheet.style.bottom = Math.max(0, window.innerHeight - vv.height - vv.offsetTop) + "px";
        ui.sheet.style.maxHeight = Math.round(vv.height * (vv.height < 420 ? 0.9 : 0.6)) + "px";
        ui.log.scrollTop = ui.log.scrollHeight;
      };
      window.visualViewport.addEventListener("resize", place);
      window.visualViewport.addEventListener("scroll", place);
    }
  }

  function setOpen(v) {
    open = v;
    ui.root.classList.toggle("open", v);
    if (v) {
      unread = 0; badge();
      renderWho();
      ui.log.scrollTop = ui.log.scrollHeight;
      setTimeout(function () { ui.input.focus(); }, 30);
    } else {
      ui.input.blur();
    }
  }
  function badge() {
    ui.badge.textContent = unread > 9 ? "9+" : String(unread);
    ui.badge.style.display = unread ? "block" : "none";
  }

  function addLine(from, text, self) {
    var p = el("p", {}, [el("b", { text: from + ": " }), document.createTextNode(text)]);
    ui.log.appendChild(p);
    while (ui.log.childNodes.length > 80) ui.log.removeChild(ui.log.firstChild);
    ui.log.scrollTop = ui.log.scrollHeight;
  }
  function toast(from, text) {
    var d = el("div", { text: from + ": " + text });
    ui.toasts.appendChild(d);
    while (ui.toasts.childNodes.length > 3) ui.toasts.removeChild(ui.toasts.firstChild);
    setTimeout(function () { d.style.opacity = "0"; setTimeout(function () { d.remove(); }, 600); }, 6000);
  }

  function renderWho() {
    ui.who.textContent = "";
    Object.keys(peers).forEach(function (id) {
      var p = peers[id];
      var b = el("button", { type: "button", text: (p.talking ? "🎙 " : p.muted ? "🔇 " : "🔊 ") + p.name });
      if (p.muted) b.className = "m";
      else if (p.talking) b.className = "t";
      b.addEventListener("click", function () {
        p.muted = !p.muted;
        if (p.muted) muted[p.name] = true; else delete muted[p.name];
        try { localStorage.setItem("soh.chatMuted", JSON.stringify(muted)); } catch (e) {}
        applyMute(p); renderWho(); renderTalk();
      });
      ui.who.appendChild(b);
    });
  }
  function renderTalk() {
    ui.talk.textContent = "";
    if (talking) ui.talk.appendChild(el("span", { text: "🎙 You" }));
    Object.keys(peers).forEach(function (id) {
      var p = peers[id];
      if (p.talking && !p.muted) ui.talk.appendChild(el("span", { text: "🎙 " + p.name }));
    });
  }
  function applyMute(p) { if (p.audio) p.audio.muted = !!p.muted; }

  // ---- socket ----
  function connect() {
    if (closedForGood) return;
    var proto = location.protocol === "https:" ? "wss://" : "ws://";
    ws = new WebSocket(proto + location.host + "/chat");
    ws.onopen = function () {
      retry = 0;
      ws.send(JSON.stringify({ t: "hello", room: cfg.room, name: cfg.name }));
    };
    ws.onmessage = function (ev) {
      var m; try { m = JSON.parse(ev.data); } catch (e) { return; }
      if (m.t === "welcome") {
        myId = m.id;
        Object.keys(peers).forEach(dropPeer);
        ui.log.textContent = "";
        (m.history || []).forEach(function (h) { addLine(h.from, h.text); });
        (m.peers || []).forEach(function (p) { addPeer(p.id, p.name); });
        renderWho();
      } else if (m.t === "join") { addPeer(m.id, m.name); renderWho(); }
      else if (m.t === "leave") { dropPeer(m.id); renderWho(); renderTalk(); }
      else if (m.t === "msg") {
        var mine = m.id === myId;
        addLine(m.from, m.text);
        if (!mine && !open) { unread++; badge(); toast(m.from, m.text); }
      } else if (m.t === "talk") {
        var p = peers[m.id]; if (p) { p.talking = !!m.on; renderTalk(); if (open) renderWho(); }
      } else if (m.t === "sig") { onSignal(m.from, m.data); }
      else if (m.t === "slow") { toast("Chat", "Slow down a little."); }
    };
    ws.onclose = function (ev) {
      Object.keys(peers).forEach(dropPeer); renderWho(); renderTalk();
      if (closedForGood || ev.code === 1008 || ev.code === 1013) return;
      retry = Math.min(retry + 1, 6);
      setTimeout(connect, 1000 * retry);
    };
  }
  function send() {
    var text = ui.input.value.trim();
    if (!text || !ws || ws.readyState !== 1) return;
    ws.send(JSON.stringify({ t: "msg", text: text }));
    ui.input.value = "";
  }
  function sig(to, data) { if (ws && ws.readyState === 1) ws.send(JSON.stringify({ t: "sig", to: to, data: data })); }

  // ---- voice ----
  var ICE = { iceServers: [{ urls: "stun:stun.l.google.com:19302" }] };
  function addPeer(id, name) {
    if (peers[id]) return;
    var p = { id: id, name: name, talking: false, muted: !!muted[name], pc: null, audio: null };
    peers[id] = p;
    // The lower id makes the offer, so each pair negotiates exactly once.
    if (myId < id) startPc(p, true);
  }
  function dropPeer(id) {
    var p = peers[id]; if (!p) return;
    if (p.pc) try { p.pc.close(); } catch (e) {}
    if (p.audio) { p.audio.srcObject = null; p.audio.remove(); }
    delete peers[id];
  }
  function startPc(p, offerer) {
    var pc = new RTCPeerConnection(ICE);
    p.pc = pc;
    var tx = null;
    if (offerer) tx = pc.addTransceiver("audio", { direction: "sendrecv" });
    pc.onicecandidate = function (e) { if (e.candidate) sig(p.id, { c: e.candidate }); };
    pc.ontrack = function (e) {
      if (!p.audio) {
        p.audio = document.createElement("audio");
        p.audio.autoplay = true;
        p.audio.setAttribute("playsinline", "");
        document.body.appendChild(p.audio);
      }
      p.audio.srcObject = e.streams[0] || new MediaStream([e.track]);
      applyMute(p);
      var pr = p.audio.play(); if (pr && pr.catch) pr.catch(function () {});
    };
    if (micTrack) attachTrack(pc, tx);
    if (offerer) {
      pc.createOffer().then(function (o) { return pc.setLocalDescription(o); })
        .then(function () { sig(p.id, { d: pc.localDescription }); }).catch(function () {});
    }
    return pc;
  }
  function attachTrack(pc, tx) {
    var t = tx || pc.getTransceivers().filter(function (x) { return x.receiver && x.receiver.track && x.receiver.track.kind === "audio"; })[0];
    if (t && t.sender) { try { t.direction = "sendrecv"; t.sender.replaceTrack(micTrack); } catch (e) {} }
  }
  function onSignal(from, data) {
    var p = peers[from]; if (!p) return;
    if (data.d) {
      if (data.d.type === "offer") {
        if (p.pc) try { p.pc.close(); } catch (e) {}
        var pc = startPc(p, false);
        pc.setRemoteDescription(data.d).then(function () {
          if (micTrack) attachTrack(pc);
          return pc.createAnswer();
        }).then(function (a) { return pc.setLocalDescription(a); })
          .then(function () { sig(from, { d: pc.localDescription }); }).catch(function () {});
      } else if (p.pc) {
        p.pc.setRemoteDescription(data.d).catch(function () {});
      }
    } else if (data.c && p.pc) {
      p.pc.addIceCandidate(data.c).catch(function () {});
    }
  }
  function ensureMic() {
    if (mic) return Promise.resolve(true);
    if (micDenied || !navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) return Promise.resolve(false);
    return navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true } }).then(function (s) {
      mic = s; micTrack = s.getAudioTracks()[0]; micTrack.enabled = false;
      Object.keys(peers).forEach(function (id) { if (peers[id].pc) attachTrack(peers[id].pc); });
      return true;
    }, function () { micDenied = true; toast("Voice", "Microphone blocked. Allow it in the browser settings to talk."); return false; });
  }
  var pttWant = false;
  function ptt(on) {
    pttWant = on;
    if (on) {
      if (!mic) { // first press only asks for the microphone
        ensureMic().then(function (ok) { if (ok) toast("Voice", "Microphone ready. Hold to talk."); });
        return;
      }
      if (talking) return;
      talking = true; micTrack.enabled = true;
    } else {
      if (!talking) return;
      talking = false; if (micTrack) micTrack.enabled = false;
    }
    ui.micBtn.classList.toggle("live", talking);
    if (ws && ws.readyState === 1) ws.send(JSON.stringify({ t: "talk", on: talking }));
    renderTalk();
  }
  window.addEventListener("blur", function () { ptt(false); });

  // ---- start once the player has joined a room ----
  var timer = setInterval(function () {
    if (!window._anchorConfig || !window._anchorConfig.room) return;
    clearInterval(timer);
    cfg = window._anchorConfig;
    buildUI();
    ui.root.classList.add("on");
    connect();
  }, 500);
  window.GameChat = { open: function () { setOpen(true); } };
})();
