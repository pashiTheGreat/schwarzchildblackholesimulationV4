cmake_minimum_required(VERSION 3.21)
project(BlackHoleSim)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
enable_testing()

option(BUILD_RENDERERS "Build the OpenGL visualizer targets" ON)

function(enable_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(BUILD_RENDERERS)
    # Dependencies used only by the windowed visualizer targets.
    find_package(GLEW REQUIRED)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(glm CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)

    set(DEPS glfw GLEW::GLEW glm::glm imgui::imgui OpenGL::GL)
endif()

# The dependency-free CPU reference is the sole owner of reusable
# Schwarzschild mathematics.  It can be tested without creating a GLFW window.
add_library(SchwarzschildPhysics STATIC
    physics/accretion_disk.cpp
    physics/schwarzschild.cpp
    physics/simulation_config.cpp
)
target_include_directories(SchwarzschildPhysics PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(SchwarzschildPhysics PUBLIC cxx_std_17)

enable_project_warnings(SchwarzschildPhysics)

if(BUILD_RENDERERS)
    # 2D Lensing executable (historical visualization; see ARCHIVAL.md).
    add_executable(BlackHole2D 2D_lensing.cpp)
    target_link_libraries(BlackHole2D PRIVATE ${DEPS})
    target_include_directories(BlackHole2D PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    enable_project_warnings(BlackHole2D)

    # 3D Black Hole executable.
    add_executable(BlackHole3D
        app/camera.cpp
        black_hole.cpp
        rendering/shader_loader.cpp
    )
    target_link_libraries(BlackHole3D PRIVATE SchwarzschildPhysics ${DEPS})
    target_include_directories(BlackHole3D PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    enable_project_warnings(BlackHole3D)
endif()

add_executable(SchwarzschildValidation tests/schwarzschild_validation.cpp)
target_link_libraries(SchwarzschildValidation PRIVATE SchwarzschildPhysics)
target_include_directories(SchwarzschildValidation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
enable_project_warnings(SchwarzschildValidation)
add_test(
    NAME SchwarzschildValidation
    COMMAND SchwarzschildValidation --report "${CMAKE_BINARY_DIR}/baseline_validation_report.txt"
)

add_executable(GpuValidationCompare tests/gpu_validation_compare.cpp)
target_link_libraries(GpuValidationCompare PRIVATE SchwarzschildPhysics)
target_include_directories(GpuValidationCompare PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
enable_project_warnings(GpuValidationCompare)

# Shader files (copy to output dir)
file(GLOB SHADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.frag"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.comp"
)

if(BUILD_RENDERERS)
    foreach(shader ${SHADERS})
        add_custom_command(TARGET BlackHole3D POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${shader}
            $<TARGET_FILE_DIR:BlackHole3D>
        )
    endforeach()
endif()

