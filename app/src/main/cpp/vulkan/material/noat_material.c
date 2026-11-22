/*
 * noat_material.c - Material parameters for paper rendering
 */

#include "noat_material.h"
#include <string.h>

/* Default material: standard notebook paper */
const NoatMaterialParams noat_material_default = {
    .color = {0.92f, 0.94f, 0.98f},
    .roughness = 0.4f,
    .specular_strength = 0.1f,
    .specular_roughness = 0.6f,
    .fiber_strength = 0.5f,
    .grain_scale = 1.0f,
    .thickness = 0.08f,
};