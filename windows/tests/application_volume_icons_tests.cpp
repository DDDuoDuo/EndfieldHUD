// Volume app-row icons on the host's shared utility queue
// (native/volume_icon_provider.hpp VolumeQueuedIconProvider), the built-in
// Volume owner's icon path in EndfieldHUD.exe: one bounded job at a time on
// the queue's existing worker, the plan's exact key and revision in the
// cache, no repeated extraction, a full queue retried on capacity, hidden
// plans and destroyed providers ignoring late completions, and the default
// extractor reading an executable's own icon resource only while the file
// still has the process's identity. Real files: the Windows shell executable
// (read only) and this test executable (no icon resource). No window, user
// data, audio session or Shell icon handler.
#include "app/utility_executor.hpp"
#include "native/file_shelf_files.hpp"
#include "native/volume_icon_provider.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace n = endfield::native;
namespace {
unsigned checks{};
void check(bool value, const char* what) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL after %u: %s\n", checks, what); std::exit(1); }
}
std::string utf8(const std::wstring& value) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(std::max(count, 0)), '\0');
    if (count > 0) WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), out.data(), count, nullptr, nullptr);
    return out;
}
// A synthetic app row; the token is its ephemeral process UUID.
n::VolumeIconApplication app(unsigned index, std::string path, ehud::data::ShelfFileIdentity identity) {
    const auto digits = std::to_string(index);
    n::VolumeIconApplication a;
    a.applicationID = digits + ":1";
    a.requestToken = "00000000-0000-4000-8000-" + std::string(12 - digits.size(), '0') + digits;
    a.executable = n::AudioApplicationExecutable{std::move(path), std::move(identity)};
    return a;
}
n::VolumeIconApplication synthetic(unsigned index) {
    ehud::data::ShelfFileIdentity identity; identity.objectID[0] = static_cast<std::uint8_t>(index); identity.volumeSerial = 7;
    return app(index, "C:\\EndfieldHUD-synthetic\\app-" + std::to_string(index) + ".exe", identity);
}
n::ShelfIconPixels pixels(std::uint8_t red) {
    n::ShelfIconPixels p; p.width = p.height = 64; p.result = S_OK; p.straightRGBA.assign(64u * 64u * 4u, 0);
    for (std::size_t k = 0; k < p.straightRGBA.size(); k += 4) { p.straightRGBA[k] = red; p.straightRGBA[k + 3] = 255; }
    return p;
}
const n::VolumeIconBinding* bound(const n::VolumeQueuedIconProvider& p, const n::VolumeIconApplication& a) { return p.plan().binding(a.applicationID); }

struct Queue {
    endfield::app::UtilityExecutor executor{[] {}, 2};
    endfield::app::UtilityExecutor::Route route = executor.makeRoute();
    bool full{};
    unsigned submitted{};
    n::VolumeQueuedIconProvider::Submit submit() {
        return [this](std::function<void()> work, n::VolumeQueuedIconProvider::Done done) {
            if (full) return false;
            ++submitted;
            return executor.submit(route, std::move(work), [done = std::move(done)](std::exception_ptr error) { done(bool(error)); });
        };
    }
    void settle() { executor.waitIdle(); (void)executor.drain(); }
};

