#pragma once
#include "app/module_owner.hpp"
#include "native/audio_runtime.hpp"
#include <stdexcept>
#include <string_view>
#include <utility>
#include <windows.h>

namespace endfield::app {
// The shared AudioRuntime's Volume binding publishes synchronously (the
// VolumeSnapshot conversion, its failure text and the Volume canvas's
// receiver), so every binding call that can publish (receive, setVisible,
// the language's failure text) runs under the Volume owner's failure
// isolation (ModuleRegistry::guard, as Application::Impl::isolate): a
// production failure disables only Volume and never reaches the host.
// A Volume owner that is disabled, by this call or any earlier one, or
// absent (its startup failed) closes the binding
// (VolumeProviderBinding::close): its activation vote is withdrawn, so a dead
// Volume never keeps endpoint and per-app session reads running, and every
// later binding call is a no-op. False when Volume did not run the call or
// failed in it.
template <class F>
bool volumeBindingCall(ModuleRegistry& registry, ModuleOwner* volume, native::AudioRuntime& audio, std::string_view operation, F&& f) {
    if (volume && registry.guard(*volume, std::forward<F>(f), operation)) return true;
    audio.volume().close();
    return false;
}

// The worker's notice for the HUD window: post the audio owner message. A
// post that fails throws, as the utility notice and Now Playing's wake do, so
// AudioRuntime's coalescer re-arms and the next publication posts again; a
// dropped result left the notice pending and audio silent for the run.
inline native::AudioRuntime::Notice audioNotice(HWND window, UINT message, WPARAM generation) {
    return [window, message, generation] { if (!PostMessageW(window, message, generation, 0)) throw std::runtime_error("Post audio change"); };
}

// The audio owner message (application_events.cpp): adopt the worker's
// publication outside Volume's isolation (Now Playing routes, the Event Log
// device list), then republish Volume within it.
inline native::AudioRuntimeChanges drainAudio(ModuleRegistry& registry, ModuleOwner* volume, native::AudioRuntime& audio) {
    auto changes = audio.drain();
    if (changes.snapshot) (void)volumeBindingCall(registry, volume, audio, "audio", [&] { changes.volume = audio.volume().receive(); });
    return changes;
}
} // namespace endfield::app
