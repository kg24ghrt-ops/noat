#version 450
#extension GL_ARB_separate_shader_objects : enable
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(push_constant) uniform PushConstants {
    float time;
    float resolution_x;
    float resolution_y;
    float seed;
} pc;
void main() {
    vec2 uv = v_uv;
    vec3 col = mix(vec3(0.96, 0.94, 0.90), vec3(0.88, 0.84, 0.78), uv.y);
    col += 0.03 * sin(uv.x * 10.0 + pc.time) * cos(uv.y * 10.0);
    o_color = vec4(col, 1.0);
}
