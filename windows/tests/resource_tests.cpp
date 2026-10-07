#include "resources/resource_data.h"
#include <iostream>
#include <stdexcept>
using Bytes = std::vector<std::uint8_t>;
int main() {
    unsigned checks{};
    auto check = [&](bool value) { ++checks; if (!value) throw std::runtime_error("Resource contract failure"); };
    auto rejects = [&](const Bytes& bytes) { try { ehud::resources::decode(bytes); } catch (const std::runtime_error&) { ++checks; return; } throw std::runtime_error("Invalid resource accepted"); };
    try {
        // Raw DEFLATE of UTF-8 "hello"; no zlib wrapper.
        Bytes valid{'E','H','U','D','Z','0','1',0,5,0,0,0,0,0,0,0,0xcb,0x48,0xcd,0xc9,0xc9,0x07,0};
        auto result = ehud::resources::decode(valid); check(std::string(result.begin(), result.end()) == "hello");
        auto bytes = valid; bytes.pop_back(); rejects(bytes);
        bytes = valid; bytes.push_back(0); rejects(bytes);
        bytes = valid; bytes[8] = 4; rejects(bytes);
        bytes = valid; bytes[8] = 6; rejects(bytes);
        bytes = valid; bytes[8] = 0; rejects(bytes);
        bytes = valid; bytes[11] = 9; rejects(bytes);
        bytes = valid; bytes.resize(12); rejects(bytes);
        bytes = valid; bytes[6] = '2'; rejects(bytes);
        bytes = valid; bytes[16] = 0xff; rejects(bytes);
        bytes = {'x','y','z'}; check(ehud::resources::decode(bytes) == bytes);
        std::cout << checks << " native resource decoder checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
