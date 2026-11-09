// The shared byte codecs of core/data: FIPS 180-4 SHA-256 (core/data/sha256,
// the Archive's portable PBKDF2) and Foundation-style base64
// (core/data/base64, Archive verifiers and other Data fields). Published test
// vectors only.
#include "core/data/base64.hpp"
#include "core/data/sha256.hpp"
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
namespace d = ehud::data;
unsigned checks{};
void check(bool value, const std::string& message) { ++checks; if (!value) throw std::runtime_error(message); }

void sha256() {
    check(d::Sha256::hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 empty vector");
    check(d::Sha256::hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 abc vector");
    check(d::Sha256::hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA-256 two-block vector");
    { d::Sha256 h; const std::string chunk(1000, 'a'); for (int i = 0; i < 1000; ++i) h.update(chunk); check(h.hex() == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA-256 million-a streaming vector"); }
}
void base64() {
    check(d::base64Encode("") == "" && d::base64Encode("f") == "Zg==" && d::base64Encode("fo") == "Zm8=" && d::base64Encode("foobar") == "Zm9vYmFy", "Base64 vectors");
    check(d::base64Decode("Zm9vYmE=") == std::optional<std::string>("fooba") && !d::base64Decode("Zm9vYmE") && !d::base64Decode("Zg=a") && !d::base64Decode("Zh=="), "Strict canonical base64");
    check(!d::base64Decode(" Zm9v") && !d::base64Decode("Zm9v\nYmFy"), "Whitespace is refused");
    check(d::base64Decode("Zh==", false) == std::optional<std::string>("f") && !d::base64Decode("Zh=="), "Non-zero unused bits only when the tail is not strict");
}
}
int main() {
    try {
        sha256();
        base64();
        std::cout << "PASS " << checks << " data encoding checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
