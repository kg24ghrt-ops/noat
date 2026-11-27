#version 450
#extension GL_ARB_separate_shader_objects : enable

precision highp float;
precision mediump int;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(push_constant) uniform PushConstants {
    float time;
    float resolution_x;
    float resolution_y;
    float seed;
} pc;

layout(std140, binding = 0) uniform Params {
    float paper_size_mm_x;
    float paper_size_mm_y;
    float warmth;
    float fiber;
    float formation;
    float speckle;
    float tooth;
    float aniso;
    float rule_style;
    float rule_gap_mm;
    float rule_top_mm;
    float rule_weight_mm;
    float rule_ink_r;
    float rule_ink_g;
    float rule_ink_b;
    float margin_enabled;
    float margin_x_mm;
    float margin_ink_r;
    float margin_ink_g;
    float margin_ink_b;
    float print_fade;
    float light_az_deg;
    float light_el_deg;
    float light_intensity;
    float light_temp_k;
    float fill_intensity;
    float sheen;
    float exposure;
    float grain;
    float vignette;
    float content_seed;
    float graphite_amount;
    float graphite_darkness;
    float graphite_smudge;
    float graphite_eraser;
    float pen_amount;
    float pen_color_r;
    float pen_color_g;
    float pen_color_b;
    float pen_feather;
    float teacher_amount;
    float teacher_color_r;
    float teacher_color_g;
    float teacher_color_b;
    float teacher_marks;
    float smudge_amount;
    float eraser_amount;
    float quality;
    float curl_amount;
    float curl_freq;
    float corner_lift_amount;
    float crease_amount;
    float crease_x;
    float crease_y;
    float crease_angle;
    float hole_style;
    float edge_style;
    float coffee_amount;
    float water_amount;
    float foxing_amount;
    float fingerprint_amount;
    float pencil_smudge_amount;
    float eraser_abrasion_amount;
    float edge_aging_amount;
    float translucency_amount;
} P;

layout(set = 0, binding = 1) uniform sampler2D u_albedo;
layout(set = 0, binding = 2) uniform sampler2D u_height;
layout(set = 0, binding = 3) uniform sampler2D u_sdf_atlas;

// ---------- glyph instance ----------
layout(std140, binding = 4) uniform GlyphInstances {
    // Up to 8 glyph instances
    vec4 glyph_pos[8];      // x, y, w, h (screen space)
    vec4 glyph_color[8];    // r, g, b, a
    vec2 glyph_scale[8];    // x, y scale variation
    float glyph_rot[8];     // rotation radians
    int glyph_cluster[8];   // cluster id
    int glyph_count;        // number of active glyphs
} Glyphs;

// ---------- deformation parameters ----------
layout(std140, binding = 5) uniform DeformParams {
    float warp_freq;        // spatial frequency of warp field
    float warp_amp;         // amplitude of warp (pixels)
    float warp_speed;       // animation speed
    float cluster_drift;    // per-cluster baseline drift
    float rot_variation;    // per-cluster rotation variation
    float size_variation;   // per-cluster size variation
    float advance_var;      // advance variation
    float ink_feather;      // feather width (pixels)
    float pressure_var;     // pressure/ink density variation
} Deform;

// ---------- hash and noise ----------
float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float hash23(vec3 p) {
    vec3 p3 = fract(vec3(p.xyz) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y + p3.z) * p3.z);
}

float noise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), f.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), f.x), f.y);
}

float fbm2(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    vec2 shift = vec2(100.0);
    for (int i = 0; i < 4; ++i) {
        v += a * noise2(p);
        p = p * 2.0 + shift;
        a *= 0.5;
    }
    return v;
}

// ---------- warp field ----------
vec2 warp_field(vec2 p, float t) {
    vec2 q = p * Deform.warp_freq + vec2(0.0, t * Deform.warp_speed);
    float n1 = noise2(p * 0.5 + vec2(0.0, t * 0.5));
    float n2 = noise2(p * 0.5 + vec2(1.0, t * 0.5 + 1.0));
    return vec2(n1, n2) * Deform.warp_amp;
}

// ---------- rotate point around center ----------
vec2 rotate_point(vec2 p, vec2 c, float angle) {
    float s = sin(angle), c_angle = cos(angle);
    vec2 d = p - c;
    return c + vec2(d.x * c_angle - d.y * s, d.x * s + d.y * c_angle);
}

