#pragma once
#include "modules/reader_model.hpp"
#include <thread>

namespace endfield::modules {
class ReaderRepository {
public:virtual ~ReaderRepository()=default;virtual ReaderLibrary load()=0;virtual void save(const ReaderLibrary&)=0;
};
// Explicit Reader directory, library.json version1. Constructor performs no IO.
// Both load/save belong to one borrowed FIFO file executor. Atomic replacement
// checks original bytes before writes; conflict never changes the in-memory
// baseline, corrupt/newer files are preserved. Never opens referenced books.
class ReaderJSONRepository final:public ReaderRepository {
public:
    explicit ReaderJSONRepository(std::filesystem::path readerDirectory,ReaderPreferences freshDefaults={});
    ReaderLibrary load()override;void save(const ReaderLibrary&)override;
    const std::filesystem::path&path()const noexcept{return path_;}
private:
    std::filesystem::path directory_,path_;ReaderPreferences defaults_;
    std::optional<std::thread::id>owner_;std::optional<std::string>persisted_;
    bool loaded_{};ReaderLibrary value_;void onOwner();
};
} // namespace endfield::modules
