#pragma once
#include "modules/notes_media_presentation.hpp"
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace endfield::core {
enum class MediaRequestChannel {image,video,inspection};
using MediaClient=std::uint64_t;
struct ScopedMediaRequest {
    std::string key,path;std::uint64_t revision{};int maximumDimension{512};
    modules::NotesMediaKind kind{modules::NotesMediaKind::image};
    bool firstFrameOnly{},videoPoster{},allowVideoInspection{},workerPoster{};
    std::optional<double>duration;std::shared_ptr<void>accessLease;
    bool operator==(const ScopedMediaRequest&)const=default;
};
struct ScopedMediaBinding {
    MediaClient client{};std::uint64_t generation{};MediaRequestChannel channel{};
    ScopedMediaRequest request;std::string globalKey;
};
// Only request metadata/identities; no IO, callback, clock, thread or decoder.
// Storage limits remain independent. An excessive ACTIVE metadata union fails
// explicitly before mutation, rather than shortening a client's request list.
class MediaRequestCatalog final {
public:
    static constexpr std::size_t maximumClients=32,maximumRequests=10016,
        maximumLocalKeyBytes=300,maximumMetadataBytes=32*1024*1024;
    struct Transaction {
        Transaction(Transaction&&)noexcept;Transaction&operator=(Transaction&&)noexcept;
        Transaction(const Transaction&)=delete;Transaction&operator=(const Transaction&)=delete;
        std::span<const ScopedMediaBinding>aggregate()const noexcept{return aggregate_;}
        bool changed()const noexcept{return changed_;}bool stale()const noexcept{return stale_;}
    private:friend class MediaRequestCatalog;Transaction()=default;
        struct Batch {std::uint64_t generation{};std::vector<ScopedMediaBinding>bindings;
            std::map<std::string,std::size_t,std::less<>>index;};
        using Key=std::pair<MediaClient,MediaRequestChannel>;
        std::map<Key,std::shared_ptr<const Batch>>next_;
        std::vector<ScopedMediaBinding>aggregate_;const MediaRequestCatalog*owner_{};
        std::uint64_t revision_{};bool changed_{},stale_{};
    };
    MediaClient attach();
    Transaction prepare(MediaClient,MediaRequestChannel,std::uint64_t,
        std::span<const ScopedMediaRequest>)const;
    // Staging can be handed to the single native provider; publish ONLY after
    // it accepts the union. A stale/reused transaction never mutates the catalog.
    void commit(Transaction&&);
    std::span<const ScopedMediaBinding>bindings(MediaClient,MediaRequestChannel)const;
    const ScopedMediaBinding*find(MediaClient,MediaRequestChannel,std::string_view localKey)const noexcept;
    std::uint64_t generation(MediaClient,MediaRequestChannel)const noexcept;
    std::uint64_t revision()const noexcept{return revision_;}
    bool detach(MediaClient); // only after all three channels have been cleared
private:
    using Batch=Transaction::Batch;using Key=Transaction::Key;
    std::map<MediaClient,bool>clients_;std::map<Key,std::shared_ptr<const Batch>>batches_;
    MediaClient next_{};std::uint64_t revision_{};
};
} // namespace endfield::core
