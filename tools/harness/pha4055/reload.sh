#!/bin/bash
# after ./install.sh: reload both clients (auto-joins from saved settings), click canvas for focus. Then run load.py via pc.sh to load file 1 (x, x ...) if needed.
for c in A B; do /tmp/z4055/t/pc.sh $c <<< "page.reload(); time.sleep(35); page.mouse.click(480,270)" & done; wait
