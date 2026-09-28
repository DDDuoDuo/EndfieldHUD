import AppKit
import CoreAudio

enum PerAppAudioState: String { case preparing, active, stopping, failed }

struct PerAppAudioSession: Identifiable, Equatable {
    var id: UInt32 { processID }
    let processID: UInt32
    let pid: Int32
    let name: String
    var gain: Double
    var state: PerAppAudioState
    var error: String?
    var applicationURL: URL? = nil
    var icon: NSImage? = nil
}

enum PerAppAudioRouteEvent { case active; case failed(String); case stopped(String?) }

protocol PerAppAudioRoute: AnyObject {
    var isStopped: Bool { get }
    var isStopping: Bool { get }
    func begin() throws
    func setGain(_ value: Double)
    /// Returns a visible cleanup error, if any. isStopped distinguishes errors
    /// that still need a retry from a safely dismantled route.
    func stop() -> String?
}
extension PerAppAudioRoute { var isStopping: Bool { false } }

protocol PerAppAudioRouteFactory {
    var isSupported: Bool { get }
    func make(application: AudioApplicationInfo, output: AudioDeviceInfo,
              event: @escaping (PerAppAudioRouteEvent) -> Void) throws -> PerAppAudioRoute
}

/// Explicitly opted-in sessions survive leaving Volume. The app owner stops
/// every session on sleep, logout and termination; no automatic restart occurs.
final class PerAppAudioController {
    private(set) var sessions: [PerAppAudioSession] = []
    private(set) var statusMessage: String?
    var isSupported: Bool { factory.isSupported }
    var capabilityMessage: String? {
        isSupported ? nil : L10n.text("Experimental app volume requires macOS 14.2 or later.", "实验性应用音量需要 macOS 14.2 或更新版本。")
    }
    private struct OwnedRoute { let generation: UUID; let route: PerAppAudioRoute }
    private var routes: [UInt32: OwnedRoute] = [:]
    private var observers: [UUID: () -> Void] = [:]
    private let factory: PerAppAudioRouteFactory

    init(factory: PerAppAudioRouteFactory = CorePerAppAudioFactory()) { self.factory = factory }
    static func fixture() -> PerAppAudioController { PerAppAudioController(factory: FixturePerAppAudioFactory()) }
    @discardableResult func observe(_ callback: @escaping () -> Void) -> UUID {
        requireMain(); let token = UUID(); observers[token] = callback; return token
    }
    func removeObserver(_ token: UUID) { requireMain(); observers.removeValue(forKey: token) }

    func availability(application: AudioApplicationInfo, output: AudioDeviceInfo) -> String? {
        if !isSupported { return capabilityMessage }
        if (!application.isRunningOutput && application.outputDeviceIDs.isEmpty) || application.pid <= 0 || application.pid == ProcessInfo.processInfo.processIdentifier {
            return L10n.text("Choose another process with active audio output.", "请选择正在输出音频的其他进程。")
        }
        if output.isBluetooth || output.outputChannels != 2 || output.inputChannels != 0 {
            return L10n.text("This experimental route supports stereo output-only built-in or USB devices. Bluetooth and devices with input channels are not supported.", "此实验性路由支持仅具备双声道输出的内建或 USB 设备，暂不支持蓝牙或带输入声道的设备。")
        }
        if let transport = output.transportType, transport != kAudioDeviceTransportTypeBuiltIn && transport != kAudioDeviceTransportTypeUSB {
            return L10n.text("This output device does not support app volume routing.", "此输出设备暂不支持应用音量路由。")
        }
        if !application.outputDeviceIDs.isEmpty && Set(application.outputDeviceIDs) != Set([output.id]) {
            return L10n.text("This app uses a different output route.", "此应用使用其他输出路由。")
        }
        return nil
    }

    @discardableResult func start(application: AudioApplicationInfo, output: AudioDeviceInfo, initialGain: Double = 1) -> Bool {
        requireMain()
        guard initialGain.isFinite else {
            statusMessage = L10n.text("Choose a finite app volume between 0% and 100%.", "请选择 0% 至 100% 之间的有效应用音量。")
            notify(); return false
        }
        let gain = min(1, max(0, initialGain))
        if let unavailable = availability(application: application, output: output) { statusMessage = unavailable; notify(); return false }
        guard routes[application.id] == nil,
              !sessions.contains(where: { $0.pid == application.pid && routes[$0.processID] != nil }) else {
            statusMessage = L10n.text("Stop the existing route before starting another for this process.", "请先停止此进程的现有路由，再重新开始。")
            notify(); return false
        }
        let generation = UUID()
        do {
            let route = try factory.make(application: application, output: output) { [weak self] event in
                self?.receive(event, processID: application.id, generation: generation)
            }
            routes[application.id] = OwnedRoute(generation: generation, route: route)
            sessions.removeAll { $0.processID == application.id }
            sessions.append(PerAppAudioSession(processID: application.id, pid: application.pid, name: application.name,
                gain: gain, state: .preparing, error: nil, applicationURL: application.applicationURL, icon: application.icon))
            route.setGain(gain)
            statusMessage = L10n.text("Allow System Audio Recording if asked, then play audio in the selected app to activate.", "如有提示，请允许系统音频录制，然后在所选应用中播放音频以激活。")
            notify()
            try route.begin()
            return true
        } catch {
            let detail = error.localizedDescription
            let hadRoute = routes[application.id] != nil
            receive(.failed(detail), processID: application.id, generation: generation)
            if !hadRoute {
                if let index = sessions.firstIndex(where: { $0.processID == application.id }) {
                    sessions[index].state = .failed; sessions[index].error = detail
                } else {
                    sessions.append(PerAppAudioSession(processID: application.id, pid: application.pid, name: application.name,
                        gain: gain, state: .failed, error: detail, applicationURL: application.applicationURL, icon: application.icon))
                }
                statusMessage = detail; notify()
            }
            return false
        }
    }

