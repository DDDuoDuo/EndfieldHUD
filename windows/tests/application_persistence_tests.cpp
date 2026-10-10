// Production storage entry-point selection for the Notes and File Shelf owners
// (app/application_persistence.hpp) over synthetic owner types: development
// keeps the NEW-root fixture constructor, production uses the persistent
// entry point, and a build without it refuses instead of running fixture code
// against the user's data.
#include "app/application_persistence.hpp"
#include <iostream>
#include <string>

namespace app = endfield::app;
namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }

struct Media { int client{}; };
// Mirrors NotesPreview's signature with and without the persistent flag.
struct FixtureOnlyNotes {
    std::string root; bool persistent{};
    FixtureOnlyNotes(std::string r, int, Media) : root(std::move(r)) {}
};
struct PersistentNotes {
    std::string root; bool persistent{};
    PersistentNotes(std::string r, int, Media, bool p = false) : root(std::move(r)), persistent(p) {}
};
struct FixtureShelfOptions { std::string newDataRoot; bool nativeIcons{true}; };
struct PersistentShelfOptions { std::string newDataRoot; bool nativeIcons{true}; bool persistentDataRoot{}; };
template <class F> bool throws(F&& f) { try { f(); } catch (const std::runtime_error&) { return true; } return false; }
}

int main() {
    try {
        const Media media{7};
        auto fixture = app::makeNotesOwner<PersistentNotes>(false, std::string("new-root"), 1, media);
        check(fixture->root == "new-root" && !fixture->persistent, "Development keeps the NEW-root fixture entry point");
        auto production = app::makeNotesOwner<PersistentNotes>(true, std::string("v1"), 1, media);
        check(production->root == "v1" && production->persistent, "Production opens the persistent root");
        check(app::makeNotesOwner<FixtureOnlyNotes>(false, std::string("new-root"), 1, media)->root == "new-root", "Fixture-only owner still serves development");
        check(throws([&] { (void)app::makeNotesOwner<FixtureOnlyNotes>(true, std::string("v1"), 1, media); }),
              "Production refuses a Notes owner without the persistent entry point");
        check(app::persistentNotesSupported<PersistentNotes, std::string, int, const Media&> &&
              !app::persistentNotesSupported<FixtureOnlyNotes, std::string, int, const Media&>, "Entry point detection");
        PersistentShelfOptions shelf{"v1"};
        app::persistentShelfRoot(shelf);
        check(shelf.persistentDataRoot && shelf.newDataRoot == "v1", "Production Shelf opens the persistent root");
        FixtureShelfOptions old{"v1"};
        check(throws([&] { app::persistentShelfRoot(old); }), "Production refuses a Shelf owner without the persistent entry point");
        check(app::persistentShelfSupported<PersistentShelfOptions> && !app::persistentShelfSupported<FixtureShelfOptions>, "Shelf detection");
        std::cout << "Persistent storage entry points: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Persistent storage entry points failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
