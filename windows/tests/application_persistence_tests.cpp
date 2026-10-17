// Production storage entry-point selection for the Notes and File Shelf owners
// (app/application_persistence.hpp) over synthetic owner types: development
// keeps the NEW-root fixture constructor, production uses the persistent entry
// point, and an owner without that entry point cannot be selected at all (the
// former runtime refusal is now a compile-time requirement). The real owners
// are exercised on pre-existing roots by application_data_root_tests.
#include "app/application_persistence.hpp"
#include <iostream>
#include <stdexcept>
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

// The selection is a compile-time requirement: fixture-only owners are not
// accepted by makeNotesOwner/persistentShelfRoot (static_assert).
static_assert(app::persistentNotesSupported<PersistentNotes, std::string, int, const Media&>);
static_assert(!app::persistentNotesSupported<FixtureOnlyNotes, std::string, int, const Media&>);
static_assert(app::persistentShelfSupported<PersistentShelfOptions> && !app::persistentShelfSupported<FixtureShelfOptions>);
}

int main() {
    try {
        const Media media{7};
        auto fixture = app::makeNotesOwner<PersistentNotes>(false, std::string("new-root"), 1, media);
        check(fixture->root == "new-root" && !fixture->persistent, "Development keeps the NEW-root fixture entry point");
        auto production = app::makeNotesOwner<PersistentNotes>(true, std::string("v1"), 1, media);
        check(production->root == "v1" && production->persistent, "Production opens the persistent root");
        check(app::persistentNotesSupported<PersistentNotes, std::string, int, const Media&> &&
              !app::persistentNotesSupported<FixtureOnlyNotes, std::string, int, const Media&>, "Entry point detection");
        PersistentShelfOptions shelf{"v1"};
        check(!shelf.persistentDataRoot, "Shelf options default to the fixture contract");
        app::persistentShelfRoot(shelf);
        check(shelf.persistentDataRoot && shelf.newDataRoot == "v1", "Production Shelf opens the persistent root");
        check(app::persistentShelfSupported<PersistentShelfOptions> && !app::persistentShelfSupported<FixtureShelfOptions>, "Shelf detection");
        std::cout << "Persistent storage entry points: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Persistent storage entry points failed after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
