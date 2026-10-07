#!/bin/bash
# restart.sh <serve-dir-name> [clients] : fresh rig on that build, 94-piece fort built by A
cd /tmp/z4062/t
python3 down.py >/dev/null 2>&1; sleep 2
rm -f serve; ln -s ../$1 serve
for p in A B C D E; do rm -rf "prof-$p/Default/Cache" "prof-$p/Default/Code Cache" "prof-$p/Default/GPUCache" 2>/dev/null; done
rm -f playd-?.log
bash up.sh >up.out 2>&1; sleep 20
boot(){ python3 x.py $1 "NAME='$2'; TUNIC='$3'
exec(open('/tmp/z4062/t/boot2.py').read())" > boot-$1.out 2>&1; }
boot A Kai 3CB043
boot B Jules D33A2C & boot C Rin 2F6FDB & boot D Moss F2C14E & wait
tail -qn1 boot-?.out
