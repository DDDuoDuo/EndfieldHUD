# Original Watch HDR / UIComposite package

This bounded package is generated outside the app by `game-reference/package_source_hdr_assets.py`.

- `Composite/manifest.json`: six original pass-1 variants, 12 MSL/SPIR-V stages with reflection, named descriptor/constant-buffer bindings and six original parameter records. Program 726 has no optional bloom/vignette keyword.
- `WatchBlur/scene.json`: all 23 original prefab objects, including exact transforms, RawImage/Canvas/CanvasGroup/UIBlurRT/UIAnimationWrapper metadata and both original animation clips. Preserve raw class IDs and key data.
- `WatchBlur/material-runtime.json`: one normalized original material record, including both enabled source passes, original property color flags and null texture/ST bindings. Default is Equal0/Keep and the second pass Equal32/Keep. Their four included shader stages are byte-identical to existing WatchSource programs194/1615. No stencil32 writer is inferred from the material.
- `FrostedGlass/manifest.json`: original PS10, CS4 and capture-copy2 stages, with named ABI/reflection/packed sampler records. Desktop PS mode explicitly omits unavailable scene LUT; compute mode uses a distinct original kernel.
- `Evidence/`: original target, graph, camera-relative and source bundle evidence. Every packaged file is hashed in `package-manifest.json`; source paths and source hashes are retained.

The source creates a B10G11R11 unsigned-float RGB HDR target (no alpha). Its first UI3D draw attachment explicitly clears black; WatchBlur's dynamic main-scene RawImage draws into that target before Watch. UIUber pass 1 then samples `_InputTexture` and loads the final attachment. Its base fragment retains RGB and clamps sampled alpha; optional volumes require the original runtime gates.

BlurBG is a RawImage tinted RGB 0.2980392277240753 and alpha 1. Its texture comes from `BlurredSceneColorPS`, not the UIImageBlur atlas chain or CopyUIRT's `_UIColorTexture` snapshot. Native extraction copies/resamples its supplied input with original Blit pass1; upstream `FrostedGlassPass` produces three quarter/eighth/sixteenth levels, returning the smallest. Normal scene PS first-stage APPLY_LUT and dynamic gates are documented in Evidence/frosted-glass-contract.json. Ordinary capture allocation is single-mip sRGB RGBA8; optional scaled-device initialization is a documented boundary.

The MSL is an offline conversion of original SPIR-V. Apple Metal compilation, final target orientation/sRGB policy, dynamic scene pixels and live volume state are separate validation steps. This package does not establish engine-equivalent rendering.
