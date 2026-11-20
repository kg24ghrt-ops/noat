#pragma once

#include "core/gl_object.hpp"

#ifndef GL_TESS_CONTROL_SHADER
#define GL_TESS_CONTROL_SHADER 0x8E88
#endif
#ifndef GL_TESS_EVALUATION_SHADER
#define GL_TESS_EVALUATION_SHADER 0x8E87
#endif
#ifndef GL_GEOMETRY_SHADER
#define GL_GEOMETRY_SHADER 0x8DD9
#endif
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif

#include <string>
#include <unordered_map>

// OpenGL ES shader wrapper. Compiles a shader from source and manages its lifetime.
class Shader {
public:
    enum class Type {
        Vertex,
        Fragment,
        TessellationControl,
        TessellationEvaluation,
        Geometry,
        Compute,
    };

    Shader() = default;

    Shader(Type type, const std::string& source);

    ~Shader() = default;

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    Shader(Shader&&) = default;
    Shader& operator=(Shader&&) = default;

    Type type() const { return type_; }
    GLuint id() const { return shader_.get(); }
    bool valid() const { return shader_.valid(); }
    const std::string& source() const { return source_; }

    // Recompile with new source
    void recompile(const std::string& source);

    // Get compile info log
    std::string infoLog() const;

private:
    static GLuint toGLType(Type type);

    Type type_ = Type::Vertex;
    std::string source_;
    GLShader shader_;
};

// Loads a shader source from a string (asset loading handled by caller/JNI)
inline Shader::Shader(Type type, const std::string& source)
    : type_(type), source_(source) {
    GLuint id = glCreateShader(toGLType(type));
    if (id == 0) {
        throw std::runtime_error("Failed to create GL shader object");
    }

    const char* src = source.c_str();
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);

    GLint compiled = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        GLint log_len = 0;
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &log_len);
        std::string log(log_len > 0 ? log_len : 1, '\0');
        glGetShaderInfoLog(id, log_len, nullptr, &log[0]);
        glDeleteShader(id);
        throw std::runtime_error("Shader compilation failed: " + log);
    }

    shader_.create(id);
}

inline void Shader::recompile(const std::string& source) {
    source_ = source;
    GLuint id = glCreateShader(toGLType(type_));
    if (id == 0) {
        throw std::runtime_error("Failed to create GL shader object");
    }

    const char* src = source.c_str();
    glShaderSource(id, 1, &src, nullptr);
    glCompileShader(id);

    GLint compiled = 0;
    glGetShaderiv(id, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        GLint log_len = 0;
        glGetShaderiv(id, GL_INFO_LOG_LENGTH, &log_len);
        std::string log(log_len > 0 ? log_len : 1, '\0');
        glGetShaderInfoLog(id, log_len, nullptr, &log[0]);
        glDeleteShader(id);
        throw std::runtime_error("Shader recompilation failed: " + log);
    }

    shader_.create(id);
}

inline std::string Shader::infoLog() const {
    if (!shader_.valid()) return "";
    GLint log_len = 0;
    glGetShaderiv(shader_.get(), GL_INFO_LOG_LENGTH, &log_len);
    std::string log(log_len > 0 ? log_len : 1, '\0');
    glGetShaderInfoLog(shader_.get(), log_len, nullptr, &log[0]);
    return log;
}

inline GLuint Shader::toGLType(Type type) {
    switch (type) {
        case Type::Vertex: return GL_VERTEX_SHADER;
        case Type::Fragment: return GL_FRAGMENT_SHADER;
        case Type::TessellationControl: return GL_TESS_CONTROL_SHADER;
        case Type::TessellationEvaluation: return GL_TESS_EVALUATION_SHADER;
        case Type::Geometry: return GL_GEOMETRY_SHADER;
        case Type::Compute: return GL_COMPUTE_SHADER;
    }
    return GL_VERTEX_SHADER;
}
