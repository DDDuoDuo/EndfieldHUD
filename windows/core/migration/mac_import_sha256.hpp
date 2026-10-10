#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ehud::migration {
// FIPS 180-4 SHA-256 for export manifests. Streaming, allocation-free and
// portable, so the same digest code verifies exports on macOS and Windows.
class Sha256 final {
public:
    Sha256() noexcept;
    void update(const void* bytes, std::size_t count) noexcept;
    void update(std::string_view bytes) noexcept { update(bytes.data(), bytes.size()); }
    std::array<std::uint8_t, 32> finish() noexcept;
    std::string hex() noexcept; // lowercase, 64 digits
    static std::string hex(std::string_view bytes);
private:
    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> block_{};
    std::uint64_t length_{};
    std::size_t used_{};
    bool finished_{};
    void compress(const std::uint8_t* block) noexcept;
};
bool validSha256Hex(std::string_view) noexcept;
}
