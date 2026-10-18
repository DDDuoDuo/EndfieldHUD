import Foundation

// Original controller with caller-supplied clock/timers only. No application,
// run-loop timer, Focus executor, profile store, window or real service exists.
final class FixtureTimer: WorkModeTimer {
    let interval:Double,repeats:Bool,action:()->Void
    var due:Double,valid=true
    init(_ interval:Double,_ repeats:Bool,_ now:Double,_ action:@escaping()->Void){self.interval=interval;self.repeats=repeats;self.due=now+interval;self.action=action}
    func invalidate(){valid=false}
}
final class FixtureClock {
    var now=0.0,timers:[FixtureTimer]=[]
    func schedule(_ interval:Double,_ repeats:Bool,_ tolerance:Double,_ action:@escaping()->Void)->WorkModeTimer {
        let timer=FixtureTimer(interval,repeats,now,action);timers.append(timer);return timer
    }
    func wake(){for timer in timers.filter({$0.valid && $0.due<=now}).sorted(by:{$0.due<$1.due}) {
        guard timer.valid else {continue}
        if timer.repeats {timer.due += floor((now-timer.due)/timer.interval+1)*timer.interval}else{timer.valid=false}
        timer.action()
    };timers.removeAll{!$0.valid}}
}
@main enum Main {
    static func main()throws{
        precondition(CommandLine.arguments.count==2)
        let clock=FixtureClock(),controller=WorkModeController(clock:{clock.now},scheduleTimer:clock.schedule)
        var notifications=0,checkpoints:[Double]=[];_ = controller.observe{notifications+=1};controller.onTrackedWorkSecondsChanged={checkpoints.append($0)}
        var rows:[[String:Any]]=[]
        func step(_ op:String,_ now:Double,_ value:Double=0){clock.now=now;var result:Any=NSNull()
            switch op {
            case "restore":result=controller.restoreTrackedWorkSeconds(value)
            case "countdown":result=controller.chooseCountdown(seconds:value)
            case "stopwatch":controller.chooseStopwatch()
            case "start":controller.start()
            case "pause":controller.pause()
            case "resume":controller.resume()
            case "stop":controller.stop()
            case "reset":controller.reset()
            case "visible":controller.setVisible(value != 0)
            case "suspended":controller.setSuspended(value != 0)
            case "refresh":controller.refresh()
            case "wake":clock.wake()
            case "shutdown":controller.shutdown()
            case "sample":break
            default:preconditionFailure(op)
            }
            let s=controller.snapshot
            let timers=clock.timers.filter{$0.valid};let display=timers.first{$0.repeats}?.due,completion=timers.first{!$0.repeats}?.due
            rows.append(["op":op,"now":now,"value":value,"result":result,"kind":s.kind.rawValue,"phase":s.phase.rawValue,"duration":s.duration,"elapsed":s.elapsed,"remaining":s.remaining,"progress":s.progress,"seconds":s.displayedSeconds,"text":s.timeText,"active":s.isActive,"suspended":controller.isSuspended,"revision":controller.revision,"notifications":notifications,"tracked":controller.trackedWorkSeconds,"checkpoints":checkpoints,"displayDeadline":display.map{$0 as Any} ?? NSNull(),"completionDeadline":completion.map{$0 as Any} ?? NSNull()])
            checkpoints.removeAll(keepingCapacity:true);clock.timers.removeAll{!$0.valid}
        }
        step("sample",0);step("restore",0,123.5);step("restore",0,5);step("countdown",0,300);step("start",0);step("sample",123.75);step("pause",123.75);step("sample",523.75);step("resume",523.75);step("visible",523.75,1);step("wake",524.95);step("wake",524.95);step("visible",524.95,0);step("suspended",525,1);step("suspended",1025,0);step("reset",1025);step("stopwatch",1025);step("start",1025);step("sample",1325);step("visible",1325,1);step("wake",4925.4);step("pause",4925.4);step("resume",5015.4);step("stop",5017.5);step("start",5017.5);step("reset",5017.5)
        var rng:UInt64=0x27c85
        let ops=["sample","countdown","stopwatch","start","pause","resume","stop","reset","visible","suspended","refresh","wake"]
        for _ in 0..<700 {rng=rng &* 6364136223846793005 &+ 1;let op=ops[Int((rng>>32)%UInt64(ops.count))];let increments=[0.0,0.000001,0.1,0.3,1,1.2,17,3600];clock.now += increments[Int(rng%UInt64(increments.count))];let durations=[0.0,1,1.49,1.5,5,300,1800,86400,86401];let value=op=="countdown" ? durations[Int((rng>>16)%UInt64(durations.count))] : Double((rng>>24)%2);step(op,clock.now,value)}
        step("shutdown",clock.now)
        let inputs=["","0","-1","0:00","0.01","1:60","-1:30","2.5:00","1:2.2","1:2:3","1440:01","nan","inf","tomorrow","0:01","1","1.5","1,5"," 25 ","1:02","1440:00","+1","1e1","0x1p2","0x1","0X1.8p1","1:",":1"," 1 :02","1: 02","1:02 ","1_0","1,2,3",".5","+.5","1.","\u{a0}1\u{3000}","\u{85}1\u{2028}","1:02.0","1e1:2e1","0x1p0:0x1p1"]
        let parser=inputs.map{["input":$0,"seconds":WorkModeDuration.parse($0).map{$0 as Any} ?? NSNull()]}
        let output:[String:Any]=["rows":rows,"parse":parser,"edit":[-1.0,0,1,1.5,61,86399.9,86400,100000].map{["value":$0,"text":WorkModeDuration.editText($0)]},"isolation":"Original WorkModeController, fake clock/timers, no live services or data"]
        try JSONSerialization.data(withJSONObject:output,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[1]));print("Original Work Mode: \(rows.count) controller checkpoints / \(parser.count) duration cases")
    }
}
