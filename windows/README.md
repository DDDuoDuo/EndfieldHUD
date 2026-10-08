# Windows rebuild

The previous Windows preview was rejected and removed from this build. This is
a fresh port of the **macOS v1.2.0 build 18 application**, pinned in
[`source-authority.json`](source-authority.json). Current macOS `Sources/` and
`Resources/` define the UI and behavior. The raw game scene alone is not the app.

The current target builds portable scene, motion and data components, a native
D3D11/DirectComposition renderer, retained source material and label adapters,
an event-driven window host, and Windows battery, clipboard and master-volume
service foundations. It does **not** yet produce a complete runnable HUD or a
release candidate.

The fresh graphics path now also runs the original Mac material programs. A
build-only exporter captures the current desktop shell's actual geometry,
textures, uniforms, animation library and native model layers in an isolated
fixture. Original Swift sources and assets stay unchanged. Shader translation
uses a pinned SPIRV-Cross revision, followed by Windows shader compilation and
reflection checks; no shader math is replaced with a generic approximation.

```sh
python windows/tools/check_source_authority.py
cmake -S windows -B build/windows-core -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows-core --config Release
ctest --test-dir build/windows-core -C Release --output-on-failure
```

Tests use synthetic fixtures and temporary directories. They do not read the
installed Mac app's data, clipboard, account session or windows. Windows uses its
system SQLite library; the portable tests use system SQLite on macOS/Linux.

Graphics tests render synthetic fixtures into owned offscreen textures. They
check linear-light blending, transparent edges, clipping, retained resources and
device recreation. An explicit `--composition --hardware` run of
`native_renderer_tests` also checks a hidden owned window in a logged-in Windows
desktop session. This path passed on the test laptop; it never captures another
window. Passing it does not establish game-material, full-HUD or performance
parity. Service tests exercise pure models and do not read or modify the real
clipboard or audio devices.

The first 1280×800 original-material replay contains 133 source batches and 139
passes. Against the Mac renderer's raw target, the Windows WARP result has a
mean channel error of 0.00721 out of 255; 99.98994% of pixels differ by at most
two levels in every channel. Hardware replay also runs on the test laptop.
This comparison excludes native labels, module content and the desktop backdrop;
it is not a full-interface or performance acceptance result. The retained mesh,
texture and constant payload for this frame is 6,032,252 bytes, excluding driver
allocations, frame targets, CPU models and other modules.

The portable source animation evaluator has been compared with 27,641 samples
from the original Swift curves and ten complete opening/ambient/closing poses.
Camera tests compare the actual Mac projection at two viewport sizes. The source
layout writers, finite button controllers, image geometry and Canvas clipping
are now ported. On the Windows laptop, 36 generated frames pass 1,814,023 checks
against the original Mac builder, including opening, closing, scrolling, hover
and compound tilt. The expanded portable comparison also covers 44 frames and
retained ambient motion, with 2,414,192 checks against the original builder. A
separate image-geometry comparison verifies original Float
positions, UVs and indices across 1,864 cases.

Native caption/icon content is rasterized locally and retained while its source
projection and masks move. The live label bridge passes 538 Windows checks,
including initial placement, content reload, clipping and repeated pointer
presentation without new C++ allocations or GPU resources. Local font
substitution remains explicit; this is not
a claim of identical Mac and Windows font rasterization. Live generated-frame
GPU comparisons and complete module integration are still in progress. A
sequence test exposed stale camera constants when previously hidden materials
returned after resizing. The fix has a portable regression; its full Windows
pixel replay is pending.

Selectable hover/press colors now match 22,030 original Swift events, including
shared targets, interrupted fades and the grouped personal-card highlight. This
portable check is bit-exact; MSVC comparison remains pending. Ambient updates
visit the 29 affected descendants instead of resolving all 834 source nodes.

The window host coalesces redraw requests and owns one cancellable wake timer.
Hidden windows schedule no rendering work. Its hidden-window tests and the
other 23 Windows test suites pass on the laptop. Visible input, cursor, IME,
multi-monitor behavior and full-app performance still require acceptance tests.

A compiled original-material catalog avoids runtime JSON and shader compilation.
Its schema preserves exact material templates, reflected field plans, textures
and geometry across multiple original frames; older caches remain readable.
The single-frame cache prepared in 12.5–18.9 ms on the laptop, compared with
roughly 1.6 seconds for the development JSON path, and produced identical Windows
readback bytes. These timings measure artwork preparation, not total startup.

Reference tools (macOS, temporary fixture data only):

```sh
bash windows/tools/export_shell_packet.sh build/windows-shell-packet
bash windows/tools/module_reference.sh build/windows-module-reference
```

`translate_source_shaders.py`, `compile_source_shaders` and
`replay_source_packet`, `compile_source_scene` and `replay_watch_frames` form the
build/verification path. Their development
exports, raw readbacks, compiler caches and documentation GIFs are not runtime
assets and must not be bundled with the application.

Completion requires the actual desktop shell, every module and Windows service,
same data contracts, projected editing, current animation behavior, and live
visual/input/performance checks on the test laptop. Passing core tests establishes
none of those broader acceptance results. See [the migration requirements](../WINDOWS-MIGRATION.md).
