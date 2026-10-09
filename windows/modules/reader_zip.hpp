#pragma once
#include "modules/reader_model.hpp"
#include <functional>
#include <memory>
#include <map>

namespace endfield::modules {
using ReaderCancel=std::function<bool()>;
class ReaderCancelled final:public std::runtime_error {public:ReaderCancelled():std::runtime_error("Reader request cancelled"){};};
void checkReaderCancelled(const ReaderCancel&);
// One worker-owned read-only handle. Implementations retain the opened object;
// no extraction, directory creation, shell call, network or path reopening.
class ReaderBytes {
public:virtual ~ReaderBytes()=default;virtual std::uint64_t size()const=0;
    virtual void read(std::uint64_t offset,std::span<std::uint8_t>destination)=0;
};
class ReaderZIP final {
public:
    struct Entry {std::string name;std::uint16_t method{},flags{};std::uint32_t crc{},compressed{},size{};std::uint64_t offset{};};
    static constexpr std::size_t maximumEntries=8192,maximumEntryBytes=32*1024*1024,maximumInflatedBytes=512*1024*1024;
    explicit ReaderZIP(std::shared_ptr<ReaderBytes>,ReaderCancel={});
    std::vector<std::uint8_t>data(std::string_view name,std::size_t limit=maximumEntryBytes,const ReaderCancel& cancelled={})const;
    const std::map<std::string,Entry,std::less<>>&entries()const noexcept{return entries_;}
    static bool safePath(std::string_view)noexcept;
    static std::string resolve(std::string_view href,std::string_view base);
private:
    std::shared_ptr<ReaderBytes>file_;std::map<std::string,Entry,std::less<>>entries_;std::uint64_t directoryOffset_{};
    std::vector<std::uint8_t>read(std::uint64_t,std::size_t)const;
};
} // namespace endfield::modules
