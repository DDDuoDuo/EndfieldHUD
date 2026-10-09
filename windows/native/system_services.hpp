#pragma once
#include "audio_session_worker.hpp"

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {

struct PowerStatus {
    std::uint8_t ac_line_status{255}, battery_flags{255}, battery_percent{255};
    std::uint32_t remaining_seconds{0xffffffffu}, full_seconds{0xffffffffu};
};
struct BatterySnapshot {
    bool available{};
    std::optional<bool> present, ac_connected, charging, fully_charged;
    std::optional<unsigned> percent;
    std::optional<std::uint32_t> remaining_seconds, full_seconds;
    bool operator==(const BatterySnapshot&) const = default;
};
// Pure decoder also used by synthetic tests. Unknown native values stay unknown.
BatterySnapshot decode_power_status(const PowerStatus& status) noexcept;

enum class ClipboardKind { text, url, files, image };
enum class ClipboardImageFormat { dib, dib_v5, png };
struct ClipboardPayload {
    ClipboardKind kind{ClipboardKind::text};
    std::u16string text;
    std::vector<std::u16string> files;
    ClipboardImageFormat image_format{ClipboardImageFormat::dib};
    std::vector<std::uint8_t> image;
    bool operator==(const ClipboardPayload&) const = default;
};
struct ClipboardItem {
    std::uint64_t id{};
    bool pinned{};
    ClipboardPayload payload;
};
enum class ClipboardInsertResult { inserted, moved, duplicate, excluded, invalid, full };

// No OS access, file copies, disk persistence or timers. Newest item is first.
// Image bytes preserve the accepted clipboard format, including its alpha data.
class ClipboardHistory final {
public:
    static constexpr std::size_t maximum_text_bytes = 1'048'576;
    static constexpr std::size_t maximum_image_bytes = 64 * 1'048'576;
    // ClipboardStore.swift:183–195. Encoded payload storage is independent of
    // decoded dimensions; a preview consumer must produce only a 96px thumbnail.
    static constexpr std::uint32_t maximum_image_dimension = 100'000;
    static constexpr std::uint64_t maximum_image_pixels = 100'000'000;
    static constexpr unsigned maximum_thumbnail_dimension = 96;
    static constexpr std::size_t maximum_retained_bytes = 128 * 1'048'576;
    static constexpr std::size_t maximum_files = 512;
    static constexpr std::size_t maximum_capacity = 512;
    explicit ClipboardHistory(std::size_t capacity = 10);
    const std::vector<ClipboardItem>& items() const noexcept { return items_; }
    const ClipboardItem* find(std::uint64_t id) const noexcept;
    std::size_t retained_bytes() const noexcept { return retained_bytes_; }
    std::size_t capacity() const noexcept { return capacity_; }
    ClipboardInsertResult ingest(ClipboardPayload payload, bool excluded = false);
    bool erase(std::uint64_t id);
    bool pin(std::uint64_t id, bool pinned);
    bool set_capacity(std::size_t capacity);
    void clear(bool keep_pinned = true);
    static bool valid(const ClipboardPayload& payload) noexcept;
    static std::size_t payload_bytes(const ClipboardPayload& payload) noexcept;
    static bool valid_utf16(std::u16string_view value) noexcept;
    static std::size_t utf8_bytes(std::u16string_view value) noexcept;
    static bool url_text(std::u16string_view value) noexcept;
    // Metadata preflight only: no allocation, decompression or pixel buffer.
    static bool valid_image_metadata(std::uint32_t width, std::uint32_t height,
        std::size_t encoded_bytes) noexcept;
    // Pure structural DIB/PNG validation, without invoking an image decoder.
    // PNG stream/CRC validity and successful bounded thumbnail decoding remain
    // the image consumer's responsibility, as distinct from these size limits.
    static bool valid_image(ClipboardImageFormat format, std::span<const std::uint8_t> bytes) noexcept;
private:
    std::vector<ClipboardItem> items_;
    std::size_t capacity_{10}, retained_bytes_{};
    std::uint64_t next_id_{1};
};

struct AudioDevice {
    std::wstring id, name;
    bool is_default{};
    bool headphones{}; // Explicit endpoint form factor; no name/ID guessing.
    bool operator==(const AudioDevice&) const = default;
};
struct AudioSnapshot {
    bool available{}, paused{true};
    std::vector<AudioDevice> devices;
    std::wstring default_device_id, controlled_device_id;
    std::optional<float> volume;
    std::optional<bool> muted;
    // HRESULT represented without a Windows dependency for model consumers.
    std::int32_t error{};
    std::vector<AudioDevice> inputs;
    std::wstring default_input_device_id;
    std::optional<float> balance;
    bool can_set_balance{};
    std::int32_t input_error{};
    bool application_supported{},applications_paused{true};
    std::vector<AudioApplicationRoute>applications;
    std::int32_t application_error{};
    bool operator==(const AudioSnapshot&) const = default;
};
// Same peak-preserving stereo math as Mac AudioVolumeMath. Unknown/nonfinite
// channel state remains unavailable; all-zero channels cannot infer balance.
std::optional<float> audio_stereo_balance(float left,float right) noexcept;
std::array<float,2> audio_stereo_levels(float peak,float balance) noexcept;
struct AudioStereoAccess {
    std::function<std::int32_t(unsigned,float&)>read;
    std::function<std::int32_t(unsigned,float)>write;
};
// HRESULT-shaped status, preserving the first failure unless rollback fails.
// Reads both originals before mutation, validates each readback, and restores
// all attempted channels in reverse order, as the source HAL transaction does.
std::int32_t apply_audio_stereo_balance(float,const AudioStereoAccess&) noexcept;
enum class ServiceChange : unsigned { battery = 1, clipboard = 2, audio = 4 };

// Testable, allocation-free callback mailbox. request() returns true only for
// the first event in a batch; the caller posts one message. cancel() is terminal.
// Posted messages carry a separately allocated generation, never a raw pointer.
class NotificationBatch final {
public:
    bool request(unsigned events) noexcept;
    unsigned drain() noexcept;
    void cancel() noexcept;
    bool active() const noexcept { return active_.load(std::memory_order_acquire); }
private:
    std::atomic<bool> active_{true};
    std::atomic<unsigned> pending_{};
};
class ClipboardSequence final {
public:
    void begin(std::uint32_t sequence) noexcept { last_ = sequence; own_ = 0; }
    bool should_capture(std::uint32_t sequence) const noexcept;
    void acknowledge(std::uint32_t sequence) noexcept { last_ = sequence; }
    void copied(std::uint32_t sequence) noexcept { last_ = own_ = sequence; }
private:
    std::uint32_t last_{}, own_{};
};
struct ClipboardExclusion {
    bool concealed_marker{}, malformed_history_flag{};
    std::optional<std::uint32_t> include_in_history;
};
bool excludes_clipboard(const ClipboardExclusion& policy) noexcept;

} // namespace endfield::native

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace endfield::native {

// All public methods and destruction belong to the owner HWND's thread.
// Call stop() before destroying its HWND. Changed callbacks must not stop or
// restart this service reentrantly; queue that operation onto the owner instead.
// Core Audio callbacks only post a coalesced message; they never call UI code.
// Forward WM_POWERBROADCAST, WM_CLIPBOARDUPDATE and notification_message to
// handle_message. A recognized message may still need the owner's own handling.
class SystemServices final {
public:
    using Changed = std::function<void(ServiceChange)>;
    SystemServices();
    ~SystemServices();
    SystemServices(const SystemServices&) = delete;
    SystemServices& operator=(const SystemServices&) = delete;
    // The message must be a dedicated WM_APP..0xBFFF value. No initial clipboard
    // read occurs: history begins with subsequent clipboard-change events.
    // A valid start can have unavailable individual services; inspect snapshots
    // and clipboard_status rather than treating missing hardware as fake data.
    HRESULT start(HWND owner, UINT notification_message, Changed changed, bool audio_active = false);
    // Inactive audio has no endpoint/device listeners. Battery + clipboard stay
    // registered, with no polling. Resume does a single fresh topology read.
    HRESULT set_audio_active(bool active);
    void stop() noexcept;
    bool running() const noexcept;
    bool handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    const BatterySnapshot& battery() const noexcept;
    const ClipboardHistory& clipboard() const noexcept;
    HRESULT clipboard_status() const noexcept;
    const AudioSnapshot& audio() const noexcept;
    void refresh_battery();
    // Native update events get one coalesced retry when another process holds
    // the clipboard. If still busy, this explicit UI action may retry later.
    HRESULT refresh_clipboard();
    HRESULT copy_clipboard_item(std::uint64_t id);
    bool erase_clipboard_item(std::uint64_t id);
    bool pin_clipboard_item(std::uint64_t id, bool pinned);
    bool set_clipboard_capacity(std::size_t capacity);
    void clear_clipboard(bool keep_pinned = false);
    HRESULT refresh_audio();
    // Selects the endpoint whose own volume is controlled. This does NOT switch
    // the Windows default playback device or reroute another application's audio.
    // Empty id follows the system's default eConsole render endpoint.
    HRESULT select_audio_endpoint(std::wstring id);
    HRESULT set_master_volume(float scalar);
    HRESULT set_master_mute(bool muted);
    // Public endpoint channel controls only, exactly stereo with a nonzero
    // readable peak. Unsupported/multichannel devices retain no fake balance.
    HRESULT set_output_balance(float balance);
    // Async owner commands. Accepted writes return S_OK; their actual result is
    // published through the existing audio-change notification and snapshot.
    // Gain is relative to each confirmed session's preexisting mixer level.
    HRESULT set_application_gain(std::string application,float gain);
    HRESULT stop_application(std::string application);
    HRESULT stop_applications();
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace endfield::native
#endif
