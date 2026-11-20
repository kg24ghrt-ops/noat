#include "rendering/program.h"

#include <sstream>

Program::Program(std::vector<Shader> shaders) {
    program_.create(linkProgram(shaders));
}

GLuint Program::linkProgram(std::vector<Shader>& shaders) {
    GLuint id = glCreateProgram();
    if (id == 0) {
        throw std::runtime_error("Failed to create GL program object");
    }

    for (auto& s : shaders) {
        if (!s.valid()) {
            glDeleteProgram(id);
            throw std::runtime_error("Cannot link program with invalid shader");
        }
        glAttachShader(id, s.id());
    }

    glLinkProgram(id);

    // Detach shaders after linking (they are no longer needed)
    for (auto& s : shaders) {
        glDetachShader(id, s.id());
    }

    GLint linked = 0;
    glGetProgramiv(id, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        GLint log_len = 0;
        glGetProgramiv(id, GL_INFO_LOG_LENGTH, &log_len);
        std::string log(log_len > 0 ? log_len : 1, '\0');
        glGetProgramInfoLog(id, log_len, nullptr, &log[0]);
        glDeleteProgram(id);
        throw std::runtime_error("Program linking failed: " + log);
    }

    return id;
}

void Program::use() const {
    if (!program_.valid()) {
        throw std::runtime_error("Cannot use invalid program");
    }
    glUseProgram(program_.get());
}

GLint Program::uniformLocation(const std::string& name) {
    auto it = uniform_cache_.find(name);
    if (it != uniform_cache_.end()) {
        return it->second;
    }
    GLint loc = glGetUniformLocation(program_.get(), name.c_str());
    uniform_cache_[name] = loc;
    return loc;
}

GLint Program::attribLocation(const std::string& name) {
    auto it = attrib_cache_.find(name);
    if (it != attrib_cache_.end()) {
        return it->second;
    }
    GLint loc = glGetAttribLocation(program_.get(), name.c_str());
    attrib_cache_[name] = loc;
    return loc;
}

void Program::setUniform(const std::string& name, float v) {
    glUniform1f(uniformLocation(name), v);
}

void Program::setUniform(const std::string& name, int v) {
    glUniform1i(uniformLocation(name), v);
}

void Program::setUniform(const std::string& name, const float* v, int count) {
    glUniform1fv(uniformLocation(name), count, v);
}

void Program::setUniformMatrix4fv(const std::string& name, const float* matrix, bool transpose) {
    glUniformMatrix4fv(uniformLocation(name), 1, transpose ? GL_TRUE : GL_FALSE, matrix);
}

std::string Program::infoLog() const {
    if (!program_.valid()) return "";
    GLint log_len = 0;
    glGetProgramiv(program_.get(), GL_INFO_LOG_LENGTH, &log_len);
    std::string log(log_len > 0 ? log_len : 1, '\0');
    glGetProgramInfoLog(program_.get(), log_len, nullptr, &log[0]);
    return log;
}
