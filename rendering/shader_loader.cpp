#include "rendering/shader_loader.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace blackhole::rendering {
namespace {

std::string read_text_file(const char* path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error(std::string("Failed to open shader: ") + path);
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

GLuint compile_shader(GLenum type, const char* source, const char* label) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success == GL_FALSE) {
        GLint log_length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
        std::vector<char> log(static_cast<std::size_t>(std::max(log_length, 1)));
        glGetShaderInfoLog(shader, log_length, nullptr, log.data());
        std::cerr << "Shader compile error (" << label << "):\n" << log.data() << '\n';
        glDeleteShader(shader);
        throw std::runtime_error("shader compilation failed");
    }
    return shader;
}

GLuint link_program(GLuint first_shader, GLuint second_shader, const char* label) {
    const GLuint program = glCreateProgram();
    glAttachShader(program, first_shader);
    if (second_shader != 0)
        glAttachShader(program, second_shader);
    glLinkProgram(program);

    GLint success = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (success == GL_FALSE) {
        GLint log_length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
        std::vector<char> log(static_cast<std::size_t>(std::max(log_length, 1)));
        glGetProgramInfoLog(program, log_length, nullptr, log.data());
        std::cerr << "Shader link error (" << label << "):\n" << log.data() << '\n';
        glDeleteProgram(program);
        throw std::runtime_error("shader link failed");
    }
    return program;
}

} // namespace

GLuint create_program_from_sources(const char* vertex_source, const char* fragment_source,
                                   const char* label) {
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, vertex_source, label);
    const GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, fragment_source, label);
    try {
        const GLuint program = link_program(vertex, fragment, label);
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        return program;
    } catch (...) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        throw;
    }
}

GLuint create_program_from_files(const char* vertex_path, const char* fragment_path) {
    const std::string vertex_source = read_text_file(vertex_path);
    const std::string fragment_source = read_text_file(fragment_path);
    return create_program_from_sources(vertex_source.c_str(), fragment_source.c_str(),
                                       "graphics program");
}

GLuint create_compute_program_from_file(const char* path) {
    const std::string source = read_text_file(path);
    const GLuint compute = compile_shader(GL_COMPUTE_SHADER, source.c_str(), path);
    try {
        const GLuint program = link_program(compute, 0, path);
        glDeleteShader(compute);
        return program;
    } catch (...) {
        glDeleteShader(compute);
        throw;
    }
}

} // namespace blackhole::rendering
