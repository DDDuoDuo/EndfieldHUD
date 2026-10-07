#pragma once
#include <windows.h>
#include "monitor_policy.h"
#include <vector>

namespace endfield::platform {
struct NativeMonitor {
    HMONITOR handle{};
    MonitorDescriptor descriptor;
    bool persistent_identity{};
};
// Event-driven inventory only. Device identities stay in memory; diagnostic
// reports contain geometry/DPI and identity availability rather than serials.
std::vector<NativeMonitor> collect_monitors();
}
