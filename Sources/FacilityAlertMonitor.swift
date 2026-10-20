import AppKit
import Darwin
import IOKit

/// Facility alerts (spec §3.29): the Mac treated as an outpost. Raw values are
/// the spec's alert ids.
enum FacilityAlertKind: String, CaseIterable {
    case disk, memory = "mem", heat

    /// Clicking the banner or the tag opens this module.
    var module: HUDModule {
        switch self {
        case .disk: return .storage
        case .memory: return .activityMonitor
        case .heat: return .power
        }
    }

    /// The UI-language title; also the tag's hover title.
    var title: String { title(language: nil) }
    var englishTitle: String { title(language: .english) }
    /// The CJK name the banner prints after `FACILITY ALERT`.
    var chineseTitle: String { title(language: .simplifiedChinese) }

    private func title(language: AppLanguage?) -> String {
        switch self {
        case .disk: return L10n.text("LOW DISK SPACE", "存储空间不足", language: language)
        case .memory: return L10n.text("HIGH MEMORY PRESSURE", "内存压力过高", language: language)
        case .heat: return L10n.text("OVERHEATING", "设备过热", language: language)
        }
    }
}

/// One capacity read of the startup Data filesystem (the volume Storage shows).
struct FacilityDiskReading: Equatable {
    let volumeName: String
    let totalBytes: Int64
    let availableBytes: Int64

    init?(volumeName: String, totalBytes: Int64, availableBytes: Int64) {
        guard totalBytes > 0, availableBytes >= 0, availableBytes <= totalBytes else { return nil }
        self.volumeName = volumeName; self.totalBytes = totalBytes; self.availableBytes = availableBytes
    }

    var freeFraction: Double { Double(availableBytes) / Double(totalBytes) }
    var freePercent: Int { Int((freeFraction * 100).rounded()) }
}

/// The kernel's memory pressure level (the levels a dispatch source reports).
enum FacilityMemoryPressure: Int, Comparable {
    case normal, warning, critical

    /// DISPATCH_MEMORYPRESSURE_* / NOTE_MEMORYSTATUS_PRESSURE_* bits, which are
    /// also what `kern.memorystatus_vm_pressure_level` returns: 1, 2 and 4.
    init?(kernelLevel: UInt) {
        if kernelLevel & 0x4 != 0 { self = .critical }
        else if kernelLevel & 0x2 != 0 { self = .warning }
        else if kernelLevel & 0x1 != 0 { self = .normal }
        else { return nil }
    }

    static func < (a: Self, b: Self) -> Bool { a.rawValue < b.rawValue }
}

/// Resident memory as the Activity module counts it, in bytes.
struct FacilityMemoryUsage: Equatable {
    let usedBytes: UInt64
    let totalBytes: UInt64

    init?(usedBytes: UInt64, totalBytes: UInt64) {
        guard totalBytes > 0 else { return nil }
        self.usedBytes = min(usedBytes, totalBytes); self.totalBytes = totalBytes
    }

    var usedPercent: Int { Int((Double(usedBytes) / Double(totalBytes) * 100).rounded()) }
}

struct FacilityMemoryReading: Equatable {
    let pressure: FacilityMemoryPressure
    /// Read only while pressure is elevated or the alert is active.
    let usage: FacilityMemoryUsage?
}

enum FacilityThermalState: Int, Comparable {
    case nominal, fair, serious, critical

    init(_ state: ProcessInfo.ThermalState) {
        self = FacilityThermalState(rawValue: min(max(state.rawValue, 0), 3)) ?? .nominal
    }

    static func < (a: Self, b: Self) -> Bool { a.rawValue < b.rawValue }
}

struct FacilityThermalReading: Equatable {
    let state: FacilityThermalState
    /// Battery temperature in °C, read only while the state is serious or worse
    /// (or the alert is active). nil on Macs without a battery.
    let temperatureCelsius: Double?

