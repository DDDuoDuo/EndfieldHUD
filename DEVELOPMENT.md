# Development workflow

See [architecture](docs/architecture.md) for module ownership and lifecycle boundaries,
[testing](TESTING.md) for checks, and [test distribution](docs/testing-build.md) for DMGs.

For ongoing iterations, use a native development build and checks relevant to
the change. Skip release packaging and CPU/RSS measurement rounds unless the
user explicitly requests them.

1. Make the focused change, then run `./scripts/dev.sh`.
2. Run `./scripts/test.sh` when core logic changes; use the relevant graphical
   smoke check or a brief manual check when behavior or appearance changes.
3. Quit the running copy before opening `build/dev/EndfieldHUD.app` to inspect
   the result. The build script never quits or launches an app automatically.

The development script compiles only the host's native architecture, using the
same SDK selection as `build.sh`. It targets macOS 11 on Apple silicon and
macOS 10.15.4 on Intel. These builds use the release optimizer (`-O`) by default,
so local review has representative animation and geometry performance without
release packaging. Use `DEV_OPTIMIZATION=-Onone` only for debugger-focused work;
do not use that build to judge CPU usage or responsiveness. The build log prints
the chosen mode, and `build-info.json` beside the app records it with the executable
hash so a review copy's configuration can be verified.

The first build reuses `build/EndfieldHUD.app` resources when available, or
creates a minimal bundle with the existing Info.plist, license, and credits.
Every successful build refreshes the bundle's Info.plist from
`Resources/Info.plist`, so capability purpose strings stay in step with the
executable. Supplied icon presets are copied into the development bundle. The
default Endfield icon is cached and regenerated only when its source or renderer
changes. Later builds replace
the executable only after compilation succeeds, then apply a local ad hoc
signature. A minimal bundle may display the default app icon. Bundle identity
is unchanged, so development and release copies share user preferences.

Use `DEV_BUILD_DIR` for a different development output directory and `SDKROOT`
for an explicit SDK. The universal app in `build/EndfieldHUD.app` is preserved.
No ZIP, DMG, source archive or version bump is part of this
workflow. Use release and performance workflows only when requested.

Per-process audio routing has synthetic-buffer and injected-hardware checks;
these must not enable capture or alter real output devices. Real capture/volume behavior has also been manually verified in earlier integration work; synthetic checks do not repeat or replace that hardware check. See [per-app audio](docs/per-app-audio.md).

Faction presets ship as individual 512px crops under `Resources/AppIconSources/Factions`.
The original atlas stays in the repository; the picker does not decode it at runtime.
After changing atlas selections, rebuild these assets with
`scripts/prepare-faction-icons.swift` (arguments: atlas PNG, `Sources/HUDApplicationIcon.swift`, output directory).

Release builds define `HUD_RELEASE`. The shared `HUDResources` locator then reads
only the signed app bundle; checkout resource fallbacks are compiled out.
Development and standalone test tools retain those fallbacks. Verify both modes
with `./scripts/test-release-resources.sh` before changing resource loading.

## Roadmap Batches 7/8 review — 2026-10-04

The optimized native review copy is `build/batch78-review/EndfieldHUD.app`
(arm64, macOS 11 minimum, `-O`, about 32 MiB). `build-info.json` records its
binary hash; `verification.json` records the checked source hashes and results.
This build adds the Archive category/scroll/menu corrections, Media Assembly,
and local Calendar; it does not change the release version or publish assets.

Validation on disposable fixtures:

- **78,403 core assertions passed** in `build/batch78-passed-core.log`, including
  generated native image/video output, import/export race and cancellation
  protection, new retained controls, reminder/date/persistence logic, all prior
  module tests and five-language coverage.
- **941 native assertions passed** across `batch78-native-final.log` (34),
  `batch78-batch6.log` (36), `batch78-batch34.log` (88), and
  `batch78-navigation.log` (783). Final screenshots are under
  `build/batch78-review/previews`; Calendar status clears the battery bar.
- **85 compatibility mutation checks passed**. The original Chinese suffix is
  still SHA-256 `479b39840919177c1a237fb1706504135529855ae48512b13610631ac8fb437d`.
  Existing data paths, protected contracts, release identity and original
  translations remain intact. Strict deep verification of the local ad hoc
  app signature passed.
- The full `scripts/test.sh` command exits 1 only after the passing core runner:
  its separate offline Metal probe cannot find an installed `metal` compiler.
  Native HUD rendering/snapshots worked; no Metal compiler was installed.

