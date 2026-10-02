# Original PlayerSettings cursor

`player-default-icon_mouse.png` is the original 58 × 58 RGBA Texture2D
`icon_mouse`, reached by the installed game's PlayerSettings `defaultCursor`
pointer. `player-default.json` preserves that pointer, the original `(0, 0)`
hotspot, source hashes, texture fields, and the limits of the extraction.

The macOS adapter uses the original bitmap and hotspot. AppKit image sizes
are points, so it supplies `58 / window.backingScaleFactor` points per axis
and scales the hotspot by the same factor. This preserves the source's 58
backing-pixel extent at the normal system pointer size, including a Retina
window. It recreates the cursor when the window's backing scale changes.
The view restores the preceding cursor on pointer exit, input disablement,
concealment, loss of the key window, and detachment.

This is an explicit desktop adapter policy. The original game's runtime
CursorManager overrides, Windows DPI scaling, and macOS accessibility
pointer enlargement have not been measured; the extracted default is not
proof of their behavior. The cursor is an OS cursor and is absent from GPU
drawable readback PNGs.

Apple documents the user-coordinate size of `NSImage` in
[Image Size and Resolution](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/CocoaDrawingGuide/Images/Images.html),
and the cursor initializer in
[NSCursor.init(image:hotSpot:)](https://developer.apple.com/documentation/appkit/nscursor/init(image:hotspot:)).
