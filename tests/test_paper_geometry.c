/*
 * test_paper_geometry.c
 *
 * Lightweight, dependency-free C unit test runner for paper_geometry.c.
 *
 * Compiles directly with clang/gcc using standard C (no external test
 * frameworks). Build and run:
 *
 *   clang -Wall -Wextra -std=c11 \
 *       -I app/src/main/cpp/vulkan \
 *       app/src/main/cpp/vulkan/paper_geometry.c \
 *       tests/test_paper_geometry.c \
 *       -o test_paper_geometry -lm \
 *       && ./test_paper_geometry
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "noat_paper_geometry.h"

/* ------------------------------------------------------------------ */
/* Minimal test runner                                                */
/* ------------------------------------------------------------------ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                    \
    printf("  TEST: %s ... ", name);                                  \
    fflush(stdout);                                                   \
    ++tests_run

#define PASS()                                                        \
    do {                                                              \
        printf("PASS\n");                                             \
        ++tests_passed;                                               \
    } while (0)

#define FAIL(msg)                                                     \
    do {                                                              \
        printf("FAIL: %s\n", msg);                                    \
        ++tests_failed;                                               \
    } while (0)

#define ASSERT(cond, msg)                                             \
    do {                                                              \
        if (!(cond)) {                                                \
            FAIL(msg);                                                \
            return;                                                   \
        }                                                             \
    } while (0)

#define ASSERT_FLOAT_EQ(actual, expected, tol, msg)                   \
    do {                                                              \
        float _diff = (actual) - (expected);                          \
        if (_diff < 0.0f) _diff = -_diff;                             \
        if (!(_diff <= (tol))) {                                      \
            printf("FAIL: %s (actual=%f, expected=%f, tol=%f)\n",   \
                   msg, (double)(actual), (double)(expected),       \
                   (double)(tol));                                  \
            ++tests_failed;                                           \
            return;                                                   \
        }                                                             \
    } while (0)

#define ASSERT_TRUE(cond, msg) ASSERT((cond) != 0, msg)
#define ASSERT_FALSE(cond, msg) ASSERT((cond) == 0, msg)
#define ASSERT_NOT_NULL(ptr, msg) ASSERT((ptr) != NULL, msg)
#define ASSERT_NULL(ptr, msg) ASSERT((ptr) == NULL, msg)

/* ------------------------------------------------------------------ */
/* Individual tests                                                   */
/* ------------------------------------------------------------------ */

static void test_default_size_returns_valid(void) {
    TEST("paper_geometry_default_size returns valid non-NULL PaperSize");

    const PaperSize *size = paper_geometry_default_size();
    ASSERT_NOT_NULL(size, "default size is NULL");
    ASSERT_TRUE(size->width_mm > 0.0f, "default width_mm not positive");
    ASSERT_TRUE(size->height_mm > 0.0f, "default height_mm not positive");
    ASSERT_TRUE(strcmp(size->id, "A4") == 0, "default id is not A4");
    PASS();
}

static void test_find_size_known_ids(void) {
    TEST("paper_geometry_find_size matches known IDs");

    const char *known_ids[] = { "A3", "A4", "A5", "A6", "LETTER", "LEGAL" };
    for (size_t i = 0; i < sizeof(known_ids) / sizeof(known_ids[0]); ++i) {
        const PaperSize *s = paper_geometry_find_size(known_ids[i]);
        char msg[128];
        snprintf(msg, sizeof(msg), "find_size(%s) returned NULL", known_ids[i]);
        ASSERT_NOT_NULL(s, msg);
        snprintf(msg, sizeof(msg), "find_size(%s) returned wrong id", known_ids[i]);
        ASSERT_TRUE(strcmp(s->id, known_ids[i]) == 0, msg);
        snprintf(msg, sizeof(msg), "find_size(%s) width_mm not positive", known_ids[i]);
        ASSERT_TRUE(s->width_mm > 0.0f, msg);
        snprintf(msg, sizeof(msg), "find_size(%s) height_mm not positive", known_ids[i]);
        ASSERT_TRUE(s->height_mm > 0.0f, msg);
    }
    PASS();
}

