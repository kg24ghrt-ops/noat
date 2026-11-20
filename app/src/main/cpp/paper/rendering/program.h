#pragma once

#include "core/gl_object.hpp"
#include "rendering/shader.h"

#include <string>
#include <unordered_map>
#include <vector>

// OpenGL ES program object: links shaders and manages uniform/attribute locations.
class Program {
public:
    Program() = default;

    // Link a set of shaders into a program.
    explicit Program(std::vector<Shader> shaders);

    ~Program() = default;

    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;

    Program(Program&&) = default;
    Program& operator=(Program&&) = default;

    GLuint id() const { return program_.get(); }
    bool valid() const { return program_.valid(); }

    void use() const;

    // Uniform location lookup (cached)
    GLint uniformLocation(const std::string& name);
    GLint attribLocation(const std::string& name);

    // Uniform setters (convenience)
    void setUniform(const std::string& name, float v);
    void setUniform(const std::string& name, int v);
    void setUniform(const std::string& name, const float* v, int count);
    void setUniformMatrix4fv(const std::string& name, const float* matrix, bool transpose = false);

    // Get link info log
    std::string infoLog() const;

private:
    static GLuint linkProgram(std::vector<Shader>& shaders);

    GLProgram program_;
    std::unordered_map<std::string, GLint> uniform_cache_;
    std::unordered_map<std::string, GLint> attrib_cache_;
};
