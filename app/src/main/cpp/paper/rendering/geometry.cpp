#include "rendering/geometry.h"

#include <cmath>
#include <stdexcept>

Geometry makePaperPlane(GLsizei segment_x, GLsizei segment_y,
                        float width, float height) {
    if (segment_x < 1) segment_x = 1;
    if (segment_y < 1) segment_y = 1;

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<size_t>((segment_x + 1) * (segment_y + 1)));

    for (GLsizei iy = 0; iy <= segment_y; ++iy) {
        float v = static_cast<float>(iy) / static_cast<float>(segment_y);
        float y = (v - 0.5f) * height;
        for (GLsizei ix = 0; ix <= segment_x; ++ix) {
            float u = static_cast<float>(ix) / static_cast<float>(segment_x);
            float x = (u - 0.5f) * width;
            float z = 0.0f;
            vertices.push_back(Vertex{x, y, z, 0.0f, 0.0f, 1.0f, u, v});
        }
    }

    std::vector<GLuint> indices;
    indices.reserve(static_cast<size_t>(segment_x * segment_y * 6));
    for (GLsizei iy = 0; iy < segment_y; ++iy) {
        for (GLsizei ix = 0; ix < segment_x; ++ix) {
            GLuint tl = static_cast<GLuint>(iy * (segment_x + 1) + ix);
            GLuint tr = tl + 1;
            GLuint bl = static_cast<GLuint>((iy + 1) * (segment_x + 1) + ix);
            GLuint br = bl + 1;
            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }

    return uploadGeometry(vertices, indices, true);
}

Geometry makeCurvedPaper(GLsizei segment_x, GLsizei segment_y,
                         float width, float height,
                         float bend_radians) {
    if (segment_x < 1) segment_x = 1;
    if (segment_y < 1) segment_y = 1;

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<size_t>((segment_x + 1) * (segment_y + 1)));

    // Cylindrical bend along the Y axis (paper curls left-right)
    // Radius controls how tight the bend is
    float radius = (bend_radians != 0.0f) ? (width / bend_radians) : 0.0f;

    for (GLsizei iy = 0; iy <= segment_y; ++iy) {
        float v = static_cast<float>(iy) / static_cast<float>(segment_y);
        float y = (v - 0.5f) * height;
        for (GLsizei ix = 0; ix <= segment_x; ++ix) {
            float u = static_cast<float>(ix) / static_cast<float>(segment_x);
            float x_centered = (u - 0.5f) * width;

            float x, z;
            float nx, ny, nz;

            if (radius != 0.0f) {
                // Bend around Y axis: x = r*sin(theta), z = r*(1-cos(theta))
                float theta = x_centered / radius;
                x = radius * std::sin(theta);
                z = radius * (1.0f - std::cos(theta));
                // Normal rotates with the surface
                nx = std::sin(theta);
                ny = 0.0f;
                nz = std::cos(theta);
            } else {
                x = x_centered;
                z = 0.0f;
                nx = 0.0f;
                ny = 0.0f;
                nz = 1.0f;
            }

            vertices.push_back(Vertex{x, y, z, nx, ny, nz, u, v});
        }
    }

    std::vector<GLuint> indices;
    indices.reserve(static_cast<size_t>(segment_x * segment_y * 6));
    for (GLsizei iy = 0; iy < segment_y; ++iy) {
        for (GLsizei ix = 0; ix < segment_x; ++ix) {
            GLuint tl = static_cast<GLuint>(iy * (segment_x + 1) + ix);
            GLuint tr = tl + 1;
            GLuint bl = static_cast<GLuint>((iy + 1) * (segment_x + 1) + ix);
            GLuint br = bl + 1;
            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }

    return uploadGeometry(vertices, indices, true);
}

