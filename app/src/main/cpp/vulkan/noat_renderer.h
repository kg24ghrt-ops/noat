#ifndef NOAT_RENDERER_H
#define NOAT_RENDERER_H

#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NoatRenderer NoatRenderer;

typedef struct {
    float direction[3];
    float color[3];
    float intensity;
    float ambient;
    float env_color[3];
    float env_intensity;
} NoatLightParams;

typedef struct {
    float color[3];
    float roughness;
    float specular_strength;
    float specular_roughness;
    float fiber_strength;
    float grain_scale;
    float thickness;
} NoatMaterialParams;

NoatRenderer *noat_renderer_create(void);
void noat_renderer_destroy(NoatRenderer *renderer);

int noat_renderer_init(NoatRenderer *renderer, ANativeWindow *window);
void noat_renderer_resize(NoatRenderer *renderer, int width, int height);
void noat_renderer_render_frame(NoatRenderer *renderer);
void noat_renderer_set_paper_size(NoatRenderer *renderer, const char *size_id);
void noat_renderer_set_material(NoatRenderer *renderer, const NoatMaterialParams *material);
void noat_renderer_set_light(NoatRenderer *renderer, const NoatLightParams *light);
void noat_renderer_set_quality(NoatRenderer *renderer, int quality);
void noat_renderer_shutdown(NoatRenderer *renderer);

#ifdef __cplusplus
}
#endif

#endif /* NOAT_RENDERER_H */