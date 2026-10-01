# Original Watch HDR / UIComposite package

This bounded package is generated outside the app by `game-reference/package_source_hdr_assets.py`.

- `Composite/manifest.json`: six original pass-1 variants, 12 MSL/SPIR-V stages with reflection, named descriptor/constant-buffer bindings and six original parameter records. Program 726 has no optional bloom/vignette keyword.
- `WatchBlur/scene.json`: all 23 original prefab objects, including exact transforms, RawImage/Canvas/CanvasGroup/UIBlurRT/UIAnimationWrapper metadata and both original animation clips. Preserve raw class IDs and key data.
- `WatchBlur/material-contract.json`: exact `M_ui_blur_bg` serialized state and original HGRP/UI/Default pass 1. The included program 1615 stages are byte-identical to the existing WatchSource stencil stages, so the renderer can reuse them.
- `Evidence/`: original target, graph, camera-relative and source bundle evidence. Every packaged file is hashed in `package-manifest.json`; source paths and source hashes are retained.

The source creates a B10G11R11 unsigned-float RGB HDR target (no alpha). Its first UI3D draw attachment explicitly clears black; WatchBlur's dynamic main-scene RawImage draws into that target before Watch. UIUber pass 1 then samples `_InputTexture` and loads the final attachment. Its base fragment retains RGB and clamps sampled alpha; optional volumes require the original runtime gates.

BlurBG is a RawImage tinted RGB 0.2980392277240753 and alpha 1. Its texture comes from `BlurredSceneColorPS`, not the UIImageBlur atlas chain or CopyUIRT's `_UIColorTexture` snapshot. Native extraction itself only copies/resamples its supplied input; upstream `UberPostPassUtils.FrostedGlassPass` produces that type-1 input. The upstream filter/size contract is being audited separately, so do not infer a blur kernel or desktop filter from this package.

The MSL is an offline conversion of original SPIR-V. Apple Metal compilation, final target orientation/sRGB policy, dynamic scene pixels and live volume state are separate validation steps. This package does not establish engine-equivalent rendering.
