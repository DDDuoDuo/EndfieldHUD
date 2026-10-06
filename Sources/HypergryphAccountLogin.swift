import AppKit
import WebKit
import QuartzCore

struct HypergryphAccountLoginResult {
    let region: HypergryphAccountRegion
    let cred: String
    var signingToken: String? = nil
    var deviceID: String? = nil
}

enum HypergryphAccountLoginError: Error, Equatable {
    case cancelled, expired, alreadyPresenting, unsupportedNavigation, pageUnavailable
}

/// Explicit diagnostic sessions can observe fixed lifecycle tags, never web
/// content, URLs, account identifiers or credentials. Normal HUD has no sink.
enum HypergryphAccountLoginDiagnostic {
    case consented, pageStarted, pageLoaded, pageRetried, pageTimeout
    case pageFailed(Int), webProcessTerminated, navigationBlocked
    case popupOpened, popupClosed, credentialAccepted, credentialRejected
    case loginFormRequested, loginFormUnavailable, credentialContextMissing
    case completed, cancelled, expired
    var tag: String {
        switch self {
        case .consented: return "consented"
        case .pageStarted: return "page-started"
        case .pageLoaded: return "page-loaded"
        case .pageRetried: return "page-retried"
        case .pageTimeout: return "page-timeout"
        case .pageFailed(let code): return "page-error:\(code)"
        case .webProcessTerminated: return "web-process-terminated"
        case .navigationBlocked: return "navigation-blocked"
        case .popupOpened: return "popup-opened"
        case .popupClosed: return "popup-closed"
        case .credentialAccepted: return "credential-accepted"
        case .credentialRejected: return "credential-rejected"
        case .loginFormRequested: return "login-form-requested"
        case .loginFormUnavailable: return "login-form-unavailable"
        case .credentialContextMissing: return "credential-context-missing"
        case .completed: return "completed"
        case .cancelled: return "cancelled"
        case .expired: return "expired"
        }
    }
}

/// These URLs and the three scoped community-session storage keys are observed first-party web behavior,
/// not a registered EndfieldHUD OAuth client or a guaranteed public API.
enum HypergryphAccountLoginPolicy {
    static let credentialKey = "SK_OAUTH_CRED_KEY"
    static let signingTokenKey = "SK_TOKEN_CACHE_KEY"
    static let deviceIDKey = "SK_SHUMEI_DEVICE_ID_KEY"
    static let messageName = "endfieldCommunityCredential"
    static let stateMessageName = "endfieldCommunityState"

    static func siteURL(_ region: HypergryphAccountRegion) -> URL {
        URL(string: region == .mainland ? "https://www.skland.com/" : "https://www.skport.com/")!
    }

    static func acceptsCredentialOrigin(_ url: URL?, region: HypergryphAccountRegion) -> Bool {
        guard let url, isHTTPS(url) else { return false }
        return url.host?.lowercased() == siteURL(region).host
    }

    static func permitsNavigation(_ url: URL?, region: HypergryphAccountRegion, mainFrame: Bool) -> Bool {
        guard let url else { return false }
        if !mainFrame && url.absoluteString == "about:blank" { return true }
        guard isHTTPS(url), let host = url.host?.lowercased() else { return false }
        // Document navigations only; WebKit fetches the official page's own
        // scripts/images/challenge resources normally without rewriting them.
        let hosts: Set<String> = region == .mainland
            ? ["www.skland.com", "web-api.skland.com", "user.hypergryph.com", "as.hypergryph.com", "web-api.hypergryph.com", "assets.skland.com"]
            : ["www.skport.com", "web-api.skport.com", "user.gryphline.com", "as.gryphline.com", "web-api.gryphline.com", "web-api.gryphline.net", "assets.skport.com"]
        // Verified in the official account SDK's Geetest loader. Challenges run
        // normally in their own frames and can never supply our scoped cred.
        let challengeHosts: Set<String> = ["gcaptcha4.geetest.com", "gcaptcha4.geevisit.com", "gcaptcha4.gsensebot.com",
                                          "static.geetest.com", "static.geevisit.com", "dn-staticdown.qbox.me"]
        if !mainFrame && challengeHosts.contains(host) { return true }
        return hosts.contains(host)
    }

    static func acceptsCredential(_ value: String) -> Bool {
        (1...4096).contains(value.utf8.count) && value.unicodeScalars.allSatisfy { (33...126).contains($0.value) }
    }

    static func permitsPopup(_ url: URL?, from source: URL?, region: HypergryphAccountRegion) -> Bool {
        guard permitsNavigation(source, region: region, mainFrame: true) else { return false }
        // The official SDK opens a blank child before assigning its login URL.
        // Blank navigation is never allowed as an arbitrary main-page route.
        return url?.absoluteString == "about:blank" || permitsNavigation(url, region: region, mainFrame: true)
    }

