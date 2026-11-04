#pragma once
// Production storage entry points of the Notes and File Shelf owners.
//
// EndfieldHUD.exe opens the persistent versioned data root
// (core/data/application_root.hpp) through the owners' persistent entry points
//   NotesPreview(..., NotesPreviewMediaOptions, bool persistentDataRoot)
//   ShelfServiceOptions::persistentDataRoot
// which open the existing root as it is: no "must be new" check and no sample
// note (a fresh install creates the root). Development previews and hidden
// coverage keep the NEW-root fixture contract. Both entry points are required
// at compile time, so fixture-only owners can never be wired to user data.
#include <memory>
#include <type_traits>
#include <utility>

namespace endfield::app {
template <class Owner, class... Args>
inline constexpr bool persistentNotesSupported = std::is_constructible_v<Owner, Args&&..., bool>;

// Development (persistent=false): NEW fixture root with its sample note.
// Production (persistent=true): the existing versioned root.
template <class Owner, class... Args>
std::unique_ptr<Owner> makeNotesOwner(bool persistent, Args&&... values) {
    static_assert(persistentNotesSupported<Owner, Args...>, "The Notes owner must provide the persistent data root entry point");
    return std::make_unique<Owner>(std::forward<Args>(values)..., persistent);
}

template <class Options>
inline constexpr bool persistentShelfSupported = requires(Options& value) { value.persistentDataRoot = true; };

template <class Options>
void persistentShelfRoot(Options& options) {
    static_assert(persistentShelfSupported<Options>, "The File Shelf owner must provide the persistent data root entry point");
    options.persistentDataRoot = true;
}
} // namespace endfield::app
