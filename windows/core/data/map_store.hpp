#pragma once
#include "core/data/data_store.hpp"
#include "modules/map_state.hpp"
#include <functional>

namespace ehud::data {
struct MapArchive {
    endfield::modules::MapSnapshot snapshot;
    Json preserved;
    unsigned sourceVersion{};
};
// Original Foundation Codable envelope and epoch. The returned camera already
// has the source v1/untouched-v2/v3 migration applied; custom cameras survive.
// Additive unknown root, viewport and retained-pin fields stay in preserved.
MapArchive decodeMapArchive(std::string_view bytes);
std::string encodeMapArchive(const endfield::modules::MapSnapshot&,
    const Json&preserved=Json::Object{});

class MapStore final {
public:
    static constexpr std::size_t maximumArchiveBytes=256*1024;
    // Explicit application root; uses WorldMap/map.json. No default directory,
    // timer or worker. Construction reads once and atomically migrates v1–3.
    // Corrupt/newer/nonordinary data throws without replacing original bytes.
    explicit MapStore(std::filesystem::path appRoot);
    MapStore(const MapStore&)=delete;MapStore&operator=(const MapStore&)=delete;
    const endfield::modules::MapSnapshot&value()const noexcept{return value_;}
    const Json&preserved()const noexcept{return document_;}
    const std::filesystem::path&path()const noexcept{return path_;}
    // Source commit-before-publish semantics. Call only at discrete pin edits
    // and gesture completion, never per pointer/frame. Whole-byte CAS plus
    // atomic replace; throws on an external change or failed write and retains
    // the previous in-memory snapshot. Equal snapshots perform no filesystem
    // operation. This small synchronous seam fits MapState::persistence.commit;
    // the HUD hands its writes to a writer (below): the replace took 12-24 ms
    // of a pin or camera click on the HUD thread.
    bool replace(const endfield::modules::MapSnapshot&);
    // Deferred writes (the host's app::LatestFileSave): with a writer, replace
    // encodes the archive as before, hands it over and publishes the snapshot
    // at once; the write runs later. The writer starts from persisted(), the
    // archive as this store last read or wrote it. A deferred write that
    // failed returns the store to the archive on disk (restore).
    using Writer=std::function<void(std::string)>;
    void setWriter(Writer value){writer_=std::move(value);}
    const std::optional<std::string>&persisted()const noexcept{return persisted_;}
    void restore(const std::optional<std::string>&onDisk);
    // Archives replaced on the calling thread, and handed to the writer.
    struct Writes {std::uint64_t atOnce{},handed{};};
    Writes writes()const noexcept{return writes_;}
private:
    std::filesystem::path path_;
    endfield::modules::MapSnapshot value_;
    Json document_{Json::Object{}};
    std::optional<std::string>persisted_;
    Writer writer_;
    Writes writes_;
    void commit(endfield::modules::MapSnapshot);
};
}
