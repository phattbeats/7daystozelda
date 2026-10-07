PHA-4049 Phantom Ganon, two-client live test (rig: two GPU headless Chrome clients + local anchor).
File prefix: in = intro, p/arrow/hit = painting phase and forwarded hits, rb = reflected energy ball
(rb2-<returner>-<viewer>), d = defeat. Suffix -A / -B is the client that took the screenshot.
Clients: B = authority (lowest clientId in scene), A = mirror.
Verified: intro in lockstep; painting-phase arrow hits from A and B (hp 30 -> 24, NEUTRAL mirrored);
neutral-phase sword hits from both players (24 -> 0, identical hp on both clients);
authority-side defeat (death cutscene, 1 blue warp, 1 heart, clear flag on both clients);
ball reflected by B (host) and by A (mirror); spawn-ring replay (ringSeq == replaySeq).
Not verified live: killing blow landed by the mirror; host leaving mid-fight; merged build with all other adapters.
