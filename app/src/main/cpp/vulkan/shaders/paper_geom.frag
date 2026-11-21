#version 450
#extension GL_ARB_separate_shader_objects : enable

// Geometry/fragment shader for single-pass paper rendering.
// Outputs paper albedo color directly to swapchain.
layout(location = 0) out mediump vec4 oColor;

void main() {
    // Paper albedo (rough notebook paper, slightly off-white / warm tone)
    oColor = vec4(0.92, 0.94, 0.98, 1.0);
}
