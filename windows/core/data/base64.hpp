#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace ehud::data {
// Standard base64 (RFC 4648 alphabet, '=' padding), as Foundation's Data
// encodes it in JSON and plists. Shared by the Mac import (plist <data>,
// typed fixtures) and stores that keep Data fields (Archive verifiers).
std::string base64Encode(std::string_view bytes);
// strictTail=false accepts non-zero unused bits like Foundation Data(base64Encoded:).
std::optional<std::string> base64Decode(std::string_view text, bool allowWhitespace = false, bool strictTail = true);
}
