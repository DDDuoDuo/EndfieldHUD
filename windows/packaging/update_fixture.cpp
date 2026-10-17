#include "packaging/update_fixture.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <fstream>
#include <system_error>

namespace endfield::packaging::updates {
using ehud::data::Json;

void RouteTransport::serve(std::string url, Route route) { served_[std::move(url)] = std::move(route); }
void RouteTransport::queue(std::string url, Route route) { queued_[std::move(url)].push_back(std::move(route)); }
void RouteTransport::clear() { served_.clear(); queued_.clear(); }

TransportResponse RouteTransport::answer(const TransportRequest& request) {
    TransportResponse out;
    out.response.url = request.url;
    Route route;
    if (auto q = queued_.find(request.url); q != queued_.end() && !q->second.empty()) {
        route = std::move(q->second.front());
        q->second.pop_front();
    } else if (auto s = served_.find(request.url); s != served_.end()) {
        route = s->second;
    } else {
        route.status = 404;
    }
    if (route.failure != TransportResponse::Failure::none) { out.failure = route.failure; return out; }
    out.response.status = route.status;
    out.response.headers = route.headers;
    if (route.body.size() > request.maximumBytes) { out.failure = TransportResponse::Failure::tooLarge; return out; }
    out.bytes = route.body.size();
    if (request.file.empty()) { out.response.body = std::move(route.body); return out; }
    if (route.status != 200) return out; // file mode: only a 200 body is stored
    std::error_code error;
    if (std::filesystem::exists(std::filesystem::symlink_status(request.file, error))) { out.failure = TransportResponse::Failure::file; return out; }
    std::ofstream file(request.file, std::ios::binary | std::ios::trunc);
    file.write(route.body.data(), static_cast<std::streamsize>(route.body.size()));
    if (!file) { file.close(); std::filesystem::remove(request.file, error); out.failure = TransportResponse::Failure::file; }
    return out;
}
std::uint64_t RouteTransport::start(TransportRequest request, Completion completion) {
    requests_.push_back(request);
    Pending p;
    p.id = ++next_;
    p.started = clock_ ? clock_() : 0;
    p.request = std::move(request);
    p.completion = std::move(completion);
    if (!held_) { p.response = answer(p.request); p.ready = true; }
    pending_.push_back(std::move(p));
    if (!held_ && notify_) notify_();
    return next_;
}
void RouteTransport::release() {
    held_ = false;
    bool any{};
    for (auto& p : pending_) if (!p.ready) { p.response = answer(p.request); p.ready = true; any = true; }
    if (any && notify_) notify_();
}
void RouteTransport::cancel(std::uint64_t id) noexcept {
    std::erase_if(pending_, [id](const Pending& p) { return p.id == id; });
}
std::size_t RouteTransport::drain() {
    std::size_t count{};
    while (true) {
        auto it = std::find_if(pending_.begin(), pending_.end(), [](const Pending& p) { return p.ready; });
        if (it == pending_.end()) return count;
        auto completion = std::move(it->completion);
        auto response = std::move(it->response);
        pending_.erase(it);
        completion(std::move(response));
        ++count;
    }
}
std::optional<double> RouteTransport::nextDeadline() const {
    std::optional<double> next;
    for (const auto& p : pending_) if (!p.ready) next = next ? std::min(*next, p.started + p.request.timeout) : p.started + p.request.timeout;
    return next;
}
bool RouteTransport::deadline(double now) {
    bool expired{};
    for (auto& p : pending_)
        if (!p.ready && now >= p.started + p.request.timeout) {
            p.response = {};
            p.response.failure = TransportResponse::Failure::timeout;
            p.response.response.url = p.request.url;
            p.ready = true;
            expired = true;
        }
    return expired;
}

std::string releaseFeed(const std::vector<FeedRelease>& releases) {
    Json::Array list;
    for (const auto& r : releases) {
        Json::Object e;
        e["tag_name"] = r.tag;
        e["name"] = r.name.empty() ? Json(nullptr) : Json(r.name);
        e["html_url"] = r.url.empty() ? "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/" + r.tag : r.url;
        e["draft"] = r.draft;
        e["prerelease"] = r.prerelease;
        e["published_at"] = r.published ? Json("2026-10-10T00:00:00Z") : Json(nullptr);
        Json::Array assets;
        for (const auto& a : r.assets) {
            Json::Object asset;
            asset["name"] = a.name;
            asset["browser_download_url"] = a.url;
            asset["size"] = static_cast<std::int64_t>(a.size);
            asset["state"] = a.state;
            asset["content_type"] = "application/octet-stream";
            assets.emplace_back(std::move(asset));
        }
        e["assets"] = std::move(assets);
        list.emplace_back(std::move(e));
    }
    return Json(std::move(list)).encode();
}
std::vector<FeedAsset> installerAssets(std::string_view tag, std::string_view release, int build, std::uint64_t setupBytes, std::uint64_t sumsBytes) {
    const auto setup = setupAssetName(release, build), sums = sumsAssetName(release, build);
    return {{setup, releaseDownloadURL(tag, setup), setupBytes, "uploaded"}, {sums, releaseDownloadURL(tag, sums), sumsBytes, "uploaded"}};
}
std::vector<std::uint8_t> syntheticSetupStub() {
    // One section [512, 4096): the image ends exactly at the end of the file.
    std::vector<std::uint8_t> h(4096, 0);
    const auto u16 = [&](std::size_t at, std::uint16_t v) { h[at] = static_cast<std::uint8_t>(v); h[at + 1] = static_cast<std::uint8_t>(v >> 8); };
    const auto u32 = [&](std::size_t at, std::uint32_t v) { for (int n = 0; n < 4; ++n) h[at + n] = static_cast<std::uint8_t>(v >> (8 * n)); };
    h[0] = 'M'; h[1] = 'Z';
    u32(0x3c, 0x80);
    h[0x80] = 'P'; h[0x81] = 'E';
    u16(0x84, 0x8664);
    u16(0x86, 1);
    u16(0x94, 240);
    const std::size_t optional = 0x98;
    u16(optional, 0x20b);
    u32(optional + 108, 16);
    const auto section = optional + 240;
    u32(section + 16, 4096 - 512);
    u32(section + 20, 512);
    return h;
}
std::string sumsText(const std::vector<std::pair<std::string, std::string>>& lines) {
    std::string out;
    for (const auto& [digest, name] : lines) out += digest + "  " + name + "\n";
    return out;
}
} // namespace endfield::packaging::updates