static void test_find_size_unknown_and_null(void) {
    TEST("paper_geometry_find_size returns NULL for unknown ID and NULL input");

    const PaperSize *s1 = paper_geometry_find_size("NOT_A_SIZE");
    ASSERT_NULL(s1, "find_size(\"NOT_A_SIZE\") should be NULL");

    const PaperSize *s2 = paper_geometry_find_size("");
    ASSERT_NULL(s2, "find_size(\"\") should be NULL");

    const PaperSize *s3 = paper_geometry_find_size(NULL);
    ASSERT_NULL(s3, "find_size(NULL) should be NULL");

    PASS();
}

static void test_compute_quad_null_inputs(void) {
    TEST("paper_geometry_compute_quad handles NULL inputs safely");

    PaperQuad result;
    result.qx = 1.0f;
    result.qy = 1.0f;

    /* NULL size -> output zeroed, no crash */
    paper_geometry_compute_quad(NULL, 1080.0f, 1920.0f, &result);
    ASSERT_TRUE(result.qx == 0.0f, "NULL size did not zero qx");
    ASSERT_TRUE(result.qy == 0.0f, "NULL size did not zero qy");

    /* NULL result pointer -> no crash */
    paper_geometry_compute_quad(&kPaperSizes[1], 1080.0f, 1920.0f, NULL);

    PASS();
}

static void test_compute_quad_invalid_dimensions(void) {
    TEST("paper_geometry_compute_quad zeroes output on invalid dimensions");

    PaperQuad result;
    result.qx = 1.0f;
    result.qy = 1.0f;

    paper_geometry_compute_quad(&kPaperSizes[1], 0.0f, 1920.0f, &result);
    ASSERT_TRUE(result.qx == 0.0f, "zero width did not zero qx");
    ASSERT_TRUE(result.qy == 0.0f, "zero width did not zero qy");

    result.qx = 1.0f;
    result.qy = 1.0f;
    paper_geometry_compute_quad(&kPaperSizes[1], 1080.0f, 0.0f, &result);
    ASSERT_TRUE(result.qx == 0.0f, "zero height did not zero qx");
    ASSERT_TRUE(result.qy == 0.0f, "zero height did not zero qy");

    result.qx = 1.0f;
    result.qy = 1.0f;
    paper_geometry_compute_quad(&kPaperSizes[1], -1080.0f, 1920.0f, &result);
    ASSERT_TRUE(result.qx == 0.0f, "negative width did not zero qx");
    ASSERT_TRUE(result.qy == 0.0f, "negative width did not zero qy");

    result.qx = 1.0f;
    result.qy = 1.0f;
    paper_geometry_compute_quad(&kPaperSizes[1], 1080.0f, -1920.0f, &result);
    ASSERT_TRUE(result.qx == 0.0f, "negative height did not zero qx");
    ASSERT_TRUE(result.qy == 0.0f, "negative height did not zero qy");

    /* NaN dimensions -> output zeroed */
    result.qx = 1.0f;
    result.qy = 1.0f;
    paper_geometry_compute_quad(&kPaperSizes[1], NAN, 1920.0f, &result);
    ASSERT_TRUE(result.qx == 0.0f, "NaN width did not zero qx");
    ASSERT_TRUE(result.qy == 0.0f, "NaN width did not zero qy");

    result.qx = 1.0f;
    result.qy = 1.0f;
    paper_geometry_compute_quad(&kPaperSizes[1], 1080.0f, NAN, &result);
    ASSERT_TRUE(result.qx == 0.0f, "NaN height did not zero qx");
    ASSERT_TRUE(result.qy == 0.0f, "NaN height did not zero qy");

    PASS();
}

