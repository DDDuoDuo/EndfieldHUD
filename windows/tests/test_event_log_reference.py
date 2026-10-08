#!/usr/bin/env python3
"""Compare detached original EventLogCanvas against portable emitted descriptors.
Usage: test_event_log_reference.py ORIGINAL/reference.json PORTABLE.json
No app/window, event_log, account, filesystem payload, or OS service access.
"""
import json, math, re, sys
original, portable=(json.load(open(p,encoding='utf-8')) for p in sys.argv[1:])
checks=0

def check(value,message):
    global checks
    checks+=1
    assert value,message

def close(a,b,eps=1e-6):
    if isinstance(a,(int,float)) and isinstance(b,(int,float)): return abs(a-b)<=eps
    if isinstance(a,list) and isinstance(b,list): return len(a)==len(b) and all(close(x,y,eps) for x,y in zip(a,b))
    return a==b

def leaves(node,base=(0,0)):
    if node.get("hidden",False): return
    x,y,w,h=node.get('bounds',[0,0,0,0]);px,py=node.get('position',[0,0]);ax,ay=node.get('anchorPoint',[.5,.5])
    world=(base[0]+px-x-ax*w,base[1]+py-y-ay*h)
    if node.get('kind') in ('text','shape') or node.get('contents') or node.get('backgroundColor'):
        yield node,world
    for child in node.get('children',[]): yield from leaves(child,world)

def ink(color):
    if color is None:return None
    return color['sRGB']

def signature(node,world):
    b=node.get('bounds',[0,0,0,0]);rect=[world[0]+b[0],world[1]+b[1],b[2],b[3]]
    if node.get('kind')=='text':
        t=node['text'];return ('text',[rect,t['string'],t['fontSize'],t['alignment'],t['wrapped'],t['truncation'],ink(t['foregroundColor'])])
    if node.get('contents'):return ('image',rect)
    if node.get('backgroundColor'):return ('background',[rect,node.get('cornerRadius',0),ink(node['backgroundColor'])])
    return None

check(original['isolation']=={'eventDirectory':None,'detachedLayers':True,'liveServices':False,'windowCreated':False},'original isolation declaration')
check(len(original['cases'])==len(portable['cases'])==8,'eight independently generated original cases')
for expected,actual in zip(original['cases'],portable['cases']):
    name=expected['name'];check(name==actual['name'],f'{name}: case sequence')
    check(close(expected['scroll'],actual['scroll']),f'{name}: exact scroll');check(expected['count']==actual['count'],f'{name}: exact count')
    oa=expected['actions'];pa=actual['actions'];check(len(oa)==len(pa),f'{name}: action count')
    for a,b in zip(oa,pa):check(a['id']==b['id'] and a['label']==b['label'] and close(a['rect'],b['rect']),f'{name}: original action {a} != {b}')
    ol=list(leaves(expected['layer']))
    pl=[]
    for part in actual['parts']:
        origin=part['full'][:2] if part['rowID'] else (0,0)
        pl.extend(leaves(part['layer'],origin))
    os=[s for n,w in ol if (s:=signature(n,w))]
    ps=[s for n,w in pl if (s:=signature(n,w))]
    # The native adapter separates the scrollbar from heading/toolbar; verify
    # individual source leaves by exact content/geometry rather than list order.
    check(len(os)==len(ps),f'{name}: drawing leaf count {len(os)} != {len(ps)}')
    for kind,data in os:
        matches=[i for i,(k,p) in enumerate(ps) if k==kind and close(data,p)]
        check(bool(matches),f'{name}: missing original {kind} {data}')
        ps.pop(matches[0])
    # Compare every original source fill/stroke geometry independently. Native
    # plate fill and trace are separate retained surfaces but retain one path.
    def paths(items):
        out=[]
        for node,world in items:
            if node.get('kind')!='shape':continue
            s=node['shape'];commands=[]
            for command in s['path']:
                commands.append([command['op'],[[p[0]+world[0],p[1]+world[1]] for p in command['points']]])
            for field in ('fillColor','strokeColor'):
                if s.get(field):out.append([field,commands,ink(s[field]),s.get('lineWidth',1) if field=='strokeColor' else 0])
        return out
    op,pp=paths(ol),paths(pl)
    check(len(op)==len(pp),f'{name}: original shape component count')
    for path in op:
        matches=[i for i,p in enumerate(pp) if close(path,p,2e-5)]
        check(bool(matches),f'{name}: original path mismatch {path}')
        pp.pop(matches[0])
print(f'PASS {checks} original Event Log geometry/text/path/action checks')
