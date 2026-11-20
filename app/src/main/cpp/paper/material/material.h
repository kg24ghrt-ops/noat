#pragma once

#include <array>
#include <string>

// Paper material categories. Each has physically inspired parameters.
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
    // Base paper color (albedo) - off-white to yellowed
    float color[3] = {0.95f, 0.95f, 0.90f};

    // Surface roughness (0 = smooth, 1 = very rough)
    float roughness = 0.35f;

    // Specular strength - paper is mostly diffuse (low specular)
    float specular_strength = 0.10f;

    // Specular roughness (microfacet distribution)
    float specular_roughness = 0.60f;

    // Fiber detail strength (normal map intensity)
    float fiber_strength = 0.50f;

    // Grain scale for texture tiling
    float grain_scale = 1.0f;

    // Paper thickness approximation (for edge darkening)
    float thickness = 0.10f;
};

// Preset material parameters for each paper type.
class PaperMaterialPresets {
public:
    static PaperMaterialParams get(PaperType type);
};
