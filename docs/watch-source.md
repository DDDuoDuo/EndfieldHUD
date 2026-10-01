# Original Watch scene renderer

The Watch overview now uses the installed game's `WatchPanel_PC` scene instead
of the earlier Core Animation composition. Its original icons, Chinese labels,
button order, three-dimensional transforms, camera, animation clips and material
programs drive the macOS menu. Clicking the 22 main buttons opens the existing
macOS functions. Those feature panels retain their existing desktop interfaces.

## Runtime behavior

- The source perspective camera and `UICanvasScaleHelper` determine projection
  and canvas dimensions. The PC reference is 2400×1350; a 1728×1080 viewport uses
  a 2400×1500 canvas. `UIManager.SetUICameraFOV` preserves the standard horizontal
  FOV on narrower screens; the serialized 15.381800° vertical FOV therefore
  becomes 17.066957° at 1728×1080. Projection and world-canvas scaling use this
  same runtime FOV. The prefab's saved scale is not a runtime camera scale.
- Pointer motion rotates the original world UI root. The original pitch/yaw
  curves, biased center, ±2/3 degree limits and 0.5-second OutQuad quaternion
  tween are retained. Rendering and raycasts resolve the same pose and masks.
- Opening and closing sample the complete 0.75-second and 0.333333-second source
  clips. Their wrapper advances with OutQuad; the 13.683333-second ambient clip
  advances linearly. Weighted scalar keys invert the time handles before
  sampling; baked quaternion keys are retained.
- Legacy scalar curves preserve their source class ID and property names.
  RectTransform's own registry handles anchored position, anchors, size and
  pivot; the installed player's serialized-type-tree fallback also accepts
  inherited local X/Y and scale channels. For class-224 local X/Y, the fallback
  writes serialized cache fields; the native RectTransform finalizer then
  derives visible X/Y from anchored position, anchors and pivot. The adapter
  records those original sampled keys without treating cache fields as graph
  position setters. This preserves the original rows even without a parent
  layout driver. Class-4 Transform graph channels remain separate. Later layout
  writers override the axes they drive while preserving other animated axes.
  This Legacy proof does not establish every packed Animator binding entry.
- Background opacity follows WatchBlur's separate Linear wrapper and original
  0.13333334-second alpha keys: an unweighted Hermite entrance and near-linear
  exit. These keys are not stretched to the main menu's duration. On macOS 14+
  with screen-capture access, desktop pixels below the HUD feed the original
  FrostedGlass filters and WatchBlur material. The system blur remains the
  fallback when capture is unavailable. This desktop input adapter does not
  establish the game's main-scene grading or capture-pass schedule.
- Each button uses its own bound Animator clips. Highlighted is a finite flash
  with source local-Z channels, followed by its held endpoint. Normal and
  Pressed use the original transition duration. Rapid changes preserve the
  current blended pose. Repeated pointer samples do not restart the flash.
- A stationary pointer is raycast again when animation or the gyroscope moves
  the menu. Mouse release and accessibility activation share the original
  per-button 0.10000000149-second click cooldown and resolved hit eligibility.
  The original 58-pixel cursor and zero hotspot are used while the menu has
  focus; the adapter restores the previous cursor when it leaves or conceals.
  The game's runtime cursor/DPI overrides are still unobserved.
- Original layout writers and the slant effect run before rendering. Slant
  replaces the cell's world X while retaining its world Y/Z. Scroll clipping
  and inverse-plane hit tests include the external world-root rotation.
- A single view-owned clock samples these effects. Closing, concealment and
  window teardown stop its timer. Reduce Motion samples endpoints; ambient-off
  and low-power presentation freeze ambient and shader time while preserving
  finite interaction.

The desktop availability policy enables every mapped main button, matching
Watch's runtime activation rather than leaving an inactive prefab button hidden.
Game-account lock, safe-zone and unread markers are disabled. It does not infer
an account's progression or notifications.

## Rendering and resources

`Resources/WatchSource/Scene` contains exact source identities, component data,
bound clips, Sprite rectangles/UVs, font glyph metrics and runtime camera data.
`Scene/Domain` contains the original region/level hierarchy, map meshes,
materials, original mip chains and runtime Chinese level-name bindings.
`Shaders` contains the translated source programs and reflected interfaces.
`NOTICE.txt` records the pinned MIT uGUI geometry reference.

