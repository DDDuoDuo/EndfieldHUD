// Account Linking area: the hidden-coverage providers behind the production
// signed client and login policy. The coverage fixture must be the generator's
// output for the current Mac oracle fixture; the in-process community host
// answers only the fixed endpoints and never completes inside start(); the
// fake official-site host follows the WebView2 presenter's completion rules.
// No network, browser, credential store or real account is used.
#ifdef _WIN32
#include "app/areas/account_coverage.hpp"
#include "core/data/file_io.hpp"
#include "modules/hypergryph_account_crypto.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
namespace h = endfield::modules::hypergryph;
namespace areas = endfield::app::areas;
using ehud::data::Json;
unsigned checks{};
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }

void fixture(const std::filesystem::path& coverage, const std::filesystem::path& source) {
    const auto sourceBytes = ehud::data::detail::readFile(source, 8 * 1024 * 1024);
    const auto coverageBytes = ehud::data::detail::readFile(coverage, 256 * 1024);
    check(sourceBytes && coverageBytes, "Both fixtures are readable");
    const auto digest = h::sha256(std::string_view(*sourceBytes));
    check(Json::parse(*coverageBytes)["provenance"]["sourceSHA256"].string() == h::lowercaseHex(digest),
          "The coverage fixture was generated from the current Mac oracle fixture (rerun tools/hypergryph_account_coverage_fixture.py)");
    const auto f = areas::loadAccountCoverageFixture(coverage);
    check(f.region == h::Region::mainland && f.site == h::login_policy::siteURL(f.region) && !f.navigation.empty() && !f.popups.empty(),
          "The fake site host covers the official mainland site, navigation and popups");
    check(f.messages.size() >= 2 && f.messages.back().session && !f.messages.front().session, "Rejected bridge messages precede the accepted session");
    check(f.expired.status == 401 && f.expected["roles"].array().size() == 2, "Expired envelope and Mac role expectations are present");
}

void communityHost(const std::filesystem::path& coverage) {
    const auto f = areas::loadAccountCoverageFixture(coverage);
    areas::SyntheticCommunityHost host(f.responses);
    h::HypergryphAccountClient client(host, [&f] { return f.now; });
    const h::Credentials credentials{"C1", "T1", std::string("D1")};
    std::optional<h::Outcome<std::vector<h::Role>>> roles;
    auto request = client.bindings(credentials, h::Region::mainland, [&](h::Outcome<std::vector<h::Role>> value) { roles = std::move(value); });
    check(request && !roles && host.queued() == 1 && host.activeRequests() == 1, "Completions never run inside start()");
    check(host.drain() == 1 && roles && roles->ok() && roles->value().size() == 2 && host.activeRequests() == 0, "The bindings envelope decodes on the owner drain");
    const auto sent = host.requests().front();
    check(sent.host == "zonai.skland.com" && sent.path == "/api/v1/game/player/binding" && sent.header("sign") && sent.header("sign")->size() == 32 &&
          sent.header("cred") == std::optional<std::string>("C1") && sent.header("dId") == std::optional<std::string>("D1"), "The production client signs the request it sends");
    check(roles->value()[0].id() == f.expected["roles"].array()[0]["id"].string(), "The decoded role matches the Mac decode");
    // Expired session through a scripted one-shot override.
    host.script("/api/v1/game/endfield/card/detail", f.expired);
    std::optional<h::Outcome<h::Snapshot>> card;
    (void)client.profile(roles->value()[0], credentials, [&](h::Outcome<h::Snapshot> value) { card = std::move(value); });
    host.drain();
    check(card && !card->ok() && card->error().kind == h::ErrorKind::authenticationExpired, "HTTP 401 code 10002 maps to authenticationExpired");
    card.reset();
    (void)client.profile(roles->value()[0], credentials, [&](h::Outcome<h::Snapshot> value) { card = std::move(value); });
    host.drain();
    check(card && card->ok() && card->value().stamina && card->value().stamina->maximum == 360, "The override is consumed once; the fixture envelope returns");
    // Anything but the fixed endpoints is refused, never answered.
    h::HttpRequest other;
    other.host = "example.invalid";
    other.path = "/api/v1/game/player/binding";
    std::optional<h::HttpOutcome> outcome;
    (void)host.start(other, [&](h::HttpOutcome value) { outcome = std::move(value); });
    host.drain();
    check(host.rejected() == 1 && outcome && outcome->failure && outcome->failure->kind == h::ErrorKind::transport, "Unknown hosts are refused");
    // A cancelled request delivers nothing and releases its slot.
    bool delivered{};
    auto cancelled = host.start(sent, [&](h::HttpOutcome) { delivered = true; });
    cancelled->cancel();
    host.drain();
    check(!delivered && host.activeRequests() == 0, "Cancellation drops the completion");
}

void loginHost(const std::filesystem::path& coverage) {
    const auto f = areas::loadAccountCoverageFixture(coverage);
    areas::SyntheticCommunityHost queue(f.responses);
    areas::SyntheticLoginHost login(queue, f.nonce);
    std::vector<std::pair<bool, h::LoginFailure>> results;
    const auto record = [&](std::optional<h::LoginResult> value, h::LoginFailure failure) { results.emplace_back(value.has_value(), failure); };
    login.present(h::Region::mainland, record);
    check(login.isPresenting() && login.session() && login.session()->nonce() == f.nonce && login.presentations() == 1, "Connect opens one session");
    login.present(h::Region::mainland, record);
    check(results.size() == 1 && results[0] == std::pair{false, h::LoginFailure::alreadyPresenting}, "A second presentation fails synchronously");
    auto* session = login.session();
    check(session->consent(1) && session->allowNavigation(f.site, true, false), "Consent opens the official site");
    const auto& accepted = f.messages.back();
    auto result = session->credentialMessage(Json::parse(accepted.body), accepted.mainFrame, accepted.frame, accepted.page);
    check(result && login.finish(std::move(result), h::LoginFailure::cancelled) && !login.isPresenting() && results.size() == 1,
          "Finishing disposes the session; the completion waits for the owner drain");
    queue.drain();
    check(results.size() == 2 && results[1].first, "The accepted session reaches the controller on the drain");
    login.present(h::Region::mainland, record);
    login.cancel();
    check(results.size() == 3 && results[2] == std::pair{false, h::LoginFailure::cancelled} && !login.isPresenting(), "Cancel completes synchronously");
    const auto& tags = login.diagnostics();
    check(std::find(tags.begin(), tags.end(), "cancelled") != tags.end(), "The cancelled session records its outcome tag");
    check(!login.finish(std::nullopt, h::LoginFailure::cancelled), "Nothing is finished twice");
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        check(argc == 3, "Pass hypergryph-account-coverage.json and hypergryph-account-source.json");
        fixture(argv[1], argv[2]);
        communityHost(argv[1]);
        loginHost(argv[1]);
        std::cout << "PASS " << checks << " account area provider checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#endif