Core compilation now uses a single unoptimized whole module, avoiding repeated
parsing of hundreds of primary files while preserving debug assertions and
runtime checks. App optimization remains `-O`. Generated compiler caches from
this review were removed after testing; review bundles, logs and test binaries
were retained. No running user copy, user media, Notes, profiles, Shelf stores,
macOS calendars or notification permission/delivery were changed by these tests.

Real Finder/Shelf drag sessions, VoiceOver use, native reminder permission and
actual delivery after quitting, broad camera/codec compatibility and hardware
CPU/GPU/RAM measurements remain manual validation. Game filter/sticker assets
remain explicitly marked placeholders until the user supplies them. Full feature
contracts and limits are in `docs/media-assembly.md`, `docs/calendar.md` and
`docs/archive.md`; the authoritative roadmap is `docs/implementation-roadmap.txt`.

## UI and Photo Mode polish review — 2026-10-05

The current optimized local review copy is `build/polish-review/EndfieldHUD.app`
(arm64, macOS 11 minimum, `-O`). Its `build-info.json` identifies the executable;
`verification.json` records source hashes, asset inventory and validation results.
This supersedes the earlier placeholder Media Assembly review above. No version
bump, release upload, installation or launch of the normal user application was
performed.

Implemented in this pass:

- Light-mode Storage/Activity Monitor captions, shared `//` section headings,
  Archive gallery scrollbar/category frame clearance/attachment counter, and
  the neutral File Shelf scrollbar.
- Centered Projection toolbar artwork; narrower anchored Reader popovers,
  inset zoom controls, retained page zoom, vertical panning and page continuity.
- Calendar's filled theme-color Today and light Add controls, Storage-style
  refresh arrow, left-aligned events, centered ticks and reduced explanatory copy.
- Reference-based Media Assembly staging, 1–20× image zoom/pan, real preset
  grids, and direct sticker move/resize/rotate/delete handles. Sticker transforms
  and viewport changes update layers without scheduling image-processing work.
  The 14 authentic LUTs and 28 stickers occupy 2,872,150 bytes including thumbnails
  and provenance. LUT compaction preserves every supplied Float32 lookup node.

Validation used only generated media, temporary stores and diagnostic fixtures:

- **78,845 core assertions passed**, including the 179 focused Media backend
  assertions and new layout, input, localization and appearance coverage.
- **891 native assertions passed** across the final Batch7/8 run (41), Reader/
  Archive regression (36), Projection regression (31) and navigation (783).
  Batch7/8 was repeated on the final binary after the last Calendar color and
  shared resource-locator corrections; the other suites cover unchanged paths.
  Native screenshots are in `build/polish-review/previews`.
- **85 compatibility mutation checks passed**. The original Chinese roadmap
  suffix remains SHA-256
  `479b39840919177c1a237fb1706504135529855ae48512b13610631ac8fb437d`.
- Strict deep verification of the local ad hoc signature passed. All selected
  Media Assembly files in the bundle match repository bytes. Development and
  release resource lookup checks confirm that release mode has no checkout
  fallback. The packaging regression now allows only the three previously
  introduced map-marker images, still rejecting the full extracted Domain scene.

The full core script still exits 1 at its separate final offline Metal probe:
the installed developer tools have no `metal` compiler. Native runtime Metal
rendering and the UI checks above passed. Physical trackpad feel, VoiceOver use,
native notification permission/delivery, broad real-camera codec coverage and
fresh hardware CPU/GPU/RAM profiles remain unverified. Authentic external LUTs
are used; the game's scene-dependent HDR/lighting/particle pipeline is not
reconstructed. No requested UI behavior is deferred.

Disposable compiler caches from this review are removed after testing. Review
bundles, logs and screenshots remain. Existing user Notes, media, personal card,
settings, running application and real notification state were not changed.


## 2026-10-05 — Inline media controls and Closure’s Minigame

User scope: inline Media Assembly editing, removal of four marketing stickers,
cache cleanup, and the silent native OrbiPom minigame using the supplied original
source/resources. Existing Chinese roadmap requirements remain byte-for-byte
unchanged; earlier uncommitted batches and user-data contracts are preserved.

Implemented:

- Crop, Adjust, Curves, Levels and Trim replace the existing tool drawer with
  smoothly scrolling controls. Crop uses four direct corner handles with source
  coordinate mapping through rotation/mirroring. Video Trim uses one dual-ended
  range rail and coalesced immediate preview seeking from either endpoint.
- Alipay, Five Towers, Katsuya and Happy Lemon are removed from the catalog,
  translations, provenance and bundled artwork. Remaining sticker IDs are stable.
  Media resources now total 2,758,221 bytes (52 assets plus provenance); staging
  replaces this directory so deleted images cannot survive in an older bundle.
