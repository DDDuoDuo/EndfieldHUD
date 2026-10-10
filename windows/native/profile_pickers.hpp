#pragma once
#include "core/localization.hpp"
#include "modules/profile_state.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace endfield::native {
// Host pickers for the Personal Profile requests (ProfileOwnerOptions::
// chooseImage/chooseColor). Owner thread with COM STA initialized. Both are
// modal over the borrowed HUD window, like the source sheet/panel, and are
// only called from the owner's deferred picker message, never inside input.

// HUDPersonalProfileInteraction.deactivate cancels an open chooser and orders
// the colour panel out. A session handed to a picker exposes that close while
// its modal loop runs; cancel() from the same thread (e.g. the HUD hiding)
// makes the picker return as cancelled. Idle sessions ignore cancel().
class ProfilePickerSession final {
public:
    void cancel();
    bool open()const noexcept{return static_cast<bool>(close_);}
    void attach(std::function<void()>close){close_=std::move(close);} // picker implementation only
    void detach()noexcept{close_={};}
private:std::function<void()>close_;
};
// HUDPersonalProfileInteraction.chooseImage NSOpenPanel strings.
struct ProfileImagePickerText {std::wstring title,okLabel,filterName;};
ProfileImagePickerText profileImagePickerText(modules::ProfileImageKind,core::Language);
// UTType.image on Windows: every file extension of the WIC decoders installed
// on this machine ("*.bmp;*.dib;...;*.png"), sorted and unique. The filter only
// narrows the listing; the import still lets the content choose the codec.
std::wstring profileImageFilterSpec();
// Modal IFileOpenDialog: one existing file-system file (shortcuts resolved,
// working directory unchanged). nullopt when cancelled.
std::optional<std::filesystem::path>chooseProfileImage(HWND owner,modules::ProfileImageKind,core::Language,ProfilePickerSession* =nullptr);

// NSColorPanel(wheel, isContinuous, no alpha, "Card color") counterpart: the
// full ChooseColorW dialog with a hook that streams every complete RGB change
// to live while it is open. OK returns the chosen colour; Cancel returns
// nullopt (the owner restores the colour the picker started from).
std::wstring profileColorPickerTitle(core::Language);
std::optional<std::array<double,3>>chooseProfileColor(HWND owner,std::array<double,3>initial,core::Language,
    const std::function<void(std::array<double,3>)>&live,ProfilePickerSession* =nullptr);
// String(format:"%02X", Int((c * 255).rounded())) channels as a COLORREF.
COLORREF profileColorRef(std::array<double,3>)noexcept;
std::array<double,3>profileColorFromRef(COLORREF)noexcept;
// The dialog's 0...255 Red/Green/Blue fields; nullopt while a field is incomplete.
std::optional<std::array<double,3>>profileColorFromFields(std::wstring_view red,std::wstring_view green,std::wstring_view blue);
// Coalesces the dialog's field notifications: setting R, G and B arrives as
// three edits, so the hook posts once and delivers the settled triple; equal
// colours are not repeated.
class ProfileColorStream final {
public:
    explicit ProfileColorStream(std::function<void(std::array<double,3>)>live,std::optional<std::array<double,3>>initial={});
    bool fieldsChanged()noexcept; // true: post one settle message now
    bool settle(std::optional<std::array<double,3>>fields); // true: live was called
    const std::optional<std::array<double,3>>&last()const noexcept{return last_;}
private:
    std::function<void(std::array<double,3>)>live_;std::optional<std::array<double,3>>last_;bool pending_{};
};
}
#endif
