#pragma once
#include "core/source_animation.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace endfield::core::source {
// MSVC allocates a sentinel for even an empty std::map. Reuse one immutable
// default instead of constructing a temporary map on every idle resolve.
const Overrides& emptyLayoutOverrides();
// Unity local coordinates (+Y up). Negative sizes are intentional source data.
struct SourceRect {
    Vec2 origin{}, size{};
    static SourceRect fromSizePivot(Vec2 size, Vec2 pivot) noexcept;
    std::array<Vec3,4> corners() const noexcept;
    bool contains(Vec2 point, double tolerance = 1e-9) const noexcept;
    bool operator==(const SourceRect&) const = default;
};
struct ResolvedNode {
    const Node* node{};
    Matrix4 localMatrix, worldMatrix;
    std::optional<SourceRect> rect;
    bool activeInHierarchy{};
};
// Pure HUDSourceScene.resolve translation. The immutable definition must outlive
// this layout and results. Results keep original serialized node order; traversal
// indices provide the source's m_Children depth-first order without allocation.
// Watch-specific layout writers (scroll slant, grids, bindings) are a later stage.
class SourceLayout final {
public:
    explicit SourceLayout(const SceneDefinition&);
    std::vector<ResolvedNode> resolve(std::optional<SourceRect> rootParentRect = {},
                                      const Overrides& overrides = emptyLayoutOverrides()) const;
    std::span<const std::size_t> traversalIndices() const noexcept { return traversal_; }
    std::optional<std::size_t> nodeIndex(std::string_view id) const noexcept;
    const SceneDefinition& scene() const noexcept { return *scene_; }
    static ResolvedNode resolveNode(const Node&, const TransformOverride*,
                                    const std::optional<SourceRect>& parentRect,
                                    const Matrix4& parentWorld, bool parentActive);
private:
    friend class IncrementalResolver;
    const SceneDefinition* scene_;
    std::map<std::string_view,std::size_t,std::less<>> indices_;
    std::vector<std::size_t> traversal_, parents_;
    std::vector<std::vector<std::size_t>> children_;
};
// One result per node, no history or timers. Changed branches are staged before
// committing, including the input snapshot and counters. An invalid update
// leaves every previously returned node unchanged. Unchanged resolves allocate
// nothing; changing values with the same override/component keys also allocate
// nothing. Structural changes may allocate a new override-map snapshot.
// Returned spans remain valid until destruction, and their contents change only
// after a successful resolve. The object is intended for its owner render thread.
class IncrementalResolver final {
public:
    explicit IncrementalResolver(const SceneDefinition&);
    std::span<const ResolvedNode> resolve(std::optional<SourceRect> rootParentRect = {},
                                          const Overrides& overrides = emptyLayoutOverrides());
    std::span<const ResolvedNode> nodes() const noexcept;
    const ResolvedNode* node(std::string_view id) const noexcept;
    std::span<const std::size_t> traversalIndices() const noexcept { return layout_.traversalIndices(); }
    std::size_t lastRebuiltNodeCount() const noexcept { return lastRebuilt_; }
    std::uint64_t rebuiltNodeCount() const noexcept { return rebuilt_; }
    std::uint64_t reusedNodeCount() const noexcept { return reused_; }
    std::uint64_t revision() const noexcept { return revision_; }
private:
    SourceLayout layout_;
    Overrides previousOverrides_;
    std::optional<SourceRect> previousRootParentRect_;
    std::vector<ResolvedNode> cached_, staged_;
    std::vector<unsigned char> dirty_;
    std::vector<std::size_t> pending_;
    bool initialized_{};
    std::size_t lastRebuilt_{};
    std::uint64_t rebuilt_{}, reused_{}, revision_{};
};
} // namespace endfield::core::source