    static func pageZoom(for width: CGFloat) -> CGFloat {
        guard width.isFinite else { return 1 }
        // The official community's desktop header has a fixed 1200px layout.
        // Give it 1280 CSS pixels so Login remains inside our narrower panel.
        return max(0.5, min(1, width / 1280))
    }

    static func decodeCredential(_ body: Any, expectedNonce: String, region: HypergryphAccountRegion,
                                 mainFrame: Bool, frameOrigin: URL?, pageURL: URL?) -> String? {
        guard !expectedNonce.isEmpty, mainFrame, acceptsCredentialOrigin(frameOrigin, region: region),
              acceptsCredentialOrigin(pageURL, region: region), let payload = body as? [String: Any],
              payload.count == 2, payload["nonce"] as? String == expectedNonce,
              let value = payload["cred"] as? String, acceptsCredential(value) else { return nil }
        return value
    }

    /// Production requires the community signing token and the official device
    /// context alongside the credential. A cred alone cannot bootstrap the
    /// signed refresh endpoint. The two-field decoder above remains a legacy
    /// fixture helper; production never completes from a partial payload.
    static func decodeSession(_ body: Any, expectedNonce: String, region: HypergryphAccountRegion,
                              mainFrame: Bool, frameOrigin: URL?, pageURL: URL?) -> HypergryphAccountLoginResult? {
        guard !expectedNonce.isEmpty, mainFrame, acceptsCredentialOrigin(frameOrigin, region: region),
              acceptsCredentialOrigin(pageURL, region: region), let payload = body as? [String: Any],
              Set(payload.keys) == ["nonce", "cred", "signingToken", "deviceID"],
              payload["nonce"] as? String == expectedNonce,
              let cred = payload["cred"] as? String, acceptsCredential(cred),
              let token = payload["signingToken"] as? String, acceptsCredential(token),
              let device = payload["deviceID"] as? String, acceptsCredential(device) else { return nil }
        return HypergryphAccountLoginResult(region: region, cred: cred, signingToken: token, deviceID: device)
    }

    /// Only three known community keys are read. Device storage is a bounded
    /// {id,timestamp} object; only id leaves the owned page. Never read passport
    /// tokens, cookies, forms, unrelated storage or browser sessions.
    private static var sessionReaderScript: String {
        """
        const valid = value => typeof value === 'string' && /^[\\x21-\\x7e]{1,4096}$/.test(value);
        const readSession = () => {
          try {
            const cred = window.localStorage.getItem('\(credentialKey)');
            const signingToken = window.localStorage.getItem('\(signingTokenKey)');
            const raw = window.localStorage.getItem('\(deviceIDKey)');
            let deviceID = null;
            if (typeof raw === 'string' && raw.length <= 16384) {
              try { const value = JSON.parse(raw); if (value && !Array.isArray(value) && typeof value === 'object') deviceID = value.id; } catch (_) {}
            }
            return {cred, signingToken, deviceID};
          } catch (_) { return {}; }
        };
        """
    }

    static func bridgeScript(region: HypergryphAccountRegion, nonce: String) -> String {
        """
        (() => {
          if (window.top !== window || location.origin !== '\(siteURL(region).absoluteString.dropLast())') return;
          \(sessionReaderScript)
          const keys = ['\(credentialKey)', '\(signingTokenKey)', '\(deviceIDKey)'];
          let last = null, missingReported = false;
          const read = () => {
            const value = readSession();
            if (!valid(value.cred)) return;
            if (!valid(value.signingToken) || !valid(value.deviceID)) {
              if (!missingReported) {
                missingReported = true;
                window.webkit.messageHandlers.\(stateMessageName).postMessage({nonce:'\(nonce)',event:'context-missing'});
              }
              return;
            }
            const signature = JSON.stringify([value.cred, value.signingToken, value.deviceID]);
            if (last === signature) return;
            last = signature;
            window.webkit.messageHandlers.\(messageName).postMessage({nonce:'\(nonce)',cred:value.cred,signingToken:value.signingToken,deviceID:value.deviceID});
          };
          const original = Storage.prototype.setItem;
          Storage.prototype.setItem = function(name, value) {
            const result = Reflect.apply(original, this, arguments);
            try { if (this === window.localStorage && keys.includes(name)) read(); } catch (_) {}
            return result;
          };
          if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', read, {once:true});
          else read();
          window.addEventListener('storage', event => {
            if (keys.includes(event.key) && event.storageArea === window.localStorage) read();
          });
          window.addEventListener('pageshow', read);
          window.addEventListener('focus', read);
          document.addEventListener('visibilitychange', () => { if (document.visibilityState === 'visible') read(); });
        })();
        """
    }

