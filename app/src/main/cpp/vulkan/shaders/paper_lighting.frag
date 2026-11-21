#version 450
#extension GL_ARB_separate_shader_objects : enable

// Lighting subpass fragment shader.
// Reads albedo and normal from input attachments (subpass 0 outputs),
// applies simple directional lighting, and writes final color to swapchain.
layout(input_attachment_index = 0, binding = 0) uniform subpassInput uAlbedo;
layout(input_attachment_index = 1, binding = 1) uniform subpassInput uNormal;

layout(location = 0) out mediump vec4 oColor;

void main() {
    mediump vec4 albedo = subpassLoad(uAlbedo);
    mediump vec4 normal = subpassLoad(uNormal);

    // Directional light from upper-left
    mediump vec3 lightDir = normalize(vec3(0.5, -1.0, 0.5));
    mediump vec3 N = normalize(normal.xyz);
    mediump float diff = max(dot(N, lightDir), 0.0);

    // Ambient + diffuse lighting
    mediump vec3 lighting = albedo.rgb * (0.3 + 0.7 * diff);
    oColor = vec4(lighting, albedo.a);
}
