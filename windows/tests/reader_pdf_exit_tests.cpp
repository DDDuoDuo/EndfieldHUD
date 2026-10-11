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
#include <d3d11.h>
#include <wrl/client.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
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
            const auto pages = document.render({}, {}, prefs, {376, 334}, true, {});
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