The renderer uses original Sprite trimming, slicing, tiling and fill rules,
source SDF glyphs and font material underlay. Rect clipping selects the actual
UIImage/TMP shader variants. A source null UI material resolves to the game's
`resources.assets:2` `M_ui_default`, whose required program modules were checked
against the exported equivalents. Materials preserve their fixed blend,
depth/stencil and texture state.

Soft masks use the original UIImage's full atlas, Sprite rectangle, sliced
border geometry and native Canvas-to-mask matrix. The UIImage's own color alpha
does not disable its mask: the original map mask has alpha zero. The bundled
Metal stages include the original default/TMP/VFX/world/stencil soft-mask
combinations, preserving their texture slots and existing material keywords.
Missing mask inputs are diagnosed rather than replaced with a white texture.
RectMask2D softness is taken from the nearest active ancestor mask, excluding
the graphic's own mask. Native CanvasRenderer forwards the two source axes
without a scale conversion; the Watch viewport therefore keeps its original
48-unit vertical feather, independently of HG's four-edge softness.

The original PlayerSettings prefix proves Linear color space. Canvas vertex
colors and original Color/Gamma material properties follow their separate
conversion rules; the direct LDR drawable uses sRGB output. Original texture
color-space, filtering, wrapping, anisotropy and every authored mip are retained.
BC7 textures use their original compressed data on capable devices, or the
decoded pixels of every original mip. No replacement mip chain is generated.

UIImage references loaded by `imgRefPath` are resolved through the original
AssetBundle container and persistent manifest as well as serialized Sprite
pointers. This includes the two `ui_out_ring` nodes, `deco_04` and
`ui_main_loop_bg`. An unresolved declared Sprite is diagnosed rather than
silently becoming a white rectangle. Original banner templates remain inactive;
their visible cells are derived source clones under the original list container.

HG's UI screen tuple copies `_ScreenSize`: width, height and their reciprocals.
The UI render clock uses the proved 0.05t/t/2t source components; the gameplay
clock and the recording's absolute render-clock phase remain unobserved.
HG's `_UIProjectionParams` uses the inverse GPU projection's homogeneous
`(0,1,0,1)` probe to select its Y sign, followed by near/far/reciprocal-far.
The native HG writer obtains projections with `renderIntoTexture = true`.
The port evaluates that rule on its actual adapter projection rather than
using a platform constant. `_InvViewMatrix` uses the original Camera getter's
negative-Z camera space: `Transform.localToWorld * Scale(1,1,-1)`. The original
engine getter and default view-cache writer establish that reflection. The
CPU projection/raycast adapter keeps its positive-Z convention separately.
The original offscreen UI draw's fresh null payload is zeroed by the render
graph; its callback writes `(Injected=1, FlipX=0, FlipY=0, orientation=0)`.
This is distinct from the UI3D camera enum and from later backbuffer passes.
The port pairs that flag with the GPU projection times the view matrix with
its translation column removed. The shader subtracts world camera XYZ once;
the original global stores those unchanged XYZ with W=0. The full VP remains
separate for the source non-injected path. Rotated/translated-camera tests
verify that camera-relative drawing preserves the CPU projection.
Custom view overrides and the game's live render-target pipeline still need
runtime correspondence; these shader values do not establish pixel parity.

Normal Watch entry explicitly enables the UI3D camera path and disables the
menu's own Volume. The installed pipeline requests format74,
`B10G11R11_UFloatPack32`, an RGB HDR target without alpha; its default settings
Volume profile is empty. The original UI postprocess constructor invokes copy,
distortion, bloom and UI-uber stages, whose active effects depend on actual
camera Volume state. Authored values in the disabled menu Volume are not an
active-effect preset. The live desktop view uses the packed-HDR mode after its
original WatchBlur draw has a successfully prepared desktop input; fallback
keeps the existing direct LDR mode over the system backdrop.
An explicit `sourceRGBHDR` renderer mode now draws into Metal's `rg11b10Float`
mapping of format74, clears the UI scene black, then uses the unmodified
original UberPost_CompositeUI pass1 base program726 to load and composite the
final attachment. The adapter initializes that final attachment separately;
this initialization is not a recreation of the game's earlier scene render.
The source pass samples LOD0, retains RGB and clamps alpha. No optional
bloom/vignette keyword is enabled without its original live-state gates.
All six original composite variants, reflection, parameter records and source
provenance are preserved under `WatchSource/HDR`. The final Metal RT adapter's
flip tuple is separate from the offscreen UI camera tuple. Native asymmetric
texture fixtures check both row orientations, source-alpha blending and the
sRGB output transfer; separate Watch samples retain the original packed HDR
scene blit.

