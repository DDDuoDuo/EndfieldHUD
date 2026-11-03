#pragma once
#include "app/utility_executor.hpp"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace endfield::app {
// One file's whole-content saves off the owner thread (Notes/centre.json,
// WorldMap/map.json; the Mac writes both .atomic on the main thread, here the
// durable replace and its flush took 5-20 ms of a click). The store publishes
// a change at once and hands its encoded file to save(). One save runs at a
// time on the utility executor and the latest bytes handed meanwhile wait
// behind it (an earlier waiting one is superseded: the last write wins). Each
// save is the synchronous one (detail::replaceFile: compare against the bytes
// the last successful save wrote, temporary file, flush, replace), so the disk
// only ever holds a whole file. A failed save reports on the owner thread
// (`failed`, inside a utility drain); the next save again expects the bytes
// last known on disk. An executor that cannot take the save writes it at
// once, on this thread. Owner thread only. Destruction waits for a running
// save and writes the waiting bytes at once, without callbacks.
class LatestFileSave final {
public:
    using Failed=std::function<void(std::exception_ptr)>;
    // queued: saves the executor took; atOnce: written on this thread (a full
    // executor); written / failed: saves that finished either way.
    struct Stats {std::uint64_t queued{},atOnce{},written{},failed{};};
    // onDisk: the file as its store last read or wrote it (none: no file).
    LatestFileSave(UtilityExecutor&,std::filesystem::path,std::size_t maximum,std::optional<std::string> onDisk,Failed={});
    ~LatestFileSave();
    LatestFileSave(const LatestFileSave&)=delete;
    LatestFileSave&operator=(const LatestFileSave&)=delete;
    void save(std::string bytes);
    // Drops the waiting bytes (a failure handler that returned its store to
    // the file on disk: they were built on the change that failed).
    void discardWaiting()noexcept;
    // The quit / session-end barrier: returns once no save runs or waits; the
    // utility completions (this file's failures among them) run meanwhile.
    void flush();
    bool idle()const noexcept;
    const std::optional<std::string>&onDisk()const noexcept; // as of the last finished save
    Stats stats()const noexcept;
private:
    struct Impl;std::shared_ptr<Impl>impl_;
};
}
