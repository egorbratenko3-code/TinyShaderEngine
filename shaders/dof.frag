#version 450
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D sceneDepth; // linear view-space depth in .r, world units

layout(push_constant) uniform PushConsts {
    vec2 texelSize;
    float focusDistance;
    float focusRange;
    float bokehStrength;
} pc;

// 0 in the focus band, ramps to 1 over `focusRange` on either side.
float computeCoC(float depth) {
    float dist = abs(depth - pc.focusDistance);
    return clamp((dist - pc.focusRange) / max(pc.focusRange, 0.001), 0.0, 1.0);
}

void main() {
    float centerDepth = texture(sceneDepth, inUV).r;
    float centerCoC = computeCoC(centerDepth) * pc.bokehStrength;

    if (centerCoC <= 0.001) {
        outColor = vec4(texture(sceneColor, inUV).rgb, 1.0);
        return;
    }

    // 24-tap Vogel spiral distribution across the disc (golden angle = ~2.39996323 rad)
    // Produces buttery-smooth photographic bokeh blur without circular ring ghosting.
    const int TAPS = 24;
    const float GOLDEN_ANGLE = 2.39996323;
    vec3 sum = texture(sceneColor, inUV).rgb;
    float total = 1.0;

    for (int i = 0; i < TAPS; i++) {
        float r = sqrt(float(i) + 0.5) / sqrt(float(TAPS));
        float theta = float(i) * GOLDEN_ANGLE;
        vec2 offset = vec2(cos(theta), sin(theta)) * r * centerCoC * pc.texelSize * 24.0;

        vec2 sampleUV = clamp(inUV + offset, 0.0, 1.0);
        vec3 col = texture(sceneColor, sampleUV).rgb;
        float sampleDepth = texture(sceneDepth, sampleUV).r;
        float sampleCoC = computeCoC(sampleDepth) * pc.bokehStrength;

        // Weight samples to prevent sharp foreground bleeding into blurred background
        float weight = (sampleDepth < centerDepth) ? (sampleCoC / max(centerCoC, 1e-4)) : 1.0;
        weight = clamp(weight, 0.1, 1.0);

        sum += col * weight;
        total += weight;
    }

    outColor = vec4(sum / total, 1.0);
}

