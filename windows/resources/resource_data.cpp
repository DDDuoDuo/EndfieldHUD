#include "resource_data.h"
#include <zlib.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
namespace ehud::resources {
std::vector<std::uint8_t> decode(std::span<const std::uint8_t> input) {
    constexpr std::array<std::uint8_t, 8> magic{'E','H','U','D','Z','0','1',0};
    if (input.size() > maximumBytes + 1024 * 1024) throw std::runtime_error("Resource input exceeds bound");
    if (input.size() < magic.size() || !std::equal(magic.begin(), magic.end(), input.begin())) {
        if (input.size() >= 5 && std::equal(magic.begin(), magic.begin() + 5, input.begin()))
            throw std::runtime_error("Unknown EHUD resource container");
        if (input.size() > maximumBytes) throw std::runtime_error("Resource exceeds decoded bound");
        return {input.begin(), input.end()};
    }
    if (input.size() < 16) throw std::runtime_error("Truncated EHUD resource header");
    std::uint64_t expected{};
    for (unsigned index = 0; index < 8; ++index) expected |= std::uint64_t(input[8 + index]) << (8 * index);
    if (!expected || expected > maximumBytes) throw std::runtime_error("Invalid EHUD resource length");
    // One extra byte detects streams whose header understates their output.
    std::vector<std::uint8_t> output(static_cast<std::size_t>(expected) + 1);
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(input.data() + 16);
    stream.avail_in = static_cast<uInt>(input.size() - 16);
    stream.next_out = output.data(); stream.avail_out = static_cast<uInt>(output.size());
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) throw std::runtime_error("Cannot initialize resource decoder");
    const int status = inflate(&stream, Z_FINISH);
    const bool valid = status == Z_STREAM_END && stream.total_out == expected && stream.avail_in == 0;
    inflateEnd(&stream);
    if (!valid) throw std::runtime_error("Incomplete, oversized or trailing EHUD resource payload");
    output.resize(static_cast<std::size_t>(expected)); return output;
}
std::vector<std::uint8_t> read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Missing runtime resource");
    const auto size = file.tellg();
    if (size < 0 || static_cast<std::uint64_t>(size) > maximumBytes + 1024 * 1024)
        throw std::runtime_error("Runtime resource file exceeds bound");
    std::vector<std::uint8_t> input(static_cast<std::size_t>(size)); file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(input.data()), size)) throw std::runtime_error("Cannot read runtime resource");
    return decode(input);
}
std::string text(const std::filesystem::path& path) {
    auto bytes = read(path); return {bytes.begin(), bytes.end()};
}
}
