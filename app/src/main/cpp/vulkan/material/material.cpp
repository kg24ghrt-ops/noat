#include "material/material.h"

PaperMaterialParams PaperMaterialPresets::get(PaperType type) {
    PaperMaterialParams p;

    switch (type) {
        case PaperType::StandardNotebook:
            p.color[0] = 0.92f; p.color[1] = 0.94f; p.color[2] = 0.98f;
            p.roughness = 0.40f;
            p.specularStrength = 0.08f;
            p.specularRoughness = 0.65f;
            p.fiberStrength = 0.45f;
            p.grainScale = 1.0f;
            p.thickness = 0.08f;
            break;

        case PaperType::PremiumWriting:
            p.color[0] = 0.98f; p.color[1] = 0.98f; p.color[2] = 0.96f;
            p.roughness = 0.25f;
            p.specularStrength = 0.06f;
            p.specularRoughness = 0.75f;
            p.fiberStrength = 0.30f;
            p.grainScale = 0.8f;
            p.thickness = 0.10f;
            break;

        case PaperType::Printer:
            p.color[0] = 0.96f; p.color[1] = 0.96f; p.color[2] = 0.96f;
            p.roughness = 0.30f;
            p.specularStrength = 0.07f;
            p.specularRoughness = 0.70f;
            p.fiberStrength = 0.35f;
            p.grainScale = 0.9f;
            p.thickness = 0.09f;
            break;

        case PaperType::Recycled:
            p.color[0] = 0.85f; p.color[1] = 0.86f; p.color[2] = 0.84f;
            p.roughness = 0.65f;
            p.specularStrength = 0.04f;
            p.specularRoughness = 0.45f;
            p.fiberStrength = 0.80f;
            p.grainScale = 1.3f;
            p.thickness = 0.11f;
            break;

        case PaperType::Watercolor:
            p.color[0] = 0.94f; p.color[1] = 0.92f; p.color[2] = 0.86f;
            p.roughness = 0.75f;
            p.specularStrength = 0.03f;
            p.specularRoughness = 0.40f;
            p.fiberStrength = 0.90f;
            p.grainScale = 1.5f;
            p.thickness = 0.18f;
            break;

        case PaperType::Cotton:
            p.color[0] = 0.96f; p.color[1] = 0.93f; p.color[2] = 0.88f;
            p.roughness = 0.35f;
            p.specularStrength = 0.05f;
            p.specularRoughness = 0.68f;
            p.fiberStrength = 0.55f;
            p.grainScale = 1.1f;
            p.thickness = 0.12f;
            break;

        case PaperType::Handmade:
            p.color[0] = 0.90f; p.color[1] = 0.87f; p.color[2] = 0.82f;
            p.roughness = 0.70f;
            p.specularStrength = 0.03f;
            p.specularRoughness = 0.42f;
            p.fiberStrength = 0.95f;
            p.grainScale = 1.6f;
            p.thickness = 0.15f;
            break;

        case PaperType::RoughTextured:
            p.color[0] = 0.88f; p.color[1] = 0.87f; p.color[2] = 0.85f;
            p.roughness = 0.85f;
            p.specularStrength = 0.02f;
            p.specularRoughness = 0.35f;
            p.fiberStrength = 1.0f;
            p.grainScale = 2.0f;
            p.thickness = 0.20f;
            break;

        case PaperType::Parchment:
            p.color[0] = 0.90f; p.color[1] = 0.82f; p.color[2] = 0.65f;
            p.roughness = 0.50f;
            p.specularStrength = 0.04f;
            p.specularRoughness = 0.55f;
            p.fiberStrength = 0.60f;
            p.grainScale = 1.2f;
            p.thickness = 0.14f;
            break;
    }

    return p;
}
