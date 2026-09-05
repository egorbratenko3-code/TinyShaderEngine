#pragma once
#include "ShadowMap.h"
#include "Bloom.h"
#include "DepthOfField.h"
#include "ColorCorrect.h"
#include "GodRays.h"

// ==========================================================================
// All five deferred features now have real Vulkan implementations. This
// header is just the bundle that ties them together for Application/UI —
// each effect's own .h documents its GPU resources and pipeline.
// ==========================================================================
struct EffectStack {
    ShadowMap    shadowMap;
    Bloom        bloom;
    DepthOfField depthOfField;
    ColorCorrect colorCorrect;
    GodRays      godRays;
};
