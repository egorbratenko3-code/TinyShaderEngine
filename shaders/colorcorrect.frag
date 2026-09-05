#version 450
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D srcTex;
layout(push_constant) uniform PushConsts {
    float exposure;
    float contrast;
    float saturation;
    float _pad;
    float filterR, filterG, filterB;
} pc;

void main() {
    vec3 color = texture(srcTex, inUV).rgb;
    color *= vec3(pc.filterR, pc.filterG, pc.filterB);
    color *= exp2(pc.exposure);
    color = (color - 0.5) * pc.contrast + 0.5;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, pc.saturation);
    outColor = vec4(max(color, 0.0), 1.0);
}
