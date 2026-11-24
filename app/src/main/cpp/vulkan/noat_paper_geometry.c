#include "noat_vulkan.h"
#include "noat_paper_geometry.h"

#include <math.h>
#include <string.h>

/*
 * One source of truth for paper sizes. Dimensions in millimetres, portrait
 * orientation. Legal uses the exact US size (215.9 × 355.6 mm), not the
 * rounded 216 × 356 mm.
 */
static const PaperSize kPaperSizesData[] = {
    { "A3",   "A3",   297.0f, 420.0f },
    { "A4",   "A4",   210.0f, 297.0f },
    { "A5",   "A5",   148.0f, 210.0f },
    { "A6",   "A6",   105.0f, 148.0f },
    { "LETTER", "Letter", 215.9f, 279.4f },
    { "LEGAL",  "Legal",  215.9f, 355.6f },
};

const size_t kPaperSizeCount =
    sizeof(kPaperSizesData) / sizeof(kPaperSizesData[0]);

const PaperSize *kPaperSizes = kPaperSizesData;

const NoatPaperSize *noat_paper_default_size(void) {
    /* Default to A4: index 1 in kPaperSizesData. */
    return (const NoatPaperSize *)&kPaperSizesData[1];
}

const NoatPaperSize *noat_paper_find_size(const char *id) {
    if (id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < kPaperSizeCount; ++i) {
        if (strcmp(kPaperSizesData[i].id, id) == 0) {
            return (const NoatPaperSize *)&kPaperSizesData[i];
        }
    }
    return NULL;
}

void noat_paper_compute_quad(const NoatPaperSize *size,
                             float surface_width_px,
                             float surface_height_px,
                             float *qx, float *qy) {
    /* Guard against NULL output pointers so callers can safely probe. */
    if (qx) *qx = 0.0f;
    if (qy) *qy = 0.0f;

    if (size == NULL) {
        return;
    }
    if (surface_width_px <= 0.0f || surface_height_px <= 0.0f ||
        isnan(surface_width_px) || isnan(surface_height_px) ||
        isinf(surface_width_px) || isinf(surface_height_px)) {
        return;
    }

    if (qx == NULL && qy == NULL) {
        return;
    }

    /* Physical paper aspect ratio: width / height in mm. */
    float paper_aspect = size->width_mm / size->height_mm;

    /* Viewport aspect ratio: width / height in pixels. */
    float screen_aspect = surface_width_px / surface_height_px;

    /* How wide the paper is relative to the screen.
     * rel > 1 : paper is wider than screen → fill width, constrain height.
     * rel < 1 : paper is taller than screen → fill height, constrain width. */
    float rel = paper_aspect / screen_aspect;

    /* Available scale after applying NOAT_PAPER_SCALE (0.85 = 85% fill,
     * 7.5% margin on each side). */
    float scale = NOAT_PAPER_SCALE;

    if (rel > 1.0f) {
        /* Paper wider than screen relative to height → fill width. */
        *qx = scale;
        *qy = scale / rel;
    } else {
        /* Paper taller than screen relative to width → fill height. */
        *qy = scale;
        *qx = scale * rel;
    }
}

/* Backward-compatible wrappers for the paper_geometry_* names declared
 * in noat_paper_geometry.h. These delegate to the noat_paper_* API. */
const PaperSize *paper_geometry_default_size(void) {
    return (const PaperSize *)noat_paper_default_size();
}

const PaperSize *paper_geometry_find_size(const char *id) {
    return (const PaperSize *)noat_paper_find_size(id);
}

void paper_geometry_compute_quad(const PaperSize *size,
                                 float surface_width_px,
                                 float surface_height_px,
                                 PaperQuad *result) {
    if (result == NULL) {
        noat_paper_compute_quad((const NoatPaperSize *)size,
                                surface_width_px, surface_height_px, NULL, NULL);
        return;
    }
    float qx, qy;
    noat_paper_compute_quad((const NoatPaperSize *)size,
                            surface_width_px, surface_height_px, &qx, &qy);
    result->qx = qx;
    result->qy = qy;
}