    static func credentialReadScript(region: HypergryphAccountRegion, nonce: String) -> String {
        """
        (() => {
          if (window.top !== window || location.origin !== '\(siteURL(region).absoluteString.dropLast())') return;
          \(sessionReaderScript)
          const value = readSession();
          if (valid(value.cred) && valid(value.signingToken) && valid(value.deviceID))
            window.webkit.messageHandlers.\(messageName).postMessage({nonce:'\(nonce)',cred:value.cred,signingToken:value.signingToken,deviceID:value.deviceID});
        })();
        """
    }

    /// Explicit Connect consent opens the site's own login control once. The
    /// bounded observer accommodates SPA mounting, never fills/submits a form
    /// or solves a challenge, and is discarded after success or 20 seconds.
    static func directLoginScript(region: HypergryphAccountRegion, nonce: String) -> String {
        """
        (() => {
          if (window.top !== window || location.origin !== '\(siteURL(region).absoluteString.dropLast())') return;
          let ended = false, stage = 0, observer = null, deadline = null;
          const stop = event => {
            if (ended) return; ended = true;
            if (observer) observer.disconnect();
            if (deadline !== null) clearTimeout(deadline);
            if (event) window.webkit.messageHandlers.\(stateMessageName).postMessage({nonce:'\(nonce)',event});
          };
          const visible = element => !!element && element.getBoundingClientRect().width > 0 && element.getBoundingClientRect().height > 0;
          const attempt = () => {
            if (ended) return;
            try { if (window.localStorage.getItem('\(credentialKey)')) { stop(null); return; } } catch (_) {}
            if (\(region == .mainland ? "true" : "false")) {
              const button = document.querySelector('header .header-right .header-button');
              if (visible(button) && button.textContent.trim() === '登录') {
                stop('form-requested'); button.click();
              }
            } else if (stage === 0) {
              const avatar = document.querySelector('[class*="Header__AvatarWrapper"]');
              if (visible(avatar)) { stage = 1; avatar.click(); Promise.resolve().then(attempt); }
            } else {
              const menu = document.querySelector('[class*="HAvatar__PopoverWrapper"]');
              const items = menu ? Array.from(menu.querySelectorAll('.hover-item')) : [];
              const button = items[items.length - 1];
              if (visible(button) && button.textContent.trim().length > 0) { stop('form-requested'); button.click(); }
            }
          };
          observer = new MutationObserver(attempt);
          observer.observe(document.documentElement, {childList:true,subtree:true});
          deadline = setTimeout(() => stop('form-unavailable'), 20000);
          window.addEventListener('pagehide', () => stop(null), {once:true});
          attempt();
        })();
        """
    }

    private static func isHTTPS(_ url: URL) -> Bool {
        url.scheme?.lowercased() == "https" && url.user == nil && url.password == nil &&
            (url.port == nil || url.port == 443)
    }
}

/// Creates WebKit only after explicit consent and releases it immediately on
/// completion/cancel. No persistent browser session, polling, cookie import or
/// background service. The official website owns every authentication step.
final class HypergryphAccountLogin: NSObject, NSWindowDelegate, WKNavigationDelegate, WKUIDelegate, WKScriptMessageHandler {
    var onDiagnostic: ((HypergryphAccountLoginDiagnostic) -> Void)?
    private(set) var isPresenting = false
    private(set) var hasConsented = false
    private var region: HypergryphAccountRegion = .mainland
    private var completion: ((Result<HypergryphAccountLoginResult, HypergryphAccountLoginError>) -> Void)?
    private var panel: NSPanel?
    private var webView: WKWebView?
    private var userContent: WKUserContentController?
    private var nonce = ""
    private var expiry: DispatchWorkItem?
    private var statusLabel: NSTextField?
    private struct Popup { let view: WKWebView; let panel: NSPanel }
    private var popups: [ObjectIdentifier: Popup] = [:]
    private var retryButton: NSButton?
    private var loadDeadline: DispatchWorkItem?

