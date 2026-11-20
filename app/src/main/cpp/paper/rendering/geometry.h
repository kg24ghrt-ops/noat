#pragma once

#include "core/gl_object.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// Paper geometry representation. Supports adaptive subdivision for flat,
// curved, and folded paper surfaces.
//
// On Mali GPUs, we use CPU-generated subdivided meshes (Tier A/B) as the
// primary approach, with optional hardware tessellation (Tier C) only when
// explicitly available and beneficial (Arm recommends avoiding tessellation
// on tile-based GPUs where normal/parallax mapping suffices).
struct Vertex {
    float x, y, z;       // position
    float nx, ny, nz;    // normal
    float tx, ty;        // texture coordinates
};

struct Geometry {
    GLVertexArray vao;
    GLBuffer vbo;
    GLBuffer ebo;
    GLsizei index_count = 0;
    GLsizei vertex_count = 0;
    GLsizei width_segments = 0;
    GLsizei height_segments = 0;
    bool indexed = false;

    bool valid() const { return vao.valid() && vbo.valid(); }
};

// Adaptive subdivision parameters
struct SubdivisionParams {
    GLsizei min_segments = 8;      // minimum grid segments per axis
    GLsizei max_segments = 64;     // maximum grid segments per axis
    float screen_error_threshold = 0.5f; // pixels of error tolerance
    bool adaptive = false;         // enable adaptive subdivision
};

// Generates a flat paper plane grid with optional subdivision.
// segment_x / segment_y control grid density.
Geometry makePaperPlane(GLsizei segment_x, GLsizei segment_y,
                        float width = 1.0f, float height = 1.0f);

// Generates a curved paper surface (cylindrical bend along Y axis).
// bend_radians controls curvature amount.
Geometry makeCurvedPaper(GLsizei segment_x, GLsizei segment_y,
                         float width, float height,
                         float bend_radians);

// Generates edge-curled paper (corners curled up/down).
// curl_amount controls curl strength (0 = flat).
Geometry makeCurledPaper(GLsizei segment_x, GLsizei segment_y,
                         float width, float height,
                         float curl_amount);

// Upload geometry to GPU and create VAO/VBO/EBO.
Geometry uploadGeometry(const std::vector<Vertex>& vertices,
                        const std::vector<GLuint>& indices,
                        bool indexed);

// Binds the geometry for drawing.
void bindGeometry(const Geometry& geom);

// Draws the geometry.
void drawGeometry(const Geometry& geom);
