#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <iostream>
#include <vector>
#define _USE_MATH_DEFINES
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <numeric>
#include <string>

#include "app/camera.hpp"
#include "physics/schwarzschild.hpp"
#include "physics/simulation_config.hpp"
#include "rendering/shader_loader.hpp"

// Ask hybrid-graphics laptop drivers to create the OpenGL context on the
// high-performance adapter instead of the integrated GPU.
#if defined(_WIN32)
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 0x00000001;
}
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
using namespace glm;
using namespace std;
struct RunOptions {
    bool gpu_validation = false;
    bool interaction_smoke = false;
    bool performance_smoke = false;
    bool legacy_allocation_benchmark = false;
    string capture_output_path;
    int capture_frames_remaining = 0;
};

using Camera = blackhole::app::Camera;

struct Engine {
    blackhole::physics::SimulationConfiguration& simulationConfig;
    float& diskInclinationRadians;
    float& diskThicknessInRadii;
    const RunOptions& runOptions;
    GLuint gridShaderProgram = 0;
    // -- Quad & Texture render -- //
    GLFWwindow* window = nullptr;
    GLuint quadVAO = 0;
    GLuint quadVBO = 0;
    GLuint texture = 0;
    GLuint shaderProgram = 0;
    GLuint computeProgram = 0;
    // -- UBOs -- //
    GLuint cameraUBO = 0;
    GLuint diskUBO = 0;
    GLuint integrationUBO = 0;
    GLuint diagnosticsSSBO = 0;
    GLuint statusPixelsSSBO = 0;
    GLuint invariantPixelsSSBO = 0;
    array<GLuint, 6> rayStatusCounts{};
    int lastComputeWidth = 0;
    int lastComputeHeight = 0;
    int textureWidth = 0;
    int textureHeight = 0;
    GLsizeiptr validationPixelCapacity = 0;
    bool uiInitialized = false;
    bool shutdownComplete = false;
    unsigned int glDebugErrorCount = 0;
    // -- grid mess vars -- //
    GLuint gridVAO = 0;
    GLuint gridVBO = 0;
    GLuint gridEBO = 0;
    int gridIndexCount = 0;

    int WIDTH = 800;          // Window width
    int HEIGHT = 600;         // Window height
    int COMPUTE_WIDTH = 320;  // Stationary render resolution width
    int COMPUTE_HEIGHT = 240; // Stationary render resolution height

    Engine(blackhole::physics::SimulationConfiguration& configuration,
           float& disk_inclination_radians, float& disk_thickness_in_radii,
           const RunOptions& options)
        : simulationConfig(configuration), diskInclinationRadians(disk_inclination_radians),
          diskThicknessInRadii(disk_thickness_in_radii), runOptions(options) {
        if (!glfwInit()) {
            cerr << "GLFW init failed\n";
            exit(EXIT_FAILURE);
        }
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        window = glfwCreateWindow(WIDTH, HEIGHT, "Black Hole", nullptr, nullptr);
        if (!window) {
            cerr << "Failed to create GLFW window\n";
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        glfwMakeContextCurrent(window);
        glewExperimental = GL_TRUE;
        GLenum glewErr = glewInit();
        if (glewErr != GLEW_OK) {
            cerr << "Failed to initialize GLEW: " << (const char*)glewGetErrorString(glewErr)
                 << "\n";
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        if (GLEW_VERSION_4_3 || GLEW_KHR_debug) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(debugMessageCallback, this);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0,
                                  nullptr, GL_FALSE);
        }
        cout << "OpenGL vendor: " << glGetString(GL_VENDOR) << "\n"
             << "OpenGL renderer: " << glGetString(GL_RENDERER) << "\n"
             << "OpenGL version: " << glGetString(GL_VERSION) << "\n"
             << flush;
        this->shaderProgram = CreateShaderProgram();
        gridShaderProgram = CreateShaderProgram("grid.vert", "grid.frag");

        computeProgram = CreateComputeProgram("geodesic.comp");
        glGenBuffers(1, &cameraUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
        glBufferData(GL_UNIFORM_BUFFER, 128, nullptr, GL_DYNAMIC_DRAW); // alloc ~128 bytes
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, cameraUBO); // binding = 1 matches shader

        glGenBuffers(1, &diskUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, diskUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 12, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 2, diskUBO); // binding = 2 matches compute shader

        glGenBuffers(1, &integrationUBO);
        glBindBuffer(GL_UNIFORM_BUFFER, integrationUBO);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 6 + sizeof(int) * 6, nullptr,
                     GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 4, integrationUBO);