static void test_compute_quad_portrait(void) {
    TEST("paper_geometry_compute_quad portrait (A4 on 1080x1920)");

    const PaperSize *a4 = paper_geometry_find_size("A4");
    ASSERT_NOT_NULL(a4, "A4 not found");

    PaperQuad q;
    q.qx = 1.0f;
    q.qy = 1.0f;

    paper_geometry_compute_quad(a4, 1080.0f, 1920.0f, &q);

    /* A4 aspect = 210/297 ≈ 0.7071. Screen aspect = 1080/1920 = 0.5625.
     * rel = 0.7071/0.5625 ≈ 1.257 > 1 → paper is wider relative to screen,
     * so fill width: qx = 0.85, qy = 0.85 / rel. */
    float expected_rel = (a4->width_mm / a4->height_mm) / (1080.0f / 1920.0f);
    ASSERT_TRUE(q.qx == 0.85f, "portrait qx not 0.85 (fill width)");
    ASSERT_FLOAT_EQ(q.qy, 0.85f / expected_rel, 1e-4f,
                    "portrait qy mismatch");

    /* qx, qy must be within (0, 1] */
    ASSERT_TRUE(q.qx > 0.0f && q.qx <= 1.0f, "portrait qx out of range");
    ASSERT_TRUE(q.qy > 0.0f && q.qy <= 1.0f, "portrait qy out of range");

    PASS();
}

static void test_compute_quad_landscape(void) {
    TEST("paper_geometry_compute_quad landscape (A4 on 1920x1080)");

    const PaperSize *a4 = paper_geometry_find_size("A4");
    ASSERT_NOT_NULL(a4, "A4 not found");

    PaperQuad q;
    q.qx = 1.0f;
    q.qy = 1.0f;

    paper_geometry_compute_quad(a4, 1920.0f, 1080.0f, &q);

    /* A4 aspect = 210/297 ≈ 0.7071. Screen aspect = 1920/1080 ≈ 1.7778.
     * rel = 0.7071/1.7778 ≈ 0.398 < 1 → paper is taller relative to screen,
     * so fill height: qy = 0.85, qx = 0.85 * rel. */
    float expected_rel = (a4->width_mm / a4->height_mm) / (1920.0f / 1080.0f);
    ASSERT_TRUE(q.qy == 0.85f, "landscape qy not 0.85 (fill height)");
    ASSERT_FLOAT_EQ(q.qx, 0.85f * expected_rel, 1e-4f,
                    "landscape qx mismatch");

    /* qx, qy must be within (0, 1] */
    ASSERT_TRUE(q.qx > 0.0f && q.qx <= 1.0f, "landscape qx out of range");
    ASSERT_TRUE(q.qy > 0.0f && q.qy <= 1.0f, "landscape qy out of range");

    PASS();
}

static void test_aspect_ratio_preservation(void) {
    TEST("paper_geometry_compute_quad preserves physical aspect ratio");

    /* Test multiple sizes across portrait and landscape orientations */
    const char *sizes[] = { "A3", "A4", "A5", "A6", "LETTER", "LEGAL" };
    float sw_vals[] = { 1080.0f, 1920.0f, 720.0f, 1440.0f };
    float sh_vals[] = { 1920.0f, 1080.0f, 1280.0f, 720.0f };
    int n_sizes = sizeof(sizes) / sizeof(sizes[0]);
    int n_pairs = sizeof(sw_vals) / sizeof(sw_vals[0]);

    for (int s = 0; s < n_sizes; ++s) {
        const PaperSize *sz = paper_geometry_find_size(sizes[s]);
        ASSERT_NOT_NULL(sz, "size not found");
        float paper_aspect = sz->width_mm / sz->height_mm;

        for (int p = 0; p < n_pairs; ++p) {
            PaperQuad q;
            q.qx = 1.0f;
            q.qy = 1.0f;
            paper_geometry_compute_quad(sz, sw_vals[p], sh_vals[p], &q);

            /* qx and qy should be positive and <= 1 */
            if (!(q.qx > 0.0f && q.qx <= 1.0f)) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "%s @ %.0fx%.0f: qx=%f out of range",
                         sizes[s], sw_vals[p], sh_vals[p], (double)q.qx);
                FAIL(msg);
                return;
            }
            if (!(q.qy > 0.0f && q.qy <= 1.0f)) {
                char msg[128];
                snprintf(msg, sizeof(msg),
                         "%s @ %.0fx%.0f: qy=%f out of range",
                         sizes[s], sw_vals[p], sh_vals[p], (double)q.qy);
                FAIL(msg);
                return;
            }

            /* Physical aspect preservation:
             * (qx * sw) / (qy * sh) should equal width_mm / height_mm */
            float rendered_aspect = (q.qx * sw_vals[p]) / (q.qy * sh_vals[p]);
            char msg[160];
            snprintf(msg, sizeof(msg),
                     "%s @ %.0fx%.0f: aspect mismatch "
                     "(rendered=%f, paper=%f)",
                     sizes[s], sw_vals[p], sh_vals[p],
                     (double)rendered_aspect, (double)paper_aspect);
            ASSERT_FLOAT_EQ(rendered_aspect, paper_aspect, 1e-4f, msg);
        }
    }
    PASS();
}