The desktop backdrop uses six unmodified source FrostedGlass PS draws: horizontal
program3 and vertical program8 at ceil(input size/4), /8 and /16. The native
writer supplies each output level's `(width,height,1/width,1/height)` for both
passes; this is not the sampled input texture size. Each sample retains the
original 2.5 color threshold, and each intermediate is packed RGB format74.
The original extraction Blit pass1 then writes a single-mip RGBA8 sRGB capture.
The Metal top-left adapter supplies its explicit vertical ScaleBias. Desktop
pixels already have display grading; the no-LUT input mode is deliberate.
The game scene mode rejects missing exposure/LUT/volume inputs instead of
applying a fabricated LUT. Original PS/CS variants and native evidence are
preserved under `HDR/FrostedGlass` and `HDR/Evidence`.

WatchBlur's original full-stretch RawImage uses its real `M_ui_blur_bg` material
and both original pass states. Its Color32 is RGB 76/255, alpha1, on a Canvas
with `vertexColorAlwaysGammaSpace=false`; the source linear vertex conversion
precedes the CanvasGroup alpha animation. The two passes retain stencil Equal0
and Equal32/Keep. No extra stencil write is invented. The dynamic capture uses
the source Point/Repeat, no-mip texture binding. UI3D background sorting precedes
the Watch Window category, with its own screen-space projection.

ScreenCaptureKit excludes the HUD and every known window above it. Display
tiles retain their actual profiles and native pixel scale, then receive one
explicit ICC conversion/resample into the drawable's sRGB raster. Unsupported
or incomplete window/display mapping fails to fallback. Capture never requests
permission: only a user's menu/shortcut opening can invoke the system prompt.
The framework is weak-linked for older deployment targets. CI and preview
arguments bypass capture and permission APIs entirely; GPU checks use synthetic
profiled tiles. A successful input is currently a snapshot per opening and is
invalidated/rebuilt after a window, screen, size or backing-scale change.
The original unpatched `HGCamera.OnRecordingEnd` clears duration1 Temporal
extraction requests after one camera recording while retaining the capture RT.
Both ordinary and CPP render request paths invoke that cleanup. The default
RTExtractionDone callback is a no-op. WatchBlur's explicit registration with
`autoUpdate=false` therefore supports retaining a single captured input; it
does not require continuous desktop capture. Live IFix changes, menu reuse and
dynamic capture gates remain unobserved. Original shaders are
precompiled before the visible opening animation starts. When capture is
available, the view holds the source initial pose until its input is ready,
then starts the menu and blur clocks together. Capture delay cannot consume
the short blur entrance; cancellation prevents a late result reopening the menu.
An asynchronous preparation deadline falls back after three seconds; the host's
normal opening deadline starts again when the original animation becomes ready.
Closing during preparation cancels the held pose directly. It does not sample
the normal exit clip's fully deployed starting frame or wait for background input.
Successful geometry-matched inputs survive the opening-to-stable transition.
The deadline cannot interrupt synchronous work already running on the main thread.

Synthetic Metal checks isolate packed-RGB attachment conversion with the unchanged
original extraction copy shader and a bitwise float32 identity control. Only one
independently specified rounding mode may match all diagnostic pixels. That device
result informs the CPU oracle; it does not establish the Windows driver's behavior.
Each of the six original filter intermediates is also read back and checked using
the actual preceding GPU input. Whole-image RGB tolerances remain four bytes, or
one byte for the constant fixture; alpha remains exact.
The ICC output is an explicit premultiplied RGBA8 sRGB raster uploaded without
automatic texture-format selection. The fixture reads back every uploaded byte,
then checks colors and row orientation for mixed-scale and reconstructed RGB ICC
profiles. Missing or non-RGB profiles are rejected.
The source main UI HDR target uses finalRTSize, while its extraction copy uses
sceneRTSize. They are separate live values. This desktop adapter uses its actual
drawable extent for its final target and captured raster; it does not infer the
recording's scene size, viewport or final composite target from saved camera data.
The explicit native lifecycle fixture also supplies delayed preparation completion,
closes while input is pending, and waits for the real three-second timeout. It
checks the existing source view and host deadlines without invoking capture APIs.

