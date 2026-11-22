#version 450
#extension GL_ARB_separate_shader_objects : enable

// Push constants carry the pre-computed quad half-width/half-height in NDC
// [-1, 1], computed on the CPU from the paper's physical dimensions (mm) and
// the surface pixel dimensions. This keeps all aspect-ratio math in C where
// it is visible and testable, and guarantees unstretched physical proportions
// for A5, A4, A3, Legal, etc.
layout(push_constant) uniform PushConstants {
    float qx;   /* normalised half-width  of the paper quad (NDC) */
    float qy;   /* normalised half-height of the paper quad (NDC) */
} pc;

void main() {
    // Two triangles forming a quad, clockwise winding.
    // frontFace = VK_FRONT_FACE_CLOCKWISE matches this vertex order.
    // NDC y points up in Vulkan; the viewport transform maps y-down to
    // window coords, so the quad is NOT inverted on screen.
    vec2 p;
    if (gl_VertexIndex == 0)      p = vec2(-pc.qx,  pc.qy); // top-left
    else if (gl_VertexIndex == 1) p = vec2( pc.qx,  pc.qy); // top-right
    else if (gl_VertexIndex == 2) p = vec2( pc.qx, -pc.qy); // bottom-right
    else if (gl_VertexIndex == 3) p = vec2(-pc.qx,  pc.qy); // top-left  (tri 2)
    else if (gl_VertexIndex == 4) p = vec2( pc.qx, -pc.qy); // bottom-right (tri 2)
    else                          p = vec2(-pc.qx, -pc.qy); // bottom-left

    gl_Position = vec4(p, 0.0, 1.0);
}
