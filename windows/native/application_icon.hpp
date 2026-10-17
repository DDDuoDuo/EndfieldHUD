#pragma once
#ifdef _WIN32
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
namespace endfield::native {
enum class IconInk { original,blackTemplate,whiteTemplate };
// A caller-owned HICON, never installed globally. The tray/window borrows its
// handle until it is replaced. Construction occurs only on icon/DPI/theme
// changes; no image cache, polling, application window or COM apartment is made.
class ApplicationIcon final {
public:
    ApplicationIcon()=default;~ApplicationIcon();
    ApplicationIcon(ApplicationIcon&&)noexcept;
    ApplicationIcon&operator=(ApplicationIcon&&)noexcept;
    ApplicationIcon(const ApplicationIcon&)=delete;
    ApplicationIcon&operator=(const ApplicationIcon&)=delete;
    static ApplicationIcon fromPNG(const std::filesystem::path&,std::string_view sha256,unsigned pixels,IconInk=IconInk::original);
    static ApplicationIcon fromPixels(unsigned width,unsigned height,std::span<const std::uint8_t>premultipliedBGRA);
    void*handle()const noexcept{return handle_;}
private:explicit ApplicationIcon(void* value):handle_(value){}void*handle_{};
};
}
#endif
