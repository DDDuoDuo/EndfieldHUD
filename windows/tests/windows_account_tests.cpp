// The Windows account reader for the personal cards (native/windows_account.*,
// issue #17) over a fake token, directory, registry and files: the RID from
// the SID, the name lookup order, the account picture's registry values, and
// the in-memory decode of its bytes (native/profile_image.hpp). Nothing here
// reads the real account, the real registry or a real picture: the only file
// is a synthetic PNG in a new temporary folder.
#include "native/windows_account.hpp"
#include "native/profile_image.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>
namespace {
namespace n = endfield::native;
namespace m = endfield::modules;
using Microsoft::WRL::ComPtr;
unsigned checks{};
void check(bool v, const std::string& why) { ++checks; if (!v) throw std::runtime_error(why); }
using Text = std::optional<std::wstring>;
// A synthetic PNG's bytes (encoded in memory).
std::vector<std::uint8_t> png(UINT width, UINT height) {
    ComPtr<IWICImagingFactory> f;
    check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&f))), "WIC factory");
    ComPtr<IStream> stream;
    check(SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)), "Memory stream");
    ComPtr<IWICBitmapEncoder> encoder;
    check(SUCCEEDED(f->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) && SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)), "Encoder");
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> bag;
    check(SUCCEEDED(encoder->CreateNewFrame(&frame, &bag)) && SUCCEEDED(frame->Initialize(bag.Get())) && SUCCEEDED(frame->SetSize(width, height)), "Frame");
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    check(SUCCEEDED(frame->SetPixelFormat(&format)) && format == GUID_WICPixelFormat24bppBGR, "Format");
    const UINT stride = (width * 3 + 3) & ~3u;
    std::vector<BYTE> pixels(std::size_t(stride) * height, 0x80);
    check(SUCCEEDED(frame->WritePixels(height, stride, UINT(pixels.size()), pixels.data())) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()), "Commit");
    STATSTG stat{};
    HGLOBAL memory{};
    check(SUCCEEDED(stream->Stat(&stat, STATFLAG_NONAME)) && SUCCEEDED(GetHGlobalFromStream(stream.Get(), &memory)), "Stream bytes");
    const auto* bytes = static_cast<const std::uint8_t*>(GlobalLock(memory));
    check(bytes != nullptr, "Lock the stream");
    std::vector<std::uint8_t> out(bytes, bytes + stat.cbSize.QuadPart);
    GlobalUnlock(memory);
    return out;
}
struct Fake {
    Text sid{L"S-1-5-21-1004336348-1177238915-682003330-1001"}, display, full, user{L"ada"};
    std::map<std::wstring, std::wstring> registry;          // value name -> path
    std::map<std::wstring, std::vector<std::uint8_t>> files; // path -> bytes
    std::vector<std::wstring> keys, values;
    std::uintmax_t limit{};
    bool sidThrows{};
    n::WindowsAccountCalls calls() {
        n::WindowsAccountCalls c;
        c.sid = [this]() -> Text { if (sidThrows) throw std::runtime_error("token"); return sid; };
        c.displayName = [this] { return display; };
        c.fullName = [this] { return full; };
        c.userName = [this] { return user; };
        c.machineString = [this](const std::wstring& key, const std::wstring& value) -> Text {
            keys.push_back(key); values.push_back(value);
            const auto it = registry.find(value);
            return it == registry.end() ? Text{} : Text{it->second};
        };
        c.readFile = [this](const std::filesystem::path& path, std::uintmax_t maximum) -> std::optional<std::vector<std::uint8_t>> {
            limit = maximum;
            const auto it = files.find(path.wstring());
            return it == files.end() ? std::nullopt : std::optional(it->second);
        };
        return c;
    }
};
void run() {
    // The RID: the SID's last sub-authority.
    check(n::windowsAccountRID(L"S-1-5-21-1004336348-1177238915-682003330-1001") == std::optional<std::string>("1001"), "The RID is the SID's last part");
    check(n::windowsAccountRID(L"S-1-5-18") == std::optional<std::string>("18") && !n::windowsAccountRID(L"S-1-5-21-") && !n::windowsAccountRID(L"1001")
          && !n::windowsAccountRID(L"S-1-5-21-12345678901"), "Only a decimal RID of at most ten digits");
    // The name: NameDisplay, then the full name, then the user name.
    Fake fake;
    fake.display = L"Ada Lovelace";
    auto account = n::readWindowsAccount(fake.calls());
    check(account.name == std::optional<std::string>("Ada Lovelace") && account.uid == std::optional<std::string>("1001") && !account.avatarKey, "The display name and RID");
    fake.display.reset(); fake.full = L"  \t ";
    check(n::readWindowsAccount(fake.calls()).name == std::optional<std::string>("ada"), "A blank full name falls back to the user name");
    fake.full = L"Lovelace, Ada";
    check(n::readWindowsAccount(fake.calls()).name == std::optional<std::string>("Lovelace, Ada"), "NameDisplay failing (a local account): the full name");
    fake.display = L"\u00c9lise \u5f20";
    check(n::readWindowsAccount(fake.calls()).name == std::optional<std::string>("\xc3\x89lise \xe5\xbc\xa0"), "Names are read as UTF-8");
    fake.display.reset(); fake.full.reset(); fake.user.reset();
    check(!n::readWindowsAccount(fake.calls()).name, "No name anywhere: none");
    fake.sidThrows = true;
    check(!n::readWindowsAccount(fake.calls()).uid && !n::readWindowsAccountPicture(fake.calls()), "A token that cannot be read: no UID and no picture");
    fake.sidThrows = false;
    // The picture: Image448, then Image1080, then Image240 under the SID's key.
    const auto bytes = png(120, 80);
    check(!n::readWindowsAccountPicture(fake.calls()), "No registered picture: none");
    const std::wstring key = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AccountPicture\\Users\\S-1-5-21-1004336348-1177238915-682003330-1001";
    check(fake.keys.size() == 3 && fake.keys[0] == key && (fake.values == std::vector<std::wstring>{L"Image448", L"Image1080", L"Image240"}), "The three sizes are asked in order");
    fake.registry[L"Image240"] = L"C:\\Users\\Public\\AccountPictures\\S-1\\small.png";
    fake.files[L"C:\\Users\\Public\\AccountPictures\\S-1\\small.png"] = {1, 2, 3};
    fake.registry[L"Image1080"] = L"C:\\Users\\Public\\AccountPictures\\S-1\\large.png";
    fake.files[L"C:\\Users\\Public\\AccountPictures\\S-1\\large.png"] = bytes;
    check(n::readWindowsAccountPicture(fake.calls()) == bytes && fake.limit == 16ull * 1024 * 1024, "Image1080 before Image240, read bounded to 16 MB");
    fake.registry[L"Image448"] = L"C:\\missing.png";
    check(n::readWindowsAccountPicture(fake.calls()) == bytes, "A registered file that is gone falls through to the next size");
    // The bytes decode through the avatar loader, in memory.
    const auto decoded = n::decodeProfileAvatarWorking(bytes);
    check(decoded.image.width == 120 && decoded.image.height == 80 && decoded.orientation == 1 && decoded.sourceWidth == 120, "The picture decodes as an avatar");
    bool rejected{};
    try { (void)n::decodeProfileAvatarWorking(std::vector<std::uint8_t>{1, 2, 3}); } catch (const m::ProfileError& e) { rejected = e.code() == m::ProfileFailure::image; }
    check(rejected, "Unreadable bytes are not a picture");
    // Production builds its calls without reading anything.
    const auto real = n::currentWindowsAccountCalls();
    check(real.sid && real.displayName && real.fullName && real.userName && real.machineString && real.readFile, "Production calls are complete (not called here)");
}
}
int wmain() {
    const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    try {
        check(SUCCEEDED(hr), "COM");
        run();
        CoUninitialize();
        std::cout << "PASS " << checks << " Windows account reader checks (fake token, registry and files only)\n";
        return 0;
    } catch (const std::exception& e) {
        if (SUCCEEDED(hr)) CoUninitialize();
        std::cerr << "FAIL after " << checks << ": " << e.what() << '\n';
        return 1;
    }
}
#else
int main() { return 0; }
#endif
