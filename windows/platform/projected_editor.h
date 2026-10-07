#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::platform {

// ACP offsets are UTF-16 code units, matching the version-1 Mac rich-text codec.
// Navigation uses layout cluster boundaries when supplied by DirectWrite.
class EditorModel final {
public:
    static constexpr std::size_t maximum_units = 1'048'576;
    const std::u16string& text() const noexcept { return state_.text; }
    std::size_t anchor() const noexcept { return state_.anchor; }
    std::size_t caret() const noexcept { return state_.caret; }
    std::size_t begin() const noexcept;
    std::size_t end() const noexcept;
    std::uint64_t revision() const noexcept { return revision_; }
    bool set_text(std::u16string text);
    void select(std::size_t anchor, std::size_t caret);
    bool replace(std::size_t begin, std::size_t end, std::u16string_view text);
    bool insert(std::u16string_view text);
    bool erase(bool forward);
    void move(bool forward, bool extend);
    void set_clusters(std::vector<std::size_t> boundaries);
    bool undo();
    bool redo();
    void begin_composition();
    void end_composition();
    bool composing() const noexcept { return composition_; }
    static bool valid_utf16(std::u16string_view text) noexcept;
    static std::size_t scalar_boundary(std::u16string_view text, std::size_t offset) noexcept;

private:
    struct State { std::u16string text; std::size_t anchor{}, caret{}; };
    State state_;
    State composition_before_;
    std::vector<State> undo_, redo_;
    std::vector<std::size_t> clusters_;
    std::uint64_t revision_{};
    bool composition_{};
    void remember();
    void trim_history();
    std::size_t previous(std::size_t offset) const;
    std::size_t next(std::size_t offset) const;
};

// A projective matrix maps source-plane x/y into client PHYSICAL pixels.
// Rectangles sent to TSF enclose all four projected corners, then ClientToScreen
// adds the physical monitor origin. Drawing and inverse pointer hits use this
// same matrix; no child Edit HWND is created.
struct ProjectiveMapping final {
    std::array<double, 9> values{1,0,0, 0,1,0, 0,0,1};
    bool project(double x, double y, double& out_x, double& out_y) const noexcept;
    bool unproject(double x, double y, double& out_x, double& out_y) const noexcept;
};
}

#ifdef _WIN32
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

namespace endfield::platform {
class ProjectedEditor final {
public:
    ProjectedEditor();
    ~ProjectedEditor();
    ProjectedEditor(const ProjectedEditor&) = delete;
    ProjectedEditor& operator=(const ProjectedEditor&) = delete;
    HRESULT initialize(HWND owner, IDWriteFactory* factory, std::function<void()> invalidate);
    void shutdown();
    bool set_text(std::u16string text);
    void set_rectangle(D2D1_RECT_F source_rectangle);
    void set_projection(ProjectiveMapping mapping);
    void focus(bool active);
    bool focused() const;
    // Call before TranslateMessage in the application's one message loop.
    bool pre_translate(const MSG& message);
    bool handle_message(UINT message, WPARAM wparam, LPARAM lparam, LRESULT& result);
    // Render into the existing HUD plane, beneath its existing GPU transform.
    void draw(ID2D1RenderTarget* target);
    const EditorModel& model() const;
    HRESULT tsf_status() const;
    std::wstring font_inventory() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
#endif
