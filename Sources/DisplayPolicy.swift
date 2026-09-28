enum DisplayMode: String, CaseIterable {
    case always
    // Keep this saved raw value compatible with earlier releases. The mode now
    // presents both starting and stopping charge/power-connection transitions.
    case whenChargingStarts
}

/// The view controller owns the hide deadline; the policy only decides when a
/// new presentation starts. Battery-percentage updates never extend a deadline.
enum DisplayAction: Equatable {
    case hide
    case showPersistent
    case showTransient
    case keepCurrent
}

enum DisplayPolicy {
    static func action(
        for snapshot: BatterySnapshot,
        previous: BatterySnapshot?,
        mode: DisplayMode
    ) -> DisplayAction {
        guard snapshot.hasBattery else { return .hide }
        if mode == .always { return .showPersistent }

        // Starting the app must not manufacture a charging-state event.
        // Use the same baseline when changing display modes.
        // Likewise, recovering after an unavailable battery report establishes
        // a fresh baseline instead of producing a spurious connection popup.
        guard let previous = previous, previous.hasBattery else { return .hide }

        let connectionChanged = previous.isPluggedIn != snapshot.isPluggedIn
        let chargingChanged = previous.isCharging != snapshot.isCharging
        return connectionChanged || chargingChanged ? .showTransient : .keepCurrent
    }
}

// On system wake, refresh the existing monitor without clearing its last
// snapshot or the controller's previous snapshot. A real AC/charging transition
// during sleep is then shown once; an unchanged wake produces no new popup.
