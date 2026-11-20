#pragma once

#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <type_traits>

// RAII wrapper for OpenGL objects that are deleted with glDelete*.
// T = GL object type, Deleter = function pointer to the glDelete* function.
template <typename T, void (*Deleter)(GLsizei, const T*)>
class GLObject {
public:
    GLObject() : id_(0) {}

    explicit GLObject(T id) : id_(id) {}

    ~GLObject() {
        reset();
    }

    GLObject(const GLObject&) = delete;
    GLObject& operator=(const GLObject&) = delete;

    GLObject(GLObject&& other) noexcept : id_(other.id_) {
        other.id_ = 0;
    }

    GLObject& operator=(GLObject&& other) noexcept {
        if (this != &other) {
            reset();
            id_ = other.id_;
            other.id_ = 0;
        }
        return *this;
    }

    T get() const { return id_; }

    operator T() const { return id_; }

    bool valid() const { return id_ != 0; }

    void reset() {
        if (id_ != 0) {
            Deleter(1, &id_);
            id_ = 0;
        }
    }

    void create(T id) {
        reset();
        id_ = id;
    }

private:
    T id_;
};

// Specialized deleters
inline void deleteBuffers(GLsizei n, const GLuint* ids) { glDeleteBuffers(n, ids); }
inline void deleteTextures(GLsizei n, const GLuint* ids) { glDeleteTextures(n, ids); }
inline void deleteFramebuffers(GLsizei n, const GLuint* ids) { glDeleteFramebuffers(n, ids); }
inline void deleteRenderbuffers(GLsizei n, const GLuint* ids) { glDeleteRenderbuffers(n, ids); }
inline void deleteVertexArrays(GLsizei n, const GLuint* ids) { glDeleteVertexArrays(n, ids); }
inline void deleteQueries(GLsizei n, const GLuint* ids) { glDeleteQueries(n, ids); }
inline void deleteSamplers(GLsizei n, const GLuint* ids) { glDeleteSamplers(n, ids); }
inline void deleteTransformFeedbacks(GLsizei n, const GLuint* ids) { glDeleteTransformFeedbacks(n, ids); }

// Typed RAII wrappers
using GLBuffer = GLObject<GLuint, deleteBuffers>;
using GLTexture = GLObject<GLuint, deleteTextures>;
using GLFramebuffer = GLObject<GLuint, deleteFramebuffers>;
using GLRenderbuffer = GLObject<GLuint, deleteRenderbuffers>;
using GLVertexArray = GLObject<GLuint, deleteVertexArrays>;
using GLQuery = GLObject<GLuint, deleteQueries>;
using GLSampler = GLObject<GLuint, deleteSamplers>;
using GLTransformFeedback = GLObject<GLuint, deleteTransformFeedbacks>;

// Shader and program are deleted with glDeleteShader / glDeleteProgram (single-arg)
class GLShader {
public:
    GLShader() : id_(0) {}

    explicit GLShader(GLuint id) : id_(id) {}

    ~GLShader() {
        if (id_ != 0) {
            glDeleteShader(id_);
        }
    }

    GLShader(const GLShader&) = delete;
    GLShader& operator=(const GLShader&) = delete;

    GLShader(GLShader&& other) noexcept : id_(other.id_) {
        other.id_ = 0;
    }

    GLShader& operator=(GLShader&& other) noexcept {
        if (this != &other) {
            if (id_ != 0) {
                glDeleteShader(id_);
            }
            id_ = other.id_;
            other.id_ = 0;
        }
        return *this;
    }

    GLuint get() const { return id_; }

    operator GLuint() const { return id_; }

    bool valid() const { return id_ != 0; }

    void reset() {
        if (id_ != 0) {
            glDeleteShader(id_);
            id_ = 0;
        }
    }

    void create(GLuint id) {
        reset();
        id_ = id;
    }

private:
    GLuint id_;
};

class GLProgram {
public:
    GLProgram() : id_(0) {}