- Closure’s Minigame uses the supplied Matter 0.20.0 engine and 74 unchanged
  original controller declarations for physics, scoring, spawn rules, energy,
  danger timing, and all four skills. The right-side icon comes from the original
  `codex/endfield-watch-motion` branch. Existing HUD controls, tilt, transitions,
  keyboard input and projected accessibility controls host the game without sound.
- A single lazy JavaScriptCore VM, retained native sprite layers and one weak
  presentation clock avoid a WebView or service. The game stops advancing on
  hidden tabs, HUD close, focus loss, sleep, manual pause or restart confirmation.
  Session state survives tab/overlay changes; only the best score is persisted.
  OrbiPom adds 560,297 resource bytes across 19 files, with no audio/video sequences.
- Development and release signing carry the narrow JavaScriptCore JIT entitlement
  alongside existing Apple Events authorization. No unsigned-memory or library
  validation exception is added. A signed hardened-runtime probe verified the
  performance benefit; details and Apple’s source are in docs/orbipom-runtime.md.

Verified using generated fixtures and isolated stores only:

- **79,243 core assertions passed**, including 108 runtime, 68 canvas/session,
  and the Media control/catalog coverage. The full script subsequently exits 1
  only at the separate offline Metal source probe: this Mac has no `metal` compiler.
- **878 native HUD assertions passed**: 22 minigame lifecycle/input, 41 Batch7/8
  Media/Calendar, and 815 navigation across all 22 center sections. The minigame
  check was repeated after final signing. Screenshots were inspected.
- **89 compatibility mutation checks passed**, including strict allowed entitlements;
  original baseline/source hashes and the Chinese roadmap suffix are unchanged.
- Every Media and OrbiPom bundled resource matches repository/provenance bytes;
  all four removed stickers are absent. Strict deep signature verification passed.
- The engine-only hardened-runtime A/B probe processed the same 600 frames with
  60 starting compound bodies and 37 merges in 0.633 seconds without JIT and
  0.186 seconds with JIT (0.310 ms/frame). This excludes native HUD rendering.
- **3,031,024,896 bytes (2.82 GiB)** of ignored, regenerable compiler caches were
  removed only after confirming no active compiler. Review bundles, logs, source
  packages, user data and the user's running app remain untouched.

Review app: `build/minigame-review-final/EndfieldHUD.app`. Logs, previews,
cleanup manifest, binary/source fingerprints and final compatibility result are
recorded alongside it in `verification.json`.

Unverified: physical trackpad/VoiceOver experience, long play sessions on other
hardware, and full-HUD Instruments CPU/GPU/RAM profiling. The original React
scene's throw cinematics, streamed wind/celebration frames, tutorials and official
account/reward UI are omitted from this lightweight native presentation; source
gameplay/physics remain intact. Sound is intentionally excluded as requested.
Nothing is published or released in this batch.


## 2026-10-05 — Game status, media close and shortcut row repair

- The game’s original five-second overflow countdown is red and to the board’s
  right. Pause/game-over text stays centered above a fading dark board layer.
  A bottom-right question mark opens the shared tilted, animated rules menu.
  Rules scroll, capture clicks, support keyboard/accessibility, and pause play
  temporarily. Manual pause is retained by the session across tab changes,
  configuration rebuilds and HUD close/reopen; Rules cannot clear it.
- Official supplied localization data replaces Energy/能量 with SP in English
  and Japanese, 技力 in both Chinese variants, and 스킬 게이지 in Korean. Rules
  are translated in all five supported languages.
- Media Assembly has × below Open. Closing releases playback/observers and
  temporary edits/cache state, invalidates in-flight imports/previews, and fades
  the existing poster briefly. It never modifies/deletes the source media.
  Close is disabled during an active export.
- Two custom apps exceeded the 18-button source pool. Its first-row X offset
  wrapped at the bottom and leaked into Y/Z under tilt, overlapping neighbors.
  Recycled rows now use a shared origin and uniform average pitch before the
  existing slant pass. The source curve, masks, bounded pool and bounce remain.
  An odd final Add App always occupies column one.

Validation:

- **79,721 core assertions passed**. The first run overlapped compilation and
  failed the existing 15ms AudioDeviceController event-drain assertion. The same
  compiled test binary passed when run alone; no audio code or test deadline was
  changed. Both logs are retained.
- **1,082 native HUD assertions passed** on the final optimized build: minigame
  30, app shortcuts 196, Media/Calendar 41, and all-section navigation 815.
  Paused/rules/danger/game-over, two-custom-app and Media previews were inspected.
