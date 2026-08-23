#include "physics/accretion_disk.hpp"
#include "physics/schwarzschild.hpp"
#include "physics/simulation_config.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct TestResult {
    std::string name;
    bool passed;
    std::string detail;
};

bool close_to(double actual, double expected, double relative_tolerance,
              double absolute_tolerance = 0.0) {
    return std::abs(actual - expected) <=
           absolute_tolerance + relative_tolerance * std::max(std::abs(actual), std::abs(expected));
}

std::string format_value(double value) {
    std::ostringstream stream;
    stream << std::setprecision(12) << value;
    return stream.str();
}

double vector_distance(const blackhole::physics::Vec3& a, const blackhole::physics::Vec3& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

blackhole::physics::Vec3 cartesian_velocity(const blackhole::physics::PhotonState& state) {
    const double sin_theta = std::sin(state.theta);
    const double cos_theta = std::cos(state.theta);
    const double sin_phi = std::sin(state.phi);
    const double cos_phi = std::cos(state.phi);
    return {
        state.dr * sin_theta * cos_phi + state.r * cos_theta * state.dtheta * cos_phi -
            state.r * sin_theta * sin_phi * state.dphi,
        state.dr * cos_theta - state.r * sin_theta * state.dtheta,
        state.dr * sin_theta * sin_phi + state.r * cos_theta * state.dtheta * sin_phi +
            state.r * sin_theta * cos_phi * state.dphi,
    };
}

double vector_angle(const blackhole::physics::Vec3& a, const blackhole::physics::Vec3& b) {
    const double dot_product = a.x * b.x + a.y * b.y + a.z * b.z;
    const double length_a = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    const double length_b = std::sqrt(b.x * b.x + b.y * b.y + b.z * b.z);
    return std::acos(std::clamp(dot_product / (length_a * length_b), -1.0, 1.0));
}

enum class TraceResult { Captured, Escaped, Unresolved };

const char* status_name(blackhole::physics::RayStatus status) {
    using blackhole::physics::RayStatus;
    switch (status) {
    case RayStatus::Captured:
        return "captured";
    case RayStatus::Escaped:
        return "escaped";
    case RayStatus::Unresolved:
        return "unresolved";
    case RayStatus::Invalid:
        return "invalid";
    }
    return "unknown";
}

TraceResult trace_fixed_step(blackhole::physics::PhotonState state, double step, int maximum_steps,
                             double capture_radius, double escape_radius) {
    using namespace blackhole::physics;
    for (int index = 0; index < maximum_steps; ++index) {
        if (state.r <= capture_radius) {
            return TraceResult::Captured;
        }
        if (state.r >= escape_radius && state.dr > 0.0) {
            return TraceResult::Escaped;
        }
        state = rk4_step(state, step);
        if (!is_finite(state)) {
            return TraceResult::Unresolved;
        }
    }
    return TraceResult::Unresolved;
}

TestResult test_schwarzschild_radius() {
    constexpr double solar_mass_kg = 1.98847e30;
    constexpr double expected_radius_m = 2953.339382066878;
    const double actual = blackhole::physics::schwarzschild_radius(solar_mass_kg);
    const bool passed = close_to(actual, expected_radius_m, 1.0e-12);
    return {"Schwarzschild radius", passed,
            "actual=" + format_value(actual) + " m, expected=" + format_value(expected_radius_m) +
                " m, rel_tol=1e-12"};
}

TestResult test_configuration() {
    const auto configuration = blackhole::physics::make_default_simulation_configuration();
    const bool passed = blackhole::physics::is_valid(configuration) &&
                        configuration.render_mode == blackhole::physics::RenderMode::Physical &&
                        configuration.camera.radius_in_schwarzschild_radii > 1.0;
    return {"Default simulation configuration", passed,
            "camera=" + format_value(configuration.camera.radius_in_schwarzschild_radii) +
                " r_s, r_s=" + format_value(configuration.black_hole.schwarzschild_radius_m) +
                " m, default_mode=Physical"};
}

TestResult test_static_tetrad_null_constraint() {
    using namespace blackhole::physics;
    const std::vector<SphericalPosition> observers{
        {1.05, 0.35, 0.1}, {1.5, 1.1, -2.2},  {4.9976, 1.5707963267948966, 0.0},
        {25.0, 2.5, 1.3},  {80.0, 0.7, -0.9},
    };
    std::vector<Vec3> directions{
        {1.0, 0.0, 0.0},
        {-1.0, 0.0, 0.0},
        {0.0, 1.0, 0.0},
        {0.0, 0.0, 1.0},
        {0.4, -0.3, 0.8660254037844386},
    };
    // Deterministic pseudo-random pixel directions supplement center, edge,
    // corner, radial, and tangential directions without a test dependency.
    std::uint32_t seed = 0x7f4a7c15u;
    for (int sample = 0; sample < 128; ++sample) {
        const auto next = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<double>(seed) / 4294967295.0;
        };
        directions.push_back({2.0 * next() - 1.0, 2.0 * next() - 1.0, 2.0 * next() - 1.0});
    }

    double maximum_null_norm = 0.0;
    double maximum_energy_error = 0.0;
    for (const auto& observer : observers) {
        const double expected_energy = std::sqrt(metric_factor(observer.r));
        for (const auto& direction : directions) {
            const PhotonState state = initialize_static_observer_photon(observer, direction);
            maximum_null_norm = std::max(maximum_null_norm, std::abs(null_norm(state)));
            maximum_energy_error = std::max(
                maximum_energy_error, std::abs(photon_invariants(state).energy - expected_energy));
        }
    }
    constexpr double tolerance = 2.0e-13;
    const bool passed = maximum_null_norm <= tolerance && maximum_energy_error <= tolerance;
    return {"Static-observer tetrad null constraint", passed,
            "max |g(k,k)|=" + format_value(maximum_null_norm) +
                ", max |E-E_inf|=" + format_value(maximum_energy_error) +
                ", 133 local directions x 5 observers, abs_tol=2e-13"};
}

