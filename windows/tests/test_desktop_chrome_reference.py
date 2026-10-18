#!/usr/bin/env python3
import argparse,hashlib,json,pathlib,struct

def check(root,source_root):
    root=pathlib.Path(root);data=json.loads((root/'chrome.json').read_text())
    assert data['schemaVersion']==1 and len(data['styles'])==20 and len(data['layouts'])==216 and len(data['projections'])==6
    assert data['safety']=={'systemHUDViewConstructed':False,'windowCreated':False,'OSClockTimersCreated':0,'liveProviders':False,'cursorSets':0}
    assert data['clockTransition']['type']=='push' and data['clockTransition']['subtype']=='fromRight'
    assert data['clockTransition']['duration']==.26 and data['clockTransition']['pageClip']==[14,12,312,75]
    assert set(data['bindings'])=={'centerNodeID','statusNodeID'}
    header=data['header']
    assert header['frame']==[270,2,600,74]
    assert [(x['text']['string'],x['frame'],x['text']['fontSize']) for x in header['children']]==[
        ('ENDFIELDHUD',[0,0,300,27],20),('SYSTEM INTERFACE',[0,31,300,18],9)]
    footer=data['footer']['children'][0]
    assert footer['text']['string'].startswith('ESC / ') and footer['text']['string'].endswith(' / CLICK OUTSIDE TO CLOSE')
    assert footer['text']['fontSize']==9 and footer['text']['alignment']=='center'
    for state in data['styles']:
        png=(root/state['png']).read_bytes();assert png.startswith(b'\x89PNG\r\n\x1a\n') and struct.unpack('>II',png[16:24])==(680,290)
        assert state['time']['text']['fontSize'] in (25,32,42)
        assert state['time']['contentsAreFlipped'] and state['date']['contentsAreFlipped']
        label={'running':'WORK MODE / ACTIVE','paused':'WORK MODE / PAUSED','idle':''}[state['workPhase']]
        assert state['badge']['text']['string']==label
    assert json.loads((root/'instrumentation.json').read_text())['SystemHUDViewConstructed'] is False
    if source_root:
        for group in ('sourceAndResourceSHA256','exporterSHA256'):
            for name,digest in json.loads((root/'provenance.json').read_text())[group].items():
                assert hashlib.sha256((pathlib.Path(source_root)/name).read_bytes()).hexdigest()==digest,name
    print('PASS original source chrome: 20 clock states, 6 projections, 216 layouts; no SystemHUDView/live providers')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root');p.add_argument('--source-root');a=p.parse_args();check(a.root,a.source_root)
