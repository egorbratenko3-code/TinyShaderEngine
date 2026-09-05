#version 450

// Depth-only pass: position is the only attribute we need. Matches the
// binding/stride of the full Vertex struct (position, normal, uv) so it can
// bind the same vertex buffer the (future) main shading pass will use —
// only location 0 is actually read here.
layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConsts {
    mat4 lightMVP; // lightProj * lightView * modelMatrix (model is identity for now)
} pc;

void main() {
    gl_Position = pc.lightMVP * vec4(inPosition, 1.0);
}
