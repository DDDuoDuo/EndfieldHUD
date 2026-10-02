# Original camera-relative UI shader pair

The normal offscreen UI draw has `_RenderPathInjected=1` through the original constructor/callback, documented in `hg-ui-render-target.json`. That flag must be paired with both constants below.

`_WorldSpaceCameraPos_Internal` at current-view constant-buffer offset **704** is **Camera.transform.position.xyz, w=0**. The native chain is Component.transform (`0x183537e43`), Transform.position (`0x183537e74`), UpdateViewConstants (`0x183537f26`), source ViewConstants native `+0x5c0` stores (`0x182deb6b1/6c0`), and PrepareBasicCameraTransforms copying HGCamera `+0x638` into the buffer (`0x18399cc0b` through `0x18399cc2b`). The vector is copied unchanged, without Z reflection or projection-space conversion. The Vector3→Vector4 helper explicitly sets W=0.

`_NonJitteredViewNoTransProjMatrix` at buffer offset **512** is:

`preTransform × GL.GetGPUProjectionMatrix(nonJitteredProjection,true) × zeroTranslation(Camera.worldToCameraMatrix)`.

UpdateViewConstants preserves the source view's first three columns exactly and replaces column 3 with `(0,0,0,1)` (`0x182deaf80/84/90`). The accompanying report symbolically checks the compiler's shuffle sequence. It multiplies the original non-jittered GPU projection by that view (`0x182deb060`) and writes source ViewConstants `+0x240` (`0x182deb64d` through `0x182deb67f`). PrepareBasicCameraTransforms reads HGCamera `+0x2b8`, premultiplies the supplied preTransform (`0x18399cb14`) and writes buffer `+0x200` (`0x18399cb26` through `0x18399cb43`).

The real owner callback computes preTransform from whether an attachment is the backbuffer (`0x188ebd96d`), then calls PrepareBasicCameraTransforms (`0x188ebd9e5`). GetPreTransformMatrix(false) returns the engine's identityMatrix (`0x188eef757`→`0x188eef77f`). It uploads the current-view constant buffer before pass rendering. The callback prepares again when camera or backbuffer classification changes and retains that prepared value otherwise.

For a normal transform-derived view V and world camera position C, `zeroTranslation(V)×(world-C,1)` equals `V×(world,1)`. This identity also survives multiplication by the same P/preTransform. Therefore the adapter must change Injected, position and no-translation VP together. Passing an absolute VP after subtracting C applies translation twice. The original camera API view retains its engine Z reflection; its world position does not receive that reflection. The source GPU projection uses renderIntoTexture=true, and translation removal introduces no additional Y flip. Source final clip-Y and Metal projection/RT adaptation remain a separate coordinated convention.

This is a read-only default-code/symbolic audit. It does not execute game code, prove arbitrary custom-view/IFix behavior or establish live recording equivalence. Exact instructions, offsets, numeric algebra residual and evidence SHA-256 values are in the adjacent JSON.
