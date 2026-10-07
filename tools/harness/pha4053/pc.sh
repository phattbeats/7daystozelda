#!/bin/bash
P=$([ "$1" = B ] && echo 19863 || echo 19853)
curl -s -m ${T:-600} --data-binary @- http://127.0.0.1:$P/