        glGenBuffers(1, &diagnosticsSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, diagnosticsSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(rayStatusCounts), nullptr, GL_DYNAMIC_READ);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, diagnosticsSSBO);

        glGenBuffers(1, &statusPixelsSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, statusPixelsSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(GLuint), nullptr, GL_DYNAMIC_READ);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, statusPixelsSSBO);
        glGenBuffers(1, &invariantPixelsSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, invariantPixelsSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * 8, nullptr, GL_DYNAMIC_READ);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, invariantPixelsSSBO);
        validationPixelCapacity = 1;

        createQuadResources();
    }
    ~Engine() { shutdown(); }
    static void APIENTRY debugMessageCallback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei,
                                              const GLchar* message, const void* userData) {
        auto* owner = const_cast<Engine*>(static_cast<const Engine*>(userData));
        if (type == GL_DEBUG_TYPE_ERROR || severity == GL_DEBUG_SEVERITY_HIGH) {
            ++owner->glDebugErrorCount;
            cerr << "[OPENGL] " << message << '\n';
        }
    }
    void generateGrid() {
        // Flamm's paraboloid z(r) = 2 sqrt(r_s (r-r_s)) embeds one
        // constant-time equatorial spatial slice. It is not a literal image
        // of four-dimensional spacetime curvature.
        constexpr int radialSegments = 28;
        constexpr int angularSegments = 64;
        const float schwarzschildRadius =
            static_cast<float>(simulationConfig.black_hole.schwarzschild_radius_m);
        const float innerRadius = 1.001f * schwarzschildRadius;
        const float outerRadius = 14.0f * schwarzschildRadius;
        vector<vec3> vertices;
        vector<GLuint> indices;

        for (int radial = 0; radial <= radialSegments; ++radial) {
            const float fraction = static_cast<float>(radial) / radialSegments;
            const float radius = innerRadius + fraction * (outerRadius - innerRadius);
            const float embeddingHeight =
                2.0f * std::sqrt(schwarzschildRadius * (radius - schwarzschildRadius));
            for (int angular = 0; angular <= angularSegments; ++angular) {
                const float angle =
                    2.0f * static_cast<float>(M_PI) * static_cast<float>(angular) / angularSegments;
                vertices.emplace_back(radius * std::cos(angle), embeddingHeight,
                                      radius * std::sin(angle));
            }
        }

        for (int radial = 0; radial <= radialSegments; ++radial) {
            for (int angular = 0; angular < angularSegments; ++angular) {
                const GLuint index = static_cast<GLuint>(radial * (angularSegments + 1) + angular);
                indices.push_back(index);
                indices.push_back(index + 1);
                if (radial < radialSegments) {
                    indices.push_back(index);
                    indices.push_back(index + angularSegments + 1);
                }
            }
        }

        if (gridVAO == 0)
            glGenVertexArrays(1, &gridVAO);
        if (gridVBO == 0)
            glGenBuffers(1, &gridVBO);
        if (gridEBO == 0)
            glGenBuffers(1, &gridEBO);

        glBindVertexArray(gridVAO);

        glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vec3), vertices.data(),
                     GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gridEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(),
                     GL_STATIC_DRAW);

        glEnableVertexAttribArray(0); // location = 0
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(vec3), (void*)0);

        gridIndexCount = static_cast<int>(indices.size());

        glBindVertexArray(0);
    }
    void drawGrid(const mat4& viewProj) {
        glUseProgram(gridShaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(gridShaderProgram, "viewProj"), 1, GL_FALSE,
                           glm::value_ptr(viewProj));
        glBindVertexArray(gridVAO);

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glDrawElements(GL_LINES, gridIndexCount, GL_UNSIGNED_INT, 0);

        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }
    void drawFullScreenQuad() {
        glUseProgram(shaderProgram); // fragment + vertex shader
        glBindVertexArray(quadVAO);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(glGetUniformLocation(shaderProgram, "screenTexture"), 0);

        glDisable(GL_DEPTH_TEST); // draw as background
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glEnable(GL_DEPTH_TEST);
    }
    GLuint CreateShaderProgram() {
        const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec2 aPos;  // Changed to vec2
        layout (location = 1) in vec2 aTexCoord;
        out vec2 TexCoord;
        void main() {
            gl_Position = vec4(aPos, 0.0, 1.0);  // Explicit z=0
            TexCoord = aTexCoord;
        })";

        const char* fragmentShaderSource = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D screenTexture;
        void main() {
            FragColor = texture(screenTexture, TexCoord);
        })";

        return blackhole::rendering::create_program_from_sources(
            vertexShaderSource, fragmentShaderSource, "fullscreen quad");
    };
    GLuint CreateShaderProgram(const char* vertPath, const char* fragPath) {
        return blackhole::rendering::create_program_from_files(vertPath, fragPath);
    }
    GLuint CreateComputeProgram(const char* path) {
        return blackhole::rendering::create_compute_program_from_file(path);
    }
    void dispatchCompute(const Camera& cam) {
        // determine target compute‐res
        const bool useReducedMotionResolution = simulationConfig.resolution.dynamic_resolution &&
                                                cam.moving &&
                                                !simulationConfig.resolution.full_resolution;
        int cw = runOptions.gpu_validation
                     ? 64
                     : (simulationConfig.resolution.full_resolution
                            ? WIDTH
                            : (useReducedMotionResolution ? std::max(80, COMPUTE_WIDTH / 2)
                                                          : COMPUTE_WIDTH));
        int ch = runOptions.gpu_validation
                     ? 48
                     : (simulationConfig.resolution.full_resolution
                            ? HEIGHT
                            : (useReducedMotionResolution ? std::max(60, COMPUTE_HEIGHT / 2)
                                                          : COMPUTE_HEIGHT));
        lastComputeWidth = cw;
        lastComputeHeight = ch;

        // 1) reallocate the texture if needed
        glBindTexture(GL_TEXTURE_2D, texture);
        if (cw != textureWidth || ch != textureHeight) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cw, ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            textureWidth = cw;
            textureHeight = ch;
        }

        // 2) bind compute program & UBOs
        glUseProgram(computeProgram);
        uploadCameraUBO(cam);
        uploadDiskUBO();
        uploadIntegrationUBO();
        const array<GLuint, 6> zeroStatusCounts{};
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, diagnosticsSSBO);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(zeroStatusCounts),
                        zeroStatusCounts.data());

        if (runOptions.gpu_validation || runOptions.legacy_allocation_benchmark) {
            const GLsizeiptr pixelCount = static_cast<GLsizeiptr>(cw) * ch;
            if (runOptions.legacy_allocation_benchmark || pixelCount > validationPixelCapacity) {
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, statusPixelsSSBO);
                glBufferData(GL_SHADER_STORAGE_BUFFER, pixelCount * sizeof(GLuint), nullptr,
                             GL_DYNAMIC_READ);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, invariantPixelsSSBO);
                glBufferData(GL_SHADER_STORAGE_BUFFER, pixelCount * sizeof(float) * 8, nullptr,
                             GL_DYNAMIC_READ);
                if (!runOptions.legacy_allocation_benchmark) {
                    validationPixelCapacity = pixelCount;
                }
            }
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, statusPixelsSSBO);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, invariantPixelsSSBO);
        }

        // 3) bind it as image unit 0
        glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);

        // 4) dispatch grid
        GLuint groupsX = (GLuint)std::ceil(cw / 16.0f);
        GLuint groupsY = (GLuint)std::ceil(ch / 16.0f);
        glDispatchCompute(groupsX, groupsY, 1);

        // 5) sync
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT |
                        GL_BUFFER_UPDATE_BARRIER_BIT);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, diagnosticsSSBO);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(rayStatusCounts),
                           rayStatusCounts.data());
    }
    void uploadCameraUBO(const Camera& cam) {
        struct UBOData {
            vec3 pos;
            float _pad0;
            vec3 right;
            float _pad1;
            vec3 up;
            float _pad2;
            vec3 forward;
            float _pad3;
            float tanHalfFov;
            float aspect;
            int moving;
            int _pad4;
        } data{};
        vec3 fwd = normalize(cam.target - cam.position());
        vec3 up = vec3(0, 1, 0); // y axis is up, so disk is in x-z plane
        vec3 right = normalize(cross(fwd, up));
        up = cross(right, fwd);

        const float schwarzschildRadius =
            static_cast<float>(simulationConfig.black_hole.schwarzschild_radius_m);
        data.pos = cam.position() / schwarzschildRadius;
        data.right = right;
        data.up = up;
        data.forward = fwd;
        data.tanHalfFov =
            tan(radians(static_cast<float>(simulationConfig.camera.vertical_fov_degrees * 0.5)));
        data.aspect = float(lastComputeWidth) / float(lastComputeHeight);
        data.moving = (cam.dragging || cam.panning) ? 1 : 0;

        glBindBuffer(GL_UNIFORM_BUFFER, cameraUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(UBOData), &data);
    }
    void uploadDiskUBO() {
        // GPU geodesic/disk intersections are dimensionless (r_s = 1).
        float r1 =
            runOptions.gpu_validation
                ? 1.0f
                : static_cast<float>(simulationConfig.disk.inner_radius_in_schwarzschild_radii);
        float r2 =
            runOptions.gpu_validation
                ? 0.0f
                : static_cast<float>(simulationConfig.disk.outer_radius_in_schwarzschild_radii);
        float thickness = diskThicknessInRadii;
        constexpr double stefanBoltzmann = 5.670374419e-8;
        const double mass = simulationConfig.black_hole.mass_kg;
        const double radius = simulationConfig.black_hole.schwarzschild_radius_m;
        const double fluxScale = 3.0 * blackhole::physics::gravitational_constant_si * mass *
                                 simulationConfig.disk.accretion_rate_kg_per_s /
                                 (8.0 * M_PI * radius * radius * radius);
        const float temperatureScale = static_cast<float>(pow(fluxScale / stefanBoltzmann, 0.25));
        struct DiskData {
            float innerRadius;
            float outerRadius;
            float inclination;
            float thickness;
            float time;
            float cinematicAccretionScale;
            float detailStrength;
            float pad0;
            float temperatureScale;
            float rotationSign;
            int visible;
            int pad1;
        } diskData{r1,
                   r2,
                   diskInclinationRadians,
                   thickness,
                   static_cast<float>(fmod(glfwGetTime(), 10000.0)),
                   1.0f,
                   1.0f,
                   0.0f,
                   temperatureScale,
                   static_cast<float>(simulationConfig.disk.rotation_sign),
                   (!runOptions.gpu_validation && !runOptions.performance_smoke &&
                    simulationConfig.disk.visible)
                       ? 1
                       : 0,
                   0};

        glBindBuffer(GL_UNIFORM_BUFFER, diskUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(diskData), &diskData);
    }
    void uploadIntegrationUBO() {
        struct UBOData {
            float minimumStep;
            float maximumStep;
            float captureRadius;
            float escapeRadius;
            float absoluteTolerance;
            float relativeTolerance;
            int maximumSteps;
            int renderMode;
            int validationOverlay;
            int backgroundMode;
            int exportDiagnostics;
            int pad2;
        } data{};
        data.minimumStep = static_cast<float>(simulationConfig.integration.minimum_step);
        // The legacy benchmark retains the previous 0.05 bound. Both paths
        // use identical error tolerances and termination rules.
        data.maximumStep = static_cast<float>(runOptions.legacy_allocation_benchmark
                                                  ? 0.05
                                                  : simulationConfig.integration.maximum_step);
        data.captureRadius = static_cast<float>(simulationConfig.integration.capture_radius);
        data.escapeRadius = static_cast<float>(simulationConfig.integration.escape_radius);
        data.absoluteTolerance =
            static_cast<float>(simulationConfig.integration.absolute_tolerance);
        data.relativeTolerance =
            static_cast<float>(simulationConfig.integration.relative_tolerance);
        data.maximumSteps = simulationConfig.integration.maximum_steps;
        data.renderMode =
            simulationConfig.render_mode == blackhole::physics::RenderMode::Physical ? 0 : 1;
        data.validationOverlay = simulationConfig.validation_overlay ? 1 : 0;
        data.backgroundMode = simulationConfig.background_mode;
        data.exportDiagnostics =
            (runOptions.gpu_validation || runOptions.legacy_allocation_benchmark) ? 1 : 0;

        glBindBuffer(GL_UNIFORM_BUFFER, integrationUBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(data), &data);
    }
    void resizeFramebuffer(int widthPixels, int heightPixels) {
        if (widthPixels <= 0 || heightPixels <= 0)
            return;
        WIDTH = widthPixels;
        HEIGHT = heightPixels;
        COMPUTE_WIDTH = std::max(160, widthPixels / 2);
        COMPUTE_HEIGHT = std::max(90, heightPixels / 2);
        glViewport(0, 0, WIDTH, HEIGHT);
    }
    void initializeUi() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGui::GetIO().FontGlobalScale = 1.1f;
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 430");
        uiInitialized = true;
    }
    void drawHud(Camera& cam, double frameMilliseconds) {
        if (!uiInitialized || runOptions.gpu_validation)
            return;
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.90f);
        ImGui::Begin("Schwarzschild Scientific HUD", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
        const bool physical =
            simulationConfig.render_mode == blackhole::physics::RenderMode::Physical;
        ImGui::Text("Mode: %s", physical ? "Physical" : "Cinematic");
        ImGui::Text("Camera: %.4f r_s | FOV: %.1f deg",
                    cam.radius /
                        static_cast<float>(simulationConfig.black_hole.schwarzschild_radius_m),
                    simulationConfig.camera.vertical_fov_degrees);
        ImGui::Text("Disk: %.2f-%.2f r_s | inclination %.1f deg",
                    simulationConfig.disk.inner_radius_in_schwarzschild_radii,
                    simulationConfig.disk.outer_radius_in_schwarzschild_radii,
                    degrees(diskInclinationRadians));
        ImGui::Text("Tolerance: abs %.1e / rel %.1e",
                    simulationConfig.integration.absolute_tolerance,
                    simulationConfig.integration.relative_tolerance);
        ImGui::Text("Resolution: %dx%d | %.2f ms | %.1f FPS", lastComputeWidth, lastComputeHeight,
                    frameMilliseconds, frameMilliseconds > 0.0 ? 1000.0 / frameMilliseconds : 0.0);
        ImGui::TextDisabled("Sampling: %s%s; physics tolerance unchanged",
                            simulationConfig.resolution.full_resolution ? "full framebuffer"
                                                                        : "half framebuffer",
                            cam.moving && simulationConfig.resolution.dynamic_resolution &&
                                    !simulationConfig.resolution.full_resolution
                                ? " (halved while orbiting)"
                                : "");
        ImGui::TextColored(rayStatusCounts[4] || rayStatusCounts[5]
                               ? ImVec4(1.0f, 0.35f, 0.25f, 1.0f)
                               : ImVec4(0.35f, 1.0f, 0.45f, 1.0f),
                           "Unresolved: %u | Invalid: %u", rayStatusCounts[4], rayStatusCounts[5]);
        ImGui::Separator();
        if (ImGui::Button(physical ? "Switch to Cinematic" : "Switch to Physical")) {
            simulationConfig.render_mode = physical ? blackhole::physics::RenderMode::Cinematic
                                                    : blackhole::physics::RenderMode::Physical;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset known view"))
            cam.reset();
        ImGui::Checkbox("Disk", &simulationConfig.disk.visible);
        ImGui::SameLine();
        ImGui::Checkbox("Flamm's paraboloid", &simulationConfig.grid_visible);
        ImGui::Checkbox("Validation colors", &simulationConfig.validation_overlay);
        ImGui::SameLine();
        ImGui::Checkbox("Full-resolution still", &simulationConfig.resolution.full_resolution);
        bool darkBackground = simulationConfig.background_mode == 1;
        if (ImGui::Checkbox("Uniform dark background", &darkBackground)) {
            simulationConfig.background_mode = darkBackground ? 1 : 0;
        }
        float inclinationDegrees = degrees(diskInclinationRadians);
        if (ImGui::SliderFloat("Disk inclination", &inclinationDegrees, 0.0f, 85.0f, "%.1f deg")) {
            diskInclinationRadians = radians(inclinationDegrees);
        }
        float fov = static_cast<float>(simulationConfig.camera.vertical_fov_degrees);
        if (ImGui::SliderFloat("Vertical FOV", &fov, 20.0f, 100.0f, "%.1f deg")) {
            simulationConfig.camera.vertical_fov_degrees = fov;
        }
        if (ImGui::Button("Reverse disk rotation")) {
            simulationConfig.disk.rotation_sign *= -1.0;
        }
        ImGui::TextDisabled("Keys: M mode, D disk, B background, G grid, V validation, Home reset");
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        const string title =
            string("Schwarzschild Black Hole - ") + (physical ? "Physical" : "Cinematic");
        glfwSetWindowTitle(window, title.c_str());
    }
    void shutdownUi() {
        if (!uiInitialized)
            return;
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        uiInitialized = false;
    }
    void shutdown() {
        if (shutdownComplete)
            return;
        shutdownComplete = true;
        if (window != nullptr) {
            glfwMakeContextCurrent(window);
            shutdownUi();
            const GLuint buffers[] = {cameraUBO,       diskUBO,          integrationUBO,
                                      diagnosticsSSBO, statusPixelsSSBO, invariantPixelsSSBO,
                                      gridVBO,         gridEBO,          quadVBO};
            glDeleteBuffers(static_cast<GLsizei>(std::size(buffers)), buffers);
            const GLuint vertexArrays[] = {quadVAO, gridVAO};
            glDeleteVertexArrays(static_cast<GLsizei>(std::size(vertexArrays)), vertexArrays);
            glDeleteTextures(1, &texture);
            const GLuint programs[] = {shaderProgram, gridShaderProgram, computeProgram};
            for (GLuint program : programs) {
                if (program != 0)
                    glDeleteProgram(program);
            }
            glfwDestroyWindow(window);
            window = nullptr;
        }
        glfwTerminate();
    }
    bool exportGpuValidation(const string& outputPath) {
        const size_t pixelCount =
            static_cast<size_t>(lastComputeWidth) * static_cast<size_t>(lastComputeHeight);
        if (pixelCount == 0)
            return false;
        vector<GLuint> statuses(pixelCount);
        vector<float> invariants(pixelCount * 8);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, statusPixelsSSBO);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                           static_cast<GLsizeiptr>(statuses.size() * sizeof(GLuint)),
                           statuses.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, invariantPixelsSSBO);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                           static_cast<GLsizeiptr>(invariants.size() * sizeof(float)),
                           invariants.data());

        ofstream output(outputPath, ios::trunc);
        if (!output)
            return false;
        output << "x,y,status,initial_null,initial_E,initial_L2,initial_Lz,"
                  "final_null,final_E,final_L2,final_r\n";
        output << setprecision(9);
        for (int y = 0; y < lastComputeHeight; ++y) {
            for (int x = 0; x < lastComputeWidth; ++x) {
                const size_t index = static_cast<size_t>(y) * lastComputeWidth + x;
                output << x << ',' << y << ',' << statuses[index];
                for (size_t component = 0; component < 8; ++component) {
                    output << ',' << invariants[index * 8 + component];
                }
                output << '\n';
            }
        }
        return true;
    }
    bool captureFramebufferBmp(const string& outputPath) {
        if (WIDTH <= 0 || HEIGHT <= 0)
            return false;
        vector<unsigned char> rgb(static_cast<size_t>(WIDTH) * HEIGHT * 3);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());

        const std::uint32_t rowSize = static_cast<std::uint32_t>((WIDTH * 3 + 3) & ~3);
        const std::uint32_t imageSize = rowSize * static_cast<std::uint32_t>(HEIGHT);
        const std::uint32_t fileSize = 54u + imageSize;
        array<unsigned char, 54> header{};
        header[0] = 'B';
        header[1] = 'M';
        const auto write32 = [&header](size_t offset, std::uint32_t value) {
            for (size_t byte = 0; byte < 4; ++byte) {
                header[offset + byte] = static_cast<unsigned char>((value >> (8 * byte)) & 0xffu);
            }
        };
        const auto write16 = [&header](size_t offset, std::uint16_t value) {
            header[offset] = static_cast<unsigned char>(value & 0xffu);
            header[offset + 1] = static_cast<unsigned char>((value >> 8) & 0xffu);
        };
        write32(2, fileSize);
        write32(10, 54u);
        write32(14, 40u);
        write32(18, static_cast<std::uint32_t>(WIDTH));
        write32(22, static_cast<std::uint32_t>(HEIGHT));
        write16(26, 1u);
        write16(28, 24u);
        write32(34, imageSize);

        ofstream output(outputPath, ios::binary | ios::trunc);
        if (!output)
            return false;
        output.write(reinterpret_cast<const char*>(header.data()), header.size());
        vector<unsigned char> row(rowSize, 0u);
        for (int y = 0; y < HEIGHT; ++y) {
            for (int x = 0; x < WIDTH; ++x) {
                const size_t source = (static_cast<size_t>(y) * WIDTH + x) * 3;
                const size_t destination = static_cast<size_t>(x) * 3;
                row[destination] = rgb[source + 2];
                row[destination + 1] = rgb[source + 1];
                row[destination + 2] = rgb[source];
            }
            output.write(reinterpret_cast<const char*>(row.data()), row.size());
        }
        return output.good();
    }

    void createQuadResources() {
        float quadVertices[] = {
            // positions   // texCoords
            -1.0f, 1.0f,  0.0f, 1.0f, // top left
            -1.0f, -1.0f, 0.0f, 0.0f, // bottom left
            1.0f,  -1.0f, 1.0f, 0.0f, // bottom right

            -1.0f, 1.0f,  0.0f, 1.0f, // top left
            1.0f,  -1.0f, 1.0f, 0.0f, // bottom right
            1.0f,  1.0f,  1.0f, 1.0f  // top right
        };

        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);

        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D,
                     0,        // mip
                     GL_RGBA8, // internal format
                     COMPUTE_WIDTH, COMPUTE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        textureWidth = COMPUTE_WIDTH;
        textureHeight = COMPUTE_HEIGHT;
    }
};
struct CallbackState {
    Camera* camera;
    Engine* engine;
};

