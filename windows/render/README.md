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
continuous effect demand for a future ambient-enabled mode is not implemented.

This remains a feasibility renderer. Complete FX/soft-mask/stencil/HDR/backdrop
passes, original font metrics and matched Mac visual output have not reached
parity. The encoded UNORM desktop adapter is not the source's full linear/HDR
render pipeline; translated arithmetic and synthetic pixels do not establish
complete compositing parity. Unsupported shader variants are still incomplete.
The two authored anisotropic samplers use D3D's anisotropic filter; their source
nearest-mip behavior needs a separate cross-platform sampling comparison.
The diagnostic label and synthetic editor make this limitation visible.
Linear RGB is encoded before premultiplication into the shared UNORM surface so
D3D, Direct2D and the desktop compositor agree on translucent pixels. A native
synthetic GPU readback verifies half-alpha white against that contract.
`source_draw_tests` additionally performs 124 offscreen WARP checks using the
unchanged staged source and isolated fixtures with original material IDs,
metadata and blend states. Analytic pixels cover tint/gamma, raw mesh colors,
red/RGBA masks, dissolve/discard/emissive, additive versus source-over layers,
animated channels, column UV rotation, 1024-second wrapping and bounded textures.
These tests capture only their own targets and do not certify hardware/HDR parity.

`NativeRenderer` owns the composition swapchain, D2D editor surface and source
resources. Diagnostic BMP readback reads only its own synthetic buffer. The
hidden lifecycle measurement drains GPU work before its memory snapshots; this
does not measure visible animation pacing. Closed windows submit no frames.

Packed resources use the native bounded EHUDZ01 decoder in `resources/` and the
hash-pinned zlib 1.3.2 inflater. Only the decoder is linked statically, and its
unchanged license is included in the Windows runtime inventory.
