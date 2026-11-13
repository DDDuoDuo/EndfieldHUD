#pragma once
// Layout synchronization shared by NativeProjectedEditor and the chunked
// NativeNotesLongEditor. An IME write raises the document revision before the
// posted owner notice lays out the new text, so a frame, wheel, key, format,
// history step or click can arrive while the layout is one revision behind.
// Both editors' Impl expose the same members: onThread(), layoutCurrent(),
// sync() (which leaves the layout alone while TSF holds a lock), alive,
// pose, poseSet, poseReady and document.composition().
namespace endfield::native::projected_editor_sync {
// Lays out pending text first. False while a TSF lock prevents the sync (the
// queued notification lays it out later) or when the sync stopped the editor.
template<class Impl> bool syncLayout(Impl& i) {
    i.onThread();
    if (!i.layoutCurrent()) i.sync();
    return i.alive && i.layoutCurrent();
}
// Hit testing reads the latest text and pose: a stale layout or pose is
// re-posed (which syncs) first. Declines during marked text, before the first
// pose and while a TSF lock prevents the sync.
template<class Editor, class Impl> bool preparePointer(Editor& editor, Impl& i) {
    i.onThread();
    if (i.document.composition() || !i.poseSet) return false;
    if (!i.poseReady || !i.layoutCurrent()) editor.setPose(i.pose);
    return i.alive && i.poseReady && i.layoutCurrent() && !i.document.composition();
}
} // namespace endfield::native::projected_editor_sync