void plan() {
    Queue queue;
    std::atomic<unsigned> extracted{}, workerCalls{};
    std::atomic<DWORD> worker{};
    unsigned changed{};
    auto provider = std::make_unique<n::VolumeQueuedIconProvider>(queue.submit(), [&] { ++changed; },
        [&](const n::ShelfIconRequest& r) {
            ++extracted; worker = GetCurrentThreadId(); ++workerCalls;
            if (r.identity.objectID[0] == 3) { n::ShelfIconPixels none; none.typeFallback = true; none.result = HRESULT_FROM_WIN32(ERROR_RESOURCE_TYPE_NOT_FOUND); return none; }
            return pixels(static_cast<std::uint8_t>(40 * r.identity.objectID[0]));
        });
    const std::array visible{synthetic(1), synthetic(2), synthetic(3)};
    check(provider->setVisible(visible) && provider->stats().jobs == 1 && provider->stats().inFlight && queue.submitted == 1, "Visible rows become one job on the shared queue");
    check(!bound(*provider, visible[0])->image, "No icon is shown before the job completes");
    check(!provider->setVisible(visible) && queue.submitted == 1, "An unchanged visible set submits nothing");
    queue.settle();
    check(extracted == 3 && worker.load() != GetCurrentThreadId() && changed == 1, "The job extracts on the queue's worker and reports the change once");
    const auto* first = bound(*provider, visible[0]);
    check(first->image && first->image->width() == 64 && provider->images().acquire(first->imageKey, first->revision) == first->image,
        "A loaded icon is bound at the plan's exact key and revision in the cache");
    check(bound(*provider, visible[1])->image && !bound(*provider, visible[2])->image && bound(*provider, visible[2])->typeFallback,
        "An executable without an icon resource keeps the nil icon");
    const auto stats = provider->stats();
    check(stats.jobs == 1 && stats.extractions == 3 && stats.published == 2 && stats.unavailable == 1 && !stats.inFlight, "Counters follow the one job");
    // Scrolling: one new row is the only extraction; the others are cached or answered.
    const std::array scrolled{synthetic(2), synthetic(3), synthetic(4)};
    check(provider->setVisible(scrolled) && bound(*provider, scrolled[0])->image && provider->stats().jobs == 2, "A row entering the window is requested; cached rows bind at once");
    queue.settle();
    check(extracted == 4 && bound(*provider, scrolled[2])->image && changed == 2, "Only the entering row was extracted");
    // Hidden while a job runs: the completion only fills the cache.
    const std::array later{synthetic(5)};
    check(provider->setVisible(later) && provider->stats().inFlight && provider->hide() && !provider->plan().active(), "Hiding withdraws the visible set");
    queue.settle();
    check(extracted == 5 && changed == 2 && provider->stats().published == 4, "A late completion of a hidden plan changes no row but keeps the icon");
    check(provider->setVisible(later) && bound(*provider, later[0])->image && provider->stats().jobs == 3, "Showing again binds the kept icon without a job");
    // A full queue: nothing runs until capacity returns.
    queue.full = true;
    const std::array blocked{synthetic(6)};
    check(provider->setVisible(blocked) && !provider->stats().inFlight && provider->stats().backpressure == 1, "A full queue leaves the request waiting");
    queue.full = false; provider->queueCapacityAvailable();
    check(provider->stats().inFlight && provider->stats().jobs == 4, "Queue capacity sends the waiting request");
    queue.settle();
    check(bound(*provider, blocked[0])->image != nullptr, "The waiting request loads");
    // Retry advances the revision and extracts again.
    const auto old = bound(*provider, blocked[0])->revision;
    check(provider->setVisible(blocked, true) && bound(*provider, blocked[0])->revision > old && provider->stats().jobs == 5, "An explicit retry loads a new revision");
    queue.settle();
    check(bound(*provider, blocked[0])->image != nullptr && bound(*provider, blocked[0])->image->revision() > old, "The retry's pixels carry the new revision");
    // Destroyed with a job in flight: the completion does nothing.
    const std::array last{synthetic(7)};
    check(provider->setVisible(last) && provider->stats().inFlight, "A job is in flight");
    provider.reset();
    queue.settle();
    check(workerCalls == 8, "The orphaned job finished on the worker without touching the destroyed provider");
    queue.executor.invalidate(queue.route);
    queue.executor.shutdown();
}

void extractor() {
    std::wstring windows(MAX_PATH, L'\0');
    windows.resize(GetWindowsDirectoryW(windows.data(), static_cast<UINT>(windows.size())));
    const auto shell = utf8(windows + L"\\explorer.exe");
    std::wstring self(32768, L'\0');
    self.resize(GetModuleFileNameW(nullptr, self.data(), static_cast<DWORD>(self.size())));
    const auto test = utf8(self);
    const n::NativeFileShelfFiles files;
    const auto shellIdentity = files.acquire(shell).metadata().identity;
    const auto testIdentity = files.acquire(test).metadata().identity;
    const auto request = [](const n::VolumeIconApplication& a) {
        n::VolumeIconPlan plan; const std::array one{a}; check(plan.setVisible(one) && plan.requests().size() == 1, "One request per executable"); return plan.requests()[0];
    };
    const auto icon = n::volumeExecutableIcon(request(app(1, shell, shellIdentity)));
    check(SUCCEEDED(icon.result) && !icon.typeFallback && icon.width == 64 && icon.height == 64 && icon.straightRGBA.size() == 64u * 64u * 4u &&
        std::any_of(icon.straightRGBA.begin(), icon.straightRGBA.end(), [](std::uint8_t v) { return v != 0; }), "The shell executable's own icon resource is read at 64 px");
    const auto none = n::volumeExecutableIcon(request(app(2, test, testIdentity)));
    check(FAILED(none.result) && none.typeFallback && none.straightRGBA.empty(), "An executable without an icon resource gives no pixels");
    auto other = shellIdentity; other.objectID[0] ^= 1;
    const auto replaced = n::volumeExecutableIcon(request(app(3, shell, other)));
    check(FAILED(replaced.result) && !replaced.typeFallback && replaced.straightRGBA.empty(), "A path that names another file than the process's executable gives no icon");
    const auto missing = n::volumeExecutableIcon(request(app(4, "C:\\EndfieldHUD-synthetic\\missing.exe", shellIdentity)));
    check(FAILED(missing.result) && missing.straightRGBA.empty(), "A missing executable gives no icon");
    // The default extractor through the provider and a real queue.
    Queue queue;
    unsigned changed{};
    n::VolumeQueuedIconProvider provider(queue.submit(), [&] { ++changed; });
    const std::array rows{app(5, shell, shellIdentity), app(6, test, testIdentity)};
    check(provider.setVisible(rows), "Real executables are requested");
    queue.settle();
    check(changed == 1 && bound(provider, rows[0])->image && !bound(provider, rows[1])->image, "The default extractor binds the real icon and leaves the icon-less executable nil");
    queue.executor.invalidate(queue.route);
    queue.executor.shutdown();
}
} // namespace

int main() {
    try {
        plan();
        extractor();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL after %u: %s\n", checks, e.what());
        return 1;
    }
    std::printf("PASS %u Volume queued icon checks\n", checks);
    return 0;
}
