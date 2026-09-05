#version 450
layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4 outColor;
layout(location = 1) out float outLinearDepth; // consumed by DepthOfField / GodRays

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

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;
layout(set = 1, binding = 0) uniform sampler2D diffuseTexture;

layout(push_constant) uniform PushConsts {
    mat4 modelMatrix;
    vec4 materialColor;
} pc;

float sampleShadow(vec3 worldPos, vec3 N, vec3 L) {
    if (ubo.shadowParams.z < 0.5) return 1.0;
    vec4 lightClip = ubo.lightViewProj * vec4(worldPos, 1.0);
    vec3 proj = lightClip.xyz / lightClip.w;
    vec2 uv = proj.xy * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || proj.z < 0.0 || proj.z > 1.0) return 1.0;

    float cosTheta = clamp(dot(N, L), 0.0, 1.0);
    float bias = max(ubo.shadowParams.x * (1.0 - cosTheta), ubo.shadowParams.x * 0.2);
    float currentDepth = proj.z - bias;

    // 3x3 PCF filter for soft anti-aliased shadows
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / 2048.0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            shadow += texture(shadowMap, vec3(uv + vec2(x, y) * texelSize, currentDepth));
        }
    }
    return shadow / 9.0;
}

void main() {
    vec3 N = normalize(inNormal);
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(ubo.cameraPos.xyz - inWorldPos);

    vec3 albedo = pc.materialColor.rgb * texture(diffuseTexture, inUV).rgb;

    // Hemispheric ambient lighting (subtle sky/ground gradient)
    float upFactor = N.y * 0.5 + 0.5;
    vec3 ambient = albedo * mix(vec3(0.04, 0.04, 0.06), vec3(0.12, 0.14, 0.18), upFactor);

    int numLights = clamp(int(ubo.lightCount.x), 0, 16);
    vec3 totalDirect = vec3(0.0);

    for (int i = 0; i < numLights; ++i) {
        vec3 lightVec = ubo.lights[i].posType.xyz - inWorldPos;
        float dist = length(lightVec);
        if (dist < 0.0001) continue;
        vec3 L = lightVec / dist;

        // Smooth physical-like distance falloff
        float atten = 1.0 / (1.0 + 0.08 * dist + 0.02 * dist * dist);

        // Spotlight cone factor
        float spot = 1.0;
        if (ubo.lights[i].spotParams.x > 0.5) {
            float cosAngle = dot(-L, normalize(ubo.lights[i].dirCone.xyz));
            float cosOuter = ubo.lights[i].dirCone.w;
            float cosInner = mix(cosOuter, 1.0, 0.3);
            spot = smoothstep(cosOuter, cosInner, cosAngle);
        }

        // Shadow mapping (primary light i == 0)
        float shadow = (i == 0) ? sampleShadow(inWorldPos, N, L) : 1.0;

        // Diffuse
        float NdotL = max(dot(N, L), 0.0);

        // Blinn-Phong Specular
        vec3 H = normalize(L + V);
        float NdotH = max(dot(N, H), 0.0);
        float spec = pow(NdotH, 32.0) * (NdotL > 0.0 ? 1.0 : 0.0);

        vec3 lightCol = ubo.lights[i].colorInt.rgb * ubo.lights[i].colorInt.a;
        vec3 diffContrib = albedo * lightCol * NdotL;
        vec3 specContrib = vec3(0.8) * lightCol * spec;

        totalDirect += (diffContrib + specContrib) * (atten * spot * shadow);
    }

    outColor = vec4(ambient + totalDirect, 1.0);
    outLinearDepth = length(ubo.cameraPos.xyz - inWorldPos);
}

