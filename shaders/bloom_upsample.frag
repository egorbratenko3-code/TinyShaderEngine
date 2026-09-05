#version 450
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D srcTex; // the smaller (already-processed) mip
layout(push_constant) uniform PushConsts {
    vec2 texelSize; // 1 / source (smaller) mip resolution
    float radius;   // tent filter spread, driven by BloomEffect::intensity's companion radius
} pc;

void main() {
    // 3x3 tent filter — standard "dual filtering" upsample. The pipeline
    // this runs in has additive blending enabled, so this contributes onto
    // whatever the next-larger mip already holds (its own downsample data).
    vec2 t = pc.texelSize * pc.radius;
    vec3 sum = texture(srcTex, inUV).rgb * 4.0;
    sum += texture(srcTex, inUV + vec2(-t.x, 0)).rgb * 2.0;
    sum += texture(srcTex, inUV + vec2( t.x, 0)).rgb * 2.0;
    sum += texture(srcTex, inUV + vec2(0, -t.y)).rgb * 2.0;
    sum += texture(srcTex, inUV + vec2(0,  t.y)).rgb * 2.0;
    sum += texture(srcTex, inUV + vec2(-t.x, -t.y)).rgb;
    sum += texture(srcTex, inUV + vec2( t.x, -t.y)).rgb;
    sum += texture(srcTex, inUV + vec2(-t.x,  t.y)).rgb;
    sum += texture(srcTex, inUV + vec2( t.x,  t.y)).rgb;
    outColor = vec4(sum / 16.0, 1.0);
}
