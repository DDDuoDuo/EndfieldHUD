#!/usr/bin/env python3
"""Extract unmodified battery layer bodies without constructing the live HUD."""
import hashlib,json,pathlib,sys
from desktop_chrome_reference_instrument import block,between
if len(sys.argv)==3 and sys.argv[1]=='--compact':
    out=pathlib.Path(sys.argv[2])
    reference=json.loads((out/'reference.json').read_text())
    provenance=json.loads((out/'provenance.json').read_text())
    assert not reference['liveServices']
    # The serializer reports the original highlight subclass by name. Its two
    # concrete child paths are exported and checked individually by the oracle.
    assert all(issue=={'category':'unverified-layer-subclass',
                       'feature':'layer subclass HUDControlHighlightLayer',
                       'node':'battery/7'} for issue in reference['unsupported'])
    nodes,identities=[],{}
    def intern(layer):
        value=dict(layer)
        value['children']=[intern(child) for child in value.get('children',[])]
        key=json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=False)
        if key not in identities:
            identities[key]=len(nodes)
            nodes.append(value)
        return identities[key]
    cases=[]
    for original in reference['cases']:
        item={key:value for key,value in original.items() if key!='layer'}
        item['root']=intern(original['layer'])
        cases.append(item)
    fixture={'sourceSHA256':provenance['sources'],'nodes':nodes,'cases':cases,
             'scope':'Detached unmodified source battery layer bodies. No live HUD, window, battery provider or user data.'}
    (out/'battery-source.json').write_text(json.dumps(fixture,separators=(',',':'),ensure_ascii=False)+'\n')
    print(f'Exported {len(cases)} battery cases, {len(nodes)} unique nodes')
    sys.exit(0)
root,out=map(pathlib.Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
source=(root/'Sources/SystemHUDView.swift').read_text()
assert hashlib.sha256(source.encode()).hexdigest()=='b2ab7090ad0a44c6bc4acbcaf6169b21983b138d868ed6135e4114791725d42f'
build=between(source,'        core.frame = CGRect(x: 300, y: 152','    private func buildPanels()').rsplit('    }',1)[0]
build+=between(source,'        capacityLabel = text("", rect: CGRect(x: 0, y: 264','        powerSettingsButton.target =')
appearance=between(source,'            let foreground = dark ? NSColor(white: 0.94','            let selectedColor =')
appearance+=between(source,'            for shape in self.foregroundShapes','            for shape in self.structuralShapes')
appearance+=between(source,'            for label in self.primaryTexts','            for label in self.accentTexts')
appearance+=between(source,'            let tone: NSColor','            self.currentDark = dark')
appearance+=between(source,'            self.percent.string =','            self.updateWorkPresentation(force: true)')
fixture='''import AppKit
import QuartzCore
final class BatteryReferenceFixture {
 let core=CALayer(),laptop=CAShapeLayer()
 var batteryHeading=CATextLayer(),percent=CATextLayer(),centerState=CATextLayer(),centerSource=CATextLayer(),capacityLabel=CATextLayer(),healthLabel=CATextLayer(),powerSettingsLabel=CATextLayer()
 var primaryTexts:[CATextLayer]=[],mutedTexts:[CATextLayer]=[],accentTexts:[CATextLayer]=[]
 var foregroundShapes:[CAShapeLayer]=[]
 var snapshot=BatterySnapshot.unavailable
 static let powerSettingsRect=CGRect(x:111,y:314,width:178,height:20)
 init(){
'''+build+'''
 }
 func apply(_ dark:Bool){
'''+appearance+'''
 }
'''+block(source,'private enum TextRole').replace('private ','',1)+'\n'+block(source,'private func text(').replace('private ','',1)+'\n}\n'
(out/'BatteryReferenceFixture.swift').write_text(fixture)
pins={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in ['Sources/SystemHUDView.swift','Sources/BatteryMonitor.swift','Sources/BatteryCapacity.swift','Sources/HUDControlHighlightLayer.swift']}
(out/'provenance.json').write_text(json.dumps({'sources':pins,'sourceModified':False,'liveHUDConstructed':False,'extractedBodies':[build,appearance]},indent=2)+'\n')
