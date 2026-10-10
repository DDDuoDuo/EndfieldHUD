#include "core/application_arguments.hpp"
#include <cstdint>
#include <stdexcept>

namespace endfield::core {
namespace {
bool validUtf8(std::string_view text) noexcept {
    std::size_t at = 0;
    while (at < text.size()) {
        const auto c = static_cast<unsigned char>(text[at]);
        std::size_t length = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 0;
        if (!length || at + length > text.size()) return false;
        std::uint32_t value = length == 1 ? c : c & (0x7f >> length);
        for (std::size_t n = 1; n < length; ++n) {
            const auto next = static_cast<unsigned char>(text[at + n]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if ((length == 2 && value < 0x80) || (length == 3 && value < 0x800) || (length == 4 && (value < 0x10000 || value > 0x10ffff)) ||
            (value >= 0xd800 && value <= 0xdfff)) return false;
        at += length;
    }
    return true;
}
void bounded(std::span<const std::string_view> arguments) {
    if (arguments.size() > applicationArgumentLimit) throw std::invalid_argument("Too many application arguments");
    for (const auto value : arguments)
        if (value.size() > applicationArgumentBytes || value.find('\0') != std::string_view::npos || !validUtf8(value))
            throw std::invalid_argument("Invalid application argument");
}
}

ApplicationArguments parseApplicationArguments(std::span<const std::string_view> arguments) {
    bounded(arguments);
    ApplicationArguments result;
    for (const auto value : arguments) {
        if (value == "--login") result.startup.login = true;
        else if (value == "--no-onboarding") result.startup.noOnboarding = true;
        else if (value == "--settings") result.startup.settings = true;
        else if (value == "--power") result.startup.power = true;
        else if (value == "--preview") result.startup.preview = true;
        else if (value == "--notification-activation") result.notificationActivation = true;
        else ++result.ignored;
    }
    return result;
}

ForwardedActivation forwardedActivation(const ApplicationArguments& value) noexcept {
    if (value.startup.settings) return ForwardedActivation::settings;
    if (value.notificationActivation) return ForwardedActivation::none;
    return ForwardedActivation::reopen;
}

std::string encodeForwardedArguments(std::span<const std::string_view> arguments) {
    bounded(arguments);
    std::string out(forwardedArgumentsMagic);
    for (const auto value : arguments) { out.append(value); out.push_back('\0'); }
    return out;
}

std::vector<std::string> decodeForwardedArguments(std::string_view payload) {
    constexpr std::size_t maximum = forwardedArgumentsMagic.size() + applicationArgumentLimit * (applicationArgumentBytes + 1);
    if (payload.size() > maximum || payload.substr(0, forwardedArgumentsMagic.size()) != forwardedArgumentsMagic)
        throw std::invalid_argument("Invalid forwarded activation");
    payload.remove_prefix(forwardedArgumentsMagic.size());
    std::vector<std::string> out;
    while (!payload.empty()) {
        const auto end = payload.find('\0');
        if (end == std::string_view::npos) throw std::invalid_argument("Unterminated forwarded argument");
        out.emplace_back(payload.substr(0, end));
        payload.remove_prefix(end + 1);
        if (out.size() > applicationArgumentLimit) throw std::invalid_argument("Too many forwarded arguments");
    }
    std::vector<std::string_view> views(out.begin(), out.end());
    bounded(views);
    return out;
}
} // namespace endfield::core