    func present(region: HypergryphAccountRegion, relativeTo parent: NSWindow? = nil,
                 completion: @escaping (Result<HypergryphAccountLoginResult, HypergryphAccountLoginError>) -> Void) {
        guard !isPresenting else { completion(.failure(.alreadyPresenting)); return }
        self.region = region; self.completion = completion; isPresenting = true
        hasConsented = false; nonce = UUID().uuidString
        let panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 860, height: 650),
                            styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
        Self.configurePanelPresentation(panel, relativeTo: parent)
        panel.title = region == .mainland ? "EndfieldHUD · 森空岛" : "EndfieldHUD · SKPORT"
        panel.minSize = NSSize(width: 680, height: 540)
        if #available(macOS 11, *) {} else {
            // Older WebKit has no layout zoom; preserve the site's desktop width.
            panel.setContentSize(NSSize(width: 1280, height: 650))
            panel.minSize.width = 1280
        }
        panel.delegate = self; panel.isReleasedWhenClosed = false
        self.panel = panel
        let content = NSView(frame: panel.contentView!.bounds)
        content.autoresizingMask = [.width, .height]; panel.contentView = content
        let title = NSTextField(labelWithString: L10n.text("Connect through the official website", "通过官方网站关联"))
        title.font = .systemFont(ofSize: 20, weight: .semibold)
        let detail = NSTextField(wrappingLabelWithString: L10n.text(
            "Sign in to the official website in a private session. EndfieldHUD will save only this community session in this Mac’s Keychain to read your game profile. Your password and passport token are not collected. This is an unofficial integration.",
            "在独立会话中登录官方网站。EndfieldHUD 仅将此次社区会话保存在本机钥匙串，用于读取游戏资料，不收集密码或通行证令牌。这是非官方关联功能。"))
        detail.textColor = .secondaryLabelColor
        let site = NSTextField(labelWithString: HypergryphAccountLoginPolicy.siteURL(region).absoluteString)
        site.font = .monospacedSystemFont(ofSize: 13, weight: .regular)
        let button = HypergryphAccountAuthButton(title: L10n.text("Continue to official sign-in", "继续前往官方登录"),
            primary: true, target: self, action: #selector(beginOfficialLogin))
        let cancel = HypergryphAccountAuthButton(title: L10n.text("Cancel", "取消"), primary: false, target: self, action: #selector(cancelAction))
        cancel.keyEquivalent = "\u{1b}"
        let stack = NSStackView(views: [title, detail, site, button, cancel])
        stack.orientation = .vertical; stack.spacing = 22; stack.alignment = .centerX
        stack.translatesAutoresizingMaskIntoConstraints = false; content.addSubview(stack)
        NSLayoutConstraint.activate([stack.centerXAnchor.constraint(equalTo: content.centerXAnchor),
            stack.centerYAnchor.constraint(equalTo: content.centerYAnchor),
            stack.widthAnchor.constraint(equalTo: content.widthAnchor, multiplier: 0.76)])
        if let screen = parent?.screen {
            panel.setFrameOrigin(NSPoint(x: screen.visibleFrame.midX - panel.frame.width / 2, y: screen.visibleFrame.midY - panel.frame.height / 2))
        } else { panel.center() }
        panel.makeKeyAndOrderFront(nil)
    }

    /// The HUD lives above ordinary application windows. Its owned auth panel
    /// must remain above that parent, including other Spaces/full-screen apps.
    static func configurePanelPresentation(_ panel: NSPanel, relativeTo parent: NSWindow?) {
        panel.level = NSWindow.Level(rawValue: max(NSWindow.Level.floating.rawValue,
                                                   (parent?.level.rawValue ?? NSWindow.Level.normal.rawValue) + 1))
        panel.collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary]
        panel.hidesOnDeactivate = false
    }

    func cancel() { finish(.failure(.cancelled)) }
    @objc private func cancelAction() { cancel() }

    @objc private func beginOfficialLogin() {
        guard isPresenting, !hasConsented, let panel else { return }
        hasConsented = true
        onDiagnostic?(.consented)
        let configuration = WKWebViewConfiguration()
        configuration.websiteDataStore = .nonPersistent()
        // SDK requests can open their child after an asynchronous authorization
        // step. The delegate still enforces origin, destination and count limits.
        configuration.preferences.javaScriptCanOpenWindowsAutomatically = true
        let content = WKUserContentController()
        content.add(HypergryphWeakScriptHandler(self), name: HypergryphAccountLoginPolicy.messageName)
        content.add(HypergryphWeakScriptHandler(self), name: HypergryphAccountLoginPolicy.stateMessageName)
        content.addUserScript(WKUserScript(source: HypergryphAccountLoginPolicy.bridgeScript(region: region, nonce: nonce),
                                          injectionTime: .atDocumentStart, forMainFrameOnly: true))
        content.addUserScript(WKUserScript(source: HypergryphAccountLoginPolicy.directLoginScript(region: region, nonce: nonce),
                                          injectionTime: .atDocumentEnd, forMainFrameOnly: true))
        configuration.userContentController = content; userContent = content
        let host = NSView(frame: panel.contentView!.bounds); host.autoresizingMask = [.width, .height]
        panel.contentView = host
        let status = NSTextField(labelWithString: HypergryphAccountLoginPolicy.siteURL(region).absoluteString)
        status.frame = NSRect(x: 18, y: host.bounds.height - 30, width: host.bounds.width - 224, height: 20)
        status.autoresizingMask = [.width, .minYMargin]; status.textColor = .secondaryLabelColor
        host.addSubview(status); statusLabel = status
        let retry = HypergryphAccountAuthButton(title: L10n.text("Retry", "重试"), primary: false, target: self, action: #selector(retryPage))
        retry.frame = NSRect(x: host.bounds.width - 194, y: host.bounds.height - 37, width: 86, height: 32)
        retry.autoresizingMask = [.minXMargin, .minYMargin]; host.addSubview(retry); retryButton = retry
        let cancel = HypergryphAccountAuthButton(title: L10n.text("Cancel", "取消"), primary: false, target: self, action: #selector(cancelAction))
        cancel.frame = NSRect(x: host.bounds.width - 100, y: host.bounds.height - 37, width: 86, height: 32)
        cancel.autoresizingMask = [.minXMargin, .minYMargin]; cancel.keyEquivalent = "\u{1b}"; host.addSubview(cancel)
        let webView = WKWebView(frame: NSRect(x: 0, y: 0, width: host.bounds.width, height: host.bounds.height - 44), configuration: configuration)
        webView.autoresizingMask = [.width, .height]; webView.navigationDelegate = self; webView.uiDelegate = self
        self.webView = webView; host.addSubview(webView)
        fitCommunityPage(webView)
        loadOfficialPage()
        let expiry = DispatchWorkItem { [weak self] in self?.finish(.failure(.expired)) }
        self.expiry = expiry; DispatchQueue.main.asyncAfter(deadline: .now() + 900, execute: expiry)
    }

    private func owns(_ view: WKWebView?) -> Bool {
        guard let view else { return false }
        return view === webView || popups[ObjectIdentifier(view)] != nil
    }
    private static func originURL(_ origin: WKSecurityOrigin) -> URL? {
        var url = URLComponents(); url.scheme = origin.protocol; url.host = origin.host
        if origin.port != 0 { url.port = origin.port }; return url.url
    }
    private func readCredential(in view: WKWebView) {
        guard isPresenting, hasConsented, owns(view),
              HypergryphAccountLoginPolicy.acceptsCredentialOrigin(view.url, region: region) else { return }
        view.evaluateJavaScript(HypergryphAccountLoginPolicy.credentialReadScript(region: region, nonce: nonce), completionHandler: nil)
    }
    private func fitCommunityPage(_ view: WKWebView) {
        if #available(macOS 11, *) {
            let community = view.url == nil || HypergryphAccountLoginPolicy.acceptsCredentialOrigin(view.url, region: region)
            let zoom = community ? HypergryphAccountLoginPolicy.pageZoom(for: view.bounds.width) : 1
            if abs(view.pageZoom - zoom) > 0.001 { view.pageZoom = zoom }
        }
    }
    func windowDidResize(_ notification: Notification) {
        if let webView { fitCommunityPage(webView) }
        for popup in popups.values { fitCommunityPage(popup.view) }
    }
    private func loadOfficialPage() {
        guard isPresenting, hasConsented, let webView else { return }
        webView.load(URLRequest(url: HypergryphAccountLoginPolicy.siteURL(region),
            cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 25))
    }
    @objc private func retryPage() {
        guard isPresenting, hasConsented else { return }
        onDiagnostic?(.pageRetried)
        closePopups(); webView?.stopLoading(); loadOfficialPage()
    }
    private func showPageFailure() {
        loadDeadline?.cancel(); loadDeadline = nil
        statusLabel?.stringValue = L10n.text("The sign-in page could not load. Retry to continue.", "登录页面未能载入，请重试。")
    }
    func userContentController(_ userContentController: WKUserContentController, didReceive message: WKScriptMessage) {
        guard isPresenting, hasConsented, let owner = message.webView, owns(owner) else { return }
        let origin = Self.originURL(message.frameInfo.securityOrigin)
        if message.name == HypergryphAccountLoginPolicy.stateMessageName {
            guard message.frameInfo.isMainFrame,
                  HypergryphAccountLoginPolicy.acceptsCredentialOrigin(origin, region: region),
                  HypergryphAccountLoginPolicy.acceptsCredentialOrigin(owner.url, region: region),
                  let body = message.body as? [String: Any], Set(body.keys) == ["nonce", "event"],
                  body["nonce"] as? String == nonce, let event = body["event"] as? String else { return }
            switch event {
            case "form-requested": onDiagnostic?(.loginFormRequested)
            case "form-unavailable": onDiagnostic?(.loginFormUnavailable)
            case "context-missing": onDiagnostic?(.credentialContextMissing)
            default: break
            }
            return
        }
        guard message.name == HypergryphAccountLoginPolicy.messageName else { return }
        guard let session = HypergryphAccountLoginPolicy.decodeSession(message.body, expectedNonce: nonce,
            region: region, mainFrame: message.frameInfo.isMainFrame,
            frameOrigin: origin, pageURL: owner.url) else {
            onDiagnostic?(.credentialRejected); return
        }
        onDiagnostic?(.credentialAccepted)
        finish(.success(session))
    }

    func webView(_ webView: WKWebView, decidePolicyFor navigationAction: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        guard owns(webView) else { decisionHandler(.cancel); return }
        let url = navigationAction.request.url
        let allowed: Bool
        if navigationAction.targetFrame == nil {
            allowed = HypergryphAccountLoginPolicy.permitsPopup(url,
                from: Self.originURL(navigationAction.sourceFrame.securityOrigin), region: region)
        } else if popups[ObjectIdentifier(webView)] != nil && url?.absoluteString == "about:blank" {
            allowed = true // This child was already approved from a trusted official frame.
        } else {
            allowed = HypergryphAccountLoginPolicy.permitsNavigation(url, region: region,
                mainFrame: navigationAction.targetFrame?.isMainFrame ?? true)
        }
        decisionHandler(allowed ? .allow : .cancel)
        if !allowed { showUnsupportedNavigation() }
    }

    func webView(_ webView: WKWebView, createWebViewWith configuration: WKWebViewConfiguration,
                 for navigationAction: WKNavigationAction, windowFeatures: WKWindowFeatures) -> WKWebView? {
        guard isPresenting, hasConsented, owns(webView), navigationAction.targetFrame == nil,
              !configuration.websiteDataStore.isPersistent, popups.count < 2,
              HypergryphAccountLoginPolicy.permitsPopup(navigationAction.request.url,
                from: Self.originURL(navigationAction.sourceFrame.securityOrigin), region: region) else {
            showUnsupportedNavigation(); return nil
        }
        // Use WebKit's supplied configuration and returned child. Replacing the
        // main page or reloading the request here destroys window.opener and the
        // official SDK's callback/postMessage handshake.
        let child = WKWebView(frame: NSRect(x: 0, y: 0, width: 820, height: 640), configuration: configuration)
        let childPanel = NSPanel(contentRect: child.frame, styleMask: [.titled, .closable, .resizable], backing: .buffered, defer: false)
        Self.configurePanelPresentation(childPanel, relativeTo: panel)
        childPanel.title = panel?.title ?? "EndfieldHUD"
        childPanel.isReleasedWhenClosed = false; childPanel.delegate = self
        childPanel.minSize = NSSize(width: 680, height: 440)
        if #available(macOS 11, *) {} else {
            childPanel.setContentSize(NSSize(width: 1280, height: 640))
            childPanel.minSize.width = 1280
        }
        child.autoresizingMask = [.width, .height]; child.navigationDelegate = self; child.uiDelegate = self
        childPanel.contentView = child
        if let parent = panel { childPanel.setFrameOrigin(NSPoint(x: parent.frame.midX - childPanel.frame.width / 2, y: parent.frame.midY - childPanel.frame.height / 2)) }
        else { childPanel.center() }
        popups[ObjectIdentifier(child)] = Popup(view: child, panel: childPanel)
        onDiagnostic?(.popupOpened)
        childPanel.makeKeyAndOrderFront(nil)
        return child
    }

    private func showUnsupportedNavigation() {
        onDiagnostic?(.navigationBlocked)
        statusLabel?.stringValue = L10n.text("This sign-in route is not supported here. Use the official page’s email or phone sign-in, or cancel.", "此登录方式暂不支持，请使用官方页面的邮箱或手机号登录，或取消。")
    }
    func webView(_ webView: WKWebView, didStartProvisionalNavigation navigation: WKNavigation!) {
        guard webView === self.webView else { return }
        onDiagnostic?(.pageStarted)
        statusLabel?.stringValue = L10n.text("Loading…", "正在载入…")
        loadDeadline?.cancel()
        let deadline = DispatchWorkItem { [weak self, weak webView] in
            guard let self, self.isPresenting, webView?.isLoading == true else { return }
            self.onDiagnostic?(.pageTimeout)
            self.showPageFailure()
        }
        loadDeadline = deadline; DispatchQueue.main.asyncAfter(deadline: .now() + 25, execute: deadline)
    }
    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        guard owns(webView) else { return }
        onDiagnostic?(.pageLoaded)
        fitCommunityPage(webView)
        if webView === self.webView {
            loadDeadline?.cancel(); loadDeadline = nil
            statusLabel?.stringValue = webView.url?.host ?? HypergryphAccountLoginPolicy.siteURL(region).host ?? ""
        }
        readCredential(in: webView)
    }
    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        guard owns(webView), (error as NSError).code != NSURLErrorCancelled else { return }
        onDiagnostic?(.pageFailed((error as NSError).code))
        showPageFailure()
    }
    func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
        guard owns(webView), (error as NSError).code != NSURLErrorCancelled else { return }
        onDiagnostic?(.pageFailed((error as NSError).code))
        showPageFailure()
    }
    func webViewWebContentProcessDidTerminate(_ webView: WKWebView) {
        guard owns(webView) else { return }; onDiagnostic?(.webProcessTerminated); showPageFailure()
    }
    func webViewDidClose(_ webView: WKWebView) { closePopup(webView) }
    func windowDidBecomeKey(_ notification: Notification) {
        if let window = notification.object as? NSWindow, let view = window.contentView as? WKWebView { readCredential(in: view) }
        else if let webView { readCredential(in: webView) }
    }
    func windowWillClose(_ notification: Notification) {
        guard let window = notification.object as? NSWindow else { return }
        if window === panel { cancel() }
        else if let popup = popups.values.first(where: { $0.panel === window }) { closePopup(popup.view) }
    }
    private func closePopup(_ view: WKWebView) {
        guard let popup = popups.removeValue(forKey: ObjectIdentifier(view)) else { return }
        onDiagnostic?(.popupClosed)
        popup.panel.delegate = nil; popup.view.navigationDelegate = nil; popup.view.uiDelegate = nil
        popup.view.stopLoading(); popup.panel.close()
        if isPresenting, let webView { readCredential(in: webView); panel?.makeKeyAndOrderFront(nil) }
    }
    private func closePopups() {
        let old = Array(popups.values); popups.removeAll()
        for popup in old {
            popup.panel.delegate = nil; popup.view.navigationDelegate = nil; popup.view.uiDelegate = nil
            popup.view.stopLoading(); popup.panel.close()
        }
    }
    private func finish(_ result: Result<HypergryphAccountLoginResult, HypergryphAccountLoginError>) {
        guard isPresenting else { return }
        switch result {
        case .success: onDiagnostic?(.completed)
        case .failure(.expired): onDiagnostic?(.expired)
        case .failure: onDiagnostic?(.cancelled)
        }
        isPresenting = false; hasConsented = false; nonce = ""
        expiry?.cancel(); expiry = nil; loadDeadline?.cancel(); loadDeadline = nil
        closePopups()
        userContent?.removeScriptMessageHandler(forName: HypergryphAccountLoginPolicy.messageName)
        userContent?.removeScriptMessageHandler(forName: HypergryphAccountLoginPolicy.stateMessageName)
        userContent?.removeAllUserScripts(); userContent = nil
        webView?.stopLoading(); webView?.navigationDelegate = nil; webView?.uiDelegate = nil
        webView?.removeFromSuperview(); webView = nil
        panel?.delegate = nil; panel?.close(); panel = nil; statusLabel = nil; retryButton = nil
        let callback = completion; completion = nil; callback?(result)
    }

    deinit {
        expiry?.cancel(); loadDeadline?.cancel()
        for popup in popups.values {
            popup.panel.delegate = nil; popup.view.navigationDelegate = nil; popup.view.uiDelegate = nil
            popup.view.stopLoading(); popup.panel.close()
        }
        userContent?.removeScriptMessageHandler(forName: HypergryphAccountLoginPolicy.messageName)
        userContent?.removeScriptMessageHandler(forName: HypergryphAccountLoginPolicy.stateMessageName)
        userContent?.removeAllUserScripts()
        webView?.stopLoading(); webView?.navigationDelegate = nil; webView?.uiDelegate = nil
        panel?.delegate = nil; panel?.close()
    }

}

