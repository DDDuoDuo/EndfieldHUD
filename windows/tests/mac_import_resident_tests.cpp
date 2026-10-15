// What the Windows-only "Import macOS data…" flow keeps resident once an
// import has finished (WINDOWS-MIGRATION.md section 9: a lightweight HUD).
// Every heap block of this process is counted by replacing the global
// operator new/delete (usable sizes, malloc underneath); each scenario runs
// the production flow (app/areas/mac_import_flow.hpp) over a synthetic export
// in a temporary directory on the shared UtilityExecutor and compares the
// process heap once the card is hidden again with the heap right after the
// flow was constructed. A warm-up run first settles one-time statics
// (catalogs, thread-local generators, executor queues). No window, dialog,
// timer or real user data is involved.
#include "mac_import_test_support.hpp"
#include "app/areas/mac_import_flow.hpp"
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#include <windows.h>
#include <tlhelp32.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

namespace {
std::atomic<std::int64_t> heapBytes{0}, heapBlocks{0};
// Blocks allocated inside a measured window and still alive at its end are
// listed by address (open addressing; no heap use while tracing), so a
// failing scenario names what it left behind.
constexpr std::size_t traceSlots = std::size_t{1} << 20;
std::atomic<bool> tracing{false}, traceOverflow{false};
std::atomic<void*> traced[traceSlots];
void* const traceTomb = reinterpret_cast<void*>(std::uintptr_t{1});
std::size_t traceSlot(void* p) noexcept { return static_cast<std::size_t>((reinterpret_cast<std::uintptr_t>(p) >> 4) * 0x9E3779B97F4A7C15ull >> 44) & (traceSlots - 1); }
void track(void* p) noexcept {
    for (std::size_t n = 0, i = traceSlot(p); n < traceSlots; ++n, i = (i + 1) & (traceSlots - 1)) {
        void* current = traced[i].load(std::memory_order_relaxed);
        if ((current == nullptr || current == traceTomb) && traced[i].compare_exchange_strong(current, p)) return;
    }
    traceOverflow = true;
}
void untrack(void* p) noexcept {
    for (std::size_t n = 0, i = traceSlot(p); n < traceSlots; ++n, i = (i + 1) & (traceSlots - 1)) {
        void* current = traced[i].load(std::memory_order_relaxed);
        if (current == nullptr) return;
        if (current == p && traced[i].compare_exchange_strong(current, traceTomb)) return;
    }
}
std::size_t usable(void* p) noexcept {
#ifdef _WIN32
    return _msize(p);
#elif defined(__APPLE__)
    return malloc_size(p);
#else
    return malloc_usable_size(p);
#endif
}
void* allocate(std::size_t n) noexcept {
    void* p = std::malloc(n ? n : 1);
    if (p) { heapBytes += static_cast<std::int64_t>(usable(p)); ++heapBlocks; if (tracing.load(std::memory_order_relaxed)) track(p); }
    return p;
}
void release(void* p) noexcept {
    if (!p) return;
    untrack(p);
    heapBytes -= static_cast<std::int64_t>(usable(p));
    --heapBlocks;
    std::free(p);
}
} // namespace

