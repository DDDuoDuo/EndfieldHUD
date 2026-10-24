// A process that ends right after rendering a PDF page must exit cleanly.
// Windows.Data.Pdf's PdfPage.RenderToStreamAsync kept the component's own
// Direct3D device alive until an internal idle timer; EndfieldHUD quitting
// within seconds of a PDF render then faulted in dxgi.dll during static
// teardown (exit code 0x87A = 2170). The Reader now renders on a device it
// owns and releases with the document. The parent writes a synthetic PDF to
// a private temporary folder and runs this executable as a hidden child that
// loads Direct3D first (as the HUD's renderer does), renders pages through the
// production document provider on an MTA worker, releases it like the Reader
// owner's teardown and returns at once; every child must exit with 0.
#include "native/reader_document.hpp"
#include "native/layer_raster.hpp"
#include "core/data/data_store.hpp"
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
namespace {
namespace fs = std::filesystem;
using namespace endfield::modules;
using namespace endfield::native;
unsigned checks{};
void check(bool value, const char* why) { ++checks; if (!value) throw std::runtime_error(why); }

std::string pdf() {
    std::string out = "%PDF-1.4\n";
    const std::vector<std::string> objects{
        "<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R 5 0 R] /Count 2 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] /Resources << >> /Contents 4 0 R >>",
        "<< /Length 26 >>\nstream\n1 0 0 rg 0 0 200 300 re f\nendstream",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 200] /Resources << >> /Contents 6 0 R >>",
        "<< /Length 26 >>\nstream\n0 0 1 rg 0 0 300 200 re f\nendstream"};
    std::vector<std::size_t> offsets{0};
    for (std::size_t n = 0; n < objects.size(); ++n) { offsets.push_back(out.size()); out += std::to_string(n + 1) + " 0 obj\n" + objects[n] + "\nendobj\n"; }
    const auto xref = out.size();
    out += "xref\n0 " + std::to_string(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (std::size_t n = 1; n < offsets.size(); ++n) { char row[32]; std::snprintf(row, sizeof(row), "%010llu 00000 n \n", static_cast<unsigned long long>(offsets[n])); out += row; }
    out += "trailer\n<< /Size " + std::to_string(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + std::to_string(xref) + "\n%%EOF\n";
    return out;
}
std::string utf8(const fs::path& path) { const auto raw = path.u8string(); return {reinterpret_cast<const char*>(raw.data()), raw.size()}; }

// Child: Direct3D loaded before the Reader (the HUD renderer's order), one
// document on an MTA worker, PDF pages rendered, released, then return 0.
int child(const fs::path& fonts, const fs::path& book) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 9;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr)) &&
        FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr)))
        return 10;
    LayerFontResources resources;
    { LayerRasterizer raster(fonts); resources = raster.retainedFontResources(); }
    int result = 11;
    std::thread worker([&] {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
        try {
            NativeReaderDocument document(resources);
            document.open(windowsReaderReference(ehud::data::makeUUID(), utf8(book), "Exit"), {});
            const auto prefs = ReaderPreferences::defaults("System");
            const auto pages = document.render({}, {}, prefs, {376, 334}, true,1,{});
            const auto zoomed = document.detail({}, prefs, {376, 334}, false, {4, {}}, {});
            const auto& pixels = *pages.front().pixels;
            const auto center = (std::size_t(pixels.height / 2) * pixels.width + pixels.width / 2) * 4;
            const auto stats = document.stats();
            const bool rendered = pages.size() == 2 && pages.front().illustration && pixels.bytes[center + 2] > 220 && pixels.bytes[center] < 30 &&
                                  zoomed.pixels && stats.pdfRenders >= 3 && stats.pdfDevices == 1 && stats.pdfDeviceReady;
            document.release();
            result = !rendered ? 12 : document.stats().pdfDeviceReady ? 13 : 0;
        } catch (const std::exception& e) {
            std::cerr << "child: " << e.what() << '\n';
            result = 14;
        }
        CoUninitialize();
    });
    worker.join();
    device.Reset();
    CoUninitialize();
    std::cout << "child rendered and released PDF pages, result " << result << std::endl;
    return result; // ends at once: static teardown follows the render immediately
}