/// Only EndfieldHUD's surrounding auth controls use this frame. The official
/// website and its own login/captcha buttons are never restyled or intercepted.
final class HypergryphAccountAuthButton: NSButton {
    private let plate = CAShapeLayer()
    private let label = CATextLayer()
    private var feedback: HUDControlHighlightLayer?
    private var tracking: NSTrackingArea?
    private let primary: Bool
    private var hovered = false
    private var trackingPress = false
    private var focused = false
    override var isFlipped: Bool { true }

    init(title: String, primary: Bool, target: AnyObject?, action: Selector) {
        self.primary = primary
        super.init(frame: .zero)
        self.title = title; self.target = target; self.action = action
        setButtonType(.momentaryPushIn); isBordered = false; focusRingType = .none
        font = .systemFont(ofSize: 13, weight: .bold)
        wantsLayer = true; layer?.masksToBounds = false
        layer?.addSublayer(plate); layer?.addSublayer(label)
        setAccessibilityLabel(title)
    }
    required init?(coder: NSCoder) { nil }
    override var intrinsicContentSize: NSSize {
        NSSize(width: max(primary ? 240 : 110, (title as NSString).size(withAttributes: [.font: font!]).width + 34), height: 42)
    }
    override func layout() {
        super.layout()
        guard let layer else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        let face = bounds.insetBy(dx: 4, dy: 4)
        plate.frame = bounds; plate.path = Self.cutCorner(face)
        let accent = HUDRuntimeAppearance.accent
        plate.fillColor = (primary ? accent : NSColor(white: 0.84, alpha: 1)).cgColor
        feedback?.removeFromSuperlayer()
        feedback = HUDControlHighlightLayer.add(to: layer, rect: face, shape: .cutCorner, framed: true)
        // The text remains above the finite highlight tint.
        label.removeFromSuperlayer(); layer.addSublayer(label)
        label.frame = CGRect(x: 10, y: (bounds.height - 18) / 2, width: max(0, bounds.width - 20), height: 18)
        label.string = title; label.font = font; label.fontSize = 13; label.alignmentMode = .center
        label.contentsScale = window?.backingScaleFactor ?? 2
        let rgb = accent.usingColorSpace(.deviceRGB)
        let darkAccent = rgb.map { $0.redComponent * 0.2126 + $0.greenComponent * 0.7152 + $0.blueComponent * 0.0722 < 0.36 } ?? false
        label.foregroundColor = (primary && darkAccent ? NSColor.white : NSColor.black).cgColor
        CATransaction.commit(); refreshFeedback()
    }
    override func draw(_ dirtyRect: NSRect) { refreshFeedback() }
    override func updateTrackingAreas() {
        super.updateTrackingAreas()
        if let tracking { removeTrackingArea(tracking) }
        let tracking = NSTrackingArea(rect: bounds, options: [.activeInKeyWindow, .mouseEnteredAndExited, .inVisibleRect], owner: self)
        self.tracking = tracking; addTrackingArea(tracking)
    }
    override func mouseEntered(with event: NSEvent) { hovered = true; refreshFeedback() }
    override func mouseExited(with event: NSEvent) { hovered = false; refreshFeedback() }
    override func mouseDown(with event: NSEvent) {
        trackingPress = true; refreshFeedback(); super.mouseDown(with: event)
        trackingPress = false; refreshFeedback()
    }
    override func becomeFirstResponder() -> Bool {
        let result = super.becomeFirstResponder(); focused = result; refreshFeedback(); return result
    }
    override func resignFirstResponder() -> Bool {
        let result = super.resignFirstResponder(); if result { focused = false }; refreshFeedback(); return result
    }
    private func refreshFeedback() {
        guard let layer else { return }
        feedback?.setEnabled(isEnabled)
        let active = isEnabled && (hovered || trackingPress || focused)
        HUDControlHighlightLayer.update(in: layer, point: active ? CGPoint(x: bounds.midX, y: bounds.midY) : nil,
                                        pressed: trackingPress || cell?.isHighlighted == true)
    }
    private static func cutCorner(_ rect: CGRect) -> CGPath {
        let c: CGFloat = min(4, min(rect.width, rect.height) / 3), path = CGMutablePath()
        path.move(to: CGPoint(x: rect.minX + c, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.minY))
        path.addLine(to: CGPoint(x: rect.maxX, y: rect.maxY - c))
        path.addLine(to: CGPoint(x: rect.maxX - c, y: rect.maxY))
        path.addLine(to: CGPoint(x: rect.minX, y: rect.maxY))
        path.addLine(to: CGPoint(x: rect.minX, y: rect.minY + c)); path.closeSubpath(); return path
    }
}

private final class HypergryphWeakScriptHandler: NSObject, WKScriptMessageHandler {
    private weak var target: HypergryphAccountLogin?
    init(_ target: HypergryphAccountLogin) { self.target = target }
    func userContentController(_ userContentController: WKUserContentController, didReceive message: WKScriptMessage) {
        target?.userContentController(userContentController, didReceive: message)
    }
}
