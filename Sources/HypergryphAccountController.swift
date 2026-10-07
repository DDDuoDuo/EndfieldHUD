import AppKit

/// One retained owner for both regions. The HUD's existing visible clock calls
/// tick; this service has no timer, observer loop, or hidden refresh worker.
final class HypergryphAccountController {
    /// Application policy, not a claimed provider limit. Visible HUD ticks
    /// project recovery locally between ten-minute network samples.
    static let automaticRefreshInterval: TimeInterval = 600
    struct RegionRecord: Codable {
        var linked = false
        var requiresReconnect = false
        var roles: [HypergryphRole] = []
        var selectedRoleID: String?
        var snapshots: [String: HypergryphProfileSnapshot] = [:]
        var bindingsAt: Date?
    }
    struct Cache: Codable {
        var version = 1
        var region: HypergryphAccountRegion = .mainland
        var header = "endfield"
        var syncProfile = false
        var syncAvatar = false
        var records: [String: RegionRecord] = [:]
    }
    var onEvent: ((String) -> Void)?
    private var manualRefreshPending = false
    private(set) var cache: Cache
    private let fileURL: URL?
    /// A rejected existing cache may belong to a newer app or be recoverable.
    /// Continue in memory without replacing those original bytes.
    private let cacheWritesAllowed: Bool
    private let api: HypergryphAccountServing
    private let vault: HypergryphAccountCredentialVault
    private weak var profile: UserProfileStore?
    private let vaultQueue = DispatchQueue(label: "EndfieldHUD.account.vault", qos: .utility)
    private var credentials: [HypergryphAccountRegion: HypergryphCredentials] = [:]
    private var credentialDates: [HypergryphAccountRegion: Date] = [:]
    private var request: HypergryphAccountRequest?
    private var generation = 0
    // User account mutations outlive a presentation generation. Closing the HUD
    // may cancel reads, but cannot undo an already-requested Keychain mutation.
    private var accountRevisions: [HypergryphAccountRegion: Int] = [:]
    private var pendingDisconnects: [HypergryphAccountRegion: Int] = [:]
    private var pendingCredentialCommits = 0
    private var busy = false
    private var visible = false
    private var moduleVisible = false
    private var nextRefreshDates: [HypergryphAccountRegion: Date] = [:]
    private var lastAttemptDates: [HypergryphAccountRegion: Date] = [:]
    private var failureCounts: [HypergryphAccountRegion: Int] = [:]
    private var nextRefresh: Date {
        get { nextRefreshDates[region] ?? .distantPast }
        set { nextRefreshDates[region] = newValue }
    }
    private var lastAttempt: Date {
        get { lastAttemptDates[region] ?? .distantPast }
        set { lastAttemptDates[region] = newValue }
    }
    private var failures: Int {
        get { failureCounts[region] ?? 0 }
        set { failureCounts[region] = newValue }
    }
    private var statusMessage = ""
    private var observers: [UUID: () -> Void] = [:]
    private var login: HypergryphAccountLogin?
    private var avatar: HypergryphAvatarLoader?
    private var avatarURL: URL?
    private var profileObserver: UUID?
    private var knownAvatarFilename: String?
    private var applyingGameAvatar = false
    private var now: () -> Date
    var isPresentingLogin: Bool { login?.isPresenting == true }
    var isPresentingAccountPanel: Bool { isPresentingLogin || pendingCredentialCommits > 0 }
    var region: HypergryphAccountRegion { cache.region }
    var record: RegionRecord { cache.records[region.rawValue] ?? RegionRecord() }
    var headerMode: HUDAccountPresentation.HeaderMode { .init(rawValue: cache.header) ?? .workMode }
    var gameSyncActive: Bool { cache.syncProfile && record.linked && selectedRole?.game == .endfield }
    var selectedRole: HypergryphRole? { record.roles.first { $0.id == record.selectedRoleID } }
    var selectedSnapshot: HypergryphProfileSnapshot? { record.selectedRoleID.flatMap { record.snapshots[$0] } }
    var hasActiveRequest: Bool { busy || pendingDisconnects[region] != nil }

