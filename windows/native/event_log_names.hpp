#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
namespace endfield::native {
// Caller-owned, thread-confined ICU character iterator. Windows uses the
// system icu.dll (Windows 10 1903+), not a bundled engine or worker. Borrow this
// object in modules::EventNameCompactor; it must outlive that callback.
// Foundation whitespace/control sets are pinned to the original Mac oracle.
// Grapheme boundaries use the installed ICU Unicode version, exposed below;
// matching all future Unicode versions is not claimed.
class NativeEventNameCompactor final {
public:
    NativeEventNameCompactor();~NativeEventNameCompactor();
    NativeEventNameCompactor(const NativeEventNameCompactor&)=delete;
    NativeEventNameCompactor&operator=(const NativeEventNameCompactor&)=delete;
    std::string compact(std::string_view utf8); // <=4096 input bytes, <=160 output bytes, <=512 source characters
    static std::array<std::uint8_t,4>unicodeVersion()noexcept;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
