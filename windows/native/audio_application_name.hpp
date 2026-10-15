#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
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
} // namespace endfield::native
