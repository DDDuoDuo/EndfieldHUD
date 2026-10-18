#pragma once
// Bounded disk cache of accepted Now Playing downloads (Windows addition; the
// Mac source keeps only in-memory caches). A catalog search, a lyric document
// or a cover answered once is answered again from disk for the same request,
// across presentations and launches, so a returning track shows its cover and
// lyrics without network round trips.
//
//   <directory>\<16 hex digits>.np    one accepted body per request
//
// The file name is a hash of the request (kind and exact URL); the file holds
// a header line, the request URL (so a hash collision is a miss) and the body.
// Bounded by entry count and total bytes; the least recently used entries are
// removed first. Only bodies a transport accepted (status, media type, size)
// are stored, never a failure. Files are read and written only on the shared
// utility executor; the owner thread keeps a small in-memory index (hash,
// size, last use). The directory lives under the per-user data root
// (%LOCALAPPDATA%\EndfieldHUD\v1, current-user ACL).
#include "app/utility_executor.hpp"
#include "native/now_playing_web.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>

namespace endfield::native {
class NowPlayingWebCache final {
public:
    struct Options {
        std::filesystem::path directory;
        std::size_t maximumEntries{256};
        std::uintmax_t maximumBytes{16u*1024*1024};
        // Which accepted bodies are kept (null: every one). Runs on the
        // executor before a write and after a read; a body it refuses is never
        // written, and a stored one it refuses reads as a miss and is removed
        // (so an answer an earlier build stored heals on its first read).
        std::function<bool(const NowPlayingWeb::Request&,std::span<const std::uint8_t>)>keep;
    };
    struct Stats {
        std::size_t entries{};std::uintmax_t bytes{};
        std::uint64_t hits{},misses{},stores{},evictions{},failures{},backpressure{},refused{};
        bool indexed{};
    };
    // The executor and directory outlive nothing here: queued work holds the
    // cache state weakly, and destruction discards queued work.
    NowPlayingWebCache(app::UtilityExecutor&,Options);
    ~NowPlayingWebCache();
    NowPlayingWebCache(const NowPlayingWebCache&)=delete;
    NowPlayingWebCache&operator=(const NowPlayingWebCache&)=delete;
    // Owner thread. Starts the one directory scan; lookups miss until it ends.
    void prepare();
    // Owner thread: the index lists this request (after prepare()).
    bool contains(const NowPlayingWeb::Request&)const;
    // Owner thread: reads the entry on the executor; `done` runs on the owner
    // thread (executor drain) with the body, or null for a missing, damaged or
    // oversized entry (which is then forgotten). The returned function cancels
    // (done never runs afterwards). A full queue answers null synchronously.
    std::function<void()>load(const NowPlayingWeb::Request&,std::function<void(modules::NowPlayingBody)>done);
    // Owner thread: writes an accepted body on the executor and evicts the
    // least recently used entries beyond the bounds. Dropped when the queue
    // is full (no retry) or the body exceeds the request kind's limit.
    void store(const NowPlayingWeb::Request&,modules::NowPlayingBody);
    Stats stats()const;
    // The file name of a request (exposed for tests).
    static std::string fileName(const NowPlayingWeb::Request&);
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
