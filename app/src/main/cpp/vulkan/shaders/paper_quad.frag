#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 oColor;

// Pseudo-random grain value in [0,1] from fragment coordinates.
// Simple hash gives a stable, deterministic paper texture without textures.
float paper_grain(vec2 uv) {
    float n = fract(sin(dot(uv, vec2(12.9898, 78.233))) * 43758.5453);
    return n;
}

// Simple directional lighting: Lambertian term from a fixed light direction.
// Light comes from upper-left, slight falloff toward bottom-right.
float paper_lighting(vec2 uv) {
    vec3 light_dir = normalize(vec3(0.5, -1.0, 0.5));
    vec3 normal   = vec3(0.0, 0.0, 1.0); // paper faces the viewer
    float diff = max(dot(normal, light_dir), 0.0);
    return 0.75 + 0.25 * diff; // 0.75..1.0 range, never fully dark
}

void main() {
    // Base paper colour: warm off-white (notebook paper).
    vec3 base = vec3(0.94, 0.94, 0.90);

    // Subtle grain: low-amplitude noise varies across the sheet so the paper
    // is not a flat digital colour. Grain is strongest toward the edges.
    vec2 uv = gl_FragCoord.xy / vec2(1920.0, 1080.0);
    float grain = paper_grain(uv * 8.0) * 0.02;
    float edge_vignette = 1.0 - 0.05 * (1.0 - uv.x) * (1.0 - uv.y);

    // Apply lighting + grain + vignette.
    vec3 lit = base * paper_lighting(uv);
    lit += grain;
    lit *= edge_vignette;

    oColor = vec4(lit, 1.0);
}
