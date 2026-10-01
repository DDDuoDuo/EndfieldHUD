# Original FrostedGlass / WatchBlur capture contract

The ordinary scene pipeline feeds `BlurredSceneColorPS` from `FrostedGlassPass`, not an unfiltered camera copy. The exact source prepares three sizes `ceil(taauWidth/4, /8, /16)` and corresponding heights from the original dimensions. PS performs horizontal then vertical at each level and returns the third level; its current-level texel size, original weights, threshold 2.5 and all named bindings are preserved in the JSON and unmodified modules.

The ordinary PS scene first horizontal pass enables `APPLY_LUT`: dynamic exposure, log LUT and optional user LUT precede filtering. Compute mode is a distinct 9×9 binomial/half-cache program. The compute/permanent/enabled settings have true constructor defaults, but live overrides are unknown. SceneView is enum **2**; Reflection is **16**. Skipped updates can return cached RTs, gray, or black according to the documented conditions.

Extraction calls original Blit **pass index 1** (`useNativeBlit=false`). This corrects an earlier shorthand describing its integer argument as a bilinear boolean. Its source pass separately names `sampler_LinearClamp`; the packed flags are retained without inventing an engine bit-field decoder.

The ordinary UIBlurRT capture allocation is `R8G8B8A8_SRGB` (format4), Point/Repeat, single mip, Screen dimensions. A platform branch uses remapped rendering scale, optional half scale and integer truncation; the two platform helpers remain unresolved. Upstream FrostedGlass and main UI3D targets are format74 RGB HDR without alpha. These targets must not be conflated.

The desktop helper explicitly selects original no-LUT PS3/PS8 for display pixels and uses the original Blit program. It does not claim to reproduce live scene grading or compute mode. A named ScaleBias Y adapter retains top-left Metal row order without editing the original Vulkan-derived shaders. Full capture outputSize and sampler mapping are platform contracts, subject to native asymmetric pixel readback.

M_ui_blur_bg enables Default and Default-Stencil-Alpha-Blend. Their serialized states are Equal0/Keep and Equal32/Keep. Program194 does not prove a stencil32 write; drawing only program1615 on clear-stencil0 would reject all fragments. The outer runtime material keeps both source passes. HGDrawUIRendererList's engine-side override remains outside this closed subset.

Every cited file hash is in `frosted-glass-contract.json`. The audit only reads installed file bytes, never runs game code or observes player state.
