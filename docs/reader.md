# E-Reader

Reader is a retained, tilted HUD module. It opens PDF, EPUB and TXT books from Finder, a drop, or the Temporary File Shelf. The source file stays where it is; the library keeps a bookmark/reference rather than a second copy. Removing an item from the library never deletes its file.

The toolbar contains Open, Library, Reading settings and bookmarks. Open's source chooser sits immediately below its button. Reading settings (248 pixels wide) and bookmarks (240 pixels) also open directly beneath their respective controls, aligned inward to stay within the module. Library and bookmark panels scroll through a bounded list and use the same square close control as other HUD menus; the former pagination footer is removed.

New readers default to **font size 10, line spacing 2 and page margins 16**. Saved font/layout choices remain unchanged. Reading settings offer two navigation axes: **Left to right** turns pages with horizontal trackpad gestures or the left/right arrow keys, while ignoring vertical wheel motion; **Top to bottom** scrolls pages vertically without a page-turn animation. These are navigation modes, not text-writing directions. The redundant Continuous scroll control and old right-to-left choice are removed. Natural writing direction follows the text. The footer progress rail still seeks through the book.

PDF pages and EPUB image blocks support manga zoom from 1× through 12×. Pinch or Option/Command-scroll zooms around the pointer, dragging pans, and the compact −/1:1/+ controls adjust or reset the zoom. At 1× horizontal mode still ignores an ordinary vertical wheel; while enlarged, vertical wheel input pans within the current page without turning it. Modifier-assisted wheel input remains an explicit zoom gesture. Vertical mode scrolls through enlarged pages into the cached next or previous page without resetting zoom; horizontal page flips preserve both zoom and pan. The zoom controls sit in a centered footer row above the book/progress line, leaving their detached frames clear of the module clip. Text selection, interactive PDF forms and websites are not part of this reader.

## Rendering and lifecycle

