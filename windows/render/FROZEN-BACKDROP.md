# Frozen desktop backdrop prototype

This Windows component supplies a bounded, memory-only pre-opening snapshot and
an SDR GPU underlay. It does not establish the migration's live backdrop or
recording acceptance gate. No live desktop capture is exercised by its tests.

## Source authority and adaptation

The integrated Mac desktop explicitly disables the separate WatchBlur capture
path in `Sources/HUDSourceWatchView.swift:229-232`. It retains an active native
`NSVisualEffectView` using `.hudWindow` and `.behindWindow` in
`Sources/SystemHUDView.swift:507-511`. The private native blur kernel and material
cannot be derived from those declarations. This Windows frozen snapshot is the
pre-opening fallback permitted by `WINDOWS-MIGRATION.md:139`; its quarter-size
Gaussian prototype is not an exact port of that native material. The separate
WatchBlur/FrostedGlass shader and HDR capture path are not activated here.

The source skin settings are retained: `Models.swift:58-59` defaults to darkness
0.63 and blur amount 0.75; low-power mode disables blur. `SystemHUDView.swift:2891-2898`
uses encoded gray 0.015 in dark mode and 0.90 in light mode, with black radial
vignette alpha stops `[0, 0.06, 0.38]` or `[0, 0.02, 0.14]`. Its radial start
`(0.5, 0.46)`, end `(1, 1)`, and stops `[0, 0.5, 1]` are declared at lines 2279-2282.

The native renderer supplies the sampled source `Frame.backdropAlpha` envelope.
The fixed screen underlay composes the darkness and child vignette first, then
applies their parent envelope once. The separate blur container receives
`blur_amount * envelope`. The HUD retains its own source animation. Exact Mac
radial rasterization and implicit layer grouping still need matched visual
acceptance.

## API and ownership

`FrozenDesktopSnapshot::from_bgra(bounds, pixels, output)` accepts explicitly
supplied, opaque, top-origin, tightly packed BGRA8 SDR fixture pixels. Bounds are
physical coordinates; negative monitor origins are retained. It rejects invalid
extent, incomplete coverage, nonopaque alpha and unknown provenance. The owned
bytes are immutable and do not alias the caller's mutable input.

`capture_desktop_pre_open(hiddenHud, bounds, stopToken, output)` is an explicit
opening-only worker operation. It requires a valid hidden HWND, an exact current
full monitor rectangle, and PMv2 physical coordinates. It checks cancellation
and visibility before capture and publication, and revalidates monitor geometry.
It maps the active display path to the selected GDI monitor, rejects enabled
advanced color, clone/virtual/unmapped providers or failed capability queries,
and returns failure without publishing stale pixels. It performs one
`BitBlt(SRCCOPY | CAPTUREBLT)` into a temporary top-origin DIB. It owns no loop,
file, persistent capture exclusion or HUD visibility changes.

GDI capture is labeled `unverifiedGdiSdr`. Its byte format and disabled advanced
color do not prove a matching sRGB/ICC profile. Microsoft documents that
[BitBlt performs no color management and CAPTUREBLT includes layered windows](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-bitblt).
The prototype cannot exclude arbitrary windows above the HUD, guarantee protected
content capture or provide a globally atomic desktop image. HDR and profile
matching remain unresolved. Those failures use the source tint/vignette fallback.

The snapshot limit is 8192 pixels per side and 64 MiB of opaque pixel data.
GPU preparation owns one immutable full-size BGRA8 sRGB texture and two quarter
size RGBA16F textures. The Gaussian uses 19 normalized taps per axis, with sigma
3 at quarter resolution, approximately 12 physical pixels. This radius is an
explicit prototype choice. Blur is computed once during preparation in decoded
linear RGB; fades allocate no pixel resources and perform no readback. CPU plus
GPU retained pixel storage is bounded to approximately 144 MiB, excluding driver
overhead. Capture temporarily owns an additional DIB up to 64 MiB.

`FrozenBackdrop::initialize(device, context)` creates a cached native pipeline.
`prepare(snapshot)` replaces old CPU/GPU pixel resources, uploading and blurring
on the renderer's owning thread. `draw(sourceTarget, width, height, envelope,
style)` overwrites the full fixed screen underlay in linear-premultiplied RGB
through a BGRA8/RGBA8 sRGB RTV. It validates target device, format, dimensions,
single-sample extent and mip zero. With no snapshot it writes tint/vignette
alone. `clear()` releases all retained snapshot bytes and three pixel textures;
the cached shader states remain available for fallback. Diagnostic queries are
`ready()`, `texture_count()`, `snapshot_byte_size()` and `retained_pixel_bytes()`.

## Host integration contract

1. Begin preparation while the HUD is hidden. Retain at most one capture worker
   and pass the selected full physical monitor bounds. Keep opening clocks held.
2. Reject late completion using a host generation ticket after close, timeout,
   geometry/topology change or device loss. The pure `BackdropPreparation`
   coordinator implements the source's three-second readiness deadline and
   consumes each completion once. Cancellation alone does not make a late worker
   completion acceptable.
   An executing `BitBlt`/`GdiFlush` cannot be interrupted by a stop token; the
   deadline bounds fallback and publication, while shutdown joins the worker.
   Hidden-HWND checks do not prove the composed desktop has removed a previous
   HUD or dismissed tray menu. Rapid-reopen freshness remains a live gate.
3. Upload an accepted immutable result on the renderer thread before showing the
   HUD. Capture/upload failure or deadline expiry opens with tint/vignette alone
   and reports backdrop unavailability through native status. Start source
   opening clocks only after accepted preparation or fallback.
4. Draw the fixed underlay into the source sRGB intermediate first. Then draw
   source geometry with target preservation, run `SourcePresentationAdapter`
   into the encoded-premultiplied compositor target, and draw native editor
   content in its existing order. Do not tilt or perspective-transform the
   backdrop with the Watch scene.
5. Keep the accepted snapshot through the closing envelope. Release it when
   concealed and invalidate it on monitor/DPI/device changes. Stop capture and
   frame submission while closed. Diagnostics use explicitly supplied synthetic
   snapshots or no snapshot; they never call the capture API.

## Isolated validation

`frozen_backdrop_tests` uses supplied pixel fixtures and D3D11 WARP. It verifies
negative-origin and overflow-safe bounds, byte/provenance/opacity rejection,
immutable ownership, early cancellation before desktop APIs, target validation,
source skin controls, group-envelope ordering, dark/light/low-power behavior,
linear black/white averaging, row orientation, repeated draw resource counts and
release to zero retained CPU/GPU pixels. Test-owned staging textures are used
only for synthetic GPU readback. `backdrop_preparation_tests` separately exercises
delayed readiness, held opening, cancel, exact timeout, supersession and rejected
late completion using fake time and callbacks.

Live self-exclusion, mixed-DPI/hotplug capture, profile fidelity, HDR, native blur
parity, startup responsiveness and recording of the HUD/cursor remain
unverified acceptance items.
