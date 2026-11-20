#pragma once

#include <array>
#include <string>

// Paper material categories.
enum class PaperType {
    StandardNotebook,   // everyday notebook paper
    PremiumWriting,     // high-quality writing paper
    Printer,            // standard office printer paper
    Recycled,           // rough, flecked recycled paper
    Watercolor,         // textured, absorbent
    Cotton,             // cotton rag paper
    Handmade,           // irregular, deckle edges
    RoughTextured,      // heavily textured
    Parchment,          // yellowed, aged
};

// Material parameter set for a specific paper type.
struct PaperMaterialParams {
    float color[3] = {0.95f, 0.95f, 0.90f};
    float roughness = 0.35f;
    float specularStrength = 0.10f;
    float specularRoughness = 0.60f;
    float fiberStrength = 0.50f;
    float grainScale = 1.0f;
    float thickness = 0.10f;
};

// Preset material parameters for each paper type.
class PaperMaterialPresets {
public:
    static PaperMaterialParams get(PaperType type);
};
