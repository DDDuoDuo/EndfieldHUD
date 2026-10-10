#pragma once
// One EndfieldHUD per interactive session (Mac AppDelegate: a second copy
// activates the existing one and terminates). A named mutex in the Local\
// namespace is the writer lock for the persistent data root; a message-only
// activation window receives a second launch's bounded arguments through
// WM_COPYDATA after AllowSetForegroundWindow, on the owner's UI thread. The
// window works while the tray icon is hidden. No polling: the primary waits
// in its normal message loop; only a starting secondary waits, bounded, for a
// primary that is still creating its activation window.
#ifdef _WIN32
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::app {
struct SingleInstanceNames {
    std::wstring mutex;       // e.g. Local\EndfieldHUD.Instance.{GUID}
    std::wstring windowClass; // message-only activation window class
};
SingleInstanceNames productionSingleInstanceNames();

class SingleInstanceGuard final {
public:
    using Activation = std::function<void(std::vector<std::string>)>;
    explicit SingleInstanceGuard(SingleInstanceNames);
    ~SingleInstanceGuard();
    SingleInstanceGuard(const SingleInstanceGuard&) = delete;
    SingleInstanceGuard& operator=(const SingleInstanceGuard&) = delete;
    bool primary() const noexcept;
    // Primary only, owner UI thread: create the activation window now so a
    // concurrent second launch can deliver immediately. Activations arriving
    // before setHandler() are queued (bounded) and delivered in order.
    void listen();
    void setHandler(Activation);
    // Secondary only: deliver arguments to the primary. False when no primary
    // window appears within the bound or the primary rejects the payload.
    bool forward(std::span<const std::string_view> arguments, std::uint32_t timeoutMilliseconds = 5000) const;
    std::size_t received() const noexcept;
    static constexpr std::size_t maximumQueued = 8;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::app
#endif
