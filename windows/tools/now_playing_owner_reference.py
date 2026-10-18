#!/usr/bin/env python3
"""Execute unchanged original seek rollback and first-frame artwork decode."""
from pathlib import Path
import hashlib,json,os,struct,subprocess,zlib
root=Path(__file__).resolve().parents[2];out=root/'build/now-playing-owner-reference/source';out.mkdir(parents=True,exist_ok=True)
commit='ca04f142185c7de40acd8523bdb563195d90a1d1';paths=['Sources/NowPlayingController.swift','Sources/NowPlayingArtwork.swift']
texts={}
for p in paths:
 b=(root/p).read_bytes();assert b==subprocess.check_output(['git','show',commit+':'+p],cwd=root);texts[p]=b.decode()
def between(s,a,b):return s[s.index(a):s.index(b,s.index(a))]
c=texts[paths[0]];a=texts[paths[1]]
source='''import Foundation
import ImageIO
import CoreGraphics
final class SourceRollback {
 struct PendingSeek { let revision:Int }
 struct Snapshot { var track:Int?=90 }
 var active=true,optimisticSeek:PendingSeek?=PendingSeek(revision:1),snapshot=Snapshot(),observedTrack:Int?=10
 var cancelSeekRollback:(()->Void)?,scheduled:(()->Void)?,delay:Double?,changes=0,cancellations=0
 func scheduleSeekRollback(_ delay:Double,_ callback:@escaping()->Void)->()->Void { self.delay=delay;scheduled=callback;return {[weak self] in self?.cancellations += 1} }
 func changed(){changes += 1}
'''+between(c,'    private func clearOptimisticSeek()','    private func acknowledges(')+between(c,'    private func armSeekRollback()','    private func applyOptimisticSeek()')+'''
 func run(_ mode:String)->[String:Any] {armSeekRollback();if mode=="replaced" {optimisticSeek=PendingSeek(revision:2)};if mode=="hidden" {active=false};scheduled?();return ["mode":mode,"delay":delay as Any,"hasSeek":optimisticSeek != nil,"position":snapshot.track as Any,"changes":changes,"cancellations":cancellations]}
}
struct SourceArtwork {
 static let maximumBytes=8*1024*1024,maximumPixels=32_000_000,thumbnailSize=512
'''+between(a,'    static func decode(','    /// Known Spotify/')+'''}
let rows=try JSONSerialization.jsonObject(with:Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[1]))) as! [[String:Any]]
var images:[[String:Any]]=[]
for row in rows {let bytes=Data(base64Encoded:row["base64"] as! String)!;var result=row;if let image=SourceArtwork.decode(bytes) {result["width"]=image.width;result["height"]=image.height;var rgba=[UInt8](repeating:0,count:image.width*image.height*4);let color=CGColorSpace(name:CGColorSpace.sRGB)!;rgba.withUnsafeMutableBytes {storage in let context=CGContext(data:storage.baseAddress,width:image.width,height:image.height,bitsPerComponent:8,bytesPerRow:image.width*4,space:color,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue)!;context.draw(image,in:CGRect(x:0,y:0,width:image.width,height:image.height))};for n in stride(from:0,to:rgba.count,by:4){let alpha=Int(rgba[n+3]);for k in 0..<3 {rgba[n+k]=alpha>0 ? UInt8(min(255,(Int(rgba[n+k])*255+alpha/2)/alpha)):0}};result["rgba"]=rgba}else{result["width"]=0;result["height"]=0};images.append(result)}
let result:[String:Any] = ["rollback":["same","replaced","hidden"].map{mode in let owner=SourceRollback();return withExtendedLifetime(owner){owner.run(mode)}},"images":images]
try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[2]))
'''
def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b)&0xffffffff)
png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',2,1,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(bytes([0,255,0,0,128,0,255,0,255])))+chunk(b'IEND',b'')
# Uncompressed3x2 RGB TIFF, all eight EXIF orientations; no codec loss/filter.
def tiff(orientation):
 tags=[(256,4,1,3),(257,4,1,2),(258,3,3,0),(259,3,1,1),(262,3,1,2),(273,4,1,0),(274,3,1,orientation),(277,3,1,3),(278,4,1,2),(279,4,1,18)]
 end=8+2+len(tags)*12+4;tags[2]=(258,3,3,end);tags[5]=(273,4,1,end+6)
 return b'II'+struct.pack('<HIH',42,8,len(tags))+b''.join(struct.pack('<HHII',*v) for v in tags)+struct.pack('<I',0)+struct.pack('<HHH',8,8,8)+bytes([255,0,0,0,255,0,0,0,255,255,255,0,0,255,255,255,0,255])
# Source PNG with the same orientation-tagged TIFF metadata and original2x1 pixels.
rotatedPNG=png[:33]+chunk(b'eXIf',tiff(6))+png[33:]
import base64
rows=[{'name':name,'base64':base64.b64encode(b).decode(),'sha256':hashlib.sha256(b).hexdigest()} for name,b in [('straight-alpha-png',png),('oriented-png',rotatedPNG)]+[('oriented-tiff-'+str(n),tiff(n)) for n in range(1,9)]+[('malformed',b'not an image')]]
(out/'oracle.swift').write_text(source);(out/'input.json').write_text(json.dumps(rows));env=os.environ.copy();env.pop('SDKROOT',None)
sdk=subprocess.check_output(['bash',str(root/'scripts/build.sh'),'--print-sdk'],text=True,env=env).strip();subprocess.run(['/usr/bin/swiftc','-sdk',sdk,'-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(out/'oracle.swift'),'-o',str(out/'oracle')],check=True,env=env);subprocess.run([str(out/'oracle'),str(out/'input.json'),str(out/'output.json')],check=True)
result=json.loads((out/'output.json').read_text());
for row in result['images']: row['encodedHex']=base64.b64decode(row.pop('base64')).hex()
result.update(schemaVersion=1,sourceCommit=commit,sourcePins={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths});dest=root/'windows/tests/fixtures/now-playing-owner-source.json';dest.write_text(json.dumps(result,ensure_ascii=False,separators=(',',':'))+'\n');print(dest,len(dest.read_bytes()),hashlib.sha256(dest.read_bytes()).hexdigest())
