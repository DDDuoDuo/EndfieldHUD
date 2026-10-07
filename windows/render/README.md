# Native renderer status

`SourceDraw` uploads the original selected BC7/RGBA/R8 mip chains to D3D11
immutable textures. It retains the original wrapping, filtering, mip bias and
color-space metadata. Source IDs remain strings. A single dynamic vertex upload
per frame feeds ordered draws, with perspective-correct UV interpolation and
projected ancestor rectangular masks from the same frame used for hit testing.
Empty source text produces no solid placeholder rectangle. DirectWrite text
surfaces are replaced by component identity and removed when inactive.

The basic image fragment path follows the source program's alpha quantization,
opaque switch, premultiplication and additive alpha. This is still a feasibility
renderer: translated FX/soft-mask/stencil/HDR/backdrop passes and original font
metrics have not reached parity. Mesh material execution remains incomplete.
The two authored anisotropic samplers use D3D's anisotropic filter; their source
nearest-mip behavior needs a separate cross-platform sampling comparison.
The diagnostic label and synthetic editor make this limitation visible.
Linear RGB is encoded before premultiplication into the shared UNORM surface so
D3D, Direct2D and the desktop compositor agree on translucent pixels. A native
synthetic GPU readback verifies half-alpha white against that contract.

`NativeRenderer` owns the composition swapchain, D2D editor surface and source
resources. Diagnostic BMP readback reads only its own synthetic buffer. The
hidden lifecycle measurement drains GPU work before its memory snapshots; this
does not measure visible animation pacing. Closed windows submit no frames.

Packed resources use the native bounded EHUDZ01 decoder in `resources/` and the
hash-pinned zlib 1.3.2 inflater. Only the decoder is linked statically, and its
unchanged license is included in the Windows runtime inventory.
