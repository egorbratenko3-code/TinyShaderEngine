#version 450
// Draws a single triangle that covers the whole screen — the standard
// vertex-buffer-free trick for post-process passes. Shared by every
// Bloom/DoF fragment shader below.
layout(location = 0) out vec2 outUV;

void main() {
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}
