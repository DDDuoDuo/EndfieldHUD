#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace endfield::core {
struct Point { double x{}, y{}; bool operator==(const Point&) const = default; };
struct Rect {
    double x{}, y{}, width{}, height{};
    bool contains(Point p) const noexcept;
    bool operator==(const Rect&) const = default;
};
struct Matrix4 {
    // Column-major, matching the macOS source's SIMD/CATransform3D convention.
    std::array<double,16> values{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    static Matrix4 translation(double x,double y,double z = 0);
    static Matrix4 scale(double x,double y,double z = 1);
    static Matrix4 rotation(double xRadians,double yRadians,double zRadians);
    bool finite() const noexcept;
    bool operator==(const Matrix4&) const = default;
};
Matrix4 operator*(const Matrix4&,const Matrix4&);

// A plane-to-viewport homography is shared by text, pointer hits and IME.
// Updating the camera does not invalidate textures, layouts or scene geometry.
struct Projection {
    std::array<double,9> values{1,0,0, 0,1,0, 0,0,1};
    static Projection viewport(const Matrix4& viewProjectionWorld,double width,double height);
    std::optional<Point> project(Point p) const;
    std::optional<Point> unproject(Point screen) const;
};
bool polygonContains(std::span<const Point> polygon,Point p) noexcept;

using NodeID = std::uint32_t;
constexpr NodeID noNode = UINT32_MAX;
struct SceneNode {
    std::string sourceID;
    NodeID parent{noNode};
    Matrix4 local,world;
    double opacity{1},resolvedOpacity{1};
    bool visible{true},resolvedVisible{true};
};
struct HitTarget {
    std::string action;
    NodeID node{noNode};
    std::vector<Point> contour;
    struct Clip { NodeID node{noNode};std::vector<Point> contour; };
    std::vector<Clip> clips;
    bool enabled{true};
};

// Nodes are parent-first and stable for the life of a retained module. The root
// camera is deliberately outside this graph: pointer tilt is one GPU uniform,
// not a reason to rebuild every module, text layout or texture each frame.
class RetainedScene final {
public:
    static constexpr std::size_t maximumNodes = 16384,maximumTargets = 4096;
    NodeID append(std::string sourceID,NodeID parent = noNode,Matrix4 local = {});
    bool setLocal(NodeID,Matrix4);
    bool setOpacity(NodeID,double);
    bool setVisible(NodeID,bool);
    void setTargets(std::vector<HitTarget>);
    std::size_t resolve(); // number of changed nodes, zero for an idle scene
    const SceneNode& node(NodeID) const;
    std::span<const SceneNode> nodes() const noexcept {return nodes_;}
    std::uint64_t revision() const noexcept {return revision_;}
    std::optional<std::string> hit(Point screen,const Matrix4& viewProjectionRoot,
                                   double width,double height) const;
private:
    std::vector<SceneNode> nodes_;
    std::vector<bool> dirty_;
    std::vector<HitTarget> targets_;
    std::uint64_t revision_{};
};
}
