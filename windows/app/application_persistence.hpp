#pragma once
// Production storage entry points of the Notes and File Shelf owners.
//
// EndfieldHUD.exe opens the persistent versioned data root
// (core/data/application_root.hpp). The development owners in tools/ accept
// only a NEW fixture root (and Notes seeds a sample note into it); their
// persistent entry points are
//   NotesPreview(..., NotesPreviewMediaOptions, bool persistentDataRoot)
//   ShelfPreviewOptions::persistentDataRoot
// which open an existing root as it is (no "must be new" check, no sample).
// These adapters select them in production and keep the fixture contract in
// development. A build whose owners do not provide them refuses to start
// Notes (so the user's data is never touched by fixture code) and starts
// without the File Shelf, with a diagnostic naming the missing entry point.
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace endfield::app {
template <class Owner, class... Args>
inline constexpr bool persistentNotesSupported = std::is_constructible_v<Owner, Args&&..., bool>;

template <class Owner, class... Args>
std::unique_ptr<Owner> makeNotesOwner(bool persistent, Args&&... values) {
    if (!persistent) return std::make_unique<Owner>(std::forward<Args>(values)...);
    if constexpr (persistentNotesSupported<Owner, Args...>) return std::make_unique<Owner>(std::forward<Args>(values)..., true);
    else throw std::runtime_error("This EndfieldHUD build cannot open persistent Notes data (NotesPreview persistentDataRoot entry point missing)");
}

template <class Options>
inline constexpr bool persistentShelfSupported = requires(Options& value) { value.persistentDataRoot = true; };

template <class Options>
void persistentShelfRoot(Options& options) {
    if constexpr (persistentShelfSupported<Options>) options.persistentDataRoot = true;
    else throw std::runtime_error("This EndfieldHUD build cannot open persistent File Shelf data (ShelfPreviewOptions::persistentDataRoot missing)");
}
} // namespace endfield::app
