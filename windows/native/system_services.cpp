#include "system_services.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace endfield::native {
BatterySnapshot decode_power_status(const PowerStatus& raw) noexcept {
    BatterySnapshot result; result.available = true;
    if (raw.ac_line_status <= 1) result.ac_connected = raw.ac_line_status == 1;
    if (raw.battery_flags != 255) {
        result.present = (raw.battery_flags & 128) == 0;
        // Match the source: explicit offline AC wins over a contradictory flag.
        result.charging = *result.present && result.ac_connected != false && (raw.battery_flags & 8) != 0;
        if (result.ac_connected == std::nullopt && result.charging == true) result.ac_connected = true;
    }
    if (result.present != false) {
        if (raw.battery_percent <= 100) result.percent = raw.battery_percent;
        if (raw.remaining_seconds != 0xffffffffu) result.remaining_seconds = raw.remaining_seconds;
        if (raw.full_seconds != 0xffffffffu) result.full_seconds = raw.full_seconds;
    }
    if (result.present.has_value() && result.ac_connected.has_value() && result.charging.has_value() && result.percent.has_value())
        result.fully_charged = *result.present && *result.ac_connected && !*result.charging && *result.percent == 100;
    return result;
}

bool NotificationBatch::request(unsigned events) noexcept {
    if (!events || !active()) return false;
    const bool post = pending_.fetch_or(events, std::memory_order_acq_rel) == 0;
    if (!active()) { pending_.store(0, std::memory_order_release); return false; }
    return post;
}
unsigned NotificationBatch::drain() noexcept {
    const auto events = pending_.exchange(0, std::memory_order_acq_rel);
    return active() ? events : 0;
}
void NotificationBatch::cancel() noexcept {
    active_.store(false, std::memory_order_release); pending_.store(0, std::memory_order_release);
}
bool ClipboardSequence::should_capture(std::uint32_t sequence) const noexcept {
    return sequence && sequence != last_ && sequence != own_;
}
bool excludes_clipboard(const ClipboardExclusion& policy) noexcept {
    return policy.concealed_marker || policy.malformed_history_flag || policy.include_in_history == 0u;
}

namespace {
std::uint16_t little16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return std::uint16_t(bytes[offset]) | (std::uint16_t(bytes[offset + 1]) << 8);
}
std::uint32_t little32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return std::uint32_t(bytes[offset]) | (std::uint32_t(bytes[offset + 1]) << 8) |
        (std::uint32_t(bytes[offset + 2]) << 16) | (std::uint32_t(bytes[offset + 3]) << 24);
}
std::uint32_t big32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return (std::uint32_t(bytes[offset]) << 24) | (std::uint32_t(bytes[offset + 1]) << 16) |
        (std::uint32_t(bytes[offset + 2]) << 8) | std::uint32_t(bytes[offset + 3]);
}
bool absolute_file(std::u16string_view value) noexcept {
    for (const auto unit : value) if (unit < 0x20 || unit == 0x7f) return false;
    if (value.size() >= 3 && ((value[0] >= u'A' && value[0] <= u'Z') ||
        (value[0] >= u'a' && value[0] <= u'z')) && value[1] == u':' &&
        (value[2] == u'\\' || value[2] == u'/')) return true;
    if (value.size() < 5 || value[0] != u'\\' || value[1] != u'\\') return false;
    if (value[2] == u'?' || value[2] == u'.') return false;
    const auto slash = value.find(u'\\', 2);
    if (slash == std::u16string_view::npos || slash == 2 || slash + 1 == value.size()) return false;
    const auto file = value.find(u'\\', slash + 1);
    return file != std::u16string_view::npos && file > slash + 1 && file + 1 < value.size();
}
}