std::uint64_t privateBytes() { PROCESS_MEMORY_COUNTERS_EX m{}; m.cb = sizeof(m); return GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m), sizeof(m)) ? std::uint64_t(m.PrivateUsage) : 0; }
unsigned threadCount() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); if (snapshot == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry); unsigned count{};
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++count;
    CloseHandle(snapshot); return count;
}
// Diagnostics: this process's threads grouped by the module of their start
// address (thread-pool workers start in ntdll.dll), e.g. "ntdll.dll=21 ...".
std::string threadModules() {
    using Query = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    const auto query = reinterpret_cast<Query>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread")));
    std::map<std::string, unsigned> counts;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); if (snapshot == INVALID_HANDLE_VALUE || !query) return {};
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId()) continue;
        std::string name = "?";
        if (const HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID)) {
            void* start{}; HMODULE module{}; wchar_t path[MAX_PATH]{};
            if (query(thread, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof(start), nullptr) >= 0 && start &&
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(start), &module) &&
                GetModuleFileNameW(module, path, MAX_PATH)) name = fs::path(path).filename().string();
            CloseHandle(thread);
        }
        ++counts[name];
    }
    CloseHandle(snapshot);
    std::string out; for (const auto& [name, count] : counts) out += (out.empty() ? "" : " ") + name + "=" + std::to_string(count);
    return out;
}
// Child: the Reader's lifetime on the shared utility executor, whose worker
// may change between jobs (an idle worker exits; the next job starts a new
// one, and its threads do not initialize COM). Every show opens and renders
// the PDF on one worker, a zoomed page arrives on the next and every hide
// releases the document on a third, each worker ending after its job, while
// the creating STA thread holds the HUD's Direct3D device. Forty cycles must
// keep working and leave private bytes and threads where the first left them.
int cycles(const fs::path& fonts, const fs::path& book, const fs::path& report, unsigned cycleCount) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 9;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr)) &&
        FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr)))
        return 10;
    int result = 0; std::ofstream out(report);
    try {
        LayerFontResources resources;
        { LayerRasterizer raster(fonts); resources = raster.retainedFontResources(); }
        NativeReaderDocument document(resources);
        const auto job = [](const auto& work) { std::exception_ptr error; std::thread worker([&] { try { work(); } catch (...) { error = std::current_exception(); } }); worker.join(); if (error) std::rethrow_exception(error); };
        const auto prefs = ReaderPreferences::defaults("System");
        const auto before = privateBytes(); const auto beforeThreads = threadCount();
        std::uint64_t held{}, first{}, last{}; unsigned heldThreads{}, firstThreads{}, lastThreads{}, devices{};std::string series;
        double coldShow{}, warmShow{}, zoom{};
        for (unsigned cycle = 0; cycle < cycleCount; ++cycle) {
            const auto shown = std::chrono::steady_clock::now();
            job([&] {
                document.open(windowsReaderReference(ehud::data::makeUUID(), utf8(book), "Cycles"), {});
                const auto pages = document.render({}, {}, prefs, {376, 334}, true,1,{});
                check(pages.size() == 2 && pages.front().illustration && pages.front().pixels, "A show renders the PDF pages on its worker");
            });
            const auto showMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - shown).count();
            if (cycle == 0) coldShow = showMs; else if (cycle == 1) warmShow = showMs;
            const auto zoomStart = std::chrono::steady_clock::now();
            job([&] {
                const auto zoomed = document.detail({}, prefs, {376, 334}, true, {4, {}}, {});
                const auto stats = document.stats();
                check(zoomed.pixels && stats.pdfDeviceReady, "A later worker renders a zoomed page with the same document and device");
                devices = unsigned(stats.pdfDevices);
            });
            if (cycle == 1) zoom = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - zoomStart).count();
            if (cycle == 0) { held = privateBytes(); heldThreads = threadCount(); out << "threads with a PDF open: " << threadModules() << '\n'; }
            job([&] { document.release(); check(!document.stats().pdfDeviceReady, "A hide releases the document and its device on a third worker"); });
            if (cycle == 1) { first = privateBytes(); firstThreads = threadCount(); }
            last = privateBytes(); lastThreads = threadCount();
            char value[32]; std::snprintf(value, sizeof(value), "%s%.2f", series.empty() ? "" : " ", last / 1048576.); series += value;
        }
        out << "private MB after each hide: " << series << '\n' << "show (open + two pages) ms: first " << coldShow << ", second " << warmShow << "; 4x zoomed page ms: " << zoom << '\n';
        out << "READER_PDF_CYCLES private MB before=" << before / 1048576. << " open=" << held / 1048576. << " released(cycle 2)=" << first / 1048576.
            << " released(last cycle)=" << last / 1048576. << "; threads before=" << beforeThreads << " open=" << heldThreads << " released(cycle 2)=" << firstThreads
            << " released(last cycle)=" << lastThreads << "; devices=" << devices << '\n' << "threads after the last cycle: " << threadModules() << '\n';
        check(devices == cycleCount, "Each show creates one owned device, released with the document");
        // The owned WARP device rasterizes on the job's worker: an open PDF
        // adds at most a few thread-pool workers, no device threads.
        check(heldThreads <= beforeThreads + 12, "An open PDF starts no device threads");
        check(last <= first + 8ull * 1024 * 1024, "Further show/hide cycles keep private bytes within 8 MB of the second");
        // Opening a PDF starts tens of system threads (a document's Direct3D
        // device and the PDF and thread-pool workers); idle pool workers come
        // and go on their own timeouts, so the bound allows a few of them but
        // not one more document's worth per cycle.
        check(lastThreads <= firstThreads + 8, "Show/hide cycles leave no threads behind");
    } catch (const std::exception& e) {
        out << "cycles: " << e.what() << '\n'; result = 15;
    }
    out.close(); device.Reset(); CoUninitialize();
    return result;
}

