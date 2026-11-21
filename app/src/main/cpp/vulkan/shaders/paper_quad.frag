#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 oColor;

// Off-white paper sheet color.
void main() {
    oColor = vec4(0.94, 0.94, 0.90, 1.0);
}