void* operator new(std::size_t n) { if (auto* p = allocate(n)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (auto* p = allocate(n)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return allocate(n); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return allocate(n); }
void operator delete(void* p) noexcept { release(p); }
void operator delete[](void* p) noexcept { release(p); }
void operator delete(void* p, std::size_t) noexcept { release(p); }
void operator delete[](void* p, std::size_t) noexcept { release(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { release(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { release(p); }

namespace {
namespace a = endfield::areas;
using Step = a::MacImportStep;

std::int64_t heap() { return heapBytes.load(); }
void beginTrace() {
    for (auto& slot : traced) slot.store(nullptr, std::memory_order_relaxed);
    traceOverflow = false;
    tracing = true;
}
// Ends the window; the blocks it left alive, as "size: printable preview".
std::vector<std::string> endTrace() {
    tracing = false;
    std::vector<void*> alive;
    for (auto& slot : traced) {
        void* p = slot.load(std::memory_order_relaxed);
        if (p && p != traceTomb && alive.size() < 64) alive.push_back(p);
    }
    std::vector<std::string> out;
    for (auto* p : alive) {
        const auto size = usable(p);
        std::string line = std::to_string(size) + " B: ";
        const auto* bytes = static_cast<const unsigned char*>(p);
        for (std::size_t n = 0; n < size && n < 64; ++n) line.push_back(bytes[n] >= 0x20 && bytes[n] < 0x7f ? static_cast<char>(bytes[n]) : '.');
        line += " |";
        static constexpr char hex[] = "0123456789abcdef";
        for (std::size_t n = 0; n < size && n < 24; ++n) { line.push_back(' '); line.push_back(hex[bytes[n] >> 4]); line.push_back(hex[bytes[n] & 15]); }
        out.push_back(std::move(line));
    }
    if (traceOverflow) out.push_back("(trace table overflowed)");
    return out;
}
// Threads of this process (the shared executor's worker included).
std::optional<std::size_t> threads() {
#ifdef _WIN32
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return std::nullopt;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    std::size_t count{};
    const auto self = GetCurrentProcessId();
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry))
        if (entry.th32OwnerProcessID == self) ++count;
    CloseHandle(snapshot);
    return count;
#elif defined(__APPLE__)
    thread_act_array_t list{};
    mach_msg_type_number_t count{};
    if (task_threads(mach_task_self(), &list, &count) != KERN_SUCCESS) return std::nullopt;
    for (mach_msg_type_number_t n = 0; n < count; ++n) mach_port_deallocate(mach_task_self(), list[n]);
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(list), sizeof(thread_t) * count);
    return count;
#else
    return std::nullopt;
#endif
}
std::string utf8(const fs::path& p) { const auto t = p.u8string(); return std::string(reinterpret_cast<const char*>(t.data()), t.size()); }

// A realistic export manifest: hundreds of listed files no store reads
// (verified, reported as ignored), so whatever the flow keeps of an import is
// visible in the heap.
constexpr unsigned extraFiles = 512;
Export largeExport(const fs::path& root) {
    auto e = fullExport(root);
    for (unsigned n = 0; n < extraFiles; ++n) e.add("EndfieldCharge/Unknown/retained-check/" + uuid(1000 + n) + "-future-record.bin", "future record " + std::to_string(n));
    return e;
}

// One production flow over the shared executor with a recording host.
struct Flow {
    endfield::app::UtilityExecutor& executor;
    fs::path destination;
    bool inProcess{true};
    std::shared_ptr<m::MacImportSession> restarted;
    unsigned forgotten{};
    std::unique_ptr<a::MacImportFlow> flow;
    Flow(endfield::app::UtilityExecutor& e, fs::path d) : executor(e), destination(std::move(d)) {
        fs::create_directories(destination.parent_path());
        a::MacImportFlow::Host host;
        host.chooseFolder = [](const std::string&, const std::string&) { return true; };
        host.cancelChoice = [] {};
        host.inProcess = [this] { return inProcess; };
        host.restart = [this](std::shared_ptr<m::MacImportSession> s) { restarted = std::move(s); return true; };
        host.restartAvailable = [] { return true; };
        host.forgetOutcome = [this] { ++forgotten; m::removeMacImportOutcome(destination); };
        host.changed = [] {};
        host.unixNow = [] { return 1760000000.0; };
        flow = std::make_unique<a::MacImportFlow>(executor, destination, [] { return platform(); }, std::move(host));
    }
    ~Flow() { flow.reset(); executor.waitIdle(); executor.drain(); }
    Flow(const Flow&) = delete;
    Flow& operator=(const Flow&) = delete;
    void settle() {
        for (int guard = 0; guard < 4000 && flow->busy(); ++guard) { executor.waitIdle(); executor.drain(); flow->utilityCompleted(); }
        executor.waitIdle(); executor.drain();
        check(!flow->busy(), "The flow settles through the shared executor");
    }
    void stage(const fs::path& folder) {
        flow->open();
        flow->activate(1);
        check(flow->step() == Step::choosing, "Choose Folder waits for the shared dialog");
        flow->folderChosen(false, false, {utf8(folder)});
        settle();
    }
};

struct Measure {
    std::string scenario;
    std::int64_t open{}, finished{};  // heap above the hidden flow: while open (reviewed or reported), after it hid
    std::optional<std::size_t> threadsBefore, threadsAfter;
    std::size_t sessionsOpen{}, sessionsAfter{};
    a::MacImportFlowResidency residencyOpen, residencyAfter;
    std::vector<std::string> leftovers; // blocks of the window still alive at its end
};
std::vector<Measure> measures;

// Imports into a fresh destination and walks the card to its end; `open`
// is sampled at the step that holds the most (review or report).
template <class Walk> void scenario(endfield::app::UtilityExecutor& executor, const fs::path& base, std::string name, Walk walk) {
    Measure measure;
    measure.scenario = std::move(name);
    measure.threadsBefore = threads();
    {
        Flow f(executor, base / measure.scenario / "EndfieldHUD" / "v1");
        f.settle();
        beginTrace();
        const auto hidden = heap();
        check(f.flow->residency() == a::MacImportFlowResidency{} && m::macImportSessionsAlive() == 0, measure.scenario + ": a new flow holds nothing");
        walk(f, [&] {
            const auto bytes = heap() - hidden;
            if (bytes < measure.open) return;
            measure.open = bytes;
            measure.residencyOpen = f.flow->residency();
            measure.sessionsOpen = m::macImportSessionsAlive();
        });
        f.settle();
        check(!f.flow->presented() && f.flow->step() == Step::hidden, measure.scenario + ": the card is hidden again");
        f.restarted.reset();
        f.settle();
        measure.finished = heap() - hidden;
        measure.leftovers = endTrace();
        measure.residencyAfter = f.flow->residency();
        measure.sessionsAfter = m::macImportSessionsAlive();
    }
    measure.threadsAfter = threads();
    measures.push_back(std::move(measure));
}

void run(endfield::app::UtilityExecutor& executor, const fs::path& base, const fs::path& exportRoot) {
    // The session alone, without the flow or the executor: staged on this
    // thread, then discarded.
    scenario(executor, base, "session", [&](Flow& f, auto sample) {
        m::MacImportOptions consent; consent.acceptRejected = {m::MacImportStore::archive};
        m::MacImportSession session(exportRoot, f.destination, platform(), consent);
        session.stage();
        check(session.staged() && session.committable() && m::macImportSessionsAlive() == 1, "session: staged on this thread");
        sample();
        session.discard();
        check(!fs::exists(session.workDirectory()), "session: the discard removed its private folder");
    });
    // Completed in this run: review, commit, report, relink list, Done.
    scenario(executor, base, "completed", [&](Flow& f, auto sample) {
        f.stage(exportRoot);
        check(f.flow->step() == Step::review && f.flow->residency().session && f.flow->residency().results && m::macImportSessionsAlive() == 1,
              "completed: the reviewed session is held for the commit");
        sample();
        f.flow->activate(1);
        f.settle();
        check(f.flow->step() == Step::report && !f.flow->residency().session && !f.flow->residency().results && f.flow->residency().outcome &&
              m::macImportSessionsAlive() == 0, "completed: the report holds the outcome only; the session is gone");
        sample();
        f.flow->activate(0);
        check(f.flow->step() == Step::relink, "completed: the relink list");
        sample();
        f.flow->activate(1);
    });
    // Cancelled on the review: the staged copy is discarded on the worker.
    scenario(executor, base, "cancelled", [&](Flow& f, auto sample) {
        f.stage(exportRoot);
        check(f.flow->step() == Step::review, "cancelled: staged for review");
        sample();
        f.flow->activate(0);
    });
    // Not an export: the failure card, then Close.
    scenario(executor, base, "failed", [&](Flow& f, auto sample) {
        f.stage(exportRoot.parent_path() / "not-an-export");
        check(f.flow->step() == Step::failed && !f.flow->residency().session && !f.flow->residency().results && f.flow->residency().outcome &&
              m::macImportSessionsAlive() == 0, "failed: the failure card holds its outcome only");
        sample();
        f.flow->activate(0);
    });
    // Production: the reviewed session goes to the restart handoff; the host
    // declines (a failed save), then Cancel discards it on the worker.
    scenario(executor, base, "declined", [&](Flow& f, auto sample) {
        f.inProcess = false;
        f.stage(exportRoot);
        f.flow->activate(1);
        check(f.flow->step() == Step::restarting && f.restarted && f.flow->residency().session && !f.flow->residency().results && m::macImportSessionsAlive() == 1,
              "declined: handed to the restart; the service keeps no copy of the review");
        sample();
        f.flow->restartDeclined();
        f.restarted.reset(); // the host destroyed its unrun work
        f.flow->activate(0);
    });
    // The next run: the recorded outcome and the root's relink list, Done.
    scenario(executor, base, "recorded", [&](Flow& f, auto sample) {
        m::MacImportOptions consent; consent.acceptRejected = {m::MacImportStore::archive};
        {
            m::MacImportSession session(exportRoot, f.destination, platform(), consent);
            session.stage();
            const auto outcome = m::commitMacImportSession(session, f.destination, 1760000100);
            check(outcome.state == m::MacImportOutcome::State::completed, "recorded: the post-shutdown commit completes");
        }
        const auto ledger = m::MacRelinkLedger(f.destination).unresolved();
        f.flow->showOutcome(*m::readMacImportOutcome(f.destination), ledger);
        check(f.flow->step() == Step::report && f.flow->outcomeRecorded(), "recorded: the next run reports");
        sample();
        f.flow->activate(0);
        sample();
        f.flow->activate(1);
        check(f.forgotten == 1, "recorded: Done forgets the note");
    });
    // An earlier import: the replace question, answered with Cancel.
    scenario(executor, base, "replace", [&](Flow& f, auto sample) {
        m::MacImportOptions consent; consent.acceptRejected = {m::MacImportStore::archive};
        {
            m::MacImportSession earlier(exportRoot, f.destination, platform(), consent);
            earlier.stage();
            earlier.commit();
        }
        f.stage(exportRoot);
        check(f.flow->step() == Step::replace && !f.flow->residency().session && m::macImportSessionsAlive() == 0, "replace: an earlier import asks first; the refused session is gone");
        sample();
        f.flow->activate(0);
    });
}
} // namespace

int main() {
    try {
        Temporary t;
        auto e = largeExport(t.root / "export");
        e.write();
        fs::create_directories(t.root / "not-an-export");
        endfield::app::UtilityExecutor executor([] {});
        // Warm-up: one-time statics and the executor's queues.
        run(executor, t.root / "warm-up", e.root);
        measures.clear();
        run(executor, t.root / "measured", e.root);
        executor.waitIdle(); executor.drain(); executor.shutdown();
        // Report every scenario before judging any.
        constexpr std::int64_t bound = 512; // bytes; allocator rounding only
        for (const auto& s : measures) {
            std::cout << "  " << s.scenario << ": open +" << s.open << " B (" << s.sessionsOpen << " session), after the card hid +" << s.finished << " B (" << s.sessionsAfter
                      << " session); threads "
                      << (s.threadsBefore ? std::to_string(*s.threadsBefore) : std::string("n/a")) << " -> "
                      << (s.threadsAfter ? std::to_string(*s.threadsAfter) : std::string("n/a")) << '\n';
        }
        for (const auto& s : measures) {
            check(s.open > 0, s.scenario + ": the open card holds its import");
            check(s.residencyAfter == a::MacImportFlowResidency{} && s.sessionsAfter == 0, s.scenario + ": the hidden flow reports nothing held and no session is alive");
            std::string left;
            for (const auto& line : s.leftovers) left += "\n    " + line;
            check(s.finished <= bound, s.scenario + ": nothing of the import stays resident once the card hid (" + std::to_string(s.finished) + " B left in " +
                                          std::to_string(s.leftovers.size()) + " blocks):" + left);
            check(!s.threadsBefore || !s.threadsAfter || *s.threadsAfter <= *s.threadsBefore, s.scenario + ": the import starts no thread of its own");
        }
        std::cout << "PASS " << checks << " macOS import residency checks over " << extraFiles << " extra listed files; temporary synthetic exports only\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << ": " << error.what() << '\n';
        return 1;
    }
}
