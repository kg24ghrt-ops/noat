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
