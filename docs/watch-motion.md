# Earlier desktop Watch motion adapter

This document records the earlier Core Animation adapter. The current Watch
overview uses the original scene, camera, geometry, fonts, texture mips and
translated shader programs. See [the source renderer](watch-source.md) for the
current behavior, native validation and remaining fidelity limits. The retained
macOS feature panels can still use this older desktop artwork.


Main navigation hover previously moved the card while its brightness, rim and
icon marker changed instantly. Decorative rings and three randomly placed
triangles also made unrelated full turns. This change adapts the installed
game's `WatchPanel_PC` bindings to the retained AppKit HUD.

## Changes

- Main tiles use the `keyboard_btn` Highlighted envelope: gray → white → gray
  → white at 0, 1/30, 2/30 and 3/30 seconds, holding through 1/6 second.
  The color segments use the decoded smoothstep interpolation. The idle card
  uses the source gray/alpha, while the hovered card becomes opaque white.
- Hover exit uses the controller's 0.1-second Normal blend. All appearance and
  depth tracks retarget from presentation values; repeated pointer samples do
  not restart activation. Existing desktop lift distances and selection scale
  are retained; the game binds local Z values, which cannot be substituted
  directly for AppKit XY geometry.
- Six equally spaced source triangle sprites replace the random three-marker
  layout. They share the rings' bounded 15-degree excursion. The dot backplane
  counter-rotates by 25 degrees behind a stationary circular clip.
- Ambient tracks share one start time and restart together when ambient motion
  is enabled. They remain owned by `HUDMotionController`, so hiding, retraction,
  Reduce Motion and low-power visual mode clear their repeating animations.
- The source segmented ring and hollow triangle Sprite crops are bundled.
  `HUDWatchArtwork` caches source pixels and bounded tinted variants. The ring
  stays behind the bearing/readout well; theme tint retains source transparency.

## Evidence and adaptation limits

See [the extraction report](reference/game-ui-reference.md),
[verified serialized motion](reference/verified-motion.json), and
[bundle, curve and sprite checks](reference/correspondence-checks.json), plus
[sprite provenance](../Resources/Watch/SOURCES.json). The baseline source is
upstream commit `a8770680044c4f7b664c6c8adecc0c02bed02a2c`.

The rotation adapter uses linear degree excursions with a 6.833333-second leg,
matching the ring channels' 13.666667-second span. It does not reproduce every
baked quaternion key or the whole clip's extra 1/60-second logo tail. Unity's
Y-up Z angles are negated for the HUD's downward Y drawing coordinates.
Static triangle phases account for the desktop path's initial upward direction.
Their source 3D child tilt and the game's camera projection are not recreated.

The dot lattice is existing desktop artwork. It is **not** the game's mesh
texture: that material uses a segmented strip, animated UV masking and dissolve.
The Unity shader has not been ported to Core Animation. Battery pulses/scanning
retain their desktop semantic role. The opening/closing bindings were identified
and recorded (0.75 and 0.333333 seconds); this focused change leaves the existing
desktop deployment/retraction choreography in place.

## Verification

On macOS, run:

```sh
./scripts/dev.sh
./scripts/test.sh
./scripts/test-release-resources.sh
APP=build/dev/EndfieldHUD.app/Contents/MacOS/EndfieldHUD
"$APP" --ui-test --lifecycle-smoke-test
"$APP" --ui-test --navigation-smoke-test
bash ./scripts/render-watch-previews.sh
```

Core checks cover finite hover activation/exit, rapid reversal, steady held
feedback, live Reduce Motion cancellation, shared ambient clocks, ambient
disable/enable, teardown, cached tint pixels, sprite alpha and bundled resources.
The fixture preview renders the real HUD with temporary stores and a private
pasteboard. It writes only its own layers, not the desktop framebuffer.
The preview script compiles with `HUD_WATCH_MOTION_PREVIEW` so CI can exercise
animation even when its host enables Reduce Motion. This affects only the
isolated preview binary; the shipped app retains its macOS accessibility policy.
The capture manifest records both the host preference and effective preview
policy, and static captures cannot pass as animation evidence.
Captured hover timestamps are measured; rendering overhead means PNG captures
are visual samples rather than exact 30 Hz curve measurements.

The Windows authoring host verified source bundle integrity, sprite bytes,
provenance, curve correspondence and whitespace. AppKit compilation, native
tests and live visual assessment require macOS; their results must be taken from
the pull request's macOS checks and the `watch-motion-previews` artifact.

## 中文说明

这批改动优先改善主菜单按钮悬停与背景：按游戏资源提取的约 0.167 秒高亮
曲线实现灰白两次短促激活，退出使用 0.1 秒混合；背景改为六个等间距
三角与饰环共同缓慢往返，点阵反向摆动，并接入原始刻度环和空心三角素材。
保留主题染色、减少动态效果、低功耗与关闭清理。

这是基于实际游戏资源的原生桌面适配。点阵、三维投影和 Unity 材质
UV／溶解效果仍有差异；开关动画的源绑定已查明，本批未替换既有开关编排。
完整来源和明确的适配边界见上方提取报告。
