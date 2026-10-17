#!/usr/bin/env python3
"""Execute unchanged source activate/deactivate with inert owned dependencies."""
from pathlib import Path
import hashlib,json,os,subprocess
root=Path(__file__).resolve().parents[2];out=root/'build/now-playing-warm-reference/source';out.mkdir(parents=True,exist_ok=True);commit='ca04f142185c7de40acd8523bdb563195d90a1d1';path='Sources/NowPlayingController.swift';data=(root/path).read_bytes();assert data==subprocess.check_output(['git','show',commit+':'+path],cwd=root);text=data.decode();methods=text[text.index('    func activate() {'):text.index('    deinit { token?.cancel();',text.index('    func activate() {'))]
source='''import Foundation
enum Failure {case timedOut,unavailable,other}
struct NowPlayingSnapshot {var application:Int?,track:Double?,failure:Failure?;var busy=false}
final class Inert {func cancel(){};func clear(){};func subscribeToChanges(_ callback:@escaping()->Void)->()->Void { {} }}
final class SourceWarm {
 var active=true,generation=0,hasFreshMetadataForPresentation=true,isRequestingPermission=false,retryArtworkAfterRefresh=false,pendingRefresh=false
 var snapshot=NowPlayingSnapshot(application:7,track:90,failure:nil,busy:true),warmSnapshot:NowPlayingSnapshot?,observedTrack:Double?=12
 var playerVolume:Double?=0.5,warmVolume:Double?,pendingVolume:Int?,pendingCommand:Int?,preferred:Int?
 var optimisticSeek:Int?=1,cancellation:(()->Void)?,backendCancellation:(()->Void)?
 var token:Inert?=Inert(),backend=Inert(),artworkLoader=Inert(),lyricsLoader=Inert(),catalog:Inert?=Inert()
 var alive=true,changes=0,refreshes=0
 func applications()->[Int] {alive ? [7]:[]}
 func isApplicationAlive(_ app:Int,running:[Int])->Bool {running.contains(app)}
 func subscribe(_ callback:@escaping(Int?)->Void)->()->Void { {} }
 func clearOptimisticSeek(){optimisticSeek=nil}
 func changed(){changes += 1}
 func refresh(){refreshes += 1}
'''+methods+'''
 func run(_ running:Bool)->[String:Any] {alive=running;deactivate();let hiddenTrack=snapshot.track as Any? ?? NSNull();let hiddenActive=active;activate();return ["alive":running,"hiddenTrack":hiddenTrack,"hiddenActive":hiddenActive,"visibleTrack":snapshot.track as Any? ?? NSNull(),"fresh":hasFreshMetadataForPresentation,"warmTrack":warmSnapshot?.track as Any? ?? NSNull(),"volume":playerVolume as Any? ?? NSNull(),"changed":changes,"refreshes":refreshes,"optimisticSeek":optimisticSeek as Any? ?? NSNull()]}
}
let owners=[SourceWarm(),SourceWarm()]
let rows=owners.enumerated().map{index,owner in owner.run(index==0)}
try JSONSerialization.data(withJSONObject:rows,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[1]))
'''
(out/'oracle.swift').write_text(source);env=os.environ.copy();env.pop('SDKROOT',None);sdk=subprocess.check_output(['bash',str(root/'scripts/build.sh'),'--print-sdk'],text=True,env=env).strip();subprocess.run(['/usr/bin/swiftc','-sdk',sdk,'-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(out/'oracle.swift'),'-o',str(out/'oracle')],check=True,env=env)
for name in ['output','repeat']:subprocess.run([str(out/'oracle'),str(out/(name+'.json'))],check=True)
assert (out/'output.json').read_bytes()==(out/'repeat.json').read_bytes();j={'schemaVersion':1,'sourceCommit':commit,'sourcePins':{path:hashlib.sha256(data).hexdigest()},'rows':json.loads((out/'output.json').read_text())};dest=root/'windows/tests/fixtures/now-playing-warm-source.json';dest.write_text(json.dumps(j,separators=(',',':'))+'\n');print(dest,len(dest.read_bytes()),hashlib.sha256(dest.read_bytes()).hexdigest())
