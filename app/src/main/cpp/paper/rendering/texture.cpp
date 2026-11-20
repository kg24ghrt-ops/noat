#include "rendering/texture.h"

#ifndef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
#define GL_COMPRESSED_RGBA_ASTC_4x4_KHR 0x93B0
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_8x8_KHR
#define GL_COMPRESSED_RGBA_ASTC_8x8_KHR 0x93B7
#endif

#include <cstring>

Texture::Texture(GLsizei width, GLsizei height, Format format,
                 const void* data, bool generate_mipmaps)
    : width_(width), height_(height), format_(format) {

    GLuint id = 0;
    glGenTextures(1, &id);
    if (id == 0) {
        throw std::runtime_error("Failed to generate OpenGL texture");
    }
    texture_.create(id);

    bind(0);

    GLenum gl_format = toGLFormat(format);
    GLenum gl_internal = toGLInternalFormat(format);
    GLenum gl_type = toGLType(format);
    bool compressed = isCompressed(format);

    if (compressed) {
        // For compressed formats, compute data size from block size
        GLsizei block_w = 4; // ASTC/ETC2 blocks are 4x4
        GLsizei block_h = 4;
        GLsizei blocks_w = (width + block_w - 1) / block_w;
        GLsizei blocks_h = (height + block_h - 1) / block_h;
        GLsizei block_bytes = blockSize(format);
        GLsizei data_size = blocks_w * blocks_h * block_bytes;

        glCompressedTexImage2D(GL_TEXTURE_2D, 0, gl_internal, width, height,
                               0, data_size, data);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, gl_internal, width, height,
                     0, gl_format, gl_type, data);
    }

    if (generate_mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
}

void Texture::upload(GLsizei width, GLsizei height, Format format, const void* data) {
    bind(0);
    GLenum gl_format = toGLFormat(format);
    GLenum gl_internal = toGLInternalFormat(format);
    GLenum gl_type = toGLType(format);
    bool compressed = isCompressed(format);

    if (compressed) {
        GLsizei blocks_w = (width + 3) / 4;
        GLsizei blocks_h = (height + 3) / 4;
        GLsizei block_bytes = blockSize(format);
        GLsizei data_size = blocks_w * blocks_h * block_bytes;
        glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                                  gl_internal, data_size, data);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                        gl_format, gl_type, data);
    }

    width_ = width;
    height_ = height;
    format_ = format;
}

void Texture::generateMipmap() const {
    bind(0);
    glGenerateMipmap(GL_TEXTURE_2D);
}

void Texture::bind(GLuint unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture_.get());
}

void Texture::setWrapMode(WrapMode s, WrapMode t) {
    bind(0);
    GLenum gs = (s == WrapMode::Repeat) ? GL_REPEAT :
                (s == WrapMode::MirroredRepeat) ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE;
    GLenum gt = (t == WrapMode::Repeat) ? GL_REPEAT :
                (t == WrapMode::MirroredRepeat) ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gs);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gt);
}

void Texture::setFilter(Filter min_filter, Filter mag_filter) {
    bind(0);
    GLenum gmin = GL_LINEAR;
    switch (min_filter) {
        case Filter::Nearest: gmin = GL_NEAREST; break;
        case Filter::Linear: gmin = GL_LINEAR; break;
        case Filter::NearestMipmapNearest: gmin = GL_NEAREST_MIPMAP_NEAREST; break;
        case Filter::LinearMipmapNearest: gmin = GL_LINEAR_MIPMAP_NEAREST; break;
        case Filter::NearestMipmapLinear: gmin = GL_NEAREST_MIPMAP_LINEAR; break;
        case Filter::LinearMipmapLinear: gmin = GL_LINEAR_MIPMAP_LINEAR; break;
    }
    GLenum gmag = (mag_filter == Filter::Nearest) ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gmin);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gmag);
}

GLenum Texture::toGLFormat(Format f) {
    switch (f) {
        case Format::RGBA8: return GL_RGBA;
        case Format::RGB8: return GL_RGB;
        case Format::RGBA4: return GL_RGBA;
        case Format::RGB565: return GL_RGB;
        case Format::DepthComponent16: return GL_DEPTH_COMPONENT;
        default: return GL_RGBA;
    }
}

GLenum Texture::toGLInternalFormat(Format f) {
    switch (f) {
        case Format::RGBA8: return GL_RGBA8;
        case Format::RGB8: return GL_RGB8;
        case Format::RGBA4: return GL_RGBA4;
        case Format::RGB565: return GL_RGB565;
        case Format::DepthComponent16: return GL_DEPTH_COMPONENT16;
        case Format::ASTC_RGBA_4x4: return GL_COMPRESSED_RGBA_ASTC_4x4_KHR;
        case Format::ASTC_RGBA_8x8: return GL_COMPRESSED_RGBA_ASTC_8x8_KHR;
        case Format::ETC2_RGBA8: return GL_COMPRESSED_RGBA8_ETC2_EAC;
        case Format::ETC2_RGB8: return GL_COMPRESSED_RGB8_ETC2;
        default: return GL_RGBA8;
    }
}

GLenum Texture::toGLType(Format f) {
    switch (f) {
        case Format::RGBA8:
        case Format::RGB8:
        case Format::RGBA4:
        case Format::RGB565:
        case Format::DepthComponent16:
            return GL_UNSIGNED_BYTE;
        case Format::ASTC_RGBA_4x4:
        case Format::ASTC_RGBA_8x8:
        case Format::ETC2_RGBA8:
        case Format::ETC2_RGB8:
            return GL_UNSIGNED_BYTE;
        default:
            return GL_UNSIGNED_BYTE;
    }
}

GLboolean Texture::isCompressed(Format f) {
    switch (f) {
        case Format::ASTC_RGBA_4x4:
        case Format::ASTC_RGBA_8x8:
        case Format::ETC2_RGBA8:
        case Format::ETC2_RGB8:
            return GL_TRUE;
        default:
            return GL_FALSE;
    }
}

GLsizei Texture::blockSize(Format f) {
    // ETC2: 8 bytes per 4x4 block
    // ASTC 4x4: 8 bytes per 4x4 block
    // ASTC 8x8: 16 bytes per 8x8 block (2 bytes per pixel)
    switch (f) {
        case Format::ETC2_RGBA8:
        case Format::ETC2_RGB8:
        case Format::ASTC_RGBA_4x4:
            return 8;
        case Format::ASTC_RGBA_8x8:
            return 16;
        default:
            return 0;
    }
}
