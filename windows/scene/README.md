# Native scene feasibility core

This is a Windows-independent C++20 port of the source scene's transform,
animation, camera and inverse raycast contracts. It is infrastructure for the
first feasibility gate, not a completed HUD or proof of visual parity.

Authorities are `Sources/HUDSourceScene.swift`,
`HUDSourceWatchAnimation.swift`, `HUDSourceWatchCamera.swift`,
`HUDSourceImageGeometry.swift`, `HUDSourceWatchFrameBuilder.swift`,
`HUDSourceWatchLayout.swift`, `HUDSourceCanvasSorting.swift`,
`HUDSourceWatchButtonAnimation.swift`, `HUDSourceSelectableColor.swift` and
`HUDDeploymentFlicker.swift`, plus the desktop path in
`HUDSourceWatchDocument.swift`, `HUDSourceDesktopNavigationLayout.swift` and
`HUDSourceWatchView.swift` at the fresh GitHub baseline
`4036174a3facf935260f4d0a9c63bfff33b98c37`. Mac
sources and resources remain unchanged.

`Document::load` reads the actual exported scene, wrapper clips, per-instance bound button
clips and packed controller transition metadata. Identifiers
remain complete CAB:path strings, including signed values beyond 2^53; numeric
animation and geometry values use doubles. Hierarchy traversal follows child
order. The root's serialized zero scale is replaced only by the explicit
runtime canvas initialization. The finite wrapper applies OutQuad once, while
source key interpolation preserves Hermite, stepped and weighted Bezier tracks.

The native desktop host must use `Document::loadDesktop`, matching
`SystemHUDView`'s `HUDSourceWatchView(desktopMode: true)`. It mounts the selected
45-node BP13 card into its original empty parent, validates disjoint signed
identities and required bindings, and merges original then card components,
sprites, textures and materials. `load` stays available for raw reference
contracts. Desktop frames omit all game `UIText`, game-only branches and the
bottom shadow's unrelated children; they expose the original 22 main buttons
plus the two explicit Techtree/Report supplements, without game lock or unread
state. Hidden nodes remain queryable source geometry for native planes.

`desktopInfo()` exposes the exact profile bindings and root highlight,
center/status anchors, original ordered row pool and navigation extents.
`Button::captionNodeId` and `iconNodeId` bind native overlays to authored source
rects. `FrameInput::desktopEntryCount` and normalized scroll sample the canonical
row reuse formulas before source layout/slant: no node cloning, fixed logical
pitch, original deployment depth and first-column placement for partial rows.
`Frame::desktopRightAssignments` maps the current physical slot to its logical
right-side entry. `NodeGeometry` carries inherited alpha, masks and source/world
matrices for native caption/icon alignment.

`desktop_scroll.hpp` ports the pure wheel/spring portion of
`HUDSourceDesktopScrollMotion`. The host supplies normalized deltas and the same
frame clock through `scroll`/`advance`, resets on lifecycle changes, and parks
when `requiresFrames()` is false. It retains 180-unit wheel edge travel,
0.55 overflow gain, source-length epsilon, decay 11/frequency 15, the two-second
integration cap and exact delayed-frame settling. Availability reads the bounded
target. Only the canonical desktop navigation content uses the source 240-unit
layout presentation allowance; row assignment remains bounded while its content
visibly rebounds. Other source ScrollRects keep their original 0...1 clamp.
Windows wheel-line preferences and projected pixel-to-source conversion belong
to the host. Precision touchpad gesture phases, OS momentum ownership and gesture
continuation remain unverified; this wheel API does not synthesize them.

`Frame::scrollIndicators` exposes the original outer UpLine/BottonLine planes,
not the nested decorative duplicates. They retain exact source rects, masks and
world/scene matrices through camera-only reprojection. Supply the bounded motion
target in `FrameInput::desktopScrollTarget`: up is available below `1-1e-9`, down
above `1e-9`, provided the canonical content overflows. The source styles use
linear Float tint 1/0.32 and opacity 1/0.65, multiplying the sampled authored
alpha throughout opening/closing. `scrollDirectionAt` includes disabled active
arrow hits; the host checks `canScroll` on matching release so disabled arrows
consume input without dismissing the HUD. Input becomes active only in the
visible phase. Mouse activation uses the source's animated 32-point step,
converted through the actual projected viewport into source units; accessibility
activation follows the source default immediate step.