- Focused fixtures passed 89 game canvas, 71 Media and 21,765 source-navigation
  assertions. These are included in the integrated checks, not extra totals.
- **89 compatibility mutation checks passed**. The original Chinese roadmap,
  original game runtime and OrbiPom resources are byte-identical to the prior
  reviewed build. All Media/OrbiPom bundled resource bytes match source, and
  strict deep code-signature verification passed.
- The separate offline Metal compiler probe remains unavailable on this Mac;
  native Metal rendering works in the tested HUD. Physical trackpad/VoiceOver
  feel and cross-device performance were not profiled in this follow-up.

Review app: `build/game-polish-review/EndfieldHUD.app`. Previews, binary/source
hashes and log references are in `build/game-polish-review/verification.json`.
Tests used disposable data and generated media; the user's running application
and personal data were not changed. No release or push was performed.

## 2026-10-05 — Mainland and Global account linking

Roadmap batch 10 adds an Account module in the existing tilted HUD. Mainland
森空岛 is the initial region; SKPORT uses a separate Global session and role cache.
The module shares personal-card menus, confirmation, keyboard/accessibility,
finite transitions and theme controls. The original game MoneyCell artwork adds
an optional header for Endfield/Arknights sanity, Work Mode minutes, or hidden.

Linking is explicit and uses an ephemeral official-site sign-in window with
EndfieldHUD controls. The window stays above the HUD and restores focus only
while the originating overlay remains visible. Only a scoped community session
is saved to the region-specific Keychain entry. No password, passport token,
existing browser session, account mutation or Heybox identity is collected.

Card sync and game-avatar sync are separate opt-ins. Sparse service responses
map role ID, wake date, name, levels and inventory counts without inventing
missing values. The original local UID stays in its original profile archive.
The newer explicit user instruction takes precedence over the old roadmap lock:
Player ID and awakening date remain editable, and manual overrides survive sync.
Tag, biography, existing image/background and crop settings remain compatible.

A single retained controller reuses the existing visible HUD clock. It refreshes
at most once per minute while needed, cancels network work on close, bounds
responses/caches/avatar decode size, and backs off failures. Explicit Keychain
mutations are isolated from visibility generations, so closing cannot leave a
removed session usable or erase a newer login. Relinking clears old role identity
before fetching fresh bindings. Account Event Log metadata uses only closed
action names; periodic refresh is silent. No new polling timer, SDK or bundled
WebKit runtime is introduced. Exact API evidence is in docs/account-api-research.md;
behavior and verification limits are in docs/account-linking.md.

Validation used temporary profiles, a memory credential vault, generated/fixture
responses and opt-in native diagnostic modes only:

- **80,344 core assertions passed** on the final source. Account-specific suites
  include 55 API, 94 login/Keychain/panel, 37 avatar, 100 controller and 36 canvas/
  gauge assertions. They cover both hosts, request signatures and bounds,
  cancellation, regional isolation, manual overrides, account replacement and
  unlink races, expired sessions, secret-free persistence/logging, and preserved
  corrupt/future-version files. Earlier profile action-count expectations were
  updated to include the new editable Player ID control.
- **866 native assertions passed** on the final optimized app: Account 19 and
  navigation 847 across all 23 center sections. Source gauge artwork, projected
  secondary menus, confirmation, preferences, close/reopen and zero retained
  hidden animations passed. Account previews were visually inspected.
- **89 compatibility mutation checks passed**. Removing only the new Batch 10
  status paragraph reproduces the previous roadmap file exactly; original
  Chinese requirements and prior game/Media resource bytes remain unchanged.
- Strict deep ad hoc signature verification passed. The three added original
  wallet textures match source bytes. The native app adds **540,377 bytes** over
  the prior review bundle, with no bundled web engine or third-party account SDK.
- Removed **741,821,356 bytes (707.5 MiB)** of this batch’s regenerable compiler
  caches after compilers/tests exited. Review bundles, test binaries, logs,
  previews and source/research evidence remain.

Review app: `build/account-review/EndfieldHUD.app`. Final source and binary hashes,
checks, screenshots and cleanup are recorded in its sibling `verification.json`.
The user’s running app, real Keychain, profiles and Notes were not changed.
No version change, commit, push, release or real account login was performed.

Unverified: owner sign-in and actual SMS/email/captcha flows, live game-field and
avatar availability, publisher throttling, cross-device behavior, physical
VoiceOver use and full-HUD Instruments CPU/GPU/RAM profiling. The adapter follows
observed official website interfaces rather than a registered public SDK.
External Google/Apple SSO is deferred until its provider flow is verified; the
owned official website’s phone/email routes are implemented. No public primary
source established Heybox’s Endfield partner API, so the integration does not
use it. The separate offline Metal compiler probe remains unavailable on this
host; shaders are unchanged and native Metal HUD rendering passed.

