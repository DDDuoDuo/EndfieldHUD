#!/usr/bin/env python3
"""Run unchanged source Track/Lyrics methods with inert Foundation-only shims."""
from pathlib import Path
import hashlib,json,os,subprocess,sys
root=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else root/'build/now-playing-reference'
destination=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else None
authority='ca04f142185c7de40acd8523bdb563195d90a1d1'
paths=['Sources/NowPlayingController.swift','Sources/NowPlayingLyrics.swift','Sources/NowPlayingCanvas.swift','Sources/NowPlayingArtwork.swift']
texts={}
for path in paths:
    b=(root/path).read_bytes();assert b==subprocess.check_output(['git','show',authority+':'+path],cwd=root)
    texts[path]=b.decode()
def between(text,start,end):return text[text.index(start):text.index(end,text.index(start))]
controller=texts[paths[0]];canvas=texts[paths[2]]
source='import Foundation\n'+between(controller,'struct NowPlayingTrack:','\nenum NowPlayingFailure:')+'\n'+between(texts[paths[1]],'struct NowPlayingLyricLine:','\n/// At most two requests')
source+='\nstruct SourceSeek { struct PendingSeek { let value:Double; let sampledAt:Double }\n'+between(controller,'    private func acknowledges(','    private func armSeekRollback()')+'\nfunc test(_ track:NowPlayingTrack,_ value:Double,_ now:Double)->Bool { acknowledges(track,seek:PendingSeek(value:value,sampledAt:now)) }\n}\n'
source+='''
final class SourceCanvas {
    struct Snapshot { var track:NowPlayingTrack? }; struct Controller { var lyrics:NowPlayingLyrics? }
    final class Layer { func removeAllAnimations() {} }
    var active=true,lyricsVisible=true
    var draggedSlider:String?
    var snapshot=Snapshot(),controller=Controller(),progressFill=Layer(),progressHandle=Layer()
    var clockDeadline:Double?,cancelClock:(()->Void)?,clockGeneration=0,time=0.0
    func now()->Double { time }
    func stopClock(){clockDeadline=nil;cancelClock=nil;clockGeneration += 1}
    func tick(){}
    func scheduleDisplayUpdate(_ delay:Double,_ action:@escaping()->Void)->()->Void { {} }
    func test()->Double? { reconcileTimer();return clockDeadline }
'''+between(canvas,'    private func reconcileTimer()','    private func stopClock()')+between(canvas,'    static func time(_ value:','    private func withoutActions(')+'}\n'
source+=r'''
func number(_ value:Any?)->Double? { if let v=value as? NSNumber {return v.doubleValue};switch value as? String {case "nan":return .nan;case "inf":return .infinity;case "-inf":return -.infinity;default:return nil} }
func field(_ value:Double?)->Any {value.flatMap{$0.isFinite ? $0:nil}.map{$0 as Any} ?? NSNull()}
func track(_ j:[String:Any])->NowPlayingTrack {NowPlayingTrack(title:j["title"] as? String ?? "",artist:j["artist"] as? String ?? "",album:j["album"] as? String ?? "",duration:number(j["duration"]),position:number(j["position"]),isPlaying:j["playing"] as? Bool ?? false,sampledAt:number(j["sampledAt"]) ?? 0,identifier:j["identifier"] as? String,timedLyrics:j["lyrics"] as? String,artworkRevision:j["artworkRevision"] as? String,supportsSeeking:j["supportsSeeking"] as? Bool ?? true)}
let input=try JSONSerialization.jsonObject(with:Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[1]))) as! [String:Any]
var result=input
result["tracks"]=(input["tracks"] as! [[String:Any]]).map { row -> [String:Any] in
 var row=row;let t=track(row["input"] as! [String:Any]);row["expected"]=["title":t.title,"artist":t.artist,"album":t.album,"duration":field(t.duration),"position":field(t.position),"playing":t.isPlaying,"sampledAt":t.sampledAt,"identifier":t.identifier as Any? ?? NSNull(),"artworkRevision":t.artworkRevision as Any? ?? NSNull(),"elapsed":(row["times"] as! [Any]).map{field(t.elapsed(at:number($0)!))},"formatted":(row["times"] as! [Any]).map{SourceCanvas.time(number($0))}];return row
}
result["lyrics"]=(input["lyrics"] as! [[String:Any]]).map { row -> [String:Any] in
 var row=row;let l=NowPlayingLyrics(lrc:row["input"] as! String);row["expected"]=l.map{l in ["lines":l.lines.map{["time":$0.time,"text":$0.text] as [String:Any]},"windows":(row["times"] as! [Any]).map{l.window(at:number($0)!)},"boundaries":(row["times"] as! [Any]).map{field(l.nextBoundary(after:number($0)!))}]} as Any? ?? NSNull();return row
}
result["identities"]=(input["identities"] as! [[String:Any]]).map { row -> [String:Any] in var row=row;row["expected"]=track(row["a"] as! [String:Any]).hasSameIdentity(as:track(row["b"] as! [String:Any]));return row }
result["seek"]=(input["seek"] as! [[String:Any]]).map { row -> [String:Any] in var row=row;row["expected"]=SourceSeek().test(track(row["track"] as! [String:Any]),number(row["requested"])!,number(row["sampledAt"])!);return row }
result["deadlines"]=(input["deadlines"] as! [[String:Any]]).map { row -> [String:Any] in var row=row;let c=SourceCanvas();c.snapshot.track=track(row["track"] as! [String:Any]);c.controller.lyrics=(row["lyrics"] as? String).flatMap{NowPlayingLyrics(lrc:$0)};c.time=number(row["now"])!;c.active=row["active"] as? Bool ?? true;c.lyricsVisible=row["lyricsVisible"] as? Bool ?? true;c.draggedSlider=(row["seeking"] as? Bool ?? false) ? "seek":nil;row["expected"]=field(c.test());return row }
try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[2]))
'''
out.mkdir(parents=True,exist_ok=True);(out/'oracle.swift').write_text(source)
base={'title':'歌 이름','artist':'Artiste','album':'Album','duration':240,'position':12.25,'playing':True,'sampledAt':10}
times=[-1,0,10,10.5,100,1000000,'nan','inf']
tracks=[base,{**base,'duration':-1,'position':5,'sampledAt':'nan'}, {**base,'duration':'inf','position':-1}, {**base,'duration':1e9,'position':1e9}, {**base,'duration':12,'position':30,'playing':False}, {**base,'title':'👩🏽‍🚀'*514,'artist':'e\u0301'*513,'album':'한'*515,'identifier':'X'*513,'artworkRevision':'r'*258}]
lyrics=['[00:01.250] First\n[00:02] 二行\n[00:03]\n[00:04.5] 끝','[offset:-1500]\n[00:01] A\n[00:01] A\n[00:01] B\n[00:02] C','[01:02:03,5][00:02] Late\n[00:99] Invalid\n[123:59.999] Long','[00:01] A [٠٠:٠٢] End\n[٠١:00:03] Hours\n[00:04] Later','[offſet:+500]\n[00:01] e\u0301\n[00:01] é\n[00:02] Next','[00:01]  中文\u200b\r\n[00:02]한\u0085[00:03]三\u2028[00:04]Four',' [00:01] invalid','[00:01]\n[00:02] ', ''.join('[00:%02d]'%n for n in range(40))+' end','[00:01]'+'x'*8193+'\n[00:02] Valid']
identities=[{'a':base,'b':{**base,'position':150,'playing':False,'sampledAt':500}}, {'a':{**base,'identifier':'one'},'b':{**base,'identifier':'two'}},{'a':{**base,'identifier':'one'},'b':base},{'a':{**base,'title':'é'},'b':{**base,'title':'e\u0301'}},{'a':base,'b':{**base,'duration':241}}]
seek=[{'track':{**base,'position':p,'sampledAt':s},'requested':100,'sampledAt':10} for p,s in [(100,10),(101.25,10),(101.251,10),(103,100),(104.251,100)]]
cue='[00:12.255] Tiny\n[00:12.5] Next\n[00:13] End'
deadlines=[{'track':base,'now':10,'lyrics':cue},{'track':base,'now':10,'lyrics':cue,'seeking':True},{'track':base,'now':10,'lyrics':cue,'lyricsVisible':False},{'track':base,'now':10,'active':False},{'track':{**base,'playing':False},'now':10},{'track':{**base,'duration':12.26},'now':10},{'track':base,'now':1000}]
data={'schemaVersion':1,'sourceCommit':authority,'sourcePins':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},'tracks':[{'input':t,'times':times} for t in tracks],'lyrics':[{'input':s,'times':[-1,0,1,1.25,2,3,4.5,10000,'nan']} for s in lyrics],'identities':identities,'seek':seek,'deadlines':deadlines}
(out/'input.json').write_text(json.dumps(data,ensure_ascii=False))
env=os.environ.copy();env.pop('SDKROOT',None)
sdk=subprocess.check_output(['bash',str(root/'scripts/build.sh'),'--print-sdk'],text=True,env=env).strip()
subprocess.run(['/usr/bin/swiftc','-sdk',sdk,'-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(out/'oracle.swift'),'-o',str(out/'oracle')],check=True,env=env)
subprocess.run([str(out/'oracle'),str(out/'input.json'),str(out/'now-playing-source.json')],check=True)
value=(out/'now-playing-source.json').read_bytes()
if destination:destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(value)
print('Original Swift Now Playing fixture',len(value),'bytes SHA256',hashlib.sha256(value).hexdigest())
