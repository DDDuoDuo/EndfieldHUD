import AppKit
import JavaScriptCore
import Security
import LocalAuthentication

/// No live website, account, user defaults, browser session or system Keychain.
enum HypergryphAccountLoginTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String) { count += 1; precondition(condition, message) }
        typealias Policy = HypergryphAccountLoginPolicy
        let credential = String(repeating: "a", count: 32)
        let nonce = "isolated-fixture"
        let payload = ["nonce": nonce, "cred": credential]
        for region in HypergryphAccountRegion.allCases {
            let site = Policy.siteURL(region)
            check(Policy.acceptsCredentialOrigin(site, region: region), "Region accepts only its exact official community origin")
            check(Policy.permitsNavigation(site, region: region, mainFrame: true), "Official login page is permitted")
            check(Policy.decodeCredential(payload, expectedNonce: nonce, region: region, mainFrame: true,
                                          frameOrigin: site, pageURL: site) == credential, "Owned main-frame credential can be imported")
            check(Policy.decodeCredential(payload, expectedNonce: nonce, region: region, mainFrame: false,
                                          frameOrigin: site, pageURL: site) == nil, "Even a same-origin subframe cannot import credentials")
            check(Policy.decodeCredential(payload, expectedNonce: "previous-session", region: region, mainFrame: true,
                                          frameOrigin: site, pageURL: site) == nil, "A retired login message cannot affect a later attempt")
            let other = Policy.siteURL(region == .mainland ? .global : .mainland)
            check(Policy.decodeCredential(payload, expectedNonce: nonce, region: region, mainFrame: true,
                                          frameOrigin: other, pageURL: site) == nil, "Cross-region frame cannot supply credentials")
            check(Policy.decodeCredential(payload, expectedNonce: nonce, region: region, mainFrame: true,
                                          frameOrigin: site, pageURL: other) == nil, "Credential from a page no longer at the expected origin is rejected")
            check(Policy.decodeCredential(payload.merging(["token": "passport-must-not-be-read"]) { a, _ in a }, expectedNonce: nonce,
                                          region: region, mainFrame: true, frameOrigin: site, pageURL: site) == nil,
                  "Unexpected payload fields cannot introduce passport tokens")
            for url in ["http://\(site.host!)/", "https://\(site.host!).example.com/", "https://user@\(site.host!)/",
                        "https://\(site.host!):9443/", "javascript:alert(1)", "file:///tmp/login.html", "data:text/html,hello"] {
                check(!Policy.acceptsCredentialOrigin(URL(string: url), region: region), "Untrusted origin is rejected")
                check(!Policy.permitsNavigation(URL(string: url), region: region, mainFrame: true), "Unsafe navigation is cancelled")
            }
            check(!Policy.permitsNavigation(URL(string: "https://accounts.google.com/"), region: region, mainFrame: true),
                  "Unverified third-party sign-in route is explicit rather than implicitly trusted")
            check(Policy.permitsNavigation(URL(string: "https://gcaptcha4.geetest.com/"), region: region, mainFrame: false) &&
                  !Policy.permitsNavigation(URL(string: "https://gcaptcha4.geetest.com/"), region: region, mainFrame: true) &&
                  !Policy.acceptsCredentialOrigin(URL(string: "https://gcaptcha4.geetest.com/"), region: region),
                  "Official captcha frames can operate but cannot navigate the account page or provide credentials")

            let blank = URL(string: "about:blank")!
            let passport = URL(string: region == .mainland ? "https://user.hypergryph.com/" : "https://user.gryphline.com/")!
            check(Policy.permitsPopup(blank, from: site, region: region) && Policy.permitsPopup(blank, from: passport, region: region),
                  "An official community or passport page can create its initially blank login child")
            check(Policy.permitsPopup(passport, from: site, region: region) && Policy.permitsPopup(site, from: passport, region: region),
                  "Owned login popups can move between their region's official account and community hosts")
            check(!Policy.permitsNavigation(blank, region: region, mainFrame: true) && !Policy.acceptsCredentialOrigin(blank, region: region),
                  "Popup exception cannot make an arbitrary blank top-level document trusted")
            for source in [nil, blank, other, URL(string: "https://example.com/"), URL(string: "http://\(site.host!)/")] {
                check(!Policy.permitsPopup(blank, from: source, region: region), "Untrusted or cross-region source cannot create an owned blank login child")
            }
            for target in [nil, other, URL(string: "https://example.com/"), URL(string: "https://accounts.google.com/"), URL(string: "javascript:alert(1)")] {
                check(!Policy.permitsPopup(target, from: site, region: region), "A trusted source cannot open an unsupported or cross-region popup target")
            }

            let fullPayload = ["nonce": nonce, "cred": credential, "signingToken": "scoped-signing-token", "deviceID": "official-device-id"]
            let decoded = Policy.decodeSession(fullPayload, expectedNonce: nonce, region: region, mainFrame: true, frameOrigin: site, pageURL: site)
            check(decoded?.cred == credential && decoded?.signingToken == "scoped-signing-token" && decoded?.deviceID == "official-device-id",
                  "Production session preserves the three exact scoped values")
            check(Policy.decodeSession(payload, expectedNonce: nonce, region: region, mainFrame: true, frameOrigin: site, pageURL: site) == nil,
                  "Production cannot finish from a credential before official signing and device context exists")
            for key in ["nonce", "cred", "signingToken", "deviceID"] {
                var incomplete = fullPayload; incomplete.removeValue(forKey: key)
                check(Policy.decodeSession(incomplete, expectedNonce: nonce, region: region, mainFrame: true, frameOrigin: site, pageURL: site) == nil,
                      "Every official context field is mandatory")
            }
            for key in ["cred", "signingToken", "deviceID"] {
                for invalid in ["", "has space", "bad\nvalue", "中文", String(repeating: "a", count: 4097)] {
                    var malformed = fullPayload; malformed[key] = invalid
                    check(Policy.decodeSession(malformed, expectedNonce: nonce, region: region, mainFrame: true, frameOrigin: site, pageURL: site) == nil,
                          "Malformed scoped values are rejected before authentication completion")
                }
            }
            check(Policy.decodeSession(fullPayload.merging(["passportToken":"forbidden"]) { $1 }, expectedNonce:nonce,
                region:region, mainFrame:true, frameOrigin:site, pageURL:site) == nil, "No unrelated data can enter a session payload")
            check(Policy.decodeSession(fullPayload, expectedNonce:"old", region:region, mainFrame:true, frameOrigin:site, pageURL:site) == nil
                && Policy.decodeSession(fullPayload, expectedNonce:nonce, region:region, mainFrame:false, frameOrigin:site, pageURL:site) == nil
                && Policy.decodeSession(fullPayload, expectedNonce:nonce, region:region, mainFrame:true, frameOrigin:other, pageURL:site) == nil
                && Policy.decodeSession(fullPayload, expectedNonce:nonce, region:region, mainFrame:true, frameOrigin:site, pageURL:other) == nil,
                  "Session context is bound to the current nonce and exact owned main-frame origin")

            let vm = makeContext(origin: site)
            let bridge = Policy.bridgeScript(region: region, nonce: nonce)
            vm.evaluateScript(bridge)
            check(vm.exception == nil && vm.evaluateScript("reads.length===0 && typeof docListeners.DOMContentLoaded==='function'")!.toBool(),
                  "Loading page defers scoped session read to DOM readiness")
            vm.evaluateScript("docListeners.DOMContentLoaded();")
            check(vm.evaluateScript("reads.length===3 && messages.length===0 && states.length===0")!.toBool(),
                  "Empty session reads only the three exact community keys")
            vm.evaluateScript("window.localStorage.setItem('HG_INFO_KEY','private');window.localStorage.setItem('ACCOUNT','private');window.sessionStorage.setItem('SK_OAUTH_CRED_KEY','\(credential)');")
            check(vm.evaluateScript("reads.length===3 && messages.length===0 && states.length===0")!.toBool(), "Unrelated keys and sessionStorage never trigger reads")
            vm.evaluateScript("window.localStorage.setItem('SK_OAUTH_CRED_KEY','\(credential)');")
            check(vm.evaluateScript("messages.length===0 && states.length===1 && states[0].event==='context-missing' && Object.keys(states[0]).length===2")!.toBool(),
                  "A credential alone stays on the official page and reports only a fixed missing-context tag")
            vm.evaluateScript("window.localStorage.setItem('SK_TOKEN_CACHE_KEY','scoped-signing-token');")
            check(vm.evaluateScript("messages.length===0 && states.length===1")!.toBool(), "Signing token still waits for the official device object without diagnostic spam")
            vm.evaluateScript("window.localStorage.setItem('SK_SHUMEI_DEVICE_ID_KEY',JSON.stringify({id:'official-device-id',timestamp:1700000000000}));")
            check(vm.evaluateScript("messages.length===1 && messages[0].cred==='\(credential)' && messages[0].signingToken==='scoped-signing-token' && messages[0].deviceID==='official-device-id' && messages[0].nonce==='\(nonce)' && Object.keys(messages[0]).length===4")!.toBool(),
                  "Complete official context arrives atomically without the device-cache timestamp")
            vm.evaluateScript("window.localStorage.setItem('SK_OAUTH_CRED_KEY','\(credential)');windowListeners.pageshow();windowListeners.focus();document.visibilityState='visible';docListeners.visibilitychange();")
            check(vm.evaluateScript("messages.length===1")!.toBool(), "Duplicate context is deduplicated across writes and lifecycle events")
            vm.evaluateScript("var readCount=reads.length;document.visibilityState='hidden';docListeners.visibilitychange();windowListeners.storage({key:'HG_INFO_KEY',storageArea:window.localStorage});windowListeners.storage({key:'SK_TOKEN_CACHE_KEY',storageArea:window.sessionStorage});windowListeners.storage({key:null,storageArea:window.localStorage});")
            check(vm.evaluateScript("reads.length===readCount && messages.length===1")!.toBool(), "Hidden visibility, other storage areas and unrelated keys do not wake the bridge")
            vm.evaluateScript("window.localStorage.values.SK_TOKEN_CACHE_KEY='rotated-scoped-token';windowListeners.storage({key:'SK_TOKEN_CACHE_KEY',storageArea:window.localStorage});")
            check(vm.evaluateScript("messages.length===2 && messages[1].signingToken==='rotated-scoped-token'")!.toBool(),
                  "Owned popup token rotation is observed through its specific storage event")
            let opaque = "eyJhbGciOiJIUzI1NiJ9.c2NvcGVkLWNyZWQ=.sig+/_-="
            vm.evaluateScript("window.localStorage.setItem('SK_OAUTH_CRED_KEY','\(opaque)');")
            check(vm.evaluateScript("messages.length===3 && messages[2].cred==='\(opaque)'")!.toBool(), "Opaque scoped credentials preserve punctuation exactly")
            for invalid in ["null", "[]", "{}", "not-json", "{\"id\":7}", "{\"id\":\"bad value\"}", String(repeating:" ",count:16385)] {
                let quoted = String(data:try! JSONSerialization.data(withJSONObject:invalid,options:.fragmentsAllowed),encoding:.utf8)!
                vm.evaluateScript("window.localStorage.setItem('SK_SHUMEI_DEVICE_ID_KEY',\(quoted));")
                check(vm.exception == nil && vm.evaluateScript("messages.length===3")!.toBool(), "Malformed or oversized device JSON cannot complete authentication")
            }
            check(vm.evaluateScript("reads.every(key=>['SK_OAUTH_CRED_KEY','SK_TOKEN_CACHE_KEY','SK_SHUMEI_DEVICE_ID_KEY'].includes(key)) && window.localStorage.values.HG_INFO_KEY==='private'")!.toBool(),
                  "Every bridge path is confined to three observed scoped keys and leaves other storage untouched")
            let seed = "window.localStorage.values.SK_OAUTH_CRED_KEY='\(opaque)';window.localStorage.values.SK_TOKEN_CACHE_KEY='scoped-signing-token';window.localStorage.values.SK_SHUMEI_DEVICE_ID_KEY=JSON.stringify({id:'official-device-id',timestamp:1700000000000});"
            let restored = makeContext(origin:site,readyState:"complete"); restored.evaluateScript(seed); restored.evaluateScript(bridge)
            check(restored.exception == nil && restored.evaluateScript("reads.length===3 && messages.length===1 && messages[0].deviceID==='official-device-id' && !docListeners.DOMContentLoaded")!.toBool(),
                  "A loaded official page immediately imports complete existing community context")
            let reader = makeContext(origin:site,readyState:"complete");reader.evaluateScript(seed)
            let readScript = Policy.credentialReadScript(region:region,nonce:nonce);reader.evaluateScript(readScript)
            check(reader.exception == nil && reader.evaluateScript("reads.length===3 && messages.length===1 && Object.keys(messages[0]).length===4")!.toBool(),
                  "Native lifecycle fallback imports the same complete session schema")
            reader.evaluateScript("window.localStorage.values.SK_TOKEN_CACHE_KEY='';");reader.evaluateScript(readScript)
            check(reader.evaluateScript("messages.length===1")!.toBool(), "Lifecycle fallback never emits incomplete signing context")
            for source in [site,other] {
                let protected = makeContext(origin:source)
                if source == site {protected.evaluateScript("window.top={};")}
                protected.evaluateScript(seed);protected.evaluateScript(bridge);protected.evaluateScript(readScript)
                check(protected.exception == nil && protected.evaluateScript("reads.length===0 && messages.length===0 && Object.keys(windowListeners).length===0 && Object.keys(docListeners).length===0")!.toBool(),
                      "Neither context bridge reads an iframe or the other region's storage")
            }
            check([bridge,readScript].allSatisfy{!$0.contains("setInterval") && !$0.contains("setTimeout") && !$0.contains("fetch(") && !$0.contains("document.cookie")},
                  "Session context observation remains event driven and never requests or reads cookies")

            let direct = Policy.directLoginScript(region:region,nonce:nonce)
            let menu = makeDirectContext(origin:site,region:region)
            menu.evaluateScript(direct)
            check(menu.exception == nil && menu.evaluateScript("loginClicks===1 && states.length===1 && states[0].event==='form-requested' && !observing && timerCleared && timerDelay===20000")!.toBool(),
                  "Explicit consent opens only the verified official login control and tears down the bounded observer")
            check(menu.evaluateScript(region == .mainland ? "avatarClicks===0" : "avatarClicks===1")!.toBool(),
                  "Global account menu opens once; mainland uses its direct header login button")
            menu.evaluateScript("observerCallback();observerCallback();")
            check(menu.evaluateScript("loginClicks===1")!.toBool(), "Later DOM mutations cannot click login again")
            let late = makeDirectContext(origin:site,region:region)
            late.evaluateScript("available=false;");late.evaluateScript(direct)
            check(late.evaluateScript("loginClicks===0 && observing")!.toBool(), "SPA mounting can delay the official login control")
            late.evaluateScript("available=true;observerCallback();")
            check(late.exception == nil && late.evaluateScript("loginClicks===1 && !observing && timerCleared")!.toBool(), "The bounded mutation observer opens a newly mounted login control")
            let absent = makeDirectContext(origin:site,region:region);absent.evaluateScript("available=false;");absent.evaluateScript(direct)
            absent.evaluateScript("timerCallback();available=true;observerCallback();")
            check(absent.evaluateScript("loginClicks===0 && !observing && timerCleared && states[0].event==='form-unavailable'")!.toBool(),
                  "A missing control stops after twenty seconds without repeated searches or unintended clicks")
            let existing = makeDirectContext(origin:site,region:region);existing.evaluateScript(seed);existing.evaluateScript(direct)
            check(existing.evaluateScript("loginClicks===0 && avatarClicks===0 && !observing && states.length===0")!.toBool(),
                  "An existing community session never opens the login form again")
            let foreign = makeDirectContext(origin:other,region:region);foreign.evaluateScript(direct)
            check(foreign.evaluateScript("reads.length===0 && loginClicks===0 && !observing")!.toBool(), "Direct login never touches another region's page")

        }
        for (width, expected): (CGFloat, CGFloat) in [(320, 0.5), (640, 0.5), (960, 0.75), (1280, 1), (2560, 1), (0, 0.5), (-20, 0.5)] {
            check(abs(Policy.pageZoom(for: width) - expected) < 0.0001, "Official desktop login viewport stays reachable inside bounded 0.5–1× page zoom")
        }
        for width: CGFloat in [.nan, .infinity, -.infinity] {
            check(Policy.pageZoom(for: width) == 1, "Non-finite login viewport dimensions fall back to normal page zoom")
        }
        check(!Policy.acceptsCredential("") && !Policy.acceptsCredential(String(repeating: "a", count: 4097)), "Invalid or oversized credentials are rejected")
        check(Policy.acceptsCredential("x") && Policy.acceptsCredential(String(repeating: "~", count: 4096)), "Credential length bounds are inclusive1...4096")
        for invalid in [credential + "\n", credential + "\r", "bad value", "bad\tvalue", "bad\u{7f}value", "中文"] {
            check(!Policy.acceptsCredential(invalid), "Opaque credential excludes whitespace, controls and non-ASCII")
        }
        let login = HypergryphAccountLogin()
        check(!login.isPresenting && !login.hasConsented, "Creating login service does not create WebKit or start a session")
        login.cancel(); login.cancel()
        check(!login.isPresenting, "Idle cancellation is idempotent")
        // Unordered disposable panels only: no real HUD, WebKit, login request
        // or focus change is involved in verifying the presentation policy.
        _ = NSApplication.shared
        let parentPanel = NSPanel(contentRect: .zero, styleMask: .borderless, backing: .buffered, defer: true)
        let authPanel = NSPanel(contentRect: .zero, styleMask: [.titled, .closable], backing: .buffered, defer: true)
        parentPanel.isReleasedWhenClosed = false; authPanel.isReleasedWhenClosed = false
        parentPanel.level = .statusBar
        HypergryphAccountLogin.configurePanelPresentation(authPanel, relativeTo: parentPanel)
        check(authPanel.level.rawValue == parentPanel.level.rawValue + 1,
              "Login consent and official webpage are above the status-bar-level HUD")
        check(authPanel.collectionBehavior.contains(.canJoinAllSpaces) && authPanel.collectionBehavior.contains(.fullScreenAuxiliary),
              "Auth remains available wherever the HUD appears, including full-screen Spaces")
        check(!authPanel.hidesOnDeactivate && !authPanel.isVisible && !parentPanel.isVisible,
              "Presentation policy keeps auth visible on deactivation without ordering fixture windows")
        HypergryphAccountLogin.configurePanelPresentation(authPanel, relativeTo: nil)
        check(authPanel.level == .floating, "A parentless auth panel stays above ordinary application windows")
        authPanel.close(); parentPanel.close()
        let button = HypergryphAccountAuthButton(title: "Continue", primary: true, target: nil, action: #selector(NSObject.description))
        button.frame = NSRect(x: 0, y: 0, width: 260, height: 42); button.layout()
        check(!button.isBordered && button.layer?.sublayers?.contains { $0 is HUDControlHighlightLayer } == true,
              "Our auth controls use retained Endfield frames and the shared hover renderer")
        check(button.accessibilityLabel() == "Continue" && button.intrinsicContentSize.height == 42,
              "Styled auth control preserves native accessible labeling and compact layout")
        let event = NSEvent.mouseEvent(with: .mouseMoved, location: .zero, modifierFlags: [], timestamp: 0,
                                     windowNumber: 0, context: nil, eventNumber: 0, clickCount: 0, pressure: 0)!
        button.mouseEntered(with: event)
        check(HUDControlHighlightLayer.highlightedCount(in: button.layer!) == 1, "Hover animates the entire framed auth control")
        let feedback = button.layer!.sublayers!.compactMap { $0 as? HUDControlHighlightLayer }.first!
        check(feedback.sublayers!.flatMap { item in (item.animationKeys() ?? []).compactMap { item.animation(forKey: $0) } }
            .allSatisfy { $0.repeatCount == 0 && $0.duration <= 0.14 }, "Auth hover animations are finite and reuse HUD timing")
        button.mouseExited(with: event)
        check(HUDControlHighlightLayer.highlightedCount(in: button.layer!) == 0, "Pointer exit clears highlight without a recurring timer")

        let globalSite = Policy.siteURL(.global)
        for host in ["web-api.skport.com", "user.gryphline.com", "as.gryphline.com", "web-api.gryphline.com", "web-api.gryphline.net"] {
            let destination = URL(string: "https://\(host)/account/login")!
            check(Policy.permitsPopup(destination, from: globalSite, region: .global)
                  && Policy.permitsPopup(globalSite, from: destination, region: .global),
                  "Current GRYPHLINE and SKPORT first-party account callbacks preserve an owned popup route")
            check(!Policy.permitsNavigation(destination, region: .mainland, mainFrame: true)
                  && !Policy.acceptsCredentialOrigin(destination, region: .global),
                  "Global passport callbacks cannot cross regions or become community credential origins")
        }
        let global443 = URL(string: "https://www.skport.com:443/callback")!
        let issuedGlobal = ["nonce": nonce, "cred": "global-issued-cred", "signingToken": "global-issued-signing", "deviceID": "global-issued-device"]
        let globalSession = Policy.decodeSession(issuedGlobal, expectedNonce: nonce, region: .global,
            mainFrame: true, frameOrigin: global443, pageURL: globalSite)
        check(globalSession?.region == .global && globalSession?.signingToken == "global-issued-signing"
              && globalSession?.deviceID == "global-issued-device", "Explicit default TLS port returns the complete regional community session")
        check(Policy.decodeSession(issuedGlobal, expectedNonce: nonce, region: .mainland,
            mainFrame: true, frameOrigin: global443, pageURL: globalSite) == nil,
              "A valid Global credential bundle never satisfies a Mainland login callback")
        let fixture = KeychainFixture()
        let vault = HypergryphAccountKeychain(service: "isolated.tests.\(UUID().uuidString)", access: fixture)
        let cn = HypergryphCredentials(cred: credential, signingToken: "test-signing-key")
        let global = HypergryphCredentials(cred: String(repeating: "b", count: 32), signingToken: "other-test-key")
        check(try! vault.load(region: .mainland) == nil, "Missing scoped key is a normal disconnected state")
        try! vault.save(cn, region: .mainland); try! vault.save(global, region: .global)
        check(try! vault.load(region: .mainland) == cn, "Mainland credential round-trips in isolated keychain driver")
        check(try! vault.load(region: .global) == global, "Global credentials have a separate keychain account")
        check(fixture.added.count == 2 && fixture.added.allSatisfy {
            $0[kSecAttrAccessible as String] as? String == kSecAttrAccessibleWhenUnlockedThisDeviceOnly as String &&
            $0[kSecAttrSynchronizable as String] as? Bool == false
        }, "Credentials are unlocked-only, device-local and not synchronized")
        check(fixture.reads.allSatisfy { ($0[kSecUseAuthenticationContext as String] as? LAContext)?.interactionNotAllowed == true },
              "Background credential lookup never opens system authentication UI")
        let rotated = HypergryphCredentials(cred: credential, signingToken: "rotated-test-key")
        try! vault.save(rotated, region: .mainland)
        check(fixture.added.count == 2 && (try! vault.load(region: .mainland)) == rotated, "Refresh updates the existing item without duplicate keys")
        let encoded = try! JSONSerialization.jsonObject(with: fixture.items["mainland"]!) as! [String: Any]
        check(Set(encoded.keys) == ["cred", "signingToken"], "Keychain payload contains no profile, cookies or passport tokens")
        try! vault.remove(region: .mainland); try! vault.remove(region: .mainland)
        check((try! vault.load(region: .mainland)) == nil && (try! vault.load(region: .global)) == global, "Unlink deletes only selected region and is idempotent")
        fixture.items["global"] = Data("invalid".utf8)
        do { _ = try vault.load(region: .global); check(false, "Corrupt Keychain data must not look connected") }
        catch { check((error as? HypergryphKeychainError)?.status == errSecDecode, "Corruption is reported without credential contents") }
        fixture.error = errSecInteractionNotAllowed
        do { try vault.save(cn, region: .mainland); check(false, "Locked keychain must not silently save") }
        catch { check((error as? HypergryphKeychainError)?.status == errSecInteractionNotAllowed, "Locked keychain failure propagates without secret-bearing error text") }
        let memory = HypergryphMemoryCredentialVault()
        try! memory.save(cn, region: .mainland); try! memory.save(global, region: .global)
        try! memory.remove(region: .mainland)
        check((try! memory.load(region: .mainland)) == nil && (try! memory.load(region: .global)) == global,
              "Controller previews can inject a fully isolated vault")
        return count
    }

    private static func makeContext(origin: URL, readyState: String = "loading") -> JSContext {
        let vm = JSContext()!
        vm.evaluateScript("""
        var messages = [], states = [], reads = [], docListeners = {}, windowListeners = {};
        class Storage {
          constructor(){ this.values = {}; }
          setItem(key, value){ this.values[key] = String(value); }
          getItem(key){ reads.push(key); return this.values[key] || null; }
        }
        var location = {origin:'\(origin.absoluteString.dropLast())'};
        var document = {readyState:'\(readyState)', visibilityState:'visible', addEventListener:(key, fn) => docListeners[key] = fn};
        var window = {localStorage:new Storage(), sessionStorage:new Storage(),
          addEventListener:(key, fn) => windowListeners[key] = fn,
          webkit:{messageHandlers:{endfieldCommunityCredential:{postMessage:value => messages.push(value)},endfieldCommunityState:{postMessage:value => states.push(value)}}}};
        window.top = window;
        """)
        return vm
    }

    private static func makeDirectContext(origin:URL,region:HypergryphAccountRegion)->JSContext {
        let vm=makeContext(origin:origin,readyState:"complete")
        vm.evaluateScript("""
        var available=true,loginClicks=0,avatarClicks=0,observing=false,observerCallback=null,timerCallback=null,timerCleared=false,timerDelay=0;
        var button={textContent:'\(region == .mainland ? "登录" : "Sign in")',getBoundingClientRect:()=>({width:56,height:32}),click:()=>loginClicks++};
        var menu={querySelectorAll:()=>[button]};
        var avatar={getBoundingClientRect:()=>({width:32,height:32}),click:()=>avatarClicks++};
        document.documentElement={};
        document.querySelector=selector=>{
          if(!available)return null;
          if(selector==='header .header-right .header-button')return button;
          if(selector==='[class*="Header__AvatarWrapper"]')return avatar;
          if(selector==='[class*="HAvatar__PopoverWrapper"]')return avatarClicks>0 ? menu:null;
          return null;
        };
        var MutationObserver=class{constructor(callback){observerCallback=callback;}observe(){observing=true;}disconnect(){observing=false;}};
        var setTimeout=(callback,delay)=>{timerCallback=callback;timerDelay=delay;return 1;};
        var clearTimeout=id=>timerCleared=true;
        var Promise={resolve:()=>({then:callback=>callback()})};
        """)
        return vm
    }

    private final class KeychainFixture: HypergryphKeychainAccess {
        var items: [String: Data] = [:]
        var reads: [[String: Any]] = []
        var added: [[String: Any]] = []
        var error: OSStatus? = nil
        func read(_ query: [String: Any]) -> (OSStatus, Data?) {
            reads.append(query)
            if let error { return (error, nil) }
            let data = items[query[kSecAttrAccount as String] as! String]
            return (data == nil ? errSecItemNotFound : errSecSuccess, data)
        }
        func update(_ query: [String: Any], attributes: [String: Any]) -> OSStatus {
            if let error { return error }
            let account = query[kSecAttrAccount as String] as! String
            guard items[account] != nil else { return errSecItemNotFound }
            items[account] = attributes[kSecValueData as String] as? Data
            return errSecSuccess
        }
        func add(_ attributes: [String: Any]) -> OSStatus {
            if let error { return error }; added.append(attributes)
            items[attributes[kSecAttrAccount as String] as! String] = attributes[kSecValueData as String] as? Data
            return errSecSuccess
        }
        func delete(_ query: [String: Any]) -> OSStatus {
            if let error { return error }
            return items.removeValue(forKey: query[kSecAttrAccount as String] as! String) == nil ? errSecItemNotFound : errSecSuccess
        }
    }
}