    static func applicationFile() -> URL? {
        if CommandLine.arguments.contains(where: { $0 == "--ui-test" || $0.hasSuffix("smoke-test") || $0.hasPrefix("--render-") }) { return nil }
        return UserProfileStore.applicationDirectory().deletingLastPathComponent().appendingPathComponent("Account/profile-cache.json")
    }
    init(profile: UserProfileStore? = nil, fileURL: URL? = nil,
         api: HypergryphAccountServing = HypergryphAccountAPI(),
         vault: HypergryphAccountCredentialVault = HypergryphMemoryCredentialVault(), now: @escaping () -> Date = Date.init) {
        self.profile = profile; self.fileURL = fileURL; self.api = api; self.vault = vault; self.now = now
        if let fileURL, let size = try? fileURL.resourceValues(forKeys: [.fileSizeKey]).fileSize,
           size <= 1_048_576, let data = try? Data(contentsOf: fileURL), let saved = try? JSONDecoder().decode(Cache.self, from: data), saved.version == 1 {
            cache = saved; cacheWritesAllowed = true
        } else {
            cache = Cache()
            cacheWritesAllowed = fileURL.map { !FileManager.default.fileExists(atPath: $0.path)
                && (try? FileManager.default.attributesOfItem(atPath: $0.path)) == nil } ?? false
        }
        knownAvatarFilename = profile?.profile.avatarFilename
        profileObserver = profile?.observe { [weak self] in
            guard let self else { return }
            let filename = self.profile?.profile.avatarFilename
            guard filename != self.knownAvatarFilename else { return }
            self.knownAvatarFilename = filename
            if !self.applyingGameAvatar && self.cache.syncAvatar {
                self.cache.syncAvatar = false; self.avatar?.cancel(); self.avatar = nil; self.avatarURL = nil
                self.changed(save: true)
            }
        }
        // A previously enabled sync remains authoritative after an app update.
        // Reuse its cached snapshot without a startup API or credential read.
        applyProfile()
    }
    deinit { request?.cancel(); avatar?.cancel(); if let profileObserver { profile?.removeObserver(profileObserver) } }
    @discardableResult func observe(_ callback: @escaping () -> Void) -> UUID { let id = UUID(); observers[id] = callback; return id }
    func removeObserver(_ id: UUID) { observers.removeValue(forKey: id) }
    private func changed(save: Bool = false) {
        if save, cacheWritesAllowed, let fileURL {
            do {
                let data = try JSONEncoder().encode(cache)
                try FileManager.default.createDirectory(at: fileURL.deletingLastPathComponent(), withIntermediateDirectories: true)
                try data.write(to: fileURL, options: [.atomic])
                try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: fileURL.path)
            } catch { statusMessage = L10n.text("Profile cache could not be saved.", "无法保存账户缓存。") }
        }
        Array(observers.values).forEach { $0() }
    }
    func setVisible(_ value: Bool, accountModule: Bool) {
        moduleVisible = accountModule
        guard visible != value else { return }; visible = value
        if !value {
            generation += 1; request?.cancel(); request = nil; avatar?.cancel(); avatar = nil
            busy = false; login?.cancel(); login = nil
        }
    }
    func tick() {
        guard visible, record.linked, !record.requiresReconnect, !busy,
              moduleVisible || headerMode == .endfield || headerMode == .arknights,
              now() >= nextRefresh else { return }
        refresh()
    }
    var presentation: HUDAccountPresentation {
        var result = HUDAccountPresentation()
        result.region = region == .mainland ? .china : .global
        result.isLinked = record.linked
        result.statusMessage = record.requiresReconnect ? L10n.text("Sign in again to continue syncing.", "请重新登录以继续同步。") : statusMessage
        result.status = isPresentingLogin ? .connecting : hasActiveRequest ? .refreshing : !result.statusMessage.isEmpty ? .failed : record.linked ? .connected : .disconnected
        result.roles = record.roles.map { .init(id: $0.id,
            title: ($0.game == .endfield ? "Endfield" : "Arknights") + " · " + ($0.name ?? $0.roleID), subtitle: $0.serverName ?? "") }
        result.selectedRoleID = record.selectedRoleID
        result.headerMode = headerMode; result.syncProfile = cache.syncProfile; result.syncAvatar = cache.syncAvatar
        result.accountName = selectedSnapshot?.name ?? selectedRole?.name ?? ""
        if let snapshot = selectedSnapshot {
            result.lastSync = L10n.text("Updated ", "更新于 ") + Self.dateFormatter.string(from: snapshot.observedAt)
        }
        return result
    }
    private static let dateFormatter: DateFormatter = { let f = DateFormatter(); f.dateFormat = "MM/dd HH:mm:ss"; return f }()
    func sanityPresentation(at date: Date? = nil) -> HypergryphSanityPresentation? {
        guard record.linked else { return nil }
        let game: HypergryphGame? = headerMode == .endfield ? .endfield : headerMode == .arknights ? .arknights : nil
        guard let game else { return nil }
        let role = selectedRole?.game == game ? selectedRole : record.roles.first { $0.game == game && $0.isAvailable }
        guard let role, let snapshot = record.snapshots[role.id] else { return nil }
        let date = date ?? now()
        return snapshot.sanityPresentation(at: date, isRefreshing: hasActiveRequest,
            refreshAvailable: visible && !busy && !record.requiresReconnect && pendingDisconnects[region] == nil
                && date.timeIntervalSince(lastAttempt) >= 5)
    }
    func gaugeValue(work: WorkModeSnapshot) -> (String, String, Bool) {
        if headerMode == .hidden { return ("", "", false) }
        let game: HypergryphGame? = headerMode == .endfield ? .endfield : headerMode == .arknights ? .arknights : nil
        if record.linked, game != nil {
            let stamina = sanityPresentation()
            let value = stamina.map { "\($0.current) / \($0.maximum)" } ?? "— / —"
            return (value, headerMode.title + " · " + L10n.text("Sanity", "理智") + " " + value, true)
        }
        let remaining = work.kind == .countdown ? max(0, Int(ceil(work.remaining / 60))) : max(0, Int(work.elapsed / 60))
        let total = max(0, Int(ceil(work.duration / 60)))
        let value = "\(remaining) / \(total)"
        return (value, L10n.text("Work Mode minutes", "工作模式分钟") + " " + value, true)
    }
    func perform(_ action: HUDAccountCanvas.Action, window: NSWindow?) {
        switch action {
        case .connect(let region): connect(region: region == .china ? .mainland : .global, window: window)
        case .refresh: refresh(manual: true)
        case .disconnect: disconnect()
        case .selectRegion(let choice):
            guard !busy else { return }; cancelRequests(); cache.region = choice == .china ? .mainland : .global
            statusMessage = ""; nextRefresh = .distantPast; avatarURL = nil; applyProfile(); onEvent?("settings"); changed(save: true); tick()
        case .selectRole(let id):
            guard record.roles.contains(where: { $0.id == id }), !busy else { return }
            cache.records[region.rawValue, default: RegionRecord()].selectedRoleID = id
            avatarURL = nil; applyProfile(); onEvent?("settings"); changed(save: true); nextRefresh = .distantPast; tick()
        case .selectHeaderMode(let mode): cache.header = mode.rawValue; onEvent?("settings"); changed(save: true); nextRefresh = .distantPast; tick()
        case .setSyncProfile(let enabled): cache.syncProfile = enabled; if enabled { applyProfile() }; onEvent?("settings"); changed(save: true)
        case .setSyncAvatar(let enabled): cache.syncAvatar = enabled; avatarURL = nil
            if enabled { applyAvatar() } else { avatar?.cancel(); avatar = nil }; onEvent?("settings"); changed(save: true)
        }
    }
    private func connect(region: HypergryphAccountRegion, window: NSWindow?) {
        guard !busy, !isPresentingLogin else { return }
        cancelRequests(); cache.region = region; statusMessage = ""
        let login = HypergryphAccountLogin(); self.login = login
        login.present(region: region, relativeTo: window) { [weak self, weak window] result in
            guard let self else { return }; self.login = nil
            // The login owns its separate key window. Return focus only while
            // the originating HUD still exists and is actually presented.
            defer { if self.visible, window?.isVisible == true { window?.makeKeyAndOrderFront(nil) } }
            switch result {
            case .success(let value): self.acceptLogin(cred: value.cred, region: value.region,
                signingToken: value.signingToken, deviceID: value.deviceID)
            case .failure(let error):
                if error != .cancelled { self.statusMessage = L10n.text("Sign-in could not finish. Please try again.", "登录未完成，请重试。") }
                self.changed()
            }
        }
        changed()
    }
    /// Internal seam for isolated tests; credentials come only from our explicit official-site login.
    func acceptLogin(cred: String, region: HypergryphAccountRegion, signingToken: String? = nil, deviceID: String? = nil) {
        cancelRequests(); cache.region = region
        let mutation = advanceAccountRevision(region)
        if pendingDisconnects.removeValue(forKey: region) != nil {
            // A new login supersedes the pending unlink, but must not revive
            // the old identity if that new login itself fails.
            credentials.removeValue(forKey: region); credentialDates.removeValue(forKey: region)
            cache.records.removeValue(forKey: region.rawValue)
        }
        lastAttempt = now(); busy = true; statusMessage = ""; changed()
        let token = generation
        let completion: (Result<HypergryphCredentials, HypergryphAPIError>) -> Void = { [weak self] result in
            guard let self, self.generation == token, self.accountRevisions[region] == mutation else { return }
            switch result {
            case .failure(let error): self.fail(error)
            case .success(let credentials):
                self.commitLoginCredentials(credentials, region: region, token: token, mutation: mutation)
            }
        }
        if let signingToken, let deviceID {
            guard HypergryphAccountLoginPolicy.acceptsCredential(cred),
                  HypergryphAccountLoginPolicy.acceptsCredential(signingToken),
                  HypergryphAccountLoginPolicy.acceptsCredential(deviceID) else { fail(.invalidCredentials); return }
            // Official login has already issued this complete signing pair.
            // Preserve it through the existing commit and first reads; rotating
            // it immediately adds a request before it has ever been used.
            commitLoginCredentials(HypergryphCredentials(cred: cred, signingToken: signingToken, deviceID: deviceID),
                                   region: region, token: token, mutation: mutation)
        } else if signingToken == nil && deviceID == nil {
            // Existing fixture callers and legacy integrations remain source
            // compatible. The official login bridge requires the full context.
            request = api.refreshCredentials(cred: cred, region: region, completion: completion)
        } else { fail(.invalidCredentials) }
    }
    private func advanceAccountRevision(_ region: HypergryphAccountRegion) -> Int {
        let value = (accountRevisions[region] ?? 0) &+ 1
        accountRevisions[region] = value; return value
    }
    private func commitLoginCredentials(_ value: HypergryphCredentials, region: HypergryphAccountRegion, token: Int, mutation: Int) {
        // An explicit sign-in may represent another person. Old community IDs
        // must never be retried with this new credential, even if saving or the
        // first binding read fails. The independently owned personal card stays.
        credentials.removeValue(forKey: region); credentialDates.removeValue(forKey: region)
        cache.records[region.rawValue] = RegionRecord()
        changed(save: true)
        let vault = vault
        pendingCredentialCommits += 1
        vaultQueue.async { [weak self] in
            let result = Result { try vault.save(value, region: region) }
            DispatchQueue.main.async {
                guard let self else { return }
                self.pendingCredentialCommits = max(0, self.pendingCredentialCommits - 1)
                guard self.accountRevisions[region] == mutation else { return }
                switch result {
                case .success:
                    self.credentials[region] = value; self.credentialDates[region] = self.now()
                    self.cache.records[region.rawValue, default: RegionRecord()].linked = true
                    self.nextRefreshDates[region] = .distantPast
                    self.onEvent?("linked"); self.changed(save: true)
                    if self.generation == token, self.region == region, self.visible {
                        self.fetchBindings(value, token: token)
                    } else if self.region == region { self.busy = false }
                case .failure:
                    if self.region == region, self.generation == token {
                        self.busy = false
                        self.statusMessage = L10n.text("Keychain could not be updated. Try again.", "无法更新钥匙串，请重试。")
                        self.changed()
                    }
                }
            }
        }
    }
    private func cancelRequests() { manualRefreshPending = false; generation += 1; request?.cancel(); request = nil; avatar?.cancel(); avatar = nil; busy = false }
    func disconnect() {
        let region = region
        guard pendingDisconnects[region] == nil else { return }
        cancelRequests(); login?.cancel(); login = nil
        let mutation = advanceAccountRevision(region), vault = vault
        pendingDisconnects[region] = mutation
        busy = true; changed()
        vaultQueue.async { [weak self] in
            let result = Result { try vault.remove(region: region) }
            DispatchQueue.main.async {
                guard let self, self.accountRevisions[region] == mutation else { return }
                self.pendingDisconnects.removeValue(forKey: region)
                if self.region == region { self.busy = false }
                switch result {
                case .success:
                    self.credentials.removeValue(forKey: region); self.credentialDates.removeValue(forKey: region)
                    self.cache.records.removeValue(forKey: region.rawValue)
                    self.nextRefreshDates.removeValue(forKey: region); self.lastAttemptDates.removeValue(forKey: region)
                    self.failureCounts.removeValue(forKey: region)
                    if self.region == region { self.avatarURL = nil; self.statusMessage = "" }
                    self.onEvent?("unlinked"); self.changed(save: true)
                case .failure:
                    if self.region == region {
                        self.statusMessage = L10n.text("Keychain could not be updated. Try again.", "无法更新钥匙串，请重试。")
                        self.changed()
                    }
                }
            }
        }
    }
    func refresh(manual: Bool = false) {
        guard visible, record.linked, !busy, !record.requiresReconnect, pendingDisconnects[region] == nil,
              now().timeIntervalSince(lastAttempt) >= 5 else { return }
        if !manual {
            let roles = rolesNeededForPresentation
            let dates = roles.compactMap { record.snapshots[$0.id]?.observedAt }
            if !dates.isEmpty, dates.count == roles.count,
               let oldest = dates.min(), now().timeIntervalSince(oldest) >= 0,
               now().timeIntervalSince(oldest) < Self.automaticRefreshInterval {
                nextRefresh = oldest.addingTimeInterval(Self.automaticRefreshInterval)
                return
            }
        }
        manualRefreshPending = manual
        lastAttempt = now(); busy = true; statusMessage = ""; let token = generation, region = region
        changed()
        if let credentials = credentials[region] { refreshSigningIfNeeded(credentials, token: token); return }
        let vault = vault
        vaultQueue.async { [weak self] in
            let result = Result { try vault.load(region: region) }
            DispatchQueue.main.async {
                guard let self, self.generation == token else { return }
                guard case .success(let value?) = result else { self.fail(.authenticationExpired); return }
                self.credentials[region] = value; self.refreshSigningIfNeeded(value, token: token)
            }
        }
    }
    private func refreshSigningIfNeeded(_ credentials: HypergryphCredentials, token: Int) {
        if let date = credentialDates[region], now().timeIntervalSince(date) < 1200 { fetchBindingsIfNeeded(credentials, token: token); return }
        request = api.refreshCredentials(credentials: credentials, region: region) { [weak self] result in
            guard let self, self.generation == token else { return }
            switch result {
            case .failure(let error): self.fail(error)
            case .success(let value): self.saveCredentials(value, region: self.region, token: token) { [weak self] in self?.fetchBindingsIfNeeded(value, token: token) }
            }
        }
    }
    private func saveCredentials(_ value: HypergryphCredentials, region: HypergryphAccountRegion, token: Int, completion: @escaping () -> Void) {
        let vault = vault
        pendingCredentialCommits += 1
        vaultQueue.async { [weak self] in
            let result = Result { try vault.save(value, region: region) }
            DispatchQueue.main.async {
                guard let self else { return }
                self.pendingCredentialCommits = max(0, self.pendingCredentialCommits - 1)
                guard self.generation == token else { return }
                guard case .success = result else {
                    self.busy = false; self.statusMessage = L10n.text("Keychain could not be updated. Try again.", "无法更新钥匙串，请重试。"); self.changed(); return
                }
                self.credentials[region] = value; self.credentialDates[region] = self.now(); completion()
            }
        }
    }
    private func fetchBindingsIfNeeded(_ credentials: HypergryphCredentials, token: Int) {
        if let date = record.bindingsAt, now().timeIntervalSince(date) < 300, !record.roles.isEmpty { fetchProfiles(credentials, token: token); return }
        fetchBindings(credentials, token: token)
    }
    private func fetchBindings(_ credentials: HypergryphCredentials, token: Int) {
        request = api.bindings(credentials: credentials, region: region) { [weak self] result in
            guard let self, self.generation == token else { return }
            switch result {
            case .failure(let error): self.fail(error)
            case .success(let roles):
                var record = self.record
                record.roles = Array(roles.filter { $0.region == self.region && $0.isAvailable }.prefix(64)); record.bindingsAt = self.now()
                if !record.roles.contains(where: { $0.id == record.selectedRoleID }) {
                    record.selectedRoleID = record.roles.first { $0.game == .endfield && $0.isDefault }?.id
                        ?? record.roles.first { $0.game == .endfield }?.id ?? record.roles.first?.id
                }
                let valid = Set(record.roles.map(\.id)); record.snapshots = record.snapshots.filter { valid.contains($0.key) }
                self.cache.records[self.region.rawValue] = record; self.changed(save: true)
                self.fetchProfiles(credentials, token: token)
            }
        }
    }
    private var rolesNeededForPresentation: [HypergryphRole] {
        var roles = selectedRole.map { [$0] } ?? []
        let game: HypergryphGame? = headerMode == .endfield ? .endfield : headerMode == .arknights ? .arknights : nil
        if let game, !roles.contains(where: { $0.game == game }), let role = record.roles.first(where: { $0.game == game }) { roles.append(role) }
        return roles
    }
    private func fetchProfiles(_ credentials: HypergryphCredentials, token: Int) {
        fetchNext(rolesNeededForPresentation, credentials: credentials, token: token)
    }
    private func fetchNext(_ roles: [HypergryphRole], credentials: HypergryphCredentials, token: Int) {
        guard let role = roles.first else {
            busy = false; request = nil; failures = 0; nextRefresh = now().addingTimeInterval(Self.automaticRefreshInterval)
            if record.roles.isEmpty { statusMessage = L10n.text("No linked game account found.", "未找到已绑定的游戏账户。") }
            applyProfile(); applyAvatar()
            if manualRefreshPending && selectedSnapshot != nil && statusMessage.isEmpty { onEvent?("synced") }
            manualRefreshPending = false
            changed(save: true); return
        }
        request = api.profile(role: role, credentials: credentials) { [weak self] result in
            guard let self, self.generation == token else { return }
            switch result {
            case .failure(let error): self.fail(error)
            case .success(let snapshot):
                guard snapshot.role.id == role.id else { self.fail(.invalidResponse); return }
                self.cache.records[self.region.rawValue, default: RegionRecord()].snapshots[role.id] = snapshot
                self.fetchNext(Array(roles.dropFirst()), credentials: credentials, token: token)
            }
        }
    }
    private func fail(_ error: HypergryphAPIError) {
        manualRefreshPending = false
        busy = false; request = nil; failures = min(5, failures + 1)
        nextRefresh = now().addingTimeInterval(min(900, 60 * pow(2, Double(failures - 1))))
        if error == .authenticationExpired || error == .invalidCredentials {
            cache.records[region.rawValue, default: RegionRecord()].requiresReconnect = true
            statusMessage = L10n.text("Sign in again to continue syncing.", "请重新登录以继续同步。")
        } else { statusMessage = L10n.text("Sync unavailable. Saved data is preserved.", "暂时无法同步，已保留上次数据。") }
        changed(save: true)
    }
    private func applyProfile() {
        guard cache.syncProfile, record.linked, let snapshot = selectedSnapshot, snapshot.role.game == .endfield, let profile else { return }
        let identity = snapshot.personalProfileIdentity
        do { try profile.update { value in
            value.gamePlayerID = snapshot.role.roleID
            value.playerIDOverride = nil
            if let name = identity.name { value.name = name }
            if let tag = identity.tag { value.tag = tag }
            if let date = snapshot.createdAt {
                value.awakeningDate = date
                value.hasManualAwakeningDate = false
            }
            if let level = snapshot.level { value.permissionLevel = level }
            if let level = snapshot.worldLevel { value.explorationLevel = level }
            if let count = snapshot.operatorCount { value.operatorsCount = count }
            if let count = snapshot.weaponCount { value.weaponsCount = count }
            if let count = snapshot.documentCount { value.archivesCount = count }
        } } catch { statusMessage = L10n.text("The personal profile could not be saved.", "无法保存个人名片。") }
    }
    private func applyAvatar() {
        guard visible, cache.syncAvatar, record.linked, let snapshot = selectedSnapshot, snapshot.role.game == .endfield,
              let url = snapshot.avatarURL, url != avatarURL, let profile else { return }
        avatar?.cancel(); let loader = HypergryphAvatarLoader(); avatar = loader
        let token = generation
        loader.load(url: url) { [weak self, weak profile] result in
            guard let self, self.generation == token, self.cache.syncAvatar else { return }
            self.avatar = nil
            if case .success(let data) = result, let profile {
                let file = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldHUD-avatar-\(UUID().uuidString).png")
                defer { try? FileManager.default.removeItem(at: file) }
                self.applyingGameAvatar = true
                defer { self.applyingGameAvatar = false }
                do { try data.write(to: file); try profile.importImage(from: file, kind: .avatar); self.avatarURL = url }
                catch { self.statusMessage = L10n.text("Game avatar is unavailable.", "游戏头像暂不可用。"); self.changed() }
            }
        }
    }
}
