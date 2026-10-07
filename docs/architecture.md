# Architecture

EndfieldHUD is a native AppKit application. This integration branch renders the
source Watch shell with Metal and keeps desktop modules in Core Animation. It
uses the macOS SDK directly, with Sparkle for signed application updates. The
build scripts compile the flat `Sources` directory; folders are not Swift modules.

The integration branch's visual and interaction contract and dated design
decisions are maintained in [Desktop HUD design](design.md).

## Ownership

| Layer | Main types | Responsibility |
| --- | --- | --- |
| Application | `AppDelegate`, `GlobalShortcutController`, `LoginItemManager` | Menu bar, registered shortcut, configuration changes, system notifications, startup and shutdown |
| Presentation | `OverlayController`, `SystemOverlayState`, `ShelfDragPresentationState` | Shared panel, display choice, focus, opening/closing, drag concealment and app-launch handoffs |
| HUD shell | `SystemHUDView`, `HUDNavigation`, `HUDModuleContent` | Retained navigation and center content, module composition, projection and event routing |
| Source shell | `HUDSourceWatchView`, `HUDSourceWatchFrameBuilder`, `HUDSourceMetalRenderer` | Source scene, shared playback and projection, bounded geometry/program caches, Metal rendering |
| Native motion and artwork | `HUDMotionController`, `HUDDepthPlane`, `HUDDeploymentFlicker`, `HUDSubsectionTransition` | Separate deployment, pointer and ambient transforms; scoped animation cleanup |
| Feature UI | `*Canvas`, `HUD*Interaction` | Layer artwork and local state; native editing, accessibility, drag/drop and projected hit targets |
| Feature models | `*Store`, `*Controller`, `*Monitor` | Persistence, timers, platform I/O and work that can outlive one visible HUD |

A module's canvas implements `HUDModuleContentFactory`; an interaction bridge
translates native input into canvas actions. The shell swaps center content
inside the same panel and depth planes. It does not create a window per section.
The Power screen is a legacy special case owned directly by the shell.

## Lifetimes

`OverlayController` owns session services and persistent stores. Closing the HUD
releases `SystemHUDView`, its canvases and backing layers; it retains the selected
section and feature data. Settings live in `ConfigurationStore`, outside the
view. Quit, app launch, Finder reveal and Storage Settings handoffs finish the
closing animation before activating their destination.

Pointer following continues during opening and closing. The source playback
clock drives source entrance/exit and ambient motion; native features share its
projection. The native fallback retains its separate finite deployment and idle
animation tracks. Full teardown stops presentation clocks and removes tracks. Generation tokens reject stale animation and asynchronous
completion callbacks. Module deactivation removes visible-only observers,
editors, accessibility controls and display timers.

The native host owns one AppKit cursor tracking area while the desktop HUD is
visible, including opening/closing; the standalone source viewer owns its own.
The cursor area uses `activeInKeyWindow`; independent hover tracking uses
`activeAlways`, which AppKit does not support for cursor updates.
Visible-rect tracking areas retain their identity across native layout. The
original cursor bitmap is composited in one untransformed top-level layer while
the HUD owns the active key window. Its balanced native-cursor hide lease is
released for native editors, menus, control tracking, file drags, focus loss,
concealment and teardown. Existing app-local pointer delivery positions the
small layer with implicit animations disabled; repeated positions do not change
it. There is no added event monitor, polling timer, capture service or cursor work
in ambient rendering. AppKit still handles cursor-update arbitration and native
text/drag cursors. Projected text editors explicitly choose the I-beam using
their visible input geometry, rather than their invisible native backing frames. This
follows [AppKit tracking-area cursor ownership](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/EventOverview/TrackingAreaObjects/TrackingAreaObjects.html).
Unchanged hover and wordmark styles are compared before making
mutable copies, so settled ambient frames do not rewrite that dictionary.
Gyro updates share the finite transition clock's visibility gate: delayed
WindowServer occlusion notifications cannot freeze entrance/exit tilt, while
concealed and occluded steady-state views remain stopped.

