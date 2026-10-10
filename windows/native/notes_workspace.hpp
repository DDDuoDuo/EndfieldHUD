#pragma once
#include "native/notes_scene.hpp"
#include "native/notes_text_measure.hpp"
#include "native/projected_editor.hpp"
#include "native/notes_long_editor.hpp"
#include "native/notes_clipboard.hpp"
#include "native/notes_text_navigation.hpp"
#include "native/notes_image_playback.hpp"
#include "native/notes_video_playback.hpp"
#include "native/media_request_broker.hpp"
#include "native/notes_drawing_scene.hpp"
#include "modules/notes_motion.hpp"
#include "modules/notes_checklist.hpp"
#include <stdexcept>
#ifdef _WIN32
namespace endfield::native {
// The readable stored record is valid; only this bounded native editor cannot
// open it. Owners may report this specific recoverable condition without
// swallowing unrelated raster/TSF/state failures or truncating the document.
class NativeNotesEditorCapacityError final:public std::length_error {
public:NativeNotesEditorCapacityError():std::length_error("Notes text exceeds the current editor capacity; stored text is unchanged"){}
};
struct NativeNotesWorkspaceStyle {
    modules::NotesPalette palette;modules::NotesStrings strings;
    NativeNotesExternalEditorAppearance editor;
    std::array<double,4> selectionColor{.2,.4,.7,.5},compositionColor{1,1,1,1};
    // Detached original AppKit DarkAqua systemRed; light owners supply59/255,48/255.
    std::array<double,4> eraserColor{1,69./255.,58./255.,1};
    // Source media footer captions and NotesMediaError descriptions (source
    // order, modules::notesMediaErrorText), caller-localized. A language event
    // replaces them through setStyle; retained media keep their decode state.
    modules::NotesMediaStrings media;
    std::array<std::string,6> mediaErrors{"The saved media reference is invalid.","The original media file is unavailable. Choose it again to restore access.",
        "This file is not a supported image or playable video.","Choose an image below 128 MB and 64 megapixels.","Choose a GIF with no more than 2,000 frames.","This media could not be decoded."};
};
struct NativeNotesWorkspaceOptions {
    LayerRasterOptions raster;
    // Explicit current adapter capacities, not limits on persisted Notes data.
    // maximumEditorUnits bounds the single-layout field (checklist rows and short
    // notes). Text notes longer than chunkedEditorUnits open in the paragraph-
    // chunked long editor, bounded by maximumDocumentUnits (the text service's
    // 4 Mi-unit ACP safety maximum, within the store's 16 MiB payload) and one
    // painted paragraph; their typing cost follows the edited chunk.
    // Cards with retained artwork. When more notes are shown (the source draws
    // every note), the top cards by source layer order keep artwork and the
    // ones beneath are culled until room returns; the open field's note is
    // always kept. A paint or theme change may briefly hold replacements too.
    std::size_t maximumRetainedCards{128};std::uint32_t maximumEditorUnits{65536};
    std::uint32_t chunkedEditorUnits{4096},maximumDocumentUnits{core::text::Buffer::safetyMaximumUnits};NativeNotesLongEditorOptions longEditor;
    UINT ownerMessage{WM_APP+181};
    ITfThreadMgr* activatedTextManager{};TfClientId textClient{TF_CLIENTID_NULL}; // borrowed; never activated here
    NativeNotesImagePlayback* imagePlayback{}; // borrowed exclusive Notes owner; outlives workspace
    // Pure managed-name mapping only; file access belongs to the decoder's
    // independently owned worker resolver. No filesystem work on this thread.
    std::function<std::optional<std::string>(std::string_view)> legacyImagePath;
    // Source NotesCanvas.onAction names (createdText, createdTODO, createdMedia,
    // createdDrawing, editedText, formattedText, editedTODO, drawingEdited,
    // deletedNote, mediaPlayback), raised after each accepted mutation. The
    // owner records them as Event Log noteAction; content is never passed.
    std::function<void(std::string_view action)> noteAction;
    NativeNotesVideoPlayback* videoPlayback{}; // exclusive owner, same renderer/device
    // Optional APP-owned broker/client. Supply both and leave exclusive owners
    // null. Root alone accepts/samples/providers deadlines; workspace only reads
    // this client's records. Broker and attached client outlive the workspace,
    // including resource retirement. Client attachment/detachment is caller-owned.
    NativeMediaRequestBroker* mediaBroker{};
    NativeMediaRequestBroker::Client mediaClient{};
};
struct NativeNotesWorkspacePose {
    core::Matrix4 workspaceToScreen,screenToClip;
    unsigned pixelWidth{},pixelHeight{};float opacity{1};
    double time{};bool ownerFocused{},caretVisible{true};
};
struct NativeNotesWorkspaceHit {
    enum class Kind {body,resize,action,editor};
    std::string_view noteID,verb;Kind kind{Kind::body};core::Point workspacePoint;
};
struct NativeNotesWorkspaceStats {
    std::size_t cards{},visibleCards{},retiredCards{},retiredEditors{};
    std::uint64_t stateSynchronizations{},cardContentUpdates{},cardPlacementUpdates{},poseCardVisits{},editorContentUpdates{};
    std::size_t deletingCards{};
    std::uint64_t promotedFields{}; // short drafts reopened in the chunked editor by a paste
    std::size_t culledCards{}; // shown notes beneath the retained-card budget (no artwork)
};
using NativeNotesCardToken=std::uint64_t;
struct NativeNotesCardMotion {
    NativeNotesCardToken token{};
    // Sample notesSectionMotion/notesCardMotion/notesMutationMotion on the
    // OWNER'S existing clock. Scale uses the source card's center anchor.
    modules::NotesMotionSample sample;
};
struct NativeNotesFinishResult {bool finished{},saved{};};
enum class NativeNotesDrawingIssue {none,strokeLimit,drawingLimit};
// Attachment-free plain/rich Notes coordinator. It borrows ONE state, rasterizer, HWND and
// optional already-activated TSF manager. It creates no window, service, clock,
// publisher, persistence store, clipboard reader or global input hook.
// Only content events measure text/create artwork. Frame poses visit cached
// visible/outgoing cards, never scan NotesState or copy text. The measured font
// and style resolver drive settled lines and the same-object DWrite editor.
// Settled attributed line metrics and edited paragraph metrics follow their
// distinct original source paths. TODO uses plain font11 row editors. Optional
// still/GIF/video playback shares caller-owned decode/device/deadlines. Drawing uses
// retained completed/live/brush surfaces and commits only when the gesture ends;
// hidden unsupported records are not read/rendered. Stored text is never cut.
// Owner appends entries() to its ONE LayerComposition and republishes whenever
// compositionRevision changes. Removed scenes remain alive until collectRetired
// confirms their GPU resources have no published references. Detach every entry
// and collect retired resources BEFORE destruction; HWND/rasterizer/TSF/state
// outlive this object. Source confirmation-popup artwork is separate: deletion
// methods expose NotesState's pending confirmation, never delete on request.
class NativeNotesWorkspace final {
public:
    NativeNotesWorkspace(HWND,modules::NotesState&,LayerRasterizer&,
        NativeNotesWorkspaceStyle,NativeNotesWorkspaceOptions);
    ~NativeNotesWorkspace();
    NativeNotesWorkspace(const NativeNotesWorkspace&)=delete;
    NativeNotesWorkspace&operator=(const NativeNotesWorkspace&)=delete;
    // Explicit owner content event after direct state changes. Editor lifetime
    // is exclusively coordinated here; do not externally replace state.editing.
    bool syncState();
    // Finish the field before changing appearance/viewport. No state mutation
    // occurs when the requested geometry exceeds this adapter's native bounds.
    bool setStyle(NativeNotesWorkspaceStyle);
    std::uint64_t fontRevision()const noexcept;
    bool setWorkspaceBounds(core::Rect,std::optional<core::Point> creationPoint={});
    void setPresentation(bool notesSelected,bool retainOutgoing=false);
    void settleOutgoing(); // after owner's finite section transition; no clock here
    std::uint64_t presentationGeneration()const noexcept;
    bool settleOutgoing(std::uint64_t generation); // stale completion leaves new transition intact
    // Tokens survive content/theme/pose changes, but source section changes
    // renew tokens ONLY for cards whose target visibility changes (canceling
    // their old tracks). Pinned/unchanged cards keep their current motion.
    std::optional<NativeNotesCardToken> cardToken(std::string_view noteID)const noexcept;
    // Numeric patches, not a complete list: omitted cards keep their pose.
    // Identity sample resets a track. Returns true for every accepted batch,
    // including unchanged samples. A stale token rejects the entire batch
    // unchanged (false); invalid/repeated/nonfinite data throws before mutation.
    // No allocation, state scan, clock or resource upload; updatePose applies it.
    bool setCardMotions(std::span<const NativeNotesCardMotion>);
    bool updatePose(const NativeNotesWorkspacePose&);
    bool requiresFrames(double time)const;
    void connectVideoPlayback(NativeNotesVideoPlayback&);
    void connectImagePlayback(NativeNotesImagePlayback&); // once, before any media card was attached
    // Window visibility is independent from Notes selection: pinned media
    // stays active across module changes, but releases decoding on HUD hide.
    // Same caller clock as updatePose; preserveArtwork is source finite .6s.
    bool setMediaActive(bool,double time,bool preserveArtwork=false);
    bool acceptMedia(UINT_PTR routeGeneration,double time);
    bool sampleMedia(double time);
    // Shared root calls broker.accept/sample once BEFORE this. No decoder drain,
    // engine tick or sibling visibility change occurs here.
    bool refreshSharedMedia(double time);
    std::optional<double> mediaNextWakeTime()const;
    bool toggleMedia(std::string_view noteID,double time);
    bool beginMediaSeek(std::string_view noteID,core::Point physical,double time);
    bool updateMediaSeek(core::Point physical,double time);
    bool endMediaSeek(double time);
    bool mediaSeeking()const noexcept;
    // Before entries() publication. Upload only changed image frames and each
    // card's one retained quad; pointer/tilt updates do not touch textures.
    void uploadMedia(Renderer&);
    std::span<const LayerCompositionEntry> entries()const noexcept;
    std::uint64_t compositionRevision()const noexcept;
    // Call AFTER composition replacement. An attached scene rejects retirement
    // and stays alive; false likewise retains any still-published GPU assets.
    bool collectRetired(Renderer&);
    bool releaseResources(Renderer&); // only after owner removes ALL workspace entries
    bool select(std::optional<std::string>);
    bool createText(std::string id,double createdAt);
    bool createDrawing(std::string noteID,double createdAt);
    bool beginDrawing(std::string_view noteID,core::Point physical);
    bool updateDrawing(core::Point physical);
    bool endDrawing(); // commits once, including source cancel/section close
    bool drawingActive()const noexcept;
    bool toggleDrawingEraser(core::Point physical);
    bool updateDrawingHover(std::optional<core::Point> physical);
    bool setDrawingColor(modules::NotesColor);
    modules::NotesColor drawingColor()const noexcept;
    double drawingWidth()const noexcept;
    bool drawingErasing()const noexcept;
    NativeNotesDrawingIssue drawingIssue()const noexcept;
    bool createChecklist(std::string noteID,std::string firstItemID,double createdAt);
    bool createMedia(std::string noteID,double createdAt,std::string reference,core::Point,
        std::shared_ptr<void> accessLease={}); // retained through inspection handoff/playback/card retirement
    // A pasted image already copied to Notes/Images (NotesState::createManagedImage);
    // its card decodes the copy through options.legacyImagePath.
    bool createManagedImage(std::string noteID,double createdAt,std::string imageName,unsigned pixelWidth,unsigned pixelHeight,core::Point);
    // Explicit relink of an unavailable media note to a newly chosen, inspected
    // Windows file (NotesState::replaceMedia); the old decode retires normally.
    bool relinkMedia(std::string_view noteID,std::string reference,std::shared_ptr<void> accessLease={});
    std::optional<modules::NotesMediaKind> mediaKind(std::string_view noteID)const;
    bool addChecklistItem(std::string_view noteID,std::string itemID);
    bool mutateChecklistItem(std::string_view noteID,std::string_view itemID,modules::NotesState::ChecklistAction);
    bool beginEditingItem(std::string_view noteID,std::string_view itemID);
    std::optional<std::string_view> editingItemID()const noexcept;
    bool beginEditing(std::string_view noteID);
    NativeNotesFinishResult finishEditing(bool commit=true); // false.finished means TSF lock; retry later
    bool syncEditor(); // document/selection event, then owner republishes
    ProjectedEditorResult applyFormat(const core::notes::FormatChange&);
    ProjectedEditorResult undo();
    ProjectedEditorResult redo();
    std::optional<core::notes::TextStyle> selectionStyle()const;
    NativeNotesEditor* editor()noexcept;
    const core::text::Document* editorDocument()const noexcept;
    std::optional<std::string_view> editingNoteID()const noexcept;
    UINT_PTR editorGeneration()const noexcept;
    unsigned takeEditorChanges(UINT_PTR generation);
    HRESULT focusEditor(bool)noexcept;
    // Source Edit menu Cut/Copy/Paste on the open field; the owner reads and
    // writes the clipboard, these only edit the unsaved draft (one undo group
    // each). A paste that grows a short text note past maximumEditorUnits
    // first reopens the draft in the chunked editor (see promotedFields).
    std::optional<NotesClipboardText> copyEditorSelection()const;
    ProjectedEditorResult deleteEditorSelection();
    ProjectedEditorResult pasteIntoEditor(const NotesClipboardText&);
    // Word navigation keys of the open field (Ctrl+Left/Right with Shift
    // extending, Ctrl+Backspace/Delete) and the source double-click word and
    // triple-click paragraph selections around its caret.
    ProjectedEditorResult editorNavigationKey(unsigned virtualKey,bool control,bool shift);
    ProjectedEditorResult selectEditorWord();
    ProjectedEditorResult selectEditorParagraph();
    bool beginGesture(std::string_view,core::Point,modules::NotesState::Gesture);
    bool dragTo(core::Point); // movement retains content; resize is an explicit content event
    bool endGesture();
    void cancelInteraction(); // source semantics: finish changed geometry, not rollback
    bool togglePin(std::string_view);
    bool requestDeletion(std::string_view);
    void cancelDeletion();
    bool confirmDeletion(std::string_view);
    // Original source ordering: persistence/state removal first, then .20s
    // notesCardMotion(false) artwork. No automatic timing: caller samples this
    // returned token and settles it at completion (or immediately if reduced).
    std::optional<NativeNotesCardToken> confirmDeletionRetainingArtwork(std::string_view);
    bool settleDeletion(NativeNotesCardToken); // then republish + collectRetired
    std::optional<NativeNotesWorkspaceHit> hitTest(core::Point physicalClientPoint)const;
    // Positive-down physical client displacement, mapped through the top
    // card's CURRENT sampled projection. True means consumed, including at
    // bounds/zero delta/during a card gesture; nonfinite input returns false.
    // Scroll positions are session-only. Changed settled content reuses its
    // measured line index and rebuilds only viewport-bounded card artwork;
    // no text measurement, persistence, timer or full-document bitmap.
    bool scrollAt(core::Point physicalClientPoint,double physicalDeltaY);
    bool setFeedback(std::string_view noteID,std::optional<std::string_view> verb,bool pressed,bool reduceMotion,double time);
    const modules::NotesCardPresentation* card(std::string_view)const noexcept;
    NativeNotesWorkspaceStats stats()const noexcept;
    NativeNotesTextMeasureStats measurementStats()const;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::native
#endif
