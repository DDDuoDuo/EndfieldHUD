#include "scene/watch_scene.hpp"
#include "app/frame_schedule.h"
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
void frameScheduling() {
    ehud::app::FrameSchedule schedule;
    Playback playback(0.2, 0.2);
    playback.open(0);
    auto phase = playback.sample(0.184).phase;
    auto decision = schedule.next(phase, false, false, false);
    check(decision.submit && decision.rebuild, "Opening submits source geometry");
    schedule.submitted(phase, false, false);
    phase = playback.sample(0.216).phase;
    decision = schedule.next(phase, false, false, false);
    check(decision.submit && decision.rebuild, "Entrance deadline draws exact source endpoint before parking");
    schedule.submitted(phase, false, false);
    check(!schedule.next(phase, false, false, false).submit, "Settled entrance parks on next tick");

    GyroMotion motion;
    motion.retarget({2, 3, 0}, 1, 0.2);
    motion.finishIfNeeded(1.184);
    decision = schedule.next(phase, false, motion.animating(), false);
    check(decision.submit && !decision.rebuild, "Pointer-only motion reuses source geometry");
    schedule.submitted(phase, motion.animating(), false);
    motion.finishIfNeeded(1.216);
    decision = schedule.next(phase, false, motion.animating(), false);
    check(decision.submit && !decision.rebuild, "Gyro deadline reprojects exact settled transform");
    schedule.submitted(phase, motion.animating(), false);
    check(!schedule.next(phase, false, false, false).submit, "Settled gyro parks after endpoint");

    decision = schedule.next(phase, true, false, true);
    check(decision.submit && decision.rebuild, "Button animation rebuilds changed source geometry");
    schedule.submitted(phase, false, true);
    decision = schedule.next(phase, false, false, false);
    check(decision.submit && decision.rebuild, "Button deadline rebuilds its final tint and depth");
    schedule.submitted(phase, false, false);
    for (int i = 0; i < 100; ++i)
        check(!schedule.next(phase, false, false, false).submit, "Settled demand does not restart idle frames");
    schedule.submitted(phase, true, true);
    check(!schedule.next(Phase::concealed, true, true, true).submit, "Closed state cancels all animation work");
    check(!schedule.next(Phase::concealed, false, false, false).submit, "Closed state remains parked");
    decision = schedule.next(Phase::visible, true, false, false);
    check(decision.submit && decision.rebuild, "Reduced-motion reopen still draws full endpoint");
    schedule.submitted(Phase::visible, false, false);
    check(!schedule.next(Phase::visible, false, false, false).submit, "Reopen forgets interrupted animation demand");
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
// An independent exported-format fixture, with no OS or real application data.
Document fixture() {
    static const std::map<std::string, std::string> resources = {
        {"scene", R"fixture({
  "root_node_id": "CAB:root",
  "nodes": [
    {
      "id": "CAB:root",
      "name": "CAB:root",
      "path": "fixture/CAB:root",
      "parent_id": null,
      "child_ids": [
        "CAB:group",
        "CAB:view",
        "CAB:mesh"
      ],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 100,
            "y": 100
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "component:Canvas",
          "script": "Canvas",
          "type": "MonoBehaviour",
          "data": {
            "m_SortingOrder": 1000,
            "m_OverrideSorting": false,
            "m_VertexColorAlwaysGammaSpace": true
          }
        },
        {
          "id": "component:RectMask2D",
          "script": "RectMask2D",
          "type": "MonoBehaviour",
          "data": {}
        }
      ]
    },
    {
      "id": "CAB:group",
      "name": "CAB:group",
      "path": "fixture/CAB:group",
      "parent_id": "CAB:root",
      "child_ids": [
        "CAB:-9007199254740993",
        "CAB:ignored"
      ],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 200,
            "y": 80
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "component:Canvas",
          "script": "Canvas",
          "type": "MonoBehaviour",
          "data": {
            "m_SortingOrder": 777,
            "m_OverrideSorting": false,
            "m_VertexColorAlwaysGammaSpace": true
          }
        },
        {
          "id": "component:UISortingOrder",
          "script": "UISortingOrder",
          "type": "MonoBehaviour",
          "data": {
            "m_Enabled": false,
            "_renderType": 1,
            "_sortingOrderOffset": 7
          }
        },
        {
          "id": "component:HorizontalLayoutGroup",
          "script": "HorizontalLayoutGroup",
          "type": "MonoBehaviour",
          "data": {
            "m_ChildControlWidth": true,
            "m_ChildControlHeight": false,
            "m_ChildAlignment": 0,
            "m_Spacing": 10,
            "m_Padding": {
              "m_Left": 5,
              "m_Right": 5,
              "m_Top": 0,
              "m_Bottom": 0
            }
          }
        }
      ]
    },
    {
      "id": "CAB:-9007199254740993",
      "name": "CAB:-9007199254740993",
      "path": "fixture/CAB:-9007199254740993",
      "parent_id": "CAB:group",
      "child_ids": [
        "CAB:hover"
      ],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 20,
            "y": 10
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "CAB:image",
          "script": "UIImage",
          "type": "MonoBehaviour",
          "data": {
            "m_Color": {
              "r": 1,
              "g": 1,
              "b": 1,
              "a": 1
            },
            "m_RaycastTarget": true
          }
        },
        {
          "id": "component:UIButton",
          "script": "UIButton",
          "type": "MonoBehaviour",
          "data": {
            "m_Transition": 1,
            "m_Interactable": true,
            "m_TargetGraphic": {
              "target_id": "CAB:image"
            },
            "m_Colors": {
              "m_NormalColor": {
                "r": 0.6,
                "g": 0.6,
                "b": 0.6,
                "a": 1
              },
              "m_HighlightedColor": {
                "r": 1,
                "g": 1,
                "b": 1,
                "a": 1
              },
              "m_PressedColor": {
                "r": 0.2,
                "g": 0.2,
                "b": 0.2,
                "a": 1
              },
              "m_SelectedColor": {
                "r": 0.4,
                "g": 0.4,
                "b": 0.4,
                "a": 1
              },
              "m_DisabledColor": {
                "r": 0,
                "g": 0,
                "b": 0,
                "a": 1
              },
              "m_ColorMultiplier": 1,
              "m_FadeDuration": 0.2
            }
          }
        },
        {
          "id": "component:LayoutElement",
          "script": "LayoutElement",
          "type": "MonoBehaviour",
          "data": {
            "m_MinWidth": 10,
            "m_PreferredWidth": 20,
            "m_FlexibleWidth": 0
          }
        }
      ]
    },
    {
      "id": "CAB:hover",
      "name": "CAB:hover",
      "path": "fixture/CAB:hover",
      "parent_id": "CAB:-9007199254740993",
      "child_ids": [],
      "game_object": {
        "data": {
          "m_IsActive": false
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 20,
            "y": 10
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": []
    },
    {
      "id": "CAB:ignored",
      "name": "CAB:ignored",
      "path": "fixture/CAB:ignored",
      "parent_id": "CAB:group",
      "child_ids": [],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 40,
            "y": 10
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "component:LayoutElement",
          "script": "LayoutElement",
          "type": "MonoBehaviour",
          "data": {
            "m_Enabled": false,
            "m_IgnoreLayout": true
          }
        }
      ]
    },
    {
      "id": "CAB:view",
      "name": "CAB:view",
      "path": "fixture/CAB:view",
      "parent_id": "CAB:root",
      "child_ids": [
        "CAB:content"
      ],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 80,
            "y": 50
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "component:UIScrollRect",
          "script": "UIScrollRect",
          "type": "MonoBehaviour",
          "data": {
            "m_Vertical": true,
            "m_Content": {
              "target_id": "CAB:content"
            },
            "m_Viewport": {
              "target_id": "CAB:view"
            },
            "m_ScrollSensitivity": 2
          }
        },
        {
          "id": "component:UIScrollCellSlantEffect",
          "script": "UIScrollCellSlantEffect",
          "type": "MonoBehaviour",
          "data": {
            "_bottomY": -100,
            "_topY": 100,
            "_leftX": 5,
            "_maxWidth": 20,
            "_cells": [
              {
                "target_id": "CAB:content"
              }
            ],
            "_curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 0,
                  "inSlope": 1,
                  "outSlope": 1,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 1,
                  "inSlope": 1,
                  "outSlope": 1,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ]
            }
          }
        }
      ]
    },
    {
      "id": "CAB:content",
      "name": "CAB:content",
      "path": "fixture/CAB:content",
      "parent_id": "CAB:view",
      "child_ids": [],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 80,
            "y": 150
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "CAB:scrollimage",
          "script": "UIImage",
          "type": "MonoBehaviour",
          "data": {
            "m_Color": {
              "r": 1,
              "g": 1,
              "b": 1,
              "a": 1
            }
          }
        }
      ]
    },
    {
      "id": "CAB:mesh",
      "name": "CAB:mesh",
      "path": "fixture/CAB:mesh",
      "parent_id": "CAB:root",
      "child_ids": [],
      "game_object": {
        "data": {
          "m_IsActive": true
        }
      },
      "transform": {
        "type": "RectTransform",
        "raw": {
          "m_LocalPosition": {
            "x": 0,
            "y": 0,
            "z": 0
          },
          "m_LocalScale": {
            "x": 1,
            "y": 1,
            "z": 1
          },
          "m_LocalRotation": {
            "x": 0,
            "y": 0,
            "z": 0,
            "w": 1
          },
          "m_AnchorMin": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchorMax": {
            "x": 0.5,
            "y": 0.5
          },
          "m_AnchoredPosition": {
            "x": 0,
            "y": 0
          },
          "m_SizeDelta": {
            "x": 20,
            "y": 10
          },
          "m_Pivot": {
            "x": 0.5,
            "y": 0.5
          },
          "m_Father": {
            "m_FileID": 0,
            "m_PathID": "0"
          }
        }
      },
      "components": [
        {
          "id": "component:MeshFilter",
          "script": "MeshFilter",
          "type": "MonoBehaviour",
          "data": {
            "m_Mesh": {
              "target_id": "CAB:meshdata"
            }
          }
        },
        {
          "id": "component:MeshRenderer",
          "script": "MeshRenderer",
          "type": "MonoBehaviour",
          "data": {
            "m_SortingOrder": 99,
            "m_Materials": [
              {
                "target_id": "CAB:material"
              }
            ]
          }
        },
        {
          "id": "component:UISortingOrder",
          "script": "UISortingOrder",
          "type": "MonoBehaviour",
          "data": {
            "m_Enabled": false,
            "_renderType": 0,
            "_sortingOrderOffset": -5
          }
        }
      ]
    }
  ]
})fixture"},
        {"clips", R"fixture({
  "clips": [
    {
      "binding": "_animationIn",
      "id": "_animationIn",
      "last_key_time": 1,
      "curves": []
    },
    {
      "binding": "_animationLoop",
      "id": "_animationLoop",
      "last_key_time": 1,
      "curves": []
    },
    {
      "binding": "_animationOut",
      "id": "_animationOut",
      "last_key_time": 1,
      "curves": []
    },
    {
      "binding": "Animator",
      "id": "Normal",
      "last_key_time": 1,
      "wrap_mode": 2,
      "curves": [
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.x",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.y",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.z",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 0,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 0,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        }
      ]
    },
    {
      "binding": "Animator",
      "id": "Highlighted",
      "last_key_time": 1,
      "wrap_mode": 2,
      "curves": [
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.x",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.y",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.z",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 0,
                  "inSlope": -10,
                  "outSlope": -10,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": -10,
                  "inSlope": -10,
                  "outSlope": -10,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        }
      ]
    },
    {
      "binding": "Animator",
      "id": "Pressed",
      "last_key_time": 1,
      "wrap_mode": 2,
      "curves": [
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.x",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.y",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.z",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": -20,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": -20,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        }
      ]
    },
    {
      "binding": "Animator",
      "id": "Disabled",
      "last_key_time": 1,
      "wrap_mode": 2,
      "curves": [
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.x",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.y",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 999,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        },
        {
          "group": "m_FloatCurves",
          "path": "button",
          "attribute": "m_LocalPosition.z",
          "class_id": 224,
          "node_matches": [
            "CAB:-9007199254740993"
          ],
          "raw": {
            "curve": {
              "m_Curve": [
                {
                  "time": 0,
                  "value": 30,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                },
                {
                  "time": 1,
                  "value": 30,
                  "inSlope": 0,
                  "outSlope": 0,
                  "inWeight": 0.3333333333333333,
                  "outWeight": 0.3333333333333333,
                  "weightedMode": 0
                }
              ],
              "m_PreInfinity": 2,
              "m_PostInfinity": 2
            }
          }
        }
      ]
    }
  ],
  "controller_instances": [
    {
      "root_node_id": "CAB:-9007199254740993",
      "states": [
        {
          "name": "Normal",
          "bound_clip_id": "Normal",
          "source_clip_id": "Normal"
        },
        {
          "name": "Highlighted",
          "bound_clip_id": "Highlighted",
          "source_clip_id": "Highlighted"
        },
        {
          "name": "Pressed",
          "bound_clip_id": "Pressed",
          "source_clip_id": "Pressed"
        },
        {
          "name": "Disabled",
          "bound_clip_id": "Disabled",
          "source_clip_id": "Disabled"
        }
      ]
    }
  ]
})fixture"},
        {"controller-transitions", R"fixture({
  "controllers": [
    {
      "id": "controller",
      "state_machines": [
        {
          "index": 0,
          "states": [
            {
              "index": 0,
              "name": "Normal",
              "source_clip_id": "Normal",
              "speed": 1,
              "cycle_offset": 0
            },
            {
              "index": 1,
              "name": "Highlighted",
              "source_clip_id": "Highlighted",
              "speed": 2,
              "cycle_offset": 0
            },
            {
              "index": 2,
              "name": "Pressed",
              "source_clip_id": "Pressed",
              "speed": 1,
              "cycle_offset": 0
            },
            {
              "index": 3,
              "name": "Disabled",
              "source_clip_id": "Disabled",
              "speed": 1,
              "cycle_offset": 0
            }
          ]
        }
      ]
    }
  ],
  "instances": [
    {
      "root_node_id": "CAB:-9007199254740993",
      "controller_id": "controller",
      "source_interactable": true,
      "hover_enable_node_id": "CAB:hover",
      "transitions": [
        {
          "source_state_index": null,
          "destination_name": "Normal",
          "destination_bound_clip_id": "Normal",
          "destination_source_clip_id": "Normal",
          "duration": 0.2,
          "has_fixed_duration": true,
          "can_transition_to_self": false
        },
        {
          "source_state_index": null,
          "destination_name": "Highlighted",
          "destination_bound_clip_id": "Highlighted",
          "destination_source_clip_id": "Highlighted",
          "duration": 0,
          "has_fixed_duration": true,
          "can_transition_to_self": false
        },
        {
          "source_state_index": null,
          "destination_name": "Pressed",
          "destination_bound_clip_id": "Pressed",
          "destination_source_clip_id": "Pressed",
          "duration": 0.25,
          "has_fixed_duration": false,
          "can_transition_to_self": false
        },
        {
          "source_state_index": null,
          "destination_name": "Disabled",
          "destination_bound_clip_id": "Disabled",
          "destination_source_clip_id": "Disabled",
          "duration": 0,
          "has_fixed_duration": true,
          "can_transition_to_self": true
        }
      ]
    }
  ]
})fixture"},
        {"runtime-root-camera", R"fixture([
  {
    "type": "RectTransform",
    "cab": "Runtime",
    "path_id": "1",
    "data": {
      "m_LocalPosition": {
        "x": 0,
        "y": 0,
        "z": 100
      },
      "m_LocalScale": {
        "x": 1,
        "y": 1,
        "z": 1
      },
      "m_LocalRotation": {
        "x": 0,
        "y": 0,
        "z": 0,
        "w": 1
      },
      "m_AnchorMin": {
        "x": 0.5,
        "y": 0.5
      },
      "m_AnchorMax": {
        "x": 0.5,
        "y": 0.5
      },
      "m_AnchoredPosition": {
        "x": 0,
        "y": 0
      },
      "m_SizeDelta": {
        "x": 20,
        "y": 10
      },
      "m_Pivot": {
        "x": 0.5,
        "y": 0.5
      },
      "m_Father": {
        "m_FileID": 0,
        "m_PathID": "0"
      },
      "m_GameObject": {
        "m_PathID": "10"
      }
    }
  },
  {
    "type": "Transform",
    "cab": "Runtime",
    "path_id": "2",
    "data": {
      "m_LocalPosition": {
        "x": 0,
        "y": 0,
        "z": 0
      },
      "m_LocalScale": {
        "x": 1,
        "y": 1,
        "z": 1
      },
      "m_LocalRotation": {
        "x": 0,
        "y": 0,
        "z": 0,
        "w": 1
      },
      "m_AnchorMin": {
        "x": 0.5,
        "y": 0.5
      },
      "m_AnchorMax": {
        "x": 0.5,
        "y": 0.5
      },
      "m_AnchoredPosition": {
        "x": 0,
        "y": 0
      },
      "m_SizeDelta": {
        "x": 20,
        "y": 10
      },
      "m_Pivot": {
        "x": 0.5,
        "y": 0.5
      },
      "m_Father": {
        "m_FileID": 0,
        "m_PathID": "0"
      },
      "m_GameObject": {
        "m_PathID": "20"
      }
    }
  },
  {
    "type": "MonoBehaviour",
    "cab": "Runtime",
    "script": {
      "m_ClassName": "UIGyroscopeEffect"
    },
    "data": {
      "m_GameObject": {
        "m_PathID": "10"
      },
      "x": {
        "valueCurve": {
          "m_Curve": [
            {
              "time": 0,
              "value": -1,
              "inSlope": 2,
              "outSlope": 2,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            },
            {
              "time": 1,
              "value": 1,
              "inSlope": 2,
              "outSlope": 2,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            }
          ]
        },
        "maxAngle": 2
      },
      "y": {
        "valueCurve": {
          "m_Curve": [
            {
              "time": 0,
              "value": -1,
              "inSlope": 2,
              "outSlope": 2,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            },
            {
              "time": 1,
              "value": 1,
              "inSlope": 2,
              "outSlope": 2,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            }
          ]
        },
        "maxAngle": 3
      },
      "time": 0.2,
      "ease": 6,
      "enableDetect": true
    }
  },
  {
    "type": "MonoBehaviour",
    "cab": "Runtime",
    "script": {
      "m_ClassName": "UICanvasScaleHelper"
    },
    "data": {
      "m_GameObject": {
        "m_PathID": "10"
      }
    }
  },
  {
    "type": "Camera",
    "cab": "Runtime",
    "data": {
      "m_GameObject": {
        "m_PathID": "20"
      },
      "field of view": 60,
      "near clip plane": 0.3,
      "far clip plane": 1000,
      "m_NormalizedViewPortRect": {
        "x": 0,
        "y": 0,
        "width": 1,
        "height": 1
      },
      "m_LensShift": {
        "x": 0,
        "y": 0
      }
    }
  }
])fixture"},
        {"watch-blur", R"fixture({
  "wrapper": {
    "_options": {
      "animEase": 1
    }
  },
  "default_speed": 1,
  "entrance": {
    "curve": {
      "raw": {
        "curve": {
          "m_Curve": [
            {
              "time": 0,
              "value": 0,
              "inSlope": 1,
              "outSlope": 1,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            },
            {
              "time": 1,
              "value": 1,
              "inSlope": 1,
              "outSlope": 1,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            }
          ]
        }
      }
    }
  },
  "exit": {
    "curve": {
      "raw": {
        "curve": {
          "m_Curve": [
            {
              "time": 0,
              "value": 1,
              "inSlope": -1,
              "outSlope": -1,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            },
            {
              "time": 1,
              "value": 0,
              "inSlope": -1,
              "outSlope": -1,
              "inWeight": 0.3333333333333333,
              "outWeight": 0.3333333333333333,
              "weightedMode": 0
            }
          ]
        }
      }
    }
  }
})fixture"},
        {"sprites", R"fixture({
  "sprites": [],
  "source_textures": []
})fixture"},
        {"materials", R"fixture({
  "materials": [
    {
      "id": "CAB:material",
      "data": {
        "m_SavedProperties": {
          "m_TexEnvs": [
            [
              "_MainTex",
              {
                "m_Texture": {
                  "target_id": "CAB:-17"
                }
              }
            ]
          ]
        }
      }
    }
  ]
})fixture"},
        {"Equipring", R"fixture({
  "cab": "CAB",
  "path_id": "meshdata",
  "positions": [
    [
      0,
      0,
      0
    ],
    [
      0,
      1,
      0
    ],
    [
      1,
      0,
      0
    ]
  ],
  "uv0": [
    [
      0,
      0
    ],
    [
      0,
      1
    ],
    [
      1,
      0
    ]
  ],
  "indices": [
    0,
    1,
    2
  ]
})fixture"},
        {"watchline", R"fixture({
  "cab": "CAB",
  "path_id": "meshdata",
  "positions": [
    [
      0,
      0,
      0
    ],
    [
      0,
      1,
      0
    ],
    [
      1,
      0,
      0
    ]
  ],
  "uv0": [
    [
      0,
      0
    ],
    [
      0,
      1
    ],
    [
      1,
      0
    ]
  ],
  "indices": [
    0,
    1,
    2
  ]
})fixture"},
        {"Plane", R"fixture({
  "cab": "CAB",
  "path_id": "meshdata",
  "positions": [
    [
      0,
      0,
      0
    ],
    [
      0,
      1,
      0
    ],
    [
      1,
      0,
      0
    ]
  ],
  "uv0": [
    [
      0,
      0
    ],
    [
      0,
      1
    ],
    [
      1,
      0
    ]
  ],
  "indices": [
    0,
    1,
    2
  ]
})fixture"},
        {"Cylinder", R"fixture({
  "cab": "CAB",
  "path_id": "meshdata",
  "positions": [
    [
      0,
      0,
      0
    ],
    [
      0,
      1,
      0
    ],
    [
      1,
      0,
      0
    ]
  ],
  "uv0": [
    [
      0,
      0
    ],
    [
      0,
      1
    ],
    [
      1,
      0
    ]
  ],
  "indices": [
    0,
    1,
    2
  ]
})fixture"},
    };
    return Document::load("fixture/Scene",
                          [](const auto &path) { return resources.at(path.stem().string()); });
}
double area(const FilledGeometry &geometry) {
    double result = 0;
    for (const auto &q : geometry.quads) {
        double twice = 0;
        for (int i = 0; i < 4; ++i)
            twice += q[i].x * q[(i + 1) % 4].y - q[(i + 1) % 4].x * q[i].y;
        result += std::abs(twice) * 0.5;
    }
    return result;
}
void fills() {
    const Rect rect{{-4, 2}, {12, 8}};
    const std::array<Vec2, 4> uv = {Vec2{.2, .3}, Vec2{.2, .9}, Vec2{.8, .9}, Vec2{.8, .3}};
    for (int method = 0; method <= 4; ++method)
        for (int origin = 0; origin < (method < 2 ? 2 : 4); ++origin)
            for (bool clockwise : {false, true}) {
                auto empty = filledGeometry(rect, uv, method, origin, clockwise, .0009);
                check(empty.quads.empty(), "Source filled mesh threshold omits tiny fills");
                auto full = filledGeometry(rect, uv, method, origin, clockwise, 1);
                check(full.quads.size() == 1, "Full source fill retains one quad");
                near(area(full), 96, 1e-12, "Full source fill covers authored rectangle");
                auto half = filledGeometry(rect, uv, method, origin, clockwise, .5);
                near(area(half), 48, 1e-10, "Source cardinal half fill covers half rectangle");
                for (std::size_t i = 0; i < half.quads.size(); ++i)
                    for (int vertex = 0; vertex < 4; ++vertex) {
                        near(half.uvQuads[i][vertex].x,
                             .2 + (half.quads[i][vertex].x + 4) / 12 * .6, 1e-12,
                             "Radial cutting preserves source U mapping");
                        near(half.uvQuads[i][vertex].y, .3 + (half.quads[i][vertex].y - 2) / 8 * .6,
                             1e-12, "Radial cutting preserves source V mapping");
                    }
            }
    auto quarter = filledGeometry(rect, uv, 4, 0, true, .25);
    near(area(quarter), 24, 1e-10, "Source radial360 quarter fill");
    auto right = filledGeometry(rect, uv, 0, 1, true, .25);
    near(right.quads.front()[0].x, 5, 1e-12,
         "Horizontal right-origin crop starts at original fraction");
    rejects([&] { filledGeometry(rect, uv, 4, 0, true, std::numeric_limits<double>::quiet_NaN()); },
            "Nonfinite fill cannot enter vertex stream");
}
void layoutAndButtons() {
    auto doc = fixture();
    ButtonMotion motion(doc);
    const SourceId button = "CAB:-9007199254740993";
    auto input = [&](double time, double scroll = 1) {
        FrameInput in{{1920, 1080}, {Phase::visible, 1, {}, {}, 0}, {}};
        in.interaction = &motion;
        in.time = time;
        in.verticalNormalizedPosition = scroll;
        return in;
    };
    auto graphic = [&](const Frame &frame) -> const Graphic & {
        for (const auto &g : frame.graphics)
            if (g.componentId == "CAB:image")
                return g;
        throw std::runtime_error("Missing fixture Graphic");
    };
    check(motion.instanceIds().size() == 1,
          "Per-instance controller and tint share original button ID");
    motion.reset(10);
    auto normal = doc.frame(input(10));
    const auto *node = normal.node(button);
    check(node && node->rect, "Signed button instance retained in frame");
    near(node->rect->size.x, 20, 1e-12, "LayoutElement preferred width controls source layout");
    near(node->sceneWorld.values[12], -85, 1e-12,
         "Layout ignores disabled ILayoutIgnorer child and drives controller X");
    near(node->sceneWorld.values[13], 35, 1e-12,
         "Source group top-origin anchoring drives controller Y");
    check(node->sortingOrder == 1007,
          "Disabled registered canvas sorting writer receives panel base directly");
    check(graphic(normal).masks.empty(), "Override canvas clears inherited mask chain");
    check(graphic(normal).vertexColorReady, "UI vertex tint/color-space policy finalized once");
    near(graphic(normal).color[0], static_cast<float>(.6), 1e-8,
         "Default source normal ColorTint applies independently of Graphic color");
    check(normal.graphics.front().sortingOrder == -5 &&
              normal.graphics.front().textureId == "CAB:-17",
          "Renderer writer uses standalone order and original material texture ID");
    check(normal.scroll.has_value(), "Source ScrollRect produces bounds for native wheel owner");
    near(normal.scroll->hiddenLength, 100, 1e-12, "Original viewport/content scroll hidden length");
    near(normal.node("CAB:content")->sceneWorld.values[13], -50, 1e-12,
         "Normalized top aligns source upper bounds");
    near(normal.node("CAB:content")->sceneWorld.values[12], 10, 1e-12,
         "Source slant writer evaluates original Y curve");
    auto bottom = doc.frame(input(10, 0));
    near(bottom.node("CAB:content")->sceneWorld.values[13], 50, 1e-12,
         "Normalized bottom aligns source lower bounds");
    near(bottom.node("CAB:content")->sceneWorld.values[12], 20, 1e-12,
         "Scroll updates slant position from finalized Y");
    auto baseInput = input(10);
    baseInput.panelBase = 2000;
    check(doc.frame(baseInput).node(button)->sortingOrder == 2007,
          "Panel base never accumulates parent sorting offset");
    motion.setHovered(true, button, 10);
    auto halfTint = doc.frame(input(10.1));
    near(graphic(halfTint).color[0], .8, 1e-7, "Source ColorTween uses linear Float channel fade");
    motion.setHovered(true, button, 10.1);
    auto endpoint = doc.frame(input(11));
    near(endpoint.node(button)->sceneWorld.values[14], -10, 1e-12,
         "Highlighted finite endpoint ignores source clip loop flag");
    check(!motion.requiresFrames(11), "Settled Highlighted and tint stop demand scheduling");
    check(endpoint.node("CAB:hover")->active, "Highlighted enables original hover transform");
    motion.setState(ButtonState::selected, button, 11, true);
    near(graphic(doc.frame(input(11))).color[0], .4, 1e-7,
         "Selectable selected color has independent renderer channel");
    motion.setEnabled(false, button, 11);
    near(graphic(doc.frame(input(11))).color[0], 1, 1e-12,
         "Component disable clears CanvasRenderer tint to white");
    motion.setEnabled(true, button, 11);
    near(graphic(doc.frame(input(11))).color[0], .6, 1e-7,
         "Component enable resets source selection instantly");
    motion.reset(100);
    motion.setHovered(true, button, 100);
    near(doc.frame(input(100.125)).node(button)->sceneWorld.values[14], -2.5, 1e-10,
         "Controller speed samples source finite local time without wrapper easing");
    motion.setState(ButtonState::pressed, button, 100.125);
    auto pressed = doc.frame(input(100.1875));
    near(pressed.node(button)->sceneWorld.values[14], -11.875, 1e-10,
         "Normalized transition scales by previous length/speed and samples moving origin");
    motion.setHovered(false, button, 100.1875);
    check(motion.state(button) == ButtonState::pressed &&
              !doc.frame(input(100.1875)).node("CAB:hover")->active,
          "Leaving while pressed clears hover separately from state");
    motion.setState(ButtonState::normal, button, 100.1875);
    near(doc.frame(input(100.1875)).node(button)->sceneWorld.values[14], -11.875, 1e-10,
         "Interrupted blend snapshots current channels without snapping");
    near(doc.frame(input(100.2875)).node(button)->sceneWorld.values[14], -5.9375, 1e-10,
         "Interrupted transition blends from snapshot channels");
    near(doc.frame(input(100.2)).node(button)->sceneWorld.values[14], -5.9375, 1e-10,
         "Controller sampler monotonic clock cannot regress");
    motion.setState(ButtonState::highlighted, button, 200, true);
    auto reduced = input(200);
    reduced.reduceMotion = true;
    near(doc.frame(reduced).node(button)->sceneWorld.values[14], -10, 1e-12,
         "Reduced motion seeks source controller endpoint");
    check(!motion.requiresFrames(200), "Reduced controller/tint states require no perpetual timer");
    auto projected = doc.frame(reduced);
    const auto *allocation = projected.graphics.front().quads.data();
    Quaternion tilt{0, std::sin(.05), 0, std::cos(.05)};
    doc.reproject(projected, tilt);
    reduced.rootRotation = tilt;
    auto rebuilt = doc.frame(reduced);
    check(projected.graphics.front().quads.data() == allocation,
          "Slant reproject retains finalized geometry allocation");
    for (const auto &n : projected.nodes)
        for (int axis = 0; axis < 16; ++axis)
            near(n.world.values[axis], rebuilt.node(n.id)->world.values[axis], 1e-9,
                 "Gyro-only slant path equals full source layout frame");
    auto other = fixture();
    rejects([&] { other.frame(reduced); },
            "Button state cannot cross immutable document ownership");
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
    for (const auto &g : frame.graphics) {
        check(g.quads.size() == g.uvQuads.size(), "Mesh and source UV counts agree");
        if (!g.texturePath.empty())
            check(!g.textureId.empty(), "Original sprite retains exact native texture ID");
    }
    check(frame.nodes.size() == doc.nodeCount(),
          "Finalized actual hierarchy available to renderer and presentation adapters");
    for (std::size_t i = 1; i < frame.graphics.size(); ++i)
        check(frame.graphics[i - 1].sortingOrder <= frame.graphics[i].sortingOrder,
              "Actual source render sequence respects effective sorting writers");
    ButtonMotion buttons(doc);
    buttons.reset(1, true);
    check(buttons.instanceIds().size() >= 32,
          "All source Animator instances and Selectable bindings loaded");
    for (const auto &id : buttons.instanceIds())
        buttons.setState(ButtonState::highlighted, id, 2, true);
    FrameInput interactive{{1920, 1080}, {Phase::visible, doc.entranceDuration(), {}, {}, 0}, {}};
    interactive.interaction = &buttons;
    interactive.time = 2;
    interactive.reduceMotion = true;
    check(!doc.frame(interactive).graphics.empty(),
          "All actual source controller Highlighted endpoints sample together");
    check(!buttons.requiresFrames(2),
          "Actual reduced controller endpoints settle without idle timers");
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
        frameScheduling();
        projection();
        gyro();
        flicker();
        fills();
        layoutAndButtons();
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