    explicit GLProgram(GLuint id) : id_(id) {}

    ~GLProgram() {
        if (id_ != 0) {
            glDeleteProgram(id_);
        }
    }

    GLProgram(const GLProgram&) = delete;
    GLProgram& operator=(const GLProgram&) = delete;

    GLProgram(GLProgram&& other) noexcept : id_(other.id_) {
        other.id_ = 0;
    }

    GLProgram& operator=(GLProgram&& other) noexcept {
        if (this != &other) {
            if (id_ != 0) {
                glDeleteProgram(id_);
            }
            id_ = other.id_;
            other.id_ = 0;
        }
        return *this;
    }

    GLuint get() const { return id_; }

    operator GLuint() const { return id_; }

    bool valid() const { return id_ != 0; }

    void reset() {
        if (id_ != 0) {
            glDeleteProgram(id_);
            id_ = 0;
        }
    }

    void create(GLuint id) {
        reset();
        id_ = id;
    }

private:
    GLuint id_;
};

// EGL context RAII wrapper
class PaperEGLContext {
public:
    PaperEGLContext() : display_(EGL_NO_DISPLAY), context_(EGL_NO_CONTEXT), surface_(EGL_NO_SURFACE) {}

    PaperEGLContext(EGLDisplay display, EGLContext context, EGLSurface surface)
        : display_(display), context_(context), surface_(surface) {}

    ~PaperEGLContext() {
        destroy();
    }

    PaperEGLContext(const PaperEGLContext&) = delete;
    PaperEGLContext& operator=(const PaperEGLContext&) = delete;

    PaperEGLContext(PaperEGLContext&& other) noexcept
        : display_(other.display_), context_(other.context_), surface_(other.surface_) {
        other.display_ = EGL_NO_DISPLAY;
        other.context_ = EGL_NO_CONTEXT;
        other.surface_ = EGL_NO_SURFACE;
    }

    PaperEGLContext& operator=(PaperEGLContext&& other) noexcept {
        if (this != &other) {
            destroy();
            display_ = other.display_;
            context_ = other.context_;
            surface_ = other.surface_;
            other.display_ = EGL_NO_DISPLAY;
            other.context_ = EGL_NO_CONTEXT;
            other.surface_ = EGL_NO_SURFACE;
        }
        return *this;
    }

    EGLDisplay display() const { return display_; }
    EGLContext context() const { return context_; }
    EGLSurface surface() const { return surface_; }

    bool valid() const { return context_ != EGL_NO_CONTEXT; }

    void destroy() {
        if (context_ != EGL_NO_CONTEXT && display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (surface_ != EGL_NO_SURFACE) {
                eglDestroySurface(display_, surface_);
            }
            if (context_ != EGL_NO_CONTEXT) {
                eglDestroyContext(display_, context_);
            }
            if (display_ != EGL_NO_DISPLAY) {
                eglTerminate(display_);
            }
        }
        display_ = EGL_NO_DISPLAY;
        context_ = EGL_NO_CONTEXT;
        surface_ = EGL_NO_SURFACE;
    }

    bool makeCurrent() const {
        if (display_ == EGL_NO_DISPLAY || context_ == EGL_NO_CONTEXT) {
            return false;
        }
        return eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE;
    }

private:
    EGLDisplay display_;
    EGLContext context_;
    EGLSurface surface_;
};

// OpenGL error checking
inline std::string glErrorString(GLenum error) {
    switch (error) {
        case GL_NO_ERROR: return "GL_NO_ERROR";
        case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
        case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
        case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
        case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
        case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
        default: return "Unknown GL error 0x" + std::to_string(error);
    }
}

#define PAPER_GL_CHECK() do { \
    GLenum __err = glGetError(); \
    if (__err != GL_NO_ERROR) { \
        throw std::runtime_error("OpenGL error at " __FILE__ ":" + \
            std::to_string(__LINE__) + ": " + glErrorString(__err)); \
    } \
} while (0)
