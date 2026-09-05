#pragma once
#include "../../scene/Scene.h"

// Numeric controls for the Camera ("C") and Light ("L") nodes that can't be
// set by dragging alone: FOV, WASD/arrow-key speeds, light color/intensity,
// and light aim (yaw/pitch + spotlight cone angle). Position is still set
// by dragging the node markers in the Viewport, or by WASD/arrow keys for
// the camera.
class ScenePanel {
public:
    explicit ScenePanel(Scene& scene) : scene_(scene) {}
    void Draw();

private:
    Scene& scene_;
};
