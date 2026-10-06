import AppKit

/// No real login, Keychain, HTTP request, or existing profile is accessed.
enum HypergryphAccountControllerTests {
    static func run() -> Int {
        var checks = 0
        func check(_ condition: Bool, _ message: String) { checks += 1; if !condition { fatalError(message) } }
        func wait(_ predicate: () -> Bool) {
            let deadline = Date().addingTimeInterval(3)
            while !predicate() && Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.003)) }
            check(predicate(), "Fixture account operation must finish")
        }
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("AccountControllerTests-\(UUID().uuidString)")
        try! FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        var now = Date(timeIntervalSince1970: 1_800_000_000)
        let profile = try! UserProfileStore(directory: directory.appendingPathComponent("Profile"), now: now)
        let originalUID = profile.profile.uid, manualDate = Date(timeIntervalSince1970: 1_500_000_000)
        try! profile.update {
            $0.playerIDOverride = "MY-PLAYER-ID"; $0.hasManualAwakeningDate = true; $0.awakeningDate = manualDate
            $0.name = "Local"; $0.tag = "0042"; $0.introduction = "Keep this biography"
            $0.avatarFilename = "00000000-0000-0000-0000-000000000001.png"; $0.backgroundFilename = "00000000-0000-0000-0000-000000000002.png"
            $0.avatarZoom = 1.5; $0.backgroundZoom = 2; $0.permissionLevel = 55
            $0.explorationLevel = 6; $0.operatorsCount = 33; $0.weaponsCount = 44; $0.archivesCount = 55
        }
        let cacheURL = directory.appendingPathComponent("Account/profile-cache.json")
        let api = HypergryphControllerFixtureAPI(now: { now })
        let vault = HypergryphMemoryCredentialVault()
        let controller = HypergryphAccountController(profile: profile, fileURL: cacheURL, api: api, vault: vault, now: { now })
        var events: [String] = []; controller.onEvent = { events.append($0) }
        for _ in 0..<100 { controller.tick() }
        check(api.total == 0 && !controller.hasActiveRequest, "A hidden unlinked controller performs no API or credential work")
        controller.setVisible(true, accountModule: true)
        controller.acceptLogin(cred: "cn-scoped-credential", region: .mainland)
        wait { !controller.hasActiveRequest }
        check(events == ["linked"], "Successful consented authentication emits one identifier-free account event")
        check(controller.record.linked && controller.selectedSnapshot?.role.region == .mainland && api.total == 3,
              "An explicit login stores scoped credentials, fetches bindings, and then the selected role")
        check((try! vault.load(region: .mainland))?.cred == "cn-scoped-credential" && (try! vault.load(region: .global)) == nil,
              "The fixture credential vault is independently keyed by region")
        check(profile.profile.name == "Local" && profile.profile.gamePlayerID == nil, "Linking alone does not opt in to personal-card synchronization")
        let initial = api.total
        controller.refresh(manual: true); controller.tick()
        check(api.total == initial, "Initial login participates in the manual five-second cooldown")
        controller.perform(.setSyncProfile(true), window: nil)
        check(profile.profile.name == "Game Mainland" && profile.profile.gamePlayerID == "cn-role", "Explicit sync imports available Endfield identity and name")
        check(profile.profile.uid == originalUID && profile.profile.displayedUID == "MY-PLAYER-ID" && profile.profile.awakeningDate == manualDate,
              "Game sync preserves the original local UID plus explicit UID and wake-date overrides")
        check(profile.profile.tag == "0042" && profile.profile.introduction == "Keep this biography"
              && profile.profile.avatarFilename == "00000000-0000-0000-0000-000000000001.png" && profile.profile.backgroundFilename == "00000000-0000-0000-0000-000000000002.png"
              && profile.profile.avatarZoom == 1.5 && profile.profile.backgroundZoom == 2,
              "Sync preserves tag, biography, chosen images and their crop settings")
        check(profile.profile.permissionLevel == 23 && profile.profile.explorationLevel == 4
              && profile.profile.operatorsCount == 20 && profile.profile.weaponsCount == 12 && profile.profile.archivesCount == 7,
              "Present game levels and collection counts update the matching card fields")
        let bytes = try! Data(contentsOf: cacheURL), cacheText = String(decoding: bytes, as: UTF8.self)
        check(!cacheText.contains("cn-scoped-credential") && !cacheText.contains("fixture-signing-secret") && !cacheText.contains("signingToken") && !cacheText.contains("\"cred\""),
              "The persisted account cache contains no community credential or signing secret")
        let readback = HypergryphAccountController(fileURL: cacheURL, api: HypergryphControllerFixtureAPI(now: { now }), vault: HypergryphMemoryCredentialVault(), now: { now })
        check(readback.record.linked && readback.selectedRole?.id == controller.selectedRole?.id && readback.selectedSnapshot?.stamina?.current == 80,
              "Nonsecret account/profile snapshots survive a cache reload without querying Keychain")
        let reopenAPI = HypergryphControllerFixtureAPI(now: { now })
        let freshReopen = HypergryphAccountController(fileURL: cacheURL, api: reopenAPI,
            vault: HypergryphMemoryCredentialVault(), now: { now })
        freshReopen.setVisible(true, accountModule: true); freshReopen.tick()
        check(reopenAPI.total == 0 && !freshReopen.hasActiveRequest,
              "A fresh persisted snapshot avoids even credential access on a reopened HUD")
        controller.perform(.selectHeaderMode(.endfield), window: nil)
        let work = WorkModeSnapshot(kind: .countdown, phase: .running, duration: 1800, elapsed: 60)
        check(controller.gaugeValue(work: work).0 == "80 / 240", "The selected game's reported stamina drives the header")
        controller.setVisible(false, accountModule: false)
        now.addTimeInterval(120)
        for _ in 0..<100 { controller.tick(); controller.refresh(manual: true) }
        check(api.total == initial, "Closing the HUD stops automatic and manual-triggered refresh work")
        controller.setVisible(true, accountModule: false); controller.tick()
        wait { !controller.hasActiveRequest }
        check(api.total == initial, "Reopening a fresh account projects locally without an unnecessary network read")
        let afterResume = api.total
        now.addTimeInterval(479); for _ in 0..<600 { controller.tick() }
        check(api.total == afterResume, "Visible clock ticks do not bypass the ten-minute sample cadence")
        now.addTimeInterval(1); controller.tick(); wait { !controller.hasActiveRequest }
        check(api.total == afterResume + 2 && api.profiles.count == 2 && api.bindingRegions.count == 2,
              "At ten minutes one aged binding read and selected profile read refresh the account")
        api.sparse = true; now.addTimeInterval(5); controller.refresh(manual: true); wait { !controller.hasActiveRequest }
        check(profile.profile.permissionLevel == 23 && profile.profile.explorationLevel == 4
              && profile.profile.operatorsCount == 20 && profile.profile.weaponsCount == 12 && profile.profile.archivesCount == 7,
              "Sparse subsequent responses preserve existing local levels/counts instead of writing zeros")
        check(events.filter { $0 == "synced" }.count == 1, "Visible periodic reads are silent; only explicit refresh records an event")
        let afterManual = api.total; now.addTimeInterval(4.9); controller.refresh(manual: true)
        check(api.total == afterManual, "Manual refresh cannot repeat within five seconds")
        now.addTimeInterval(0.1); controller.refresh(manual: true); wait { !controller.hasActiveRequest }
        check(api.total == afterManual + 1, "Manual refresh becomes eligible at five seconds")
        api.sparse = false
        controller.acceptLogin(cred: "global-scoped-credential", region: .global,
                               signingToken: "global-fresh-signing", deviceID: "global-fresh-device")
        wait { !controller.hasActiveRequest }
        check(controller.selectedRole?.roleID == "global-role" && controller.record.roles.allSatisfy { $0.region == .global },
              "Global login selects only the Global account's roles")
        check((try! vault.load(region: .mainland))?.cred == "cn-scoped-credential" && (try! vault.load(region: .global))?.cred == "global-scoped-credential",
              "Linking Global never overwrites the Mainland credential")
        check(!api.refreshRegions.contains(.global) && api.bindingContexts.last?.signingToken == "global-fresh-signing",
              "Global fresh sign-in uses its own issued pair directly while legacy Mainland login remains refresh-compatible")
        check(controller.cache.records["mainland"]?.snapshots.values.first?.role.region == .mainland
              && controller.cache.records["global"]?.snapshots.values.first?.role.region == .global, "Regional role/snapshot caches remain independent")
        now.addTimeInterval(600); controller.tick(); wait { !controller.hasActiveRequest }
        let priorCN = api.profiles.filter { $0.region == .mainland }.count
        controller.perform(.selectRegion(.china), window: nil); wait { !controller.hasActiveRequest }
        check(api.profiles.filter { $0.region == .mainland }.count == priorCN + 1,
              "A just-refreshed Global region does not impose its cooldown on overdue Mainland data")
        let localBeforeDisconnect = profile.profile
        controller.disconnect(); wait { !controller.hasActiveRequest }
        check(!controller.record.linked && controller.record.roles.isEmpty && controller.selectedSnapshot == nil
              && (try! vault.load(region: .mainland)) == nil, "Disconnect removes that region's credentials and role cache")
        check((try! vault.load(region: .global)) != nil && controller.cache.records["global"]?.linked == true,
              "Disconnecting Mainland leaves the Global account intact")
        check(events.last == "unlinked" && events.allSatisfy { ["linked", "unlinked", "synced", "settings"].contains($0) }, "Unlink emits only a closed action name without account data")
        check(profile.profile == localBeforeDisconnect, "Disconnect preserves the local personal card and all manual edits")
        check(controller.gaugeValue(work: work).0 == "29 / 30", "An unlinked stamina header falls back to Work Mode minutes")

        // Retry deadlines and stale-completion gates are exercised independently of card sync.
        let failureAPI = HypergryphControllerFixtureAPI(now: { now })
        let expiredCache = directory.appendingPathComponent("Expired/cache.json")
        let failureController = HypergryphAccountController(fileURL: expiredCache, api: failureAPI, vault: HypergryphMemoryCredentialVault(), now: { now })
        failureController.setVisible(true, accountModule: true)
        failureController.acceptLogin(cred: "failure-fixture", region: .mainland); wait { !failureController.hasActiveRequest }
        failureAPI.failure = .transport
        now.addTimeInterval(600); failureController.tick(); wait { !failureController.hasActiveRequest }
        let firstFailure = failureAPI.total
        now.addTimeInterval(59); failureController.tick(); check(failureAPI.total == firstFailure, "First failure backs off for sixty seconds")
        now.addTimeInterval(1); failureController.tick(); wait { !failureController.hasActiveRequest }
        let secondFailure = failureAPI.total
        now.addTimeInterval(119); failureController.tick(); check(failureAPI.total == secondFailure, "Repeated failure backs off for 120 seconds")
        now.addTimeInterval(1); failureController.tick(); wait { !failureController.hasActiveRequest }
        check(failureAPI.total == secondFailure + 1, "Backoff eventually allows a bounded retry")
        failureAPI.failure = .authenticationExpired; now.addTimeInterval(5)
        failureController.refresh(manual: true); wait { !failureController.hasActiveRequest }
        let expired = failureAPI.total; now.addTimeInterval(3600); failureController.tick(); failureController.refresh(manual: true)
        check(failureController.record.requiresReconnect && failureAPI.total == expired, "Expired credentials stop automatic retries until explicit sign-in")
        check(failureController.selectedSnapshot != nil, "A service/authentication failure keeps the last successful snapshot")
        let reopened = HypergryphAccountController(fileURL: expiredCache, api: failureAPI, vault: HypergryphMemoryCredentialVault(), now: { now })
        check(reopened.presentation.status == .failed && !reopened.presentation.statusMessage.isEmpty && reopened.selectedSnapshot != nil,
              "A reopened expired session shows reconnect guidance alongside preserved data without touching Keychain")

        let staleAPI = HypergryphControllerFixtureAPI(now: { now })
        let stale = HypergryphAccountController(api: staleAPI, vault: HypergryphMemoryCredentialVault(), now: { now })
        stale.setVisible(true, accountModule: true)
        stale.acceptLogin(cred: "stale-fixture", region: .mainland,
                          signingToken: "stale-fresh-signing", deviceID: "stale-fresh-device")
        wait { !stale.hasActiveRequest }
        let saved = stale.selectedSnapshot
        staleAPI.holdProfiles = true; now.addTimeInterval(600); stale.tick()
        wait { staleAPI.pending.count == 1 }
        check(stale.hasActiveRequest && staleAPI.pending.count == 1, "The stale-completion fixture really has an in-flight profile")
        stale.setVisible(false, accountModule: false)
        check(staleAPI.handles.last?.cancelled == true && !stale.hasActiveRequest, "Hiding cancels the active request and clears busy state")
        staleAPI.completePending(withName: "STALE MUST NOT APPLY")
        RunLoop.main.run(until: Date().addingTimeInterval(0.02))
        check(stale.selectedSnapshot == saved, "An already-queued completion cannot replace the snapshot after close")
        stale.setVisible(true, accountModule: true); now.addTimeInterval(60); stale.tick()
        check(staleAPI.pending.count == 1, "Reopening can initiate a fresh generation")
        stale.disconnect(); wait { !stale.hasActiveRequest }; staleAPI.completePending(withName: "DISCONNECTED")
        RunLoop.main.run(until: Date().addingTimeInterval(0.02))
        check(!stale.record.linked && stale.record.snapshots.isEmpty, "A stale result cannot recreate an account after disconnect")

        let contextAPI = HypergryphControllerFixtureAPI(now: { now })
        let contextVault = HypergryphMemoryCredentialVault()
        let contextCache = directory.appendingPathComponent("Context/cache.json")
        let contextController = HypergryphAccountController(fileURL: contextCache, api: contextAPI, vault: contextVault, now: { now })
        contextController.setVisible(true, accountModule: true)
        contextController.acceptLogin(cred: "context-scoped-credential", region: .mainland,
                                      signingToken: "official-page-signing", deviceID: "official-page-device")
        wait { !contextController.hasActiveRequest }
        let freshContext = HypergryphCredentials(cred: "context-scoped-credential",
            signingToken: "official-page-signing", deviceID: "official-page-device")
        check(contextAPI.refreshRegions.isEmpty && contextAPI.total == 2,
              "A complete fresh official session goes directly to bindings and profile without an unnecessary refresh")
        check(contextAPI.bindingContexts == [freshContext] && contextAPI.profileContexts == [freshContext],
              "The first binding and profile reads retain the exact page-issued credential, signing token and device context")
        check(contextController.record.linked && contextController.selectedSnapshot != nil
              && (try! contextVault.load(region: .mainland)) == freshContext,
              "The exact fresh session commits to the injected vault before its first profile is applied")
        let contextText = String(decoding: try! Data(contentsOf: contextCache), as: UTF8.self)
        check(!contextText.contains("official-page-device") && !contextText.contains("deviceID"),
              "Device context never enters the ordinary profile/cache JSON")
        let resumedContextAPI = HypergryphControllerFixtureAPI(now: { now })
        let resumedContext = HypergryphAccountController(fileURL: contextCache, api: resumedContextAPI, vault: contextVault, now: { now })
        now.addTimeInterval(600)
        resumedContext.setVisible(true, accountModule: true); resumedContext.tick()
        wait { !resumedContext.hasActiveRequest }
        check(resumedContextAPI.refreshContexts.first?.deviceID == "official-page-device",
              "An overdue reopened sample restores complete context instead of a cred-only refresh")
        now.addTimeInterval(600); contextController.tick(); wait { !contextController.hasActiveRequest }
        check(contextAPI.refreshContexts == [freshContext]
              && contextAPI.profileContexts.last?.signingToken == "fixture-signing-secret",
              "An aged fresh session still performs the existing signed refresh before subsequent profile reads")
        let beforeInvalid = contextAPI.total
        contextController.acceptLogin(cred: "context-scoped-credential", region: .mainland,
                                      signingToken: "official-page-signing", deviceID: "invalid device")
        check(contextAPI.total == beforeInvalid && !contextController.hasActiveRequest,
              "Invalid device context cannot initiate a refresh or credential commit")
        contextController.acceptLogin(cred: "invalid credential", region: .mainland,
                                      signingToken: "official-page-signing", deviceID: "official-page-device")
        check(contextAPI.total == beforeInvalid && !contextController.isPresentingAccountPanel
              && (try! contextVault.load(region: .mainland))?.cred == freshContext.cred,
              "Skipping refresh never permits an invalid credential to reach the vault or API")
        contextController.acceptLogin(cred: "partial-context", region: .mainland, signingToken: "only-signing-token")
        check(contextAPI.total == beforeInvalid && !contextController.hasActiveRequest
              && (try! contextVault.load(region: .mainland))?.cred == freshContext.cred,
              "A partial session cannot fall back to the legacy cred-only refresh or replace the committed session")

        let rejectedVault = HypergryphControllerDelayedVault(rejectSaves: true)
        let rejectedAPI = HypergryphControllerFixtureAPI(now: { now })
        let rejected = HypergryphAccountController(api: rejectedAPI, vault: rejectedVault, now: { now })
        rejected.setVisible(true, accountModule: true)
        rejected.acceptLogin(cred: "rejected-commit", region: .mainland,
                             signingToken: "fresh-signing", deviceID: "fresh-device")
        wait { !rejected.hasActiveRequest }
        check(!rejected.record.linked && !rejected.isPresentingAccountPanel && rejectedAPI.total == 0
              && (try! rejectedVault.load(region: .mainland)) == nil,
              "A failed fresh-session vault commit stops all reads and releases account-panel focus protection")

        // A user-authorized unlink is a data mutation, independent of HUD visibility.
        let delayedVault = HypergryphControllerDelayedVault()
        let delayedAPI = HypergryphControllerFixtureAPI(now: { now })
        let delayed = HypergryphAccountController(api: delayedAPI, vault: delayedVault, now: { now })
        delayed.setVisible(true, accountModule: true)
        delayedVault.holdNextSave()
        delayed.acceptLogin(cred: "commit-focus-fixture", region: .mainland,
                            signingToken: "fresh-signing", deviceID: "fresh-device")
        wait { delayedVault.saveStarted }
        check(delayed.isPresentingAccountPanel && !delayed.isPresentingLogin && delayedAPI.total == 0,
              "Focus protection remains active during the credential commit after WebKit closes")
        delayed.setVisible(false, accountModule: false)
        delayedVault.releaseSave(); wait { !delayed.isPresentingAccountPanel }
        check(delayed.record.linked && delayedAPI.total == 0 && !delayed.hasActiveRequest,
              "Closing during a fresh-session commit preserves the requested save without starting hidden reads")
        delayed.setVisible(true, accountModule: true); now.addTimeInterval(5); delayed.tick()
        wait { !delayed.hasActiveRequest }
        check(delayedAPI.refreshRegions.isEmpty && delayedAPI.bindingContexts.first?.signingToken == "fresh-signing",
              "Reopening after a completed fresh commit starts bindings with the original pair without refresh")
        delayed.acceptLogin(cred: "unlink-before-close", region: .mainland); wait { !delayed.hasActiveRequest }
        delayedVault.holdNextRemoval()
        delayed.disconnect(); wait { delayedVault.removalStarted }
        delayed.setVisible(false, accountModule: false)
        now.addTimeInterval(120); delayed.setVisible(true, accountModule: true)
        let pendingReads = delayedAPI.total
        delayed.tick(); delayed.refresh(manual: true)
        check(delayedAPI.total == pendingReads && delayed.hasActiveRequest, "A pending unlink blocks cached-credential reads across close/reopen")
        delayedVault.releaseRemoval(); wait { !delayed.hasActiveRequest }
        check(!delayed.record.linked && delayed.record.roles.isEmpty && (try! delayedVault.load(region: .mainland)) == nil,
              "Successful unlink finalizes local credentials and role cache even after the presentation generation changes")

        delayed.acceptLogin(cred: "unlink-old-region", region: .mainland); wait { !delayed.hasActiveRequest }
        delayed.acceptLogin(cred: "keep-global", region: .global); wait { !delayed.hasActiveRequest }
        delayed.perform(.selectRegion(.china), window: nil)
        delayedVault.holdNextRemoval(); delayed.disconnect(); wait { delayedVault.removalStarted }
        delayed.setVisible(false, accountModule: false)
        delayed.perform(.selectRegion(.global), window: nil)
        let globalRecordID = delayed.selectedRole?.id, globalPresentation = delayed.presentation.status
        delayedVault.releaseRemoval(); wait { delayed.cache.records["mainland"] == nil }
        check(delayed.region == .global && delayed.selectedRole?.id == globalRecordID && delayed.presentation.status == globalPresentation,
              "Finishing an old-region unlink does not overwrite the active region's role, status, or busy state")
        check((try! delayedVault.load(region: .global))?.cred == "keep-global", "Unlink completion removes only the requested region's vault entry")

        delayed.setVisible(true, accountModule: true)
        delayed.acceptLogin(cred: "old-before-relink", region: .mainland); wait { !delayed.hasActiveRequest }
        delayedVault.holdNextRemoval(); delayed.disconnect(); wait { delayedVault.removalStarted }
        delayed.setVisible(false, accountModule: false); delayed.setVisible(true, accountModule: true)
        delayed.acceptLogin(cred: "new-after-unlink", region: .mainland,
                            signingToken: "new-after-unlink-signing", deviceID: "new-after-unlink-device")
        delayedVault.releaseRemoval(); wait { !delayed.hasActiveRequest }
        check(delayed.record.linked && (try! delayedVault.load(region: .mainland))?.cred == "new-after-unlink",
              "An older delayed unlink cannot erase a newer explicit login; serial vault writes preserve the newer credential")

        let relinkAPI = HypergryphControllerFixtureAPI(now: { now })
        let relink = HypergryphAccountController(api: relinkAPI, vault: HypergryphMemoryCredentialVault(), now: { now })
        relink.setVisible(true, accountModule: true)
        relink.acceptLogin(cred: "first-person", region: .mainland); wait { !relink.hasActiveRequest }
        let oldRole = relink.selectedRole!.id, oldReads = relinkAPI.profiles.count
        relinkAPI.roleSuffix = "-second-person"; relinkAPI.bindingFailure = .transport
        let beforeFreshRelinkRefresh = relinkAPI.refreshRegions.count
        relink.acceptLogin(cred: "second-person", region: .mainland,
                           signingToken: "second-person-signing", deviceID: "second-person-device")
        wait { !relink.hasActiveRequest }
        check(relink.record.bindingsAt == nil && relink.record.roles.isEmpty && relink.selectedSnapshot == nil,
              "A new credential invalidates previous community identity before its first binding request can fail")
        check(relinkAPI.refreshRegions.count == beforeFreshRelinkRefresh && relinkAPI.profiles.count == oldReads,
              "A failed binding read from a fresh session does not rotate credentials or reuse a previous person's profile")
        now.addTimeInterval(5); relink.refresh(manual: true); wait { !relink.hasActiveRequest }
        check(relinkAPI.profiles.count == oldReads && relinkAPI.bindingRegions.count == 3,
              "Failed-binding retries cannot skip to old role IDs using the replacement credential")
        relinkAPI.bindingFailure = nil; now.addTimeInterval(5)
        relink.refresh(manual: true); wait { !relink.hasActiveRequest }
        check(relink.selectedRole?.id != oldRole && relinkAPI.profiles.last?.roleID == "cn-role-second-person",
              "Only successfully refreshed bindings permit the new account's profile read")
        delayed.setVisible(false, accountModule: false); relink.setVisible(false, accountModule: false)

        var futureCache = HypergryphAccountController.Cache(); futureCache.version = 99
        let protectedCacheFixtures: [Data] = [
            Data("{unfinished-json".utf8),
            try! JSONEncoder().encode(futureCache),
            Data(repeating: 32, count: 1_048_577),
            Data("{\"version\":1,\"records\":\"wrong-schema\"}".utf8)
        ]
        for (index, original) in protectedCacheFixtures.enumerated() {
            let protectedURL = directory.appendingPathComponent("protected-cache-\(index).json")
            try! original.write(to: protectedURL)
            let protectedAPI = HypergryphControllerFixtureAPI(now: { now })
            let protected = HypergryphAccountController(fileURL: protectedURL, api: protectedAPI,
                vault: HypergryphMemoryCredentialVault(), now: { now })
            protected.perform(.selectHeaderMode(.hidden), window: nil)
            check(protected.headerMode == .hidden && (try! Data(contentsOf: protectedURL)) == original,
                  "Rejected cache permits in-memory settings without replacing original bytes")
            protected.setVisible(true, accountModule: true)
            protected.acceptLogin(cred: "protected-cache-fixture", region: .mainland)
            wait { !protected.hasActiveRequest }
            check(protected.record.linked && protected.selectedSnapshot != nil && (try! Data(contentsOf: protectedURL)) == original,
                  "Malformed, future, oversized, and invalid-schema caches remain untouched through usable memory-only login")
            protected.disconnect(); wait { !protected.hasActiveRequest }
            check(!protected.record.linked && (try! Data(contentsOf: protectedURL)) == original,
                  "Disconnect clears the memory session while preserving rejected cache bytes")
            protected.setVisible(false, accountModule: false)
        }

        let legacyDirectory = directory.appendingPathComponent("LegacyProfile")
        let legacy = try! UserProfileStore(directory: legacyDirectory, now: manualDate)
        let legacyURL = legacyDirectory.appendingPathComponent("profile.json")
        var legacyJSON = try! JSONSerialization.jsonObject(with: Data(contentsOf: legacyURL)) as! [String: Any]
        var legacyProfile = legacyJSON["profile"] as! [String: Any]
        ["gamePlayerID", "playerIDOverride", "hasManualAwakeningDate"].forEach { legacyProfile.removeValue(forKey: $0) }
        legacyJSON["profile"] = legacyProfile
        try! JSONSerialization.data(withJSONObject: legacyJSON).write(to: legacyURL)
        let migrated = try! UserProfileStore(directory: legacyDirectory, now: now)
        check(migrated.profile.uid == legacy.profile.uid && migrated.profile.awakeningDate == manualDate
              && migrated.profile.displayedUID == legacy.profile.uid && !migrated.profile.hasManualAwakeningDate,
              "Old personal cards retain their identity/date while additive account override fields default safely")
        controller.setVisible(false, accountModule: false); failureController.setVisible(false, accountModule: false); stale.setVisible(false, accountModule: false)
        return checks
    }
}

