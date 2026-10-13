# PHA-4242: 7DtZ lobby page audit and design direction

Live page audited: `soh-web:pha4177` on 2026-10-13, at phone (390x844), tablet (820x1180) and desktop (1440x900).
Captured through Browserless on PHATT-RAID (`shoot.py`).

## Audit: where the patchwork shows

The page runs on two stylesheets that fight each other. The first-launch sheet (slate cards, `#d9b55b` gold, blue
focus ring, system font) still sits underneath. On top of it is the PHA-3906 "N64 reskin" (royal-blue gradient
panels, `#c9a24a` gold borders, Georgia, CRT scanlines), which wins through about ten `!important` overrides.
Each later milestone added its block in whichever of the two it was copied from.

**Type**
- Five families on one page: Georgia (titles, tagline), system-ui (body, buttons), ui-monospace (kbd, hex),
  Tahoma and Courier New (the arcade button).
- Panel titles are Georgia gold with a glow ("1 · GAME FILES"). The sub-headings inside them ("LOBBY CODE",
  "OPEN ROOMS") are system-ui muted caps, and the field labels ("Your name", "Your colors") are sentence-case grey.
  That makes three heading styles for one level.
- Inline `style=` font sizes (.76rem, .8rem) on the raids and colours labels.

**Colour**
- Five golds: `#d9b55b`, `#c9a24a`, `#ffd85a`, `#f3e2a8`, `#ffe9a8`.
- Primary "Join room" is a blue gradient and "Play solo" is a green gradient. Neither colour appears anywhere else.
- N64 four-colour bar under the tagline (red/yellow/green/blue): console branding, not the game's.
- Arcade strip: near-black bar with an orange rule (shared PHATT arcade component, PHA-4216).

**Components**
- Seven button looks: grey `.btn`, blue primary, green solo, black "Create lobby", gold-outline tabs with emoji,
  the arcade's brown bevel, and underlined `.linkish` links.
- Panels: steps 1 and 2 are bordered panels, but "3 · Controls" is a rule-separated block inside panel 2.
  "Optional · HD textures" (hdpack.js) sits between 1 and 2.
- Menu music widget (`#bgm`) is fixed bottom-right. **On phone it covers the "Quick match" row** (see
  before-phone-first-screen). It also reuses the `.ctl` class, so it picks up the Controls block's top border
  (the stray line inside the widget).
- Emoji used as icons in buttons (⚡ 📖 📱 🎮 ⌨️).

**Flow and layout**
- Joining by room name, quick match, lobby codes and the open-room list all sit in one undivided stack, and the
  actual "Join room" / "Play solo" buttons are at the very bottom, about 2,000px down on phone. Raid interval and
  colours sit between them.
- Name and room are paired, but room belongs with joining and name belongs with colours (who you are vs where
  you go).
- Desktop renders the phone column (480px) in the middle of a 1440 screen.
- Two "Copy invite link" buttons (room invite and lobby-code invite) with no visual relation.

**Copy voice**
- Placeholders are in-jokes ("Tyler", "boys").
- Mixed numbering ("1 · Game files", "Optional · HD textures", "2 · Who's playing", "3 · Controls").
- The guide button carries the copy "Official Strategy Guide — free" with an em dash. There is no devlog link,
  although the phattmedia.club tag page exists.

**Leftovers**
- CRT scanline overlay and blue-violet tint from the PHA-3906 reskin.
- `index.html.pre-fairypicker` (+ .gz) still shipped in `/app/public`.
- The `invite only` page (server.js) has its own inline style: system font on `#0b0d12`.
- In-game chrome (net pill, lobby-code badge, rotate prompt, crash panel) uses the first-launch slate tokens.

## Direction: "Camp at night"

Ocarina of Time meets survival. The page is the camp after dark: the existing camp art stays as the backdrop,
under a darker scrim, and everything on it is one family of night-blue panels framed in Triforce gold.
It takes from OoT's pause and file-select screens (dark translucent panels, gold titling, a diamond step marker
like the menu cursor). It doesn't borrow the Lodge's warm wood or the arcade's orange.

**Palette** (tokens in `lobby.css`)
| token | value | use |
|---|---|---|
| `--night` | `#0a0c13` | page base |
| `--panel` | `rgba(14,17,29,.9)` | every panel and dialog |
| `--raise` / `--raise-2` / `--raise-3` | `#181d30` / `#222941` / `#2c3452` | inputs, secondary buttons, hover |
| `--line` / `--line-2` | gold at 20% / 42% | hairlines and borders |
| `--text` | `#f1e9d6` parchment | body |
| `--muted` | `#aba493` | help text, labels (about 7:1 on panel) |
| `--gold` / `--gold-2` | `#e8c980` / `#f7e0a6` | titles, the one primary action, focus |
| `--ok` `--warn` `--bad` | `#8ad58e` `#ecb94f` `#ec7a64` | status only |

**Type**: two families, self-hosted (OFL, about 48 KB in total).
- Cinzel 700 for panel titles and the tagline: engraved Roman caps that sit with the logo.
- Nunito 600/800 for everything else: rounded like OoT's dialogue font, and very legible small.
- Scale: 12 caps label (tracking .11em), 14 help, 16 body and inputs, 18 panel title.

**Components** (one of each)
- Panel: 14px radius, gold hairline, soft drop shadow, 24px padding (16px phone).
- Step marker: gold diamond with the step number.
- Buttons: primary (gold fill, used once per screen: Join room), secondary (raised night with gold hairline), and
  text link (gold underline). All are at least 48px high (tabs and room-list buttons 40-42px).
- Inputs: 48px, raised night, gold focus. Lobby code in mono with wide tracking.
- Segmented control: Phone / Controller / Keyboard tabs and Private / Public.
- Field label: 12px caps muted, with an optional right-aligned note.

**Spacing**: a 4px base (4/8/12/16/24/32). **Motion**: one 160ms ease for hover and press, a short logo rise,
and nothing else. Reduced-motion turns it all off.

**Structure**
1. Arcade strip across the top, with the menu music control on its right (no longer floating over content).
2. Hero: logo, "Ocarina co-op survival", and one line of pitch.
3. **1 Game files**: ROM drop or ready state.
4. **2 Your Link**: name and colours (who you are).
5. **3 Play**: room, then Join room / Play solo / Copy invite link straight after. Below "or find players": your
   save, Quick match, lobby code join/create, open rooms, days between raids.
6. **Controls & guides**: the tabs, then Strategy guide and Devlog buttons.
7. HD textures (desktop only, optional) last.

Phone and tablet use one column. Desktop (1100px and up) uses two: who you are on the left, play on the right.

## Build notes
- `port_lobby_pha4242.py LIVE_INDEX OUT` rebuilds the page from the live index. It moves the existing fragments,
  keeps every element id (asserted), keeps the in-game touch-pad CSS verbatim, and points the arcade mount at
  `#topbar`. `phatt-arcade.js` is untouched (byte-identical across games).
- Fonts go in `public/fonts/`. server.js needs `".woff2": "font/woff2"` in MIME.
- Still to do in the build pass: hdpack.js title ("HD textures" + Optional pill), invite-only page, delete the
  `.pre-fairypicker` leftovers, and a regression of every flow plus the two-client test.
