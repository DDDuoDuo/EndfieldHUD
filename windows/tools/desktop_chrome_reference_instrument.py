#!/usr/bin/env python3
"""Build-only source extraction; never instantiate SystemHUDView or its providers."""
import hashlib,json,pathlib,sys
PINS = {'Sources/SystemHUDView.swift': 'b2ab7090ad0a44c6bc4acbcaf6169b21983b138d868ed6135e4114791725d42f', 'Sources/HUDClockStyle.swift': '4a27f3b64893ed4d621bd7c55c5b6a12ad20358bc0e8c8eeae6062960a8cff03', 'Sources/HUDClock.swift': '942954a8d08699a03272a7e1bf2ffef2a431ebf998ced9ed2ed3d99a94f5e0c6', 'Sources/HUDMotionController.swift': 'cb8ea11cd91cf8d7cc68dd86d6af2d198caa796cba9e96db307d8e4646ddc46e', 'Sources/HUDSourceWatchView.swift': '30076b42ce9eb18e5f288e04e72a820e3401300676980aa5a6451b462fe8d832'}

def block(text, marker):
    assert text.count(marker)==1, marker
    start=text.index(marker); opening=text.index('{',start); level=1; end=opening+1
    while level:
        level += (text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]

def between(text,start,end):
    assert text.count(start)==1 and text.count(end)==1,(start,end)
    return text[text.index(start):text.index(end,text.index(start))]