    /// AppleSmartBattery's `Temperature` is in hundredths of a degree Celsius.
    static func batteryCelsius(registryValue value: Any?) -> Double? {
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else { return nil }
        let celsius = number.doubleValue / 100
        return celsius.isFinite && (-40...150).contains(celsius) ? celsius : nil
    }
}

/// The latest value of every source; nil means unavailable (or not sampled yet).
struct FacilityReadings: Equatable {
    var disk: FacilityDiskReading?
    var memory: FacilityMemoryReading?
    var thermal: FacilityThermalReading?
}

/// One listed alert. Every string is rendered on access, so a language change
/// re-renders listed alerts.
struct FacilityAlert: Equatable {
    enum Measurement: Equatable {
        case disk(FacilityDiskReading)
        case memory(FacilityMemoryReading)
        case heat(FacilityThermalReading)
    }

    fileprivate(set) var measurement: Measurement
    /// True from the raise until `FacilityAlertMonitor.markPresented(_:)`: the
    /// banner is shown only for new alerts; the tag stack shows every listed one.
    fileprivate(set) var isNew: Bool

    var kind: FacilityAlertKind {
        switch measurement {
        case .disk: return .disk
        case .memory: return .memory
        case .heat: return .heat
        }
    }
    var id: String { kind.rawValue }
    var module: HUDModule { kind.module }
    var title: String { kind.title }
    var englishTitle: String { kind.englishTitle }
    var chineseTitle: String { kind.chineseTitle }

    /// The banner's big orange value and the tag's bold orange number.
    var value: String {
        switch measurement {
        case .disk(let disk): return "\(disk.freePercent)%"
        case .memory(let memory):
            if let usage = memory.usage { return "\(usage.usedPercent)%" }
            return memory.pressure == .critical ? L10n.text("CRITICAL", "危急") : L10n.text("WARNING", "警告")
        case .heat(let heat):
            if let celsius = heat.temperatureCelsius { return "\(Int(celsius.rounded()))°C" }
            return heat.state == .critical ? L10n.text("CRITICAL", "危急") : L10n.text("SERIOUS", "严重")
        }
    }

    /// The small label under the banner value.
    var unit: String {
        switch measurement {
        case .disk: return L10n.text("FREE", "可用")
        case .memory: return L10n.text("PRESSURE", "压力")
        case .heat(let heat):
            // Public APIs expose no CPU die temperature; the reading is the battery's.
            return heat.temperatureCelsius == nil ? L10n.text("THERMAL STATE", "热状态") : L10n.text("BATTERY", "电池")
        }
    }

    /// The banner's grey detail line.
    var detail: String {
        switch measurement {
        case .disk(let disk):
            let name = disk.volumeName, left = Self.storage(bytes: disk.availableBytes)
            return L10n.text("\(name)  \(left) left", "\(name)  剩余 \(left)")
        case .memory(let memory):
            guard let usage = memory.usage else { return "" }
            let used = Self.gigabytes(usage.usedBytes), total = Self.gigabytes(usage.totalBytes)
            return L10n.text("\(used) / \(total) GB in use", "已用 \(used) / \(total) GB")
        case .heat:
            // The thermal state documentation: at serious, fans run at maximum speed.
            return L10n.text("Fans at maximum", "风扇全速运转")
        }
    }

    /// Tag text = label, bold orange `value`, then the optional suffix:
    /// `DISK 8% FREE`, `RAM 92% PRESSURE`, `HEAT 92°C`.
    var tagLabel: String {
        switch kind {
        case .disk: return L10n.text("DISK", "磁盘")
        case .memory: return L10n.text("RAM", "RAM")
        case .heat: return L10n.text("HEAT", "温度")
        }
    }
    var tagSuffix: String? { kind == .heat ? nil : unit }
    var tagText: String { [tagLabel, value, tagSuffix].compactMap { $0 }.joined(separator: " ") }

    /// Decimal units as the Storage and Activity modules print them (spec §1.8).
    static func storage(bytes: Int64) -> String {
        let gigabytes = Double(bytes) / 1_000_000_000
        return gigabytes >= 1000 ? String(format: "%.2f TB", gigabytes / 1000) : String(format: "%.1f GB", gigabytes)
    }