Desktop default graphic policies retain side edge opacity 0.18, center
triangle/ring opacity 0.88 and text glow opacity 0.78. Broad profile/portrait
additive Lights are suppressed, retaining the root ColorTint target alpha for
the caller's prepared hover plate. Profile outline and quit background gains
follow the original selectable fade. The background selects the canonical
`business_card_topic_normal_1` sprite. `DesktopPresentation` accepts explicit
replacement hiding, finite source properties and styles; `normalMaterialNodes`
is applied only when the caller supplies its normal-alpha replacement. Native
avatar, accent-themed background/hover artwork, persisted profile fields and
native typography remain the host/provider's responsibility.

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
Appended native source-plane graphics preserve their calibration in
`Graphic::nodeLocal`; source slant/gyro uses the new node world times that local
matrix. `fixedWorld` retains the already-projected screen/design footer world.
`initialRootRotation()` returns the normalized authored camera root quaternion
for gyro initialization and neutral native-plane calibration (identity in the
selected source asset).
There are no timers, renderer loops, OS services, accounts or application-data
paths here. The lifecycle owner must stop requesting frames when concealed.
`Frame::sceneTime` retains the caller's finite monotonic clock for animated
shader UVs. Pointer-only `reproject` preserves this clock; the host explicitly
updates it when submitting a later camera-only frame.

`DeploymentFlicker` retains the source SplitMix64 pulses, original candidate
indices, unsigned wrapped seeds, directional delays, duplicate and selected
ancestor suppression, local Float baselines and linear additive opacity.
Opening gates backward-fill until a group's turn; closing gates keep the group
off after completion without retaining frame demand. Applying to a subset
cancels its old tracks before the reduced-motion guard and retains unselected
tracks, matching `HUDDeploymentFlicker.apply(to:)`. `cancel()` clears all tracks
when concealed or reused. The effect has no timer and no per-frame randomness.

The source `SystemHUDView.addDeploymentFlicker` selects legacy artwork in this
order: machinery plus progress; visible navigation cards excluding power and
profile plus identity-card children; readout descendants; the selected profile
background; header/footer lettering plus industry wordmark; notes. Its opening
sweep uses duration 0.13, delay 0.20 and span 0.25; closing uses 0.11, 0.01 and
0.23, both in canvas Y range 0...640. `DeploymentOptions::legacySweep` preserves
those constants. The source Watch branch follows its wrapper clips and does
not call this helper, so Windows does not infer legacy groups from Watch sprites.
The charge badge's separate ungated application can use the same owner with its
own explicit group and timing.

For an approved artwork provider, measure each group's visual bounds before
mechanical transforms change: shape-path bounds first, otherwise nonempty local
bounds, otherwise the union of converted child visual bounds. Convert the
center into the caller's source coordinate space once. Supply those ordered
group snapshots directly to `DeploymentFlicker::begin`, or use
`Document::deploymentGroups(settledFrame, orderedCandidates)` for explicitly
selected source nodes. The document helper derives ancestry and local
CanvasGroup model opacity; the caller provides the settled Y and additional
layer delay. It does not substitute screen pixels for source artwork coordinates.

Pass the owner through `FrameInput::deployment` only for these explicit source
groups. Sample `opacity` directly for external artwork. Supply one seed per
opening/closing and the same clock as scene playback. Rebuild through the final
finite endpoint, then park; `requiresFrames` excludes held closing fill. Clearing
or bypassing tracks for reduced motion requires a fresh frame. The effect adds
its captured baseline to the current local group opacity; it never scales a
reveal curve's sampled opacity as a replacement animation. Source hit regions,
wrapper curves, ambient transforms and backdrop/blur remain independent.

