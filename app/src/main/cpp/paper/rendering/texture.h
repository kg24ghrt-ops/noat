#pragma once

#include "core/gl_object.hpp"

#include <cstdint>
#include <string>

// Texture wrapper for OpenGL ES. Supports compressed formats (ASTC, ETC2)
// preferred on Mali GPUs to reduce bandwidth.
class Texture {
public:
    enum class Format {
        RGBA8,
        RGB8,
        RGBA4,
        RGB565,
        DepthComponent16,
        // Compressed formats (Mali-friendly)
        ASTC_RGBA_4x4,
        ASTC_RGBA_8x8,
        ETC2_RGBA8,
        ETC2_RGB8,
    };

    enum class WrapMode {
        Repeat,
        ClampToEdge,
        MirroredRepeat,
    };

    enum class Filter {
        Nearest,
        Linear,
        NearestMipmapNearest,
        LinearMipmapNearest,
        NearestMipmapLinear,
        LinearMipmapLinear,
    };

    Texture() = default;

    Texture(GLsizei width, GLsizei height, Format format,
            const void* data = nullptr, bool generate_mipmaps = false);

    ~Texture() = default;

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&&) = default;
    Texture& operator=(Texture&&) = default;

    GLuint id() const { return texture_.get(); }
    bool valid() const { return texture_.valid(); }
    GLsizei width() const { return width_; }
    GLsizei height() const { return height_; }
    Format format() const { return format_; }

    void bind(GLuint unit = 0) const;

    // Upload pixel data (for uncompressed formats)
    void upload(GLsizei width, GLsizei height, Format format, const void* data);

    // Generate mipmaps
    void generateMipmap() const;

    // Set sampling parameters
    void setWrapMode(WrapMode s, WrapMode t);
    void setFilter(Filter min_filter, Filter mag_filter);

private:
    static GLenum toGLFormat(Format f);
    static GLenum toGLInternalFormat(Format f);
    static GLenum toGLType(Format f);
    static GLboolean isCompressed(Format f);
    static GLsizei blockSize(Format f); // compressed block size in bytes (for ASTC/ETC2)

    GLTexture texture_;
    GLsizei width_ = 0;
    GLsizei height_ = 0;
    Format format_ = Format::RGBA8;
};