TestResult test_static_observer_limit() {
    auto configuration = blackhole::physics::make_default_simulation_configuration();
    configuration.camera.radius_in_schwarzschild_radii = 1.0;
    const bool passed = !blackhole::physics::is_valid(configuration);
    return {"Static-observer horizon limit", passed,
            "r_observer=1 r_s is rejected; static observers require r > r_s"};
}

TestResult test_y_up_coordinate_round_trip() {
    using namespace blackhole::physics;
    const SphericalPosition original{7.25, 1.2, -2.4};
    const SphericalPosition round_trip = cartesian_to_spherical(spherical_to_cartesian(original));
    const bool passed = close_to(round_trip.r, original.r, 1.0e-14) &&
                        close_to(round_trip.theta, original.theta, 1.0e-14) &&
                        close_to(round_trip.phi, original.phi, 1.0e-14);
    return {"Y-up spherical coordinate round trip", passed,
            "dr=" + format_value(round_trip.r - original.r) +
                ", dtheta=" + format_value(round_trip.theta - original.theta) +
                ", dphi=" + format_value(round_trip.phi - original.phi) + ", rel_tol=1e-14"};
}

TestResult test_geodesic_invariants() {
    using namespace blackhole::physics;
    PhotonState state = initialize_static_observer_photon({8.0, 1.1, 0.7}, {0.55, 0.22, 0.81});
    const PhotonInvariants initial = photon_invariants(state);
    double maximum_energy_drift = 0.0;
    double maximum_angular_momentum_drift = 0.0;
    double maximum_plane_drift = 0.0;
    double maximum_null_norm = 0.0;

    for (int step = 0; step < 5000; ++step) {
        state = rk4_step(state, 0.001);
        const PhotonInvariants current = photon_invariants(state);
        maximum_energy_drift =
            std::max(maximum_energy_drift, std::abs(current.energy - initial.energy));
        maximum_angular_momentum_drift =
            std::max(maximum_angular_momentum_drift,
                     std::abs(current.angular_momentum_squared - initial.angular_momentum_squared));
        maximum_plane_drift =
            std::max(maximum_plane_drift, vector_distance(current.angular_momentum_vector,
                                                          initial.angular_momentum_vector));
        maximum_null_norm = std::max(maximum_null_norm, std::abs(null_norm(state)));
    }

    constexpr double tolerance = 2.0e-10;
    const bool passed = maximum_energy_drift < tolerance &&
                        maximum_angular_momentum_drift < tolerance &&
                        maximum_plane_drift < tolerance && maximum_null_norm < tolerance;
    return {"Geodesic invariants and orbital plane", passed,
            "max dE=" + format_value(maximum_energy_drift) +
                ", max dL2=" + format_value(maximum_angular_momentum_drift) +
                ", max |dL_vector|=" + format_value(maximum_plane_drift) +
                ", max |g(k,k)|=" + format_value(maximum_null_norm) + ", abs_tol=2e-10"};
}

