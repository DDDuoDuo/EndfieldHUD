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
  exit. These keys are not stretched to the main menu's duration. The desktop
  still supplies system blur; the game's captured scene input and capture-pass
  schedule have not been reproduced.
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
does not disable its mask: the original map mask has alpha zero. The 132 bundled
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
silently becoming a white rectangle. The original disabled banner prototypes
remain without an invented runtime banner.

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
active-effect preset. The current direct LDR macOS drawable does not yet
reproduce that target or postprocess chain.

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
runtime comparison. The direct LDR port does not claim HG HDR/bloom/tonemapping.
The desktop backdrop currently uses the system blur. Runtime account fields and
two serialized multiline counter placeholders cannot stand in for live values.
Unsupported text/layout/material features are diagnosed instead of rendered
with substitute artwork. The disabled original UnlitCylinder mesh is not drawn.

These limits are separate from the confirmed source assets and native method
evidence. Original foreground geometry and animation should be compared first,
without fitting camera parameters to absorb an unresolved layout or shader error.
