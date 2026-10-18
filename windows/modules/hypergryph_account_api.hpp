#pragma once
#include "modules/hypergryph_account_model.hpp"
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

// Port of HypergryphAccountAPI.swift: fixed read-only endpoints, byte-identical
// request canonicalization and signing, signing-token recovery (10000), clock
// calibration (10003), bounded response interpretation and payload decoding.
// Everything runs on the owner thread; a transport delivers completions only
// from the owner's drain, so there is no lock, timer, worker or polling here.
namespace endfield::modules::hypergryph {
template<class T> class Outcome {
public:
    Outcome(T value):value_(std::move(value)) {}
    Outcome(Error error):value_(error) {}
    bool ok() const noexcept {return std::holds_alternative<T>(value_);}
    const T& value() const {return std::get<T>(value_);}
    T& value() {return std::get<T>(value_);}
    Error error() const {return ok()?Error{}:std::get<Error>(value_);}
private:
    std::variant<T,Error> value_;
};
using EnvelopeOutcome=Outcome<Json>;

class AccountRequest {
public:
    virtual ~AccountRequest()=default;
    virtual void cancel()=0;
};
using RequestHandle=std::shared_ptr<AccountRequest>;

struct HttpRequest {
    std::string host,path,query; // query is the exact percent-encoded string that is signed
    std::vector<std::pair<std::string,std::string>> queryItems; // unescaped values (redaction only)
    std::vector<std::pair<std::string,std::string>> headers;    // Mac setValue(_:forHTTPHeaderField:) order
    std::string url() const;
    std::optional<std::string> header(std::string_view name) const; // case-insensitive
};
inline constexpr std::size_t maximumResponseBytes=8*1024*1024;
inline constexpr double requestTimeoutSeconds=15,resourceTimeoutSeconds=20;
// Raw transport result. The transport owns HTTPS, the 8 MiB cap (Content-Length
// and streamed bytes), redirect rejection, timeouts and cancellation; it sets
// failure for those and never forwards a credential to another host.
struct HttpOutcome {std::optional<Error> failure;int status{200};std::string body;};
class HttpTransport {
public:
    virtual ~HttpTransport()=default;
    // One GET. completion runs once on the owner thread (never inside start()).
    // After cancel() the completion may be skipped or receive `cancelled`.
    virtual RequestHandle start(HttpRequest,std::function<void(HttpOutcome)> completion)=0;
    // DispatchQueue.main.async equivalent: runs on the next owner drain.
    virtual void post(std::function<void()>)=0;
};

enum class EndfieldCardRoute {web,app,authenticatedSelf};
std::string_view endfieldCardPath(EndfieldCardRoute) noexcept;
bool requiresCommunityUser(EndfieldCardRoute) noexcept;
bool allowedPath(std::string_view) noexcept;
std::string queryEscape(std::string_view);
std::string signature(std::string_view path,std::string_view query,std::string_view timestamp,
                      std::string_view signingToken,const std::optional<std::string>& deviceID);
// makeRequest: unsigned when credentials is null (cred-only refresh).
HttpRequest makeRequest(Region,std::string_view path,const std::vector<std::pair<std::string,std::string>>& query,
                        const std::optional<std::string>& cred,const Credentials* credentials,Time now,double clockOffset);
// HypergryphBoundedRequest completion: HTTP status gate and JSON dictionary.
EnvelopeOutcome interpretResponse(const HttpOutcome&);
EnvelopeOutcome payload(const Json& envelope);
Outcome<std::vector<Role>> decodeBindings(const Json& data,Region);
Outcome<Snapshot> decodeProfile(const Json& data,const Role&,Time observedAt);
std::int64_t arknightsAP(std::int64_t current,std::int64_t maximum,std::optional<Time> fullRecovery,
                         std::optional<Time> lastRecovery,Time reference);
std::string redactedServiceMessage(const Json& message,const HttpRequest&,const std::vector<std::string>& secrets);
std::optional<bool> jsonBool(const Json&); // Swift `as? Bool` on JSONSerialization values

struct Diagnostic {DiagnosticEndpoint endpoint;std::int64_t code{};DiagnosticReason reason;};

// The controller's service seam (HypergryphAccountServing).
class AccountService {
public:
    virtual ~AccountService()=default;
    virtual RequestHandle refreshCredentials(const std::string& cred,Region,std::function<void(Outcome<Credentials>)>)=0;
    virtual RequestHandle refreshCredentials(const Credentials&,Region,std::function<void(Outcome<Credentials>)>);
    virtual RequestHandle bindings(const Credentials&,Region,std::function<void(Outcome<std::vector<Role>>)>)=0;
    virtual RequestHandle profile(const Role&,const Credentials&,std::function<void(Outcome<Snapshot>)>)=0;
};

class HypergryphAccountClient final:public AccountService {
public:
    HypergryphAccountClient(HttpTransport&,std::function<Time()> now,EndfieldCardRoute=EndfieldCardRoute::authenticatedSelf);
    ~HypergryphAccountClient() override;
    HypergryphAccountClient(const HypergryphAccountClient&)=delete;
    HypergryphAccountClient& operator=(const HypergryphAccountClient&)=delete;
    RequestHandle refreshCredentials(const std::string& cred,Region,std::function<void(Outcome<Credentials>)>) override;
    RequestHandle refreshCredentials(const Credentials&,Region,std::function<void(Outcome<Credentials>)>) override;
    RequestHandle bindings(const Credentials&,Region,std::function<void(Outcome<std::vector<Role>>)>) override;
    RequestHandle profile(const Role&,const Credentials&,std::function<void(Outcome<Snapshot>)>) override;
    // Explicit diagnostics only (null in the HUD). No response text or headers
    // other than the redacted service sentence ever cross this boundary.
    void setDiagnostics(std::function<void(const Diagnostic&)>,
                        std::function<void(DiagnosticEndpoint,std::int64_t,const std::string&)> serviceMessage={});
private:
    struct Impl;std::shared_ptr<Impl> impl_;
};
}
