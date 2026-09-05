#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;

struct GPULight {
    vec4 posType;    // xyz = position, w = type (0 = point, 1 = spot)
    vec4 colorInt;   // rgb = color, a = intensity
    vec4 dirCone;    // xyz = aim direction, w = cos(halfAngle)
    vec4 spotParams; // x = isSpot, y = cosInner, z = range, w = unused
};

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 viewProj;
    mat4 lightViewProj;
    vec4 cameraPos;
    vec4 shadowParams; // x = bias, y = pcfRadius, z = shadowEnabled, w = unused
    vec4 lightCount;   // x = count, yzw unused
    GPULight lights[16];
} ubo;

// Per-draw: object transform (move/rotate) + flat material color. Declared
// identically in scene.frag — GLSL requires matching push_constant layout
// across every stage that uses the same range.
layout(push_constant) uniform PushConsts {
    mat4 modelMatrix;
    vec4 materialColor;
} pc;

void main() {
    vec4 worldPos = pc.modelMatrix * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;
    outNormal = mat3(pc.modelMatrix) * inNormal;
    outUV = inUV;
    gl_Position = ubo.viewProj * worldPos;
}

