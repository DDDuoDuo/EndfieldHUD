#pragma once

#include "scene/watch_scene.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace ehud::scene {

enum class DesktopLanguage { english, simplifiedChinese, traditionalChinese, japanese, korean };
enum class DesktopModule {
    system, display, hotkeys, about, storage, activityMonitor,
    notes, fileShelf, clipboard, archive, mediaAssembly, minigame,
    nowPlaying, volume, projection, reader, workMode, calendar,
    map, eventLog, profile, account, power, addApp
};
enum class DesktopGroup { left, right, bottom, power };

// This is presentation input, not a provider or persistence owner. The caller
// supplies fixture values; the model never reads profiles, accounts or clocks.
struct DesktopShellFixture {
    DesktopLanguage language{DesktopLanguage::english};
    std::optional<DesktopModule> selectedModule{DesktopModule::power};
    std::string profileName{"Endministrator"}, profileUID;
    int permissionLevel{60};
    std::string clockTime{"12:34:56"}, clockDate{"FRI Jan 2"};
    std::string summonShortcut{"CTRL + SHIFT + E"};
    std::array<double, 3> accent{250.0/255, 212.0/255, 31.0/255};
    bool dark{true}, reduceMotion{};
    Phase phase{Phase::visible};
    double phaseElapsed{}, closingCanvasOpacity{1};
    double hudScale{1};
    Vec2 hudOffset{}; // fractions of the selected client viewport
};

struct DesktopBinding {
    SourceId buttonId, captionNodeId, iconNodeId;
    std::string sourcePath;
    DesktopModule module{};
    std::string title, caption;
    // All modules remain unavailable at this feasibility milestone. This must
    // not be interpreted as a successful action or a module implementation.
    bool implemented{};
};
struct DesktopNativePlane {
    SourceId nodeId;
    Rect sourceRect{};
    Vec2 designSize{};
    Mat4 world{Mat4::identity()}, sceneWorld{Mat4::identity()};
    double alpha{1};
    int sortingOrder{};
    std::vector<HitRegion::Mask> masks;
    // Design coordinates have top-left origin; source local coordinates are +up.
    Rect rect(Rect topLeftDesignRect) const;
    double fontSize(double designPoints) const;
};
struct DesktopShellPresentation {
    std::vector<DesktopBinding> bindings;
    std::optional<DesktopNativePlane> centerPlane, statusPlane, footerPlane;
    double canvasAlpha{};
    bool sourceLogoRetained{};
};

class DesktopShell {
  public:
    explicit DesktopShell(const Document &document);
    static const std::array<DesktopModule, 18> &rightModules();
    static std::string identifier(DesktopModule module);
    static DesktopGroup group(DesktopModule module);
    static std::string title(DesktopModule module, DesktopLanguage language);
    static std::string caption(DesktopModule module, DesktopLanguage language);
    // Empty means the exact HUDNavigationEntry.iconPath silhouette is used.
    static std::filesystem::path approvedIcon(DesktopModule module);
    static DesktopLanguage resolveLanguage(const std::vector<std::string> &preferredLanguages);
    static double canvasAlpha(const DesktopShellFixture &fixture);
    // Pass this before Document::frame to update the original profile's fill,
    // glow and accent properties. No images, timers or module models are made.
    DesktopPresentation sourcePresentation(const DesktopShellFixture &fixture) const;
    // Replaces only native desktop overlays and their explicit source icons.
    // Repeated decoration is idempotent; source camera/geometry/hits remain owned
    // by Document. Right-side logical assignment comes from that same frame.
    DesktopShellPresentation decorate(Frame &frame, const DesktopShellFixture &fixture = {});

  private:
    Document document_;
    DesktopSceneInfo info_;
    std::vector<Button> buttons_;
    std::vector<SourceId> profileAccentIds_;
    Vec2 calibrationViewport_{};
    double centerUnitsPerPoint_{};
    void calibrate(Vec2 viewport);
};
} // namespace ehud::scene
