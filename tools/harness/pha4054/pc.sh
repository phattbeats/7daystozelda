#!/bin/bash
P=$([ "$1" = B ] && echo 29858 || echo 29857)
curl -s -m ${T:-600} --data-binary @- http://127.0.0.1:$P/