A one-time utility-queue launch preparation decodes immutable source metadata
and prepares the desktop shader functions, including required clipped variants
within the fixed eight-pair limit. It creates no hidden renderer or
textures and runs no recurring timer. Immutable programs remain in the existing
bounded cache after closing.

Some work intentionally continues with the HUD closed:

- Activity history samples at 5-second intervals, increasing to 1 second when
  active. Per-application activity and its network helper stop when hidden.
- Clipboard watching checks `NSPasteboard.changeCount` once per second before
  decoding content. It pauses when the application session is suspended.
- Work Mode uses elapsed/deadline time independently of its visible display
  timer. User-enabled per-app audio routes also outlive the Volume section.

Map camera input transforms a few retained image layers. A single background
worker paints a bounded detail image; newer requests replace pending work.
Deactivation cancels work and releases geometry caches. The map, motion and
telemetry code do not sample system statistics on every display frame.

## Persistence and platform boundaries

| Data | Storage |
| --- | --- |
| Settings and shortcut configuration | User defaults |
| Notes | SQLite plus managed image files |
| Shelf and app shortcuts | JSON metadata and file bookmarks; shelf items are references |
| Personal card and map pins/camera | Versioned JSON; imported profile images are managed separately |
| Event Log | Bounded JSON archive |
| Clipboard cache and activity history | Bounded session memory |

The existing bundle identifier and `EndfieldCharge` Application Support paths
are compatibility identifiers. Renaming these without a migration would lose
access to current preferences, login registration or user data.

Keep platform calls in their feature service: Core Audio in the audio
controllers/engine, process metrics in the monitors, bookmark access in the
shelf/shortcut stores, and Focus automation in `WorkModeFocusController`.
The UI consumes results and capability states instead of simulating unsupported
system behavior. `HUDResources` centralizes bundled resource lookup; release builds
compile out the source-tree fallback used by standalone development tools. Third-party assets and dataset provenance belong in
`CREDITS.md` and the feature documentation.

## Tests, builds and remaining seams

`L10n` resolves English, Simplified Chinese, Traditional Chinese and Japanese.
Literal templates and interpolated values are separate, so translated messages
do not change user content. `LocalizationCatalog` stores contextual translations;
source-coverage tests flag new untranslated strings. Background audio jobs capture
their language when submitted. Visible canvases invalidate labels for the resolved
language, including switches between the two Chinese scripts.

`scripts/render-readme-previews.sh` renders the real HUD into documentation images
and GIFs (requires FFmpeg). It uses temporary stores, a private clipboard, a sample
profile and fixture telemetry. It does not capture the desktop or read personal
application data. Generated intermediate frames stay in ignored `build/` storage.

`scripts/test.sh` runs model, geometry, canvas and lifecycle checks using
injected clocks/readers where possible. Graphical verification helpers exercise
the actual AppKit panel with isolated data. Map benchmark programs and app-launch
fixtures are separate executables under `Tests`; they are not ordinary app
entry points. `build.sh` creates the optimized universal app, `dev.sh` creates a
native development build, and `package.sh` creates distribution archives.

The main remaining concentration points are `SystemHUDView` (composition,
projection and event routing), `AppDelegate` (application orchestration plus
diagnostic entry points), and `HUDNavigation` (layout and artwork). Their size is
a maintenance cost; this release cleanup does not claim to remove it. Future
extractions should follow those ownership boundaries and retain the existing
lifecycle tests, rather than introduce another parallel state model.

Sources remain flat to avoid changing every build and benchmark path during a
release. If directories are introduced later, update source discovery and the
explicit test manifest first. A suitable grouping is `App`, `Shell`, `Platform`
and feature folders containing each feature's store/controller, canvas and
interaction bridge. Keep diagnostics separate from production orchestration.
