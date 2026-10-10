#include "app/single_instance.hpp"
#ifdef _WIN32
#include "core/application_arguments.hpp"
#include <cstdio>
#include <stdexcept>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace endfield::app {
namespace {
constexpr ULONG_PTR activationMagic = 0x45485544; // "EHUD"
constexpr UINT deliverMessage = WM_APP + 1;
constexpr DWORD payloadLimit = static_cast<DWORD>(core::forwardedArgumentsMagic.size() + core::applicationArgumentLimit * (core::applicationArgumentBytes + 1));
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
}

SingleInstanceNames productionSingleInstanceNames() {
    // Stable for every EndfieldHUD.exe build; Local\ scopes it to this session.
    return {L"Local\\EndfieldHUD.Instance.{6E8C4B52-1F0A-4C55-9E39-ED7F1A3C5B21}", L"EndfieldHUD.Activation.{6E8C4B52-1F0A-4C55-9E39-ED7F1A3C5B21}"};
}

struct SingleInstanceGuard::Impl {
    SingleInstanceNames names;
    HANDLE mutex{};
    bool primary{}, registered{}, posted{};
    HWND window{};
    Activation handler;
    std::vector<std::vector<std::string>> queued;
    std::size_t received{};

    void flush() {
        posted = false;
        if (!handler) return;
        auto pending = std::move(queued);
        queued.clear();
        for (auto& arguments : pending) {
            try { handler(std::move(arguments)); }
            catch (const std::exception& e) { std::fprintf(stderr, "EndfieldHUD activation failed: %s\n", e.what()); }
            catch (...) { std::fputs("EndfieldHUD activation failed\n", stderr); }
        }
    }
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) noexcept {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == WM_COPYDATA) {
            const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(l);
            if (!data || data->dwData != activationMagic || !data->lpData || data->cbData == 0 || data->cbData > payloadLimit) return FALSE;
            try {
                auto arguments = core::decodeForwardedArguments(std::string_view(static_cast<const char*>(data->lpData), data->cbData));
                if (self->queued.size() >= maximumQueued) return FALSE;
                self->queued.push_back(std::move(arguments));
                ++self->received;
                // Release the waiting second launch first; activate later in
                // this thread's ordinary message loop.
                if (!self->posted && self->handler) self->posted = PostMessageW(hwnd, deliverMessage, 0, 0) != FALSE;
                return TRUE;
            } catch (...) { return FALSE; }
        }
        if (self && message == deliverMessage) { self->flush(); return 0; }
        if (self && message == WM_NCDESTROY) { SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); self->window = nullptr; }
        return DefWindowProcW(hwnd, message, w, l);
    }
};

SingleInstanceGuard::SingleInstanceGuard(SingleInstanceNames names) : impl_(std::make_unique<Impl>()) {
    if (names.mutex.empty() || names.windowClass.empty() || names.windowClass.size() > 200) fail("Single-instance names are required");
    impl_->names = std::move(names);
    impl_->mutex = CreateMutexW(nullptr, FALSE, impl_->names.mutex.c_str());
    const auto error = GetLastError();
    if (!impl_->mutex && error != ERROR_ACCESS_DENIED) fail("Single-instance guard could not be created");
    impl_->primary = impl_->mutex && error != ERROR_ALREADY_EXISTS;
}
SingleInstanceGuard::~SingleInstanceGuard() {
    if (impl_->window) DestroyWindow(impl_->window);
    if (impl_->registered) UnregisterClassW(impl_->names.windowClass.c_str(), GetModuleHandleW(nullptr));
    if (impl_->mutex) CloseHandle(impl_->mutex);
}
bool SingleInstanceGuard::primary() const noexcept { return impl_->primary; }
std::size_t SingleInstanceGuard::received() const noexcept { return impl_->received; }
void SingleInstanceGuard::listen() {
    auto& p = *impl_;
    if (!p.primary) throw std::logic_error("Only the primary instance listens for activation");
    if (p.window) return;
    WNDCLASSEXW type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = Impl::procedure;
    type.hInstance = GetModuleHandleW(nullptr);
    type.lpszClassName = p.names.windowClass.c_str();
    if (!RegisterClassExW(&type)) fail("Activation window class could not be registered");
    p.registered = true;
    p.window = CreateWindowExW(0, p.names.windowClass.c_str(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), &p);
    if (!p.window) fail("Activation window could not be created");
}
void SingleInstanceGuard::setHandler(Activation handler) {
    impl_->handler = std::move(handler);
    impl_->flush();
}
bool SingleInstanceGuard::forward(std::span<const std::string_view> arguments, std::uint32_t timeout) const {
    if (impl_->primary) throw std::logic_error("The primary instance does not forward activation");
    const auto payload = core::encodeForwardedArguments(arguments);
    const auto deadline = GetTickCount64() + timeout;
    HWND target{};
    // Bounded wait only in this short-lived second launch: the primary
    // creates its window before loading the HUD.
    while (!(target = FindWindowExW(HWND_MESSAGE, nullptr, impl_->names.windowClass.c_str(), nullptr))) {
        if (GetTickCount64() >= deadline) return false;
        Sleep(50);
    }
    DWORD process{};
    GetWindowThreadProcessId(target, &process);
    if (process && process != GetCurrentProcessId()) AllowSetForegroundWindow(process);
    COPYDATASTRUCT data{activationMagic, static_cast<DWORD>(payload.size()), const_cast<char*>(payload.data())};
    DWORD_PTR result{};
    const auto now = GetTickCount64();
    const UINT remaining = static_cast<UINT>(deadline > now ? deadline - now : 1);
    if (!SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_BLOCK | SMTO_ABORTIFHUNG, remaining, &result)) return false;
    return result == TRUE;
}
} // namespace endfield::app
#endif
