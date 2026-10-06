import AppKit

struct MediaAssemblyParameter {
    let id: String
    let title: String
    let range: ClosedRange<Double>
    var value: Double
    var step: Double = 0.01
}

/// Source/export confirmations share the retained personal-card surface.
/// Editing tools live in the canvas drawer, so this menu owns no slider state.
final class MediaAssemblyControlMenu: NotesRetainedMenu {
    var onCommand: ((String) -> Void)?
    private let heading: String
    private let commands: [(String,String)]
    init(title: String, commands: [(String,String)] = [], dark: Bool) {
        self.heading = title; self.commands = commands
        super.init(size:CGSize(width:370,height:max(75,42+commands.count*31)),dark:dark)
        items = [Item(id:"close",title:"×",rect:CGRect(x:339,y:8,width:23,height:23))]
        for (index,command) in commands.enumerated() {
            items.append(Item(id:command.0,title:command.1,rect:CGRect(x:8,y:38+index*31,width:354,height:26)))
        }
        paint()
    }
    required init?(coder:NSCoder) { nil }
    override func paintContent(on parent: CALayer) {
        text(HUDSectionHeading.text(heading),rect:CGRect(x:10,y:11,width:317,height:20),size:12,parent:parent)
    }
    override func perform(_ id: String) {
        if commands.contains(where:{$0.0 == id}) { onCommand?(id) } else { super.perform(id) }
    }
}
