#pragma once
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace endfield::native {
// Display names of packaged (MSIX/AppX) audio applications: the Windows
// counterpart of the source's NSRunningApplication.localizedName for a
// process with package identity, whose audio session usually carries no
// display name and whose executable's version resource names the binary
// rather than the app (e.g. "Microsoft.Media.Player"). The Windows mixer
// shows the package application's DisplayName; so does the Volume list.
//
// A manifest DisplayName is a literal or an "ms-resource:" reference into the
// package's resources.pri. audioPackageResourceReferences returns the
// SHLoadIndirectString candidates for a reference, most specific first:
//   ms-resource://Name/...   kept as is (absolute)
//   ms-resource:/Strings/Key ms-resource://<package name>/Strings/Key
//   ms-resource:Key          ms-resource://<package name>/resources/Key,
//                            then ms-resource:///resources/Key
// each wrapped as "@{<package full name>?<uri>}". Empty for a literal, an
// empty key or invalid identity text (never guessed from the process name).
std::vector<std::wstring>audioPackageResourceReferences(std::wstring_view packageFullName,std::wstring_view packageName,std::wstring_view displayName);
// The application id of a packaged AppUserModelID ("<family name>!<id>");
// empty when there is none.
std::wstring_view audioPackageApplicationID(std::wstring_view appUserModelID)noexcept;
// The names an AppxManifest.xml declares: the package identity name, the
// DisplayName of the matching <Application Id=...>'s VisualElements and the
// package <Properties><DisplayName>. Values are trimmed; empty when absent.
struct AudioPackageManifestNames {
    std::wstring packageName,applicationDisplayName,packageDisplayName;
    bool operator==(const AudioPackageManifestNames&)const=default;
};
#ifdef _WIN32
// Reads a package manifest with XmlLite: no DTD, at most 4 MiB, 64 levels and
// 4096 characters per value; read-only, on the caller's thread (the audio
// worker's MTA). nullopt for a missing, oversized or malformed manifest or one
// without a package identity name.
std::optional<AudioPackageManifestNames>readAudioPackageManifest(const std::filesystem::path&manifest,std::wstring_view applicationID);
#endif
// Per-app audio labels: the one rule for the WASAPI session backend's name
// chain and the audio worker's backstop over any backend. A label is shown
// only when it is non-empty, holds no NUL, is well-formed UTF-16 and its UTF-8
// form fits a Volume row's 4096 bytes. Checked without allocating.
bool audioLabelValid(std::wstring_view)noexcept;
// An endpoint's label wherever it is shown (a Volume row, the Event Log's
// device list): its name when audioLabelValid accepts it, else its endpoint
// ID, so an empty, ill-formed or over-long name never withholds the endpoint.
// It returns one of its arguments, so temporaries are refused: a kept result
// would dangle.
inline const std::wstring&audioEndpointLabel(const std::wstring&name,const std::wstring&id)noexcept{return audioLabelValid(name)?name:id;}
const std::wstring&audioEndpointLabel(std::wstring&&,const std::wstring&)=delete;
const std::wstring&audioEndpointLabel(const std::wstring&,std::wstring&&)=delete;
const std::wstring&audioEndpointLabel(std::wstring&&,std::wstring&&)=delete;
// The first valid candidate, else "PID n" (the worker's backstop passes none).
std::wstring audioApplicationLabel(std::initializer_list<std::wstring_view>candidates,std::uint32_t pid);
// The WASAPI session backend's two labels in the source's order (name,
// bundle, PID), so the order itself is under test. A process: the package
// application's DisplayName, else the executable's FileDescription (read
// only when the package name is not shown), else its stem, else "PID n".
template<class Description>std::wstring audioProcessLabel(std::wstring_view packaged,Description&&description,std::wstring_view stem,std::uint32_t pid){
    if(audioLabelValid(packaged))return std::wstring(packaged);
    const std::wstring read=std::forward<Description>(description)();
    return audioApplicationLabel({read,stem},pid);
}
// A session: its own display name, else its process's label.
std::wstring audioSessionLabel(std::wstring_view display,std::wstring_view process,std::uint32_t pid);
} // namespace endfield::native
