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

const PaperSize *paper_geometry_default_size(void) {
    /* Default to A4: index 1 in kPaperSizesData. */
    return &kPaperSizesData[1];
}

const PaperSize *paper_geometry_find_size(const char *id) {
    if (id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < kPaperSizeCount; ++i) {
        if (strcmp(kPaperSizesData[i].id, id) == 0) {
            return &kPaperSizesData[i];
        }
    }
    return NULL;
}

void paper_geometry_compute_quad(const PaperSize *size,
                                 float surface_width_px,
                                 float surface_height_px,
                                 PaperQuad *result) {
    /* Guard against NULL result pointer so callers can safely probe. */
    if (result == NULL) {
        return;
    }

    /* Zero-initialise output so callers always get defined values. */
    result->qx = 0.0f;
    result->qy = 0.0f;

    if (size == NULL) {
        return;
    }
    if (surface_width_px <= 0.0f || surface_height_px <= 0.0f ||
        isnan(surface_width_px) || isnan(surface_height_px)) {
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

    /* Available scale after applying PAPER_SCALE (0.85 = 85% fill, 7.5%
     * margin on each side). */
    float scale = PAPER_SCALE;
    float qx, qy;

    if (rel > 1.0f) {
        /* Paper wider than screen relative to height → fill width. */
        qx = scale;
        qy = scale / rel;
    } else {
        /* Paper taller than screen relative to width → fill height. */
        qy = scale;
        qx = scale * rel;
    }

    result->qx = qx;
    result->qy = qy;
}
