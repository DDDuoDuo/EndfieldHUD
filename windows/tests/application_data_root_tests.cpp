// Production data roots of the Notes and File Shelf owners. EndfieldHUD.exe
// opens the existing %LOCALAPPDATA%\EndfieldHUD\v1 root through the persistent
// entry points selected by app/application_persistence.hpp. This test gives
// both real owners a temporary root that already holds an earlier run's
// records, edits through the owners, "restarts" them and reads the stores
// back: content survives, no sample note is written, and the development
// fixture contract (a NEW root) still refuses an existing one. Hidden HWND,
// WARP renderer and temporary directories only; no real user data.
#ifdef _WIN32
#include "app/application_persistence.hpp"
#include "core/data/data_store.hpp"
#include "core/data/file_io.hpp"
#include "core/module_presentation.hpp"
#include "native/file_shelf_files.hpp"
#include "native/module_scene.hpp"
#include "tools/notes_preview.hpp"
#include "tools/shelf_preview.hpp"
#include <ole2.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace gpu = endfield::native;
namespace core = endfield::core;
namespace app = endfield::app;
namespace source = core::source;
namespace data = ehud::data;
namespace tools = endfield::tools;
namespace fs = std::filesystem;
namespace {
unsigned checks{};
void check(bool value, const std::string& why) { ++checks; if (!value) throw std::runtime_error(why); }
std::string utf8(const fs::path& p) { const auto s = p.u8string(); return {reinterpret_cast<const char*>(s.data()), s.size()}; }
struct Ole { Ole() { check(SUCCEEDED(OleInitialize(nullptr)), "Owned OLE apartment"); } ~Ole() { OleUninitialize(); } };
struct Window {
    HWND hwnd{};
    Window() {
        WNDCLASSW c{}; c.lpfnWndProc = DefWindowProcW; c.hInstance = GetModuleHandleW(nullptr); c.lpszClassName = L"EndfieldDataRootFixture";
        check(RegisterClassW(&c) != 0, "Register the owned fixture class");
        hwnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW, c.lpszClassName, L"Owned data root fixture", WS_POPUP, 0, 0, 1280, 800, nullptr, nullptr, c.hInstance, nullptr);
        check(hwnd && !IsWindowVisible(hwnd), "Hidden owned fixture HWND");
    }
    ~Window() { if (hwnd) DestroyWindow(hwnd); UnregisterClassW(L"EndfieldDataRootFixture", GetModuleHandleW(nullptr)); }
};
struct Temp {
    fs::path root = fs::absolute(fs::temp_directory_path()) / ("ehud-data-root-" + data::makeUUID());
    Temp() { check(fs::create_directory(root), "Create a unique owned fixture parent"); }
    ~Temp() { std::error_code ignored; fs::remove_all(root, ignored); }
};
std::vector<data::Note> savedNotes(const fs::path& root) { data::NotesStore store(root); return store.notes(); }
std::vector<data::ShelfRecord> savedShelf(const fs::path& root) { gpu::NativeFileShelfFiles files; data::FileShelfStore store(root, files.platform()); return store.items(); }

// One Notes owner run on the persistent root, driven like the shell owner.
struct NotesRun {
    gpu::Renderer& renderer;
    gpu::LayerRasterizer raster;
    std::unique_ptr<tools::NotesPreview> preview;
    gpu::LayerComposition composition;
    source::DesktopChromeSettings settings;
    core::Matrix4 design, camera{gpu::layerViewportProjection(1280, 800)};
    double time{};
    bool released{};
    NotesRun(HWND hwnd, gpu::Renderer& r, const fs::path& root, const gpu::NativeNotesControlsAssets& assets) : renderer(r) {
        settings.viewport = {0, 0, 1280, 800}; settings.module = core::Module::notes; settings.sourceShell = true;
        design = source::DesktopChromeLayout::make(settings, {}, {}).designToScreen;
        // The production selection path of app/application.cpp.
        preview = app::makeNotesOwner<tools::NotesPreview>(true, hwnd, raster, root, assets, false, fs::path{}, std::span<const data::Note>{}, tools::NotesPreviewMediaOptions{});
        preview->resize({1280, 800, 96, 1, 1280, 800});
        frame();
    }
    ~NotesRun() {
        if (released) return;
        try { composition.detach(renderer); if (preview) preview->release(renderer); } catch (...) { renderer.reset(); }
    }
    void frame() {
        preview->update(design, settings, 1, time, true);
        preview->upload(renderer);
        composition.setEntries(renderer, preview->entries());
        preview->collected(renderer);
        composition.present(renderer);
        renderer.setCamera(camera);
    }
    bool pointer(app::PointerKind kind, core::Point p) { time += .01; frame(); const bool handled = preview->pointer({kind, app::PointerButton::left, p.x, p.y, 0}, time); frame(); return handled; }
    bool key(app::KeyKind kind, std::uint32_t value, std::optional<tools::NotesKeyModifiers> modifiers = {}) {
        time += .01; frame(); const bool handled = preview->key({kind, value}, time, modifiers); frame(); return handled;
    }
    void release() {
        check(preview->finish(), "Notes editor is unlocked at shutdown");
        frame(); composition.detach(renderer); preview->release(renderer); preview.reset(); released = true;
    }
};

