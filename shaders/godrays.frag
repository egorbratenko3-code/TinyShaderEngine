#version 450
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

// Reads the same linear-depth output the scene pass produces (shared with
// DoF). Pixels at/near the far plane (nothing rendered there) are treated
// as "unoccluded sky" the light can shine through; anything with real
// geometry depth blocks the ray. This is the standard screen-space radial
// god-rays technique (Mitchell, GPU Gems 3-style).
layout(binding = 0) uniform sampler2D sceneDepth;

layout(push_constant) uniform PushConsts {
    vec2 lightScreenPos; // light's projected position in 0..1 UV space
    float density;
    float decay;
    float weight;
    float farPlaneDepth; // depth values >= this count as unoccluded
    int samples;
    float lightColorR, lightColorG, lightColorB;
} pc;

void main() {
    vec2 deltaUV = (inUV - pc.lightScreenPos) * (pc.density / float(max(pc.samples, 1)));
    vec2 uv = inUV;
    float illumination = 1.0;
    float accum = 0.0;

    const int MAX_SAMPLES = 128; // compile-time bound; pc.samples (UI-clamped <= 128) breaks out early
    for (int i = 0; i < MAX_SAMPLES; i++) {
        if (i >= pc.samples) break;
        uv -= deltaUV;
        float edgeDist = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
        float borderFade = smoothstep(0.0, 0.05, edgeDist);

        float d = texture(sceneDepth, clamp(uv, 0.0, 1.0)).r;
        float unoccluded = step(pc.farPlaneDepth, d);
        accum += unoccluded * illumination * pc.weight * borderFade;
        illumination *= pc.decay;
    }

    float dist = length(inUV - pc.lightScreenPos);
    float radialFalloff = exp(-dist * 1.2);

    outColor = vec4(vec3(pc.lightColorR, pc.lightColorG, pc.lightColorB) * accum * radialFalloff, 1.0);
}

