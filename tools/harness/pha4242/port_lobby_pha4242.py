#!/usr/bin/env python3
"""PHA-4242: rebuild the 7DtZ lobby page on one design system.

usage: port_lobby_pha4242.py LIVE_INDEX OUT_INDEX [CSS]

Takes the live (minified) index.html, keeps every script and every element id the scripts use,
and replaces the presentation:
  * both <style> blocks -> the in-game rules (canvas, touch pad) kept verbatim + lobby.css
  * the #overlay markup -> one column on phone/tablet, two columns on desktop, three numbered
    steps (Game files, Your Link, Play) + Controls & guides; existing fragments are moved, not rewritten
  * #bgm (menu music) moves into the arcade strip at the top; the arcade mounts on #topbar
Every replacement asserts its anchor is found exactly once, so a changed live page fails loudly.
"""
import re, sys

src, out = sys.argv[1], sys.argv[2]
css_path = sys.argv[3] if len(sys.argv) > 3 else __file__.rsplit("/", 1)[0] + "/lobby.css"
s = open(src, encoding="utf-8").read()


def one(hay, needle):
    n = hay.count(needle)
    assert n == 1, f"expected 1 of {needle[:70]!r}, found {n}"
    return hay.index(needle)


def cut(hay, start, end, incl_end=True):
    """Return the substring from `start` to `end` (first end after start)."""
    i = one(hay, start)
    j = hay.index(end, i + len(start))
    return hay[i:j + (len(end) if incl_end else 0)]


# ---------- CSS ----------
styles = re.findall(r"<style\b[^>]*>(.*?)</style>", s, flags=re.S)
assert len(styles) == 2, len(styles)


def top_rules(css):
    out, depth, cur = [], 0, ""
    for ch in css:
        cur += ch
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                out.append(cur.strip()); cur = ""
    return out


KEEP = ("#canvas", "body.playing", "#touch-", ".touch-", "#tb-")
kept = [r for r in top_rules(styles[0]) if r.startswith(KEEP)]
assert len(kept) >= 15, len(kept)
new_css = "".join(kept) + "\n" + open(css_path, encoding="utf-8").read()
s = s.replace("<style>" + styles[0] + "</style>", "<style>" + new_css + "</style>", 1)
s = s.replace("<style>" + styles[1] + "</style>", "", 1)
assert s.count("<style>") == 1

# ---------- fragments reused from the live page ----------
rom_inner = cut(s, "<div id=rom-loading>", "<div id=error-msg role=alert></div>")
colors = cut(s, "<div class=colors>", "<div id=tunic-warn role=status aria-live=polite hidden></div>")
mm = cut(s, "<div id=mm class=mm>", "<label class=f style=margin-top:.8rem>Days between raids", incl_end=False)
raids = cut(s, "<select id=f-raids name=raids>", "</select>")
hidden_inputs = "<input type=checkbox id=f-horde hidden><input type=checkbox id=f-sevendays checked hidden>"
one(s, hidden_inputs)
ctl = cut(s, "<div class=ctl-tabs", "<p class=ctl-guide>", incl_end=False)
name_in = cut(s, "<input id=f-name", ">")
room_in = cut(s, "<input id=f-room", ">")
bgm = cut(s, "<div id=bgm role=region", "</div></div></div>")

# copy + markup tidy inside the moved fragments
mm = mm.replace("⚡ Quick match", "Quick match")
mm = mm.replace("<div class=mm-list-h><span>Lobby code</span></div>", "<div class=lbl><span>Lobby code</span></div>")
mm = mm.replace('<span class=muted>Your progress</span>', '<span class=muted>Your save</span>')
ctl = ctl.replace("📱 Phone", "Phone").replace("🎮 Controller", "Controller").replace("⌨️ Keyboard", "Keyboard")
name_in = name_in.replace("placeholder=Tyler", 'placeholder="What should we call you?"')
room_in = room_in.replace("placeholder=boys", 'placeholder="Name a room, or use your invite"')
bgm = bgm.replace("<div class=ctl>", "<div class=mus>")

