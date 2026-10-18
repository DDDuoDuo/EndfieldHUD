import AppKit

// The unchanged drawing/color implementation, synthetic points only. No app,
// screen/window, user preference, data store or drawing canvas is initialized.
@main enum Reference {
    static func main() throws {
        precondition(CommandLine.arguments.count == 2)
        let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
        func value<T: Encodable>(_ v: T) throws -> Any { try JSONSerialization.jsonObject(with: encoder.encode(v)) }
        var drawing = NotesDrawing(), rows: [[String:Any]] = [], seed: UInt64 = 93
        func random() -> Double { seed = seed &* 6364136223846793005 &+ 1; return Double((seed >> 32) % 10001) / 10000 }
        for index in 0..<240 {
            if index % 3 != 2 {
                var points:[NotesDrawingPoint] = []
                for _ in 0..<(1 + index % 8) { points.append(.init(x:random(),y:random())) }
                let stroke = NotesDrawingStroke(points:points,width:1 + random()*79,color:.init(red:random(),green:random(),blue:random(),alpha:random()))
                let result = drawing.append(stroke), size = CGSize(width:290,height:184)
                var path:[[Double]] = []
                NotesDrawing.path(stroke,size:size).applyWithBlock { element in
                    precondition(element.pointee.type == .moveToPoint || element.pointee.type == .addLineToPoint)
                    path.append([Double(element.pointee.points[0].x),Double(element.pointee.points[0].y)])
                }
                rows.append(["operation":"append","stroke":try value(stroke),"result":result,"drawing":try value(drawing),"path":path])
            } else {
                let p=CGPoint(x:random()*290,y:random()*184),radius=random()*40
                let result=drawing.erase(at:p,radius:radius,in:CGSize(width:290,height:184))
                rows.append(["operation":"erase","point":[p.x,p.y],"radius":radius,"result":result,"drawing":try value(drawing)])
            }
        }
        let color=NotesRGBA(red:1,green:0,blue:0)
        let invalid:[NotesDrawingStroke] = [.init(points:[],width:8,color:color),.init(points:[.init(x:0,y:0)],width:0,color:color),.init(points:[.init(x:1.001,y:0)],width:8,color:color),.init(points:[.init(x:0,y:0)],width:81,color:color)]
        for stroke in invalid {let result=drawing.append(stroke);rows.append(["operation":"append","stroke":try value(stroke),"result":result,"drawing":try value(drawing)])}
        try JSONSerialization.data(withJSONObject:["rows":rows],options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[1]))
        print("Original NotesDrawing: \(rows.count) geometry/mutation cases")
    }
}
