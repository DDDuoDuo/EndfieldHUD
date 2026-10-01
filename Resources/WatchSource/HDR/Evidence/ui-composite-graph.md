# Original Watch background → HDR UI → UI composite

Read-only source/native inspection; no game execution, fitting or live GPU claim.

The normal menu renders its **scene-captured background into the newly created RGB HDR UI texture**, then uses the original `UberPost_CompositeUI` pass. A clear-to-black-only desktop implementation would omit that original background draw.

## Actual order and attachment actions

1. `HGRenderPath3DUI.RenderInternal` creates GraphicsFormat74 `B10G11R11_UFloatPack32`, RGB HDR without alpha (`0x188ed1297`). Its initial conditional camera-background clear does not establish the subsequent draw clear.
2. An optional **UIImageBlur atlas cache** runs (`0x188ed138d`). The manager supplies its own external source/target and atlas rect. The callback draws shader pass0 to its transient texture, then pass1 to its external target. This is separate from WatchBlur scene capture and the new HDR UI buffer.
3. The first UIPP renderer-list pass (`0x188ed164e`) explicitly sets **Clear black, Store** (`0x188e84e27`). Its explicit attachment overload sets `manuallyOverride=true` (`0x188e1d69d`). Clearing happens **before** all draws, so the background RawImage can remain in the resulting HDR buffer.
4. `CopyUIRT` reads that HDR source, writes a new format74 texture at `camera.sceneRTSize`, blits the source, and binds the copy globally as `_UIColorTexture` (`0x188ec17ae`). This is the snapshot used by the later UI distortion path, not the final composite's `_InputTexture`.
5. `RenderUIDistortion` uses **Load/Store** on the HDR source (`0x188ec5997`). Actual active distortion renderers remain unobserved.
6. The bloom preparation and `UIUberPass` follow. UIUber reads HDR source and bloom dependencies, binds HDR as `_InputTexture`, draws the original **shader pass1**, and uses **Load/Store** on the selected intermediate/backbuffer (`0x188ec6323`). Its base variant copies RGB unchanged and clamps alpha; no tone-map or saturation operation occurs in that branch. Original blending is SrcAlpha/OneMinusSrcAlpha. Bloom/vignette variants require the original active-volume and HG setting gates.
7. The later UI layer pass switches mask to32/UI and uses **Load**, retaining prior output. Later multiblur/overlay/final-copy targets depend on the original stage cutoff and runtime conditions.

## Original WatchBlur draw

`PhaseWatch._InitAllPhaseItems` opens or retains WatchBlur. `PanelConfig.WatchBlur.clearedPanel=false`; `UIManager.ClearScreen` hides only `clearedPanel=true`, so normal Watch clearing does not hide this background. TransitionIn and BackToTop recursively set WatchBlur to **UIPP layer10**. The exact engine layer table and `HGUtils` native cctor agree: UI layer5/mask32 and UIPP layer10/mask1024. The manager camera request includes both, mask1056.

BlurBG is **Unity RawImage**, not UIImageBlur: stretch anchors0..1, sizeDelta0, centered pivot, UV rect0..1, vertex color **(.2980392277,.2980392277,.2980392277,1)**. `UIBlurRT.InitRT` assigns its texture; `Register` requests main-camera `BlurredSceneColorPS` with `Temporal` duration. Original explicit material is `M_ui_blur_bg`, full ID `CAB-19672075dc041493137296c53a2330a5:8413750250904750805`, shader `HGRP/UI/Default`, keyword `STENCIL_ALPHA_BLEND`, `_UIImageOpaque=1`, `_UIImageBlurToggle=0`, `_Linear2Gamma=0`. The capture texture supplies its runtime `_MainTex`; null serialized texture is not a white-source instruction.

Source sorting categories place WatchBlur before Watch: `Types.UI3D=1` vs `Window=7`, type interval1000 and stack increments20. `UICtrl.SetSortingOrder` writes `panelCanvas.sortingOrder`. These imply category ranges1000..1980 and7000..7980; exact stack slots remain runtime state. The prefab root's serialized zero rect/scale must be initialized by the original UIManager/Canvas path, not used as a zero-sized background.

Thus the normal background is drawn **after black clear and before UIUber**, where it can survive. An opaque black HDR result composited over a separate desktop blur would hide that adaptation. A faithful migration needs the background quad/material/alpha animation and explicit captured-background input inside the HDR pass. Static game assets cannot provide the actual player's scene pixels; desktop capture/system blur must be described as an adaptation. The extraction's C++ kernel scheduling, live volumes, final sRGB policy and GPU fallback are not inferred here.

## Port evidence and reproduction

`ui-composite-port-manifest.json` contains all **6 original pass1 variants / 12 original stages**, original blend/pass state, source parameter bytes, named texture/sampler descriptors, exact buffer names/offsets and SPIRV-Cross reflection checks. Base program726 uses `_InputTexture` at Metal texture0 and the original named `sampler_LinearClamp` at sampler0. Other variants' actual masks/bloom bindings are retained. No keyword activation is inferred from their availability.

The bounded scripts are `decode_ui_composite_shader.ps1`, `export_ui_composite_shader.py`, `convert_ui_composite_shader.py`, `build_ui_composite_port_manifest.py`, `audit_watch_blur_composite_scene.py` and `report_ui_composite_graph.py`. Run with the already installed workspace Python/dependencies; scripts write only this outer evidence directory. No production files are copied. The source JSON includes individual evidence-file hashes and addresses. Offline MSL conversion is verified, but Apple compilation/pixel equivalence and Metal clip/RT UV orientation remain integration checks.
