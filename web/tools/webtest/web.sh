#!/bin/bash
# usage: web.sh <public_dir>   (restarts the bundle server on :8080)
for p in $(pgrep -x node); do
  if tr '\0' ' ' < /proc/$p/cmdline | grep -q "server.js" && grep -q "PORT=8080" /proc/$p/environ 2>/dev/null; then kill $p; fi
done
sleep 1
cd /root/web-bundle && PUBLIC_DIR=$1 ANCHOR_HOST=127.0.0.1 PING_MS=3000 ACCESS_KEY=testkey PORT=8080 nohup node server.js > ${WEBTEST_DIR:-$HOME/webtest-work}/logs/web.log 2>&1 &
sleep 1.5; head -1 ${WEBTEST_DIR:-$HOME/webtest-work}/logs/web.log