WatchCtrl loads the BP13 business-card prefab separately. Its 101-node subtree
is mounted under the original PlayInfoPosNode with unchanged local TRS and its
364-by-128 size, inside the 492-by-164 parent slot. Source UIImage pixels, native
texture mip chains and CN3500 glyphs supply the card, Pelica portrait, BP1 frame,
Yvonne banner and an explicit Typhoeus weapon-banner fixture. The desktop profile
supplies its existing name, UID and permission level; game experience, rewards,
birthdays, seasons and account eligibility are not inferred. Original profile
UIButton targets open the existing macOS personal-profile module.

The deterministic fixture default has one static banner. The desktop explicitly
shows the two reference artworks as source cell clones. The list keeps its
365-by-128.5 viewport, 360-by-122 cells, 6.5 spacing and original padding.
Its four-second hold discards tick overshoot and its 0.2-second ease3 transition
uses the installed OutSine implementation. Center-changed callbacks also restart
the hold clock, and beginning a drag kills an existing tween without completing
it. The adapter advances tween, sampled center callback and hold tick in that
order; the original Unity/DOTween/Lua ordering across frames remains unobserved.
Original non-interactable page toggles indicate state; they are not new buttons.
Desktop banner buttons open the existing event-log module. This reference
sequence is not a recovered game account's eligible list or its JumpOut targets.
Three activity decoration materials require
additional source shader variants and remain behind the original inactive gates.
Controlled GPU fixtures use generic profile text and original artwork without
publishing the supplied recording's personal name or UID. Widget source/projection
provenance is recorded in Scene/Widgets/provenance.json.

Map geometry uses the source instance matrices, submeshes and material slots.
The enabled `UIRegionBuildingTexManager` components bind their own original
outline Texture2D as `_BuildingTex`. The source checks every shared-material
slot and applies a renderer-wide property block if any shader matches; it
does not change `_MinimapBuildingTex`. Dependencies referenced by these
components retain their original pixels, mips and sampler settings.
Single-component packed normal channels retain their original float32 words,
including NaN payloads, for decoding by the original map vertex shader. They
are not treated as three-component normals or normalized on the CPU.
`_WatchWorldToLocalMatrix` comes from the actual `UIWatchPanelCut` cylinder's
inverse world matrix. Watch's original region placement is retained; a generic
map rotation tween is not additionally applied. The reference view loads all
declared Region01 levels. A matching live-game map additionally requires the
actual current level, player position, loaded/unlocked levels and selection.
The original level-model UIAnimationWrappers are sampled in the joined Domain
scene, using instance-bound clips rather than the PC menu's node graph. Watch
initialization plays the current model's selected clip at timeScale 1 and seeks
other model wrappers to their deselected endpoints. A reference with no current
level explicitly uses the original deselected endpoints for every model; it
does not claim game account state. Selection curves retain each original
wrapper's Linear or OutQuad ease, material colors and exact child targets. Ground materials are
not overwritten. Original hover curves affect model Lightness and two separate
glow-material alphas; explicit clip-time samples are available for verification,
while Watch's live map hover dispatch remains unverified. The saved idle glow
materials already have alpha zero. Region02's nested wheel wrapper has
autoPlay=1, Linear ease and its original 14.966666-second quaternion loop; it
does not rely on the Animation component's disabled PlayAutomatically flag.
The installed Legacy Renderer animation handler writes plain `material.*`
channels to the same renderer-wide property block used by the original
`Renderer.SetPropertyBlock` internal call. These overrides therefore reach all
of that Renderer’s material slots, including Region02's four-slot models;
they do not alter shared Material assets or another Renderer’s ground material.
Controller color writes and animated scalar channels share this property block:
the last actual writer determines an overlapping property. A controller color
is not assumed to permanently override a continuously sampled clip.
Material-index-specific property-block precedence remains outside this path.
Renderer sorting uses native UISortingOrder's absolute renderer offset; it is
not added to the parent panel's Canvas order. Source terrain and spaceship
instances therefore retain their native negative sorting offsets.

