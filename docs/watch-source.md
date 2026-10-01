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
- Legacy scalar curves preserve their source class ID. The installed player's
  RectTransform registry accepts Z, anchored position, anchors, size and pivot;
  its hash resolver rejects unregistered local X/Y. These serialized curves are
  retained in the resource but ignored during playback, matching the native
  binding. Transform-class vector/packed position and scale tracks still run.
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

Map geometry uses the source instance matrices, submeshes and material slots.
Single-component packed normal channels retain their original float32 words,
including NaN payloads, for decoding by the original map vertex shader. They
are not treated as three-component normals or normalized on the CPU.
`_WatchWorldToLocalMatrix` comes from the actual `UIWatchPanelCut` cylinder's
inverse world matrix. Watch's original region placement is retained; a generic
map rotation tween is not additionally applied. The reference view loads all
declared Region01 levels. A matching live-game map additionally requires the
actual current level, player position, loaded/unlocked levels and selection.
Renderer sorting uses native UISortingOrder's absolute renderer offset; it is
not added to the parent panel's Canvas order. Source terrain and spaceship
instances therefore retain their native negative sorting offsets.

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
