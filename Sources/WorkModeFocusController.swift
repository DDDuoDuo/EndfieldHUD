import Foundation

enum WorkModeFocusState { case idle, starting, enabled, preserved, stopping, failed, unavailable }
enum WorkModeFocusCommand: Equatable {
    case start, end
    var shortcutName: String { self == .start ? "EndfieldCharge Focus Start" : "EndfieldCharge Focus End" }
}
enum WorkModeFocusRunError: Error, Equatable {
    case unavailable, notConfigured, launchFailed, commandFailed(Int32), invalidResponse, stillRunning
    var mayHaveChangedFocus: Bool {
        switch self { case .commandFailed, .invalidResponse, .stillRunning: return true; default: return false }
    }
    var message: String {
        switch self {
        case .unavailable:
            return L10n.text("Automatic Focus requires macOS 13 or later and Shortcuts.", "自动专注模式需要 macOS 13 或更新版本及快捷指令。")
        case .notConfigured:
            return L10n.text("Add EndfieldCharge Focus Start and End shortcuts to enable automatic Focus.", "请添加 EndfieldCharge Focus Start 和 End 快捷指令以启用自动专注模式。")
        case .stillRunning:
            return L10n.text("Waiting for Shortcuts; the Focus change is not confirmed yet.", "正在等待快捷指令，尚未确认专注模式更改。")
        case .launchFailed:
            return L10n.text("Shortcuts could not open. Automatic Focus was not started.", "无法启动快捷指令，自动专注模式未启动。")
        case .commandFailed, .invalidResponse:
            return L10n.text("Automatic Focus could not be confirmed. Check the Focus shortcuts and current Focus.", "无法确认自动专注模式，请检查专注模式快捷指令及当前状态。")
        }
    }
}

protocol WorkModeFocusExecuting: AnyObject {
    var isAvailable: Bool { get }
    /// Completion must run on main. A command is never considered cancelled
    /// merely because its CLI is slow: Shortcuts may already own the operation.
    func run(_ command: WorkModeFocusCommand, completion: @escaping (Result<String, WorkModeFocusRunError>) -> Void)
}

/// Executes only the two user-owned shortcuts through Apple's public CLI.
/// There are no private Focus preferences, scripting additions or UI events.
final class ShortcutsWorkModeFocusExecutor: WorkModeFocusExecuting {
    private let worker = DispatchQueue(label: "EndfieldCharge.workMode.focus", qos: .utility)
    private let commandExecutor: ([String], Int) throws -> String
    private let availability: () -> Bool
    init(commandExecutor: (([String], Int) throws -> String)? = nil, availability: (() -> Bool)? = nil) {
        self.commandExecutor = commandExecutor ?? Self.execute
        self.availability = availability ?? {
            if #available(macOS 13, *) { return FileManager.default.isExecutableFile(atPath: "/usr/bin/shortcuts") }
            return false
        }
    }
    var isAvailable: Bool { availability() }
    func run(_ command: WorkModeFocusCommand, completion: @escaping (Result<String, WorkModeFocusRunError>) -> Void) {
        guard isAvailable else { completion(.failure(.unavailable)); return }
        worker.async {
            let result: Result<String, WorkModeFocusRunError>
            do {
                if command == .start {
                    // Require both unambiguous names before enabling anything;
                    // an absent cleanup shortcut must never be discovered late.
                    let inventory = try self.commandExecutor(["list"], 65_536)
                    let names = inventory.split(whereSeparator: \.isNewline).map(String.init)
                    guard [WorkModeFocusCommand.start, .end].allSatisfy({ command in
                        names.filter { $0 == command.shortcutName }.count == 1
                    }) else { throw WorkModeFocusRunError.notConfigured }
                }
                // The shortcuts return literal text already. Asking macOS
                // 15.7 to coerce it via --output-type can recursively crash
                // Apple's CLI after the Focus operation has executed.
                let response = try self.commandExecutor(["run", command.shortcutName], 4096)
                result = .success(response)
            } catch let error as WorkModeFocusRunError { result = .failure(error) }
            catch { result = .failure(.launchFailed) }
            // AppKit's terminate-later loop may be nested inside a main GCD
            // block. Re-entering that queue deadlocks; run-loop work remains
            // serviceable while macOS waits for our Focus cleanup reply.
            RunLoop.main.perform(inModes: [.common, RunLoop.Mode("NSModalPanelRunLoopMode")]) {
                completion(result)
            }
        }
    }

    private static func execute(_ arguments: [String], limit: Int) throws -> String {
        let task = Process(), pipe = Pipe()
        task.executableURL = URL(fileURLWithPath: "/usr/bin/shortcuts")
        task.arguments = arguments // No shell expansion or user-provided command text.
        task.standardInput = FileHandle.nullDevice
        task.standardOutput = pipe
        task.standardError = FileHandle.nullDevice
        do { try task.run() } catch { throw WorkModeFocusRunError.launchFailed }
        var collected = Data(), oversized = false
        // Drain continuously so even a wrongly edited shortcut cannot block on
        // a full stdout pipe; retain only a bounded acknowledgement, not output.
        while true {
            let chunk = pipe.fileHandleForReading.readData(ofLength: 4096)
            if chunk.isEmpty { break }
            if collected.count + chunk.count <= limit { collected.append(chunk) }
            else { oversized = true }
        }
        task.waitUntilExit()
        guard task.terminationReason == .exit, task.terminationStatus == 0 else {
            throw WorkModeFocusRunError.commandFailed(task.terminationStatus)
        }
        guard !oversized, let text = String(data: collected, encoding: .utf8) else { throw WorkModeFocusRunError.invalidResponse }
        return text.trimmingCharacters(in: .whitespacesAndNewlines)
    }
}