    static func gigabytes(_ bytes: UInt64) -> String { String(format: "%.1f", Double(bytes) / 1_000_000_000) }
}

enum FacilityAlertEvent: Equatable {
    case memoryPressure(FacilityMemoryPressure)
    case thermalStateChanged
    case volumesChanged
    case didWake
}

/// Every input of the monitor. Reads run on the worker; events and timer ticks
/// arrive on the main queue. Tests replace all of it with synthetic readings.
struct FacilityAlertSources {
    typealias Cancel = () -> Void
    var readDisk: () -> FacilityDiskReading?
    var readMemoryPressure: () -> FacilityMemoryPressure?
    var readMemoryUsage: () -> FacilityMemoryUsage?
    var readThermalState: () -> FacilityThermalState
    var readTemperature: () -> Double?
    var observeEvents: (@escaping (FacilityAlertEvent) -> Void) -> Cancel
    var scheduleRepeating: (TimeInterval, @escaping () -> Void) -> Cancel
    var runWorker: (@escaping () -> Void) -> Void
    /// Monotonic seconds, for the recovery hold.
    var clock: () -> TimeInterval

    static func system() -> FacilityAlertSources {
        FacilityAlertSources(
            readDisk: FacilitySystemReader.readDisk,
            readMemoryPressure: FacilitySystemReader.readMemoryPressure,
            readMemoryUsage: FacilitySystemReader.readMemoryUsage,
            readThermalState: { FacilityThermalState(ProcessInfo.processInfo.thermalState) },
            readTemperature: FacilitySystemReader.readBatteryTemperature,
            observeEvents: FacilitySystemReader.observeEvents,
            scheduleRepeating: FacilitySystemReader.scheduleRepeating,
            runWorker: { FacilitySystemReader.worker.async(execute: $0) },
            clock: { ProcessInfo.processInfo.systemUptime })
    }
}

/// Owns the facility alert list. Main-thread only. While disabled it holds no
/// timer, observer or worker job. While enabled: memory pressure, thermal state,
/// mounts and wake are event-driven; one slow timer re-reads everything (disk
/// has no change event). Readings are taken off the main thread.
///
/// Raising is edge-triggered: an alert is listed when its condition starts and
/// never again while that condition lasts, so a dismissed alert stays away
/// until the condition clears and then occurs again. A condition clears only
/// after `recoveryHold` seconds of recovered readings (disk also needs
/// `diskClearFreeFraction` free); a recovered alert leaves the list.
final class FacilityAlertMonitor {
    static let pollInterval: TimeInterval = 60
    static let diskRaiseFreeFraction = 0.10
    static let diskClearFreeFraction = 0.12
    static let recoveryHold: TimeInterval = 120

    /// Listed alerts in raise order (the tag stack order after Recording mode).
    private(set) var alerts: [FacilityAlert] = []
    /// The latest sample. Not covered by `onChange`.
    private(set) var readings = FacilityReadings()
    /// Called on the main queue after every change to `alerts` or the hints below.
    var onChange: (() -> Void)?

    /// Bound to the Display → `Facility alerts` toggle. Off clears every alert
    /// and stops all monitoring; on starts it and checks every source at once.
    var isEnabled = false {
        didSet {
            precondition(Thread.isMainThread)
            guard oldValue != isEnabled else { return }
            if isEnabled { start() } else { stop() }
        }
    }

    var newAlerts: [FacilityAlert] { alerts.filter { $0.isNew } }
    func alert(_ kind: FacilityAlertKind) -> FacilityAlert? { alerts.first { $0.kind == kind } }

