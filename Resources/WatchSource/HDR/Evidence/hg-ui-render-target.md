# Original Watch UI3D render target and clear contract

This is a read-only static audit of installed original code, prefabs and Lua. It does not execute the game or claim a live GPU observation.

## Proven target request

`HGRenderPath3DUI.RenderInternal` (`0x188ed10e4`) calls `CreateColorBuffer` at `0x188ed1297` with GraphicsFormat **74 = B10G11R11_UFloatPack32**, the exact original enum default bytes `8094`. The requested target is packed unsigned-float **RGB HDR without alpha**, not RGBA half-float. Its dimensions come from `HGCamera.finalRTSize`. The inspected call contains no constant 1.25 crop or scale. MSAA is the original camera setting; descriptor defaults are 2D, one slice, filter 0 and wrap 1.

This establishes the requested render-graph descriptor, not a device fallback/actual allocation or live color.

## Clear rule and serialized defaults

`NeedClearColorBuffer` (`0x188ea96b8`) returns true for **Sky=0** and **Color=1**; other modes use `HGUtils.IsRegularPreviewCamera`. `GetColorBufferClearColor` reads `HGCamera.backgroundColorHDR`. With valid additional-camera data and a non-None mode, that getter returns the additional-camera RGBA unchanged. None yields clear zero; only the no-additional-camera fallback reads/converts the ordinary Camera background.

The exact UICamera source prefab has `clearColorMode=0`, `backgroundColorHDR=(0.025,0.07,0.19,0)`, `enableAlpha=0`, automatic flipY=0 and initial renderPath=1. These affect the **initial texture descriptor**, not the complete UI draw's final clear contract. The alpha value does not provide transparency in the requested RGB target.

The subsequent first `UIPassConstructor.PrepareUIPassData` compares its layer mask with `HGUtils.UI_3D_LAYER=1024/UIPP` (`0x188e84dfe`). It sets **loadAction=1/Clear, storeAction=0/Store and clearColor=(0,0,0,0)** at `0x188e84e27`. The explicit overload sets `manuallyOverride=true` at `0x188e1d69d`. `RenderInternal` supplied that UIPP mask at `0x188ed155a`, so the actual first UI draw clears RGB **black before drawing**, overriding the initial descriptor's camera-background clear. A WatchBlur background RawImage on the UIPP layer is part of the later draws inside this pass; this clear does not erase it after drawing. The second ordinary UI pass changes its mask to `HGUtils.UI_2D_LAYER=32/UI` (`0x188ed186e`) and therefore uses **Load**, preserving its target. Do not use the camera's serialized blue HDR background as the final menu background.

## Normal opening does select UI3D

`PhaseWatch._DoPhaseTransitionIn` disables the menu's local volume at line 73, calls `PlayAnimationIn` at line 81, then `_SetCameraCfg` at line 84. `_DoPhaseTransitionBackToTop` calls `_SetCameraCfg` at line 146. `WatchCtrl._SetCameraCfg` (line 948) requests `SetUICameraPostProcess(true)` and the UIPP culling layer. The native manager (`0x184476d10`) passes **bool+1** to the HGCamera renderPath setter: true means **2/UI3D**, false means **1/UI**. `_ClearCameraCfg` reverses that request on close/hide.

The source opening therefore requests the UI3D/postprocess path. The menu-local WatchPanel_Volume is explicitly disabled in the normal transition, so its serialized Bloom/Vignette values are not a justified normal-output preset. Global volumes, frame/quality settings, IFix and live postprocess values remain unobserved.

## Actual per-pass writer

`HGRenderPathBase.OnRenderPassExecution` (`0x188ebd808`) tests whether a pass has issued and whether any valid color attachment is the backbuffer. For offscreen attachments it writes zero FlipX/FlipY/orientation. On a backbuffer it excludes OpenGLES2/OpenGLES3/OpenGLCore, otherwise selecting FlipX=1 for Display.preTransform 1/3 or FlipY=1 for other values, with orientation equal to the display value.

It reads the **second float of customPayload** at `0x188ebdd5a`: equality to zero selects Injected=1; unequal or NaN selects 0. At `0x188ebddac` it writes `(Injected,FlipX,FlipY,orientation)` to buffer offset 0, copies the raw 16-byte payload to offset 16, and binds 32 bytes at `0x188ebde0d` through HGShaderIDs static offset 0x318.

UI3D calls `UIPassConstructor.ConstructPass` at `0x188ed164e` and `0x188ed18cb`. That constructor supplies a null customPayload (`0x188e848a3`) to SetRenderFunc (`0x188e848f6`). The generic forwarding reaches `HGRenderGraphPass<T>.SetupRenderFuncDescHelper` at `0x187751084`: its null branch writes the descriptor's 16-byte fixed buffer with **UnsafeUtility.MemSet(destination,0,16)** (`0x1877510b7` through `0x183387650`). The execute function then passes that buffer to the original owner callback. Therefore the newly created **offscreen UI draw** pass's tuple is **(Injected=1, FlipX=0, FlipY=0, orientation=0)**. This is the actual constructor/callback contract, not inference from a reset call. The later UI pass or composite to a backbuffer still follows the separate attachment/display-preTransform branch. Do not hardcode the tuple for every pass.

The pipeline reset tuple `(0,0,1,0)` and terrain-bake tuple `(1,0,0,0)` are not evidence of Watch per-pass constants.

## Original pipeline configuration and effect boundary

`RenderPipelineDataLoader.PreloadFullRpResources` resolves the exact source literals `Assets/Beyond/InitialAssets/Settings/RenderPipeline/HGRenderPipelineAsset.asset` and `HGRenderPipelineGlobalSettings.asset`. Both are in one original settings bundle, decoded separately without expanding its 213 dependencies. The pipeline asset also requests format 74 and has dynamic resolution disabled, with 100% minimum/maximum. Global desktop TAAU scaleFactor is 1. Its referenced **DefaultSettingsVolumeProfile has an empty components list**. The separate DefaultLookDevProfile is not that default profile; its Bloom/Tonemapping active flags are zero.

The UI3D path calls `UIPostProcessConstructor.ConstructPass` at `0x188ed17aa`. That constructor calls CopyUIRT, RenderUIDistortion, BloomPass and UIUberPass. UIUberPass reads the camera's volume components and tests Bloom.IsActive before applying bloom values; the original vignette parameter helper is also called. This establishes an actual original postprocess construction path, but does not supply live weighted scene-volume values or prove that each effect is active. No faithful .65 bloom or arbitrary tone/saturation preset follows from the source evidence obtained here.

All exact source locations and file SHA-256 values are in the adjacent JSON. GPU projection/shader final-Y/RT UV adaptation remains separate from the original PC condition; renderPath enum 2 is not an Injected value.
