#!/usr/bin/env python3
"""Compact exact source icon/caption primitives; never produce combination presets."""
import argparse,hashlib,json,pathlib,shutil
p=argparse.ArgumentParser();p.add_argument('source',type=pathlib.Path);p.add_argument('output',type=pathlib.Path);a=p.parse_args()
manifest=(a.source/'watch-content.json').read_bytes();j=json.loads(manifest)
assert j['schemaVersion']==1 and j['desktopMode'] and not j['unsupported']
def tree(key):
 r=j['contentTrees'][key];b=(a.source/r['file']).read_bytes();assert len(b)==r['bytes']and hashlib.sha256(b).hexdigest()==r['sha256'];return json.loads(b)
icons={}
for v in j['variants']:
 key='module:'+v['target'];t=tree(v['icon'])
 if key in icons:assert icons[key]['tree']==t,'Source icon differs across its recorded appearance variants'
 else:icons[key]={'tree':t,'reportOnly':v['target']=='activityMonitor'}
for v in j['shortcutIcons']:
 key='shortcut:'+v['preset'];t=tree(v['icon'])
 if key in icons:assert icons[key]['tree']==t,'Shortcut icon changes by source slot'
 else:icons[key]={'tree':t,'reportOnly':False}
caption=tree(j['variants'][0]['caption']);assets={}
def collect(v):
 if isinstance(v,dict):
  if 'asset'in v and 'sha256'in v:
   raw=(a.source/v['asset']).read_bytes();assert hashlib.sha256(raw).hexdigest()==v['sha256'];assets[v['asset']]=raw
  for x in v.values():collect(x)
 elif isinstance(v,list):
  for x in v:collect(x)
for item in icons.values():collect(item['tree'])
a.output.mkdir(parents=True,exist_ok=True)
for name,raw in assets.items():
 target=a.output/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
out={'schemaVersion':1,'sourceManifestSHA256':hashlib.sha256(manifest).hexdigest(),'captionTemplate':caption,'icons':icons,'limitations':['Report activity-monitor bitmap is valid only in the authored Report slot. Custom original application artwork is caller supplied. Runtime strings/colors/fonts are evaluated; this asset does not contain finite language/theme/slot presets.']}
b=json.dumps(out,ensure_ascii=False,sort_keys=True,separators=(',',':')).encode();(a.output/'manifest.json').write_bytes(b)
print(f'{len(icons)} original icon templates, {len(assets)} raster assets, {len(b)} manifest bytes, sha256={hashlib.sha256(b).hexdigest()}')