void notes(HWND hwnd, gpu::Renderer& renderer, const gpu::NativeNotesControlsAssets& assets) {
    Temp temp;
    const auto root = temp.root / "EndfieldHUD" / "v1";
    data::Note earlier; earlier.text = "Earlier run"; earlier.x = 180; earlier.y = 245; earlier.width = 240; earlier.height = 145;
    fs::create_directories(root);
    { data::NotesStore store(root); check(store.upsert(earlier), "An earlier run's note is in the persistent root"); }
    {
        gpu::LayerRasterizer raster;
        bool refused{};
        try { tools::NotesPreview fixture(hwnd, raster, root, assets, false); } catch (const std::exception&) { refused = true; }
        check(refused, "The development fixture entry point still refuses an existing root");
    }
    auto saved = savedNotes(root);
    check(saved.size() == 1 && saved.front().text == earlier.text, "The refused fixture left the root untouched");
    {
        NotesRun run(hwnd, renderer, root, assets);
        saved = savedNotes(root);
        check(saved.size() == 1 && saved.front().id == earlier.id && saved.front().text == earlier.text, "Opening the persistent root writes no sample note");
        const tools::NotesKeyModifiers control{true, false, false, false};
        check(run.pointer(app::PointerKind::doubleClick, {earlier.x + 30, earlier.y + 45}), "The earlier run's note is loaded and opens for editing");
        check(run.key(app::KeyKind::down, VK_END) && run.key(app::KeyKind::character, '!'), "The owner edits the persisted note");
        check(run.key(app::KeyKind::down, VK_RETURN, control), "Ctrl+Return commits the edit");
        saved = savedNotes(root);
        check(saved.size() == 1 && saved.front().id == earlier.id && saved.front().text == earlier.text + "!", "The edit is saved into the persistent root");
        run.release();
    }
    {
        // Restart: the next run opens the same root and sees the edit.
        NotesRun run(hwnd, renderer, root, assets);
        check(run.preview->diagnosticEditing(true, run.time + .1), "The restarted owner loaded the persisted note");
        check(run.preview->diagnosticEditing(false, run.time + .2), "The restarted editor finishes");
        saved = savedNotes(root);
        check(saved.size() == 1 && saved.front().text == earlier.text + "!", "The edited note survives the restart unchanged");
        run.release();
    }
    {
        // Fresh install: the versioned root is created, still without a sample.
        const auto fresh = temp.root / "Fresh" / "v1";
        NotesRun run(hwnd, renderer, fresh, assets);
        check(fs::is_directory(fresh) && savedNotes(fresh).empty(), "A fresh persistent root starts empty");
        run.release();
    }
}

