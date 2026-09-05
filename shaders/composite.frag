#version 450
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D baseTex;     // scene color, after DoF + ColorCorrect (whichever ran)
layout(binding = 1) uniform sampler2D bloomTex;    // Bloom::GetResultView() — black if Bloom is off
layout(binding = 2) uniform sampler2D godRaysTex;  // GodRays::GetResultView() — black if GodRays is off

layout(push_constant) uniform PushConsts {
    float bloomIntensity;
    float godRaysIntensity;
    int   tonemapMode; // 0 = ACES Filmic, 1 = Reinhard, 2 = Linear (Off)
    float gamma;       // default 2.2
} pc;

// Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
vec3 acesFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    vec3 base = texture(baseTex, inUV).rgb;
    vec3 bloom = texture(bloomTex, inUV).rgb * pc.bloomIntensity;
    vec3 godRays = texture(godRaysTex, inUV).rgb * pc.godRaysIntensity;

    vec3 color = base + bloom + godRays;

    // Tonemapping
    if (pc.tonemapMode == 0) {
        color = acesFilm(color);
    } else if (pc.tonemapMode == 1) {
        color = color / (color + vec3(1.0));
    }

    // Gamma correction
    float g = pc.gamma > 0.1 ? 1.0 / pc.gamma : 1.0 / 2.2;
    color = pow(max(color, vec3(0.0)), vec3(g));

    outColor = vec4(color, 1.0);
}

