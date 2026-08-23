#pragma once

#include <cstdint>

namespace blackhole::physics {

// RenderMode deliberately keeps non-physical presentation choices separate
// from the default numerical model.  The renderer consumes this configuration
// instead of hard-coding an astrophysical object in a shader.
enum class RenderMode : std::uint8_t {
    Physical,
    Cinematic,
};

struct BlackHoleConfiguration {
    // SI input.  Vacuum geodesic integration converts this to r_s = 1.
    double mass_kg = 8.54e36;
    double schwarzschild_radius_m = 0.0; // Derived during configuration.
};

struct CameraConfiguration {
    // Dimensionless Schwarzschild radii.  A static observer must remain
    // strictly outside the horizon, hence the explicit margin.
    double radius_in_schwarzschild_radii = 4.9976;
    double minimum_static_radius = 1.05;
    double maximum_radius = 80.0;
    double vertical_fov_degrees = 60.0;
};

struct DiskConfiguration {
    // Dimensionless radii.  Physical disk thermodynamics remain in SI when
    // they are added to the renderer.
    double inner_radius_in_schwarzschild_radii = 3.0;
    double outer_radius_in_schwarzschild_radii = 12.0;
    double inclination_degrees = 20.0;
    double half_thickness_in_schwarzschild_radii = 0.12;
    double accretion_rate_kg_per_s = 1.0e15;
    double rotation_sign = 1.0;
    bool visible = true;
};

struct IntegrationConfiguration {
    // Affine parameter is scaled by r_s: lambda_dimensionless = lambda / r_s.
    // The production shader uses IEEE-754 float.  Tolerances below its
    // practical strong-field resolution cause false step rejection; the
    // double-precision CPU validator uses tighter per-test settings.
    double absolute_tolerance = 2.0e-5;
    double relative_tolerance = 2.0e-5;
    double minimum_step = 1.0e-5;
    double maximum_step = 0.10;
    int maximum_steps = 24000;
    double capture_radius = 1.0005;
    double escape_radius = 80.0;
};

struct ResolutionConfiguration {
    int width = 1280;
    int height = 720;
    // Interactive rendering uses half the framebuffer size and may halve it
    // again while orbiting. This changes sampling density only; the same
    // geodesic equations, tolerances, and termination rules remain active.
    bool dynamic_resolution = true;
    bool full_resolution = false;
};

struct SimulationConfiguration {
    BlackHoleConfiguration black_hole;
    CameraConfiguration camera;
    DiskConfiguration disk;
    IntegrationConfiguration integration;
    ResolutionConfiguration resolution;
    RenderMode render_mode = RenderMode::Physical;
    int background_mode = 0; // 0=procedural sky, 1=uniform dark validation sky.
    bool grid_visible = false;
    bool validation_overlay = false;
};

// Validates and completes a configuration.  It clamps only values whose
// validity is required for a static observer; callers can report the clamp.
SimulationConfiguration make_default_simulation_configuration();
bool is_valid(const SimulationConfiguration& configuration);

} // namespace blackhole::physics
