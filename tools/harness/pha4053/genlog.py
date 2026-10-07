import os,re,sys
# usage: genlog.py <build-dir>   -- rewrites .ninja_log (v5) from build.ninja + rules.ninja, mtimes from disk
bd=sys.argv[1]; os.chdir(bd)
M=(1<<64)-1
def murmur64a(data,seed=0xDECAFBADDECAFBAD):
    m=0xc6a4a7935bd1e995; r=47; n=len(data)
    h=(seed^((n*m)&M))&M
    nb=n//8
    for i in range(nb):
        k=int.from_bytes(data[i*8:i*8+8],'little')
        k=(k*m)&M; k^=k>>r; k=(k*m)&M
        h^=k; h=(h*m)&M
    tail=data[nb*8:]
    if tail:
        h^=int.from_bytes(tail,'little'); h=(h*m)&M
    h^=h>>r; h=(h*m)&M; h^=h>>r
    return h
def logical_lines(path):
    out=[];cur=''
    for ln in open(path,encoding='utf-8',errors='surrogateescape').read().split('\n'):
        if ln.endswith('$') and not ln.endswith('$$'):
            cur+=ln[:-1].lstrip() if cur else ln[:-1]; continue
        out.append(cur+ln); cur=''
    return out
gvars={}; rules={}; edges=[]
def parse(path):
    lines=logical_lines(path); i=0
    while i<len(lines):
        ln=lines[i]
        if not ln.strip() or ln.lstrip().startswith('#'): i+=1; continue
        if ln.startswith('include '): parse(ln.split(None,1)[1].strip()); i+=1; continue
        if ln.startswith('rule '):
            name=ln.split()[1]; v={}; i+=1
            while i<len(lines) and lines[i].startswith(' '):
                k,_,val=lines[i].strip().partition(' = '); v[k.strip()]=val; i+=1
            rules[name]=v; continue
        if ln.startswith('build '):
            body=ln[6:]
            lhs,_,rhs=body.partition(': ') if ': ' in body else body.partition(':')
            def split(s): return [x for x in re.split(r'(?<!\$) +',s.strip()) if x]
            outs=lhs.split(' | ')[0]; imp=lhs.split(' | ')[1] if ' | ' in lhs else ''
            r=rhs.strip().split(None,1); rule=r[0]; rest=r[1] if len(r)>1 else ''
            ins=rest.split(' | ')[0].split(' || ')[0]
            v={}; i+=1
            while i<len(lines) and lines[i].startswith(' '):
                k,_,val=lines[i].strip().partition(' = '); v[k.strip()]=val; i+=1
            edges.append((split(outs),split(imp),rule,split(ins),v)); continue
        if ln.startswith(('pool ','default ','ninja_required')) or ln.split()[0] in('pool','default'):
            i+=1
            while i<len(lines) and lines[i].startswith(' '): i+=1
            continue
        if ' = ' in ln:
            k,_,val=ln.partition(' = '); gvars[k.strip()]=val; i+=1; continue
        i+=1
parse('build.ninja')
def unesc(s): return s.replace('$ ',' ').replace('$:',':')
def expand(s,env,rv,depth=0):
    def rep(m):
        t=m.group(0)
        if t=='$$': return '$'
        name=m.group(1) or m.group(2)
        if name in env: return expand(env[name],env,rv,depth+1)
        if name in rv: return expand(rv[name],env,rv,depth+1)
        if name in gvars: return expand(gvars[name],env,rv,depth+1)
        return ''
    return re.sub(r'\$\$|\$\{([A-Za-z0-9_.-]+)\}|\$([A-Za-z0-9_-]+)',rep,s)
n=0
with open('.ninja_log.new','w') as f:
    f.write('# ninja log v5\n')
    for outs,imp,rule,ins,v in edges:
        if rule=='phony' or rule not in rules: continue
        rv=rules[rule]
        env=dict(v)
        env['in']=' '.join(unesc(x) for x in ins); env['out']=' '.join(unesc(x) for x in outs)
        env['in_newline']='\n'.join(unesc(x) for x in ins)
        cmd=expand(rv.get('command',''),env,rv)
        rsp=expand(rv.get('rspfile_content',''),env,rv) if rv.get('rspfile_content') else ''
        if rsp: cmd=cmd+';rspfile='+rsp
        h=murmur64a(cmd.encode('utf-8','surrogateescape'))
        for o in outs+imp:
            o=unesc(o)
            try: mt=os.stat(o).st_mtime_ns
            except OSError: continue
            f.write('0\t1\t%d\t%s\t%x\n'%(mt,o,h)); n+=1
os.replace('.ninja_log.new','.ninja_log')
print('entries',n)