ClipboardHistory::ClipboardHistory(std::size_t capacity) : capacity_(std::clamp(capacity, std::size_t{1}, maximum_capacity)) {}
const ClipboardItem* ClipboardHistory::find(std::uint64_t id) const noexcept {
    const auto it = std::find_if(items_.begin(), items_.end(), [id](const auto& item) { return item.id == id; });
    return it == items_.end() ? nullptr : &*it;
}
bool ClipboardHistory::valid_utf16(std::u16string_view value) noexcept {
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto unit = value[index];
        if (!unit) return false;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (++index == value.size() || value[index] < 0xdc00 || value[index] > 0xdfff) return false;
        } else if (unit >= 0xdc00 && unit <= 0xdfff) return false;
    }
    return true;
}
std::size_t ClipboardHistory::utf8_bytes(std::u16string_view value) noexcept {
    std::size_t result{};
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto unit = value[i];
        if (!unit || (unit >= 0xdc00 && unit <= 0xdfff)) return maximum_retained_bytes + 1;
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (++i == value.size() || value[i] < 0xdc00 || value[i] > 0xdfff) return maximum_retained_bytes + 1;
            result += 4;
        } else result += unit < 0x80 ? 1 : unit < 0x800 ? 2 : 3;
        if (result > maximum_text_bytes) return maximum_retained_bytes + 1;
    }
    return result;
}
bool ClipboardHistory::url_text(std::u16string_view value) noexcept {
    // Bounded heuristic, no URL parser, network request or shell activation.
    if (value.empty() || value.size() > 2048 || !valid_utf16(value)) return false;
    for (const auto unit : value) if (unit <= 0x20 || unit == 0x7f) return false;
    const auto colon = value.find(u':');
    if (colon == std::u16string_view::npos || colon == 0) return false;
    const auto matches = [&](std::u16string_view scheme) {
        if (colon != scheme.size()) return false;
        for (std::size_t i=0; i<colon; ++i) {
            auto unit=value[i]; if (unit >= u'A' && unit <= u'Z') unit += u'a'-u'A';
            if (unit != scheme[i]) return false;
        }
        return true;
    };
    if (matches(u"http") || matches(u"https") || matches(u"ftp")) {
        const auto start = colon + 3;
        return colon + 3 < value.size() && value.substr(colon + 1, 2) == u"//" &&
            value[start] != u'/' && value[start] != u'?' && value[start] != u'#';
    }
    return (matches(u"mailto") || matches(u"tel") || matches(u"sms") || matches(u"file")) && colon+1 < value.size();
}
bool ClipboardHistory::valid_image(ClipboardImageFormat format, std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() > maximum_image_bytes || bytes.size() < 12) return false;
    if (format == ClipboardImageFormat::png) {
        constexpr std::array<std::uint8_t, 8> signature{137,80,78,71,13,10,26,10};
        if (!std::equal(signature.begin(), signature.end(), bytes.begin()) || bytes.size() < 45) return false;
        bool header = false, data = false;
        for (std::size_t offset = 8; offset <= bytes.size() - 12;) {
            const auto size = big32(bytes, offset);
            if (size > bytes.size() - offset - 12) return false;
            const auto type = big32(bytes, offset + 4);
            if (!header) {
                if (type != 0x49484452u || size != 13) return false; // IHDR
                const auto width = big32(bytes, offset + 8), height = big32(bytes, offset + 12);
                if (!width || !height || width > 16384 || height > 16384 ||
                    std::uint64_t(width) * height > maximum_image_bytes / 4) return false;
                const auto depth = bytes[offset + 16], color = bytes[offset + 17];
                const bool depth_ok = color == 0 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16) :
                    color == 3 ? (depth == 1 || depth == 2 || depth == 4 || depth == 8) :
                    (color == 2 || color == 4 || color == 6) && (depth == 8 || depth == 16);
                if (!depth_ok || bytes[offset + 18] || bytes[offset + 19] || bytes[offset + 20] > 1) return false;
                header = true;
            } else if (type == 0x49484452u) return false;
            if (type == 0x49444154u) data = true; // IDAT; decoding remains the image consumer's responsibility.
            if (type == 0x49454e44u) return data && size == 0 && offset + 12 == bytes.size();
            offset += std::size_t(size) + 12;
        }
        return false;
    }
    if (format != ClipboardImageFormat::dib && format != ClipboardImageFormat::dib_v5) return false;
    const auto header = little32(bytes, 0);
    if (format == ClipboardImageFormat::dib_v5 && header != 124) return false;
    if (header != 12 && header != 40 && header != 52 && header != 56 && header != 108 && header != 124) return false;
    if (bytes.size() < header) return false;
    std::uint32_t width{}, height{}, compression{}, colors{}, image_size{};
    std::uint16_t planes{}, bits{};
    if (header == 12) {
        width = little16(bytes, 4); height = little16(bytes, 6);
        planes = little16(bytes, 8); bits = little16(bytes, 10);
    } else {
        const auto signed_width = static_cast<std::int32_t>(little32(bytes, 4));
        const auto signed_height = static_cast<std::int32_t>(little32(bytes, 8));
        if (signed_width <= 0 || signed_height == 0 || signed_height == std::numeric_limits<std::int32_t>::min()) return false;
        width = static_cast<std::uint32_t>(signed_width);
        height = static_cast<std::uint32_t>(signed_height < 0 ? -signed_height : signed_height);
        planes = little16(bytes, 12); bits = little16(bytes, 14);
        compression = little32(bytes, 16); image_size = little32(bytes, 20); colors = little32(bytes, 32);
    }
    if (!width || !height || width > 16384 || height > 16384 || planes != 1 ||
        std::uint64_t(width) * height > maximum_image_bytes / 4 ||
        (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32)) return false;
    // Packed, uncompressed or bitfield DIBs only. RLE/JPEG/PNG-in-DIB need a
    // separate bounded codec before becoming acceptable clipboard previews.
    if (compression != 0 && compression != 3 && compression != 6) return false;
    if (compression && bits != 16 && bits != 32) return false;
    if (colors > 256 || (bits <= 8 && colors > (1u << bits))) return false;
    const auto palette = colors ? colors : bits <= 8 ? (1u << bits) : 0;
    const std::uint64_t masks = header == 40 ? (compression == 3 ? 12u : compression == 6 ? 16u : 0u) : 0u;
    const std::uint64_t offset = header + masks + std::uint64_t(palette) * (header == 12 ? 3u : 4u);
    const std::uint64_t required = ((std::uint64_t(width) * bits + 31) / 32) * 4 * height;
    if (offset > bytes.size() || required > bytes.size() - offset) return false;
    if (image_size && (image_size < required || image_size > bytes.size() - offset)) return false;
    if (header == 124) {
        const auto profile_offset = little32(bytes, 112), profile_size = little32(bytes, 116);
        if (profile_size && (profile_offset < header || profile_offset > bytes.size() || profile_size > bytes.size() - profile_offset)) return false;
    }
    return true;
}
std::size_t ClipboardHistory::payload_bytes(const ClipboardPayload& value) noexcept {
    if (value.kind == ClipboardKind::image) return value.image.size();
    // Charge actual retained UTF-16 bytes as well as the source's UTF-8 limit.
    if (value.kind == ClipboardKind::text || value.kind == ClipboardKind::url)
        return value.text.size() > maximum_text_bytes ? maximum_retained_bytes + 1 : value.text.size() * sizeof(char16_t);
    std::size_t total{};
    for (const auto& path : value.files) {
        if (path.size() > maximum_text_bytes || total > maximum_text_bytes * 2 - path.size() * 2) return maximum_retained_bytes + 1;
        total += path.size() * 2;
    }
    return total;
}
bool ClipboardHistory::valid(const ClipboardPayload& value) noexcept {
    if (value.kind == ClipboardKind::text || value.kind == ClipboardKind::url)
        return value.files.empty() && value.image.empty() && !value.text.empty() &&
            value.text.size() <= maximum_text_bytes && utf8_bytes(value.text) <= maximum_text_bytes &&
            (value.kind != ClipboardKind::url || url_text(value.text));
    if (value.kind == ClipboardKind::files) {
        if (!value.text.empty() || !value.image.empty() || value.files.empty() || value.files.size() > maximum_files ||
            payload_bytes(value) > maximum_text_bytes * 2) return false;
        std::size_t source_bytes{};
        for (const auto& path : value.files) {
            if (path.empty() || path.size() > 32767 || !valid_utf16(path) || !absolute_file(path)) return false;
            const auto bytes = utf8_bytes(path);
            if (bytes > maximum_text_bytes || source_bytes > maximum_text_bytes - bytes) return false;
            source_bytes += bytes;
        }
        return true;
    }
    return value.kind == ClipboardKind::image && value.text.empty() && value.files.empty() && valid_image(value.image_format, value.image);
}
ClipboardInsertResult ClipboardHistory::ingest(ClipboardPayload payload, bool excluded) {
    if (excluded) return ClipboardInsertResult::excluded;
    if (!valid(payload)) return ClipboardInsertResult::invalid;
    const auto existing = std::find_if(items_.begin(), items_.end(), [&](const auto& item) { return item.payload == payload; });
    if (existing != items_.end()) {
        if (existing == items_.begin()) return ClipboardInsertResult::duplicate;
        std::rotate(items_.begin(), existing, existing + 1);
        return ClipboardInsertResult::moved;
    }
    if (!next_id_ || next_id_ == std::numeric_limits<std::uint64_t>::max()) return ClipboardInsertResult::full;
    const auto bytes = payload_bytes(payload);
    std::size_t retained = retained_bytes_, count = items_.size();
    std::vector<std::size_t> evicted;
    for (std::size_t index = items_.size(); (count >= capacity_ || retained > maximum_retained_bytes - bytes) && index;) {
        --index;
        if (!items_[index].pinned) { evicted.push_back(index); retained -= payload_bytes(items_[index].payload); --count; }
    }
    if (count >= capacity_ || retained > maximum_retained_bytes - bytes) return ClipboardInsertResult::full;
    items_.reserve(items_.size() + 1); // Allocation failure leaves old history intact.
    for (const auto index : evicted) items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    items_.insert(items_.begin(), ClipboardItem{next_id_++, false, std::move(payload)});
    retained_bytes_ = retained + bytes;
    return ClipboardInsertResult::inserted;
}
bool ClipboardHistory::erase(std::uint64_t id) {
    const auto it = std::find_if(items_.begin(), items_.end(), [id](const auto& item) { return item.id == id; });
    if (it == items_.end()) return false;
    retained_bytes_ -= payload_bytes(it->payload); items_.erase(it); return true;
}
bool ClipboardHistory::pin(std::uint64_t id, bool pinned) {
    const auto it = std::find_if(items_.begin(), items_.end(), [id](const auto& item) { return item.id == id; });
    if (it == items_.end() || it->pinned == pinned) return false;
    it->pinned = pinned; return true;
}
bool ClipboardHistory::set_capacity(std::size_t capacity) {
    if (!capacity || capacity > maximum_capacity || capacity == capacity_ ||
        std::size_t(std::count_if(items_.begin(), items_.end(), [](const auto& item) { return item.pinned; })) > capacity) return false;
    for (std::size_t index = items_.size(); items_.size() > capacity && index;) {
        --index;
        if (!items_[index].pinned) { retained_bytes_ -= payload_bytes(items_[index].payload); items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index)); }
    }
    capacity_ = capacity; return true;
}
void ClipboardHistory::clear(bool keep_pinned) {
    if (!keep_pinned) { items_.clear(); retained_bytes_ = 0; return; }
    for (std::size_t index = items_.size(); index;) {
        --index;
        if (!items_[index].pinned) { retained_bytes_ -= payload_bytes(items_[index].payload); items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index)); }
    }
}
} // namespace endfield::native

