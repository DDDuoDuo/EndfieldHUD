#!/usr/bin/env python3
"""Compact unchanged Mac Calendar metadata; no runtime assets or expected algorithm."""
import hashlib,json,pathlib,sys

def compact(node):
    b,p,a=node['bounds'],node['position'],node['anchorPoint']
    out={k:node[k] for k in ('kind','bounds','opacity','hidden','allowsGroupOpacity','masksToBounds','cornerRadius','borderWidth')}
    out.update(position=[p[0]-a[0]*b[2],p[1]-a[1]*b[3]],anchorPoint=[0,0],children=[compact(c) for c in node['children']])
    for key in ('name','backgroundColor','borderColor'):
        if node.get(key) is not None:out[key]=node[key] if key=='name' else {k:node[key][k] for k in ('sourceComponents','sourceColorSpaceModel','sRGB')}
    if node.get('text'):
        t=node['text'];out['text']={k:t[k] for k in ('string','fontSize','alignment','wrapped','truncation')}
        out['text']['foregroundColor']={k:t['foregroundColor'][k] for k in ('sourceComponents','sourceColorSpaceModel','sRGB')}
        out['text']['font']={k:t['font'][k] for k in ('familyName','postScriptName','pointSize')}
    if node.get('shape'):
        s=node['shape'];out['shape']={k:s[k] for k in ('path','lineWidth','lineCap','lineJoin','fillRule')}
        for key in ('fillColor','strokeColor'):
            if s[key] is not None:out['shape'][key]={k:s[key][k] for k in ('sourceComponents','sourceColorSpaceModel','sRGB')}
    return out

def find_refresh(n):
    if n.get('name')=='calendar.refresh.arrow':return n
    for c in n['children']:
        found=find_refresh(c)
        if found:return found
    return None

def main():
    if len(sys.argv)!=3:raise SystemExit('Usage: prepare_calendar_reference.py source-oracle-directory NEW-output-directory')
    source,target=map(pathlib.Path,sys.argv[1:]);target.mkdir(parents=True,exist_ok=True)
    if (target/'calendar-source.json').exists() or (target/'calendar_refresh_path.inc').exists():raise SystemExit('Refusing to overwrite prepared reference')
    raw=(source/'calendar-reference.json').read_bytes()
    if len(raw)>4*1024*1024:raise SystemExit('Reference manifest exceeds bound')
    root=json.loads(raw);provenance=json.loads((source/'provenance.json').read_text())
    if provenance['sourceAuthority']!='ca04f142185c7de40acd8523bdb563195d90a1d1':raise SystemExit('Unexpected Mac source authority')
    if root['rasterAssets'] or root['windowCreated'] or root['nativeNotificationServiceCreated']:raise SystemExit('Calendar reference isolation or raster contract changed')
    root['cases']=[]
    for entry in root.pop('entries'):
        name=entry['file']
        if '/' in name or '\\' in name or not name.endswith('.json'):raise SystemExit('Invalid Calendar reference path')
        data=(source/name).read_bytes()
        if len(data)>2*1024*1024:raise SystemExit('Reference case exceeds bound')
        row=json.loads(data);row['root']=compact(row['root']);root['cases'].append(row)
    arrow=find_refresh(root['cases'][0]['root'])
    if arrow is None:raise SystemExit('Original refresh path is missing')
    path=json.dumps(arrow['shape']['path'],separators=(',',':'))
    inc='// Exact CGPath produced by unchanged Sources/HUDCalendarCanvas.swift.\n// Build-only calendar_reference.sh provenance pins the source and serializer.\nconstexpr std::string_view calendarRefreshPath=R"calendar('+path+')calendar";\n'
    (target/'calendar_refresh_path.inc').write_text(inc)
    root.pop('sourceMetadataLimitations');root['provenance']=provenance
    root['provenance']['generatedPathSHA256']=hashlib.sha256(inc.encode()).hexdigest()
    data=json.dumps(root,separators=(',',':'),ensure_ascii=False)+'\n'
    if len(data.encode())>4*1024*1024:raise SystemExit('Prepared source fixture exceeds bound')
    (target/'calendar-source.json').write_text(data)
    print('PASS prepared Calendar source metadata:',len(root['cases']),'cases,',len(data.encode()),'bytes')
if __name__=='__main__':main()