    /// Side effects (spec §3.29). They follow the condition, not the list, so a
    /// dismissed alert keeps them until the condition clears or the toggle is off.
    /// Storage capacity cells turn red.
    var isStorageLow: Bool { conditions[.disk]?.isActive == true }
    /// Battery's temperature reads orange.
    var isTemperatureHigh: Bool { conditions[.heat]?.isActive == true }
    /// The battery temperature read while hot; nil otherwise.
    var temperatureCelsius: Double? { isTemperatureHigh ? readings.thermal?.temperatureCelsius : nil }

    private struct Condition { var isActive = false; var recoverySince: TimeInterval? }
    private enum Signal { case raise, hold, recover }
    private struct Published: Equatable {
        let alerts: [FacilityAlert], storageLow: Bool, temperatureHigh: Bool, temperature: Double?
    }

    private let sources: FacilityAlertSources
    private var conditions: [FacilityAlertKind: Condition] = [:]
    private var cancelTimer: FacilityAlertSources.Cancel?
    private var cancelEvents: FacilityAlertSources.Cancel?
    private var generation = 0
    private var sampling = false
    private var needsSample = false
    private var eventPressure: FacilityMemoryPressure?

    init(sources: FacilityAlertSources = .system()) {
        precondition(Thread.isMainThread)
        self.sources = sources
    }

    deinit { cancelTimer?(); cancelEvents?() }

    /// Re-read every source now, e.g. after the boot log (§3.28). Ignored while off.
    func checkNow() {
        precondition(Thread.isMainThread)
        requestSample()
    }

    /// The user crossed the tag out. It stays away while its condition lasts.
    func dismiss(_ kind: FacilityAlertKind) {
        precondition(Thread.isMainThread)
        mutate { alerts.removeAll { $0.kind == kind } }
    }

    /// The banner for this alert was shown (or deliberately skipped).
    func markPresented(_ kind: FacilityAlertKind) {
        precondition(Thread.isMainThread)
        mutate {
            for index in alerts.indices where alerts[index].kind == kind { alerts[index].isNew = false }
        }
    }

    private func start() {
        generation += 1
        let token = generation
        cancelEvents = sources.observeEvents { [weak self] event in
            guard let self, self.isEnabled, self.generation == token else { return }
            if case .memoryPressure(let level) = event { self.eventPressure = level }
            self.requestSample()
        }
        cancelTimer = sources.scheduleRepeating(Self.pollInterval) { [weak self] in
            guard let self, self.isEnabled, self.generation == token else { return }
            self.requestSample()
        }
        requestSample()
    }

    private func stop() {
        generation += 1
        cancelTimer?(); cancelTimer = nil
        cancelEvents?(); cancelEvents = nil
        needsSample = false; eventPressure = nil
        mutate { alerts = []; conditions = [:]; readings = FacilityReadings() }
    }

    private func requestSample() {
        guard isEnabled else { return }
        guard !sampling else { needsSample = true; return }
        sampling = true; needsSample = false
        let token = generation, sources = self.sources, pressure = eventPressure
        let wantsUsage = conditions[.memory]?.isActive == true, wantsTemperature = conditions[.heat]?.isActive == true
        sources.runWorker { [weak self] in
            let result = Self.read(sources, eventPressure: pressure, wantsMemoryUsage: wantsUsage,
                                   wantsTemperature: wantsTemperature)
            Self.onMain { [weak self] in
                guard let self else { return }
                self.sampling = false
                if self.isEnabled, self.generation == token { self.apply(result) }
                if self.needsSample { self.requestSample() }
            }
        }
    }

    private static func read(_ sources: FacilityAlertSources, eventPressure: FacilityMemoryPressure?,
                             wantsMemoryUsage: Bool, wantsTemperature: Bool) -> FacilityReadings {
        var result = FacilityReadings(disk: sources.readDisk())
        if let pressure = sources.readMemoryPressure() ?? eventPressure {
            let usage = pressure != .normal || wantsMemoryUsage ? sources.readMemoryUsage() : nil
            result.memory = FacilityMemoryReading(pressure: pressure, usage: usage)
        }
        let state = sources.readThermalState()
        let celsius = state >= .serious || wantsTemperature ? sources.readTemperature() : nil
        result.thermal = FacilityThermalReading(state: state, temperatureCelsius: celsius)
        return result
    }

