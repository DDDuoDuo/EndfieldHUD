#pragma once
#include "core/data/file_shelf_store.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
// Optional presentation metadata, captured on the existing audio worker from
// the retained PID+creation-time process handle. The file identity describes
// the current regular file at that process-reported executable path, not a
// proof of which historical bytes Windows mapped into the running process.
// No file handle, icon pixels, persistent shelf record or UI object crosses
// into the snapshot. The shared Shell worker revalidates before extraction.
struct AudioApplicationExecutable {
    std::string path;ehud::data::ShelfFileIdentity identity;
    bool operator==(const AudioApplicationExecutable&)const=default;
};
bool validAudioApplicationExecutable(const AudioApplicationExecutable&)noexcept;
// One process of a helper's ancestry: its executable path (UTF-8, as Windows
// reports it), its creation time (FILETIME ticks), its PID, and whether it is
// an app in its own right (processIsApplication: an app window or an
// AppUserModelID).
struct AudioProcessImage {std::string path;std::uint64_t created{};std::uint32_t pid{};bool application{};};
// The ancestry read above a session's process, nearest parent first.
inline constexpr std::size_t audioHelperAncestorLimit=16;
// The app a helper process plays for (source ff06692: a helper is named after
// the app that contains it; 0585a0e: WebKit's audio service is attributed to
// the app it plays for): the index in `ancestors` (nearest parent first) of
// that app's process, or none when the process is its own app.
// - A process that is an app in its own right (`application`) is never a
//   helper: a game a launcher starts from inside the launcher's folder keeps
//   its own name (the Mac renames only a process with no running app).
// - A helper belongs to the outermost ancestor, each one created before its
//   child, whose executable's folder holds the helper's executable. The walk
//   ends at the first ancestor that is younger, outside that folder, or in
//   `systemFolder` (the Windows folder: Explorer is never an app's owner),
//   and stops at the first ancestor that is an app in its own right: that
//   app is the owner (as Activity joins a helper to the app above it).
// - A WebView2 runtime process (audioWebViewRuntime) plays for the nearest
//   older ancestor outside the runtime, then that host's own app.
// A command-line child of a launcher in another folder keeps its own name.
std::optional<std::size_t>audioHelperOwner(const AudioProcessImage&process,std::span<const AudioProcessImage>ancestors,std::string_view systemFolder);
// The Windows analogue of macOS's system XPC audio service outside any app:
// the shared Edge WebView2 runtime's executable.
bool audioWebViewRuntime(std::string_view path)noexcept;
// Internal provider for SystemServices, not another application service. Exact
// session IDs are backend lease keys; processKey is PID + creation-time identity.
// A backend must never invent process ownership for multi-process/system audio.
struct AudioSessionRecord {
    std::string id,processKey;
    std::wstring name;
    std::uint32_t pid{};
    bool active{},controllable{};
    std::optional<float> volume;
    std::int32_t error{};
    std::optional<AudioApplicationExecutable> executable;
    // IAudioSessionControl2::GetSessionIdentifier: the cross-instance key under
    // which Windows persists an application's mixer level. Used only by the
    // owned-attenuation recovery journal, never to merge processes.
    std::string persistentID;
    // GetApplicationUserModelId of the exact PID+creation-time process, when
    // the process has one (packaged/explicit identity). Never inferred.
    std::string appUserModelID;
    // The exact PID+creation-time process has terminated while this session
    // object is still held. Reported once so owned attenuation can be restored
    // on the still-existing session (Windows persists the level per app); the
    // backend releases the session on its next read. Never listed as an app.
    bool exited{};
    // The app this process plays for when it is a helper (audioHelperOwner):
    // its executable names and draws the Volume row and joins Now Playing.
    // The session's own executable stays the route's process identity.
    std::optional<AudioApplicationExecutable> application;
    // The process's package family name; empty when it has no package.
    std::string packageFamily;
    bool operator==(const AudioSessionRecord&)const=default;
};
// Native float scalars are compared with this tolerance everywhere a readback
// must prove that a value is still the one this owner wrote.
inline constexpr float audioSessionScalarTolerance=2e-6f;
enum class AudioApplicationRouteState { direct,active,failed };
struct AudioApplicationRoute {
    std::string id;
    std::wstring name;
    std::uint32_t pid{};
    bool available{};
    AudioApplicationRouteState state{};
    std::optional<float>gain;
    std::int32_t error{};
    std::optional<AudioApplicationExecutable> executable;
    // Shared by every session of this process; empty when unknown/conflicting.
    std::string appUserModelID;
    // Any of the process's sessions is AudioSessionStateActive (rendering
    // now). Now Playing reads it for window-title players, which report no
    // play state; a play/pause republishes the applications.
    bool rendering{};
    // Shared by every session of this process; reset when they disagree.
    std::optional<AudioApplicationExecutable> application;
    std::string packageFamily;
    bool operator==(const AudioApplicationRoute&)const=default;
};
struct AudioSessionSnapshot {
    std::uint64_t revision{};
    bool paused{true},supported{};
    std::wstring endpoint;
    std::vector<AudioApplicationRoute>applications;
    std::int32_t error{};
    bool operator==(const AudioSessionSnapshot&)const=default;
};
// All methods, construction and destruction are called on the ONE worker.
// Wake may run on native COM callback threads and must be nonblocking. Backend
// pause unregisters notifications but retains leased volume interfaces, allowing
// identity-bound cleanup without a second service or polling. close is terminal.
class AudioSessionBackend {
public:
    using Wake=std::function<void()>;
    virtual ~AudioSessionBackend()=default;
    virtual std::int32_t open(std::wstring_view endpoint,Wake)=0;
    virtual std::int32_t resume(Wake)=0;
    virtual void pause()noexcept=0;
    virtual std::int32_t read(std::vector<AudioSessionRecord>&)=0;
    virtual std::int32_t readVolume(std::string_view,float&)=0;
    virtual std::int32_t writeVolume(std::string_view,float)=0;
    // An owned route whose sessions all expired (its app closed its stream
    // but runs on; source 74a7935 keeps the route of a momentarily silent
    // app) asks after every read whether that exact process (PID + creation
    // time) still runs. The backend keeps watching the process (its handle and
    // exit wake) while it is asked; false when it ended or cannot be told,
    // which ends the route.
    virtual bool processRunning(std::string_view processKey)noexcept=0;
    virtual void close()noexcept=0;
};
// The processes a native backend keeps watching after their last session
// expired, for processRunning. Each read starts a round: a process the routes
// did not ask about since the previous read is released. Value is the
// backend's own handle and exit wake; the bookkeeping stays portable so it is
// tested without native audio.
template<class Value>class AudioIdleProcesses final {
public:
    explicit AudioIdleProcesses(std::size_t limit)noexcept:limit_(limit){}
    // Kept as asked, so it lasts at least until the read after next. A process
    // already kept, or a full table, keeps nothing (nullptr; value is dropped).
    Value*keep(std::string key,Value value){
        if(entries_.size()>=limit_||entries_.find(key)!=entries_.end())return nullptr;
        return &entries_.emplace(std::move(key),Kept{std::move(value),true}).first->second.value;
    }
    // Marks the process asked this round; nullptr when it is not kept.
    Value*ask(std::string_view key)noexcept{const auto found=entries_.find(key);if(found==entries_.end())return nullptr;found->second.asked=true;return &found->second.value;}
    // release(value) runs before a process not asked since the last round is erased.
    template<class Release>void round(Release&&release)noexcept{
        for(auto i=entries_.begin();i!=entries_.end();){if(!i->second.asked){release(i->second.value);i=entries_.erase(i);}else{i->second.asked=false;++i;}}
    }
    template<class F>void each(F&&f)noexcept{for(auto&[_,kept]:entries_)f(kept.value);}
    bool contains(std::string_view key)const noexcept{return entries_.find(key)!=entries_.end();}
    std::size_t size()const noexcept{return entries_.size();}
    void clear()noexcept{entries_.clear();}
private:
    struct Kept{Value value;bool asked{};};
    std::size_t limit_;std::map<std::string,Kept,std::less<>>entries_;
};
// Source-owned attenuation transaction. Direct100% does not alter the user's
// native mixer; an owned route stays listed, at its gain, while its app has
// no session and its process runs (backend processRunning), and the app's
// next session joins it from the baseline its expired one left; each opted-in session remembers its original scalar. Stop only
// restores scalars still equal to this owner’s last write, preserving external
// changes observed before cleanup. Public WASAPI has no atomic compare-and-set;
// a concurrent external write between native read/write cannot be excluded.
// No OS calls, clocks, threads or persistence exist in this typed core.
class AudioSessionRoutes final {
public:
    static constexpr std::size_t maximumSessions=512,maximumApplications=512;
    AudioSessionRoutes();~AudioSessionRoutes();
    AudioSessionRoutes(AudioSessionRoutes&&)noexcept;
    AudioSessionRoutes&operator=(AudioSessionRoutes&&)noexcept;
    AudioSessionRoutes(const AudioSessionRoutes&)=delete;
    AudioSessionRoutes&operator=(const AudioSessionRoutes&)=delete;
    void update(std::span<const AudioSessionRecord>,AudioSessionBackend&);
    std::int32_t setGain(std::string_view,float,AudioSessionBackend&);
    std::int32_t stop(std::string_view,AudioSessionBackend&);
    std::int32_t stopAll(AudioSessionBackend&);
    const std::vector<AudioApplicationRoute>&applications()const noexcept;
private:class Impl;std::unique_ptr<Impl>impl_;
};
// All lifecycle/command/drain calls belong to one owner thread. Notice must not
// destroy/stop this object on its worker; post an owner message instead.
// Caller-owned lifecycle. One sleeping atomic-wait worker, bounded/coalesced
// explicit commands, no timers. The notice runs on that worker and should only
// PostMessage the owner's existing coalesced WM_APP generation. Factory injection
// lets isolated tests exercise lifecycle without activating native audio.
class AudioSessionWorker final {
public:
    using Factory=std::function<std::unique_ptr<AudioSessionBackend>()>;
    using Notice=std::function<void()>;
    explicit AudioSessionWorker(Factory,Notice={});~AudioSessionWorker();
    AudioSessionWorker(const AudioSessionWorker&)=delete;
    AudioSessionWorker&operator=(const AudioSessionWorker&)=delete;
    // Empty endpoint means unsupported, never silently follows another device.
    bool activate(std::wstring endpoint);
    bool pause();
    bool setGain(std::string application,float scalar);
    bool stopApplication(std::string application);
    bool stopApplications();
    bool takeSnapshot(AudioSessionSnapshot&);
    void stop()noexcept;
private:class Impl;std::unique_ptr<Impl>impl_;
};
#ifdef _WIN32
// Constructed on the worker MTA only. Merely obtaining the factory does not
// activate COM/audio; tests must inject their own backend rather than invoke it.
AudioSessionWorker::Factory nativeAudioSessionBackendFactory();
// The ancestry audioHelperOwner reads for a live process: nearest parent
// first, at most audioHelperAncestorLimit, from one process snapshot's
// parent PIDs (`parents`: PID to parent PID). An ancestor is an app in its
// own right when it is in `windows` (readWindowsApplicationWindows) or has an
// AppUserModelID. It stops at an ancestor that cannot be opened or read.
// Exposed for the hidden native check.
std::vector<AudioProcessImage>readWindowsAudioAncestors(std::uint32_t pid,const std::map<std::uint32_t,std::uint32_t>&parents,const std::set<std::uint32_t>&windows);
#endif
} // namespace endfield::native
