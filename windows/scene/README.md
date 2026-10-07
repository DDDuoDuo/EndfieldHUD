# Native scene feasibility core

This is a Windows-independent C++20 port of the source scene's transform,
animation, camera and inverse raycast contracts. It is infrastructure for the
first feasibility gate, not a completed HUD or proof of visual parity.

Authorities are `Sources/HUDSourceScene.swift`,
`HUDSourceWatchAnimation.swift`, `HUDSourceWatchCamera.swift`,
`HUDSourceImageGeometry.swift`, `HUDSourceWatchFrameBuilder.swift` and
`HUDDeploymentFlicker.swift` at this migration branch's source baseline. Mac
sources and resources remain unchanged.

`Document::load` reads the actual exported scene and wrapper clips. Identifiers
remain complete CAB:path strings, including signed values beyond 2^53; numeric
animation and geometry values use doubles. Hierarchy traversal follows child
order. The root's serialized zero scale is replaced only by the explicit
runtime canvas initialization. The finite wrapper applies OutQuad once, while
source key interpolation preserves Hermite, stepped and weighted Bezier tracks.

Camera layout preserves the source's documented float arithmetic boundaries,
standard ratio, narrow-screen FOV adjustment, Unity Euler order and quaternion
OutQuad retarget. Client pointer coordinates use physical pixels and flip Y
once. A single finalized frame provides drawing and inverse hit testing,
including local raycast padding and ancestor RectMask2D masks. Graphic hits use
the source's rectangle/mask contract rather than image alpha or an enclosing
screen rectangle.

The immutable document loads metadata once. `frame` constructs source local
geometry for the requested clip pose; `reproject` updates only camera/world
matrices while keeping geometry allocations when the pointer alone changes.
There are no timers, renderer loops, OS services, accounts or application-data
paths here. The lifecycle owner must stop requesting frames when concealed.
The deterministic flicker utility retains source pulses and directional gates;
the shell must group artwork and measure settled centers before folding it.

Metadata can be plain source JSON or supplied through the `ResourceReader`
callback after bounded complete EHUDZ01 decoding. The default reader rejects
packed files. Native packaging stages byte-exact decoded approved metadata at
`Resources/NativeScene/Scene` and `Resources/NativeScene/Meshes`; source PNGs
resolve to `Resources/WatchSource/Scene`. It does not use documentation GIFs or
private reference exports. The 128 MiB bound applies per metadata input.

UI images preserve original trim, UVs, aspect, sliced borders and tiling. Mesh
graphics retain source triangle positions/UVs and material IDs. A triangle uses
the renderer's quad stream with a repeated fourth vertex; sampled material
channels remain separate in `Graphic::sampledProperties`. Native HLSL must still
implement those channels at their original shader positions.

Still requiring implementation or validation:

- Layout writers, canvas sorting writers, source slant/scroll and desktop
  navigation recycling/profile mounting.
- Per-button Animator transitions, selectable tints, desktop ambient
  per-opening seeded variation and source-language replacement.
- Shader/material effects, original mesh texture bindings, blending,
  color-space conversion, soft masks, mip chains and HDR composite.
- Native text metrics/font fallback and projected editor presentation.
- Partial radial filled-image geometry (currently omitted explicitly).
- Integrating flicker with the shell's settled groups and checking the result
  against the released Mac source frame/time/pointer.

`scene_tests` checks synthetic curve, lifecycle, gyro, sweep and projective input
contracts. Passing an actual scene path also checks the current resource graph,
clip durations, gyro endpoints, mesh/UV alignment, pointer-only reproject and
100 isolated open/close snapshots. These checks do not prove screen capture,
mixed DPI, IME, hardware frame pacing or rendering parity.