    private func apply(_ sample: FacilityReadings) {
        let now = sources.clock()
        mutate {
            // An unavailable source keeps its last reading and its alert state.
            readings.disk = sample.disk ?? readings.disk
            readings.memory = sample.memory ?? readings.memory
            readings.thermal = sample.thermal ?? readings.thermal
            if let disk = sample.disk { step(.disk(disk), at: now) }
            if let memory = sample.memory { step(.memory(memory), at: now) }
            if let thermal = sample.thermal { step(.heat(thermal), at: now) }
        }
    }

    private func step(_ measurement: FacilityAlert.Measurement, at now: TimeInterval) {
        let probe = FacilityAlert(measurement: measurement, isNew: true), kind = probe.kind
        var condition = conditions[kind] ?? Condition()
        switch Self.signal(measurement) {
        case .raise:
            condition.recoverySince = nil
            if !condition.isActive {
                condition.isActive = true
                alerts.removeAll { $0.kind == kind }
                alerts.append(probe)
            }
        case .hold:
            condition.recoverySince = nil
        case .recover:
            guard condition.isActive else { break }
            let since = condition.recoverySince ?? now
            condition.recoverySince = since
            if now - since >= Self.recoveryHold {
                condition = Condition()
                alerts.removeAll { $0.kind == kind }
            }
        }
        conditions[kind] = condition
        for index in alerts.indices where alerts[index].kind == kind { alerts[index].measurement = measurement }
    }

    private static func signal(_ measurement: FacilityAlert.Measurement) -> Signal {
        switch measurement {
        case .disk(let disk):
            if disk.freeFraction < diskRaiseFreeFraction { return .raise }
            return disk.freeFraction >= diskClearFreeFraction ? .recover : .hold
        case .memory(let memory): return memory.pressure == .normal ? .recover : .raise
        case .heat(let heat): return heat.state >= .serious ? .raise : .recover
        }
    }

    private var published: Published {
        Published(alerts: alerts, storageLow: isStorageLow, temperatureHigh: isTemperatureHigh,
                  temperature: temperatureCelsius)
    }

    private func mutate(_ change: () -> Void) {
        let before = published
        change()
        if published != before { onChange?() }
    }

    private static func onMain(_ action: @escaping () -> Void) {
        if Thread.isMainThread { action() } else { DispatchQueue.main.async(execute: action) }
    }
}

/// Documented system interfaces only: statfs, the memorystatus sysctl, Mach VM
/// statistics, ProcessInfo's thermal state and one AppleSmartBattery property.
enum FacilitySystemReader {
    static let worker = DispatchQueue(label: "EndfieldCharge.facility.samples", qos: .utility)

    /// The startup Data filesystem, as Storage reads it: f_bavail is the free
    /// capacity of the shared APFS container.
    static func readDisk() -> FacilityDiskReading? {
        let dataPath = "/System/Volumes/Data"
        let path = FileManager.default.fileExists(atPath: dataPath) ? dataPath : "/"
        var statistics = statfs()
        guard statfs(path, &statistics) == 0,
              let blocks = Int64(exactly: statistics.f_blocks),
              let free = Int64(exactly: statistics.f_bavail) else { return nil }
        let total = blocks.multipliedReportingOverflow(by: Int64(statistics.f_bsize))
        let available = free.multipliedReportingOverflow(by: Int64(statistics.f_bsize))
        guard !total.overflow, !available.overflow else { return nil }
        let name = (try? URL(fileURLWithPath: path).resourceValues(forKeys: [.volumeNameKey]).volumeName)
            ?? FileManager.default.displayName(atPath: "/")
        return FacilityDiskReading(volumeName: name, totalBytes: total.partialValue, availableBytes: available.partialValue)
    }