GUIDE = "https://phattmedia.club/7dtz-official-strategy-guide/"
DEVLOG = "https://phattmedia.club/tag/7-days-to-zelda/"

overlay = (
    "<div id=overlay><header id=topbar>" + bgm + "</header><div class=card>"
    "<form aria-labelledby=play-h autocomplete=off id=join>"
    "<div class=col-a>"
    "<div class=brand><img src=home-logo.png alt=\"7 Days to Zelda\" width=960 height=398 decoding=async>"
    "<h1 class=sr>7 Days to Zelda</h1><p class=tagline>Ocarina co-op survival</p>"
    "<p class=lede>Gather, craft and wall in your camp. Hold Hyrule through the raids, together.</p></div>"
    "<section class=panel aria-labelledby=rom-h><h2 id=rom-h><span class=step>1</span>Game files</h2>"
    + rom_inner + "</section>"
    "<section class=panel aria-labelledby=link-h><h2 id=link-h><span class=step>2</span>Your Link</h2>"
    "<label class=f><span class=lbl>Name</span>" + name_in + "</label>"
    "<div class=f><span class=lbl>Colors</span><p class=help>Everyone sees your fairy's glow and your tunic in these. "
    "You can change them in game too: Settings &rarr; Network &rarr; Anchor.</p>" + colors + "</div>"
    "</section></div>"
    "<div class=col-b>"
    "<section class=panel aria-labelledby=play-h><h2 id=play-h><span class=step>3</span>Play</h2>"
    "<label class=f><span class=lbl>Room</span>" + room_in + "</label>"
    "<div class=actions><button type=submit class=\"btn primary\" id=btn-join disabled>Join room</button>"
    "<button type=button class=btn id=btn-solo disabled>Play solo</button></div>"
    "<div class=invite><button type=button class=linkish id=btn-invite>Copy invite link</button>"
    "<span id=copy-note aria-live=polite></span></div>"
    "<div class=or><span>or find players</span></div>"
    + mm +
    "<label class=\"f raids\"><span class=lbl>Days between raids <small>the host's pick counts</small></span>"
    + raids + "</label>" + hidden_inputs + "</section>"
    "<section class=panel id=controls aria-labelledby=ctl-h><h2 id=ctl-h>Controls &amp; guides</h2>"
    + ctl +
    "<div class=sub><div class=guides>"
    "<a class=btn href=\"" + GUIDE + "\" target=_blank rel=noopener>Strategy guide</a>"
    "<a class=btn href=\"" + DEVLOG + "\" target=_blank rel=noopener>Devlog</a></div>"
    "<p class=help>Both open on phattmedia.club. The guide is free with an email sign-up.</p></div>"
    "</section></div>"
    "</form>"
    "<p class=foot>Bring your own legally dumped ROM. No Nintendo files are hosted here.</p>"
    "</div></div>"
)

i = one(s, "<div id=overlay>")
j = one(s, "<div id=net-pill")
s = s[:i] + overlay + s[j:]
# the old #bgm (outside the overlay) goes; the copy above is the only one
k = s.index(bgm.replace("<div class=mus>", "<div class=ctl>"))
s = s[:k] + s[k + len(bgm):]
assert s.count("id=bgm ") == 1

# arcade button mounts in the strip instead of the card
s = s.replace('data-mount="#overlay .card"', 'data-mount="#topbar"')
assert 'data-mount="#topbar"' in s

# every id the old page had must still exist exactly once
old_ids = set(re.findall(r"\bid=([\w-]+)", open(src, encoding="utf-8").read()))
for x in sorted(old_ids):
    n = len(re.findall(r"\bid=" + re.escape(x) + r"[\s>]", s))
    assert n == 1 or x in ("join-h",), f"id {x} found {n} times"
open(out, "w", encoding="utf-8").write(s)
print("ok", len(s), "bytes; kept", len(kept), "in-game css rules")