static void test_all_sizes_have_valid_dimensions(void) {
    TEST("all paper sizes have valid positive dimensions");

    for (size_t i = 0; i < kPaperSizeCount; ++i) {
        const PaperSize *s = &kPaperSizes[i];
        char msg[128];
        snprintf(msg, sizeof(msg), "size %s has non-positive width_mm", s->id);
        ASSERT_TRUE(s->width_mm > 0.0f, msg);
        snprintf(msg, sizeof(msg), "size %s has non-positive height_mm", s->id);
        ASSERT_TRUE(s->height_mm > 0.0f, msg);
        snprintf(msg, sizeof(msg), "size %s has NULL id", s->id);
        ASSERT_NOT_NULL(s->id, msg);
        snprintf(msg, sizeof(msg), "size %s has NULL name", s->id);
        ASSERT_NOT_NULL(s->name, msg);
    }
    PASS();
}

static void test_kpaper_size_count_matches_table(void) {
    TEST("kPaperSizeCount matches number of entries in table");

    size_t expected = 6; /* A3, A4, A5, A6, LETTER, LEGAL */
    ASSERT_TRUE(kPaperSizeCount == expected,
                "kPaperSizeCount does not match expected entry count");
    PASS();
}

/* ------------------------------------------------------------------ */
/* Test registry                                                      */
/* ------------------------------------------------------------------ */

typedef void (*test_fn)(void);

typedef struct {
    const char *name;
    test_fn fn;
} test_entry;

static const test_entry tests[] = {
    { "default_size_returns_valid", test_default_size_returns_valid },
    { "find_size_known_ids", test_find_size_known_ids },
    { "find_size_unknown_and_null", test_find_size_unknown_and_null },
    { "compute_quad_null_inputs", test_compute_quad_null_inputs },
    { "compute_quad_invalid_dimensions", test_compute_quad_invalid_dimensions },
    { "compute_quad_portrait", test_compute_quad_portrait },
    { "compute_quad_landscape", test_compute_quad_landscape },
    { "aspect_ratio_preservation", test_aspect_ratio_preservation },
    { "all_sizes_have_valid_dimensions", test_all_sizes_have_valid_dimensions },
    { "kpaper_size_count_matches_table", test_kpaper_size_count_matches_table },
};

int main(void) {
    printf("paper_geometry unit tests\n");
    printf("=========================\n\n");

    size_t n_tests = sizeof(tests) / sizeof(tests[0]);
    for (size_t i = 0; i < n_tests; ++i) {
        tests[i].fn();
    }

    printf("\n=========================\n");
    printf("Results: %d run, %d passed, %d failed\n",
           tests_run, tests_passed, tests_failed);

    if (tests_failed > 0) {
        printf("FAILURE: %d test(s) failed.\n", tests_failed);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}
