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

layout(std140, binding = 0) uniform Params {
    // ---- paper stock ----
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
    // ---- lighting ----
    float light_az_deg;
    float light_el_deg;
    float light_intensity;
    float light_temp_k;
    float fill_intensity;
    float sheen;
    float exposure;
    float grain;
    float vignette;
    // ---- content ----
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
    // ---- wear / sheet shape ----
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

// ---------- highp hash / noise ----------
float hash2(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float hash3(vec3 p) {
    vec3 p3 = fract(vec3(p.xyz) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = mix(hash2(i + vec2(0.0, 0.0)), hash2(i + vec2(1.0, 0.0)), u.x);
    float b = mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), u.x);
    return mix(a, b, u.y);
}

float ridged(vec2 p) {
    return 1.0 - abs(noise2(p) * 2.0 - 1.0);
}

// LOD-weighted fbm: fades bands below the pixel footprint (no shimmer).
float fbm_lod(vec2 p, float base_freq, float persistence, float lacunarity,
              float footprint_mm) {
    float amp = 0.5;
    float freq = base_freq;
    float sum = 0.0;
    float amp_sum = 0.0;
    float lambda = 1.0 / freq;
    for (int i = 0; i < 5; i++) {
        float w = 1.0;
        if (lambda < footprint_mm) {
            w = smoothstep(0.0, footprint_mm * 0.6, lambda);
        }
        sum += noise2(p * freq) * amp * w;
        amp_sum += amp * w;
        p *= lacunarity;
        amp *= 0.55;
        freq *= lacunarity;
        lambda /= lacunarity;
    }
    return amp_sum > 0.0001 ? sum / amp_sum : 0.0;
}

// ---------- color temperature ----------
vec3 temp_rgb(float K) {
    float t = clamp(K / 100.0, 10.0, 100.0);
    vec3 cool = vec3(0.72, 0.86, 1.00);
    vec3 warm = vec3(1.00, 0.86, 0.66);
    return mix(cool, warm, smoothstep(0.25, 0.70, t));
}

// ---------- stroke shape: believable handwriting mark ----------
float stroke_mark(vec2 p, vec2 center, vec2 dir, float width, float length,
                  float pressure, float wobble_seed) {
    vec2 d = p - center;
    float along = dot(d, dir);
    float across = dot(d, vec2(-dir.y, dir.x));
    float t = clamp((along + length * 0.5) / length, 0.0, 1.0);
    float taper = 1.0 - 0.6 * abs(t - 0.5) * 2.0;
    taper = clamp(taper, 0.0, 1.0);
    float wob = sin(along * 12.0 + wobble_seed) * 0.12 + 1.0;
    float eff_width = width * taper * wob * pressure;
    float falloff = 1.0 - smoothstep(0.0, eff_width, abs(across));
    falloff *= clamp(1.0 - abs(along) / (length * 0.5 + 0.05), 0.0, 1.0);
    return falloff * pressure;
}

// ---------- word cluster along a ruled line ----------
float word_cluster(vec2 p, float line_y, float start_x, float num_words_seed) {
    float result = 0.0;
    int n_words = 3 + int(hash2(vec2(start_x, num_words_seed)) * 4.0);
    float cx = start_x;
    for (int k = 0; k < 6; k++) {
        if (k >= n_words) break;
        float wseed = hash2(vec2(start_x + float(k) * 0.5, num_words_seed + 1.0));
        float wx = cx + hash2(vec2(float(k), num_words_seed + 2.0)) * 3.5;
        float wy = line_y + hash2(vec2(float(k), num_words_seed + 3.0)) * 0.5 - 0.25;
        float wlen = 1.5 + hash2(vec2(float(k), num_words_seed + 4.0)) * 3.0;
        float wang = hash2(vec2(float(k), num_words_seed + 5.0)) * 0.3 - 0.15;
        float wpressure = 0.55 + hash2(vec2(float(k), num_words_seed + 6.0)) * 0.45;
        vec2 wdir = vec2(cos(wang), sin(wang));
        result += stroke_mark(p, vec2(wx, wy), wdir, 0.18, wlen, wpressure, wseed);
        cx = wx + wlen * 0.9 + 1.2;
    }
    return result;
}

// ---------- circle mask for holes ----------
float circle_mask(vec2 p, vec2 center, float radius) {
    float d = length(p - center);
    return 1.0 - smoothstep(0.0, radius, d);
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

    // ---------- sheet shape: curl ----------
    if (P.curl_amount > 0.001) {
        highp float curl = sin(paper_mm.y * P.curl_freq * 0.1 + pc.seed * 0.1) *
                           P.curl_amount * 6.0 *
                           (paper_mm.x / P.paper_size_mm_x);
        paper_mm.x += curl;
    }

    // ---------- sheet shape: corner lift ----------
    if (P.corner_lift_amount > 0.001) {
        vec2 corner = vec2(P.paper_size_mm_x, P.paper_size_mm_y);
        highp float corner_dist = length(paper_mm - corner);
        highp float lift = smoothstep(60.0, 8.0, corner_dist) * P.corner_lift_amount;
        vec2 center = vec2(P.paper_size_mm_x, P.paper_size_mm_y) * 0.5;
        paper_mm = center + (paper_mm - center) * (1.0 - lift * 0.25);
    }

    // ---------- desk background ----------
    highp vec2 desk_uv = screen / vec2(pc.resolution_x, pc.resolution_y);
    highp float desk_grain = fbm_lod(desk_uv * 55.0, 1.0, 0.5, 2.1, 0.0);
    vec3 desk_color = vec3(0.20, 0.155, 0.115) +
                      vec3(0.05, 0.035, 0.025) * desk_grain;

    highp float az = radians(P.light_az_deg);
    highp float el = radians(P.light_el_deg);
    highp vec2 light_pos = vec2(cos(az), sin(az)) * cos(el) * 0.42 + 0.5;
    highp float lamp_dist = length(desk_uv - light_pos);
    highp float lamp_pool = 1.0 - smoothstep(0.18, 0.80, lamp_dist);
    vec3 desk_lit = desk_color * (0.30 + 0.70 * lamp_pool * P.light_intensity);

    highp float vign = length(desk_uv - 0.5) * 1.45;
    desk_lit *= 1.0 - P.vignette * smoothstep(0.0, 1.0, vign);

    // ---------- sheet mask (edge) ----------
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

    // Edge style: torn or deckle adds jitter to edge distance
    highp float edge_jitter = 0.0;
    if (P.edge_style == 1.0) {
        edge_jitter = ridged(vec2(paper_mm.y * 0.1, pc.seed * 0.01)) * 1.5;
    } else if (P.edge_style == 2.0) {
        edge_jitter = ridged(vec2(paper_mm.y * 0.05, pc.seed)) * 2.5;
    }
    highp float effective_edge = edge_dist_mm + edge_jitter;
    highp float sheet_edge = smoothstep(-edge_px * 1.0, edge_px * 1.0, effective_edge * scale);
    highp float sheet_mask = inside * sheet_edge;

    // ---------- holes ----------
    float hole_mask = 0.0;
    float hole_radius = 4.0;
    if (P.hole_style == 1.0) {
        // 3-hole loose-leaf along left edge
        float hy = P.rule_top_mm + P.rule_gap_mm * 1.5;
        hole_mask = max(hole_mask, circle_mask(paper_mm, vec2(10.0, hy), hole_radius));
        hole_mask = max(hole_mask, circle_mask(paper_mm, vec2(10.0, hy + P.rule_gap_mm * 3.0), hole_radius));
        hole_mask = max(hole_mask, circle_mask(paper_mm, vec2(10.0, hy + P.rule_gap_mm * 6.0), hole_radius));
    } else if (P.hole_style == 2.0) {
        // 4-hole A4: two columns of two
        float hx1 = 15.0, hx2 = P.paper_size_mm_x - 15.0;
        float hy1 = P.rule_top_mm + P.rule_gap_mm * 1.0;
        float hy2 = hy1 + P.rule_gap_mm * 11.0;
        hole_mask = max(hole_mask, circle_mask(paper_mm, vec2(hx1, hy1), hole_radius));
        hole_mask = max(hole_mask, circle_mask(paper_mm, vec2(hx1, hy2), hole_radius));
        hole_mask = max(hole_mask, circle_mask(paper_mm, vec2(hx2, hy1), hole_radius));
        hole_mask = max(hole_mask, circle_mask(paper_mm, vec2(hx2, hy2), hole_radius));
    } else if (P.hole_style == 3.0) {
        // 2-hole
        float hy = P.rule_top_mm + P.rule_gap_mm * 3.5;
        hole_mask = max(hole_mask, circle_mask(paper_mm, vec2(12.0, hy), hole_radius));
        hole_mask = max(hole_mask, circle_mask(paper_mm, vec2(12.0, hy + P.rule_gap_mm * 7.0), hole_radius));
    }
    sheet_mask *= (1.0 - hole_mask);

    // Hole rim: fiber raggedness + compression shadow
    float hole_rim = 0.0;
    if (P.hole_style > 0.001) {
        hole_rim = hole_mask * 0.5;
        desk_lit *= (1.0 - hole_mask * 0.6);
    }

    // ---------- paper base ----------
    highp float ft_um = footprint_mm * 1000.0;
    highp float tooth_w = smoothstep(12.0, 90.0, ft_um);

    highp float formation = fbm_lod(paper_mm * 0.16, 0.5, 0.6, 2.1, footprint_mm) * P.formation;

    highp vec2 fiber_dir = vec2(1.0, 0.22 * (1.0 - P.aniso));
    highp vec2 fiber_p = vec2(dot(paper_mm * 0.62, fiber_dir),
                              paper_mm.y * 0.3 + 0.045 * sin(paper_mm.y * 0.65 + pc.seed * 0.01));
    highp float fiber = ridged(fiber_p) * P.fiber;

    highp float tooth_height = fbm_lod(paper_mm * 16.0, 1.0, 0.55, 2.3, footprint_mm) *
                               P.tooth * tooth_w;

    highp float height = formation * 0.42 + fiber * 0.34 + tooth_height * 0.62;

    float speckle = 0.0;
    {
        vec2 speck_ij = floor(paper_mm * 0.22);
        float s = hash2(speck_ij + vec2(P.content_seed * 0.001, 0.0));
        if (s > 0.988) speckle = -0.045;
        if (s < 0.012) speckle = 0.032;
    }
    speckle *= P.speckle;

    vec3 base = vec3(0.96, 0.94, 0.90);
    vec3 warm_tint = vec3(1.0, 0.985, 0.955);
    vec3 albedo = base * (1.0 + P.warmth * 0.055) * warm_tint;
    albedo += vec3(formation * 0.028 + fiber * 0.022 + tooth_height * 0.014 + speckle);
    albedo = clamp(albedo, 0.80, 1.04);

    // ---------- micro normal ----------
    float hdx = dFdx(height);
    float hdy = dFdy(height);
    vec3 normal = normalize(vec3(-hdx * P.tooth * 0.045, -hdy * P.tooth * 0.045, 1.0));

    // ---------- lighting ----------
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

    // ---------- ruling ----------
    float rule_ao = 1.0 - smoothstep(0.5, 1.2, height) * 0.2;
    float ruling_mask = 0.0;
    vec3 ruling_col = vec3(P.rule_ink_r, P.rule_ink_g, P.rule_ink_b);

    if (P.rule_style == 0.0 || P.rule_style == 1.0 || P.rule_style == 2.0) {
        float gap = P.rule_gap_mm;
        float topm = P.rule_top_mm;
        float li = floor((paper_mm.y - topm) / gap + 0.5);
        float center = topm + li * gap;
        float wave = sin(li * 1.7 + paper_mm.x * 0.045 + pc.seed * 0.013) * 0.045;
        float c = center + wave;
        float d = abs(paper_mm.y - c);
        float density = 0.80 + 0.20 * sin(li * 2.4 + pc.seed * 0.017);
        float hw = P.rule_weight_mm * 0.5;
        float feather = max(footprint_mm, 0.045) * 0.55;
        float fade =
            clamp(1.0 - (paper_mm.y - topm) * 0.014 * P.print_fade, 0.5, 1.0);
        float line = (1.0 - smoothstep(hw, hw + feather, d)) * density * fade * rule_ao;
        ruling_mask = max(ruling_mask, line);
    } else if (P.rule_style == 3.0) {
        float g = 5.0;
        float hw = P.rule_weight_mm * 0.5;
        float feather = max(footprint_mm, 0.045) * 0.55;
        float fade = clamp(1.0 - paper_mm.y * 0.014 * P.print_fade, 0.5, 1.0);

        float lix = floor((paper_mm.x - 4.0) / g + 0.5);
        float cx = 4.0 + lix * g;
        float wx = sin(lix * 1.7 + paper_mm.y * 0.045) * 0.045;
        float dx = abs(paper_mm.x - (cx + wx));
        float lx = (1.0 - smoothstep(hw, hw + feather, dx)) * fade * rule_ao;

        float liy = floor((paper_mm.y - 4.0) / g + 0.5);
        float cy = 4.0 + liy * g;
        float wy = sin(liy * 1.7 + paper_mm.x * 0.045) * 0.045;
        float dy = abs(paper_mm.y - (cy + wy));
        float ly = (1.0 - smoothstep(hw, hw + feather, dy)) * fade * rule_ao;

        ruling_mask = max(lx, ly);
    }

    // ---------- margin ----------
    float margin_mask = 0.0;
    if (P.margin_enabled != 0.0) {
        float d = abs(paper_mm.x - P.margin_x_mm);
        float hw = P.rule_weight_mm * 0.5;
        float feather = max(footprint_mm, 0.045) * 0.55;
        margin_mask = (1.0 - smoothstep(hw, hw + feather, d));
    }

    // ink darkens paper where printed
    albedo = mix(albedo, vec3(P.rule_ink_r, P.rule_ink_g, P.rule_ink_b) * 0.92, ruling_mask * 0.70);
    albedo = mix(albedo, vec3(P.margin_ink_r, P.margin_ink_g, P.margin_ink_b) * 0.92, margin_mask * 0.70);

    // re-apply lighting to inked regions
    paper_lit = albedo * diffuse + sheen_col * (1.0 - ruling_mask * 0.3 - margin_mask * 0.3);
    paper_lit = clamp(paper_lit, 0.0, 1.6);

    // ---------- thickness rim ----------
    float rim = smoothstep(5.0, 0.0, edge_dist_px) * clamp(ndl, 0.0, 1.0);
    paper_lit += vec3(0.13) * rim * sheet_edge;

    // ---------- content: pencil graphite ----------
    float graphite_mask = 0.0;
    if (P.graphite_amount > 0.001) {
        float writing = 0.0;
        if (P.rule_style == 0.0 || P.rule_style == 1.0 || P.rule_style == 2.0) {
            float gap = P.rule_gap_mm;
            float topm = P.rule_top_mm;
            int n_lines = int((P.paper_size_mm_y - topm) / gap) + 1;
            for (int ln = 0; ln < 8; ln++) {
                if (ln >= n_lines) break;
                float ly = topm + float(ln) * gap;
                float lseed = P.content_seed + float(ln) * 17.0;
                float wx = 45.0 + hash2(vec2(float(ln), lseed)) * 120.0;
                writing += word_cluster(paper_mm, ly, wx, lseed);
            }
        } else if (P.rule_style == 4.0) {
            float wx = 40.0 + hash2(vec2(P.content_seed, 1.0)) * 130.0;
            float ly = 60.0 + hash2(vec2(P.content_seed, 2.0)) * 160.0;
            writing = word_cluster(paper_mm, ly, wx, P.content_seed + 100.0);
        }
        writing = clamp(writing, 0.0, 1.0);
        float tooth_bite = smoothstep(0.0, 0.5, tooth_height * 1.5);
        graphite_mask = writing * tooth_bite * P.graphite_amount;
    }

    // ---------- content: pen ink ----------
    float pen_mask = 0.0;
    if (P.pen_amount > 0.001) {
        float pen_writing = 0.0;
        if (P.rule_style == 0.0 || P.rule_style == 1.0 || P.rule_style == 2.0) {
            float gap = P.rule_gap_mm;
            float topm = P.rule_top_mm;
            int n_lines = int((P.paper_size_mm_y - topm) / gap) + 1;
            for (int ln = 0; ln < 6; ln++) {
                if (ln >= n_lines) break;
                float ly = topm + float(ln) * gap + 0.35;
                float lseed = P.content_seed + float(ln) * 23.0 + 500.0;
                float wx = 55.0 + hash2(vec2(float(ln), lseed)) * 100.0;
                pen_writing += word_cluster(paper_mm, ly, wx, lseed);
            }
        }
        pen_writing = clamp(pen_writing, 0.0, 1.0);
        float feather = P.pen_feather * P.fiber * 0.6;
        if (feather > 0.001) {
            vec2 fiber_step = fiber_dir * feather * 0.5;
            float shifted = word_cluster(paper_mm + fiber_step, paper_mm.y, 55.0,
                                         P.content_seed + 500.0);
            pen_writing = max(pen_writing, shifted * 0.4);
        }
        pen_mask = pen_writing * P.pen_amount;
    }

    // ---------- content: teacher marks (red) ----------
    float teacher_mask = 0.0;
    if (P.teacher_amount > 0.001) {
        float cx = P.paper_size_mm_x - 35.0;
        float cy = P.rule_top_mm + 10.0;
        float check = stroke_mark(paper_mm, vec2(cx - 8.0, cy + 4.0), vec2(0.55, -0.85),
                                  0.35, 14.0, 0.9, P.content_seed + 9000.0);
        check += stroke_mark(paper_mm, vec2(cx - 2.0, cy - 4.0), vec2(0.5, 0.7),
                             0.35, 16.0, 0.9, P.content_seed + 9001.0);
        teacher_mask += check;

        float mx = 150.0 + hash2(vec2(P.content_seed + 77.0)) * 60.0;
        float my = P.rule_top_mm + P.rule_gap_mm * 3.0 + 4.0;
        float xseed = P.content_seed + 8000.0;
        teacher_mask += stroke_mark(paper_mm, vec2(mx - 6.0, my - 6.0), vec2(0.7, 0.7),
                                    0.4, 12.0, 0.85, xseed);
        teacher_mask += stroke_mark(paper_mm, vec2(mx + 6.0, my - 6.0), vec2(-0.7, 0.7),
                                    0.4, 12.0, 0.85, xseed + 1.0);

        float mdx = P.margin_x_mm;
        float mdy = P.rule_top_mm + P.rule_gap_mm * 5.0 + 6.0;
        float circ_d = length(paper_mm - vec2(mdx, mdy)) - 5.0;
        float circ = 1.0 - smoothstep(0.0, 0.5, abs(circ_d));
        teacher_mask += circ * 0.9;

        teacher_mask = clamp(teacher_mask, 0.0, 1.0) * P.teacher_amount;
    }

    // ---------- wear: coffee ring ----------
    if (P.coffee_amount > 0.001) {
        vec2 coffee_center = vec2(P.paper_size_mm_x * 0.7, P.paper_size_mm_y * 0.3);
        float coffee_dist = length(paper_mm - coffee_center);
        float ring_inner = 12.0;
        float ring_outer = 18.0;
        float coffee_ring = smoothstep(ring_outer, ring_inner, coffee_dist) * P.coffee_amount;
        float tide = smoothstep(ring_outer + 2.0, ring_outer - 1.0, coffee_dist) * 0.12;
        albedo *= (1.0 - coffee_ring * 0.75);
        albedo += vec3(0.92, 0.82, 0.70) * tide;
        albedo += vec3(0.35, 0.22, 0.12) * coffee_ring * 0.2;
    }

    // ---------- wear: water tide line ----------
    if (P.water_amount > 0.001) {
        vec2 water_center = vec2(P.paper_size_mm_x * 0.3, P.paper_size_mm_y * 0.7);
        float water_dist = length(paper_mm - water_center);
        float water_stain = smoothstep(30.0, 12.0, water_dist) * P.water_amount;
        albedo += vec3(0.04, 0.03, 0.02) * water_stain;
        paper_lit *= (1.0 - water_stain * 0.15);
    }

    // ---------- wear: foxing spots ----------
    if (P.foxing_amount > 0.001) {
        float foxing = 0.0;
        for (int i = 0; i < 10; i++) {
            vec2 spot_center = vec2(
                30.0 + hash2(vec2(float(i), P.content_seed + 1.0)) * (P.paper_size_mm_x - 60.0),
                40.0 + hash2(vec2(float(i), P.content_seed + 2.0)) * (P.paper_size_mm_y - 80.0));
            float spot_dist = length(paper_mm - spot_center);
            float spot = smoothstep(4.0, 0.8, spot_dist) * P.foxing_amount;
            foxing = max(foxing, spot);
        }
        albedo += vec3(0.55, 0.38, 0.28) * foxing * 0.35;
    }

    // ---------- wear: fingerprint oil smudge ----------
    if (P.fingerprint_amount > 0.001) {
        vec2 fp_center = vec2(P.paper_size_mm_x * 0.6, P.paper_size_mm_y * 0.55);
        float fp_dist = length(paper_mm - fp_center);
        float fingerprint = smoothstep(22.0, 4.0, fp_dist) * P.fingerprint_amount;
        albedo *= (1.0 - fingerprint * 0.45);
        sheen_col *= (1.0 + fingerprint * 0.6);
    }

    // ---------- wear: pencil smudge ----------
    if (P.pencil_smudge_amount > 0.001) {
        vec2 sm_center = vec2(P.paper_size_mm_x * 0.4, P.paper_size_mm_y * 0.4);
        float sm_dist = length(paper_mm - sm_center);
        float pencil_smudge = smoothstep(28.0, 6.0, sm_dist) * P.pencil_smudge_amount;
        albedo = mix(albedo, vec3(0.42, 0.42, 0.44), pencil_smudge * 0.55);
    }

    // ---------- wear: eraser abrasion ----------
    if (P.eraser_abrasion_amount > 0.001) {
        vec2 eras_center = vec2(P.paper_size_mm_x * 0.45, P.paper_size_mm_y * 0.25);
        float eras_dist = length(paper_mm - eras_center);
        float eraser_abr = smoothstep(18.0, 3.0, eras_dist) * P.eraser_abrasion_amount;
        albedo += vec3(0.06) * eraser_abr;
        height += eraser_abr * 0.15;
    }

    // ---------- wear: edge aging ----------
    if (P.edge_aging_amount > 0.001) {
        highp float aging = smoothstep(15.0, 0.0, edge_dist_mm) * P.edge_aging_amount;
        albedo += vec3(0.12, 0.06, 0.0) * aging;
    }

    albedo = clamp(albedo, 0.75, 1.06);

    // re-light content
    paper_lit = albedo * diffuse + sheen_col * (1.0 - graphite_mask * 0.2 - pen_mask * 0.1);
    paper_lit = clamp(paper_lit, 0.0, 1.6);

    // ---------- wear: backlight / translucency ----------
    if (P.translucency_amount > 0.001) {
        highp float transluc = smoothstep(3.0, 0.0, edge_dist_px) * P.translucency_amount;
        paper_lit += vec3(1.0, 0.92, 0.82) * transluc * 0.35;
    }

    // ---------- contact shadow on desk ----------
    float contact = smoothstep(10.0, 0.0, edge_dist_px);

    // ---------- combine desk + paper ----------
    vec3 color = mix(desk_lit, paper_lit, sheet_mask);
    color *= (1.0 - contact * 0.5 * (1.0 - sheet_mask));

    // Hole rim shadow on desk
    color *= (1.0 - hole_rim * 0.4);

    // ---------- grain, vignette, exposure ----------
    float g = noise2(screen * 2.1 + vec2(P.content_seed * 0.001)) * 2.0 - 1.0;
    color += vec3(g) * P.grain * 0.025;
    color = clamp(color, 0.0, 1.8);

    color *= P.exposure;
    color = color * (color * 0.75 + 0.06) / (color * 0.75 + 0.20);
    color = pow(color, vec3(1.0 / 2.2));
    color = clamp(color, 0.0, 1.0);

    o_color = vec4(color, 1.0);
}