Metadata can be plain source JSON or supplied through the `ResourceReader`
callback after bounded complete EHUDZ01 decoding. The default reader rejects
packed files. Native packaging stages byte-exact decoded approved metadata at
`Resources/NativeScene/Scene` and `Resources/NativeScene/Meshes`; source PNGs
resolve to `Resources/WatchSource/Scene` for source-path diagnostics. Native
rendering uses each `Graphic::textureId` with approved `WatchSource/textures.json`
and original mip data instead of relying on PNG deployment. Twelve metadata
files are required: Scene/{scene,clips,controller-transitions,runtime-root-camera,
sprites,watch-blur,materials,desktop-profile-card}.json and
Meshes/{Equipring,watchline,Plane,Cylinder}.json. It does not use documentation GIFs or
private reference exports. The 128 MiB bound applies per metadata input.

UI images preserve original trim, UVs, aspect, sliced borders, tiling and all five filled-image methods, including
original radial cuts in geometry and UV coordinates. Mesh
graphics retain source triangle positions/UVs, raw RGBA channels and material IDs. A triangle uses
the renderer's quad stream with a repeated fourth vertex; sampled material
channels remain separate in `Graphic::sampledProperties`. Native HLSL must still
implement those channels at their original shader positions.
`Graphic::colorQuads` is empty for implicit white mesh vertices, or aligned with
triangle quad order, including the repeated fourth vertex. Mesh color channels
are raw bounded source floats and bypass UI Color32/gamma policy. All four
currently approved source meshes omit these channels. Enabled
`UIGraphicAnimation` emits its sampled inverse scale and centered offsets as
`material._VFXMainTex_ST.{x,y,z,w}` and alpha as `material._TintColorAlpha`, at the
source Float uniform boundary. The component changes its cloned material rather
than the transform; RawImage also retains its cloned material binding.

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

- Desktop precision-gesture/momentum integration; custom GridLayoutGroup,
  NotchAdapter and UIStepScrollList behavior.
- Slant Tick scheduling relative to the original engine Canvas rebuild,
  source layout/transition screenshots and matched native frame cost.
- Desktop ambient per-opening seeded variation and source-language replacement.
- Shader/material effects, blending, soft masks, HDR composite and text
  metrics/font fallback. Exact texture IDs and mip bindings are available;
  their native shader behavior belongs to the render adapter.
- Mounting the approved legacy artwork groups before applying their sweep and
  checking visual output against the released Mac source frame/time/pointer.

`scene_tests` checks synthetic curve, lifecycle, gyro, sweep, projective input,
all filled methods and an independent in-memory exported scene. That fixture
checks layout priorities, disabled sorting/ignore registration, scroll/slant,
finite controller speed and normalized/interrupted blends, separate hover,
ColorTint fades/enable/disable, reduced motion and slant-aware reproject.
Explicit deployment tests cover source seeded reference values, candidate-index
filtering, ancestor/duplicate suppression, additive baselines, frozen sweep
centers, reduced-motion cancellation and finite/held endpoints. Shader channel
fixtures verify raw mesh RGBA, bounds validation, sampled material scale/alpha,
zero-scale threshold, cloned RawImage binding and preserved scene clock.
Passing an actual scene path also checks the current resource graph, clip
lengths, source button instances, gyro endpoints, sorting, exact texture IDs,
mesh/UV alignment and 100 isolated open/close snapshots. Actual desktop checks
independently verify canonical card identity/size/mounting, raw game-text
suppression, game-only hiding, original button/hit geometry, chosen background,
glow policies, bounded row recycling/partial rows, finite property overrides,
native source-plane reprojection and fixed footer continuity. These checks do not
prove screen capture, mixed DPI, IME, hardware pacing or rendering parity.
Wheel tests independently check source spring samples/cadence, accumulated
retargeting, both edge bounds, invalid input, reduced motion, velocity demand,
source-length epsilon and exact settling. Actual scene checks verify visible
rebound displacement, bounded row assignments and inverse-hit reachability of
the final source slot after scrolling.
