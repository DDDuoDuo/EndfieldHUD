#include "app/presentation_priority.hpp"
#include "native/renderer.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <avrt.h>

namespace endfield::app {
namespace {
// d3dkmthk.h D3DKMT_SCHEDULINGPRIORITYCLASS: NORMAL 2, HIGH 4. The entry
// point is exported by gdi32 (no WDK header in this build).
constexpr int schedulingNormal = 2, schedulingHigh = 4;
using SetSchedulingClass = LONG(WINAPI*)(HANDLE, int);
SetSchedulingClass schedulingClassEntry() noexcept {
    const auto gdi = GetModuleHandleW(L"gdi32.dll");
    return gdi ? reinterpret_cast<SetSchedulingClass>(reinterpret_cast<void*>(GetProcAddress(gdi, "D3DKMTSetProcessSchedulingPriorityClass"))) : nullptr;
}
}
PresentationPriority::~PresentationPriority() { restore(); }
void PresentationPriority::raise(native::Renderer* renderer) noexcept {
    if (raised_) return;
    raised_ = true; renderer_ = renderer; ++report_.raises;
    const auto process = GetCurrentProcess();
    report_.previousClass = GetPriorityClass(process);
    report_.processRaised = report_.previousClass == NORMAL_PRIORITY_CLASS && SetPriorityClass(process, ABOVE_NORMAL_PRIORITY_CLASS) != FALSE;
    DWORD index = 0;
    task_ = AvSetMmThreadCharacteristicsW(L"Games", &index);
    taskIndex_ = index;
    report_.threadRegistered = task_ != nullptr;
    if (const auto entry = schedulingClassEntry()) {
        report_.gpuSchedulingStatus = entry(process, schedulingHigh);
        report_.gpuSchedulingRaised = report_.gpuSchedulingStatus >= 0;
    } else report_.gpuSchedulingStatus = -1;
    report_.gpuThreadResult = renderer ? renderer->setGpuThreadPriority(7) : -1;
    report_.gpuThreadRaised = report_.gpuThreadResult >= 0;
}
void PresentationPriority::restore() noexcept {
    if (!raised_) return;
    raised_ = false; ++report_.restores;
    const auto process = GetCurrentProcess();
    if (report_.gpuThreadRaised && renderer_) (void)renderer_->setGpuThreadPriority(0);
    if (report_.gpuSchedulingRaised) if (const auto entry = schedulingClassEntry()) (void)entry(process, schedulingNormal);
    if (task_) { AvRevertMmThreadCharacteristics(task_); task_ = nullptr; }
    // Only undo our own change: a class set meanwhile by the user stays.
    if (report_.processRaised && GetPriorityClass(process) == ABOVE_NORMAL_PRIORITY_CLASS) SetPriorityClass(process, NORMAL_PRIORITY_CLASS);
    report_.processRaised = report_.threadRegistered = report_.gpuSchedulingRaised = report_.gpuThreadRaised = false;
    renderer_ = nullptr;
}
} // namespace endfield::app
