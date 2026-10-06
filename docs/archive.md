# Archive

The document gallery has a separate scrollbar in its right gutter. Wheel input,
thumb dragging and the native accessibility value all update the same fractional
scroll offset; they retain the existing bounded card pool and do not write data.
Category clips leave room for border/hover artwork, including inside the chooser.
The attachment counter is centered between its previous/next arrows.

The supplied Archive video informs the bracketed light document gallery and
focused dark reading column. The HUD shows a clipped gallery with six complete card positions, with
All, Uncategorized and user-created category filters. Gallery, sidebar and category chooser scrolling apply fractional logical pixel
deltas, including native momentum, without page thresholds or per-scroll fades.
Visible card/category layers are retained and clipped hit/accessibility rectangles
share the same geometry; at most eight partial gallery cards/sidebar rows and
seven chooser rows are retained. There is no idle scrolling timer or scroll-driven
SQLite write. The Add control opens a
retained category chooser; it can create a category and document together. The
top-right category control moves the current document using the same menu style.
The adjacent minus removes only the selected custom category after confirmation;
All and Uncategorized are permanent and disable that control. Its documents move
to Uncategorized. Document deletion also requires confirmation. Existing Journal/Q&A documents retain their content
and migrate to editable category membership; fresh archives have no fixed categories.
The original Archive icon is retained; Clipboard now uses the source branch's
Database icon. No video or extra atlas is bundled.

Title, date and body use the shared projected native editor, so they retain
selection, undo, composition and scrolling while moving with the HUD. Menus use
the existing retained personal-card style and finite open/close transitions.
Grey Title/Content placeholders are projected artwork, never saved content.
Focusing either field reveals Notes' square font, size, color and trait controls
at the bottom left, with clearance above the battery bar. Formatting affects the selection or future typing and supports
undo/redo. Explicit text colors survive appearance changes. Native TextKit backing
is suppressed at composition time so no second copy appears at the screen origin.
Gallery/category/document transitions crossfade content above a steady backdrop;
the old document stays visible until a replacement finishes loading.

A document's first image or video supplies a static gallery thumbnail. Only the six most visible
cards can request thumbnails, through one serial utility decoder. A
12-image LRU stores images no larger than 256 pixels; failed loads use the existing
Archive icon and do not create a retry loop. No gallery card owns a video player.
Closing clears the cache and invalidates pending callbacks.
The image/video source menu anchors directly beneath the document’s + button
on the same projected plane. Adding media uses the same Finder, Shelf, file-drop
and validation rules as Notes. Only one attachment preview owns a decoder at a time; video exposes
play/pause and a seek rail. Closing stops playback immediately, retaining only
bounded outgoing artwork for the close transition.

## Storage and lifecycle

Archive uses its own lazily opened `Archive/archive.sqlite`, beside the Notes
folder in Application Support. Diagnostic UI, render and smoke-test runs use an
Archive subfolder inside Notes' unique process-specific temporary root; they
cannot fall back to a shared `/tmp/Archive` or the normal user database.

The gallery reads at most 2,000 summary rows without decoding document bodies.
A summary contains its category ID and first attachment descriptor, so the UI can
request a bounded static thumbnail for visible cards. Serialized descriptors in
the gallery have a 16 MiB aggregate budget; the controller also bounds retained
descriptors using a conservative byte estimate. An omitted descriptor remains
available through a lazy metadata-only lookup. Same-document requests coalesce;
closing or changing presentation cancels callbacks, and an old completion cannot
replace a newly opened request. The store never decodes images or videos.

Opening a document decodes only its selected payload. A document stores its
legacy template, UUID, category ID, title, date, body, media references and
modification time. Optional title/body rich text uses Notes' closed UTF-16 range
format, with font, size, color, bold, italic, underline and strikethrough. Base
typing styles persist even when a field is empty; absent styles default to
17-point title and 12-point body. Plain strings remain canonical. Invalid ranges
or styles reject the entire save without replacing the prior record. Titles are
capped at 200 characters, body text at 2 MiB, and attachments at 16. Media
descriptors reuse Notes' bounded validation. Saving or deleting an Archive entry
never opens, copies, changes or deletes the referenced original file.

Schema 2 stores up to 100 custom categories. Names are trimmed on creation,
bounded to 80 characters/1,024 UTF-8 bytes, and compared case-insensitively to
avoid duplicate categories. A fresh archive has no predefined categories; a nil
category ID means uncategorized. Category creation returns an optimistic ID,
then queues the category before documents referring to it on the same writer.
SQLite rejects dangling category references. Category failures and dependent
document drafts stay available for explicit retry and shutdown draining. Success
events fire after commit, without logging user-provided category names. Moving a
document updates its category while preserving its ID, template and content.