def run(root,output):
    root=pathlib.Path(root);output=pathlib.Path(output);output.mkdir(parents=True,exist_ok=True)
    for name,expected in PINS.items():
        assert hashlib.sha256((root/name).read_bytes()).hexdigest()==expected,'Source changed; review extraction before updating pin: '+name
    source=(root/'Sources/SystemHUDView.swift').read_text()
    slices={
      'header':between(source,'        header.frame = CGRect(x: 270','        accountGauge.layer.position ='),
      'status':between(source,'        workBadge = text("", rect: CGRect(x: 320','        for index in -1..<5 {'),
      'footer':between(source,'        footer.frame = CGRect(x: 0','    private func buildIndustryWordmark()').rsplit('    }',1)[0],
      'design':between(source,'            self.designScale = max(0.1, min(','            self.notesLayout.position ='),
    }
    names=['private enum TextRole','private func text(','private func withoutActions(',
           'private func updateStatusPanel()','private func applySourceStatusProjection(',
           'private var reportScale:','private var reportCenterY:','private func reportRect(',
           'private func updateWorkPresentation(']
    methods='\n'.join(block(source,n).replace('private ', '', 1) for n in names)
    footer='\n'.join(line for line in source.splitlines() if 'self.hintLabel.string =' in line or 'self.hintLabel.frame.origin.y =' in line)
    assert footer.count('self.hintLabel')==3
    fixture='''import AppKit
import QuartzCore
// Original chrome bodies in a data-only fixture. Excluded calls are explicit
// inert stand-ins, and no SystemHUDView/NSWindow/controller is instantiated.
final class ChromeReadingSource { var reading: HUDClockReading? }
final class ChromeWorkSource { enum Phase { case idle,running,paused }; struct Snapshot { var phase: Phase = .idle }; var snapshot = Snapshot() }
final class ChromeProfileStub { func refreshWorkDuration() {} }
final class ChromeControlStub { func setAccessibilityHelp(_ text: String?) {} }
final class ChromeSourceFixture {
    static let verticalLift: CGFloat = 30
    let canvas=CALayer(),header=CALayer(),footer=CALayer(),statusPanel=CALayer()
    let corePlane=HUDDepthPlane(name:"core",depth:54,travel:24,lag:0.22)
    var titleLabel=CATextLayer(),clockTime=CATextLayer(),clockDate=CATextLayer(),workBadge=CATextLayer(),hintLabel=CATextLayer()
    let statusPlate=CAShapeLayer(),statusFrame=CAShapeLayer(),statusUnderline=CAShapeLayer()
    let clockPage=HUDClockPageViewport(),clockStyleArtwork=HUDClockStyleArtwork(),headerClock=ChromeReadingSource()
    let profileCanvas=ChromeProfileStub(),workModeController=ChromeWorkSource()
    var displayedWorkPhase: ChromeWorkSource.Phase?
    var navigationButtons:[HUDNavigationTarget:ChromeControlStub]=[:]
    var primaryTexts:[CATextLayer]=[],mutedTexts:[CATextLayer]=[],accentTexts:[CATextLayer]=[]
    var configuration=AppConfiguration.defaults,selectedModule=HUDModule.power
    var usesSourceShell=true,currentDark=true,clockHovered=false
    var currentAccent=NSColor(srgbRed:0.98,green:0.83,blue:0.12,alpha:1)
    var sourceStatusProjection:CATransform3D?,designScale:CGFloat=1,designOrigin=CGPoint.zero,bounds=CGRect(x:0,y:0,width:1280,height:800)
    let window:NSWindow?=nil
    func layoutClockControls() {} // Controls are exported as geometry; never native views.
    init(){canvas.bounds=CGRect(x:0,y:0,width:1000,height:640);canvas.addSublayer(corePlane.deployment);buildChrome()}
    func buildChrome(){
'''+'\n'.join(slices[k] for k in ('header','status','footer'))+'''
    }
    func layout(){
'''+slices['design']+footer+'''
    }
    func applyAppearance(dark:Bool){
        let foreground = dark ? NSColor(white: 0.94, alpha: 1) : NSColor(white: 0.12, alpha: 1)
        let muted = dark ? NSColor(white: 0.66, alpha: 1) : NSColor(white: 0.39, alpha: 1)
        let selectedColor = self.configuration.accentColor
        let yellow = dark ? selectedColor : selectedColor.blended(withFraction: 0.35, of: .black) ?? selectedColor
        for label in self.primaryTexts { label.foregroundColor = foreground.cgColor }
        for label in self.mutedTexts { label.foregroundColor = muted.cgColor }
        for label in self.accentTexts { label.foregroundColor = yellow.cgColor }
        self.currentDark=dark;self.currentAccent=yellow;updateStatusPanel()
    }
'''+methods+'\n}\n'
    # Every authored appearance statement must occur literally in production.
    for line in fixture.split('func applyAppearance(dark:Bool){',1)[1].split('self.currentDark=dark',1)[0].splitlines():
        if line.strip():assert line.strip() in source,line
    (output/'ChromeSourceFixture.swift').write_text(fixture)
    view=(root/'Sources/HUDSourceWatchView.swift').read_text()
    view+='''
#if HUD_CHROME_REFERENCE
extension HUDSourceWatchView {
    func chromeReferenceProjection(_ frame:HUDSourceWatchFrameBuilder.Frame,_ camera:HUDSourceWatchCamera.Frame) throws -> [String:Any] {
        try updateDesktopCenterPlane(frame:frame,camera:camera)
        updateDesktopStatusPlane(frame:frame,camera:camera)
        return ["centerNodeID":desktopCenterID!.rawValue,"statusNodeID":desktopStatusID!.rawValue,
                "unitsPerPoint":desktopPlaneCalibration?.unitsPerPoint ?? 0]
    }
}
#endif
'''
    (output/'HUDSourceWatchView.swift').write_text(view)
    timing={n:block(source,n) for n in ['private func animateSourceEntrance(','private func animateSourceExit(','private func animate(_ item:']}
    for fragment in ['duration: 0.24, delay: 0.20','duration: 0.06, delay: 0.35','controlPoints: 0.20, 0.72, 0.22, 1']:
        assert any(fragment in s for s in timing.values()),fragment
    report={'sourceSHA256':PINS,'productionSourceModified':False,'SystemHUDViewConstructed':False,
            'extractedSlices':slices,'extractedMethods':methods,'sourceTiming':timing,
            'stubs':['profile duration refresh omitted','work phase supplied','clock reading supplied','native accessibility controls omitted'],
            'scope':'Actual source layer construction/status update/layout functions; detached source Watch projection only; not a full SystemHUDView runtime.'}
    (output/'instrumentation.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':run(*sys.argv[1:])
