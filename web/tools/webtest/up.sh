#!/bin/bash
sed -i 's/proxy_read_timeout 240;/proxy_read_timeout 10;/; s/proxy_send_timeout 240;/proxy_send_timeout 10;/' ${WEBTEST_DIR:-$HOME/webtest-work}/config/nginx/proxy.conf
cd /root/anchor-server && nohup ./anchor > ${WEBTEST_DIR:-$HOME/webtest-work}/logs/anchor.log 2>&1 &
cd /root/web-bundle && ANCHOR_HOST=127.0.0.1 PING_MS=3000 ACCESS_KEY=testkey PORT=8080 nohup node server.js > ${WEBTEST_DIR:-$HOME/webtest-work}/logs/web.log 2>&1 &
nginx -c ${WEBTEST_DIR:-$HOME/webtest-work}/nginx.conf
