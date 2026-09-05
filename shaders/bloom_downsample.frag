#version 450
layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D srcTex;
layout(push_constant) uniform PushConsts {
    vec2 texelSize;   // 1 / source resolution
    float threshold;  // bright-pass cutoff, only used when prefilter != 0
    int prefilter;    // 1 on the very first downsample (source = scene color), 0 otherwise
} pc;

void main() {
    // 13-tap Jimenez downsampling filter (samples center, 4 inner diagonals, 4 outer cardinals)
    // Eliminates fireflies, temporal flickering, and boxy artifacts.
    vec2 x = vec2(pc.texelSize.x, 0.0);
    vec2 y = vec2(0.0, pc.texelSize.y);

    vec3 a = texture(srcTex, inUV - 2.0 * x + 2.0 * y).rgb;
    vec3 b = texture(srcTex, inUV + 2.0 * y).rgb;
    vec3 c = texture(srcTex, inUV + 2.0 * x + 2.0 * y).rgb;

    vec3 d = texture(srcTex, inUV - 2.0 * x).rgb;
    vec3 e = texture(srcTex, inUV).rgb;
    vec3 f = texture(srcTex, inUV + 2.0 * x).rgb;

    vec3 g = texture(srcTex, inUV - 2.0 * x - 2.0 * y).rgb;
    vec3 h = texture(srcTex, inUV - 2.0 * y).rgb;
    vec3 i = texture(srcTex, inUV + 2.0 * x - 2.0 * y).rgb;

    vec3 j = texture(srcTex, inUV - x + y).rgb;
    vec3 k = texture(srcTex, inUV + x + y).rgb;
    vec3 l = texture(srcTex, inUV - x - y).rgb;
    vec3 m = texture(srcTex, inUV + x - y).rgb;

    vec3 color = e * 0.125;
    color += (a + c + g + i) * 0.03125;
    color += (b + d + f + h) * 0.0625;
    color += (j + k + l + m) * 0.125;

    if (pc.prefilter != 0) {
        // Quadratic threshold curve ("soft knee") for smooth transition
        float brightness = max(color.r, max(color.g, color.b));
        float knee = 0.5 * pc.threshold;
        float soft = brightness - pc.threshold + knee;
        soft = clamp(soft, 0.0, 2.0 * max(knee, 1e-4));
        soft = (soft * soft) / (4.0 * max(knee, 1e-4));
        float contribution = max(soft, brightness - pc.threshold) / max(brightness, 1e-5);
        color *= contribution;
    }

    outColor = vec4(max(color, vec3(0.0)), 1.0);
}

