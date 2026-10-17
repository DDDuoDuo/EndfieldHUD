import AppKit
import QuartzCore

// Detached oracle for EventLogCanvas.timestamp (en_US_POSIX "MM-dd HH:mm:ss"
// in the app's default time zone). The original canvas is compiled from an
// unchanged copy except for the visibility of that one private method; no
// store directory, window, observer or user event is created. Each zone gets
// a new canvas because DateFormatter captures the default zone when created.
enum HUDRuntimeAppearance {static var accent=NSColor(srgbRed:250/255,green:212/255,blue:31/255,alpha:1);static let reduceMotion=true}
enum L10n {static func text(_ english:String,_ chinese:String)->String {chinese}}
struct HUDModuleContentStyle {let dark:Bool;let contentsScale:CGFloat}
protocol HUDModuleContentFactory {func makeContent(for module:HUDModule,style:HUDModuleContentStyle)->CALayer}
@main enum Main {
    static func main() throws {
        precondition(CommandLine.arguments.count==2)
        let output=URL(fileURLWithPath:CommandLine.arguments[1])
        // IANA zone -> Windows registry time zone key (the Windows test resolves
        // the same zone through EnumDynamicTimeZoneInformation).
        let zones:[(String,String)]=[("UTC","UTC"),("America/Los_Angeles","Pacific Standard Time"),("Europe/Berlin","W. Europe Standard Time"),
            ("Europe/London","GMT Standard Time"),("Asia/Shanghai","China Standard Time"),("Asia/Kolkata","India Standard Time"),
            ("America/St_Johns","Newfoundland Standard Time"),("Australia/Lord_Howe","Lord Howe Standard Time"),("Pacific/Chatham","Chatham Islands Standard Time")]
        // Fixed instants (Foundation reference seconds): epoch, sub-second floors,
        // pre-2001 negative values, a leap day and year boundaries.
        // Sub-millisecond values pin how Foundation reaches whole milliseconds
        // before ICU takes the floor of the second.
        let fixed:[Double]=[0,0.999,-0.25,-1.5,812000000,812000000.999999,-978307200,
            0.0004,0.0005,0.0006,0.9994,0.9995,0.9996,1.9999,-0.0004,-0.0005,-0.0006,-0.9995,-1.0004,812000000.4995,812000000.9995,812000000.99949,
            Date(timeIntervalSince1970:1835395199.5).timeIntervalSinceReferenceDate,  // 2028-02-28T23:59:59.5Z
            Date(timeIntervalSince1970:1835481600).timeIntervalSinceReferenceDate,    // 2028-03-01T00:00:00Z
            Date(timeIntervalSince1970:1798761599).timeIntervalSinceReferenceDate,    // 2026-12-31T23:59:59Z
            Date(timeIntervalSince1970:1798761600).timeIntervalSinceReferenceDate]    // 2027-01-01T00:00:00Z
        var rows:[[String:Any]]=[]
        let window=(Date(timeIntervalSince1970:1735689600),Date(timeIntervalSince1970:1830297600)) // 2025-01-01 ... 2028-01-01
        for (identifier,windowsKey) in zones {
            let zone=TimeZone(identifier:identifier)!
            NSTimeZone.default=zone
            let canvas=EventLogCanvas(store:SystemEventLog(),reduceMotion:{true})
            var instants=fixed
            // Every DST transition in 2025...2027, with the seconds and the
            // half hour around each switch.
            var cursor=window.0
            while let next=zone.nextDaylightSavingTimeTransition(after:cursor),next<window.1 {
                for delta in [-1800.0,-1,-0.001,0,0.5,1,1800] {instants.append(next.timeIntervalSinceReferenceDate+delta)}
                cursor=next
            }
            for instant in instants {
                rows.append(["zone":identifier,"windowsZone":windowsKey,"reference":instant,"text":canvas.timestamp(Date(timeIntervalSinceReferenceDate:instant))])
            }
        }
        let data:[String:Any]=["format":"en_US_POSIX MM-dd HH:mm:ss","source":"Sources/EventLogCanvas.swift timestamp(_:)","cases":rows]
        try JSONSerialization.data(withJSONObject:data,options:[.prettyPrinted,.sortedKeys]).write(to:output.appendingPathComponent("event_log_timestamps.json"))
        print("Exported \(rows.count) original Event Log timestamps in \(zones.count) zones")
    }
}
