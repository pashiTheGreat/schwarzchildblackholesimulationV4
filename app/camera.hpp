#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "physics/simulation_config.hpp"

namespace blackhole::app {

// Owns the static-observer camera and all keyboard/mouse state changes. The
// configuration is explicit so input cannot silently alter renderer globals.
class Camera {
  public:
    Camera(physics::SimulationConfiguration& configuration, float& disk_inclination_radians,
           float& disk_thickness_in_radii);

    glm::vec3 position() const;
    void reset();
    void update();
    void process_mouse_move(double x, double y);
    void process_mouse_button(int button, int action, int mods, GLFWwindow* window);
    void process_scroll(double x_offset, double y_offset);
    void process_key(int key, int scancode, int action, int mods);

    glm::vec3 target{0.0f};
    float radius = 0.0f;
    float min_radius = 0.0f;
    float max_radius = 0.0f;
    float azimuth = 0.0f;
    float elevation = 0.0f;
    float orbit_speed = 0.01f;
    double zoom_speed = 25e9;
    bool dragging = false;
    bool panning = false;
    bool moving = false;
    double last_x = 0.0;
    double last_y = 0.0;

  private:
    physics::SimulationConfiguration& configuration_;
    float& disk_inclination_radians_;
    float& disk_thickness_in_radii_;
};

} // namespace blackhole::app
