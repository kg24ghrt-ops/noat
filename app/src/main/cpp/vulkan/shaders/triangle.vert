#version 450
#extension GL_ARB_separate_shader_objects : enable

// Shared vertex shader for both geometry and lighting subpasses.
// Emits a full-screen triangle using gl_VertexIndex (no vertex buffers needed).
void main() {
    vec2 pos;
    if (gl_VertexIndex == 0) {
        pos = vec2(-1.0, -1.0);
    } else if (gl_VertexIndex == 1) {
        pos = vec2(1.0, -1.0);
    } else {
        pos = vec2(0.0, 1.0);
    }
    gl_Position = vec4(pos, 0.0, 1.0);
}