/// Focus follows Work Mode's active session, including pause. A shortcut's
/// exact acknowledgement is the only ownership evidence. Existing Focus is
/// left untouched, and no claim is made to restore schedules or user edits.
final class WorkModeFocusController {
    private(set) var state: WorkModeFocusState = .idle
    var statusMessage: String? { failure?.message }
    var isPending: Bool { pending != nil }
    private let executor: WorkModeFocusExecuting
    private let warningDelay: TimeInterval
    private var desired = false
    private var owned = false
    private var preserved = false
    private var uncertain = false
    private var shuttingDown = false
    private var pending: UUID?
    private var warning: DispatchWorkItem?
    private var failure: WorkModeFocusRunError?
    private var observers: [UUID: () -> Void] = [:]
    private var shutdownCallbacks: [(Bool) -> Void] = []

    init(executor: WorkModeFocusExecuting = ShortcutsWorkModeFocusExecutor(), warningDelay: TimeInterval = 20) {
        self.executor = executor; self.warningDelay = max(0, warningDelay)
    }
    @discardableResult func observe(_ callback: @escaping () -> Void) -> UUID {
        requireMain(); let id = UUID(); observers[id] = callback; return id
    }
    func removeObserver(_ id: UUID) { requireMain(); observers.removeValue(forKey: id) }
    func receive(_ snapshot: WorkModeSnapshot) {
        requireMain(); guard !shuttingDown, desired != snapshot.isActive else { return }
        desired = snapshot.isActive
        if !desired { preserved = false }
        reconcile()
    }
    func shutdown(completion: @escaping (Bool) -> Void) {
        requireMain(); shuttingDown = true; desired = false; preserved = false
        shutdownCallbacks.append(completion)
        reconcile()
    }

    private func reconcile() {
        guard pending == nil else { return }
        if owned {
            if !desired || failure != nil { begin(.end) }
            else { publish(.enabled, failure: nil) }
        } else if desired && !preserved {
            begin(.start)
        } else {
            if !uncertain { publish(desired && preserved ? .preserved : .idle, failure: nil) }
            finishShutdownIfPossible()
        }
    }
    private func begin(_ command: WorkModeFocusCommand) {
        guard executor.isAvailable else {
            publish(.unavailable, failure: .unavailable); finishShutdownIfPossible(); return
        }
        let token = UUID(); pending = token
        publish(command == .start ? .starting : .stopping, failure: nil)
        let delayed = DispatchWorkItem { [weak self] in
            guard let self, self.pending == token else { return }
            self.publish(.failed, failure: .stillRunning)
        }
        warning = delayed
        DispatchQueue.main.asyncAfter(deadline: .now() + warningDelay, execute: delayed)
        executor.run(command) { [weak self] result in self?.finished(command, token: token, result: result) }
    }
    private func finished(_ command: WorkModeFocusCommand, token: UUID, result: Result<String, WorkModeFocusRunError>) {
        requireMain(); guard pending == token else { return }
        pending = nil; warning?.cancel(); warning = nil
        let parsed: Result<String, WorkModeFocusRunError>
        switch result {
        case .success(let text):
            let value = text.trimmingCharacters(in: .whitespacesAndNewlines)
            let valid = command == .start ? ["enabled", "preserved"] : ["released", "unchanged"]
            parsed = valid.contains(value) ? .success(value) : .failure(.invalidResponse)
        case .failure: parsed = result
        }
        switch parsed {
        case .success(let value):
            uncertain = false
            if command == .start {
                owned = value == "enabled"; preserved = value == "preserved" && desired
                publish(owned ? .enabled : (preserved ? .preserved : .idle), failure: nil)
            } else {
                owned = false; preserved = false
                publish(.idle, failure: nil)
            }
            // A Reset received during Start is compensated only after enabled;
            // a new Start received during End waits for cleanup acknowledgement.
            reconcile()
        case .failure(let error):
            uncertain = uncertain || error.mayHaveChangedFocus
            publish(error == .unavailable || error == .notConfigured ? .unavailable : .failed, failure: error)
            // Never retry automatically from an error callback: keep ownership
            // honest and avoid a command loop or disabling a preserved Focus.
            finishShutdownIfPossible()
        }
    }
    private func finishShutdownIfPossible() {
        guard shuttingDown, pending == nil else { return }
        let callbacks = shutdownCallbacks; shutdownCallbacks.removeAll()
        callbacks.forEach { $0(!owned && !uncertain) }
    }
    private func publish(_ next: WorkModeFocusState, failure: WorkModeFocusRunError?) {
        let changed = state != next || self.failure != failure
        state = next; self.failure = failure
        if changed { Array(observers.values).forEach { $0() } }
    }
    private func requireMain() { precondition(Thread.isMainThread) }
    deinit { warning?.cancel() }
}