### Account sign-in repair and source numerals — 2026-10-05

The supplied endfield-wallpaper example uses an unofficial relay rather than
calling Hypergryph directly. The repair keeps first-party requests and fixes
confirmed desktop-window problems: the community site's Login button was beyond
the 860-point viewport, and SDK popups lost their opener when the delegate loaded
their request into the parent. Community pages now fit a 1280 CSS-pixel viewport;
owned child windows retain their supplied configuration, ephemeral store and
opener. Retry stays available after loading errors. Exact-origin, nonce-gated
credential observation covers storage and navigation/window lifecycle events
without polling. Cancelling or finishing tears down all owned WebKit windows.

API codes for signing-key expiration and clock drift now receive one bounded
recovery attempt, including error envelopes returned with HTTP 401/403. Business
errors remain distinct from expired credentials. Transient recovered keys are
bounded to one SHA-256 account identity per region.

The sanity gauge moves 72 design points right to mirror ENDFIELDHUD around the
HUD center. Its digits use the original MoneyCell HarmonyOS Sans SC Medium glyphs
and metrics, extracted reproducibly from the source branch. Thirteen glyphs add
9,834 compressed resource bytes; only changed values/scales trigger rasterization.
The surrounding HUD animation/tilt hierarchy remains unchanged.

Final verification: **80,468 core assertions**, **867 native HUD assertions**
(Account 20, navigation 847), **16 isolated native WebKit popup/storage checks**,
and **89 compatibility mutation checks** passed. The focused account suites
contain API 104, login/Keychain 156, avatar 37, controller 100, and gauge/canvas 43
assertions. Original Chinese requirement bytes and final compile inputs were
verified unchanged. Strict deep ad hoc signature verification passed.

Review app: `build/account-repair-review/EndfieldHUD.app` (native arm64, optimized).
It occupies **35,922,442 bytes**, **78,724 bytes** above the preceding account
review app, counting regular files without duplicate symlink targets. Native
previews confirm the mirrored gauge and packaged source font. Public probes
confirm both official homepage loads and the CN form; they do not authenticate.
The local WebKit fixture verifies opener/message/storage mechanics without an
external network. Full source hashes and logs are in the sibling
`verification.json`; regenerable compiler cache cleanup is in `cleanup.json`.

Still unverified: successful live CN/global authentication, captcha/SMS/email
completion, real account data and avatar availability. External Google/Apple
SSO remains deferred. No account, user profile, real Keychain, running HUD,
release, version, Git commit or remote branch was changed by this repair.

### Account device-context repair and explicit user test — 2026-10-05

The owner confirmed CN web login succeeds and the login window closes, while
the HUD stays disconnected. An actual production WebKit bridge fixture ruled
out the prior origin/nonce/handler theory. A public API probe using only a
synthetic invalid credential reproduced provider code 10001: missing device
information. The official site retains three pieces of scoped session context;
the old native handoff kept only the credential.

The repaired handoff includes the official signing token and already-issued
device ID. Native login/resume/recovery requests retain that context, sign it,
and supply the matching dId header. Refresh corrects clock skew at most once;
transient signing replacements are isolated by account and device context.
The Keychain model is additive and still decodes older records. The device ID
never enters the profile/cache or Event Log. Credential commit temporarily
retains HUD focus protection if macOS displays a Keychain prompt.

After explicit Connect consent, a bounded one-shot action opens the official
login dialog directly. No form fields, challenges or consent choices are
submitted by the app. The CN dialog was verified on the real unauthenticated
site; Global DOM routing has isolated fixtures but still needs a live check.

A separate `build/account-live-test/EndfieldHUD Account Test.app` was built for
the requested owner test. It uses the same Login/API source and fresh ephemeral
WebKit, keeps credentials in memory, and performs read-only refresh/binding/card
requests. It writes only fixed lifecycle tags, safe counts/booleans and numeric
error codes to its result log. It does not access Keychain, personal profiles,
Notes or the running HUD. The user was asked to test Mainland China and report
the result; authenticated success is not inferred from fixture checks.

Final isolated verification: **80,599 core assertions**, **20 native Account HUD
assertions**, **224 focused login assertions**, **156 focused API assertions**,
**18 actual native full-context handoff assertions**, **5 real unauthenticated CN
login-form assertions**, and **89 compatibility mutation checks** passed. The
complete context-review app compiles and passes strict deep ad hoc signature
verification; compile-input hashes and original Chinese requirement bytes remain
unchanged. No release, version, commit or remote update was made.