    static func readMemoryPressure() -> FacilityMemoryPressure? {
        var level: Int32 = 0
        var size = MemoryLayout<Int32>.size
        guard sysctlbyname("kern.memorystatus_vm_pressure_level", &level, &size, nil, 0) == 0,
              size == MemoryLayout<Int32>.size, level > 0 else { return nil }
        return FacilityMemoryPressure(kernelLevel: UInt(level))
    }

    /// Anonymous resident + wired + compressor pages, the Activity module's "used".
    static func readMemoryUsage() -> FacilityMemoryUsage? {
        let host = mach_host_self(); defer { mach_port_deallocate(mach_task_self_, host) }
        var info = vm_statistics64_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64_data_t>.size / MemoryLayout<integer_t>.size)
        let expected = count
        let status = withUnsafeMutablePointer(to: &info) { pointer in
            pointer.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                host_statistics64(host, HOST_VM_INFO64, $0, &count)
            }
        }
        var pageSize: vm_size_t = 0
        guard status == KERN_SUCCESS, count >= expected,
              host_page_size(host, &pageSize) == KERN_SUCCESS, pageSize > 0 else { return nil }
        let anonymous = UInt64(info.internal_page_count), purgeable = UInt64(info.purgeable_count)
        let pages = (anonymous - min(anonymous, purgeable)).addingReportingOverflow(UInt64(info.wire_count))
        let all = pages.partialValue.addingReportingOverflow(UInt64(info.compressor_page_count))
        let bytes = all.partialValue.multipliedReportingOverflow(by: UInt64(pageSize))
        guard !pages.overflow, !all.overflow, !bytes.overflow else { return nil }
        return FacilityMemoryUsage(usedBytes: bytes.partialValue, totalBytes: ProcessInfo.processInfo.physicalMemory)
    }

    static func readBatteryTemperature() -> Double? {
        // Zero is the default IOKit main port. One numeric property is read.
        let service = IOServiceGetMatchingService(0, IOServiceMatching("AppleSmartBattery"))
        guard service != 0 else { return nil }
        defer { IOObjectRelease(service) }
        let value = IORegistryEntryCreateCFProperty(service, "Temperature" as CFString, kCFAllocatorDefault, 0)?
            .takeRetainedValue()
        return FacilityThermalReading.batteryCelsius(registryValue: value)
    }

    static func observeEvents(_ handler: @escaping (FacilityAlertEvent) -> Void) -> FacilityAlertSources.Cancel {
        let pressure = DispatchSource.makeMemoryPressureSource(eventMask: [.normal, .warning, .critical], queue: .main)
        pressure.setEventHandler { [weak pressure] in
            guard let event = pressure?.data, let level = FacilityMemoryPressure(kernelLevel: event.rawValue) else { return }
            handler(.memoryPressure(level))
        }
        pressure.resume()
        let center = NotificationCenter.default
        let thermal = center.addObserver(forName: ProcessInfo.thermalStateDidChangeNotification, object: nil,
                                         queue: .main) { _ in handler(.thermalStateChanged) }
        let workspace = NSWorkspace.shared.notificationCenter
        let workspaceTokens = [
            (NSWorkspace.didMountNotification, FacilityAlertEvent.volumesChanged),
            (NSWorkspace.didUnmountNotification, .volumesChanged),
            (NSWorkspace.didWakeNotification, .didWake)
        ].map { name, event in workspace.addObserver(forName: name, object: nil, queue: .main) { _ in handler(event) } }
        return {
            pressure.setEventHandler {}; pressure.cancel()
            center.removeObserver(thermal)
            workspaceTokens.forEach(workspace.removeObserver)
        }
    }

    static func scheduleRepeating(_ interval: TimeInterval, _ action: @escaping () -> Void) -> FacilityAlertSources.Cancel {
        let timer = DispatchSource.makeTimerSource(queue: .main)
        timer.schedule(deadline: .now() + interval, repeating: interval, leeway: .seconds(5))
        timer.setEventHandler(handler: action); timer.resume()
        return { timer.setEventHandler {}; timer.cancel() }
    }
}
