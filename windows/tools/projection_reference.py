#!/usr/bin/env python3
"""Evaluate unchanged Mac Projection geometry in an isolated AppKit executable."""
from pathlib import Path
import hashlib, json, os, subprocess, sys
root=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]) if len(sys.argv)>1 else root/'build/projection-reference'
out.mkdir(parents=True,exist_ok=True)
paths=['Sources/ProjectionModel.swift','Sources/ProjectionControls.swift']
sources={p:(root/p).read_text() for p in paths}
def method(s,name):
 start=s.index(name);brace=s.index('{',start);depth=1;i=brace+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
constrain=method(sources[paths[0]],'static func constrain(')
inset=method(sources[paths[1]],'static func topInset(')
bounds=[[0,0,1920,1080],[20,30,600,400],[0,0,90,80],[100,100,-50,70],[-12,10,0,10]]
frames=[[x,40,w,h] for x in [-1000,-1,0,45,500,2200] for w,h in [[-100,50],[0,0],[1,1],[110,100],[480,360],[3000,2000]]]
frames += [["nan",30,240,180],[20,"nan",240,180],[20,30,"nan",180],[20,30,240,"nan"],["inf",30,240,180]]
inputs={'constrain':[{'frame':f,'bounds':b} for b in bounds for f in frames], 'insets':[[a,b] for a in [-10,0,24,64,120,'nan','inf'] for b in [-1,0,23,44,144,'nan']]}
(out/'input.json').write_text(json.dumps(inputs))
program='''import AppKit
final class OriginalModel {\n'''+constrain+'\n}\nfinal class OriginalToolbar {\n'+inset+'''\n}
func number(_ v: Any) -> CGFloat { if let n=v as? NSNumber{return CGFloat(n.doubleValue)}; switch v as! String {case "nan":return .nan;case "inf":return .infinity;default:fatalError("input")} }
func rect(_ a:[Any])->CGRect{CGRect(x:number(a[0]),y:number(a[1]),width:number(a[2]),height:number(a[3]))}
let input=try JSONSerialization.jsonObject(with:Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[1]))) as! [String:Any]
var rows:[[String:Any]]=[]
for r in input["constrain"] as! [[String:Any]] {let next=OriginalModel.constrain(rect(r["frame"] as! [Any]),to:rect(r["bounds"] as! [Any]));var value=r;value["expected"]=[next.origin.x,next.origin.y,next.width,next.height];rows.append(value)}
var insets:[[String:Any]]=[]
for r in input["insets"] as! [[Any]] {insets.append(["input":r,"expected":OriginalToolbar.topInset(safeAreaTop:number(r[0]),visibleTop:number(r[1]))])}
let result:[String:Any]=["constrain":rows,"insets":insets]
try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[2]))
'''
(out/'oracle.swift').write_text(program)
# Apple's Python launcher can inject its own (newer) SDKROOT. Let the
# repository choose the SDK that matches its selected Swift compiler.
selection_env=os.environ.copy();selection_env.pop('SDKROOT',None)
sdk=subprocess.check_output(['bash',str(root/'scripts/build.sh'),'--print-sdk'],text=True,env=selection_env).strip()
subprocess.run(['xcrun','swiftc','-sdk',sdk,'-module-cache-path',str(out/'module-cache'),str(out/'oracle.swift'),'-o',str(out/'oracle')],check=True)
subprocess.run([str(out/'oracle'),str(out/'input.json'),str(out/'output.json')],check=True)
data=json.loads((out/'output.json').read_text());data['authority']='ca04f142185c7de40acd8523bdb563195d90a1d1'
data['sourceSHA256']={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths}
data['scope']='Exact original constrain/topInset methods; no media decoding, UI, input or user data.'
(root/'windows/tests/fixtures/projection-model-source.json').write_text(json.dumps(data,ensure_ascii=False,separators=(',',':'))+'\n')
print('Original AppKit Projection geometry:',len(data['constrain']), 'frames,',len(data['insets']),'insets')
