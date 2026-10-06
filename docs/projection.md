# Projection

Projection closes the HUD before presenting a separate transparent window on the
same display. Return dismisses that window, then the HUD opens again. Escape
first dismisses an open submenu; with no submenu it returns to the HUD.
The overlay owner coordinates this handoff and display changes. Projection does
not close when Finder or its file picker takes focus.

The upper toolbar uses the existing retained Notes/personal-card menu colors
with rounded backing, face and controls. Its compact 336×42-point plate has
30-point buttons and stays centered near the top. Its top inset is at least
64 points and also clears the display's camera safe area and visible menu-bar
inset by 24 points; it updates when the owner changes display.
It offers the shared color wheel, brush width, eraser, Clear All, background
toggle, background darkness and blur, image/video import, and Return. The wheel changes
brush width from 1 to 80 points; the cursor circle shows the current width.
Right click switches between drawing and erasing. Menu reveals/dismissals and
background changes use finite transitions, with no motion under Reduce Motion.

With background enabled, a static dotted field and adjustable darkness sit over
the native macOS live material. The blur control blends the material opacity,
as in the HUD: public `NSVisualEffectView` does not expose a blur-radius control.
Disabling the background removes the full-screen visual cover and deactivates
the material after its short fade. The window still receives drawing input;
transparent mode does not forward clicks to the presentation underneath it.
The window allows read-only sharing for ordinary display mirroring.

Drawings and references remain in RAM while switching between Projection and
the HUD, until the app quits. There is no new database, migration, or write to
the Notes store. Strokes reuse `NotesDrawing`, including its 2,000-stroke,
4,096-points-per-stroke and 100,000-total-points limits. A rejected stroke leaves
the previous drawing intact. Committing only on mouse-up avoids persistence or
event-log writes per pointer sample. Erasing retains unchanged stroke layers.

Clear All sits next to Eraser. A small retained confirmation asks before
removing every drawing and media reference. Cancel or Escape keeps the content.
Confirmation also cancels pending imports, closes the file picker, and releases
all media presentations; late import, picker and confirmation callbacks cannot
restore the cleared content. Brush color/width, eraser selection, background
visibility, darkness and blur remain unchanged. Original source files are never
deleted. Clearing produces one event instead of one event per removed object.

Import uses `NotesMediaFactory` and `NotesMediaPresentation` unchanged. The same
Finder filter, GIF validation/decoding, native video support, security bookmarks,
and temporary-shelf picker rules apply. A drop or file selection is validated
as one batch before adding it. Sources are referenced, never overwritten,
moved, or copied. At most 16 media objects are retained in a session. Drag the
media title bar to move it, or the lower-right grip to resize it; movie controls
use the Notes play/pause and seek implementation. The close control removes
only the session reference.

Only visible presentations own decoders, security access and playback items.
Closing invalidates pending imports, cancels the file picker, removes submenu
animations, and releases native AV items, GIF deadlines and progress observers.
The controller releases its complete window and retained canvas after the
finite close animation. There is no closed-window timer or display link.

Projection has no named projector/whiteboard sprite in the inspected source
branch `origin/codex/endfield-watch-motion`; navigation uses the existing screen
glyph. Toolbar/menu controls reuse the HUD's retained plate and color components
rather than importing a second asset family or decoder implementation.

Verification is split between `ProjectionTests` (isolated session geometry,
stroke commits/erase, controls, event coalescing, menu lifecycle and Reduce
Motion) and the native `--projection-smoke-test` (HUD handoff, transparency,
session continuity, hotkey return and shutdown). Tests do not open real Notes,
control playback in another app, or modify user media.

## Review verification

The optimized arm64 review app passed 31 native Projection handoff assertions.
The full core suite passed 77,372 assertions, including real temporary PNG import,
bounded poster decode, movement/resizing, atomic invalid-batch rejection and
late import cancellation. Existing navigation passed 719 native assertions;
editing/music passed 88. Compatibility checks passed 85 mutations and the
original Chinese roadmap suffix remains unchanged.

AirPlay on a physical receiver, mixed-DPI display removal and manual keyboard
input-method/VoiceOver operation have not been verified on hardware. The app's
native Metal renderer passed; the separate offline shader probe needs an installed
Metal compiler. This is a local review build, with development source paths;
release packaging/signing and subsequent roadmap batches are deferred to their
own step. The user's running app and existing stores were not changed.

The toolbar/clear refinement passed 61 focused `ProjectionTests` assertions,
including safe-area calculations, compact targets, Return accessibility text,
confirmation cancellation, player ownership disposal, late import rejection,
stale chooser callbacks and settings retention. The shared media decoder used
only a temporary 8×8 PNG fixture; this check did not show a real window or change
user files. Native HUD handoff, geometry, Return and confirmed clearing passed
in the integrated review app.


## Toolbar centering follow-up

Projection retains the compact 336 × 42 toolbar and its existing button geometry. Each text/symbol face is drawn into a tiny retained bitmap using Core Text's actual glyph bounds, centered on both axes within its button. This removes the excess space below glyphs caused by top-aligned text inside a taller line box, including fallback CJK and symbol glyphs. Color remains a centered square swatch. Rendering happens only when toolbar state changes; there is no added timer or background work.

Regression tests inspect every rendered glyph's alpha bounds to verify equal opposing margins, in addition to existing toolbar placement, hit targets, rounded shape and lifecycle checks. Runtime validation of this follow-up is pending the coordinated build.
