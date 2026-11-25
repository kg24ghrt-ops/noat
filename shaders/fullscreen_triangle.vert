#version 450
#extension GL_ARB_separate_shader_objects : enable
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 p = vec2(-1.0, -1.0);
    if (gl_VertexIndex == 1) p = vec2(3.0, -1.0);
    if (gl_VertexIndex == 2) p = vec2(-1.0, 3.0);
    gl_Position = vec4(p, 0.0, 1.0);
    v_uv = p * 0.5 + 0.5;
}
