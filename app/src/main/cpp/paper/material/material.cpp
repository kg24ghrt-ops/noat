#include "material/material.h"

PaperMaterialParams PaperMaterialPresets::get(PaperType type) {
    PaperMaterialParams p;

    switch (type) {
        case PaperType::StandardNotebook:
            // Everyday notebook paper: slightly blue-white, moderate roughness
            p.color[0] = 0.92f; p.color[1] = 0.94f; p.color[2] = 0.98f;
            p.roughness = 0.40f;
            p.specular_strength = 0.08f;
            p.specular_roughness = 0.65f;
            p.fiber_strength = 0.45f;
            p.grain_scale = 1.0f;
            p.thickness = 0.08f;
            break;

        case PaperType::PremiumWriting:
            // High-quality writing paper: smooth, bright white, low roughness
            p.color[0] = 0.98f; p.color[1] = 0.98f; p.color[2] = 0.96f;
            p.roughness = 0.25f;
            p.specular_strength = 0.06f;
            p.specular_roughness = 0.75f;
            p.fiber_strength = 0.30f;
            p.grain_scale = 0.8f;
            p.thickness = 0.10f;
            break;

        case PaperType::Printer:
            // Standard office printer paper: bright, uniform, low texture
            p.color[0] = 0.96f; p.color[1] = 0.96f; p.color[2] = 0.96f;
            p.roughness = 0.30f;
            p.specular_strength = 0.07f;
            p.specular_roughness = 0.70f;
            p.fiber_strength = 0.35f;
            p.grain_scale = 0.9f;
            p.thickness = 0.09f;
            break;

        case PaperType::Recycled:
            // Recycled paper: grayish, rough, visible flecks
            p.color[0] = 0.85f; p.color[1] = 0.86f; p.color[2] = 0.84f;
            p.roughness = 0.65f;
            p.specular_strength = 0.04f;
            p.specular_roughness = 0.45f;
            p.fiber_strength = 0.80f;
            p.grain_scale = 1.3f;
            p.thickness = 0.11f;
            break;

        case PaperType::Watercolor:
            // Watercolor paper: textured, absorbent, off-white
            p.color[0] = 0.94f; p.color[1] = 0.92f; p.color[2] = 0.86f;
            p.roughness = 0.75f;
            p.specular_strength = 0.03f;
            p.specular_roughness = 0.40f;
            p.fiber_strength = 0.90f;
            p.grain_scale = 1.5f;
            p.thickness = 0.18f;
            break;

        case PaperType::Cotton:
            // Cotton rag paper: luxurious, soft, cream-colored
            p.color[0] = 0.96f; p.color[1] = 0.93f; p.color[2] = 0.88f;
            p.roughness = 0.35f;
            p.specular_strength = 0.05f;
            p.specular_roughness = 0.68f;
            p.fiber_strength = 0.55f;
            p.grain_scale = 1.1f;
            p.thickness = 0.12f;
            break;

        case PaperType::Handmade:
            // Handmade paper: irregular, deckle edges, variable thickness
            p.color[0] = 0.90f; p.color[1] = 0.87f; p.color[2] = 0.82f;
            p.roughness = 0.70f;
            p.specular_strength = 0.03f;
            p.specular_roughness = 0.42f;
            p.fiber_strength = 0.95f;
            p.grain_scale = 1.6f;
            p.thickness = 0.15f;
            break;

        case PaperType::RoughTextured:
            // Heavily textured paper: pronounced surface variation
            p.color[0] = 0.88f; p.color[1] = 0.87f; p.color[2] = 0.85f;
            p.roughness = 0.85f;
            p.specular_strength = 0.02f;
            p.specular_roughness = 0.35f;
            p.fiber_strength = 1.0f;
            p.grain_scale = 2.0f;
            p.thickness = 0.20f;
            break;

        case PaperType::Parchment:
            // Aged parchment: yellowed, dark, uneven coloration
            p.color[0] = 0.90f; p.color[1] = 0.82f; p.color[2] = 0.65f;
            p.roughness = 0.50f;
            p.specular_strength = 0.04f;
            p.specular_roughness = 0.55f;
            p.fiber_strength = 0.60f;
            p.grain_scale = 1.2f;
            p.thickness = 0.14f;
            break;
    }

    return p;
}
