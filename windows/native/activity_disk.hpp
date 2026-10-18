#pragma once
#include "modules/activity_state.hpp"
#include <memory>
namespace endfield::native {
struct ActivityDiskCounter {std::string instance;std::uint64_t bytes{};bool valid{true};};
// Pure bounded pairing. _Total is excluded, not summed a second time. Empty,
// partial, duplicate or invalid instances are unavailable, never fake idle.
std::optional<modules::ActivityCounters>activityDiskCounters(std::span<const ActivityDiskCounter>reads,std::span<const ActivityDiskCounter>writes);
class WindowsActivityDisk final {
public:
    WindowsActivityDisk();~WindowsActivityDisk();
    WindowsActivityDisk(const WindowsActivityDisk&)=delete;WindowsActivityDisk&operator=(const WindowsActivityDisk&)=delete;
    // Worker-only serial call. Inert constructor, lazily retained PDH query,
    // synchronous collection only (no PDH Ex timing thread). No device/file
    // opens, IOCTLs, scans, counter enabling, privilege elevation or registry edits.
    // Windows PhysicalDisk provider topology is not a Mac I/O Registry claim.
    std::optional<modules::ActivityCounters>sample();
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
