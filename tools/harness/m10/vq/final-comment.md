Both picked pieces are built and verified in a raid in the browser game. Chrome now runs on your NVIDIA GPU.

**Scarecrow decoy** (start unlock, 4 Wood + 4 Fiber). A Stalchild came off the workbench lane, stopped at the scarecrow, and broke it from 80 HP to zero. After it broke, the Stalchild went back to normal routing toward the bench.
- [Stalchild drawn to the scarecrow](/api/attachments/18288049-eb95-44ce-919a-6c62af052c24/content)
- [Scarecrow taking damage](/api/attachments/5973f262-2a86-496c-8fd5-8d34b7dc8366/content)
- [Scarecrow broken, Stalchild reroutes](/api/attachments/26ec72c8-89a8-4f60-8971-9e151a376a16/content)

**Guard Baba** (unlocked by the Deku Tree). A Stalchild heading for the bench was bitten from 2 HP to 1, then killed (+1 Bone). The rerouted Stalchild got the same treatment, and the wave cleared ("The village held").
- [Bite and kill, +1 Bone](/api/attachments/18727a10-9dc8-4cc6-9b26-8b9fe23e9d69/content)
- [Kill, wave cleared, zoomed](/api/attachments/751f9ad1-fe7f-4821-8a64-4b2be79e7ee9/content)
- [Kill, wave cleared, full frame](/api/attachments/f71cc109-ae54-43b4-a6b5-382cdff3802d/content)
- [Per-second raid log](/api/attachments/568a24e3-f446-4c78-86be-a7abbe6ae05d/content) (raider HP and positions, piece HP)

**Bug found and fixed.** Stalchildren stop about 60 units from the scarecrow, but it only took damage from raiders within about 50. A Stalchild-only crowd could stand at it forever without breaking it, and the wave would stall. I gave the scarecrow a 64-unit damage reach. The raid above ran on that build.

**GPU.** Headless Chrome now uses the Quadro K2200 (ANGLE gl-egl) and the game runs at about 44 fps, up from 2-4. Launcher: /tmp/m10web/launch-gpu.sh.

**Saved.** Patch 0025 (commit 6870a71) and the bundle soh-coop-web-2026-10-03-pha3915.tar.gz (patch, build, evidence) are on Nextcloud at PHATT-TECH/Projects/7daystozelda/. I did not deploy it to the public web build.

**Small gaps:**
- The Guard Baba recipe costs 3 Rot + 2 Fiber. The 1 Deku Nut from your placeholder cost isn't in it.
- Keese circle the scarecrow but fly too high to damage it.
