#include "physics/simulation_config.hpp"

#include "physics/schwarzschild.hpp"

#include <algorithm>
#include <cmath>

namespace blackhole::physics {

SimulationConfiguration make_default_simulation_configuration() {
    SimulationConfiguration configuration;
    configuration.black_hole.schwarzschild_radius_m =
        schwarzschild_radius(configuration.black_hole.mass_kg);
    configuration.camera.radius_in_schwarzschild_radii =
        std::max(configuration.camera.radius_in_schwarzschild_radii,
                 configuration.camera.minimum_static_radius);
    return configuration;
}

bool is_valid(const SimulationConfiguration& configuration) {
    const auto& hole = configuration.black_hole;
    const auto& camera = configuration.camera;
    const auto& disk = configuration.disk;
    const auto& integration = configuration.integration;
    const auto& resolution = configuration.resolution;

    return std::isfinite(hole.mass_kg) && hole.mass_kg > 0.0 &&
           std::isfinite(hole.schwarzschild_radius_m) && hole.schwarzschild_radius_m > 0.0 &&
           std::isfinite(camera.radius_in_schwarzschild_radii) &&
           camera.minimum_static_radius > 1.0 &&
           camera.radius_in_schwarzschild_radii >= camera.minimum_static_radius &&
           camera.radius_in_schwarzschild_radii <= camera.maximum_radius &&
           camera.vertical_fov_degrees > 0.0 && camera.vertical_fov_degrees < 180.0 &&
           disk.inner_radius_in_schwarzschild_radii >= 3.0 &&
           disk.outer_radius_in_schwarzschild_radii > disk.inner_radius_in_schwarzschild_radii &&
           std::isfinite(disk.half_thickness_in_schwarzschild_radii) &&
           disk.half_thickness_in_schwarzschild_radii >= 0.02 &&
           disk.half_thickness_in_schwarzschild_radii <= 0.75 &&
           std::abs(disk.rotation_sign) == 1.0 && integration.absolute_tolerance > 0.0 &&
           integration.relative_tolerance > 0.0 && integration.minimum_step > 0.0 &&
           integration.maximum_step >= integration.minimum_step && integration.maximum_steps > 0 &&
           integration.capture_radius > 1.0 &&
           integration.escape_radius > integration.capture_radius && resolution.width > 0 &&
           resolution.height > 0 && configuration.background_mode >= 0 &&
           configuration.background_mode <= 1;
}

} // namespace blackhole::physics
