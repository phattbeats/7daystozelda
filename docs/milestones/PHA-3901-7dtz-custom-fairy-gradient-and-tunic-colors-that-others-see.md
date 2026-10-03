# PHA-3901: 7DtZ: custom fairy gradient and tunic colors that others see

Status at export (2026-10-03): done

Brandon (PHA-3871, 2026-10-02): "custom fairy color with gradient that shows in game to others; custom tunic color to distinguish links. These might exist somewhere already."

**What exists today** (patch 0014 tree):
- The lobby has one color with four preset swatches (Green/Red/Blue/Purple), saved as `gRemote.Anchor.Color` and sent as `color` in Anchor client state.
- Other players see it on your puppet fairy, but only on the outer aura (`PuppetFairy.cpp`; the inner core stays white). Your own Navi isn't tinted.
- With `gSevenDays.TunicColors` on, the same color also tints every Link's tunic and cap (`TunicColors.cpp`), so the fairy and the tunic can't differ.
- SoH's Cosmetics Editor already has Navi inner/outer and tunic colors, but they're local only: other players never see them.

**Build:**
1. **Fairy gradient:** two colors, inner (core) and outer (aura), sent in client state as `fairyInner` and `fairyOuter`. Older clients keep reading `color`, which falls back to the outer color. Others' puppet fairies use both. Your own Navi uses them too, so you see what others see (option to keep your cosmetics instead).
2. **Tunic color:** its own color (`tunic`), separate from the fairy, used by TunicColors for every Link, including DummyPlayer puppets.
3. **Pickers:** a full custom picker (hex + swatches) in the web lobby for fairy inner, fairy outer and tunic, with a live preview of the fairy SVG gradient and a tunic chip. The invite link carries the colors (`#fairy=RRGGBB-RRGGBB&tunic=RRGGBB`). Matching ImGui pickers in Settings → Network → Anchor.
4. **Live updates:** changing a color mid-session updates everyone within a second (UPDATE_CLIENT_STATE).
5. **Readability:** tunic colors too close to another player's get a warning in the lobby.

**Done when:** two browser profiles in one room are played in real browsers, with screenshots showing each player's gradient fairy and tunic on the other's screen, and the M3 co-op guide and the 7 Days off path are unchanged. Ship as the next numbered patch in a new bundle on Nextcloud.