TestResult test_radial_equatorial_and_off_equatorial_rays() {
    using namespace blackhole::physics;
    PhotonState radial =
        initialize_static_observer_photon({4.0, 1.5707963267948966, 0.0}, {1.0, 0.0, 0.0});
    PhotonState equatorial = initialize_static_observer_photon({7.0, 1.5707963267948966, 0.4},
                                                               {0.45, 0.0, 0.8930285549745876});
    PhotonState off_equatorial =
        initialize_static_observer_photon({7.0, 0.93, -0.4}, {0.35, -0.38, 0.8551023330720614});
    const PhotonInvariants off_equatorial_initial = photon_invariants(off_equatorial);
    double max_radial_l2 = 0.0;
    double max_equatorial_deviation = 0.0;
    double max_off_equatorial_plane_drift = 0.0;

    for (int step = 0; step < 3000; ++step) {
        radial = rk4_step(radial, 0.001);
        equatorial = rk4_step(equatorial, 0.001);
        off_equatorial = rk4_step(off_equatorial, 0.001);
        max_radial_l2 =
            std::max(max_radial_l2, std::abs(photon_invariants(radial).angular_momentum_squared));
        max_equatorial_deviation =
            std::max(max_equatorial_deviation, std::abs(equatorial.theta - 1.5707963267948966));
        max_off_equatorial_plane_drift =
            std::max(max_off_equatorial_plane_drift,
                     vector_distance(photon_invariants(off_equatorial).angular_momentum_vector,
                                     off_equatorial_initial.angular_momentum_vector));
    }
    constexpr double tolerance = 2.0e-10;
    const bool passed = max_radial_l2 < tolerance && max_equatorial_deviation < tolerance &&
                        max_off_equatorial_plane_drift < tolerance;
    return {"Radial, equatorial, and off-equatorial rays", passed,
            "max radial L2=" + format_value(max_radial_l2) +
                ", max equatorial |theta-pi/2|=" + format_value(max_equatorial_deviation) +
                ", max off-plane drift=" + format_value(max_off_equatorial_plane_drift) +
                ", abs_tol=2e-10"};
}

TestResult test_photon_sphere_and_critical_impact_parameter() {
    using namespace blackhole::physics;
    constexpr double pi = 3.141592653589793238462643383279502884;
    constexpr double photon_sphere_radius = 1.5;
    constexpr double critical_impact_parameter = 2.5980762113533159403;
    PhotonState circular =
        initialize_static_observer_photon({photon_sphere_radius, pi * 0.5, 0.0}, {0.0, 0.0, 1.0});
    double maximum_radius_error = 0.0;
    for (int step = 0; step < 94248; ++step) { // ten affine-parameter periods
        circular = rk4_step(circular, 0.001);
        maximum_radius_error =
            std::max(maximum_radius_error, std::abs(circular.r - photon_sphere_radius));
    }

    const double measured_impact_parameter =
        std::sqrt(photon_invariants(circular).angular_momentum_squared) /
        photon_invariants(circular).energy;
    constexpr double radius_tolerance = 2.0e-10;
    constexpr double impact_tolerance = 2.0e-10;
    const bool passed =
        maximum_radius_error < radius_tolerance &&
        std::abs(measured_impact_parameter - critical_impact_parameter) < impact_tolerance;
    return {"Photon sphere and critical impact parameter", passed,
            "max |r-1.5 r_s|=" + format_value(maximum_radius_error) +
                ", b=" + format_value(measured_impact_parameter) +
                ", b_crit=" + format_value(critical_impact_parameter) + ", abs_tol=2e-10"};
}

TestResult test_subcritical_capture_and_supercritical_escape() {
    using namespace blackhole::physics;
    constexpr double observer_radius = 15.0;
    const double local_to_coordinate_scale = std::sqrt(metric_factor(observer_radius));
    const auto make_inward_ray = [local_to_coordinate_scale,
                                  observer_radius](double impact_parameter) {
        const double n_phi = impact_parameter * local_to_coordinate_scale / observer_radius;
        return initialize_static_observer_photon({observer_radius, 1.5707963267948966, 0.0},
                                                 {-std::sqrt(1.0 - n_phi * n_phi), 0.0, n_phi});
    };
    const TraceResult subcritical =
        trace_fixed_step(make_inward_ray(2.4), 0.001, 60000, 1.02, 30.0);
    const TraceResult supercritical =
        trace_fixed_step(make_inward_ray(3.0), 0.001, 60000, 1.02, 30.0);
    const bool passed =
        subcritical == TraceResult::Captured && supercritical == TraceResult::Escaped;
    return {"Subcritical capture and supercritical escape", passed,
            "b=2.4 -> " +
                std::string(subcritical == TraceResult::Captured ? "captured" : "not captured") +
                "; b=3.0 -> " +
                (supercritical == TraceResult::Escaped ? "escaped" : "not escaped") +
                "; b_crit=2.59807621135 r_s"};
}