Review bundle: `build/account-context-review/EndfieldHUD.app`, **35,942,906 bytes**
(20,464 bytes above the preceding repair build, without duplicate symlink
counts). The running account-repair app was not replaced. The owner test remains
pending; no real credential/profile response or real Keychain save was tested.
The complete evidence is recorded in `build/account-context-review/verification.json`.


## Account profile request follow-up (2026-10-05)

The account owner completed Mainland sign-in, signed refresh, bindings and
community lookup in the isolated tester. The profile request alone failed with
provider 10001. The original fixed-tag diagnostic log is preserved; no real
credential, profile response or Keychain item was inspected. The official
webpage redirects the owner toward the mobile app.

A fresh primary-source audit found an unsupported `sk-language: zh-cn` header
on Mainland requests; it is now omitted, matching the CN client. Global keeps
`en`. Current role identity/query/signature fields match the official client.
The normal HUD still uses its existing web route; the independent Profile Test
can compare the documented app route with the same session and query. It does
not automatically route around a recognized permission or identity restriction.

The optional diagnostic callback exposes only a fixed endpoint, numeric code
and fixed message category. It is unset in the HUD, adds no polling or requests,
and never passes raw provider text. The isolated tester adds finite in-memory
retry support instead of requiring a new login for each attempt; it does not
write account material to disk or Keychain.

Focused API checks: **184 assertions passed**. Compatibility: **89 mutation
checks passed**. Real profile success is still pending the owner's retest.
No version, release or remote repository changes were made.

The optimized review app (`build/account-profile-review/EndfieldHUD.app`)
compiles, passes strict deep ad hoc signature verification and **20 native
Account HUD assertions**. It is 35,951,898 regular-file bytes, 8,992 bytes above
the preceding context-review app. Only this pass's compiler caches were
removed (450,880,968 bytes); review apps and evidence remain. The running HUD
was not replaced. Evidence: `build/account-profile-validation/verification.json`.

Profile tester ready: `build/account-profile-test/EndfieldHUD Profile Test.app`
(584,530 bytes). Its 23 isolated lifecycle/route-gating checks, strict signature
and hidden idle preview passed. The account owner must still run the corrected
profile request; the tester was not launched for authentication by the assistant.


## Account service-reason follow-up (2026-10-05)

The owner tested the corrected header plus both documented card routes; both
returned 10001 after refresh and binding succeeded. No route/header hypothesis
is reported as a confirmed fix. All three newly supplied reference projects
were checked: one uses the separate passport/U8 gacha scope, two implement
original Arknights player-info rather than Endfield card data. No code or
broader account access was imported.

The exact official signing function matches eight native loopback-wire
fixtures. A separate synthetic numeric-ID precision defect was reproduced and
fixed without changing string identities. The optional diagnostic UI callback
now displays a bounded, redacted error sentence only in the standalone tester;
it never logs that sentence or exposes successful/nested profile data. Normal
HUD callbacks remain unset; no polling or new authenticated endpoints were
added.

`build/account-reason-test/EndfieldHUD Service Reason Test.app` is ready and
strictly signed. It uses the actual latest API/Models/Login and only the normal
web card route. The 193 API, 23 tester lifecycle and 89 compatibility assertions
pass; the tester's hidden preview passes. Live profile success and the service
reason remain pending the owner test. Original logs and running HUD remain
unchanged. Evidence: `build/account-reason-validation/verification.json`.

## Account reference-flow correction (2026-10-05)

The owner reported the provider explanation `操作失败，请稍后重试` and
confirmed that this account's Endfield profile and sanity load in the official
mobile app. The integration's card request remains the unresolved step.

Both supplied SKLAND implementations were compared through their complete
authentication-to-profile flows (`erzaozi/skland-plugin` at `09bc5e4` and
`fxquarter/astrbot_plugin_arknights_sanity` at `9bca538`). They use the newly
issued credential/signing-token pair immediately. The HUD now does the same
for complete official login sessions, retaining its original async credential
commit, cancellation, regional isolation and focus protection. Saved and aged
sessions still refresh as before. This removes one unnecessary initial request;
it does not establish the cause of the service rejection.

A fresh traversal of the official Endfield asset graph corrected the earlier
header audit: the card service owns a separate OneFetch client and does not
inherit the shared game client's `sk-game-role` header. Card requests now omit
that extra header and retain the official role/server/community query identity.
No private session data, extra scopes, fabricated device IDs or polling were
introduced. Research and pinned references are in `docs/account-api-research.md`.