Geometry makeCurledPaper(GLsizei segment_x, GLsizei segment_y,
                         float width, float height,
                         float curl_amount) {
    if (segment_x < 1) segment_x = 1;
    if (segment_y < 1) segment_y = 1;

    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<size_t>((segment_x + 1) * (segment_y + 1)));

    float half_w = width * 0.5f;
    float half_h = height * 0.5f;

    for (GLsizei iy = 0; iy <= segment_y; ++iy) {
        float v = static_cast<float>(iy) / static_cast<float>(segment_y);
        float y = (v - 0.5f) * height;
        for (GLsizei ix = 0; ix <= segment_x; ++ix) {
            float u = static_cast<float>(ix) / static_cast<float>(segment_x);
            float x = (u - 0.5f) * width;

            // Edge curl: corners lift up based on distance from center
            // curl_amount in [0, 1] controls curl strength
            float dx = std::abs(x) / half_w; // 0 at center, 1 at edge
            float dy = std::abs(y) / half_h;
            float corner_factor = dx * dy; // 1 at corners, 0 at center/edges
            float z = curl_amount * corner_factor * corner_factor * 0.1f * height;

            // Normal: tilt based on curl gradient
            float dzdx = curl_amount * 2.0f * dx * (x > 0 ? 1.0f : -1.0f) /
                         (half_w * half_w) * corner_factor * 0.1f * height;
            float dzdy = curl_amount * 2.0f * dy * (y > 0 ? 1.0f : -1.0f) /
                         (half_h * half_h) * corner_factor * 0.1f * height;

            // Normal = normalize(-dzdx, -dzdy, 1)
            float nx = -dzdx;
            float ny = -dzdy;
            float nz = 1.0f;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len > 0.0f) {
                nx /= len;
                ny /= len;
                nz /= len;
            } else {
                nx = 0.0f; ny = 0.0f; nz = 1.0f;
            }

            vertices.push_back(Vertex{x, y, z, nx, ny, nz, u, v});
        }
    }

    std::vector<GLuint> indices;
    indices.reserve(static_cast<size_t>(segment_x * segment_y * 6));
    for (GLsizei iy = 0; iy < segment_y; ++iy) {
        for (GLsizei ix = 0; ix < segment_x; ++ix) {
            GLuint tl = static_cast<GLuint>(iy * (segment_x + 1) + ix);
            GLuint tr = tl + 1;
            GLuint bl = static_cast<GLuint>((iy + 1) * (segment_x + 1) + ix);
            GLuint br = bl + 1;
            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);
            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }

    return uploadGeometry(vertices, indices, true);
}

Geometry uploadGeometry(const std::vector<Vertex>& vertices,
                        const std::vector<GLuint>& indices,
                        bool indexed) {
    Geometry geom;

    // Create VAO
    GLuint vao_id = 0;
    glGenVertexArrays(1, &vao_id);
    if (vao_id == 0) {
        throw std::runtime_error("Failed to generate VAO");
    }
    geom.vao.create(vao_id);

    // Create VBO
    GLuint vbo_id = 0;
    glGenBuffers(1, &vbo_id);
    if (vbo_id == 0) {
        throw std::runtime_error("Failed to generate VBO");
    }
    geom.vbo.create(vbo_id);

    glBindVertexArray(geom.vao.get());

    glBindBuffer(GL_ARRAY_BUFFER, geom.vbo.get());
    glBufferData(GL_ARRAY_BUFFER,
                 vertices.size() * sizeof(Vertex),
                 vertices.data(),
                 GL_STATIC_DRAW);

    // Position attribute (location 0)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex),
                         reinterpret_cast<void*>(offsetof(Vertex, x)));

    // Normal attribute (location 1)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex),
                         reinterpret_cast<void*>(offsetof(Vertex, nx)));

    // TexCoord attribute (location 2)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE,
                          sizeof(Vertex),
                         reinterpret_cast<void*>(offsetof(Vertex, tx)));

    if (indexed && !indices.empty()) {
        GLuint ebo_id = 0;
        glGenBuffers(1, &ebo_id);
        if (ebo_id == 0) {
            throw std::runtime_error("Failed to generate EBO");
        }
        geom.ebo.create(ebo_id);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geom.ebo.get());
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices.size() * sizeof(GLuint),
                     indices.data(),
                     GL_STATIC_DRAW);
        geom.indexed = true;
        geom.index_count = static_cast<GLsizei>(indices.size());
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    geom.vertex_count = static_cast<GLsizei>(vertices.size());
    geom.width_segments = 0;
    geom.height_segments = 0;

    return geom;
}

void bindGeometry(const Geometry& geom) {
    if (!geom.valid()) {
        throw std::runtime_error("Cannot bind invalid geometry");
    }
    glBindVertexArray(geom.vao.get());
}

void drawGeometry(const Geometry& geom) {
    if (!geom.valid()) {
        throw std::runtime_error("Cannot draw invalid geometry");
    }
    glBindVertexArray(geom.vao.get());
    if (geom.indexed && geom.index_count > 0) {
        glDrawElements(GL_TRIANGLES, geom.index_count, GL_UNSIGNED_INT, nullptr);
    } else if (geom.vertex_count > 0) {
        glDrawArrays(GL_TRIANGLES, 0, geom.vertex_count);
    }
    glBindVertexArray(0);
}