Deleting a category is another transaction in the same serial writer. It validates
and rewrites affected persisted documents one at a time, preserving their IDs,
text, mixed formatting, media references, timestamps and selection. Latest dirty
drafts join that transaction, and the category row is removed last. Failure rolls
all of those changes back. The controller retains the confirmed intent and dirty
snapshots for explicit retry or shutdown draining; reopening continues to show
the pending Uncategorized assignment with the error. Newer edits arriving during
the operation are written afterwards, and a subsequent document deletion cannot
be undone by the category operation. The single `categoryDeleted` event is emitted
only after success, with no document content or category names. This correction
uses schema 2 unchanged and never automatically removes existing categories.

The transactional schema-1 migration validates every original record before
changing tables. It maps only templates actually used by existing documents to
deterministic Journal/Q&A category IDs, adds first-attachment metadata and keeps
original document payload bytes and selection intact. Validation uses one
bounded decoded payload at a time. Any validation or backfill failure rolls back
both schema and metadata changes. The first later edit writes the effective
category and optional formatting using the new payload fields.

One serial utility queue owns the production SQLite connection. Edits coalesce
behind a finite 350 ms delay. The controller retains each latest dirty revision
until SQLite acknowledges that exact revision. Switching directly between documents keeps the outgoing document until the replacement loads; a failed load preserves readable content and reports the error. Explicit Back clears selection immediately, and generation checks prevent old requests from undoing newer navigation. A save already in flight cannot
clear a newer edit; closing flushes the final revision. Failed commits retain
the draft and report an error after hiding or reopening. Explicit retry is
available through `retryPendingSaves()`. Failures do not start a polling or retry
loop. New documents are blocked while persistence errors remain, preventing an
unbounded queue of unsaved new drafts.

Delete removes the current editor and gallery item immediately and rejects
callbacks for that identity while deletion is in progress. FIFO queue ordering
places deletion after any earlier save. SQLite deletes the document and clears
its matching saved selection in one transaction. On failure, the gallery item
and any unsaved snapshot are restored with an error. A document's delayed save
cannot silently recreate a successfully deleted entry.

`drainPendingWrites(timeout:completion:)` is a finite shutdown barrier. Its
Boolean is true only after queued commits have acknowledged success with no
unsaved draft or persistence error. Timeout returns false once and does not
cancel an in-flight SQLite commit. Production acknowledgments and the deadline use common/modal run-loop modes,
so termination requested from a main dispatch callback cannot deadlock its own
queue. The main run loop must remain active while waiting. Queued operations retain the controller until acknowledgment, even if
the HUD releases its reference first.

Initialization rejects newer schemas, foreign version-zero databases and
missing required version-one or version-two tables/columns without repairing or replacing the original.
Selected payloads must agree with their summary metadata. Corrupt payloads,
invalid selections and invalid summary fields produce a visible error. Capacity
checking and insertion share one transaction, including across separate store
instances; editing an existing entry remains possible at the 2,000-entry limit.

`ArchiveStoreTests.run()` passes isolated checks against temporary real SQLite
databases and production media-reference validation. The category/formatting
increment adds migration byte preservation and post-DDL rollback, exact category
limits, rich-text/typing-style validation, ordered category/document retry,
metadata thumbnail budgets and cancellation, plus document-switch retention.
The existing reopen, selection, capacity, corruption/future-schema preservation,
reference safety, coalesced writes, failed commits, stale-delete protection,
ownership after HUD release, retry and nested termination checks remain. No user
store or live media is used. Canvas and native integration checks are separate.

The previous Batch6 review passed 41 Archive canvas assertions within the 77,372-core
suite, plus 27 native Batch6 assertions shared with Reader. These cover projected
editors, menu layering, save failure/retry, confirmation, close/reopen persistence,
bounded outgoing artwork and zero hidden animations. Gallery/editor previews
were inspected on the native HUD plane. Real user documents and media were not
opened; physical input-method and VoiceOver behavior still need manual checks.

The category/formatting revision separately passes 125 storage and 84 canvas
assertions against temporary fixtures. This includes category creation and moves
through the actual retained menus, selection formatting/undo/redo and persistence,
first-focus control visibility, projected hit-testing, menu click-through
prevention, placeholder artwork, stable transition background, real image
thumbnail loading, and the bounded single-worker queue. Final layout checks also keep body, media,
seek rail, formatting squares and their popovers separate, above the battery
area. Integrated validation
results are recorded in the implementation roadmap.

The category-removal/scrolling correction passes 138 isolated storage assertions
and 106 offscreen canvas assertions. It covers atomic rollback after category deletion failure, multiple
pending documents, edits during deletion, explicit retry/drain, deletion ordering,
permanent default filters, confirm/cancel, fractional retained scrolling, clipped
input geometry, bounded visible rows and the anchored source menu. Physical
trackpad feel and native HUD integration are separate checks; no live user data
or application was used.
