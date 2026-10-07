# Native renderer status

`SourceDraw` uploads the original selected BC7/RGBA/R8 mip chains to D3D11
immutable textures. It retains the original wrapping, filtering, mip bias and
color-space metadata. Source IDs remain strings. A single dynamic vertex upload
per frame feeds ordered draws, with perspective-correct UV interpolation and
projected ancestor rectangular masks from the same frame used for hit testing.
Empty source text produces no solid placeholder rectangle. DirectWrite text
surfaces are replaced by component identity and removed when inactive.

The basic image fragment path follows the source program's material color,
alpha quantization, opaque switch, premultiplication and additive alpha. Selected
HGRP/UI/Default programs 211/272 and UIMeshVfxEffect programs 12/13/15 are
translated from the original extracted shader arithmetic: main/mask/dissolve
textures, centered column-matrix UV rotation and time wrapping, tint/intensity,
red-channel alpha, directional dissolve/discard, edge emissive and exposure.
Mesh colors retain their raw source channels and authored blend factors. Material
color/gamma flags are read from canonical shader metadata; animated overrides
merge with serialized channels before conversion. The scene clock and sampled
UIGraphicAnimation material fields feed the same presentation frame.
The shell's ambient-off mode supplies shader time zero, matching the source.
Finite geometry/button clocks continue independently and park at their endpoints;
the normal desktop uses the source's ambient-enabled default and keeps the
shader clock running only while presented. `--ambient-off` freezes it explicitly.

This remains a feasibility renderer. Complete FX/soft-mask/stencil/HDR/backdrop
passes, original font metrics and matched Mac visual output have not reached
parity. The source LDR accumulation and final desktop adapter are separate
stages; their synthetic pixels do not establish complete compositing parity
or the original HDR pipeline. Unsupported shader variants are still incomplete.
The two authored anisotropic samplers use D3D's anisotropic filter; their source
nearest-mip behavior needs a separate cross-platform sampling comparison.
The normal desktop contains no diagnostic banner or editor. `--editor-fixture`
explicitly enables the synthetic projected editor for isolated input checks.
`SourceDraw::draw` explicitly selects its output contract. The host uses
`SourceLinearPremultiplied`: original UI fragments premultiply linear RGB,
mesh fragments retain their authored straight-alpha blend factors, and an
`_UNORM_SRGB` attachment transfers RGB after linear accumulation. This follows
`HUDSourceMetalRenderer.swift`'s `bgra8Unorm_srgb` target and the extracted
UI211/272 and Mesh12/13/15 fragment programs. Half-alpha white is approximately
188 RGB / 128 alpha in that source attachment. A separate presentation adapter
converts the accumulated result for the encoded-premultiplied DComp surface;
Direct2D editor overlays draw there afterward. The standalone
`EncodedPremultiplied` contract remains explicit for existing isolated fixtures
(half-alpha white 128 RGB / 128 alpha), and mismatched attachment contracts
are rejected before rendering.
`source_draw_tests` performs offscreen WARP checks using the
unchanged staged source and isolated fixtures with original material IDs,
metadata and blend states. Analytic pixels cover tint/gamma, raw mesh colors,
red/RGBA masks, dissolve/discard/emissive, additive versus source-over layers,
animated channels, column UV rotation, 1024-second wrapping and bounded textures.
Independent sRGB-target expectations additionally cover translucent white/gray,
ordered source-over, additive color with zero alpha, selected mesh blend states
and the source themed profile's finite normal-alpha hover. Profile artwork keeps
the original premultiplied byte-domain chroma operations, crop/row direction,
integer unpremultiplication and bounded per-accent texture cache; matched
CoreGraphics/Direct2D rounded-boundary antialiasing remains unverified.
These tests capture only their own targets and do not certify hardware/HDR parity.
Zero-alpha additive source pixels retain RGB energy, as the original UI fragment
and Mac drawable readback require; the final adapter preserves those channels
without division by zero. Matched Mac/Windows desktop composition over real
backdrops remains unverified, including mixed additive and nonzero-alpha layers.

`NativeRenderer` owns the composition swapchain, D2D editor surface and source
resources. Diagnostic BMP readback reads only its own synthetic buffer. The
hidden lifecycle measurement drains GPU work before its memory snapshots; this
does not measure visible animation pacing. Closed windows submit no frames.

Packed resources use the native bounded EHUDZ01 decoder in `resources/` and the
hash-pinned zlib 1.3.2 inflater. Only the decoder is linked statically, and its
unchanged license is included in the Windows runtime inventory.