private final class HypergryphControllerFixtureRequest: HypergryphAccountRequest {
    private(set) var cancelled = false
    func cancel() { cancelled = true }
}
private final class HypergryphControllerFixtureAPI: HypergryphAccountServing {
    let now: () -> Date
    var refreshRegions: [HypergryphAccountRegion] = [], bindingRegions: [HypergryphAccountRegion] = [], profiles: [HypergryphRole] = []
    var handles: [HypergryphControllerFixtureRequest] = []
    var sparse = false, holdProfiles = false
    var failure: HypergryphAPIError?
    var bindingFailure: HypergryphAPIError?
    var roleSuffix = ""
    var pending: [(HypergryphRole, (Result<HypergryphProfileSnapshot, HypergryphAPIError>) -> Void)] = []
    var refreshContexts: [HypergryphCredentials] = []
    var bindingContexts: [HypergryphCredentials] = [], profileContexts: [HypergryphCredentials] = []
    var total: Int { refreshRegions.count + bindingRegions.count + profiles.count }
    init(now: @escaping () -> Date) { self.now = now }
    private func handle() -> HypergryphControllerFixtureRequest { let value = HypergryphControllerFixtureRequest(); handles.append(value); return value }
    func refreshCredentials(cred: String, region: HypergryphAccountRegion, completion: @escaping (Result<HypergryphCredentials, HypergryphAPIError>) -> Void) -> HypergryphAccountRequest? {
        refreshRegions.append(region); let request = handle()
        DispatchQueue.main.async { completion(.success(HypergryphCredentials(cred: cred, signingToken: "fixture-signing-secret"))) }; return request
    }
    func refreshCredentials(credentials: HypergryphCredentials, region: HypergryphAccountRegion, completion: @escaping (Result<HypergryphCredentials, HypergryphAPIError>) -> Void) -> HypergryphAccountRequest? {
        refreshContexts.append(credentials); refreshRegions.append(region); let request = handle()
        DispatchQueue.main.async { completion(.success(HypergryphCredentials(cred: credentials.cred,
            signingToken: "fixture-signing-secret", deviceID: credentials.deviceID))) }; return request
    }
    func bindings(credentials: HypergryphCredentials, region: HypergryphAccountRegion, completion: @escaping (Result<[HypergryphRole], HypergryphAPIError>) -> Void) -> HypergryphAccountRequest? {
        bindingRegions.append(region); bindingContexts.append(credentials); let request = handle()
        let role = HypergryphRole(region: region, game: .endfield, bindingUID: region.rawValue + "-binding",
            roleID: (region == .mainland ? "cn-role" : "global-role") + roleSuffix, serverID: "1", isDefault: true, communityUserID: region.rawValue + "-community")
        let result: Result<[HypergryphRole], HypergryphAPIError> = bindingFailure.map { .failure($0) } ?? .success([role])
        DispatchQueue.main.async { completion(result) }; return request
    }
    func profile(role: HypergryphRole, credentials: HypergryphCredentials, completion: @escaping (Result<HypergryphProfileSnapshot, HypergryphAPIError>) -> Void) -> HypergryphAccountRequest? {
        profiles.append(role); profileContexts.append(credentials); let request = handle()
        if holdProfiles { pending.append((role, completion)); return request }
        let result: Result<HypergryphProfileSnapshot, HypergryphAPIError> = failure.map { .failure($0) } ?? .success(snapshot(role))
        DispatchQueue.main.async { completion(result) }; return request
    }
    func snapshot(_ role: HypergryphRole) -> HypergryphProfileSnapshot {
        var result = HypergryphProfileSnapshot(role: role, observedAt: now())
        if !sparse {
            result.name = role.region == .mainland ? "Game Mainland" : "Game Global"
            result.createdAt = Date(timeIntervalSince1970: 1_700_000_000)
            result.level = 23; result.worldLevel = 4; result.operatorCount = 20; result.weaponCount = 12; result.documentCount = 7
            result.stamina = HypergryphStamina(current: 80, maximum: 240)
        }
        return result
    }
    func completePending(withName name: String) {
        let entries = pending; pending = []
        for (role, completion) in entries { var value = snapshot(role); value.name = name; completion(.success(value)) }
    }
}

