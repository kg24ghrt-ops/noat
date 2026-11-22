/*
 * noat_material.h - Material parameters for paper rendering
 */

#ifndef NOAT_MATERIAL_H
#define NOAT_MATERIAL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float color[3];
    float roughness;
    float specular_strength;
    float specular_roughness;
    float fiber_strength;
    float grain_scale;
    float thickness;
} NoatMaterialParams;

#ifdef __cplusplus
}
#endif

#endif /* NOAT_MATERIAL_H */