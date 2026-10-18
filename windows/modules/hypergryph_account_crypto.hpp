#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// Portable digests for the community request signature (HMAC-SHA256, then MD5
// of its lowercase hex) and the API's identity digest (SHA-256). Inputs are
// treated as opaque bytes; nothing is logged, cached or retained here.
namespace endfield::modules::hypergryph {
using Sha256Digest=std::array<std::uint8_t,32>;
using Md5Digest=std::array<std::uint8_t,16>;
Sha256Digest sha256(std::span<const std::uint8_t>) noexcept;
Sha256Digest sha256(std::string_view) noexcept;
Sha256Digest hmacSha256(std::string_view key,std::string_view message) noexcept;
Md5Digest md5(std::string_view) noexcept;
std::string lowercaseHex(std::span<const std::uint8_t>);
// MD5(lowercase-hex(HMAC-SHA256(key=signingToken, message))) as lowercase hex.
std::string communitySignatureDigest(std::string_view signingToken,std::string_view message);
// Best-effort wipe of a temporary secret copy (not a guarantee against the
// allocator or swap); never used as a reason to keep secrets longer.
void wipe(std::string&) noexcept;
}