void shelf(HWND hwnd, gpu::Renderer& renderer, const gpu::NativeShelfAssets& assets, std::shared_ptr<const core::SubsectionMaskSampler> masks) {
    Temp temp;
    const auto root = temp.root / "EndfieldHUD" / "v1";
    const auto earlierFile = temp.root / "earlier.txt", laterFile = temp.root / "later.txt";
    for (const auto& file : {earlierFile, laterFile}) { std::ofstream out(file); out << "Temporary synthetic fixture"; }
    fs::create_directories(root);
    {
        gpu::NativeFileShelfFiles files; data::FileShelfStore store(root, files.platform());
        const std::array tokens{utf8(earlierFile)};
        check(store.add(tokens) == 1, "An earlier run's reference is in the persistent root");
    }
    {
        gpu::LayerRasterizer raster;
        bool refused{};
        try { tools::ShelfPreview fixture(hwnd, raster, assets, {root, false, false, masks}); } catch (const std::exception&) { refused = true; }
        check(refused && savedShelf(root).size() == 1, "The development fixture entry point refuses an existing root and leaves it untouched");
    }
    const auto run = [&](const fs::path& dataRoot, const std::function<void(tools::ShelfPreview&, double)>& body) {
        gpu::LayerRasterizer raster; gpu::LayerComposition composition; core::ModulePresentation modules{core::Module::fileShelf};
        tools::ShelfPreviewOptions options{dataRoot, false, false, masks};
        app::persistentShelfRoot(options); // the production selection path of app/application.cpp
        tools::ShelfPreview owner(hwnd, raster, assets, std::move(options));
        owner.resize({1280, 800, 96, 1, 1280, 800});
        source::DesktopChromeSettings settings{{0, 0, 1280, 800}, 1, {}, core::Module::fileShelf, true};
        const auto center = source::DesktopChromeLayout::make(settings, {}, {}).designToScreen;
        double time{};
        bool released{};
        struct Cleanup { gpu::Renderer& r; gpu::LayerComposition& c; tools::ShelfPreview& s; bool& done; ~Cleanup() { if (done) return; try { c.detach(r); s.release(r); } catch (...) { r.reset(); } } } cleanup{renderer, composition, owner, released};
        const auto frame = [&] {
            time += .02; const auto sample = modules.sample(time).presentation;
            owner.update(center, settings, sample, 1, time); owner.upload(renderer); composition.setEntries(renderer, owner.entries()); owner.collected(renderer); composition.present(renderer);
        };
        frame();
        body(owner, time);
        frame();
        composition.detach(renderer); owner.release(renderer); released = true;
    };
    run(root, [&](tools::ShelfPreview& owner, double time) {
        check(owner.state().items().size() == 1 && owner.state().items().front().name == "earlier.txt", "The owner loads the earlier run's reference");
        const std::array paths{utf8(laterFile)};
        check(owner.importFiles(paths, time), "The owner adds a reference to the persistent root");
        check(savedShelf(root).size() == 2, "The new reference is saved");
    });
    run(root, [&](tools::ShelfPreview& owner, double) {
        const auto& items = owner.state().items();
        check(items.size() == 2 && std::any_of(items.begin(), items.end(), [](const auto& i) { return i.name == "later.txt"; }),
              "Both references survive the restart");
    });
    const auto fresh = temp.root / "Fresh" / "v1";
    run(fresh, [&](tools::ShelfPreview& owner, double) { check(owner.state().items().empty() && fs::is_directory(fresh), "A fresh persistent root starts empty"); });
    for (const auto& file : {earlierFile, laterFile}) check(fs::exists(file), "References never move or delete the original files");
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        check(argc == 7, "Usage: application_data_root_tests hud.hlsl notes-controls-assets notes-sha notes-commit shelf-assets reveal-mask.bin");
        const auto ascii = [](const wchar_t* value) { std::string out; for (; *value; ++value) { check(*value <= 127, "Pins are ASCII"); out.push_back(static_cast<char>(*value)); } return out; };
        Ole ole;
        Window window;
        gpu::Renderer renderer;
        renderer.initialize(window.hwnd, 1280, 800, {gpu::Driver::warpForTests, argv[1], gpu::RenderTarget::offscreenForTests});
        const gpu::NativeNotesControlsAssets notesAssets(fs::absolute(argv[2]), {ascii(argv[3]), ascii(argv[4])});
        notes(window.hwnd, renderer, notesAssets);
        const gpu::NativeShelfAssets shelfAssets(fs::absolute(argv[5]));
        const auto bytes = data::detail::readFile(argv[6], 128 * 1024);
        check(bytes.has_value(), "Original reveal samples exist");
        auto masks = std::make_shared<core::SubsectionMaskSampler>(std::span(reinterpret_cast<const std::uint8_t*>(bytes->data()), bytes->size()), core::SubsectionMaskSampler::assetSHA256);
        shelf(window.hwnd, renderer, shelfAssets, std::move(masks));
        check(!IsWindowVisible(window.hwnd), "The fixture never showed a window");
        renderer.reset();
        std::cout << "Persistent Notes and File Shelf roots: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Persistent Notes and File Shelf roots failed after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
#endif
