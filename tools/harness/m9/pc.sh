#!/bin/bash
curl -s -m ${T:-600} --data-binary @- http://127.0.0.1:${PORT:-19700}/
