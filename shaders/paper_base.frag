#version 450
#extension GL_ARB_separate_shader_objects : enable

precision highp float;
precision highp int;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(push_constant) uniform PushConstants {
    float time;
    float resolution_x;
    float resolution_y;
    float seed;
} pc;

layout(std140, binding = 0) uniform PaperParams {
    vec2 paper_size_mm;
    float warmth;
    float fiber;
    float formation;
    float speckle;
    float tooth;
    float aniso;
    int rule_style;
    float rule_gap_mm;
    float rule_top_mm;
    float rule_weight_mm;
    vec3 rule_ink;
    int margin_enabled;
    float margin_x_mm;
    vec3 margin_ink;
    float print_fade;
    float light_az_deg;
    float light_el_deg;
    float light_temp_k;
    float light_intensity;
    float fill_intensity;
    float sheen;
    float exposure;
    float grain;
    float vignette;
} P;

// ---------- highp hash / noise ----------
highp float hash2(vec2 p) {
    highp vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

highp float hash3(vec3 p) {
    highp vec3 p3 = fract(vec3(p.xyz) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

highp float noise2(vec2 p) {
    highp vec2 i = floor(p);
    highp vec2 f = fract(p);
    highp vec2 u = f * f * (3.0 - 2.0 * f);
    highp float a = mix(hash2(i + vec2(0.0, 0.0)), hash2(i + vec2(1.0, 0.0)), u.x);
    highp float b = mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), u.x);
    return mix(a, b, u.y);
}

highp float ridged(vec2 p) {
    return 1.0 - abs(noise2(p) * 2.0 - 1.0);
}

// LOD-weighted fbm: fades bands below the pixel footprint (no shimmer).
highp float fbm_lod(vec2 p, float base_freq, float persistence, float lacunarity,
                    float footprint_mm) {
    highp float amp = 0.5;
    highp float freq = base_freq;
    highp float sum = 0.0;
    highp float amp_sum = 0.0;
    highp float lambda = 1.0 / freq;
    for (int i = 0; i < 5; i++) {
        highp float w = 1.0;
        if (lambda < footprint_mm) {
            w = smoothstep(0.0, footprint_mm * 0.6, lambda);
        }
        sum += noise2(p * freq) * amp * w;
        amp_sum += amp * w;
        p *= lacunarity;
        amp *= persistence;
        freq *= lacunarity;
        lambda /= lacunarity;
    }
    return amp_sum > 0.0001 ? sum / amp_sum : 0.0;
}

// ---------- color temperature (cheap, deterministic) ----------
mediump vec3 temp_rgb(float K) {
    highp float t = clamp(K / 100.0, 10.0, 100.0);
    mediump vec3 cool = vec3(0.72, 0.86, 1.00);
    mediump vec3 warm = vec3(1.00, 0.86, 0.66);
    return mix(cool, warm, smoothstep(0.25, 0.70, t));
}

void main() {
    highp vec2 screen = v_uv * vec2(pc.resolution_x, pc.resolution_y);
    highp float paper_aspect = P.paper_size_mm.x / P.paper_size_mm.y;
    highp float screen_aspect = pc.resolution_x / pc.resolution_y;

    // Fit sheet to frame (0.85 scale), letterboxed.
    highp float scale, off_x, off_y;
    if (paper_aspect > screen_aspect) {
        scale = 0.85 * pc.resolution_x / P.paper_size_mm.x;
        off_x = 0.0;
        off_y = (pc.resolution_y - P.paper_size_mm.y * scale) * 0.5;
    } else {
        scale = 0.85 * pc.resolution_y / P.paper_size_mm.y;
        off_x = (pc.resolution_x - P.paper_size_mm.x * scale) * 0.5;
        off_y = 0.0;
    }

    highp vec2 paper_mm = (screen - vec2(off_x, off_y)) / scale;
    highp float footprint_mm = max(length(dFdx(paper_mm)), length(dFdy(paper_mm)));

    // ---------- desk background ----------
    highp vec2 desk_uv = screen / vec2(pc.resolution_x, pc.resolution_y);
    highp float desk_grain = fbm_lod(desk_uv * 55.0, 1.0, 0.5, 2.1, 0.0);
    mediump vec3 desk_color = vec3(0.20, 0.155, 0.115) +
                              vec3(0.05, 0.035, 0.025) * desk_grain;

    highp float az = radians(P.light_az_deg);
    highp float el = radians(P.light_el_deg);
    highp vec2 light_pos = vec2(cos(az), sin(az)) * cos(el) * 0.42 + 0.5;
    highp float lamp_dist = length(desk_uv - light_pos);
    highp float lamp_pool = 1.0 - smoothstep(0.18, 0.80, lamp_dist);
    mediump vec3 desk_lit = desk_color * (0.30 + 0.70 * lamp_pool * P.light_intensity);

    highp float vign = length(desk_uv - 0.5) * 1.45;
    desk_lit *= 1.0 - P.vignette * smoothstep(0.0, 1.0, vign);

    // ---------- sheet mask (clean cut edge) ----------
    highp float edge_px = max(dFdx(screen.x), dFdy(screen.y));
    highp float eps_px = max(edge_px, 1.0);
    highp float eps_mm = eps_px / scale;
    highp float left = clamp(paper_mm.x / eps_mm, 0.0, 1.0);
    highp float right = clamp((P.paper_size_mm.x - paper_mm.x) / eps_mm, 0.0, 1.0);
    highp float top = clamp(paper_mm.y / eps_mm, 0.0, 1.0);
    highp float bottom = clamp((P.paper_size_mm.y - paper_mm.y) / eps_mm, 0.0, 1.0);
    highp float inside = left * right * top * bottom;

    highp float edge_dist_mm = min(min(paper_mm.x, P.paper_size_mm.x - paper_mm.x),
                                   min(paper_mm.y, P.paper_size_mm.y - paper_mm.y));
    highp float edge_dist_px = edge_dist_mm * scale;
    highp float sheet_edge = smoothstep(-edge_px * 1.0, edge_px * 1.0, edge_dist_px);
    highp float sheet_mask = inside * sheet_edge;

    // ---------- paper base ----------
    highp float ft_um = footprint_mm * 1000.0;
    highp float tooth_w = smoothstep(12.0, 90.0, ft_um);  // fade below ~12um pixel footprint

    // formation clouds (2-8 mm), low frequency flocculation
    highp float formation = fbm_lod(paper_mm * 0.16, 0.5, 0.6, 2.1, footprint_mm) * P.formation;

    // fiber anisotropy: machine direction mostly horizontal
    highp vec2 fiber_dir = vec2(1.0, 0.22 * (1.0 - P.aniso));
    highp vec2 fiber_p = vec2(dot(paper_mm * 0.62, fiber_dir),
                              paper_mm.y * 0.3 + 0.045 * sin(paper_mm.y * 0.65 + pc.seed * 0.01));
    highp float fiber = ridged(fiber_p) * P.fiber;

    // fine tooth (20-80 um), LOD faded, responds to raking light
    highp float tooth_height = fbm_lod(paper_mm * 16.0, 1.0, 0.55, 2.3, footprint_mm) *
                               P.tooth * tooth_w;

    // height field drives micro-normal + ink settling
    highp float height = formation * 0.42 + fiber * 0.34 + tooth_height * 0.62;

    // shives / bright filler specks (very subtle)
    highp float speckle = 0.0;
    {
        highp vec2 speck_ij = floor(paper_mm * 0.22);
        highp float s = hash2(speck_ij + vec2(pc.seed * 0.001, 0.0));
        if (s > 0.988) speckle = -0.045;   // dark shive
        if (s < 0.012) speckle = 0.032;    // bright filler
    }
    speckle *= P.speckle;

    // warm off-white base albedo
    mediump vec3 base = vec3(0.96, 0.94, 0.90);
    mediump vec3 warm_tint = vec3(1.0, 0.985, 0.955);
    mediump vec3 albedo = base * (1.0 + P.warmth * 0.055) * warm_tint;
    albedo += vec3(formation * 0.028 + fiber * 0.022 + tooth_height * 0.014 + speckle);
    albedo = clamp(albedo, 0.80, 1.04);

    // ---------- micro normal from paper height ----------
    highp float hdx = dFdx(height);
    highp float hdy = dFdy(height);
    mediump vec3 normal = normalize(vec3(-hdx * P.tooth * 0.045, -hdy * P.tooth * 0.045, 1.0));

    // ---------- lighting ----------
    mediump vec3 light_dir =
        vec3(cos(az) * cos(el), sin(az) * cos(el), sin(el));
    mediump vec3 view_dir = vec3(0.0, 0.0, 1.0);
    highp float ndl = clamp(dot(normal, light_dir), 0.0, 1.0);
    highp float wrap = 0.82 * ndl + 0.18;  // matte fiber scattering

    mediump vec3 key_col = temp_rgb(P.light_temp_k);
    mediump vec3 key = key_col * P.light_intensity * wrap;

    mediump vec3 fill_col = vec3(0.72, 0.84, 1.0);
    mediump vec3 fill = fill_col * P.fill_intensity * 0.5;

    mediump vec3 diffuse = key + fill;

    // broad low paper specular / sheen
    mediump vec3 reflect_dir = reflect(-light_dir, normal);
    highp float spec = pow(clamp(dot(reflect_dir, view_dir), 0.0, 1.0), 10.0);
    mediump vec3 sheen_col = key_col * P.sheen * spec * 0.45;

    mediump vec3 paper_lit = albedo * diffuse + sheen_col;
    paper_lit = clamp(paper_lit, 0.0, 1.6);

    // ---------- ruling (printed & absorbed, not vector-perfect) ----------
    highp float rule_ao = 1.0 - smoothstep(0.5, 1.2, height) * 0.2;
    highp float ruling_mask = 0.0;
    mediump vec3 ruling_col = P.rule_ink;

    if (P.rule_style == 0 || P.rule_style == 1 || P.rule_style == 2) {
        // college / wide / narrow: horizontal lines
        highp float gap = P.rule_gap_mm;
        highp float top = P.rule_top_mm;
        highp float li = floor((paper_mm.y - top) / gap + 0.5);
        highp float center = top + li * gap;
        highp float wave = sin(li * 1.7 + paper_mm.x * 0.045 + pc.seed * 0.013) * 0.045;
        highp float c = center + wave;
        highp float d = abs(paper_mm.y - c);
        highp float density = 0.80 + 0.20 * sin(li * 2.4 + pc.seed * 0.017);
        highp float hw = P.rule_weight_mm * 0.5;
        highp float feather = max(footprint_mm, 0.045) * 0.55;
        highp float fade =
            clamp(1.0 - (paper_mm.y - top) * 0.014 * P.print_fade, 0.5, 1.0);
        highp float line =
            (1.0 - smoothstep(hw, hw + feather, d)) * density * fade * rule_ao;
        ruling_mask = max(ruling_mask, line);
    } else if (P.rule_style == 3) {
        // graph: 5 mm grid
        highp float g = 5.0;
        highp float hw = P.rule_weight_mm * 0.5;
        highp float feather = max(footprint_mm, 0.045) * 0.55;
        highp float fade = clamp(1.0 - paper_mm.y * 0.014 * P.print_fade, 0.5, 1.0);

        highp float lix = floor((paper_mm.x - 4.0) / g + 0.5);
        highp float cx = 4.0 + lix * g;
        highp float wx = sin(lix * 1.7 + paper_mm.y * 0.045) * 0.045;
        highp float dx = abs(paper_mm.x - (cx + wx));
        highp float lx = (1.0 - smoothstep(hw, hw + feather, dx)) * fade * rule_ao;

        highp float liy = floor((paper_mm.y - 4.0) / g + 0.5);
        highp float cy = 4.0 + liy * g;
        highp float wy = sin(liy * 1.7 + paper_mm.x * 0.045) * 0.045;
        highp float dy = abs(paper_mm.y - (cy + wy));
        highp float ly = (1.0 - smoothstep(hw, hw + feather, dy)) * fade * rule_ao;

        ruling_mask = max(lx, ly);
    }
    // rule_style == 4: blank -> no rules

    // ---------- margin line ----------
    highp float margin_mask = 0.0;
    if (P.margin_enabled != 0) {
        highp float d = abs(paper_mm.x - P.margin_x_mm);
        highp float hw = P.rule_weight_mm * 0.5;
        highp float feather = max(footprint_mm, 0.045) * 0.55;
        margin_mask = (1.0 - smoothstep(hw, hw + feather, d));
    }

    // ink darkens paper where printed (absorption into fiber/valleys)
    mediump vec3 rule_tint = ruling_col * 0.92;
    albedo = mix(albedo, rule_tint, ruling_mask * 0.70);
    mediump vec3 margin_tint = P.margin_ink * 0.92;
    albedo = mix(albedo, margin_tint, margin_mask * 0.70);

    // re-apply lighting to inked regions (lines sit in tooth)
    paper_lit = albedo * diffuse + sheen_col * (1.0 - ruling_mask * 0.3 - margin_mask * 0.3);
    paper_lit = clamp(paper_lit, 0.0, 1.6);

    // ---------- thickness rim on lit cut edge ----------
    highp float rim = smoothstep(5.0, 0.0, edge_dist_px) * clamp(ndl, 0.0, 1.0);
    paper_lit += vec3(0.13) * rim * sheet_edge;

    // ---------- contact shadow on desk (tight near sheet, soft outward) ----------
    highp float contact = smoothstep(10.0, 0.0, edge_dist_px);
    mediump vec3 contact_shadow = vec3(0.0) * contact;  // applied by darkening desk below

    // ---------- combine desk + paper ----------
    mediump vec3 color = mix(desk_lit, paper_lit, sheet_mask);
    color *= (1.0 - contact * 0.5 * (1.0 - sheet_mask));

    // ---------- grain, vignette, exposure ----------
    highp float g = noise2(screen * 2.1 + vec2(pc.seed * 0.001)) * 2.0 - 1.0;
    color += vec3(g) * P.grain * 0.025;
    color = clamp(color, 0.0, 1.8);

    color *= P.exposure;
    // gentle filmic tonemap + gamma
    color = color * (color * 0.75 + 0.06) / (color * 0.75 + 0.20);
    color = pow(color, vec3(1.0 / 2.2));
    color = clamp(color, 0.0, 1.0);

    o_color = vec4(color, 1.0);
}
