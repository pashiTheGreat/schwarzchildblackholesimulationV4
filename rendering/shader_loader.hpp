#pragma once

#include <GL/glew.h>

namespace blackhole::rendering {

// Creates linked OpenGL programs and throws std::runtime_error after printing
// the driver log when any source fails to compile or link.
GLuint create_program_from_sources(const char* vertex_source, const char* fragment_source,
                                   const char* label);
GLuint create_program_from_files(const char* vertex_path, const char* fragment_path);
GLuint create_compute_program_from_file(const char* path);

} // namespace blackhole::rendering
