#pragma once
// Production fatal diagnostics (app/main.cpp writes
// <data root>/v1/Diagnostics/last-fatal.txt): which host stage and which
// module owner were active on the UI thread when an exception left the
// Application run.
//
//   const FatalStage stage("deadline");                  // host callback
//   const FatalStage stage(operation, owner.name());     // ModuleRegistry call
//
// Stages and owner names are static text (operation literals, owner name()
// literals); never paths, file names or document text. The innermost scope
// left by an exception records the whole active path once, while every outer
// scope and owner still exists; outer scopes unwound by the same exception
// keep it. A scope that completes normally while no exception is in flight
// clears it: that failure was handled (ModuleRegistry isolation, device
// recovery, a module that caught its own error). No allocation and no
// locking: fixed thread-local storage, so warm frames stay allocation-free.
#include <array>
#include <cstddef>
#include <exception>
#include <string_view>

namespace endfield::app {
class FatalStage final {
public:
    explicit FatalStage(std::string_view stage, std::string_view owner = {}) noexcept : entered_(std::uncaught_exceptions()) {
        auto& s = state();
        if (s.depth < s.frames.size()) s.frames[s.depth] = {stage, owner};
        ++s.depth;
    }
    ~FatalStage() {
        auto& s = state();
        const int inFlight = std::uncaught_exceptions();
        if (inFlight > entered_) {
            if (!s.recorded) record(s);
        } else if (inFlight == 0) {
            s.recorded = false;
            s.length = 0;
        }
        if (s.depth) --s.depth;
    }
    FatalStage(const FatalStage&) = delete;
    FatalStage& operator=(const FatalStage&) = delete;
    // "launch > application run > deadline > event log save"; an owner call
    // reads "frame [notes]". Empty when no failure path was recorded.
    static std::string_view last() noexcept {
        const auto& s = state();
        return {s.text.data(), s.length};
    }

private:
    struct Frame { std::string_view stage, owner; };
    struct State {
        std::array<Frame, 12> frames{};
        std::size_t depth{};
        std::array<char, 384> text{};
        std::size_t length{};
        bool recorded{};
    };
    static State& state() noexcept { thread_local State value; return value; }
    static void append(State& s, std::string_view value) noexcept {
        for (const char c : value) {
            if (s.length + 1 >= s.text.size()) return;
            // Printable ASCII only: stage and owner names are code literals.
            s.text[s.length++] = c >= 0x20 && c < 0x7f ? c : '?';
        }
    }
    static void record(State& s) noexcept {
        s.length = 0;
        const auto count = s.depth < s.frames.size() ? s.depth : s.frames.size();
        for (std::size_t n = 0; n < count; ++n) {
            if (n) append(s, " > ");
            append(s, s.frames[n].stage);
            if (!s.frames[n].owner.empty()) { append(s, " ["); append(s, s.frames[n].owner); append(s, "]"); }
        }
        if (s.depth > s.frames.size()) append(s, " > ...");
        s.recorded = true;
    }
    int entered_;
};
} // namespace endfield::app
