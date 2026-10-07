import sys; sys.path.insert(0,'/tmp/z4053/t')
exec(open('/tmp/z4053/t/drv.py').read())
tag=sys.argv[1]
for c in 'AB': run(c,"shot('/tmp/z4053/t/evid/%s-%s.png')"%(tag,c))
