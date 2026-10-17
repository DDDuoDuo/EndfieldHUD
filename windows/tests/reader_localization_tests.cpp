// Reader display-time localization against the unchanged Mac sources
// (tools/reader_localization_reference.py -> reader-localization-source.json),
// the typed ReaderState error and the confirmed-write Event Log actions
// (source ReaderController.onEvent). Portable; synthetic in-memory repository,
// FIFO and document source only: no file, picker, window or OS API.
#include "modules/reader_localization.hpp"
#include "modules/reader_state.hpp"
#include "core/data/file_io.hpp"
#include <array>
#include <cstdio>
#include <deque>
#include <iostream>

namespace {
using namespace endfield;
namespace m = modules;
using J = ehud::data::Json;
using L = core::Language;
unsigned checks{};
void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
constexpr std::array<std::pair<L, const char*>, 5> languages{{{L::english, "english"}, {L::simplifiedChinese, "simplifiedChinese"},
    {L::traditionalChinese, "traditionalChinese"}, {L::japanese, "japanese"}, {L::korean, "korean"}}};
constexpr std::array<std::pair<m::ReaderErrorCode, const char*>, 7> errors{{{m::ReaderErrorCode::unsupported, "unsupported"},
    {m::ReaderErrorCode::invalidArchive, "invalidArchive"}, {m::ReaderErrorCode::tooLarge, "tooLarge"}, {m::ReaderErrorCode::invalidBook, "invalidBook"},
    {m::ReaderErrorCode::unavailable, "unavailable"}, {m::ReaderErrorCode::encrypted, "encrypted"}, {m::ReaderErrorCode::changedOnDisk, "changedOnDisk"}}};

void source(const J& root) {
    check(root["source"]["authority"].string() == "ca04f142185c7de40acd8523bdb563195d90a1d1", "Fixture comes from the pinned Mac authority");
    for (const auto& [code, key] : errors) {
        const auto& row = root["errors"][key];
        const auto pair = m::readerErrorText(code);
        check(pair.english == row["english"].string() && pair.simplified == row["simplifiedChinese"].string(), "ReaderErrorCode pairs equal ReaderError.errorDescription");
        check(m::ReaderError(code).what() == row["english"].string(), "ReaderError keeps the English source text in what()");
        for (const auto& [language, name] : languages)
            check(m::readerErrorMessage(code, language) == row[name].string(), "Typed Reader errors render the Mac catalog row in all five languages");
        check(m::readerErrorMessage(code, L::system) == row["english"].string(), "Unresolved System renders English like core::localized");
        check(m::readerImportFailure(std::make_exception_ptr(m::ReaderError(code)), L::japanese) == row["japanese"].string(),
              "A typed import failure shows its source description (ReaderController.fail)");
    }
    const auto& captions = root["captions"];
    for (const auto& [language, name] : languages) {
        const auto s = m::readerStrings(language);
        const auto text = [&](const char* field) { return captions[field][name].string(); };
        check(s.open == text("open") && s.library == text("library") && s.reading == text("reading") && s.bookmarks == text("bookmarks"),
              "Toolbar captions are the source ReaderCanvas actions");
        check(s.title == text("title") && s.empty == text("empty") && s.loading == text("loading"), "Title, empty and loading text follow ReaderCanvas");
        check(s.chooseShelf == text("chooseShelf") && s.shelfTitle == text("shelfTitle"), "Shelf chooser and menu titles follow the source");
        check(s.font == text("font") && s.fontSize == text("fontSize") && s.lineSpacing == text("lineSpacing") && s.margin == text("margin") &&
              s.horizontal == text("horizontal") && s.vertical == text("vertical"), "Reading settings follow ReaderSettingsMenu");
        check(s.chooseFile == m::readerTranslation(m::readerWindowsChooseFile, language) && s.chooseFile != text("chooseFile"),
              "Windows replaces the Finder chooser row with File Explorer in every language");
        check(s.language == language, "The captions carry their resolved language");
        check(s.relink == m::readerTranslation(m::readerWindowsRelink, language) && s.relink.ends_with("…"),
              "Relink… (Windows only: Mac bookmarks need no relink) follows the import card's wording in every language");
    }
    check(m::readerTranslation(m::readerWindowsRelink, L::traditionalChinese) == "重新連結…" && m::readerTranslation(m::readerWindowsRelink, L::korean) == "다시 연결…",
          "The relink caption uses the import card's terms");
    check(m::ReaderStrings{}.language == L::english && m::ReaderStrings::simplifiedChinese().language == L::simplifiedChinese,
          "Default captions are English; the Chinese source captions say so");
    auto english = m::readerStrings(L::english);
    english.chooseFile = m::ReaderStrings{}.chooseFile;
    check(english == m::ReaderStrings{}, "English captions other than the Windows chooser are the Mac defaults");
    auto chinese = m::readerStrings(L::simplifiedChinese);
    chinese.chooseFile = m::ReaderStrings::simplifiedChinese().chooseFile;
    check(chinese == m::ReaderStrings::simplifiedChinese(), "Simplified Chinese captions are the Mac source pairs");
    check(m::readerTranslation(m::readerWindowsOpenFailed, L::system) == "Unable to open selected files" &&
          m::readerImportFailure(std::make_exception_ptr(std::runtime_error("C:\\private\\path.pdf")), L::korean) == "선택한 파일을 열 수 없습니다" &&
          m::readerImportFailure({}, L::traditionalChinese) == "無法開啟所選檔案", "Untyped import failures show the Windows notice, never their own text");
    check(m::readerTranslation(m::readerWindowsPickerBusy, L::japanese) == "ファイル選択画面は使用中です", "The busy picker notice is localized");
}

// --- ReaderState: typed error and Event Log actions ---------------------------
std::string id(unsigned n) {
    char s[64];
    std::snprintf(s, sizeof(s), "B0000000-0000-4000-8000-%012u", n);
    return s;
}
m::ReaderBook book(unsigned n) { return m::windowsReaderReference(id(n), "C:\\Owned-reader-fixture\\book" + std::to_string(n) + ".pdf", "Book " + std::to_string(n)); }
struct Repository : m::ReaderRepository {
    m::ReaderLibrary value;
    unsigned saves{};
    bool fail{}, conflict{};
    m::ReaderLibrary load() override { return value; }
    void save(const m::ReaderLibrary& v) override {
        ++saves;
        if (conflict) throw m::ReaderError(m::ReaderErrorCode::changedOnDisk);
        if (fail) throw std::runtime_error("Synthetic reader save failure");
        value = v;
    }
};
struct Queue {
    struct Job { std::function<void()> work; std::function<void(std::exception_ptr)> done; };
    std::deque<Job> jobs;
    m::ReaderExecutor executor() { return {[this](auto w, auto d) { jobs.push_back({std::move(w), std::move(d)}); return true; }}; }
    void one() {
        check(!jobs.empty(), "A queued job exists");
        auto job = std::move(jobs.front());
        jobs.pop_front();
        std::exception_ptr error;
        try { job.work(); } catch (...) { error = std::current_exception(); }
        job.done(error);
    }
    void drain() {
        for (unsigned n = 0; !jobs.empty(); ++n) {
            check(n < 1000, "No retry loop");
            one();
        }
    }
};
struct Source : m::ReaderDocumentSource {
    std::shared_ptr<m::ReaderPagePixels> pixels = std::make_shared<m::ReaderPagePixels>();
    bool failOpen{};
    Source() { pixels->width = pixels->height = 200; pixels->bytes.resize(200 * 200 * 4); }
    void open(const m::ReaderBook&, const std::function<bool()>&) override { if (failOpen) throw m::ReaderError(m::ReaderErrorCode::encrypted); }
    m::ReaderPage page(int index) {
        m::ReaderPage p;
        p.location.section = index;
        p.progress = index / 5.;
        p.summary = "Synthetic page";
        p.pixels = pixels;
        if (index < 5) p.next = m::ReaderLocation{index + 1};
        if (index > 0) p.previous = m::ReaderLocation{index - 1};
        return p;
    }
    std::vector<m::ReaderPage> render(std::optional<m::ReaderLocation> location, std::optional<double>, const m::ReaderPreferences&, core::Point, bool,
                                      const std::function<bool()>&) override {
        const int at = location ? static_cast<int>(location->section) : 0;
        std::vector<m::ReaderPage> out{page(at)};
        if (at < 5) out.push_back(page(at + 1));
        return out;
    }
    m::ReaderPage detail(m::ReaderLocation l, const m::ReaderPreferences&, core::Point, bool, m::ReaderImageView, const std::function<bool()>&) override {
        return page(static_cast<int>(l.section));
    }
    void release() override {}
};

m::ReaderBook macBook(unsigned n) {
    m::ReaderBook b;
    b.id = id(n);
    b.title = "Imported " + std::to_string(n);
    b.platform = m::ReaderReferencePlatform::macOS;
    b.bookmarkBase64 = "AQ==";
    b.path = "/Users/synthetic/Books/book" + std::to_string(n) + ".epub";
    b.location.section = 3;
    b.progress = .5;
    b.bookmarks = {{id(90 + n), b.location, .5}};
    b.originalFields = J::Object{{"syntheticUnknown", true}};
    return b;
}

// Library "Relink…" (Windows only) and the Migration/relink.json hooks: rows
// change after the confirmed write that contains the relink or removal.
void relink() {
    auto repo = std::make_shared<Repository>();
    repo->value.books = {macBook(1), macBook(2), book(3)};
    repo->value.selected = id(1);
    repo->value.preferences = m::ReaderPreferences::defaults();
    Queue q;
    auto source = std::make_shared<Source>();
    m::ReaderState state(repo, q.executor(), source);
    std::vector<std::string> log, ledger;
    unsigned reconciled{};
    state.setEventObserver([&](std::string_view action) { log.emplace_back(action); });
    state.setRelinkLedger({[&](const m::ReaderLibrary& library) { ++reconciled; check(library.books.size() == 3, "Reconcile sees the loaded library"); },
                           [&](const std::string& book, const std::string& path) { ledger.push_back("resolved " + book + " " + path); },
                           [&](const std::string& book) { ledger.push_back("dismissed " + book); }});
    check(!state.relink(id(1), "C:\\Owned-reader-fixture\\relinked.epub"), "Nothing relinks before the library loads");
    state.setActive(true);
    q.drain();
    check(reconciled == 1 && ledger.empty() && state.library().preferences.fontName == "Georgia", "The load reconciles once and keeps the imported font name");
    const auto path = std::string("C:\\Owned-reader-fixture\\relinked.epub");
    bool unknown{};
    try { state.relink(id(77), path); } catch (const m::ReaderError& e) { unknown = e.code() == m::ReaderErrorCode::unavailable; }
    check(unknown, "Relinking an unknown book is refused as unavailable");
    repo->fail = true;
    check(state.relink(id(2), path), "Relink the second imported book");
    q.drain();
    check(ledger.empty() && log.empty() && state.hasUnsavedChanges(), "A failed library write resolves no ledger row and reports nothing");
    repo->fail = false;
    state.retryPendingWrites();
    q.drain();
    check(ledger == std::vector<std::string>{"resolved " + id(2) + " " + path} && log.front() == "imported", "The retried durable write resolves the row once and reports the import");
    const auto& saved = repo->value.books[1];
    check(saved.id == id(2) && saved.platform == m::ReaderReferencePlatform::windows && saved.windowsPath == path && saved.title == "Imported 2" &&
          saved.bookmarks == macBook(2).bookmarks && saved.originalFields == macBook(2).originalFields && saved.path.empty() && saved.bookmarkBase64.empty() &&
          repo->value.selected == id(2), "The relinked book keeps id, title, bookmarks and unknown fields, drops the Mac locator and is selected");
    check(state.book() && state.book()->id == id(2) && state.current(), "The relinked book opens");
    ledger.clear();
    log.clear();
    check(!state.relink(id(2), path) && ledger.empty(), "Relinking to the same file again changes nothing");
    q.drain();
    check(state.remove(id(3)), "Remove a Windows book");
    q.drain();
    check(ledger.empty(), "A Windows book has no ledger row to dismiss");
    check(state.remove(id(1)), "Remove the other imported Mac book");
    check(ledger.empty(), "Dismissal waits for the confirmed write");
    q.drain();
    check(ledger == std::vector<std::string>{"dismissed " + id(1)} && log.back() == "deleted", "The confirmed removal dismisses the imported book's row");
}

void events() {
    auto repo = std::make_shared<Repository>();
    repo->value.books = {book(1), book(2)};
    repo->value.selected = id(1);
    repo->value.books[0].location.section = 0;
    Queue q;
    auto source = std::make_shared<Source>();
    m::ReaderState state(repo, q.executor(), source);
    std::vector<std::string> log;
    state.setEventObserver([&](std::string_view action) { log.emplace_back(action); });
    state.setActive(true);
    q.drain();
    check(state.current() && log.empty(), "Opening the saved location records nothing");
    check(state.next(), "Turn a page");
    q.drain();
    check(log == std::vector<std::string>{"progress"}, "A confirmed write of a new location records progress once");
    log.clear();
    // A fraction-only rewrite of the same location is not a progress event.
    check(state.saveProgress(state.current()->location, .99, id(1)), "Fraction-only progress write");
    q.drain();
    check(log.empty(), "Only a changed saved location is progress (source writeProgress)");
    check(state.toggleBookmark(id(10)), "Bookmark");
    check(log.empty(), "Bookmark waits for its confirmed write");
    q.drain();
    check(log == std::vector<std::string>{"bookmarked"}, "Bookmark is recorded after the durable write");
    log.clear();
    auto prefs = state.library().preferences;
    prefs.fontSize = 12;
    check(state.setPreferences(prefs), "Settings change");
    q.drain();
    check(log == std::vector<std::string>{"settings"}, "Settings are recorded after the durable write");
    log.clear();
    // Coalesced writes: two actions, one write, both reported once in order.
    repo->fail = true;
    check(state.remove(id(2)) && state.toggleBookmark(id(11)), "Remove and bookmark before the write");
    q.drain();
    check(log.empty() && state.error() && !state.errorCode() && state.hasUnsavedChanges(), "A failed write reports nothing and keeps the untyped error");
    repo->fail = false;
    state.retryPendingWrites();
    q.drain();
    check(log == std::vector<std::string>{"deleted", "bookmarked"}, "The retried write reports each contained action once, in order");
    log.clear();
    auto again = book(1);
    check(!state.addValidated(again) && log == std::vector<std::string>{"imported"},
          "Re-importing the durable current reference is reported at once (source open(url:))");
    log.clear();
    check(state.addValidated(book(3)), "Import a new reference");
    check(log.empty(), "Import waits for its confirmed write");
    q.drain();
    check(log == std::vector<std::string>{"imported"}, "Import is recorded after the durable write");
    log.clear();
    // A typed failure keeps its code for display-time localization.
    repo->conflict = true;
    prefs.fontSize = 13;
    check(state.setPreferences(prefs), "Settings change on a conflicting library");
    q.one(); // the write; the preview's re-render follows
    check(state.errorCode() == m::ReaderErrorCode::changedOnDisk && state.error() == std::string(m::ReaderError(m::ReaderErrorCode::changedOnDisk).what()) &&
          log.empty(), "A changed-on-disk conflict is typed and reports nothing");
    q.drain();
    check(!state.errorCode() || state.error(), "The typed code never outlives its error text");
    repo->conflict = false;
    state.retryPendingWrites();
    q.drain();
    check(!state.error() && !state.errorCode() && log == std::vector<std::string>{"settings"}, "The successful retry clears the typed error and reports once");
    log.clear();
    // Provider failure while opening another book.
    source->failOpen = true;
    check(state.select(id(1)), "Select the other book");
    q.drain();
    check(state.errorCode() == m::ReaderErrorCode::encrypted && log.empty(), "A typed provider error is kept; selection is not an Event Log action");
    // A detached observer stops reports.
    state.setEventObserver({});
    prefs.fontSize = 14;
    check(state.setPreferences(prefs), "Settings change without an observer");
    q.drain();
    check(log.empty() && repo->value.preferences.fontSize == 14, "Without an observer the write still lands and nothing is reported");
}
} // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "Usage: reader_localization_tests reader-localization-source.json");
        const auto raw = ehud::data::detail::readFile(argv[1], 1024 * 1024);
        check(raw.has_value(), "The Mac oracle fixture exists");
        source(J::parse(*raw));
        events();
        relink();
        std::cout << "PASS " << checks << " Reader localization and event checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
