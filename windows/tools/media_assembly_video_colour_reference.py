#!/usr/bin/env python3
"""How the unchanged original Media Assembly reads movie frame colour.

Writes windows/tests/fixtures/media-assembly-video-colour-source.json. The
oracle writes owned synthetic H.264 movies with AVAssetWriter (a 128x128
grey chart: 8x8 patches holding every code 0...255, BT.709 colour tags or
none), then reads the first frame four ways with public API only:

  raw       AVAssetReaderTrackOutput, 32BGRA: the decoded values themselves
  preview   the pinned MediaAssemblyEngine.preview movie branch:
            AVAssetImageGenerator (preferred transform, 1024 box, 0.08 s
            tolerances) -> CIImage(cgImage:) -> Engine.apply (identity) ->
            CIContext(working sRGB).createCGImage(.RGBA8, sRGB); also
            rendered as unclamped RGBAf
  composed  AVAssetReaderVideoCompositionOutput with the pinned
            MediaAssemblyEngine.composition and MediaAssemblyVideoCompositor
            (the frames AVPlayer shows and the export writes), identity and
            exposure +1

Each patch records the mean of its inner 4x4 pixels (chroma edges excluded).
The records are the black-box evidence for the Windows movie colour reading
(modules/media_assembly_processor.hpp, mediaAssemblyVideoColour).
"""
from pathlib import Path
import base64, importlib.util, json, subprocess, sys, tempfile

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('media_assembly_processor_reference', here / 'media_assembly_processor_reference.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)

SIZE, PATCH = 128, 8
MOVIES = [('bt709-tagged', True), ('untagged', False)]

DRIVER = r'''
import AVFoundation
extension Original {
@@OUTPUTSIZE@@
@@COMPOSITION@@
}
@@CLASSES@@
let size=@@SIZE@@,patch=@@PATCH@@
func write(_ url:URL,_ tagged:Bool)throws{
 try? FileManager.default.removeItem(at:url)
 let writer=try AVAssetWriter(outputURL:url,fileType:.mp4)
 var settings:[String:Any]=[AVVideoCodecKey:AVVideoCodecType.h264,AVVideoWidthKey:size,AVVideoHeightKey:size,AVVideoCompressionPropertiesKey:[AVVideoAverageBitRateKey:8_000_000]]
 if tagged{settings[AVVideoColorPropertiesKey]=[AVVideoColorPrimariesKey:AVVideoColorPrimaries_ITU_R_709_2,AVVideoTransferFunctionKey:AVVideoTransferFunction_ITU_R_709_2,AVVideoYCbCrMatrixKey:AVVideoYCbCrMatrix_ITU_R_709_2]}
 let input=AVAssetWriterInput(mediaType:.video,outputSettings:settings)
 let adaptor=AVAssetWriterInputPixelBufferAdaptor(assetWriterInput:input,sourcePixelBufferAttributes:[kCVPixelBufferPixelFormatTypeKey as String:kCVPixelFormatType_32BGRA,kCVPixelBufferWidthKey as String:size,kCVPixelBufferHeightKey as String:size])
 writer.add(input);writer.startWriting();writer.startSession(atSourceTime:.zero)
 for f in 0..<3{
  while !input.isReadyForMoreMediaData{usleep(1000)}
  var pb:CVPixelBuffer?;CVPixelBufferPoolCreatePixelBuffer(nil,adaptor.pixelBufferPool!,&pb)
  let b=pb!;CVPixelBufferLockBaseAddress(b,[]);let base=CVPixelBufferGetBaseAddress(b)!.assumingMemoryBound(to:UInt8.self);let stride=CVPixelBufferGetBytesPerRow(b)
  for y in 0..<size{for x in 0..<size{let p=base+y*stride+x*4;let v=UInt8((y/patch)*(size/patch)+x/patch);p[0]=v;p[1]=v;p[2]=v;p[3]=255}}
  CVPixelBufferUnlockBaseAddress(b,[])
  adaptor.append(b,withPresentationTime:CMTime(value:CMTimeValue(f),timescale:10))
 }
 input.markAsFinished();let sem=DispatchSemaphore(value:0);writer.finishWriting{sem.signal()};sem.wait()
 precondition(writer.status == .completed)
}
func patches(_ value:(Int,Int)->Double)->[Double]{
 var out:[Double]=[]
 for k in 0..<256{let px=(k%(size/patch))*patch,py=(k/(size/patch))*patch;var sum=0.0;for y in py+2..<py+6{for x in px+2..<px+6{sum+=value(x,y)}};out.append(sum/16)}
 return out
}
func first(_ output:AVAssetReaderOutput,_ reader:AVAssetReader)->([Double],[String]){
 reader.add(output);reader.startReading();let s=output.copyNextSampleBuffer()!;let ib=CMSampleBufferGetImageBuffer(s)!
 CVPixelBufferLockBaseAddress(ib,.readOnly);let base=CVPixelBufferGetBaseAddress(ib)!.assumingMemoryBound(to:UInt8.self);let stride=CVPixelBufferGetBytesPerRow(ib)
 let red=patches{x,y in Double(base[y*stride+x*4+2])};CVPixelBufferUnlockBaseAddress(ib,.readOnly)
 let keys=(CVBufferCopyAttachments(ib,.shouldPropagate) as? [String:Any] ?? [:]).keys.filter{$0.hasPrefix("CVImageBuffer")}.sorted()
 reader.cancelReading();return (red,keys)
}
var movies:[[String:Any]]=[]
for (index,file) in CommandLine.arguments.dropFirst(2).enumerated(){
 let url=URL(fileURLWithPath:file);try write(url,index==0)
 let asset=AVURLAsset(url:url);let track=asset.tracks(withMediaType:.video)[0]
 let raw=first(AVAssetReaderTrackOutput(track:track,outputSettings:[kCVPixelBufferPixelFormatTypeKey as String:kCVPixelFormatType_32BGRA]),try AVAssetReader(asset:asset))
 let generator=AVAssetImageGenerator(asset:asset);generator.appliesPreferredTrackTransform=true;generator.maximumSize=CGSize(width:1024,height:1024)
 generator.requestedTimeToleranceBefore=CMTime(seconds:0.08,preferredTimescale:600);generator.requestedTimeToleranceAfter=CMTime(seconds:0.08,preferredTimescale:600)
 let cg=try generator.copyCGImage(at:.zero,actualTime:nil)
 let color=CGColorSpace(name:CGColorSpace.sRGB)!
 let context=CIContext(options:[.cacheIntermediates:false,.workingColorSpace:color])
 let image=try Original.apply(MediaAssemblyAdjustments(),to:CIImage(cgImage:cg),includeStickers:false)
 let rendered=context.createCGImage(image,from:image.extent,format:.RGBA8,colorSpace:color)!
 let bytes=rendered.dataProvider!.data! as Data;let row=rendered.bytesPerRow
 let preview=patches{x,y in Double(bytes[y*row+x*4])}
 let w=Int(image.extent.width);var floats=[Float](repeating:0,count:w*Int(image.extent.height)*4)
 floats.withUnsafeMutableBytes{context.render(image,toBitmap:$0.baseAddress!,rowBytes:w*16,bounds:image.extent,format:.RGBAf,colorSpace:color)}
 let previewFloat=patches{x,y in Double(floats[(y*w+x)*4])}
 func composed(_ a:MediaAssemblyAdjustments)throws->([Double],[String]){
  let out=AVAssetReaderVideoCompositionOutput(videoTracks:[track],videoSettings:[kCVPixelBufferPixelFormatTypeKey as String:kCVPixelFormatType_32BGRA])
  out.videoComposition=try Original.composition(asset:asset,adjustments:a)
  return first(out,try AVAssetReader(asset:asset))
 }
 var exposure=MediaAssemblyAdjustments();exposure.exposure=1
 let identity=try composed(MediaAssemblyAdjustments()),exposed=try composed(exposure)
 movies.append(["mp4":try Data(contentsOf:url).base64EncodedString(),"raw":raw.0,"rawColourAttachments":raw.1,"preview":preview,"previewFloat":previewFloat,
  "previewColorSpace":(cg.colorSpace?.name as String?) ?? "","composed":identity.0,"composedColourAttachments":identity.1,"composedExposure1":exposed.0])
}
FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject:["movies":movies],options:[.sortedKeys]))
'''


def main():
    output = Path(sys.argv[1])
    sources = reference.pinned_sources()
    engine = sources['MediaAssemblyEngine.swift']
    # The pinned movie composition and compositor, unchanged except for the
    # glue's error and engine names.
    output_size = engine[engine.index('    static func outputSize('):engine.index('    static func apply(')]
    composition = engine[engine.index('    static func composition('):engine.index('    /// Caller runs preparation')]
    composition = composition.replace('MediaAssemblyError.unsupportedExport', 'MediaAssemblyError.unsupported')
    classes = engine[engine.index('private final class MediaAssemblyVideoInstruction'):].replace('MediaAssemblyEngine.apply', 'Original.apply')
    driver = (DRIVER.replace('@@OUTPUTSIZE@@', output_size).replace('@@COMPOSITION@@', composition).replace('@@CLASSES@@', classes)
              .replace('@@SIZE@@', str(SIZE)).replace('@@PATCH@@', str(PATCH)))
    with tempfile.TemporaryDirectory(prefix='endfield-media-video-colour-') as tmp:
        directory = Path(tmp)
        oracle = reference.build_oracle(sources, driver, directory)
        files = [str(directory / (name + '.mp4')) for name, _ in MOVIES]
        result = json.loads(subprocess.check_output([str(oracle), str(reference.assets)] + files))
    movies = []
    for (name, tagged), movie in zip(MOVIES, result['movies']):
        movie.update({'name': name, 'bt709Tags': tagged})
        movies.append(movie)
    fixture = {'sourceCommit': reference.commit, 'sourcePins': reference.source_pins(sources), 'usesAppOrWindow': False,
               'size': SIZE, 'patch': PATCH, 'movies': movies,
               'synthetic': 'H.264 128x128, patch k (8x8, row-major 16 per row) holds grey k; patch values are inner 4x4 means of the red channel',
               'scope': 'Owned synthetic movies written with AVAssetWriter, read with the pinned preview, composition and compositor. No user media or app.'}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(fixture, sort_keys=True, separators=(',', ':')) + '\n')
    print(output, len(movies), output.stat().st_size)


if __name__ == '__main__':
    main()
