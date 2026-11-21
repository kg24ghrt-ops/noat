#version 450
#extension GL_ARB_separate_shader_objects : enable

// Push constants carry the paper's physical dimensions (mm) and the surface
// pixel dimensions. The vertex shader derives the paper aspect ratio and
// letterboxes a quad inside the viewport so the sheet keeps its physical
// proportions (width/height in mm) in both portrait and landscape.
layout(push_constant) uniform PushConstants {
    float paperWidthMm;    // physical paper width  (mm)
    float paperHeightMm;   // physical paper height (mm)
    float surfaceWidth;    // surface width  (pixels)
    float surfaceHeight;   // surface height (pixels)
} pc;

void main() {
    float paperAspect = pc.paperWidthMm / pc.paperHeightMm;   // width / height
    float screenAspect = pc.surfaceWidth / pc.surfaceHeight;  // pixels
    float margin = 0.05;                                      // 5% margin

    // Letterbox the paper so its screen-pixel aspect equals paperAspect.
    float rel = paperAspect / screenAspect;
    float qx, qy;
    if (rel > 1.0) {
        // Paper is wider than screen relative to height -> fill width.
        qx = 1.0 - margin;
        qy = qx / rel;
    } else {
        // Paper is taller than screen relative to width -> fill height.
        qy = 1.0 - margin;
        qx = qy * rel;
    }

    // Two triangles forming a quad, clockwise winding (frontFace = CW).
    // NDC y points up in Vulkan; the viewport transform flips to window
    // coords (y down), so this is NOT inverted on screen.
    vec2 p;
    if (gl_VertexIndex == 0)      p = vec2(-qx,  qy); // top-left
    else if (gl_VertexIndex == 1) p = vec2( qx,  qy); // top-right
    else if (gl_VertexIndex == 2) p = vec2( qx, -qy); // bottom-right
    else if (gl_VertexIndex == 3) p = vec2(-qx,  qy); // top-left  (tri 2)
    else if (gl_VertexIndex == 4) p = vec2( qx, -qy); // bottom-right (tri 2)
    else                          p = vec2(-qx, -qy); // bottom-left

    gl_Position = vec4(p, 0.0, 1.0);
}
