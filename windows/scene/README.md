# Native scene feasibility core

This is a Windows-independent C++20 port of the source scene's transform,
animation, camera and inverse raycast contracts. It is infrastructure for the
first feasibility gate, not a completed HUD or proof of visual parity.

Authorities are `Sources/HUDSourceScene.swift`,
`HUDSourceWatchAnimation.swift`, `HUDSourceWatchCamera.swift`,
`HUDSourceImageGeometry.swift`, `HUDSourceWatchFrameBuilder.swift`,
`HUDSourceWatchLayout.swift`, `HUDSourceCanvasSorting.swift`,
`HUDSourceWatchButtonAnimation.swift`, `HUDSourceSelectableColor.swift` and
`HUDDeploymentFlicker.swift` at this migration branch's source baseline. Mac
sources and resources remain unchanged.

`Document::load` reads the actual exported scene, wrapper clips, per-instance bound button
clips and packed controller transition metadata. Identifiers
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
geometry for the requested clip pose; `reproject` updates camera/world
matrices and reruns the source slant writer against the cached pose before
slant, while keeping geometry allocations when the pointer alone changes.
There are no timers, renderer loops, OS services, accounts or application-data
paths here. The lifecycle owner must stop requesting frames when concealed.
The deterministic flicker utility retains source pulses and directional gates;
the shell must group artwork and measure settled centers before folding it.

Metadata can be plain source JSON or supplied through the `ResourceReader`
callback after bounded complete EHUDZ01 decoding. The default reader rejects
packed files. Native packaging stages byte-exact decoded approved metadata at
`Resources/NativeScene/Scene` and `Resources/NativeScene/Meshes`; source PNGs
resolve to `Resources/WatchSource/Scene` for source-path diagnostics. Native
rendering uses each `Graphic::textureId` with approved `WatchSource/textures.json`
and original mip data instead of relying on PNG deployment. Eleven metadata
files are required: Scene/{scene,clips,controller-transitions,runtime-root-camera,
sprites,watch-blur,materials}.json and Meshes/{Equipring,watchline,Plane,Cylinder}.json. It does not use documentation GIFs or
private reference exports. The 128 MiB bound applies per metadata input.

UI images preserve original trim, UVs, aspect, sliced borders, tiling and all five filled-image methods, including
original radial cuts in geometry and UV coordinates. Mesh
graphics retain source triangle positions/UVs and material IDs. A triangle uses
the renderer's quad stream with a repeated fourth vertex; sampled material
channels remain separate in `Graphic::sampledProperties`. Native HLSL must still
implement those channels at their original shader positions.

Source horizontal/vertical group and ContentSizeFitter passes retain measurement
priorities, LayoutElement ignore semantics, parent rect anchors, child scaling
and independent controller depth. ScrollRect resolves original transformed
bounds and normalized positions; UIScrollCellSlantEffect replaces only source
world X. Canvas sorting writers register even when disabled, receive the panel
base directly, and clear mask chains at override boundaries. Renderer sorting
writers retain their separate absolute offsets.

`ButtonMotion` owns one monotonic state clock supplied by the host. It joins
original controller states to per-instance clips, applies source speed/cycle
offset and fixed or normalized transition duration, snapshots interrupted
blends, preserves separate Pressed/hover visibility and stops demand at finite
endpoints. Selectable ColorTint has its own Float renderer channel, original
fade duration, null-target behavior and white clear on component disable.
`Graphic::vertexColorReady` means source Color32 quantization, renderer tint,
nearest-Canvas gamma policy and inherited alpha have already been applied;
renderers must not quantize or linear-convert that RGB again.

Still requiring implementation or validation:

- Desktop navigation recycling, elastic/inertial scheduling and profile mounting;
  custom GridLayoutGroup, NotchAdapter and UIStepScrollList behavior.
- Slant Tick scheduling relative to the original engine Canvas rebuild,
  source layout/transition screenshots and matched native frame cost.
- Desktop ambient per-opening seeded variation and source-language replacement.
- Shader/material effects, blending, soft masks, HDR composite and text
  metrics/font fallback. Exact texture IDs and mip bindings are available;
  their native shader behavior belongs to the render adapter.
- Integrating flicker with the shell's settled groups and checking the result
  against the released Mac source frame/time/pointer.

`scene_tests` checks synthetic curve, lifecycle, gyro, sweep, projective input,
all filled methods and an independent in-memory exported scene. That fixture
checks layout priorities, disabled sorting/ignore registration, scroll/slant,
finite controller speed and normalized/interrupted blends, separate hover,
ColorTint fades/enable/disable, reduced motion and slant-aware reproject.
Passing an actual scene path also checks the current resource graph, clip
lengths, source button instances, gyro endpoints, sorting, exact texture IDs,
mesh/UV alignment and 100 isolated open/close snapshots. These checks do not
prove screen capture, mixed DPI, IME, hardware pacing or rendering parity.
