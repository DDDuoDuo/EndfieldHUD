# Architecture

EndfieldHUD is a native AppKit application with a Core Animation HUD. It uses
the macOS SDK directly, with no third-party runtime dependencies. The build
scripts compile the flat `Sources` directory; folders are not Swift modules.

## Ownership

| Layer | Main types | Responsibility |
| --- | --- | --- |
| Application | `AppDelegate`, `GlobalShortcutController`, `LoginItemManager` | Menu bar, registered shortcut, configuration changes, system notifications, startup and shutdown |
| Presentation | `OverlayController`, `SystemOverlayState`, `ShelfDragPresentationState` | Shared panel, display choice, focus, opening/closing, drag concealment and app-launch handoffs |
| HUD shell | `SystemHUDView`, `HUDNavigation`, `HUDModuleContent` | Retained navigation and center content, module composition, projection and event routing |
| Motion and artwork | `HUDMotionController`, `HUDDepthPlane`, `HUDDeploymentFlicker`, `HUDSubsectionTransition` | Separate deployment, pointer and ambient transforms; scoped animation cleanup |
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

Pointer following is event-driven and can run during opening/closing. Ambient
tracks start only after the finite opening transaction finishes. Full teardown
removes both. Generation tokens reject stale animation and asynchronous
completion callbacks. Module deactivation removes visible-only observers,
editors, accessibility controls and display timers.

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
