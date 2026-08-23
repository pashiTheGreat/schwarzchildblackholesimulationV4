#include "app/camera.hpp"

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <iostream>

namespace blackhole::app {

Camera::Camera(physics::SimulationConfiguration& configuration, float& disk_inclination_radians,
               float& disk_thickness_in_radii)
    : configuration_(configuration), disk_inclination_radians_(disk_inclination_radians),
      disk_thickness_in_radii_(disk_thickness_in_radii) {
    min_radius = static_cast<float>(configuration_.camera.minimum_static_radius *
                                    configuration_.black_hole.schwarzschild_radius_m);
    max_radius = static_cast<float>(configuration_.camera.maximum_radius *
                                    configuration_.black_hole.schwarzschild_radius_m);
    reset();
}

glm::vec3 Camera::position() const {
    const float clamped_elevation = glm::clamp(elevation, 0.01f, glm::pi<float>() - 0.01f);
    return {radius * std::sin(clamped_elevation) * std::cos(azimuth),
            radius * std::cos(clamped_elevation),
            radius * std::sin(clamped_elevation) * std::sin(azimuth)};
}

void Camera::reset() {
    radius = static_cast<float>(configuration_.camera.radius_in_schwarzschild_radii *
                                configuration_.black_hole.schwarzschild_radius_m);
    azimuth = 0.0f;
    elevation = glm::half_pi<float>();
    target = glm::vec3(0.0f);
    dragging = false;
    panning = false;
    moving = false;
}

void Camera::update() {
    target = glm::vec3(0.0f);
    moving = dragging || panning;
}

void Camera::process_mouse_move(double x, double y) {
    const float dx = static_cast<float>(x - last_x);
    const float dy = static_cast<float>(y - last_y);
    if (dragging && !panning) {
        azimuth += dx * orbit_speed;
        elevation -= dy * orbit_speed;
        elevation = glm::clamp(elevation, 0.01f, glm::pi<float>() - 0.01f);
    }
    last_x = x;
    last_y = y;
    update();
}

void Camera::process_mouse_button(int button, int action, int, GLFWwindow* window) {
    if (button != GLFW_MOUSE_BUTTON_LEFT && button != GLFW_MOUSE_BUTTON_MIDDLE) {
        return;
    }
    if (action == GLFW_PRESS) {
        dragging = true;
        panning = false;
        glfwGetCursorPos(window, &last_x, &last_y);
    } else if (action == GLFW_RELEASE) {
        dragging = false;
        panning = false;
    }
}

void Camera::process_scroll(double, double y_offset) {
    const float requested_radius = radius - static_cast<float>(y_offset * zoom_speed);
    if (requested_radius < min_radius) {
        std::cout << "[INFO] Camera limited to " << configuration_.camera.minimum_static_radius
                  << " r_s: a static observer cannot exist at or inside "
                     "the event horizon.\n";
    }
    radius = glm::clamp(requested_radius, min_radius, max_radius);
    update();
}

void Camera::process_key(int key, int, int action, int mods) {
    if (action == GLFW_PRESS && key == GLFW_KEY_G) {
        configuration_.grid_visible = !configuration_.grid_visible;
    }
    if (action == GLFW_PRESS && key == GLFW_KEY_M) {
        configuration_.render_mode = configuration_.render_mode == physics::RenderMode::Physical
                                         ? physics::RenderMode::Cinematic
                                         : physics::RenderMode::Physical;
        std::cout << "[INFO] Render mode: "
                  << (configuration_.render_mode == physics::RenderMode::Physical ? "Physical"
                                                                                  : "Cinematic")
                  << '\n';
    }
    if (action == GLFW_PRESS && key == GLFW_KEY_D) {
        configuration_.disk.visible = !configuration_.disk.visible;
        std::cout << "[INFO] Disk " << (configuration_.disk.visible ? "visible" : "hidden") << '\n';
    }
    if (action == GLFW_PRESS && key == GLFW_KEY_R) {
        configuration_.disk.rotation_sign *= -1.0;
        std::cout << "[INFO] Disk rotation reversed\n";
    }
    if (action == GLFW_PRESS && key == GLFW_KEY_B) {
        configuration_.background_mode = 1 - configuration_.background_mode;
    }
    if (action == GLFW_PRESS && key == GLFW_KEY_V) {
        configuration_.validation_overlay = !configuration_.validation_overlay;
    }
    if (action == GLFW_PRESS && key == GLFW_KEY_HOME)
        reset();
    if ((action == GLFW_PRESS || action == GLFW_REPEAT) && key == GLFW_KEY_I) {
        const float direction = (mods & GLFW_MOD_SHIFT) ? -1.0f : 1.0f;
        disk_inclination_radians_ =
            glm::clamp(disk_inclination_radians_ + glm::radians(5.0f) * direction,
                       glm::radians(0.0f), glm::radians(85.0f));
        std::cout << "[INFO] Disk inclination: " << glm::degrees(disk_inclination_radians_)
                  << " degrees\n";
    }
    if ((action == GLFW_PRESS || action == GLFW_REPEAT) && key == GLFW_KEY_T) {
        const float scale = (mods & GLFW_MOD_SHIFT) ? (1.0f / 1.15f) : 1.15f;
        disk_thickness_in_radii_ = glm::clamp(disk_thickness_in_radii_ * scale, 0.02f, 0.75f);
        std::cout << "[INFO] Disk half-thickness: " << disk_thickness_in_radii_
                  << " Schwarzschild radii\n";
    }
}

} // namespace blackhole::app