// ---------- SDF sample with deformation ----------
float sample_sdf(vec2 glyph_uv, int cluster_idx, float t) {
    // Per-cluster baseline drift
    float drift = hash23(vec3(float(cluster_idx), 0.0, 0.0)) * Deform.cluster_drift;
    glyph_uv.y += drift * 0.5;

    // Per-cluster rotation
    float rot = hash23(vec3(float(cluster_idx), 1.0, 0.0)) * Deform.rot_variation;
    glyph_uv = rotate_point(glyph_uv, vec2(0.5, 0.5), rot);

    // Per-cluster size variation
    float size_var = 1.0 + hash23(vec3(float(cluster_idx), 2.0, 0.0)) * Deform.size_variation * 0.1;
    glyph_uv = vec2(0.5) + (glyph_uv - vec2(0.5)) * size_var;

    // Warp field
    vec2 warp = warp_field(glyph_uv, t);
    glyph_uv += warp;

    // Sample SDF atlas
    float sdf = texture(u_sdf_atlas, glyph_uv).r;
    return sdf;
}

// ---------- ink feathering ----------
float feather_edge(float sdf, float feather_px) {
    // sdf in [0,1], 128 = surface (0.5), <128 outside, >128 inside
    float sd = (sdf - 0.5) * 2.0; // convert to signed distance ~[-1,1]
    return smoothstep(-feather_px * 0.5, feather_px * 0.5, sd);
}

// ---------- color temperature ----------
vec3 temp_rgb(float K) {
    float t = clamp(K / 100.0, 10.0, 100.0);
    vec3 cool = vec3(0.72, 0.86, 1.00);
    vec3 warm = vec3(1.00, 0.86, 0.66);
    return mix(cool, warm, smoothstep(0.25, 0.70, t));
}