private final class HypergryphControllerDelayedVault: HypergryphAccountCredentialVault {
    private let lock = NSLock()
    private let rejectSaves: Bool
    init(rejectSaves: Bool = false) { self.rejectSaves = rejectSaves }
    private var values: [HypergryphAccountRegion: HypergryphCredentials] = [:]
    private var gate: DispatchSemaphore?
    private var started = false
    private var saveGate: DispatchSemaphore?
    private var saving = false
    var saveStarted: Bool { lock.lock(); defer { lock.unlock() }; return saving }
    func holdNextSave() { lock.lock(); saveGate = DispatchSemaphore(value: 0); saving = false; lock.unlock() }
    func releaseSave() { lock.lock(); let gate = saveGate; saveGate = nil; lock.unlock(); gate?.signal() }
    var removalStarted: Bool { lock.lock(); defer { lock.unlock() }; return started }
    func holdNextRemoval() { lock.lock(); gate = DispatchSemaphore(value: 0); started = false; lock.unlock() }
    func releaseRemoval() { lock.lock(); let gate = self.gate; self.gate = nil; lock.unlock(); gate?.signal() }
    func load(region: HypergryphAccountRegion) throws -> HypergryphCredentials? { lock.lock(); defer { lock.unlock() }; return values[region] }
    func save(_ credentials: HypergryphCredentials, region: HypergryphAccountRegion) throws {
        if rejectSaves { throw HypergryphAPIError.transport }
        lock.lock(); let gate = saveGate; saving = true; lock.unlock()
        if let gate, gate.wait(timeout: .now() + 3) != .success { throw HypergryphAPIError.transport }
        lock.lock(); values[region] = credentials; lock.unlock()
    }
    func remove(region: HypergryphAccountRegion) throws {
        lock.lock(); let gate = self.gate; started = true; lock.unlock()
        if let gate = gate, gate.wait(timeout: .now() + 3) != .success { throw HypergryphAPIError.transport }
        lock.lock(); values.removeValue(forKey: region); lock.unlock()
    }
}
