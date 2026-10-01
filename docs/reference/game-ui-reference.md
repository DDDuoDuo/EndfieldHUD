# Endfield PC watch-menu resource reference

Extracted read-only on 2026-09-30 from the user-specified `D:\Arknights Endfield\Endfield_Data`. The source manifest identifies itself as `2954fa80-23c1-1579-2b22-4ecfd6d70418`; this is recorded as an identity, not a release number. The adjacent `verified-motion.json` contains selected exact original key times, values, slopes, controller bindings, and source bundle SHA256/MD5/offsets. No source game pixels, Lua, bundles, or shader code are included in that JSON.

The UI was identified by the actual `WatchPanel_PC.prefab` hierarchy and its `UIAnimationWrapper`, rather than by matching similarly named animations. Its bindings are `watch_in01`, `watch_loop`, and `watch_out`. The open clip ends at **0.75 s**, close at **0.3333333433 s**. A different `watch_in` animation exists but is not this PC prefab's open binding. The wrapper also stores an ease enum value of 6; its runtime enum meaning was not established.

## Hover

The prefab connects **22 main navigation tiles** to `keyboard_btn.controller`, including character, activity, gacha, purchase, mission, map, inventory, and other grid entries. Small currency and close controls bind different controllers; their timings should not be substituted for main tiles.

`Highlighted` lasts **0.1666666716 s**, at sample rate 30. The actual color keys form two white flashes: normal gray at 0, white at 1/30, gray at 2/30, white at 3/30, then hold. The first three color segments are cubic smoothstep. Panel RGB is 0.9056603909 → 1 and alpha is 0.8627451062 → 1; icon and text become slightly darker. The raw streamed cubic coefficients are retained in the JSON. This clip has no scale channel or repeating hover pulse/sweep.

Depth keys move the animator root Z from 0 to −5; `Btn/Text`, `Btn/Icon`, and `Btn/Icon01` from 0 to −10. These are actual local Z channels, not measured desktop XY offsets. Their binding paths were verified by CRC32 against prefab paths. `Normal`, `Pressed`, and `Disabled` clips are constant states of 1/60 s. The controller's transitions to `Normal` and `Pressed` have a fixed **0.1000000015 s blend**; entry to `Highlighted` and `Disabled` has zero blend. Clip duration alone does not establish hover-exit duration.

## Background

The loop has five matching ornament rotation channels: `BlueRingNode`, `OutRingNode`, `TriagleNode`, `YellowLineNode`, and `DecoOut`. Each has 821 baked quaternion keys, moving approximately **0 → +15° → 0** over 0 → 6.833333 → 13.666667 s in Unity's local XY plane. `MeshNode` counter-rotates **0 → −25° → 0**. The exact sampled peak is at 6.8333678246 s, and raw quaternion slopes are supplied. Linear degree interpolation is a small adapter approximation to these baked keys, not a verbatim animation curve. Clip wrap mode is **Loop**; the return motion is authored inside the curve, rather than supplied by a PingPong wrap mode. The whole clip extends to 13.6833333969 s because the 3D logo has an additional final key.

There are **six** triangle sprite children, at a common radius of about 513 local units and radial phases 35.5° + 60°n. Their exact positions, scales, rotations, and sizes are in the JSON. The extracted sprite is a hollow yellow downward triangle. No independent random phases or continuous multi-revolution triangle spin were found in this binding.

Planar conversion to CALayer's downward Y axis negates local Z angles: ornaments become 0 → −15° → 0, mesh 0 → +25° → 0. The parent chain has no negative X/Y scale, but the `Content` parent has a 3D tilt; live camera projection is unverified. The loop's 0 → 90° → 0 `EndfieldLogo` channel targets the game's 3D logo and should not be applied to a desktop text wordmark.

`MeshNode/RingFoMesh` uses `M_fx_ui_watch_ring_plane`, shader `HGRP/UI/UIMeshVfxEffect`. Its actual main texture, `ui_main_menu_loop_bg01` (948×140), is a horizontal strip of segmented gray bands, diagonal hatch marks, small squares/triangles, and dashed lines. It is not a dot grid. Main texture tiling is (3,1); mask tiling (3.28,0); main UV speed (0,0), mask UV speed (0.05,0), and dissolve UV speed (−10,0). Dissolve and mask features are enabled. These stored properties establish a material-driven effect but do not by themselves establish its complete rendered appearance. A vector dot texture in a desktop adapter must be described as an approximation.

The bound `Equipring` mesh is a **cylindrical side-wall ribbon**, with 968 vertices and 5,220 triangle indices. XY radius is approximately 0.67526–0.67798; local Z spans −0.1323003769 to +0.1323003769. UV0 U winds around the circumference once and V spans the ribbon's Z width. This confirms three texture repetitions around a circle, but a flat desktop annulus substitutes a projection for the original tilted 3D cylinder. Geometry alone does not recreate the material's dissolve/mask compositing.

## Validation and limits

All **245 selected installed bundles** passed original metadata MD5 checks; SHA256 values are recorded. The bundle decoder successfully decoded 245/245. UnityPy parsed **7,799 selected objects**, including **217 AnimationClips**, with zero object-read errors. Binary texture data in diagnostic typetrees is summarized by length/hash; pixels were exported only into the local reference directory. Persistent entries override base entries only when their source chunks are installed.

The workflow reads metadata and only the selected UI bundles plus their declared dependencies. It never opens source files for writing. No game process was launched or attached, and no macOS rendering was executed on this Windows host. Shader rendering, camera projection, and runtime UI state transitions remain unverified.

Parser references: [endfieldcontentgen VFS and manifest sources](https://github.com/choopk/endfieldcontentgen), [Endfield-Studio bundle decoder](https://github.com/microruri/Endfield-Studio/blob/main/src/Endfield.Cli/Extract/EndfieldVfsDecoder.cs), and [Unity WrapMode documentation](https://docs.unity3d.com/2021.3/Documentation/ScriptReference/WrapMode.html). Tool sources and dependencies remain local; they are not needed by EndfieldHUD at runtime.