void setupCameraCallbacks(GLFWwindow* window, CallbackState& state) {
    glfwSetWindowUserPointer(window, &state);

    glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
        auto* callbacks = static_cast<CallbackState*>(glfwGetWindowUserPointer(win));
        callbacks->camera->process_mouse_button(button, action, mods, win);
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow* win, double x, double y) {
        auto* callbacks = static_cast<CallbackState*>(glfwGetWindowUserPointer(win));
        callbacks->camera->process_mouse_move(x, y);
    });

    glfwSetScrollCallback(window, [](GLFWwindow* win, double xoffset, double yoffset) {
        auto* callbacks = static_cast<CallbackState*>(glfwGetWindowUserPointer(win));
        callbacks->camera->process_scroll(xoffset, yoffset);
    });

    glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
        auto* callbacks = static_cast<CallbackState*>(glfwGetWindowUserPointer(win));
        callbacks->camera->process_key(key, scancode, action, mods);
    });
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* win, int width, int height) {
        auto* callbacks = static_cast<CallbackState*>(glfwGetWindowUserPointer(win));
        callbacks->engine->resizeFramebuffer(width, height);
    });
}

// -- MAIN -- //
int main(int argc, char** argv) {
    RunOptions runOptions;
    bool& gpuValidationMode = runOptions.gpu_validation;
    bool& interactionSmokeMode = runOptions.interaction_smoke;
    bool& performanceSmokeMode = runOptions.performance_smoke;
    bool& legacyAllocationBenchmark = runOptions.legacy_allocation_benchmark;
    string& captureOutputPath = runOptions.capture_output_path;
    int& captureFramesRemaining = runOptions.capture_frames_remaining;
    auto simulationConfig = blackhole::physics::make_default_simulation_configuration();
    float diskInclinationRadians =
        radians(static_cast<float>(simulationConfig.disk.inclination_degrees));
    float diskThicknessInRadii =
        static_cast<float>(simulationConfig.disk.half_thickness_in_schwarzschild_radii);

    for (int index = 1; index < argc; ++index) {
        if (string(argv[index]) == "--validation-export") {
            gpuValidationMode = true;
        } else if (string(argv[index]) == "--capture-physical") {
            simulationConfig.render_mode = blackhole::physics::RenderMode::Physical;
            captureOutputPath = "physical.bmp";
            captureFramesRemaining = 2;
        } else if (string(argv[index]) == "--capture-cinematic") {
            simulationConfig.render_mode = blackhole::physics::RenderMode::Cinematic;
            captureOutputPath = "cinematic.bmp";
            captureFramesRemaining = 2;
        } else if (string(argv[index]) == "--capture-vacuum") {
            simulationConfig.render_mode = blackhole::physics::RenderMode::Physical;
            simulationConfig.disk.visible = false;
            simulationConfig.background_mode = 1;
            captureOutputPath = "physical_vacuum.bmp";
            captureFramesRemaining = 2;
        } else if (string(argv[index]) == "--capture-grid") {
            simulationConfig.disk.visible = false;
            simulationConfig.background_mode = 1;
            simulationConfig.grid_visible = true;
            captureOutputPath = "flamm_paraboloid.bmp";
            captureFramesRemaining = 2;
        } else if (string(argv[index]) == "--interaction-smoke") {
            interactionSmokeMode = true;
        } else if (string(argv[index]) == "--performance-smoke") {
            performanceSmokeMode = true;
        } else if (string(argv[index]) == "--legacy-allocations") {
            legacyAllocationBenchmark = true;
        } else if (string(argv[index]) == "--full-resolution") {
            simulationConfig.resolution.full_resolution = true;
        }
    }
    Camera camera(simulationConfig, diskInclinationRadians, diskThicknessInRadii);
    Engine engine(simulationConfig, diskInclinationRadians, diskThicknessInRadii, runOptions);
    CallbackState callbackState{&camera, &engine};
    setupCameraCallbacks(engine.window, callbackState);
    bool interactionSmokePassed = true;
    if (interactionSmokeMode) {
        const auto initialMode = simulationConfig.render_mode;
        const bool initialDisk = simulationConfig.disk.visible;
        const int initialBackground = simulationConfig.background_mode;
        const bool initialGrid = simulationConfig.grid_visible;
        const bool initialValidation = simulationConfig.validation_overlay;
        const double initialRotation = simulationConfig.disk.rotation_sign;
        const float initialInclination = diskInclinationRadians;
        const float initialThickness = diskThicknessInRadii;
        camera.process_key(GLFW_KEY_M, 0, GLFW_PRESS, 0);
        camera.process_key(GLFW_KEY_D, 0, GLFW_PRESS, 0);
        camera.process_key(GLFW_KEY_B, 0, GLFW_PRESS, 0);
        camera.process_key(GLFW_KEY_G, 0, GLFW_PRESS, 0);
        camera.process_key(GLFW_KEY_V, 0, GLFW_PRESS, 0);
        camera.process_key(GLFW_KEY_R, 0, GLFW_PRESS, 0);
        camera.process_key(GLFW_KEY_I, 0, GLFW_PRESS, 0);
        camera.process_key(GLFW_KEY_T, 0, GLFW_PRESS, 0);
        camera.process_scroll(0.0, 1.0e6);
        const bool cameraLimitPassed =
            std::abs(camera.radius - camera.min_radius) < 1.0f &&
            camera.radius > static_cast<float>(simulationConfig.black_hole.schwarzschild_radius_m);
        camera.process_key(GLFW_KEY_HOME, 0, GLFW_PRESS, 0);
        interactionSmokePassed =
            simulationConfig.render_mode != initialMode &&
            simulationConfig.disk.visible != initialDisk &&
            simulationConfig.background_mode != initialBackground &&
            simulationConfig.grid_visible != initialGrid &&
            simulationConfig.validation_overlay != initialValidation &&
            simulationConfig.disk.rotation_sign == -initialRotation &&
            diskInclinationRadians > initialInclination &&
            diskThicknessInRadii > initialThickness && cameraLimitPassed &&
            std::abs(camera.radius /
                         static_cast<float>(simulationConfig.black_hole.schwarzschild_radius_m) -
                     simulationConfig.camera.radius_in_schwarzschild_radii) < 1.0e-4;
        // Restore the standard validation view before resize checks.
        simulationConfig = blackhole::physics::make_default_simulation_configuration();
        diskInclinationRadians =
            radians(static_cast<float>(simulationConfig.disk.inclination_degrees));
        diskThicknessInRadii =
            static_cast<float>(simulationConfig.disk.half_thickness_in_schwarzschild_radii);
        camera.reset();
    }
    if (!gpuValidationMode && !performanceSmokeMode)
        engine.initializeUi();
    if (!gpuValidationMode)
        engine.generateGrid();
    if (interactionSmokeMode) {
        simulationConfig.resolution.dynamic_resolution = false;
        camera.moving = false;
        engine.dispatchCompute(camera);
        const auto stationaryCounts = engine.rayStatusCounts;
        camera.moving = true;
        engine.dispatchCompute(camera);
        const bool modelStable = engine.rayStatusCounts == stationaryCounts;
        interactionSmokePassed = interactionSmokePassed && modelStable;
        cout << "[INTERACTION] motion_model_stability=" << (modelStable ? "PASS" : "FAIL") << '\n';
        camera.moving = false;
        simulationConfig.resolution.dynamic_resolution = true;
    }
    double lastPrintTime = glfwGetTime();
    int framesCount = 0;

    double lastTime = glfwGetTime();
    const array<pair<int, int>, 3> resizeSizes{{{800, 600}, {1280, 720}, {600, 900}}};
    size_t resizeIndex = 0;
    constexpr int performanceWarmupFrames = 5;
    constexpr int performanceMeasuredFrames = 30;
    int performanceFrame = 0;
    double performanceMilliseconds = 0.0;
    GLuint performanceQuery = 0;
    if (performanceSmokeMode)
        glGenQueries(1, &performanceQuery);
    while (!glfwWindowShouldClose(engine.window)) {
        if (interactionSmokeMode && resizeIndex < resizeSizes.size()) {
            simulationConfig.resolution.full_resolution = resizeIndex == 1;
            glfwSetWindowSize(engine.window, resizeSizes[resizeIndex].first,
                              resizeSizes[resizeIndex].second);
            glfwPollEvents();
            int framebufferWidth = 0;
            int framebufferHeight = 0;
            glfwGetFramebufferSize(engine.window, &framebufferWidth, &framebufferHeight);
            engine.resizeFramebuffer(framebufferWidth, framebufferHeight);
        }
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // optional, but good practice
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        double now = glfwGetTime();
        double dt = now - lastTime; // seconds since last frame
        lastTime = now;

        // ---------- RUN RAYTRACER ------------- //
        glViewport(0, 0, engine.WIDTH, engine.HEIGHT);
        if (performanceSmokeMode)
            glBeginQuery(GL_TIME_ELAPSED, performanceQuery);
        const auto computeStart = std::chrono::steady_clock::now();
        engine.dispatchCompute(camera);
        if (performanceSmokeMode)
            glEndQuery(GL_TIME_ELAPSED);
        const auto computeEnd = std::chrono::steady_clock::now();
        double computeMilliseconds =
            std::chrono::duration<double, std::milli>(computeEnd - computeStart).count();
        if (performanceSmokeMode) {
            GLuint64 elapsedNanoseconds = 0;
            glGetQueryObjectui64v(performanceQuery, GL_QUERY_RESULT, &elapsedNanoseconds);
            computeMilliseconds = static_cast<double>(elapsedNanoseconds) * 1.0e-6;
        }
        if (gpuValidationMode) {
            const bool exported = engine.exportGpuValidation("gpu_validation.csv");
            cout << (exported ? "[VALIDATION] Wrote gpu_validation.csv\n"
                              : "[VALIDATION] Failed to write gpu_validation.csv\n");
            break;
        }
        if (performanceSmokeMode) {
            engine.drawFullScreenQuad();
            glfwSwapBuffers(engine.window);
            glfwPollEvents();
            if (performanceFrame >= performanceWarmupFrames) {
                performanceMilliseconds += computeMilliseconds;
            }
            ++performanceFrame;
            if (performanceFrame >= performanceWarmupFrames + performanceMeasuredFrames) {
                const double average = performanceMilliseconds / performanceMeasuredFrames;
                cout << "[PERFORMANCE] allocation_path="
                     << (legacyAllocationBenchmark ? "legacy" : "optimized")
                     << " mode=Physical-vacuum resolution=" << engine.lastComputeWidth << 'x'
                     << engine.lastComputeHeight
                     << " abs_tol=" << simulationConfig.integration.absolute_tolerance
                     << " rel_tol=" << simulationConfig.integration.relative_tolerance
                     << " average_compute_ms=" << fixed << setprecision(3) << average
                     << " samples=" << performanceMeasuredFrames
                     << " captured=" << engine.rayStatusCounts[0]
                     << " escaped=" << engine.rayStatusCounts[1]
                     << " disk=" << engine.rayStatusCounts[2]
                     << " reserved=" << engine.rayStatusCounts[3]
                     << " unresolved=" << engine.rayStatusCounts[4]
                     << " invalid=" << engine.rayStatusCounts[5] << " total="
                     << std::accumulate(engine.rayStatusCounts.begin(),
                                        engine.rayStatusCounts.end(), 0u)
                     << '\n';
                break;
            }
            continue;
        }
        engine.drawFullScreenQuad();

        // Flamm's paraboloid is an optional embedding diagram of a
        // constant-time equatorial spatial slice, drawn as an overlay.
        if (simulationConfig.grid_visible) {
            mat4 view = lookAt(camera.position(), camera.target, vec3(0, 1, 0));
            mat4 proj = perspective(
                radians(static_cast<float>(simulationConfig.camera.vertical_fov_degrees)),
                float(engine.WIDTH) / engine.HEIGHT, 1e9f, 1e14f);
            engine.drawGrid(proj * view);
        }
        engine.drawHud(camera, dt * 1000.0);
        if (!captureOutputPath.empty() && --captureFramesRemaining <= 0) {
            const bool captured = engine.captureFramebufferBmp(captureOutputPath);
            cout << (captured ? "[CAPTURE] Wrote " : "[CAPTURE] Failed to write ")
                 << captureOutputPath << " status_total="
                 << std::accumulate(engine.rayStatusCounts.begin(), engine.rayStatusCounts.end(),
                                    0u)
                 << '\n';
            break;
        }
        if (interactionSmokeMode) {
            const double framebufferAspect = static_cast<double>(engine.WIDTH) / engine.HEIGHT;
            const double computeAspect =
                static_cast<double>(engine.lastComputeWidth) / engine.lastComputeHeight;
            const bool framePassed =
                std::abs(framebufferAspect - computeAspect) < 0.01 &&
                (!simulationConfig.resolution.full_resolution ||
                 (engine.lastComputeWidth == engine.WIDTH &&
                  engine.lastComputeHeight == engine.HEIGHT)) &&
                std::accumulate(engine.rayStatusCounts.begin(), engine.rayStatusCounts.end(), 0u) ==
                    static_cast<unsigned int>(engine.lastComputeWidth * engine.lastComputeHeight) &&
                engine.rayStatusCounts[4] == 0 && engine.rayStatusCounts[5] == 0;
            interactionSmokePassed = interactionSmokePassed && framePassed;
            cout << "[INTERACTION] " << engine.WIDTH << 'x' << engine.HEIGHT
                 << " compute=" << engine.lastComputeWidth << 'x' << engine.lastComputeHeight
                 << " unresolved=" << engine.rayStatusCounts[4]
                 << " invalid=" << engine.rayStatusCounts[5] << ' '
                 << (simulationConfig.resolution.full_resolution ? "full " : "interactive ")
                 << (framePassed ? "PASS" : "FAIL") << '\n';
            ++resizeIndex;
            if (resizeIndex >= resizeSizes.size())
                break;
        }

        ++framesCount;
        if (now - lastPrintTime >= 1.0) {
            cout << "[DIAGNOSTICS] fps=" << framesCount << " captured=" << engine.rayStatusCounts[0]
                 << " escaped=" << engine.rayStatusCounts[1]
                 << " disk=" << engine.rayStatusCounts[2]
                 << " reserved=" << engine.rayStatusCounts[3]
                 << " unresolved=" << engine.rayStatusCounts[4]
                 << " invalid=" << engine.rayStatusCounts[5] << '\n';
            framesCount = 0;
            lastPrintTime = now;
        }

        // 6) present to screen
        glfwSwapBuffers(engine.window);
        glfwPollEvents();
    }

    const unsigned int glDebugErrors = engine.glDebugErrorCount;
    if (performanceQuery != 0)
        glDeleteQueries(1, &performanceQuery);
    engine.shutdown();
    if (interactionSmokeMode) {
        cout << "[INTERACTION] overall=" << (interactionSmokePassed ? "PASS" : "FAIL") << '\n';
    }
    cout << "[OPENGL] debug_errors=" << glDebugErrors << '\n';
    return interactionSmokePassed && glDebugErrors == 0 ? 0 : 1;
}