#ifdef _WIN32
#include <atomic>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <wrl/client.h>

namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
constexpr unsigned topology_event = 1, volume_event = 2, battery_event = 4, clipboard_event = 8, clipboard_retry_event = 16;
std::atomic<UINT_PTR> next_route{1};
HRESULT last_error() noexcept {
    const auto code = GetLastError(); return code ? HRESULT_FROM_WIN32(code) : E_FAIL;
}
struct NotificationRoute {
    HWND owner{}; UINT message{}; UINT_PTR token{};
    NotificationBatch events;
    void post(unsigned event) noexcept {
        if (events.request(event) && !PostMessageW(owner, message, static_cast<WPARAM>(token), 0)) (void)events.drain();
    }
};
class DeviceNotifications final : public IMMNotificationClient {
public:
    explicit DeviceNotifications(std::shared_ptr<NotificationRoute> route) : route_(std::move(route)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
            if (id == __uuidof(IUnknown) || id == __uuidof(IMMNotificationClient)) { *result = static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = --references_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { route_->post(topology_event); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { route_->post(topology_event); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { route_->post(topology_event); return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) route_->post(topology_event); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY key) override {
        if (IsEqualPropertyKey(key, PKEY_Device_FriendlyName)) route_->post(topology_event);
        return S_OK;
    }
private:
    std::atomic<ULONG> references_{1}; std::shared_ptr<NotificationRoute> route_;
};
class VolumeNotifications final : public IAudioEndpointVolumeCallback {
public:
    explicit VolumeNotifications(std::shared_ptr<NotificationRoute> route) : route_(std::move(route)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** result) override {
        if (!result) return E_POINTER; *result = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IAudioEndpointVolumeCallback)) { *result = static_cast<IAudioEndpointVolumeCallback*>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = --references_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override { route_->post(volume_event); return S_OK; }
private:
    std::atomic<ULONG> references_{1}; std::shared_ptr<NotificationRoute> route_;
};
struct ClipboardLease {
    bool opened{};
    explicit ClipboardLease(HWND owner) : opened(OpenClipboard(owner) != FALSE) {}
    ~ClipboardLease() { if (opened) CloseClipboard(); }
};
struct ClipboardOwnerMarker {
    DWORD process{};
    UINT_PTR generation{};
};
struct LockedGlobal {
    HGLOBAL memory{}; const void* data{}; std::size_t size{};
    explicit LockedGlobal(HANDLE handle) : memory(static_cast<HGLOBAL>(handle)) {
        if (memory) { size = GlobalSize(memory); if (size) data = GlobalLock(memory); }
    }
    ~LockedGlobal() { if (data) GlobalUnlock(memory); }
};
struct OwnedGlobal {
    HGLOBAL memory{};
    explicit OwnedGlobal(std::size_t size) : memory(GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size)) {}
    ~OwnedGlobal() { if (memory) GlobalFree(memory); }
    bool fill(const void* bytes, std::size_t size) {
        if (!memory) return false;
        void* target = GlobalLock(memory); if (!target) return false;
        std::memcpy(target, bytes, size); GlobalUnlock(memory); return true;
    }
    bool publish(UINT format) {
        if (!memory || !SetClipboardData(format, memory)) return false;
        memory = nullptr; return true;
    }
};
std::wstring endpoint_id(IMMDevice* device) {
    LPWSTR value{};
    if (!device || FAILED(device->GetId(&value)) || !value) return {};
    std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> holder(value, &CoTaskMemFree);
    return value;
}
std::wstring endpoint_name(IMMDevice* device, const std::wstring& fallback) {
    ComPtr<IPropertyStore> properties;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &properties))) return fallback;
    PROPVARIANT value; PropVariantInit(&value);
    const HRESULT status = properties->GetValue(PKEY_Device_FriendlyName, &value);
    std::wstring result;
    try { if (SUCCEEDED(status) && value.vt == VT_LPWSTR && value.pwszVal) result = value.pwszVal; }
    catch (...) { PropVariantClear(&value); throw; }
    PropVariantClear(&value); return result.empty() ? fallback : result;
}
std::u16string utf16(std::wstring_view value) {
    static_assert(sizeof(wchar_t) == sizeof(char16_t));
    return {reinterpret_cast<const char16_t*>(value.data()), value.size()};
}
}

