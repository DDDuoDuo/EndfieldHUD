#pragma once
// Synthetic update sources for tests and hidden coverage (portable): an
// in-process Transport serving registered routes and a GitHub release-list
// builder. Nothing here reaches the network; files are written only where a
// request names one (a temporary or coverage folder).
#include "packaging/updater.hpp"
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace endfield::packaging::updates {
class RouteTransport final : public Transport {
public:
    struct Route {
        int status{200};
        Headers headers;
        std::string body;
        TransportResponse::Failure failure{TransportResponse::Failure::none};
    };
    // `notify` runs when a completion becomes ready (like the WinHTTP
    // transport's notice); drain() runs them.
    explicit RouteTransport(std::function<void()> notify = {}) : notify_(std::move(notify)) {}
    void serve(std::string url, Route);  // the answer whenever no queued one is left
    void queue(std::string url, Route);  // one-shot answers, in order, before the served one
    void clear();
    const std::vector<TransportRequest>& requests() const noexcept { return requests_; }
    void forget() { requests_.clear(); }
    // Holds completions back until release() (cancellation and timeout tests).
    void hold(bool value) { held_ = value; }
    void release();

    std::uint64_t start(TransportRequest, Completion) override;
    void cancel(std::uint64_t) noexcept override;
    std::size_t drain() override;
    std::optional<double> nextDeadline() const override;
    bool deadline(double now) override;
    std::size_t active() const override { return pending_.size(); }
    void setClock(std::function<double()> clock) { clock_ = std::move(clock); }
private:
    struct Pending { std::uint64_t id{}; TransportRequest request; Completion completion; double started{}; bool ready{}; TransportResponse response; };
    std::function<void()> notify_;
    std::function<double()> clock_;
    std::map<std::string, std::deque<Route>> queued_;
    std::map<std::string, Route> served_;
    std::vector<TransportRequest> requests_;
    std::deque<Pending> pending_;
    std::uint64_t next_{};
    bool held_{};
    TransportResponse answer(const TransportRequest&);
};

// A GitHub /releases list entry (only the fields the checker reads).
struct FeedAsset {
    std::string name, url;
    std::uint64_t size{};
    std::string state{"uploaded"};
};
struct FeedRelease {
    std::string tag, name, url; // url empty: the canonical release page
    bool draft{}, prerelease{}, published{true};
    std::vector<FeedAsset> assets;
};
std::string releaseFeed(const std::vector<FeedRelease>&);
// The two installer assets of a Windows release, named and linked like
// packaging/stage_release.cmake artifacts uploaded to that release.
std::vector<FeedAsset> installerAssets(std::string_view tag, std::string_view release, int build, std::uint64_t setupBytes, std::uint64_t sumsBytes);
std::string sumsText(const std::vector<std::pair<std::string, std::string>>& digestAndName);
// A minimal unsigned PE32+ x64 image (headers only, never executed) that
// packaging/package_io's writeSetup accepts as a setup stub.
std::vector<std::uint8_t> syntheticSetupStub();
} // namespace endfield::packaging::updates
