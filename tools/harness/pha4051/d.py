import sys; sys.path.insert(0,'/tmp/z4051/t')
exec(open('/tmp/z4051/t/drv.py').read())
c=sys.argv[1]; code=sys.argv[2] if len(sys.argv)>2 else sys.stdin.read()
print(run(c,code))
