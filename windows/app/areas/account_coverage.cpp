#include "app/areas/account_coverage.hpp"
#ifdef _WIN32
#include "core/data/file_io.hpp"
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace endfield::app::areas {
namespace {
namespace h = modules::hypergryph;
using ehud::data::Json;
void need(bool value, const char* why) { if (!value) throw std::runtime_error(std::string("Account coverage fixture: ") + why); }
double number(const Json& value) { return value.isString() ? std::strtod(value.string().c_str(), nullptr) : value.number(); }
AccountCoverageFixture::Response response(const Json& value) {
    need(value.isObject() && value["status"].isNumber() && value["body"].isString(), "responses carry a status and a body");
    return {static_cast<int>(value["status"].integer()), value["body"].string()};
}
struct Handle final : h::AccountRequest {
    bool cancelled{};
    void cancel() override { cancelled = true; }
};
} // namespace

AccountCoverageFixture loadAccountCoverageFixture(const std::filesystem::path& path) {
    const auto bytes = ehud::data::detail::readFile(path, 256 * 1024);
    need(bytes.has_value(), "readable synthetic fixture");
    const auto json = Json::parse(*bytes);
    AccountCoverageFixture f;
    f.now = number(json["now"]);
    const auto& login = json["login"];
    const auto region = h::regionNamed(login["region"].string());
    need(region.has_value(), "known region");
    f.region = *region;
    f.nonce = login["nonce"].string();
    f.site = login["site"].string();
    for (const auto& row : login["navigation"].array()) f.navigation.push_back({row["url"].string(), row["mainFrame"].boolean(), row["permits"].boolean()});
    for (const auto& row : login["popups"].array()) f.popups.push_back({row["url"].string(), row["source"].string(), row["permits"].boolean()});
    for (const auto& row : login["messages"].array()) {
        AccountCoverageFixture::Message m{row["body"].string(), row["frame"].string(), row["page"].string(), row["mainFrame"].boolean(), std::nullopt};
        if (row["session"].isObject()) {
            const auto& s = row["session"];
            m.session = h::LoginResult{*h::regionNamed(s["region"].string()), s["cred"].string(), s["signingToken"].string(), s["deviceID"].string()};
        }
        f.messages.push_back(std::move(m));
    }
    for (const auto& [endpoint, value] : json["responses"].object()) f.responses.emplace(endpoint, response(value));
    f.expired = response(json["expired"]);
    f.expected = json["expected"];
    need(std::isfinite(f.now) && !f.nonce.empty() && !f.navigation.empty() && !f.messages.empty() && f.messages.back().session &&
         f.responses.size() == 4 && f.expected["roles"].isArray(), "complete login, envelope and expectation sections");
    return f;
}

SyntheticCommunityHost::SyntheticCommunityHost(std::map<std::string, Response, std::less<>> routes) : routes_(std::move(routes)) {}
h::RequestHandle SyntheticCommunityHost::start(h::HttpRequest request, std::function<void(h::HttpOutcome)> completion) {
    auto handle = std::make_shared<Handle>();
    h::HttpOutcome outcome;
    const bool knownHost = request.host == h::apiHost(h::Region::mainland) || request.host == h::apiHost(h::Region::global);
    const auto route = routes_.find(request.path);
    if (!knownHost || !h::allowedPath(request.path) || route == routes_.end()) {
        // Never answered: the production client only reaches the fixed paths.
        ++rejected_;
        outcome.failure = h::Error{h::ErrorKind::transport};
    } else {
        Response chosen = route->second;
        if (auto s = scripted_.find(request.path); s != scripted_.end() && !s->second.empty()) { chosen = std::move(s->second.front()); s->second.pop_front(); }
        outcome.status = chosen.status;
        outcome.body = std::move(chosen.body);
        // WinHTTP gate: 2xx bodies, and 401/403 bodies for the auth mapping.
        if (!(outcome.status >= 200 && outcome.status <= 299) && outcome.status != 401 && outcome.status != 403)
            outcome.failure = h::Error{h::ErrorKind::http, outcome.status};
    }
    requests_.push_back(std::move(request));
    ++inFlight_;
    queue_.push_back([this, handle, outcome = std::move(outcome), completion = std::move(completion)]() mutable {
        --inFlight_;
        if (!handle->cancelled) completion(std::move(outcome));
    });
    return handle;
}
void SyntheticCommunityHost::post(std::function<void()> callback) { queue_.push_back(std::move(callback)); }
std::size_t SyntheticCommunityHost::drain() {
    // Completions may queue further work; each drain runs what is queued now
    // plus what those completions add, bounded like the WinHTTP drain.
    std::size_t ran{};
    while (!queue_.empty() && ran < 256) {
        auto callback = std::move(queue_.front());
        queue_.pop_front();
        callback();
        ++ran;
    }
    return ran;
}
void SyntheticCommunityHost::script(std::string path, Response once) { scripted_[std::move(path)].push_back(std::move(once)); }

h::RequestHandle SyntheticAvatarLoader::load(const std::string&, std::function<void(std::optional<std::vector<std::uint8_t>>)> completion) {
    ++loads_;
    auto handle = std::make_shared<Handle>();
    queue_.post([handle, completion = std::move(completion)] { if (!handle->cancelled) completion(std::nullopt); });
    return handle;
}

void SyntheticLoginHost::present(h::Region region, Completion completion) {
    // WebViewLoginPresenter::present: a second request fails synchronously.
    if (isPresenting()) { completion(std::nullopt, h::LoginFailure::alreadyPresenting); return; }
    session_.emplace(region, nonce_);
    completion_ = std::move(completion);
    ++presentations_;
}
void SyntheticLoginHost::cancel() {
    if (!isPresenting()) return;
    // Mac/WebView cancel: the completion runs synchronously inside cancel().
    session_->finish(h::LoginSession::Outcome::cancelled);
    diagnostics_ = session_->diagnostics();
    session_.reset();
    if (auto callback = std::exchange(completion_, {})) callback(std::nullopt, h::LoginFailure::cancelled);
}
bool SyntheticLoginHost::finish(std::optional<h::LoginResult> result, h::LoginFailure failure) {
    if (!isPresenting()) return false;
    session_->finish(result ? h::LoginSession::Outcome::completed
                            : failure == h::LoginFailure::expired ? h::LoginSession::Outcome::expired : h::LoginSession::Outcome::cancelled);
    diagnostics_ = session_->diagnostics();
    session_.reset();
    // Like the presenter's finishMessage: delivered on the next owner drain.
    const auto reason = result ? h::LoginFailure::cancelled : failure;
    queue_.post([callback = std::exchange(completion_, {}), result = std::move(result), reason]() mutable {
        if (callback) callback(std::move(result), reason);
    });
    return true;
}
} // namespace endfield::app::areas
#endif