The unused `build/account-card-test/EndfieldHUD Card Client Test.app` was rebuilt
with both corrections. It passes 30 isolated assertions and strict signature
verification. Its session remains in memory with a fixed ten-minute retry
expiry; only fixed diagnostic tags go to its log. Production API fixtures pass
193 assertions and the controller passes 122, including direct fresh CN/Global
sessions, exact context preservation, saved-session refresh, failed commits,
stale results and close/reopen behavior. End-to-end acceptance remains pending
the owner's next test. The running HUD and personal data remain untouched.

The owner then tested those corrections: bindings succeeded (two roles, one
available Endfield role), but card lookup still returned 10001 with the same
generic service reason. Neither correction is the demonstrated cause. The
optimized normal HUD build and 89 compatibility mutation checks pass; this
review bundle was not launched or installed over the running HUD.

Further Endfield-specific references document an authenticated-self request
with only roleId/serverId. `build/account-self-profile-test/EndfieldHUD Self
Profile Test.app` tests that contract explicitly, skipping the community-user
lookup. The production default remains unchanged pending live validation.
API coverage now passes 231 assertions; the new tester passes 30 lifecycle
checks, strict signature verification and a hidden UI preview. No new endpoint,
polling or third-party session source was added. Verification and source hashes
are recorded in `build/account-self-profile-test/verification.json`.

## Endfield self-profile verified; Arknights sanity correction (2026-10-05)

The owner confirmed the Self Profile Test succeeds with sanity, avatar, level,
awakening date and operator/weapon/file counts present. That exact
authenticated-self request is now the default in the HUD: bindings followed by
the API card route with roleId/serverId only. No community-user lookup or fresh
token rotation is needed. Existing profile/cache identities and manual overrides
remain compatible; the running application was not replaced.

The Arknights audit against both user-supplied SKLAND projects found two concrete
omissions. Player-info now receives the selected binding's channelMasterId when
available. AP decoding now follows the official last-recovery-tick calculation,
with the referenced recovery-deadline fallback for older payloads. It preserves
over-cap values and unavailable recovery timestamps, uses server time when
available, and adds no ticking service or request. The logic applies only to
Arknights. The focused API suite passes 267 assertions, including official and
Bilibili channel separation, exact signatures and AP time/overflow boundaries.
Live Arknights and Global tests remain unverified at this point; an isolated
Arknights tester is provided for the owner.

The owner then confirmed the Arknights tester's sanity matches 森空岛. Mainland
Endfield profile retrieval and Arknights sanity are therefore both live-verified
in isolated helpers. The Arknights helper passes 42 lifecycle/selection checks.
Global linking and the real-account persistence/UI flow in the complete HUD
remain unverified; no real user data or running application was changed by tests.

The optimized full review build is ready at
`build/account-linked-review/EndfieldHUD.app`; build and strict deep signature
verification pass. Evidence and final source hashes are in
`build/account-success-validation/verification.json`. Generated compiler caches
from this build and its Arknights helper were removed; runnable apps and
verification evidence remain. No version, release, remote push or installation
was performed.

## v1.2.0 build 15 maintenance (2026-10-06)

App shortcut metadata now supports native, flat, and wrapped iOS-on-Mac app
bundles. The selected outer app remains the launch/bookmark/icon identity;
wrapped metadata is accepted only inside that app's immediate Wrapper directory.
The installed 云·终末地 bundle was inspected and exercised through an isolated
shortcut store without launching it. Existing bookmark storage and migration
contracts remain unchanged. The shortcut suite passes 81 assertions and the
compatibility guard passes 112 mutation checks.

Sanity hover fill uses the existing bar artwork's nine-slice alpha silhouette.
Display now groups Ambient Motion, Reduce Motion, and Low-Power Visual Mode in
that order. Their saved settings and translations are unchanged. The replacement
Rhodes Island, Babel, and Rhine Lab center logos use the supplied transparent
wordmarks; built-in and custom center artwork is normalized once to the default
300×65 display footprint. The three replacement assets occupy about 199 KB.

Pointer rendering retains stable draw grouping and avoids repeating immutable
layout work. Clock and sanity accessibility controls update their geometry as
the interface tilts, while equal labels and state no longer trigger repeated
AppKit setters. Animation cadence, shaders, resolution, and tilt calculations
remain unchanged. Measurements, renderer comparisons, and limitations are
recorded in `docs/cursor-performance-1.2.0.md`.

The public version remains 1.2.0; build 15 allows the signed updater to recognize
the replacement. Release-description text is preserved. Tests use temporary
stores and do not replace the owner's running app or modify personal data.

## v1.2.0 build 16 maintenance (2026-10-06)