- PDF pages use [Apple PDFKit](https://developer.apple.com/documentation/pdfkit/pdfpage); text is laid out with Core Text and comic images use ImageIO thumbnails.
- A single decoder queue owns PDF documents, ZIP handles and chapter content. It publishes at most three base page images: current, previous and next. Normal pages render at 752 × 668 pixels; the renderer has hard dimension bounds. EPUB keeps at most two decoded chapters. There is no polling or idle drawing loop.
- Vertical scrolling moves existing page layers directly with native trackpad momentum; cached page changes preserve the remaining scroll offset and do not crossfade. Horizontal gestures turn at most one page per swipe and use a finite directional transition. Continuous scrolling moves existing page layers. A page boundary requests new content; a later request replaces a pending render rather than appending an unbounded queue. Cached neighbors appear immediately.
- Enlarging an illustration immediately transforms existing pixels. After 120 ms without a new zoom/pan event, the same serial decoder paints one additional **viewport-sized** detail image. Zoom never allocates a 12× page bitmap: the 752 × 668 viewport stays fixed, the three-page neighbor cache stays bounded, and the extra image is released on reset/page change/hide and regenerated for a newly visible zoomed page after the same debounce. At most one detail render runs with one coalesced successor. EPUB image decode is capped at 2,048 pixels for this detail pass.
- Presentation and input are separate: incoming content can prepare during the HUD transition, while its controls remain disabled. Closing cancels pending decoder work and releases source access/decoder caches. The outgoing page pixels remain for at most 0.35 seconds so the existing module fade does not reveal an empty surface.
- Controls and secondary menus use the same retained Endfield frames, projected hit testing, hover feedback and accessibility controls as the rest of the HUD. Scroll, page and menu animations are finite and honor Reduce Motion.

## Local library and writes

`~/Library/Application Support/EndfieldCharge/Reader/library.json` is a separate version-1 reference library. It contains at most 50 entries, each book's reading anchor/progress and up to 128 bookmarks, plus reader preferences. The version-1 `continuous` and `rightToLeft` keys remain decodable: continuous maps to vertical navigation, paged maps to horizontal, and the obsolete writing-direction flag is ignored without overwriting stored customization. Notes, Archive and existing profile data are untouched. File bookmarks use security-scoped access when available; access is released when the decoder closes.

Production library initialization is lazy: opening another HUD module does not read this file. One utility queue owns all controller library I/O. Progress updates coalesce to the latest waiting page for each book while a commit is running; pointer movement never writes. Explicit book, bookmark and preference changes share that writer. A newly displayed cached page records its progress immediately, so closing before neighbor decoding finishes does not discard it. Failed progress is retained per book, so a successful write to another book cannot erase a position waiting to retry. Quit drains pending commits with a finite deadline, using run-loop common/modal delivery so the nested macOS termination loop can receive completion.

Writes validate data, compare the last-read bytes and replace the file atomically. A corrupt or newer archive is preserved. An externally changed file is not overwritten; the UI reports that the app must restart before saving. Diagnostic `--ui-test`, `*smoke-test` and `--render-*` runs use one randomly named temporary directory per process and cannot access the real library.

## EPUB boundary

EPUB's [container, package manifest and spine](https://www.w3.org/TR/epub-33/) are parsed locally. This is a native reading implementation, not an embedded browser: it uses no WebKit, network requests, scripts, CSS execution, hyperlinks or installed book fonts. It renders text and local raster images in reading order. Complex publisher styling is simplified to the selected reading font and layout. DRM/encrypted books, ZIP64 and unsupported compression are rejected.

The ZIP reader extracts nothing to disk. It accepts stored/DEFLATE entries and uses native zlib with exact inflated length and CRC checks. Both central and local headers must agree. Absolute paths, traversal, symlink entries, ambiguous duplicates, encrypted flags and external resource URLs are rejected. Limits include 512 MiB archives/advertised total inflated bytes, 8,192 entries, 32 MiB per entry, 8 MiB per chapter, 128 XML nesting levels and 100,000 XML nodes. XML entity declarations and external entity loading are disabled. TXT files are bounded to 32 MiB; PDF source files to 1 GiB. Image metadata is checked before a maximum-1,200-pixel normal thumbnail (2,048 pixels during bounded zoom detail) is decoded.

## Original artwork

The navigation book is the original `adventurebook_icon` from `origin/codex/endfield-watch-motion`, now bundled as `Resources/AppIconSources/EndfieldWiki/Reader_Icon.png`. Its source/provenance is recorded alongside the other imported game assets. Retained toolbar geometry follows the existing HUD controls.

## Verification

`Tests/ReaderTests.swift` uses only generated temporary ZIP, EPUB, TXT, PNG and PDF fixtures. The previous batch's focused suite contained 61 assertions; the current suite adds navigation/zoom coverage below. It checks native inflate/CRC, unsafe paths and mismatched headers, XML entities, UTF-16 EPUB declarations, Unicode pagination, comic images, native PDF neighbors, reading preferences, reference-only deletion, corruption/concurrent-write protection, diagnostic isolation, asynchronous cached navigation, projected menu dismissal, modal write draining, per-book failed-write retry and decoder teardown/reopen. `BatchSixHUDVerification` exercises the module in the isolated native HUD; no test opens a user's book or edits their library.

All 61 Reader checks passed in the integrated 77,372-assertion core run. The
optimized review app also passed 27 Batch6 native assertions, including bounded
Reader pages, projected settings, committed bookmark events, and clean shutdown.
The rendered reading preview was inspected. Arbitrary third-party books,
publisher-specific layouts and physical input-method/VoiceOver use remain
unverified; the supported-format and EPUB limitations above are intentional.

## October 4 reader corrections

Focused regression checks add default/saved-settings compatibility, horizontal-versus-vertical input routing, one-swipe/one-page behavior, arrow navigation, finite page turns, anchored Open menu, shared close dimensions, scrolling library/bookmark lists, manga pinch/pan/reset, fixed-size detail rasters, and cancellation on hide. All **100 Reader assertions passed** in the updated 77,558-assertion core run on October 4; the preceding batch's integrated totals above remain historical. This pass also verified that the vertical page-boundary scroll remainder survives a cached navigation. The integrated run includes six modifier-chord assertions added after routing review. Modified arrow/Space events pass through to configured summon shortcuts and macOS handlers. The updated native HUD check also verifies an attached horizontal transition, vertical flip removal, defaults, menu anchoring and lifecycle. Physical trackpad feel and arbitrary third-party PDF/EPUB content remain unverified.


## Reader layout and zoom follow-up

The October 4 follow-up moves the −/1:1/+ targets inward without reducing the book viewport. For manga, the progress rail shares the bottom line with the book name and percentage, so it does not overlap the zoom targets. Narrow settings/bookmark menus retain the shared close button and projected hit testing.

Zoomed vertical navigation positions the same three cached page layers as a continuous enlarged strip. The current page's clamped pan and a bounded boundary offset let neighboring pixels enter the viewport before advancing the reading anchor. A page change preserves magnification and horizontal pan; horizontal flips retain the entire image view. Text pages and switching books still reset to their normal unzoomed view. No extra decoder, timer or zoom-sized bitmap was added.

Focused tests now include menu anchoring and target bounds, zoom-frame clipping margins, progress/viewport non-overlap, forward/backward enlarged page traversal, horizontal zoom/pan preservation, enlarged-page vertical panning without accidental horizontal page turns, ignored vertical motion at 1×, and fresh bounded detail images after a page change. Runtime validation for this follow-up is pending the coordinated build; the earlier passing counts above refer to the preceding revision.
