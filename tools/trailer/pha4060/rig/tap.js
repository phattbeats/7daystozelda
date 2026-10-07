// #4060 capture: mirror everything sent to an AudioContext's speakers into a MediaStream, and record canvas+audio.
(() => {
  const conn = AudioNode.prototype.connect;
  AudioNode.prototype.connect = function (dst, ...rest) {
    const r = conn.call(this, dst, ...rest);
    try {
      if (dst instanceof AudioDestinationNode) {
        const ctx = dst.context;
        if (!ctx.__tap) { ctx.__tap = ctx.createMediaStreamDestination(); window.__tapCtx = ctx; }
        conn.call(this, ctx.__tap);
      }
    } catch (e) {}
    return r;
  };
  window.__rec = {
    start(name, fps, bps) {
      const cv = document.querySelector('canvas#canvas') || document.querySelector('canvas');
      const s = cv.captureStream(fps || 30);
      if (window.__tapCtx) window.__tapCtx.__tap.stream.getAudioTracks().forEach(t => s.addTrack(t));
      const mr = new MediaRecorder(s, { mimeType: window.__mime || 'video/webm;codecs=vp8,opus', videoBitsPerSecond: bps || 20000000, audioBitsPerSecond: 192000 });
      let q = Promise.resolve();
      mr.ondataavailable = e => { if (e.data.size) { const d = e.data; q = q.then(() => fetch('http://127.0.0.1:19460/' + name, { method: 'POST', body: d }).catch(() => {})); } };
      mr.onstop = () => { q.then(() => { window.__recDone = name; }); };
      window.__recDone = null; mr.start(1000); window.__mr = mr;
      return { w: cv.width, h: cv.height, audio: s.getAudioTracks().length };
    },
    stop() { if (window.__mr && window.__mr.state !== 'inactive') window.__mr.stop(); }
  };
})();
