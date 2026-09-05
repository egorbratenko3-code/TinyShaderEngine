#pragma once
#include "../core/Math.h"
#include <string>
#include <cmath>

// A single draggable XYZ node in the viewport (Feature 3).
// CameraNode, LightNode, and Model::transformHandle all use this; the
// viewport just picks the color/label per node type when drawing.
struct SceneNode {
    Vec3        position;
    Vec3        colorRGB;     // marker color, e.g. {0.2f,0.6f,1.0f} for camera
    std::string label;        // single glyph shown in the marker, "C" / "L" / "M"
    bool        isDragging = false;

    SceneNode() = default;
    SceneNode(Vec3 pos, Vec3 color, std::string lbl)
        : position(pos), colorRGB(color), label(std::move(lbl)) {}
};

// Shared yaw/pitch -> forward-vector math for both CameraNode and LightNode.
inline Vec3 DirectionFromYawPitch(float yawDegrees, float pitchDegrees) {
    constexpr float kDeg2Rad = 3.14159265f / 180.0f;
    float yaw = yawDegrees * kDeg2Rad, pitch = pitchDegrees * kDeg2Rad;
    return Vec3(std::cos(pitch) * std::cos(yaw), std::sin(pitch), std::cos(pitch) * std::sin(yaw)).normalized();
}

struct CameraNode : SceneNode {
    std::string name = "Camera 1";
    float fovDegrees = 45.0f;
    Vec3  target{0, 0, 0};      // recomputed each frame from yaw/pitch via SyncTargetFromOrientation()
    float yawDegrees = -90.0f;  // defaults chosen so target starts at world origin, matching the old fixed target
    float pitchDegrees = -20.5f;
    float moveSpeed = 3.0f;     // world units/sec, WASD fly movement
    float lookSpeed = 60.0f;    // degrees/sec, arrow-key look rotation

    Vec3 Forward() const { return DirectionFromYawPitch(yawDegrees, pitchDegrees); }
    void SyncTargetFromOrientation() { target = position + Forward(); }

    CameraNode() : SceneNode({0, 1.5f, 4.0f}, {0.20f, 0.55f, 1.00f}, "C") {
        SyncTargetFromOrientation();
    }
    CameraNode(Vec3 pos, std::string lbl = "C", std::string n = "Camera")
        : SceneNode(pos, {0.20f, 0.55f, 1.00f}, std::move(lbl)), name(std::move(n)) {
        SyncTargetFromOrientation();
    }
};

struct LightNode : SceneNode {
    std::string name = "Light 1";
    float intensity = 1.0f;
    Vec3  colorTint{1.0f, 0.75f, 0.4f};

    // Aiming (Feature request: "moved, aimed, and configured"). When isSpot
    // is true, scene.frag applies a soft cone falloff around AimDirection();
    // when false, the light behaves as the original omnidirectional point
    // light (aim is stored but ignored by shading).
    bool  isSpot = false;
    float aimYawDegrees = -90.0f;
    float aimPitchDegrees = -55.0f; // points generally back toward the origin by default
    float spotConeDegrees = 45.0f;  // half-angle of the cone

    Vec3 AimDirection() const { return DirectionFromYawPitch(aimYawDegrees, aimPitchDegrees); }

    LightNode() : SceneNode({2.0f, 3.0f, 2.0f}, {1.00f, 0.55f, 0.15f}, "L") {}
    LightNode(Vec3 pos, Vec3 color, float inten, std::string lbl = "L", std::string n = "Light")
        : SceneNode(pos, {1.00f, 0.55f, 0.15f}, std::move(lbl)), name(std::move(n)), intensity(inten), colorTint(color) {}
};