class SystemServices::Impl final {
public:
    HWND owner{}; DWORD thread{}; UINT message{}; Changed changed;
    BatterySnapshot battery;
    ClipboardHistory clipboard;
    HRESULT clipboard_error{E_PENDING};
    AudioSnapshot audio;
    bool clipboard_registered{}, co_initialized{}, com_attempted{}, audio_active{}, device_registered{}, volume_registered{};
    HRESULT com_status{E_PENDING};
    HPOWERNOTIFY ac_notification{}, battery_notification{};
    std::shared_ptr<NotificationRoute> route, audio_route;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<DeviceNotifications> device_notifications;
    ComPtr<IAudioEndpointVolume> endpoint_volume;
    ComPtr<VolumeNotifications> volume_notifications;
    std::wstring selected_endpoint;
    ClipboardSequence clipboard_sequence;
    UINT owner_format{}, history_format{}, png_format{}, drop_effect_format{};
    std::vector<UINT> excluded_formats;

    bool owner_thread() const noexcept { return owner && thread == GetCurrentThreadId(); }
    void notify(ServiceChange change) {
        auto callback = changed;
        // Never unwind a Win32 window procedure through application code.
        if (callback) try { callback(change); } catch (...) {}
    }
    void stop_volume() noexcept {
        if (volume_registered && endpoint_volume && volume_notifications) endpoint_volume->UnregisterControlChangeNotify(volume_notifications.Get());
        volume_registered = false; endpoint_volume.Reset(); volume_notifications.Reset();
    }
    void stop_audio() noexcept {
        if (audio_route) audio_route->events.cancel();
        stop_volume();
        if (device_registered && enumerator && device_notifications) enumerator->UnregisterEndpointNotificationCallback(device_notifications.Get());
        device_registered = false; device_notifications.Reset(); enumerator.Reset(); audio_route.reset();
    }
    void stop() noexcept {
        if (route) route->events.cancel();
        changed = {};
        stop_audio();
        if (clipboard_registered && owner) RemoveClipboardFormatListener(owner);
        clipboard_registered = false;
        if (ac_notification) UnregisterPowerSettingNotification(ac_notification);
        if (battery_notification) UnregisterPowerSettingNotification(battery_notification);
        ac_notification = nullptr; battery_notification = nullptr;
        if (co_initialized) CoUninitialize();
        co_initialized = com_attempted = audio_active = false; com_status = E_PENDING;
        route.reset(); owner = nullptr; thread = 0; message = 0;
        clipboard.clear(false); battery = {}; audio = {}; selected_endpoint.clear(); excluded_formats.clear();
        clipboard_sequence.begin(0); clipboard_error = E_PENDING;
    }
    void refresh_battery() {
        if (!owner_thread()) return;
        SYSTEM_POWER_STATUS native{}; BatterySnapshot next;
        if (GetSystemPowerStatus(&native)) next = decode_power_status({native.ACLineStatus, native.BatteryFlag,
            native.BatteryLifePercent, native.BatteryLifeTime, native.BatteryFullLifeTime});
        if (next != battery) { battery = next; notify(ServiceChange::battery); }
    }
    HRESULT read_volume(AudioSnapshot& snapshot) {
        snapshot.available = false; snapshot.volume.reset(); snapshot.muted.reset();
        if (!endpoint_volume) return snapshot.error = static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_FOUND));
        float scalar{}; BOOL muted{};
        HRESULT status = endpoint_volume->GetMasterVolumeLevelScalar(&scalar);
        if (SUCCEEDED(status)) status = endpoint_volume->GetMute(&muted);
        if (SUCCEEDED(status) && (!std::isfinite(scalar) || scalar < 0 || scalar > 1)) status = E_UNEXPECTED;
        snapshot.error = static_cast<std::int32_t>(status);
        if (SUCCEEDED(status)) { snapshot.available = true; snapshot.volume = scalar; snapshot.muted = muted != FALSE; }
        return status;
    }
    HRESULT refresh_volume() {
        if (!owner_thread()) return RPC_E_WRONG_THREAD;
        if (!audio_active) return E_PENDING;
        auto next = audio; const HRESULT status = read_volume(next);
        if (next != audio) { audio = std::move(next); notify(ServiceChange::audio); }
        return status;
    }
    HRESULT refresh_audio() {
        if (!owner_thread()) return RPC_E_WRONG_THREAD;
        if (!audio_active) return E_PENDING;
        AudioSnapshot next; next.paused = false;
        HRESULT status = S_OK;
        if (FAILED(com_status) && com_status != RPC_E_CHANGED_MODE) status = com_status;
        if (SUCCEEDED(status) && !enumerator) status = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(status) && !device_registered) {
            device_notifications.Attach(new DeviceNotifications(audio_route));
            status = enumerator->RegisterEndpointNotificationCallback(device_notifications.Get());
            device_registered = SUCCEEDED(status);
        }
        stop_volume();
        ComPtr<IMMDeviceCollection> devices;
        if (SUCCEEDED(status)) status = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
        ComPtr<IMMDevice> default_device;
        if (enumerator && SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &default_device))) next.default_device_id = endpoint_id(default_device.Get());
        UINT count{};
        if (SUCCEEDED(status)) status = devices->GetCount(&count);
        if (SUCCEEDED(status) && count > 512) status = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        for (UINT index = 0; SUCCEEDED(status) && index < count; ++index) {
            ComPtr<IMMDevice> device;
            status = devices->Item(index, &device); if (FAILED(status)) break;
            auto id = endpoint_id(device.Get()); if (id.empty()) { status = E_UNEXPECTED; break; }
            next.devices.push_back({id, endpoint_name(device.Get(), id), id == next.default_device_id});
        }
        next.controlled_device_id = selected_endpoint.empty() ? next.default_device_id : selected_endpoint;
        if (SUCCEEDED(status)) {
            const bool present = std::any_of(next.devices.begin(), next.devices.end(), [&](const auto& device) { return device.id == next.controlled_device_id; });
            if (!present) status = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        ComPtr<IMMDevice> controlled;
        if (SUCCEEDED(status)) status = enumerator->GetDevice(next.controlled_device_id.c_str(), &controlled);
        if (SUCCEEDED(status)) status = controlled->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, nullptr, reinterpret_cast<void**>(endpoint_volume.GetAddressOf()));
        if (SUCCEEDED(status)) {
            volume_notifications.Attach(new VolumeNotifications(audio_route));
            status = endpoint_volume->RegisterControlChangeNotify(volume_notifications.Get());
            volume_registered = SUCCEEDED(status);
        }
        if (SUCCEEDED(status)) status = read_volume(next);
        if (FAILED(status)) { stop_volume(); next.available = false; next.volume.reset(); next.muted.reset(); next.error = static_cast<std::int32_t>(status); }
        if (next != audio) { audio = std::move(next); notify(ServiceChange::audio); }
        return status;
    }
    HRESULT set_audio_active(bool active) {
        if (!owner_thread()) return RPC_E_WRONG_THREAD;
        if (audio_active == active) return S_FALSE;
        if (!active) {
            audio_active = false; stop_audio();
            auto next = audio; next.paused = true; next.available = false;
            if (next != audio) { audio = std::move(next); notify(ServiceChange::audio); }
            return S_OK;
        }
        audio_route = std::make_shared<NotificationRoute>();
        audio_route->owner = owner; audio_route->message = message;
        audio_route->token = next_route.fetch_add(1, std::memory_order_relaxed);
        if (!com_attempted) {
            com_status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            co_initialized = SUCCEEDED(com_status); com_attempted = true;
        }
        audio_active = true;
        return refresh_audio();
    }
    bool excluded_clipboard() const {
        for (const auto format : excluded_formats) if (format && IsClipboardFormatAvailable(format)) return true;
        if (history_format && IsClipboardFormatAvailable(history_format)) {
            LockedGlobal flag(GetClipboardData(history_format));
            if (!flag.data || flag.size < sizeof(DWORD)) return true;
            DWORD include{}; std::memcpy(&include, flag.data, sizeof(include));
            if (!include) return true;
        }
        if (owner_format && IsClipboardFormatAvailable(owner_format)) {
            LockedGlobal marker(GetClipboardData(owner_format));
            ClipboardOwnerMarker source{};
            if (marker.data && marker.size >= sizeof(source)) {
                std::memcpy(&source, marker.data, sizeof(source));
                if (route && source.process == GetCurrentProcessId() && source.generation == route->token) return true;
            }
        }
        return false;
    }
    HRESULT read_clipboard(ClipboardPayload& payload, bool& found) const {
        found = false;
        if (IsClipboardFormatAvailable(CF_HDROP)) {
            const auto handle = static_cast<HDROP>(GetClipboardData(CF_HDROP));
            if (!handle) return last_error();
            const UINT count = DragQueryFileW(handle, 0xffffffffu, nullptr, 0);
            if (!count || count > ClipboardHistory::maximum_files) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            payload.kind = ClipboardKind::files; std::size_t source_bytes{};
            for (UINT index = 0; index < count; ++index) {
                const UINT length = DragQueryFileW(handle, index, nullptr, 0);
                if (!length || length > 32767) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
                std::wstring file(std::size_t(length) + 1, L'\0');
                if (DragQueryFileW(handle, index, file.data(), length + 1) != length) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                file.resize(length); auto path = utf16(file);
                const auto bytes = ClipboardHistory::utf8_bytes(path);
                if (bytes > ClipboardHistory::maximum_text_bytes || source_bytes > ClipboardHistory::maximum_text_bytes - bytes) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
                source_bytes += bytes; payload.files.push_back(std::move(path));
            }
            found = true; return S_OK;
        }
        const std::array<std::pair<UINT, ClipboardImageFormat>, 3> image_formats{{
            {CF_DIBV5, ClipboardImageFormat::dib_v5}, {CF_DIB, ClipboardImageFormat::dib}, {png_format, ClipboardImageFormat::png}}};
        bool rejected_image = false;
        for (const auto [format, kind] : image_formats) {
            if (!format || !IsClipboardFormatAvailable(format)) continue;
            LockedGlobal image(GetClipboardData(format));
            if (!image.data || image.size > ClipboardHistory::maximum_image_bytes) { rejected_image = true; continue; }
            const auto bytes = std::span(static_cast<const std::uint8_t*>(image.data), image.size);
            if (!ClipboardHistory::valid_image(kind, bytes)) { rejected_image = true; continue; }
            payload.kind = ClipboardKind::image; payload.image_format = kind;
            payload.image.assign(bytes.begin(), bytes.end()); found = true; return S_OK;
        }
        if (rejected_image) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            LockedGlobal text(GetClipboardData(CF_UNICODETEXT));
            if (!text.data || text.size < 2 || text.size > ClipboardHistory::maximum_text_bytes * 2 + 2 || text.size % 2) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            const auto units = std::span(static_cast<const char16_t*>(text.data), text.size / 2);
            const auto end = std::find(units.begin(), units.end(), u'\0');
            if (end == units.end()) return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            payload.text.assign(units.begin(), end);
            payload.kind = ClipboardHistory::url_text(payload.text) ? ClipboardKind::url : ClipboardKind::text;
            found = true;
        }
        return S_OK;
    }
    HRESULT refresh_clipboard(bool allow_retry = true) {
        if (!owner_thread()) return RPC_E_WRONG_THREAD;
        if (!clipboard_registered) return clipboard_error;
        const DWORD sequence = GetClipboardSequenceNumber();
        if (!clipboard_sequence.should_capture(sequence)) return S_FALSE;
        ClipboardPayload payload; bool found = false, excluded = false;
        HRESULT status = S_OK;
        {
            ClipboardLease lease(owner);
            if (!lease.opened) {
                const auto unavailable = last_error();
                if (allow_retry && route) route->post(clipboard_retry_event);
                return set_clipboard_error(unavailable);
            }
            if (!clipboard_sequence.should_capture(GetClipboardSequenceNumber())) return S_FALSE;
            excluded = excluded_clipboard();
            if (!excluded) status = read_clipboard(payload, found);
            // Acknowledge while the clipboard is still locked, before notifying
            // observers. A subsequent external write must retain its own event.
            clipboard_sequence.acknowledge(GetClipboardSequenceNumber());
        }
        if (FAILED(status)) return set_clipboard_error(status);
        if (excluded || !found) return set_clipboard_error(S_OK);
        const auto inserted = clipboard.ingest(std::move(payload));
        if (inserted == ClipboardInsertResult::invalid) return set_clipboard_error(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        if (inserted == ClipboardInsertResult::full) return set_clipboard_error(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY));
        const bool error_changed = clipboard_error != S_OK; clipboard_error = S_OK;
        if (inserted == ClipboardInsertResult::inserted || inserted == ClipboardInsertResult::moved || error_changed) notify(ServiceChange::clipboard);
        return inserted == ClipboardInsertResult::inserted || inserted == ClipboardInsertResult::moved ? S_OK : S_FALSE;
    }
    HRESULT set_clipboard_error(HRESULT status) {
        if (clipboard_error != status) { clipboard_error = status; notify(ServiceChange::clipboard); }
        return status;
    }
    HRESULT copy_clipboard_item(std::uint64_t id) {
        if (!owner_thread()) return RPC_E_WRONG_THREAD;
        const auto* item = clipboard.find(id);
        if (!item) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        const auto& payload = item->payload;
        UINT format{}; std::vector<std::uint8_t> bytes;
        if (payload.kind == ClipboardKind::text || payload.kind == ClipboardKind::url) {
            format = CF_UNICODETEXT; bytes.resize((payload.text.size() + 1) * sizeof(char16_t));
            std::memcpy(bytes.data(), payload.text.data(), payload.text.size() * sizeof(char16_t));
        } else if (payload.kind == ClipboardKind::files) {
            format = CF_HDROP; std::size_t units = 1;
            for (const auto& path : payload.files) units += path.size() + 1;
            bytes.resize(sizeof(DROPFILES) + units * sizeof(char16_t));
            DROPFILES drop{}; drop.pFiles = sizeof(DROPFILES); drop.fWide = TRUE;
            std::memcpy(bytes.data(), &drop, sizeof(drop)); std::size_t offset = sizeof(drop);
            for (const auto& path : payload.files) {
                std::memcpy(bytes.data() + offset, path.data(), path.size() * sizeof(char16_t)); offset += (path.size() + 1) * sizeof(char16_t);
            }
        } else {
            format = payload.image_format == ClipboardImageFormat::dib_v5 ? CF_DIBV5 :
                payload.image_format == ClipboardImageFormat::dib ? CF_DIB : png_format;
        }
        if (!format) return E_UNEXPECTED;
        const auto data = payload.kind == ClipboardKind::image ? std::span<const std::uint8_t>(payload.image) : std::span<const std::uint8_t>(bytes);
        OwnedGlobal primary(data.size()), marker(sizeof(ClipboardOwnerMarker)), effect(sizeof(DWORD));
        const DWORD copy_effect = DROPEFFECT_COPY;
        const ClipboardOwnerMarker owner_marker{GetCurrentProcessId(), route->token};
        if (!primary.fill(data.data(), data.size()) || !marker.fill(&owner_marker, sizeof(owner_marker)) ||
            (payload.kind == ClipboardKind::files && !effect.fill(&copy_effect, sizeof(copy_effect)))) return E_OUTOFMEMORY;
        HRESULT status = S_OK;
        {
            ClipboardLease lease(owner);
            if (!lease.opened) return set_clipboard_error(last_error());
            if (!EmptyClipboard()) status = last_error();
            else if (!primary.publish(format)) status = last_error();
            else {
                // The content succeeds independently of optional metadata. The
                // sequence check also suppresses our update if marker publication fails.
                if (owner_format) marker.publish(owner_format);
                if (payload.kind == ClipboardKind::files && drop_effect_format) effect.publish(drop_effect_format);
            }
            clipboard_sequence.copied(GetClipboardSequenceNumber());
        }
        return set_clipboard_error(status);
    }
};

