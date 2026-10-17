import Foundation

enum HUDClockFormat {case twentyFourHour,twelveHour}
final class FixtureTimer: HUDClockTimer {
    let fire:()->Void;var valid=true
    init(_ fire:@escaping()->Void){self.fire=fire}
    func invalidate(){valid=false}
}
@main enum Main {
    static func main()throws {
        precondition(CommandLine.arguments.count==2)
        var date=Date(timeIntervalSince1970:0),zone=TimeZone(secondsFromGMT:0)!,timers:[FixtureTimer]=[],reads=0,notices=0
        let clock=HUDClock(now:{reads+=1;return date},timeZone:{zone},scheduleTimer:{interval,action in
            precondition(interval==1);let timer=FixtureTimer(action);timers.append(timer);return timer
        })
        clock.onChange={_ in notices+=1}
        var rows:[[String:Any]]=[]
        func sample(_ op:String,_ stamp:Double,_ offset:Int,_ value:Int=0){
            date=Date(timeIntervalSince1970:stamp);zone=TimeZone(secondsFromGMT:offset)!
            switch op {
            case "active":clock.setActive(value != 0)
            case "format":clock.setFormat(value==0 ? .twentyFourHour:.twelveHour)
            case "wake":for timer in timers where timer.valid {timer.fire()}
            case "stale":timers.first?.fire()
            default:break
            }
            var calendar=Calendar(identifier:.gregorian);calendar.timeZone=zone
            let c=calendar.dateComponents([.month,.day,.weekday,.hour,.minute,.second],from:date)
            rows.append(["op":op,"value":value,"fields":[c.month!,c.day!,c.weekday!-1,c.hour!,c.minute!,c.second!],"reads":reads,"notices":notices,"active":clock.isActive,"time":clock.reading?.time as Any? ?? NSNull(),"date":clock.reading?.date as Any? ?? NSNull()])
        }
        sample("active",0,0,0);sample("format",0,0,1);sample("active",0,0,1)
        for day in 0..<28 {
            for format in 0...1 {sample("format",Double(day)*86400+Double(day%24)*3600+3599,19800,format);sample("wake",Double(day)*86400+Double(day%24)*3600+3600,-28800)}
        }
        sample("active",1791500000,0,0);sample("wake",1791501000,0);sample("format",1791502000,0,0);sample("stale",1791502000,0)
        sample("active",1791503000,0,1);sample("stale",1791503001,0);sample("wake",1791503002,0);sample("active",1791503002,0,0)
        let data=try JSONSerialization.data(withJSONObject:["source":"Sources/HUDClock.swift","rows":rows],options:[.sortedKeys])
        try data.write(to:URL(fileURLWithPath:CommandLine.arguments[1]),options:.withoutOverwriting)
    }
}