TestResult test_dimensionless_mass_scale_invariance() {
    using namespace blackhole::physics;
    const std::vector<double> masses{4.0e30, 8.54e36, 2.0e40};
    constexpr double camera_radius_in_radii = 4.9976;
    constexpr double impact_parameter = 3.0;
    bool identical_classification = true;
    double maximum_radius_ratio_error = 0.0;
    double reference_impact_parameter = 0.0;

    for (const double mass : masses) {
        const double radius_si = schwarzschild_radius(mass);
        const double camera_distance_si = camera_radius_in_radii * radius_si;
        maximum_radius_ratio_error =
            std::max(maximum_radius_ratio_error,
                     std::abs(camera_distance_si / radius_si - camera_radius_in_radii));

        const double f = metric_factor(15.0);
        const double n_phi = impact_parameter * std::sqrt(f) / 15.0;
        const PhotonState ray = initialize_static_observer_photon(
            {15.0, 1.5707963267948966, 0.0}, {-std::sqrt(1.0 - n_phi * n_phi), 0.0, n_phi});
        const PhotonInvariants invariants = photon_invariants(ray);
        const double measured_impact_parameter =
            std::sqrt(invariants.angular_momentum_squared) / invariants.energy;
        if (reference_impact_parameter == 0.0) {
            reference_impact_parameter = measured_impact_parameter;
        }
        identical_classification =
            identical_classification &&
            trace_fixed_step(ray, 0.001, 60000, 1.02, 30.0) == TraceResult::Escaped &&
            close_to(measured_impact_parameter, reference_impact_parameter, 1.0e-13);
    }

    const bool passed = identical_classification && maximum_radius_ratio_error < 1.0e-14;
    return {"Dimensionless vacuum mass-scale invariance", passed,
            "masses=4e30,8.54e36,2e40 kg; max |r/r_s-4.9976|=" +
                format_value(maximum_radius_ratio_error) +
                "; b=3.0 r_s classification=escaped for all"};
}

TestResult test_adaptive_classification_convergence() {
    using namespace blackhole::physics;
    const auto make_inward_ray = [](double impact_parameter) {
        constexpr double observer_radius = 15.0;
        const double n_phi =
            impact_parameter * std::sqrt(metric_factor(observer_radius)) / observer_radius;
        return initialize_static_observer_photon({observer_radius, 1.5707963267948966, 0.0},
                                                 {-std::sqrt(1.0 - n_phi * n_phi), 0.0, n_phi});
    };
    AdaptiveIntegrationOptions loose;
    loose.absolute_tolerance = 1.0e-7;
    loose.relative_tolerance = 1.0e-6;
    loose.minimum_step = 1.0e-5;
    loose.maximum_step = 0.05;
    loose.maximum_steps = 48000;
    loose.capture_radius = 1.001;
    loose.escape_radius = 30.0;
    AdaptiveIntegrationOptions tight = loose;
    tight.absolute_tolerance = 1.0e-9;
    tight.relative_tolerance = 1.0e-8;
    tight.minimum_step = 1.0e-6;

    const VacuumTraceResult captured_loose = trace_vacuum_adaptive(make_inward_ray(2.4), loose);
    const VacuumTraceResult captured_tight = trace_vacuum_adaptive(make_inward_ray(2.4), tight);
    const VacuumTraceResult escaped_loose = trace_vacuum_adaptive(make_inward_ray(3.0), loose);
    const VacuumTraceResult escaped_tight = trace_vacuum_adaptive(make_inward_ray(3.0), tight);
    const VacuumTraceResult near_loose = trace_vacuum_adaptive(make_inward_ray(2.61), loose);
    const VacuumTraceResult near_tight = trace_vacuum_adaptive(make_inward_ray(2.61), tight);

    const bool statuses_match =
        captured_loose.status == RayStatus::Captured &&
        captured_tight.status == RayStatus::Captured &&
        escaped_loose.status == RayStatus::Escaped && escaped_tight.status == RayStatus::Escaped &&
        near_loose.status == near_tight.status && near_tight.status != RayStatus::Unresolved &&
        near_tight.status != RayStatus::Invalid;
    const double closest_approach_difference =
        std::abs(near_loose.closest_radius - near_tight.closest_radius);
    const bool passed = statuses_match && closest_approach_difference < 1.0e-3;
    return {"Adaptive classification and closest-approach convergence", passed,
            "capture=" + std::string(status_name(captured_tight.status)) +
                " at r=" + format_value(captured_tight.state.r) +
                ", escape=" + status_name(escaped_tight.status) +
                ", near-critical=" + status_name(near_tight.status) +
                ", |r_min(loose)-r_min(tight)|=" + format_value(closest_approach_difference) +
                ", tol=1e-3"};
}

