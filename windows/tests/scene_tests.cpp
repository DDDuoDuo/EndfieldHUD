#include "scene/watch_scene.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace ehud::scene;
namespace {
int checks{};
void check(bool ok, const char *reason) {
    ++checks;
    if (!ok)
        throw std::runtime_error(reason);
}
void near(double a, double b, double tolerance, const char *reason) {
    check(std::abs(a - b) <= tolerance, reason);
}
template <class F> void rejects(F &&operation, const char *reason) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, reason);
}
void curves() {
    ScalarCurve hermite({{0, 2, 0, 3}, {2, 8, 3, 0}});
    near(hermite.sample(1), 5, 1e-12, "Source Hermite slope units use seconds");
    near(hermite.sample(-1), 2, 0, "Curve pre-infinity clamps");
    near(hermite.sample(3), 8, 0, "Curve post-infinity clamps");
    ScalarCurve stepped({{0, 0, 0, std::numeric_limits<double>::infinity()}, {1, 1, 0, 0}});
    near(stepped.sample(0.999), 0, 0, "Infinite tangent is a stepped segment");
    near(stepped.sample(1), 1, 0, "Step includes endpoint key");
    ScalarCurve weighted({{0, 0, 0, 0, 2, 1.0 / 3, 0.2}, {1, 1, 0, 0, 1, 0.2, 1.0 / 3}});
    near(weighted.sample(0.5), 0.5, 1e-12, "Weighted Bezier solves original time handles");
    rejects([] { ScalarCurve invalid({{1, 0, 0, 0}, {1, 1, 0, 0}}); },
            "Duplicate curve times reject");
    rejects([&] { hermite.sample(std::numeric_limits<double>::quiet_NaN()); },
            "Nonfinite sampling rejects");
}
void lifecycle() {
    Playback playback(0.75, 0.5);
    check(playback.phase() == Phase::concealed, "Initial phase closed");
    playback.open(10);
    auto sample = playback.sample(10.375);
    check(sample.phase == Phase::opening, "Opening duration stays source length");
    near(sample.entranceTime, 0.5625, 1e-12, "Finite wrapper applies OutQuad exactly once");
    check(!sample.ambientTime, "No ambient during opening");
    sample = playback.sample(10.75);
    check(sample.phase == Phase::visible, "Entrance completion visible");
    near(*sample.ambientTime, 0, 0, "Ambient starts after finite entrance");
    auto generation = playback.generation();
    playback.close(11);
    sample = playback.sample(11.25);
    near(*sample.exitTime, 0.375, 1e-12, "Closing wrapper OutQuad");
    playback.open(11.3);
    check(playback.generation() == generation + 2, "Interrupted transitions carry new generation");
    check(playback.sample(11.6).phase == Phase::opening,
          "Old closing deadline cannot conceal reopen");
    playback.close(12, true);
    check(playback.sample(12).phase == Phase::concealed,
          "Reduced motion close has no submitted frame");
    playback.open(13, true);
    sample = playback.sample(13, true);
    check(sample.phase == Phase::visible && !sample.ambientTime,
          "Reduced motion seeks source endpoint");
    near(Playback::clipTime(-1, 0.75), 0, 0, "Finite clock lower clamp");
    near(Playback::clipTime(100, 0.75), 0.75, 0, "Finite clock upper clamp");
}
void projection() {
    Camera camera;
    camera.viewport = {1920, 1080};
    camera.viewProjection = {};
    double y = 1 / std::tan(0.4), aspect = 1920.0 / 1080;
    camera.viewProjection.values[0] = y / aspect;
    camera.viewProjection.values[5] = y;
    camera.viewProjection.values[10] = 100.0 / 99;
    camera.viewProjection.values[11] = 1;
    camera.viewProjection.values[14] = -100.0 / 99;
    Mat4 world = Mat4::identity();
    double angle = 0.17;
    world.values[0] = std::cos(angle);
    world.values[2] = -std::sin(angle);
    world.values[8] = std::sin(angle);
    world.values[10] = std::cos(angle);
    world.values[12] = 0.4;
    world.values[14] = 10;
    Vec3 local{0.7, -0.3, 0};
    auto pixel = camera.project(local, world);
    check(pixel.has_value(), "Tilted source point projects");
    auto point = camera.hit(*pixel, world, {{-2, -2}, {4, 4}});
    check(point.has_value(), "Hit uses identical drawn camera/world");
    near(point->x, local.x, 1e-11, "Inverse raycast X");
    near(point->y, local.y, 1e-11, "Inverse raycast Y");
    check(!camera.hit(*pixel, world, {{-1, -1}, {1, 1}}),
          "Local bounds reject despite projected bounding box");
    Frame frame;
    frame.camera = camera;
    frame.hits.push_back(
        {"CAB:-9007199254740993", "CAB:9223372036854775807", {{-2, -2}, {4, 4}}, world, {}});
    check(frame.buttonAt(*pixel) == "CAB:9223372036854775807",
          "Signed large IDs preserve exact decimal digits");
    frame.hits.push_back(
        {"front", "frontButton", {{-2, -2}, {4, 4}}, world, {{{{-1, -1}, {1, 1}}, world}}});
    check(frame.buttonAt(*pixel) == "CAB:9223372036854775807",
          "Top graphic clipping mask rejects hit-through");
    frame.hits.back().masks.clear();
    check(frame.buttonAt(*pixel) == "frontButton", "Frontmost source graphic consumes input");
    Mat4 singular{};
    check(!inverse(singular), "Singular transform cannot receive input");
    check(Rect{{1, 1}, {-2, -2}}.contains({0, 0}), "Signed source rects preserve inverted sizes");
    check(!Rect{{0, 0}, {0, 1}}.contains({0, 0}), "Zero-size rect cannot receive input");
}
void gyro() {
    GyroMotion motion;
    check(motion.retarget({2, 3, 0}, 0, 0.5), "Gyro target changes");
    auto zero = motion.rotation(0);
    near(zero.w, 1, 1e-15, "Retarget snapshots current quaternion");
    auto half = motion.rotation(0.25);
    check(half.w < 1 && half.w > 0.99, "Quaternion OutQuad interpolation remains normalized");
    near(half.x * half.x + half.y * half.y + half.z * half.z + half.w * half.w, 1, 1e-12,
         "Gyro interpolation normalized");
    motion.retarget({-2, -3, 0}, 0.25, 0.5);
    auto retained = motion.rotation(0.25);
    near(retained.x, half.x, 1e-15, "Retarget keeps interpolated pose");
    motion.stop(0.3);
    check(!motion.animating(), "Closing can park gyro without another timer");
    motion.retarget({}, 1, 0.5, true);
    check(!motion.animating(), "Reduced-motion gyro settles immediately");
    near(motion.rotation(1).w, 1, 1e-15, "Reduced-motion identity pose");
}
void flicker() {
    auto opening = flickerSequence(true, 0.13, 0.20, 42),
         repeat = flickerSequence(true, 0.13, 0.20, 42),
         closing = flickerSequence(false, 0.11, 0.01, 42);
    check(opening.keyTimes == repeat.keyTimes && opening.opacityOffsets == repeat.opacityOffsets,
          "Deployment flicker seeds are deterministic");
    near(opening.duration + opening.delay, 0.33, 1e-15,
         "Random initial stagger stays inside requested total");
    near(opening.opacity(0, true), 0, 0, "Opening gate hides before group turn");
    near(opening.opacity(10, true), 1, 0, "Opening gate restores after finite dropout");
    near(closing.opacity(0, false), 1, 0, "Closing remains visible before group turn");
    near(closing.opacity(10, false), 0, 0, "Closing gate retains disappearance after track");
    near(sweepDelay(0, 0, 640, true, 0.25), 0, 0, "Opening starts at top");
    near(sweepDelay(640, 0, 640, true, 0.25), 0.25, 0, "Opening reaches bottom last");
    near(sweepDelay(0, 0, 640, false, 0.23), 0.23, 0, "Closing reaches top last");
    near(sweepDelay(640, 0, 640, false, 0.23), 0, 0, "Closing starts at bottom");
}
void source(const std::filesystem::path &path) {
    auto doc = Document::load(path);
    check(doc.nodeCount() > 700, "Actual scene graph loaded");
    check(doc.buttons().size() >= 22, "All original main source buttons retained");
    check(doc.buttons().front().id == "CAB-194e41a66c2317b9df19269f505210be:2283844765343771863",
          "JSON source IDs above 2^53 retain all original digits");
    near(doc.entranceDuration(), 0.75, 1e-15, "Source entrance duration read from clip");
    check(doc.ambientDuration() > 13, "Full ambient last-key hold retained");
    auto right = doc.pointerEuler({1920, 540}, {1920, 1080});
    near(right.y, 3, 1e-7, "Source yaw curve/max-angle endpoint");
    auto top = doc.pointerEuler({960, 0}, {1920, 1080});
    near(top.x, -2, 1e-7, "Windows Y flips exactly once for source pitch");
    Playback playback(doc.entranceDuration(), doc.exitDuration());
    playback.open(0, true);
    auto frame = doc.frame({{1920, 1080}, playback.sample(0, true), {}});
    check(!frame.graphics.empty() && !frame.hits.empty(),
          "Actual source graphics and hit regions share snapshot");
    check(!frame.diagnostics.empty(), "Prototype capability gaps remain explicit");
    std::size_t valid{};
    for (const auto &hit : frame.hits) {
        Vec3 center{hit.rect.origin.x + hit.rect.size.x / 2,
                    hit.rect.origin.y + hit.rect.size.y / 2, 0};
        if (auto pixel = frame.camera.project(center, hit.world);
            pixel && frame.camera.hit(*pixel, hit.world, hit.rect))
            ++valid;
    }
    check(valid > 20, "Actual authored hit geometry inverse-projects");
    for (const auto &g : frame.graphics)
        check(g.quads.size() == g.uvQuads.size(), "Mesh and source UV counts agree");
    std::size_t meshes{};
    for (const auto &g : frame.graphics)
        if (g.kind == "MeshRenderer")
            ++meshes;
    check(meshes > 0, "Original circle meshes loaded from approved metadata");
    for (int i = 0; i < 100; ++i) {
        playback.open(i, true);
        auto opened = doc.frame({{1920, 1080}, playback.sample(i, true), {}});
        check(opened.hits.size() == frame.hits.size(),
              "Repeated immutable opens retain geometry count");
        playback.close(i + 0.1, true);
        check(doc.frame({{1920, 1080}, playback.sample(i + 0.1, true), {}}).graphics.empty(),
              "Closed snapshot emits no rendering work");
    }
    auto narrow =
        doc.frame({{1080, 1920}, {Phase::visible, doc.entranceDuration(), {}, {}, 0}, {}});
    check(narrow.canvasSize.y > frame.canvasSize.y,
          "Source narrow-screen FOV/canvas rule retained");
    auto graphicsAddress = frame.graphics.data();
    auto geometryAddress = frame.graphics.front().quads.data();
    Quaternion tilt{0, std::sin(0.025), 0, std::cos(0.025)};
    doc.reproject(frame, tilt);
    check(frame.graphics.data() == graphicsAddress &&
              frame.graphics.front().quads.data() == geometryAddress,
          "Pointer-only reproject retains static geometry allocations");
    auto rebuilt =
        doc.frame({{1920, 1080}, {Phase::visible, doc.entranceDuration(), {}, {}, 0}, tilt});
    for (int i = 0; i < 16; ++i)
        near(frame.hits.front().world.values[i], rebuilt.hits.front().world.values[i], 1e-9,
             "Pointer fast path shares exact fresh-frame transform");
    rejects(
        [&] {
            Document::load(path, [](const auto &) {
                return std::string("{\"nodes\":[],\"root_node_id\":\"CAB:-9007199254740993\"}");
            });
        },
        "Malformed graph rejects without app data");
}
} // namespace
int main(int argc, char **argv) {
    try {
        curves();
        lifecycle();
        projection();
        gyro();
        flicker();
        if (argc > 1)
            source(std::filesystem::path(argv[1]));
        std::cout << "Passed " << checks << " scene contract checks"
                  << (argc > 1 ? " including actual source resources" : " (synthetic fixtures)")
                  << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Scene contract failed: " << error.what() << '\n';
        return 1;
    }
}