void main() {
    highp vec2 screen = v_uv * vec2(pc.resolution_x, pc.resolution_y);
    highp float paper_aspect = P.paper_size_mm_x / P.paper_size_mm_y;
    highp float screen_aspect = pc.resolution_x / pc.resolution_y;

    // Fit sheet to frame (0.85 scale), letterboxed.
    highp float scale, off_x, off_y;
    if (paper_aspect > screen_aspect) {
        scale = 0.85 * pc.resolution_x / P.paper_size_mm_x;
        off_x = 0.0;
        off_y = (pc.resolution_y - P.paper_size_mm_y * scale) * 0.5;
    } else {
        scale = 0.85 * pc.resolution_y / P.paper_size_mm_y;
        off_x = (pc.resolution_x - P.paper_size_mm_x * scale) * 0.5;
        off_y = 0.0;
    }

    highp vec2 paper_mm = (screen - vec2(off_x, off_y)) / scale;
    highp float footprint_mm = max(length(dFdx(paper_mm)), length(dFdy(paper_mm)));

    // ---------- sample baked albedo + height ----------
    vec4 baked = texture(u_albedo, v_uv);
    vec3 albedo = baked.rgb;
    float height = baked.a;

    float sheet_mask = step(0.001, albedo.r);

    // ---------- micro normal from baked height ----------
    float hdx = dFdx(height);
    float hdy = dFdy(height);
    vec3 normal = normalize(vec3(-hdx * P.tooth * 0.045, -hdy * P.tooth * 0.045, 1.0));

    // ---------- lighting ----------
    highp float az = radians(P.light_az_deg);
    highp float el = radians(P.light_el_deg);
    vec3 light_dir = vec3(cos(az) * cos(el), sin(az) * cos(el), sin(el));
    vec3 view_dir = vec3(0.0, 0.0, 1.0);
    float ndl = clamp(dot(normal, light_dir), 0.0, 1.0);
    float wrap = 0.82 * ndl + 0.18;

    vec3 key_col = temp_rgb(P.light_temp_k);
    vec3 key = key_col * P.light_intensity * wrap;

    vec3 fill_col = vec3(0.72, 0.84, 1.0);
    vec3 fill = fill_col * P.fill_intensity * 0.5;

    vec3 diffuse = key + fill;

    vec3 reflect_dir = reflect(-light_dir, normal);
    float spec = pow(clamp(dot(reflect_dir, view_dir), 0.0, 1.0), 10.0);
    vec3 sheen_col = key_col * P.sheen * spec * 0.45;

    vec3 paper_lit = albedo * diffuse + sheen_col;
    paper_lit = clamp(paper_lit, 0.0, 1.6);

    // ---------- text rendering (handwriting deformation) ----------
    vec3 text_color = vec3(0.0);
    float text_alpha = 0.0;

    for (int g = 0; g < Glyphs.glyph_count; ++g) {
        // Check if pixel is inside glyph bounding box
        vec2 glyph_origin = Glyphs.glyph_pos[g].xy;
        vec2 glyph_size = Glyphs.glyph_pos[g].zw;
        vec2 rel = (screen - glyph_origin) / glyph_size;

        if (rel.x >= 0.0 && rel.x <= 1.0 && rel.y >= 0.0 && rel.y <= 1.0) {
            // Sample SDF with deformation
            float sdf = sample_sdf(rel, Glyphs.glyph_cluster[g], pc.time);
            float alpha = feather_edge(sdf, Deform.ink_feather);

            // Pressure/ink density variation per cluster
            float pressure = 0.8 + hash23(vec3(float(Glyphs.glyph_cluster[g]), 3.0, 0.0)) * Deform.pressure_var * 0.4;

            // Advance variation
            float adv_var = 1.0 + hash23(vec3(float(Glyphs.glyph_cluster[g]), 4.0, 0.0)) * Deform.advance_var * 0.1;

            float glyph_alpha = alpha * pressure * adv_var * Glyphs.glyph_color[g].a;

            // Use the color from the glyph instance
            vec3 glyph_col = Glyphs.glyph_color[g].rgb;

            // Blend with existing text (later glyphs overlay)
            text_alpha = max(text_alpha, glyph_alpha);
            text_color = mix(text_color, glyph_col, glyph_alpha);
        }
    }

    // Composite text onto paper
    if (text_alpha > 0.001) {
        paper_lit = mix(paper_lit, paper_lit * 0.3 + text_color * 0.9, text_alpha);
    }

    // ---------- thickness rim on lit cut edge ----------
    highp float edge_px = max(dFdx(screen.x), dFdy(screen.y));
    highp float eps_px = max(edge_px, 1.0);
    highp float eps_mm = eps_px / scale;
    highp float left = clamp(paper_mm.x / eps_mm, 0.0, 1.0);
    highp float right = clamp((P.paper_size_mm_x - paper_mm.x) / eps_mm, 0.0, 1.0);
    highp float top = clamp(paper_mm.y / eps_mm, 0.0, 1.0);
    highp float bottom = clamp((P.paper_size_mm_y - paper_mm.y) / eps_mm, 0.0, 1.0);
    highp float inside = left * right * top * bottom;

    highp float edge_dist_mm = min(min(paper_mm.x, P.paper_size_mm_x - paper_mm.x),
                                   min(paper_mm.y, P.paper_size_mm_y - paper_mm.y));
    highp float edge_dist_px = edge_dist_mm * scale;
    float rim = smoothstep(5.0, 0.0, edge_dist_px) * clamp(ndl, 0.0, 1.0);
    paper_lit += vec3(0.13) * rim * sheet_mask;

    // ---------- contact shadow on desk ----------
    float contact = smoothstep(10.0, 0.0, edge_dist_px);

    // ---------- combine ----------
    // Desk background (procedural)
    highp vec2 desk_uv = screen / vec2(pc.resolution_x, pc.resolution_y);
    vec3 desk_color = vec3(0.20, 0.155, 0.115);
    highp float lamp_dist = length(desk_uv - vec2(cos(az), sin(az)) * cos(el) * 0.42 + 0.5);
    highp float lamp_pool = 1.0 - smoothstep(0.18, 0.80, lamp_dist);
    vec3 desk_lit = desk_color * (0.30 + 0.70 * lamp_pool * P.light_intensity);
    highp float vign = length(desk_uv - 0.5) * 1.45;
    desk_lit *= 1.0 - P.vignette * smoothstep(0.0, 1.0, vign);

    vec3 color = mix(desk_lit, paper_lit, sheet_mask);
    color *= (1.0 - contact * 0.5 * (1.0 - sheet_mask));

    // ---------- grain, vignette, exposure ----------
    float g = fract(sin(dot(screen, vec2(12.9898, 78.233))) * 43758.5453) * 2.0 - 1.0;
    color += vec3(g) * P.grain * 0.025;
    color = clamp(color, 0.0, 1.8);

    color *= P.exposure;
    color = color * (color * 0.75 + 0.06) / (color * 0.75 + 0.20);
    color = pow(color, vec3(1.0 / 2.2));
    color = clamp(color, 0.0, 1.0);

    o_color = vec4(color, 1.0);
}