struct Temp {
    fs::path path = fs::temp_directory_path() / ("endfield-reader-pdf-exit-" + ehud::data::makeUUID());
    Temp() { fs::create_directory(path); }
    ~Temp() { std::error_code e; fs::remove_all(path, e); }
};

DWORD run(const std::wstring& command) {
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring line = command;
    check(CreateProcessW(nullptr, line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process) != FALSE, "Start the hidden PDF child");
    const auto waited = WaitForSingleObject(process.hProcess, 120000);
    DWORD code = STILL_ACTIVE;
    if (waited == WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess, &code);
    else TerminateProcess(process.hProcess, 99);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    check(waited == WAIT_OBJECT_0, "The PDF child finishes within two minutes");
    return code;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 4 && std::wstring_view(argv[1]) == L"--child") return child(argv[2], argv[3]);
        if (argc == 6 && std::wstring_view(argv[1]) == L"--cycles") return cycles(argv[2], argv[3], argv[4], unsigned(std::stoul(argv[5])));
        check(argc == 2, "Argument is the bundled font directory");
        Temp fixture;
        const auto book = fixture.path / L"exit fixture.pdf";
        { const auto bytes = pdf(); std::ofstream file(book, std::ios::binary); file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); check(bool(file), "Owned PDF fixture written"); }
        wchar_t self[MAX_PATH * 4]{};
        check(GetModuleFileNameW(nullptr, self, MAX_PATH * 4) > 0, "Own executable path");
        const std::wstring command = L"\"" + std::wstring(self) + L"\" --child \"" + fs::path(argv[1]).wstring() + L"\" \"" + book.wstring() + L"\"";
        for (unsigned n = 0; n < 4; ++n) {
            const auto started = std::chrono::steady_clock::now();
            const auto code = run(command);
            std::cout << "PDF child " << n << " exit code " << code << " after "
                      << std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() << " s\n";
            check(code != 2170, "A process ending right after a PDF render does not fault in dxgi.dll (0x87A)");
            check(code == 0, "A process rendering PDF pages through the owned device exits cleanly at once");
        }
        {   const auto report = fixture.path / L"cycles.txt";
            const auto code = run(L"\"" + std::wstring(self) + L"\" --cycles \"" + fs::path(argv[1]).wstring() + L"\" \"" + book.wstring() + L"\" \"" + report.wstring() + L"\" 40");
            std::ifstream in(report); std::string line; while (std::getline(in, line)) std::cout << line << '\n';
            check(code == 0, "Show/hide cycles of a PDF across changing utility workers keep working, release their devices and leave no growth");
        }
        std::cout << "Reader PDF exit passed " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << " after " << checks << " checks\n";
        return 1;
    }
}
#else
int main() { return 0; }
#endif
