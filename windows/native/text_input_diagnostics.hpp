#pragma once
#include <cstdint>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace endfield::native {
struct TextInputDiagnosticTiming {
    std::uint64_t calls{};
    double totalMilliseconds{},maximumMilliseconds{};
};
// Current UI-thread totals only. No text, ACP ranges, file names, HWNDs or
// document identities are recorded. Nested lock/extent counts identify work
// requested synchronously by the real TSF sink during a layout notification.
struct TextInputDiagnostics {
    bool enabled{};
    TextInputDiagnosticTiming notifyLayout,requestLock,getTextExt;
    std::uint64_t postAttempts{},newPosts{},layoutOnlyPostAttempts{};
    std::uint64_t placementChanged{},placementEqual{};
    std::uint64_t locksDuringLayout{},extentsDuringLayout{};
};
#ifdef _WIN32
namespace text_input_diagnostic_detail {
struct Recorder {
    TextInputDiagnostics values;
    LONGLONG frequency{};
    std::uint64_t epoch{};
    unsigned layoutDepth{};
};
inline thread_local Recorder recorder;
enum class Operation {layout,lock,extent};
class Scope final {
    Operation operation_;
    std::uint64_t epoch_{};
    LONGLONG start_{};
    bool active_{};
public:
    explicit Scope(Operation operation)noexcept:operation_(operation){
        auto&r=recorder;if(!r.values.enabled)return;active_=true;epoch_=r.epoch;
        auto& timing=operation==Operation::layout?r.values.notifyLayout:operation==Operation::lock?r.values.requestLock:r.values.getTextExt;
        ++timing.calls;
        if(operation==Operation::layout)++r.layoutDepth;
        else if(r.layoutDepth){if(operation==Operation::lock)++r.values.locksDuringLayout;else ++r.values.extentsDuringLayout;}
        LARGE_INTEGER now{};if(r.frequency>0&&QueryPerformanceCounter(&now))start_=now.QuadPart;
    }
    ~Scope(){
        if(!active_)return;auto&r=recorder;if(epoch_!=r.epoch)return;
        if(operation_==Operation::layout)--r.layoutDepth;
        LARGE_INTEGER now{};if(start_&&r.frequency>0&&QueryPerformanceCounter(&now)&&now.QuadPart>=start_){
            const double elapsed=double(now.QuadPart-start_)*1000./double(r.frequency);
            auto& timing=operation_==Operation::layout?r.values.notifyLayout:operation_==Operation::lock?r.values.requestLock:r.values.getTextExt;
            timing.totalMilliseconds+=elapsed;if(elapsed>timing.maximumMilliseconds)timing.maximumMilliseconds=elapsed;
        }
    }
    Scope(const Scope&)=delete;
    Scope&operator=(const Scope&)=delete;
};
inline void postAttempt(bool layoutOnly)noexcept{auto&r=recorder.values;if(r.enabled){++r.postAttempts;if(layoutOnly)++r.layoutOnlyPostAttempts;}}
inline void newPost()noexcept{auto&r=recorder.values;if(r.enabled)++r.newPosts;}
inline void placement(bool changed)noexcept{auto&r=recorder.values;if(r.enabled){if(changed)++r.placementChanged;else ++r.placementEqual;}}
} // namespace text_input_diagnostic_detail

// Enabling starts a fresh measurement interval. Disabling stops new samples
// and preserves the totals for a final snapshot. No clocks run in the default
// disabled path; no timer, thread, heap storage, logger or file I/O is created.
inline void setTextInputDiagnosticsEnabled(bool enabled)noexcept{
    auto&r=text_input_diagnostic_detail::recorder;
    if(enabled){r.values={};++r.epoch;r.layoutDepth=0;LARGE_INTEGER frequency{};r.frequency=QueryPerformanceFrequency(&frequency)?frequency.QuadPart:0;}
    r.values.enabled=enabled;
}
inline TextInputDiagnostics textInputDiagnostics()noexcept{return text_input_diagnostic_detail::recorder.values;}
#endif
} // namespace endfield::native
