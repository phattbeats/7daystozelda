#!/bin/bash
# (re)start this rig's server.js; matches by PID file, never by command text
D=/tmp/z4155/rig
[ -f $D/server.pid ] && kill $(cat $D/server.pid) 2>/dev/null; sleep 1
cd $D && PORT=18455 ANCHOR_HOST=127.0.0.1 ANCHOR_PORT=43455 PUBLIC_DIR=$D/serve setsid node /tmp/z4155/repo/web/server.js > $D/server.log 2>&1 < /dev/null &
echo $! > $D/server.pid
