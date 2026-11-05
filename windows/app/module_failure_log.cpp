#include "app/module_failure_log.hpp"
#include "core/data/file_io.hpp"
#include <algorithm>
#include <ctime>

namespace endfield::app {
std::string redacted(std::string_view message) {
    std::string out;
    std::size_t start = 0;
    while (start < message.size() && out.size() < 512) {
        std::size_t end = start;
        while (end < message.size() && message[end] != ' ') ++end;
        const auto word = message.substr(start, end - start);
        if (word.find_first_of("\\/") != std::string_view::npos) out += "<path>";
        else for (const char c : word) out.push_back(c >= 0x20 && c < 0x7f ? c : '?');
        if (end < message.size()) out.push_back(' ');
        start = end + 1;
    }
    if (out.size() > 512) out.resize(512);
    return out;
}
std::string failureMessage(std::string_view message) { return redacted(message.substr(0, message.find(": ")).substr(0, 160)); }

bool appendBoundedRecord(const std::filesystem::path& path, std::string_view record, std::string_view header, std::size_t maximum) noexcept {
    try {
        const auto previous = ehud::data::detail::readFile(path, maximum);
        std::string text = previous.value_or(std::string()) + std::string(record);
        // The oldest records go first, whole; a record alone over the bound is cut.
        while (text.size() > maximum) {
            const auto next = header.empty() ? std::string::npos : text.find(header, 1);
            if (next == std::string::npos) { text = std::string(record.substr(0, maximum)); break; }
            text.erase(0, next);
        }
        ehud::data::detail::replaceFile(path, previous, text, maximum);
        return true;
    } catch (...) { return false; }
}

bool ModuleFailureLog::record(const ModuleFailure& f, std::string_view context) noexcept {
    try {
        if (std::find(owners_.begin(), owners_.end(), f.owner) != owners_.end() || owners_.size() >= 64) return false;
        owners_.push_back(f.owner);
        std::string text(header);
        char stamp[32]{};
        const auto now = std::time(nullptr);
        std::tm utc{};
#ifdef _WIN32
        const bool converted = !gmtime_s(&utc, &now);
#else
        const bool converted = gmtime_r(&now, &utc) != nullptr;
#endif
        if (converted && std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%SZ", &utc)) text += "utc: " + std::string(stamp) + "\n";
        text += "owner: " + redacted(std::string_view(f.owner).substr(0, 64)) + "\n";
        text += "exception: " + redacted(std::string_view(f.what).substr(0, 160)) + "\n";
        text += "message: " + (f.message.empty() ? std::string("(none)") : redacted(std::string_view(f.message).substr(0, 160))) + "\n";
        text += "operation: " + redacted(std::string_view(f.operation).substr(0, 64)) + "\n";
        text += "stage: " + redacted(std::string_view(f.stage).substr(0, 384)) + "\n";
        for (auto rest = context.substr(0, 1024); !rest.empty();) {
            const auto end = rest.find('\n');
            if (const auto line = rest.substr(0, end); !line.empty()) text += redacted(line) + "\n";
            rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        }
        text += "\n";
        return appendBoundedRecord(path_, text, header, maximumBytes);
    } catch (...) { return false; }
}
} // namespace endfield::app
