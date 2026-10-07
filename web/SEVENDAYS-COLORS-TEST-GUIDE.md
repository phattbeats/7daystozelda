# Player colors: fairy gradient and tunic (#3901)

Each player picks three colors that everyone in the room sees:

- **Fairy core** and **fairy aura**: the gradient on your fairy. Other players' screens show it on the companion fairy that follows your Link. Your own idle Navi uses it too, so you see what they see.
- **Tunic**: your Link's tunic and cap on every screen. This needs 7 Days to Zelda with "Tunic in my lobby tunic color" on (the default). If it's off, tunics stay vanilla and the fairies are still colored.

## Where to set them

- **Web lobby, "Your colors":** a color picker, a hex field and nine swatches each for core, aura and tunic. The fairy preview and the tunic chip update live. Your colors are saved in the browser.
- **In game, Settings → Network → Anchor, "Your Colors":** the same three pickers, plus "My Navi uses my fairy colors". Turn that off to keep the Cosmetics Editor's Navi colors. Changes made mid-session reach everyone right away; there's no need to rejoin.
- **Links:** the rejoin link (the address bar after you join) carries `fairy=RRGGBB-RRGGBB&tunic=RRGGBB` (core-aura). An invite link carries the inviter's colors. A friend who opens it keeps their own saved colors and gets a warning if their tunic is close to the inviter's. Old links with only `color=` still work: that color becomes the aura and the tunic.

## Telling Links apart

If your tunic is hard to tell apart from another player's, you get a warning. It shows under the lobby pickers (against the inviter and the players the game saw in that room last time) and in the in-game picker (against everyone online).

## Older clients

`color` is still sent and holds your aura. An older build tints your fairy's aura with it and leaves the core white, as before. From an older client, this build reads `color` as both the aura and the tunic, with a white core.

## Test (2 players)

1. Player A: in the lobby, set the core to pale yellow, the aura to gold and the tunic to red. Join room X and copy the invite link.
2. Player B: open the invite. B's own colors load, not A's. Pick red for the tunic: a warning names your inviter. Pick purple and the warning clears. Set an ice-blue core and a blue aura, then join.
3. Both load a file and stand next to each other. A sees B's Link in purple with an ice-blue/blue fairy. B sees A's Link in red with a yellow/gold fairy. Each player's own Navi glows in their own gradient.
4. A opens Settings → Network → Anchor and changes the aura and tunic. B sees the change within about a second.
5. Regression: with 7 Days to Zelda off, both Links wear the vanilla green tunic. The fairies are still colored, and the rest of the M3 co-op guide is unchanged.

Automated version: `tools/webtest/colors-test.py join <tag>` (the lobby and the invite) and `tools/webtest/colors-ingame.py <tag>` (in game, with live-update timing). Both use the profiles `c9A` and `c9B`, each with a file 1.
