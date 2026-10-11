#pragma once
// Scheduling priority while the HUD is shown (Windows only; the Mac HUD has
// no equivalent because WindowServer schedules its layers). A game in the
// foreground keeps the CPU cores and the GPU busy; the HUD's UI/render thread
// at normal priority then waits behind the game's threads and its small GPU
// batches queue behind the game's frames, which reads as lag. While the HUD
// is open (from the opening until it is concealed again):
//   - the process runs at ABOVE_NORMAL priority class (only when it was
//     NORMAL; a class the user chose is left alone, and restored only if
//     still ours);
//   - the UI/render thread joins the MMCSS "Games" task (AvSetMmThreadCharacteristics),
//     which the Multimedia Class Scheduler boosts for short periods while
//     still reserving CPU time for other work (SystemResponsiveness);
//   - the process's GPU scheduling priority class is HIGH
//     (D3DKMTSetProcessSchedulingPriorityClass) and the device's GPU thread
//     priority +7 (IDXGIDevice::SetGPUThreadPriority), so its few draws per
//     frame are scheduled ahead of a game's long frames.
// Each step is best effort: a step Windows refuses (e.g. without the
// "Increase scheduling priority" privilege) is recorded in report(), and the
// rest still apply. Everything is restored when the HUD is concealed, so a
// closed HUD never competes with the game.
#include <cstdint>

namespace endfield::native { class Renderer; }
namespace endfield::app {
struct PresentationPriorityReport {
    bool processRaised{}, threadRegistered{}, gpuSchedulingRaised{}, gpuThreadRaised{};
    std::uint32_t previousClass{};   // GetPriorityClass before raising
    long gpuSchedulingStatus{};      // NTSTATUS of D3DKMTSetProcessSchedulingPriorityClass (0 success)
    long gpuThreadResult{};          // HRESULT of SetGPUThreadPriority(7)
    std::uint64_t raises{}, restores{};
};
class PresentationPriority final {
public:
    PresentationPriority() = default;
    ~PresentationPriority();
    PresentationPriority(const PresentationPriority&) = delete;
    PresentationPriority& operator=(const PresentationPriority&) = delete;
    // UI thread only (the MMCSS registration belongs to the calling thread).
    void raise(native::Renderer*) noexcept;
    void restore() noexcept;
    bool raised() const noexcept { return raised_; }
    const PresentationPriorityReport& report() const noexcept { return report_; }
private:
    native::Renderer* renderer_{};
    void* task_{};
    std::uint32_t taskIndex_{};
    bool raised_{};
    PresentationPriorityReport report_;
};
} // namespace endfield::app
