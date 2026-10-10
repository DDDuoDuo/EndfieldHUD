#include "app/module_owner.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace endfield::app {
namespace {
int priority(const ModuleRouting& r, ModuleRegistry::Chain chain) {
    using C = ModuleRegistry::Chain;
    switch (chain) {
    case C::pointerCapture: return r.pointerCapture;
    case C::pointer: return r.pointer;
    case C::wheelCapture: return r.wheelCapture;
    case C::wheel: return r.wheel;
    case C::keyCapture: return r.keyCapture;
    case C::key: return r.key;
    case C::filterKey: return r.filterKey;
    case C::message: return r.message;
    case C::finishEditing: return r.finishEditing;
    case C::flush: return r.flush;
    case C::count: break;
    }
    return ModuleRouting::noRoute;
}
}

void ModuleRegistry::add(ModuleOwner& owner) {
    if (iterating_) throw std::logic_error("Module registry cannot change while routing");
    if (find(owner)) throw std::logic_error("Module owner is already registered");
    slots_.push_back({&owner, owner.routing(), false});
    rebuild();
}
void ModuleRegistry::remove(ModuleOwner& owner) noexcept {
    if (iterating_) { std::fputs("Module registry removal during routing was ignored\n", stderr); return; }
    const auto it = std::find_if(slots_.begin(), slots_.end(), [&](const Slot& s) { return s.owner == &owner; });
    if (it == slots_.end()) return;
    slots_.erase(it);
    try { rebuild(); } catch (...) { for (auto& chain : chains_) chain.clear(); }
}
bool ModuleRegistry::contains(const ModuleOwner& owner) const noexcept { return find(owner) != nullptr; }
bool ModuleRegistry::enabled(const ModuleOwner& owner) const noexcept { const auto* s = find(owner); return s && !s->failed; }
ModuleRegistry::Slot* ModuleRegistry::find(const ModuleOwner& owner) noexcept {
    for (auto& s : slots_) if (s.owner == &owner) return &s;
    return nullptr;
}
const ModuleRegistry::Slot* ModuleRegistry::find(const ModuleOwner& owner) const noexcept {
    for (const auto& s : slots_) if (s.owner == &owner) return &s;
    return nullptr;
}
void ModuleRegistry::rebuild() {
    for (std::size_t c = 0; c < chains_.size(); ++c) {
        auto& chain = chains_[c];
        chain.clear();
        for (auto& s : slots_) if (priority(s.routing, static_cast<Chain>(c)) != ModuleRouting::noRoute) chain.push_back(&s);
        // Stable: equal priorities keep registration order.
        std::stable_sort(chain.begin(), chain.end(), [c](const Slot* a, const Slot* b) {
            return priority(a->routing, static_cast<Chain>(c)) < priority(b->routing, static_cast<Chain>(c));
        });
    }
}
void ModuleRegistry::disable(Slot& slot, const char* what, std::string_view operation) {
    if (slot.failed) return;
    slot.failed = true;
    ModuleFailure failure{std::string(slot.owner->name()), what ? what : "unknown", std::string(operation)};
    // Diagnostic contains the module and exception type only; never the
    // exception message, document text, file names or account values.
    const auto line = "EndfieldHUD module " + failure.owner + " was disabled after a " + failure.what + " during " + failure.operation + "\n";
    std::fputs(line.c_str(), stderr);
#ifdef _WIN32
    OutputDebugStringA(line.c_str());
#endif
    failures_.push_back(failure);
    if (observer_) observer_(failures_.back());
}
ModuleOwner* ModuleRegistry::presenter(core::Module module) const noexcept {
    for (const auto& s : slots_) if (!s.failed && s.owner->presents(module)) return s.owner;
    return nullptr;
}
bool ModuleRegistry::covers(core::Point p) { return any([&](ModuleOwner& o) { return o.covers(p); }, "hit test"); }
bool ModuleRegistry::pointerLocked() { return any([&](ModuleOwner& o) { return o.pointerLocked(); }, "pointer lock"); }
bool ModuleRegistry::requiresFrames(double time) { return any([&](ModuleOwner& o) { return o.requiresFrames(time); }, "frame demand"); }
std::optional<double> ModuleRegistry::nextWakeTime(double now) {
    std::optional<double> next;
    forEach([&](ModuleOwner& o) { if (const auto value = o.nextWakeTime(now); value && (!next || *value < *next)) next = value; }, "deadline");
    return next;
}
void ModuleRegistry::releaseAll(native::Renderer& renderer) noexcept {
    ++iterating_;
    for (auto it = slots_.rbegin(); it != slots_.rend(); ++it) {
        try { it->owner->release(renderer); } catch (...) {}
    }
    --iterating_;
}
} // namespace endfield::app