SystemServices::SystemServices() : impl_(std::make_unique<Impl>()) {}
SystemServices::~SystemServices() { stop(); }
HRESULT SystemServices::start(HWND owner, UINT notification_message, Changed changed, bool audio_active) {
    if (!owner || !IsWindow(owner) || notification_message < WM_APP || notification_message > 0xbfff) return E_INVALIDARG;
    if (GetWindowThreadProcessId(owner, nullptr) != GetCurrentThreadId()) return RPC_E_WRONG_THREAD;
    if (impl_->owner && !impl_->owner_thread()) return RPC_E_WRONG_THREAD;
    impl_->stop();
    try {
        impl_->owner = owner; impl_->thread = GetCurrentThreadId(); impl_->message = notification_message; impl_->changed = std::move(changed);
        impl_->route = std::make_shared<NotificationRoute>(); impl_->route->owner = owner; impl_->route->message = notification_message;
        impl_->route->token = next_route.fetch_add(1, std::memory_order_relaxed);
        impl_->owner_format = RegisterClipboardFormatW(L"EndfieldHUD.Windows.ClipboardOwner.v1");
        impl_->history_format = RegisterClipboardFormatW(L"CanIncludeInClipboardHistory");
        impl_->png_format = RegisterClipboardFormatW(L"PNG");
        impl_->drop_effect_format = RegisterClipboardFormatW(L"Preferred DropEffect");
        for (const auto* name : {L"ExcludeClipboardContentFromMonitorProcessing", L"Clipboard Viewer Ignore",
            L"org.nspasteboard.ConcealedType", L"org.nspasteboard.TransientType", L"org.nspasteboard.AutoGeneratedType",
            L"de.petermaurer.TransientPasteboardType", L"com.typeit4me.clipping"})
            impl_->excluded_formats.push_back(RegisterClipboardFormatW(name));
        impl_->clipboard_sequence.begin(GetClipboardSequenceNumber());
        const bool privacy_formats_ready = impl_->history_format &&
            std::all_of(impl_->excluded_formats.begin(), impl_->excluded_formats.end(), [](const auto format) { return format != 0; });
        impl_->clipboard_registered = privacy_formats_ready && AddClipboardFormatListener(owner) != FALSE;
        impl_->clipboard_error = impl_->clipboard_registered ? S_OK : privacy_formats_ready ? last_error() : E_FAIL;
        impl_->ac_notification = RegisterPowerSettingNotification(owner, &GUID_ACDC_POWER_SOURCE, DEVICE_NOTIFY_WINDOW_HANDLE);
        impl_->battery_notification = RegisterPowerSettingNotification(owner, &GUID_BATTERY_PERCENTAGE_REMAINING, DEVICE_NOTIFY_WINDOW_HANDLE);
        if (audio_active) impl_->set_audio_active(true);
        impl_->refresh_battery();
        return S_OK;
    } catch (const std::bad_alloc&) { impl_->stop(); return E_OUTOFMEMORY; }
    catch (...) { impl_->stop(); return E_FAIL; }
}
void SystemServices::stop() noexcept { impl_->stop(); }
HRESULT SystemServices::set_audio_active(bool active) {
    try { return impl_->set_audio_active(active); }
    catch (const std::bad_alloc&) { impl_->stop_audio(); impl_->audio_active = false; return E_OUTOFMEMORY; }
    catch (...) { impl_->stop_audio(); impl_->audio_active = false; return E_FAIL; }
}
bool SystemServices::running() const noexcept { return impl_->owner != nullptr; }
const BatterySnapshot& SystemServices::battery() const noexcept { return impl_->battery; }
const ClipboardHistory& SystemServices::clipboard() const noexcept { return impl_->clipboard; }
HRESULT SystemServices::clipboard_status() const noexcept { return impl_->clipboard_error; }
const AudioSnapshot& SystemServices::audio() const noexcept { return impl_->audio; }
bool SystemServices::handle_message(UINT message, WPARAM wparam, LPARAM) {
    if (!impl_->owner_thread()) return false;
    if (message == WM_POWERBROADCAST) {
        if (wparam == PBT_APMPOWERSTATUSCHANGE || wparam == PBT_POWERSETTINGCHANGE || wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND) impl_->route->post(battery_event);
        if (impl_->audio_route && (wparam == PBT_APMRESUMEAUTOMATIC || wparam == PBT_APMRESUMESUSPEND)) impl_->audio_route->post(topology_event);
        return true;
    }
    if (message == WM_CLIPBOARDUPDATE) { impl_->route->post(clipboard_event); return true; }
    if (message == impl_->message) {
        if (impl_->route && wparam == impl_->route->token) {
            const auto events = impl_->route->events.drain();
            if (events & battery_event) refresh_battery();
            if (events & clipboard_event) refresh_clipboard();
            else if (events & clipboard_retry_event) {
                // One message-loop retry per native change, never a polling loop.
                try { impl_->refresh_clipboard(false); } catch (...) { /* Explicit UI refresh can retry. */ }
            }
        } else if (impl_->audio_route && wparam == impl_->audio_route->token) {
            const auto events = impl_->audio_route->events.drain();
            if (events & topology_event) refresh_audio();
            else if (events & volume_event) {
                try { impl_->refresh_volume(); } catch (...) { /* A later native notification may retry; never unwind a COM callback. */ }
            }
        }
        return true; // Stale generations carry no pointers and cannot refresh a restarted owner.
    }
    return false;
}
void SystemServices::refresh_battery() { impl_->refresh_battery(); }
HRESULT SystemServices::refresh_clipboard() {
    try { return impl_->refresh_clipboard(); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
}
HRESULT SystemServices::copy_clipboard_item(std::uint64_t id) {
    try { return impl_->copy_clipboard_item(id); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
}
bool SystemServices::erase_clipboard_item(std::uint64_t id) {
    if (!impl_->owner_thread() || !impl_->clipboard.erase(id)) return false;
    impl_->notify(ServiceChange::clipboard); return true;
}
bool SystemServices::pin_clipboard_item(std::uint64_t id, bool pinned) {
    if (!impl_->owner_thread() || !impl_->clipboard.pin(id, pinned)) return false;
    impl_->notify(ServiceChange::clipboard); return true;
}
bool SystemServices::set_clipboard_capacity(std::size_t capacity) {
    if (!impl_->owner_thread() || !impl_->clipboard.set_capacity(capacity)) return false;
    impl_->notify(ServiceChange::clipboard); return true;
}
void SystemServices::clear_clipboard(bool keep_pinned) {
    if (!impl_->owner_thread()) return;
    const auto count = impl_->clipboard.items().size(); impl_->clipboard.clear(keep_pinned);
    if (impl_->clipboard.items().size() != count) impl_->notify(ServiceChange::clipboard);
}
HRESULT SystemServices::refresh_audio() {
    try { return impl_->refresh_audio(); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
}
HRESULT SystemServices::select_audio_endpoint(std::wstring id) {
    if (!impl_->owner_thread()) return RPC_E_WRONG_THREAD;
    if (!impl_->audio_active) return E_PENDING;
    if (id.size() > 32767 || id.find(L'\0') != std::wstring::npos) return E_INVALIDARG;
    if (!id.empty() && std::none_of(impl_->audio.devices.begin(), impl_->audio.devices.end(), [&](const auto& device) { return device.id == id; })) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    impl_->selected_endpoint = std::move(id); return refresh_audio();
}
HRESULT SystemServices::set_master_volume(float scalar) {
    if (!impl_->owner_thread()) return RPC_E_WRONG_THREAD;
    if (!std::isfinite(scalar) || scalar < 0 || scalar > 1) return E_INVALIDARG;
    if (!impl_->audio_active) return E_PENDING;
    if (!impl_->endpoint_volume) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    const auto status = impl_->endpoint_volume->SetMasterVolumeLevelScalar(scalar, nullptr);
    if (SUCCEEDED(status)) impl_->refresh_volume(); return status;
}
HRESULT SystemServices::set_master_mute(bool muted) {
    if (!impl_->owner_thread()) return RPC_E_WRONG_THREAD;
    if (!impl_->audio_active) return E_PENDING;
    if (!impl_->endpoint_volume) return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    const auto status = impl_->endpoint_volume->SetMute(muted ? TRUE : FALSE, nullptr);
    if (SUCCEEDED(status)) impl_->refresh_volume(); return status;
}
} // namespace endfield::native
#endif