TestResult test_weak_field_deflection() {
    using namespace blackhole::physics;
    constexpr double observer_radius = 5000.0;
    constexpr double impact_parameter = 100.0;
    const double n_phi =
        impact_parameter * std::sqrt(metric_factor(observer_radius)) / observer_radius;
    const PhotonState initial = initialize_static_observer_photon(
        {observer_radius, 1.5707963267948966, 0.0}, {-std::sqrt(1.0 - n_phi * n_phi), 0.0, n_phi});
    AdaptiveIntegrationOptions options;
    options.absolute_tolerance = 1.0e-10;
    options.relative_tolerance = 1.0e-9;
    options.minimum_step = 1.0e-6;
    options.maximum_step = 2.0;
    options.maximum_steps = 30000;
    options.capture_radius = 1.001;
    options.escape_radius = observer_radius;
    const VacuumTraceResult traced = trace_vacuum_adaptive(initial, options);
    const double measured =
        vector_angle(cartesian_velocity(initial), cartesian_velocity(traced.state));
    const double leading_order = 2.0 / impact_parameter;
    const double relative_error = std::abs(measured - leading_order) / leading_order;
    const bool passed = traced.status == RayStatus::Escaped && relative_error < 0.025;
    return {"Weak-field deflection", passed,
            "b=100 r_s, measured=" + format_value(measured) +
                " rad, leading 2r_s/b=" + format_value(leading_order) +
                " rad, rel_error=" + format_value(relative_error) + ", tol=2.5%"};
}

TestResult test_default_camera_shadow_half_angle() {
    constexpr double observer_radius = 4.9976;
    constexpr double critical_impact_parameter = 2.5980762113533159403;
    constexpr double radians_to_degrees = 57.2957795130823208768;
    const double measured_degrees =
        std::asin(critical_impact_parameter *
                  std::sqrt(blackhole::physics::metric_factor(observer_radius)) / observer_radius) *
        radians_to_degrees;
    constexpr double expected_degrees = 27.707;
    constexpr double tolerance_degrees = 0.01;
    const bool passed = std::abs(measured_degrees - expected_degrees) < tolerance_degrees;
    return {"Default-camera analytical shadow half-angle", passed,
            "r_observer=4.9976 r_s, alpha=" + format_value(measured_degrees) +
                " deg, expected~=27.707 deg, abs_tol=0.01 deg"};
}

TestResult test_rk4_step_size_order() {
    using namespace blackhole::physics;
    const PhotonState initial =
        initialize_static_observer_photon({4.0, 1.12, 0.31}, {-0.25, 0.31, 0.9178235124467012});
    const auto integrate = [&initial](double step) {
        PhotonState state = initial;
        const int count = static_cast<int>(std::lround(1.6 / step));
        for (int index = 0; index < count; ++index)
            state = rk4_step(state, step);
        return state;
    };
    const PhotonState coarse = integrate(0.04);
    const PhotonState medium = integrate(0.02);
    const PhotonState reference = integrate(0.00125);
    const auto error = [](const PhotonState& a, const PhotonState& b) {
        const double dr = a.r - b.r;
        const double dtheta = a.theta - b.theta;
        const double dphi = a.phi - b.phi;
        const double dvr = a.dr - b.dr;
        const double dvtheta = a.dtheta - b.dtheta;
        const double dvphi = a.dphi - b.dphi;
        return std::sqrt(dr * dr + dtheta * dtheta + dphi * dphi + dvr * dvr + dvtheta * dvtheta +
                         dvphi * dvphi);
    };
    const double coarse_error = error(coarse, reference);
    const double medium_error = error(medium, reference);
    const double reduction = coarse_error / medium_error;
    const bool passed = reduction > 10.0 && reduction < 24.0;
    return {"RK4 step-size convergence order", passed,
            "error(h=.04)=" + format_value(coarse_error) +
                ", error(h=.02)=" + format_value(medium_error) +
                ", reduction=" + format_value(reduction) + ", expected approximately 16"};
}

