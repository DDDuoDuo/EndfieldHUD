#pragma once
// A synthetic right-hand list shaped like the source HUD's (RightBottomNode):
// a scroll viewport (UIScrollRect + RectMask2D) whose content holds a pool of
// four rows of two buttons, each button with a caption child, and a slant
// effect naming the rows (the row pool) kept inactive by the desktop
// settings. Shared by the layout, frame and session tests of Arrange on HUD.
#include "core/source_watch_frame.hpp"
#include <string>
#include <vector>

namespace endfield::tests {
struct SourceListFixture {
    core::source::SceneDefinition scene;
    core::source::MountedLayoutDocument document;
    core::source::SourceWatchFrameResources resources;
    core::source::SourceDesktopFrameSettings settings;
    std::vector<std::string> buttons; // row by row, left to right
};
inline SourceListFixture sourceListFixture() {
    using namespace core::source;
    using core::source::Json;
    const auto node = [](std::string id, std::string path, std::optional<std::string> parent, Vec2 anchored, Vec2 size, std::vector<std::string> children = {}) {
        Node n; n.id = std::move(id); n.name = path.substr(path.rfind('/') + 1); n.path = std::move(path); n.parent = std::move(parent); n.children = std::move(children);
        n.rect = RectTransform{{.5, .5}, {.5, .5}, anchored, size, {.5, .5}}; return n;
    };
    std::vector<Node> nodes;
    std::vector<std::string> rows, buttons;
    for (int r = 0; r < 4; ++r) rows.push_back("row" + std::to_string(r));
    nodes.push_back(node("root", "Root", {}, {}, {1000, 640}, {"right"}));
    nodes.push_back(node("right", "Root/RightBottomNode", "root", {250, 0}, {400, 600}, {"list"}));
    nodes.push_back(node("list", "Root/RightBottomNode/List", "right", {0, 0}, {300, 250}, {"content", "effect"}));
    auto content = rows;
    nodes.push_back(node("content", "Root/RightBottomNode/List/Content", "list", {0, 0}, {300, 400}, content));
    nodes.push_back(node("effect", "Root/RightBottomNode/List/Slant", "list", {0, 0}, {300, 250}));
    Json effect = Json::parse(R"({"_bottomY":-125,"_topY":125,"_leftX":0,"_maxWidth":0,"_curve":{"m_Curve":[{"time":0,"value":0,"inSlope":0,"outSlope":0},{"time":1,"value":0,"inSlope":0,"outSlope":0}]}})");
    Json::Array cells;
    for (int r = 0; r < 4; ++r) {
        const auto row = "row" + std::to_string(r), rowPath = "Root/RightBottomNode/List/Content/Row" + std::to_string(r);
        std::vector<std::string> children;
        for (int c = 0; c < 2; ++c) {
            const auto button = "b" + std::to_string(r) + std::to_string(c), buttonPath = rowPath + "/Btn" + std::to_string(c);
            children.push_back(button); buttons.push_back(button);
            nodes.push_back(node(button, buttonPath, row, {-75. + 150. * c, 0}, {140, 90}, {button + "label"}));
            nodes.push_back(node(button + "label", buttonPath + "/BtnName", button, {0, -20}, {100, 24}));
        }
        nodes.push_back(node(row, rowPath, "content", {0, 150. - 100. * r}, {300, 100}, children));
        cells.push_back(Json::Object{{"target_id", row}});
    }
    effect["_cells"] = Json(cells);
    SourceListFixture fixture{SceneDefinition("root", std::move(nodes)), {}, {}, {}, std::move(buttons)};
    auto& doc = fixture.document;
    Json canvas(Json::Object{}); canvas["m_SortingOrder"] = 6080;
    doc.components["root"] = {{"canvas", "MonoBehaviour", "Canvas", canvas}, {"cut", "MonoBehaviour", "UIWatchPanelCut", Json::Object{}}};
    doc.components["list"] = {{"scroll", "MonoBehaviour", "UIScrollRect", Json::Object{{"m_Vertical", true}, {"m_Content", Json::Object{{"target_id", "content"}}}, {"m_Viewport", Json::Object{{"target_id", "list"}}}}},
        {"mask", "MonoBehaviour", "RectMask2D", Json::Object{}}};
    doc.components["effect"] = {{"slant", "MonoBehaviour", "UIScrollCellSlantEffect", effect}};
    const auto color = [](double a) { return Json::Object{{"r", 1}, {"g", 1}, {"b", 1}, {"a", a}}; };
    const Json colors = Json::Object{{"m_NormalColor", color(.2)}, {"m_HighlightedColor", color(1)}, {"m_PressedColor", color(.5)}, {"m_SelectedColor", color(.8)},
        {"m_DisabledColor", color(.1)}, {"m_ColorMultiplier", 1}, {"m_FadeDuration", 1}};
    for (const auto& button : fixture.buttons) {
        Json image(Json::Object{}); image["m_RaycastTarget"] = true;
        doc.components[button] = {{"image/" + button, "MonoBehaviour", "UIImage", image},
            {"button/" + button, "MonoBehaviour", "UIButton", Json::Object{{"m_Transition", 1}, {"m_TargetGraphic", Json::Object{{"target_id", "image/" + button}}}, {"m_Colors", colors}}}};
        doc.components[button + "label"] = {{"image/" + button + "label", "MonoBehaviour", "UIImage", Json::Object{}}};
        const auto* n = fixture.scene.node(button);
        doc.buttons.push_back({button, n->path, {}});
    }
    for (const auto index : {0u, 2u}) fixture.resources.materialVariants["__ui_default"][index] = "__ui_default";
    fixture.resources.textureSizes["__white"] = {1, 1};
    fixture.settings.hiddenNodes.insert("effect");
    return fixture;
}
} // namespace endfield::tests
