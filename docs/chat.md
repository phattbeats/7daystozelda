# Text and voice chat (PHA-4175)

Browser players only; native clients are not bridged.

- `web/public/chat.js` builds its own UI once `window._anchorConfig` exists (room + name). It never touches the touch gamepad.
- `web/server.js` serves a `/chat` WebSocket (same access check as `/anchor`). Messages are per room, 240 chars, 5 per 4 s, last 40 replayed on join. The same socket carries WebRTC offer/answer/ICE; audio is peer-to-peer and never goes through the server.
- Phone: a 💬 button and a 🎙 hold-to-talk button sit under the network pill at the top right (48 px). The chat opens as a bottom sheet (18 px text, 48 px input), lifted above the keyboard with `visualViewport`. New messages show as short toasts at the top while the sheet is closed. Touches on the chat never reach the pad.
- Desktop: **T** opens chat, **Enter** sends, **Esc** closes, hold **V** to talk. While the chat box has focus its keys are stopped before SDL sees them.
- The first mic press only asks for permission. Per-player mute buttons live in the sheet and are remembered by name.
- Limits: STUN only (Google), no TURN, so strict NATs may not connect; WebRTC reveals IPs to room members; mesh voice, fine for the 4-12 player rooms.