TestResult test_thin_disk_thermodynamics_and_redshift() {
    using namespace blackhole::physics;
    const auto configuration = make_default_simulation_configuration();
    const double mass = configuration.black_hole.mass_kg;
    const double rs = configuration.black_hole.schwarzschild_radius_m;
    const double inner = 3.0 * rs;
    const double flux = thin_disk_flux(mass, 1.0e15, 4.5 * rs, inner);
    const double hotter_flux = thin_disk_flux(mass, 1.0e16, 4.5 * rs, inner);
    const double temperature = effective_temperature_from_flux(flux);
    const double hotter_temperature = effective_temperature_from_flux(hotter_flux);
    const double expected_temperature_ratio = std::pow(10.0, 0.25);

    constexpr double energy = 0.9;
    constexpr double angular_momentum = 2.1;
    const double prograde =
        circular_emitter_frequency_shift(4.9976, 4.5, energy, angular_momentum, 1.0);
    const double retrograde =
        circular_emitter_frequency_shift(4.9976, 4.5, energy, angular_momentum, -1.0);
    const double reversed_prograde =
        circular_emitter_frequency_shift(4.9976, 4.5, energy, -angular_momentum, -1.0);

    auto invalid_disk = configuration;
    invalid_disk.disk.inner_radius_in_schwarzschild_radii = 2.5;
    const bool passed =
        flux > 0.0 && temperature > 0.0 &&
        close_to(hotter_temperature / temperature, expected_temperature_ratio, 1.0e-12) &&
        prograde > retrograde && close_to(prograde, reversed_prograde, 1.0e-13) &&
        close_to(bolometric_intensity_factor(prograde), std::pow(prograde, 4.0), 1.0e-14) &&
        !is_valid(invalid_disk);
    return {"Thin-disk thermodynamics and four-vector redshift", passed,
            "T(10x Mdot)/T=" + format_value(hotter_temperature / temperature) +
                ", expected 10^(1/4)=" + format_value(expected_temperature_ratio) +
                ", g_prograde=" + format_value(prograde) +
                ", g_retrograde=" + format_value(retrograde) + ", r_in<3r_s rejected"};
}

} // namespace

int main(int argc, char** argv) {
    std::string report_path;
    if (argc == 3 && std::string(argv[1]) == "--report") {
        report_path = argv[2];
    } else if (argc != 1) {
        std::cerr << "Usage: SchwarzschildValidation [--report <path>]\n";
        return 2;
    }

    const std::vector<TestResult> results{
        test_schwarzschild_radius(),
        test_configuration(),
        test_static_tetrad_null_constraint(),
        test_static_observer_limit(),
        test_y_up_coordinate_round_trip(),
        test_geodesic_invariants(),
        test_radial_equatorial_and_off_equatorial_rays(),
        test_photon_sphere_and_critical_impact_parameter(),
        test_subcritical_capture_and_supercritical_escape(),
        test_dimensionless_mass_scale_invariance(),
        test_adaptive_classification_convergence(),
        test_weak_field_deflection(),
        test_default_camera_shadow_half_angle(),
        test_rk4_step_size_order(),
        test_thin_disk_thermodynamics_and_redshift(),
    };

    std::ostringstream report;
    report << "Schwarzschild CPU baseline validation report\n"
           << "Reference implementation: physics/schwarzschild.cpp\n"
           << "Units: r_s = 1 for geodesics; SI used only for r_s configuration.\n\n";
    bool passed = true;
    for (const auto& result : results) {
        report << (result.passed ? "PASS" : "FAIL") << "  " << result.name << "\n      "
               << result.detail << "\n";
        passed = passed && result.passed;
    }
    report << "\nOverall: " << (passed ? "PASS" : "FAIL") << "\n";

    std::cout << report.str();
    if (!report_path.empty()) {
        std::ofstream output(report_path, std::ios::trunc);
        if (!output) {
            std::cerr << "Unable to write validation report: " << report_path << '\n';
            return 2;
        }
        output << report.str();
    }
    return passed ? 0 : 1;
}
