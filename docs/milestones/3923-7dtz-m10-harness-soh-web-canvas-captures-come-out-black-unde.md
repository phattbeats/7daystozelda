# #3923: 7DtZ M10 harness: soh web canvas captures come out black under browserless

Status at export (2026-10-03): done

This is a fix to the runtime harness only. The game work stays with Vision Quest on #3915.

Setup, all in the shared container:
- The M10 build is served by python3 /tmp/m9tools/serve.py on :18080 from /tmp/m10web/serve. A relay bridges /anchor through /tmp/relay/server.js.
- /tmp/m10web/launch.sh starts /tmp/m9tools/playd.py on port 19710. playd drives browserless at ws://10.0.0.100:3000 with SwiftShader GL and opens http://172.19.0.16:18080/#name=Tester&room=m10a.

Symptom: s1.png and s2.png in /tmp/m10ev rendered. Every capture from 08:05 onward (s3 through s8_3, all 4741 bytes) is a black canvas with only the room badge visible. playd.log shows one 404.

Done when: a fresh playd session renders the game, with a screenshot showing gameplay posted here. Then mark this issue done so #3915 unblocks back to Vision Quest. Likely things to check: browserless restarted or its IP changed, the 404'd asset in serve.log, or the WebGL context getting lost.
