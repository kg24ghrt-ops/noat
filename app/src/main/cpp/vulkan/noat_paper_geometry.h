#ifndef PAPER_GEOMETRY_H
#define PAPER_GEOMETRY_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Paper size table — one source of truth for real stationery dimensions.
 * All dimensions in millimetres, portrait orientation.
 * The id string (not an ordinal) is passed over JNI to the native renderer
 * so it stays stable across reordering of the table.
 */
typedef struct {
    const char *id;         /* stable id, passed over JNI */
    const char *name;       /* human-readable label shown in the UI */
    float width_mm;         /* physical paper width  (mm) */
    float height_mm;        /* physical paper height (mm) */
} PaperSize;

/* Computed quad dimensions (normalised device coordinates, [-1, 1]). */
typedef struct {
    float qx;   /* normalised half-width  of the paper quad */
    float qy;   /* normalised half-height of the paper quad */
} PaperQuad;

/* Sheet scale: fraction of the available viewport dimension the sheet fills.
 * 0.85 = 85%, leaving 7.5% margin on each side for clean horizontal breathing
 * room on left and right edges. */
#define PAPER_SCALE 0.85f

/* Number of entries in kPaperSizes. */
extern const size_t kPaperSizeCount;

/* The paper size table itself. Defined in paper_geometry.c. */
extern const PaperSize *kPaperSizes;

/* Return the default size (A4). */
const PaperSize *paper_geometry_default_size(void);

/* Look up a size by its stable id string. Returns NULL if not found. */
const PaperSize *paper_geometry_find_size(const char *id);

/* Compute quad half-width/half-height in NDC [-1,1] so the sheet keeps its
 * physical proportions (width_mm / height_mm) relative to the viewport aspect
 * ratio, fitted inside PAPER_SCALE of the smaller viewport dimension with
 * clean horizontal margins on both left and right edges. */
void paper_geometry_compute_quad(const PaperSize *size,
                                 float surface_width_px,
                                 float surface_height_px,
                                 PaperQuad *result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PAPER_GEOMETRY_H */