    @discardableResult func setGain(_ value: Double, processID: UInt32) -> Bool {
        requireMain()
        guard value.isFinite, let index = sessions.firstIndex(where: {
            $0.processID == processID && ($0.state == .preparing || $0.state == .active)
        }),
              let owned = routes[processID] else { return false }
        let gain = min(1, max(0, value))
        let changed = sessions[index].gain != gain
        let clearStatus = statusMessage != nil
        if changed {
            owned.route.setGain(gain)
            sessions[index].gain = gain
        }
        statusMessage = nil
        // Repeated pointer/accessibility events at the same value need neither
        // another audio-worker job nor a complete Volume canvas notification.
        if changed || clearStatus { notify() }
        return true
    }

    func stop(processID: UInt32) {
        requireMain()
        if let owned = routes[processID] {
            let error = owned.route.stop()
            if owned.route.isStopped {
                routes.removeValue(forKey: processID)
                sessions.removeAll { $0.processID == processID }
            } else if let index = sessions.firstIndex(where: { $0.processID == processID }) {
                sessions[index].state = owned.route.isStopping ? .stopping : .failed
                sessions[index].error = error
            }
            statusMessage = error
        } else {
            sessions.removeAll { $0.processID == processID }
            statusMessage = nil
        }
        notify()
    }

    func stopAll(reason: String? = nil) {
        requireMain()
        var errors: [String] = []
        for id in Array(routes.keys) {
            guard let owned = routes[id] else { continue }
            if let error = owned.route.stop() { errors.append(error) }
            if owned.route.isStopped { routes.removeValue(forKey: id) }
        }
        sessions.removeAll { routes[$0.processID] == nil }
        for index in sessions.indices {
            sessions[index].state = routes[sessions[index].processID]?.route.isStopping == true ? .stopping : .failed
            sessions[index].error = errors.first
        }
        statusMessage = errors.first ?? reason
        notify()
    }

    private func receive(_ event: PerAppAudioRouteEvent, processID: UInt32, generation: UUID) {
        guard Thread.isMainThread else {
            DispatchQueue.main.async { [weak self] in self?.receive(event, processID: processID, generation: generation) }; return
        }
        guard let owned = routes[processID], owned.generation == generation,
              let index = sessions.firstIndex(where: { $0.processID == processID }) else { return }
        switch event {
        case .active:
            guard !owned.route.isStopping else { return }
            owned.route.setGain(sessions[index].gain)
            sessions[index].state = .active; statusMessage = nil
        case .failed(let message):
            let cleanup = owned.route.stop()
            if owned.route.isStopped { routes.removeValue(forKey: processID) }
            sessions[index].state = owned.route.isStopping ? .stopping : .failed
            sessions[index].error = cleanup.map { message + " " + $0 } ?? message
            statusMessage = sessions[index].error
        case .stopped(let error):
            if owned.route.isStopped {
                routes.removeValue(forKey: processID)
                sessions.removeAll { $0.processID == processID }
            } else {
                sessions[index].state = .failed; sessions[index].error = error
            }
            statusMessage = error
        }
        notify()
    }
    private func notify() { Array(observers.values).forEach { $0() } }
    private func requireMain() { precondition(Thread.isMainThread, "Per-app audio controls require the main thread") }
    deinit { for owned in routes.values { _ = owned.route.stop() } }
}

private struct FixturePerAppAudioFactory: PerAppAudioRouteFactory {
    let isSupported = true
    func make(application: AudioApplicationInfo, output: AudioDeviceInfo,
              event: @escaping (PerAppAudioRouteEvent) -> Void) throws -> PerAppAudioRoute { FixturePerAppAudioRoute(event: event) }
}
private final class FixturePerAppAudioRoute: PerAppAudioRoute {
    private let event: (PerAppAudioRouteEvent) -> Void
    private(set) var isStopped = true
    init(event: @escaping (PerAppAudioRouteEvent) -> Void) { self.event = event }
    func begin() throws { isStopped = false; event(.active) }
    func setGain(_ value: Double) {}
    func stop() -> String? { isStopped = true; return nil }
}