Canvas-type UISortingOrder writers receive the same panel base and enable
overrideSorting. BannerNode, MoneyCellRoot and ExploreRoot each use base+12;
canvas_watch's type0 writer affects Renderers and does not change its Canvas.
Rendering, hits and parent-mask boundaries use the same effective Canvas order.
This standalone panel retains the source root's authored 6080 as a relative
ordering reference. The game's live Window-panel base is allocated in steps of
20 within its 7000 sorting interval and depends on other opened/resident panels;
that stack was not captured. Nested offsets are not accumulated. Overlapping
ancestor Renderer writes remain unverified until their lifecycle order is known.

## Validation

On macOS:

```sh
./scripts/test.sh
./scripts/build.sh
./scripts/verify-bundle.sh build/EndfieldHUD.app arm64 x86_64
build/EndfieldHUD.app/Contents/MacOS/EndfieldHUD --ui-test --lifecycle-smoke-test
build/EndfieldHUD.app/Contents/MacOS/EndfieldHUD --ui-test --navigation-smoke-test
bash ./scripts/render-source-watch-previews.sh
bash ./scripts/verify-source-backdrop-gpu.sh
```

The core suite covers source projection/inverse hits, curve sampling, layout,
font glyph geometry, button transitions, gyro retargeting and original domain
assembly. The Metal probe compiles each bundled stage with Apple's compiler and
checks reflected interfaces. Bundle validation checks every WatchSource file
against the source bytes. Lifecycle and navigation checks exercise real source
phase, timer, drawable and hits as well as the retained macOS functions.
The runtime allocates constant bytes using the compiled Metal argument size,
including any MSL struct tail padding while preserving source member offsets.
The GPU fixture records these sizes in `source-constant-buffer-abi.json`.
Live verification prints the specific failed assertion and source hit/frame
state before terminating, so a native check failure remains actionable.

The `watch-source-previews` CI artifact captures the actual Metal drawable at
explicit source times. It includes geometry and diagnostics and refuses a stale
drawable from an earlier submission. The fixture has no access to a desktop
framebuffer or game account. Its manifest deliberately leaves
`recordingPixelComparisonPassed` false until a recording comparison is completed.
PNG previews use an explicit opaque black matte: each drawable RGB byte is
retained and only image alpha becomes opaque. This avoids incorrectly declaring
encoded linear-premultiplied RGB as encoded-space premultiplied CGImage data.
It also retains additive colors that bounded straight-alpha PNG cannot express.
Per-frame raw-pixel reports keep alpha counts, nominal linear RGB excess,
quantization ambiguity and deterministic raw samples. `stable-raw.bgra` retains
the full stable GPU blit with its report's row pitch. This output representation
does not alter the live CAMetalLayer, source blend state or tone mapping and
does not capture the desktop blur or reproduce HG's opaque game background.

Compilation, source correspondence and an inspected preview each establish
different evidence. They must be reported for the exact tested commit.
Successful older desktop-adapter checks do not validate this renderer.

## Remaining fidelity limits

Pixel-identical output has not yet been established against the supplied game
recording. The original HG render globals, engine scheduling, possible IFix
patches, native vertex-buffer quantization and game postprocessing still need
runtime comparison. The live direct LDR port does not claim HG HDR/bloom/tonemapping;
the packed-HDR and original desktop-input FrostedGlass/RawImage GPU path has
passed synthetic native validation, including both original stencil passes,
all three filters and explicit color-profile uploads. The desktop-capture and
permission path still needs hardware validation. The actual game main-scene input and dynamic capture
gates remain unobserved. Runtime account fields and
two serialized multiline counter placeholders cannot stand in for live values.
Unsupported text/layout/material features are diagnosed instead of rendered
with substitute artwork. The disabled original UnlitCylinder mesh is not drawn.

These limits are separate from the confirmed source assets and native method
evidence. Original foreground geometry and animation should be compared first,
without fitting camera parameters to absorb an unresolved layout or shader error.
