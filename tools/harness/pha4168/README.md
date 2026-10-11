# Ocarina sync rig (PHA-4168)

Two-client rig as pha4167 (ports +1 here). `oca1.py`: A equips the ocarina (`equip:ocarina` raid cmd), opens it on C-left (ArrowLeft) and holds A / C-down / C-right; `anchor_test_oca` on both clients shows A localNote, B ocaNote/played/puppetOca/voiced.

`rejoin3.sh` + `orch.py` (orchestra): adds client C (port 19888, prof-C copied from prof-B). All three hold different notes at once; `voices` in `anchor_test_oca` has bit i for a held remote voice slot, bit 4+i when its layer has a note playing.
`diag.py`: per-layer state of the ocarina SFX channel (`diag` in `anchor_test_oca`) around a local release. C profile copied from B keeps B's Anchor id: zero `gRemote.Anchor.LastClientId` in both /shipofharkinian.json and /persist/shipofharkinian.json, then rejoin.