Center artwork now matches the default logo's visible height with proportional
width, correcting build 15's fixed-width normalization. Transparent margins are
cropped once per selection, the centered source pivot and animations remain
intact, and decoded artwork remains bounded to 768 pixels. The source shell uses
its original 65-point height; the legacy fallback retains its 29-point height.

The desktop host is the only HUD cursor tracking owner, using
`activeInKeyWindow` instead of unsupported `activeAlways` cursor tracking.
Duplicate cursor rectangles and synchronous/queued post-event reassertion were
removed. Cursor updates answer AppKit at its arbitration point, with explicit
visibility lifecycle handling and native editor/drag ownership preserved. No
cursor timer, capture service, software pointer, or render-loop work was added.

The isolated fixtures cover proportional geometry and restored default artwork,
cursor tracking options, native mouse dispatch, text/drag cursor preservation,
idle setter counts, and release on close. Synthetic cursor updates call the
registered tracking owner because AppKit discards manufactured tracking events
without its internal region association. Actual Shift–Command–5 recording
remains a manual verification item; these fixtures cannot certify saved-video
cursor appearance. The user's running HUD and personal data remain untouched.

A separate short-lived AppKit probe then verified genuine tracking-area
delivery with a stationary pointer: entering a text view or an editable/selectable
text field selected the I-beam without clicking, and leaving restored the custom
cursor. An own-process, window-only ScreenCaptureKit recording contained 139
frames (84 Endfield, 55 I-beam, no unexpected/arrow frames), confirmed in both raw
capture buffers and the decoded movie. This verifies that isolated cursor path;
it is not a test of the full HUD through the Shift–Command–5 recorder UI.

Local verification passed 80,912 core assertions, 112 compatibility mutation
checks, 127 native lifecycle assertions, and 157 clock/logo integration
assertions on the final universal app. The core script's subsequent offline
Metal compiler probe is unavailable on this host; the complete GitHub check
also covers shader and GPU fixtures. Native cursor-region invalidation can
legitimately emit cursor updates, so the lifecycle fixture measures explicit
handler calls and settled idle intervals separately from that arbitration.

The public version stays 1.2.0. Build 16 provides a distinct signed update;
the existing release description is preserved.

## Recording-only cursor follow-up (2026-10-06)

The owner confirmed build 16 still alternates between the system arrow and
Endfield cursor during built-in macOS recording, both live and in saved video;
normal pointer movement is unaffected. The installed app was verified against
the build 16 release binary. A stationary full-HUD baseline did not reproduce
the reported physical-motion failure, so that baseline is not proof of a cause.

The candidate composites the original cursor bitmap in a small, untransformed
HUD layer and owns one balanced native-cursor hide lease. It reuses the existing
app-local input monitor, changes the layer only when coordinates change, and
adds no polling timer or production capture service. Native text, resize, menu,
control tracking and file-drag cursors retain their ownership. The host/source
tracking areas persist across layout, and the opening-to-stable transition
retains its cursor lease. Projected editors select the I-beam at their visible,
transformed hit region.

An isolated recording used the built-in screencapture service, the full HUD and
an opaque test shield beneath a small capture region, without exposing desktop
content. The final recording contains 756 presented frames: no system-arrow
return after Endfield acquisition, exactly one cursor in all 120 movement
frames, and one blank frame (16.7 ms) at the initial hardware-to-layer handoff.
Tracing confirms one hide and one unhide, including release after close. Motion
was injected only into this application's local event queue; the physical
pointer did not move. This is evidence for cursor composition, not verification
of physical movement through the Shift–Command–5 toolbar. The owner then tested the separate final Cursor Test app with physical mouse
movement and Shift–Command–5, and confirmed no flicker in either the live display
or saved video. This verifies their reported recording workflow on this Mac.

Core checks passed 80,940 assertions; the final native build passed 143 HUD
lifecycle assertions, including native-control completion and rejected drag
ownership. The script's following offline Metal
compiler probe could not run because this host lacks that toolchain; no shaders
changed. Compatibility guards pass. The opt-in diagnostic helper and its
recording/tracing code are excluded from default development and release builds.
The separate manual Cursor Test app uses temporary stores and fixture services,
exits after 90 seconds or HUD dismissal, and leaves the installed app untouched.

Cleanup removed about 7.1 GB of obsolete generated builds, duplicate packages,
compiler caches and raw recording frames. Latest build 16 distribution files,
source assets, verification reports and runnable test outputs were preserved.
The bounded cleanup manifests and recording evidence remain under
`build/storage-cleanup-20261006` and `build/cursor-build16-followup`. APFS snapshot
and clone accounting may make the available-space increase differ from the
removed files' allocated size. No real profile data was inspected or deleted.
