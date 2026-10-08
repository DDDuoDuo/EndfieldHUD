#pragma once
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace endfield::native {
// Source ArchiveCanvas yyyy-MM-dd, Gregorian/en_US_POSIX, strict round-trip.
// One borrowed-owner formatter, not a timer or a date conversion per frame.
// Empty zone means the OS zone; refreshSystemTimeZone is called on OS time-zone
// changes. Explicit IANA zones make fixtures independent of the user's settings.
class ArchiveDateFormatter final {
public:
    explicit ArchiveDateFormatter(std::u16string timeZone = {});
    ~ArchiveDateFormatter();
    ArchiveDateFormatter(const ArchiveDateFormatter&)=delete;
    ArchiveDateFormatter&operator=(const ArchiveDateFormatter&)=delete;
    std::string format(double foundationSeconds) const;
    std::optional<double>parse(std::string_view text) const;
    bool refreshSystemTimeZone();
private:struct Impl;std::unique_ptr<Impl>impl_;
};
}
