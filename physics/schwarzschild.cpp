#include "physics/schwarzschild.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace blackhole::physics {
namespace {

constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double polar_epsilon = 1.0e-12;

double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

double length(const Vec3& value) { return std::sqrt(dot(value, value)); }

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

PhotonState add_scaled(const PhotonState& state, const PhotonState& delta, double scale) {
    return {
        state.t + delta.t * scale,           state.r + delta.r * scale,
        state.theta + delta.theta * scale,   state.phi + delta.phi * scale,
        state.dt + delta.dt * scale,         state.dr + delta.dr * scale,
        state.dtheta + delta.dtheta * scale, state.dphi + delta.dphi * scale,
    };
}

PhotonState weighted_sum(const PhotonState& state, const PhotonState& k1, const PhotonState& k2,
                         const PhotonState& k3, const PhotonState& k4, double step) {
    constexpr double one_sixth = 1.0 / 6.0;
    return {
        state.t + step * one_sixth * (k1.t + 2.0 * k2.t + 2.0 * k3.t + k4.t),
        state.r + step * one_sixth * (k1.r + 2.0 * k2.r + 2.0 * k3.r + k4.r),
        state.theta + step * one_sixth * (k1.theta + 2.0 * k2.theta + 2.0 * k3.theta + k4.theta),
        state.phi + step * one_sixth * (k1.phi + 2.0 * k2.phi + 2.0 * k3.phi + k4.phi),
        state.dt + step * one_sixth * (k1.dt + 2.0 * k2.dt + 2.0 * k3.dt + k4.dt),
        state.dr + step * one_sixth * (k1.dr + 2.0 * k2.dr + 2.0 * k3.dr + k4.dr),
        state.dtheta +
            step * one_sixth * (k1.dtheta + 2.0 * k2.dtheta + 2.0 * k3.dtheta + k4.dtheta),
        state.dphi + step * one_sixth * (k1.dphi + 2.0 * k2.dphi + 2.0 * k3.dphi + k4.dphi),
    };
}

double component_error(double candidate, double reference,
                       const AdaptiveIntegrationOptions& options) {
    const double scale =
        options.absolute_tolerance +
        options.relative_tolerance * std::max(std::abs(candidate), std::abs(reference));
    return std::abs(candidate - reference) / scale;
}

double normalized_step_error(const PhotonState& whole, const PhotonState& half,
                             const AdaptiveIntegrationOptions& options) {
    const std::array<double, 8> whole_components{
        whole.t, whole.r, whole.theta, whole.phi, whole.dt, whole.dr, whole.dtheta, whole.dphi,
    };
    const std::array<double, 8> half_components{
        half.t, half.r, half.theta, half.phi, half.dt, half.dr, half.dtheta, half.dphi,
    };
    double maximum_error = 0.0;
    for (std::size_t index = 0; index < whole_components.size(); ++index) {
        maximum_error = std::max(maximum_error, component_error(whole_components[index],
                                                                half_components[index], options));
    }
    // RK4 step-doubling differs by approximately 15 times the fine-step error.
    return maximum_error / 15.0;
}

} // namespace

Vec3 spherical_to_cartesian(const SphericalPosition& position) {
    const double sin_theta = std::sin(position.theta);
    return {
        position.r * sin_theta * std::cos(position.phi),
        position.r * std::cos(position.theta),
        position.r * sin_theta * std::sin(position.phi),
    };
}

SphericalPosition cartesian_to_spherical(const Vec3& position) {
    const double radius = length(position);
    if (!(radius > 0.0) || !std::isfinite(radius)) {
        throw std::invalid_argument("cannot convert the Cartesian origin to spherical coordinates");
    }

    return {
        radius,
        std::acos(std::clamp(position.y / radius, -1.0, 1.0)),
        std::atan2(position.z, position.x),
    };
}

double schwarzschild_radius(double mass_kg) {
    if (!(mass_kg > 0.0) || !std::isfinite(mass_kg)) {
        throw std::invalid_argument("black-hole mass must be finite and positive");
    }
    return 2.0 * gravitational_constant_si * mass_kg / (speed_of_light_si * speed_of_light_si);
}

double metric_factor(double r) {
    if (!(r > 1.0) || !std::isfinite(r)) {
        throw std::invalid_argument(
            "Schwarzschild coordinates require r > 1 in static-observer calculations");
    }
    return 1.0 - 1.0 / r;
}

PhotonState initialize_static_observer_photon(const SphericalPosition& observer,
                                              const Vec3& local_direction, double local_energy) {
    if (!(observer.r > 1.0) || !std::isfinite(observer.r) || !(local_energy > 0.0) ||
        !std::isfinite(local_energy)) {
        throw std::invalid_argument(
            "static observer must be outside the horizon with positive local energy");
    }

    const double sin_theta = std::sin(observer.theta);
    if (std::abs(sin_theta) < polar_epsilon) {
        throw std::invalid_argument("static-observer tetrad is singular at a spherical pole");
    }
    const double direction_length = length(local_direction);
    if (!(direction_length > 0.0) || !std::isfinite(direction_length)) {
        throw std::invalid_argument("local photon direction must be finite and non-zero");
    }

    const Vec3 n{local_direction.x / direction_length, local_direction.y / direction_length,
                 local_direction.z / direction_length};
    const double f = metric_factor(observer.r);
    const double sqrt_f = std::sqrt(f);
    return {
        0.0,
        observer.r,
        observer.theta,
        observer.phi,
        local_energy / sqrt_f,
        local_energy * sqrt_f * n.x,
        local_energy * n.y / observer.r,
        local_energy * n.z / (observer.r * sin_theta),
    };
}

double null_norm(const PhotonState& state) {
    const double f = metric_factor(state.r);
    const double sin_theta = std::sin(state.theta);
    return -f * state.dt * state.dt + state.dr * state.dr / f +
           state.r * state.r *
               (state.dtheta * state.dtheta + sin_theta * sin_theta * state.dphi * state.dphi);
}

PhotonInvariants photon_invariants(const PhotonState& state) {
    const double sin_theta = std::sin(state.theta);
    const double angular_speed_squared =
        state.dtheta * state.dtheta + sin_theta * sin_theta * state.dphi * state.dphi;

    const Vec3 position = spherical_to_cartesian({state.r, state.theta, state.phi});
    const Vec3 velocity{
        state.dr * sin_theta * std::cos(state.phi) +
            state.r * std::cos(state.theta) * state.dtheta * std::cos(state.phi) -
            state.r * sin_theta * std::sin(state.phi) * state.dphi,
        state.dr * std::cos(state.theta) - state.r * sin_theta * state.dtheta,
        state.dr * sin_theta * std::sin(state.phi) +
            state.r * std::cos(state.theta) * state.dtheta * std::sin(state.phi) +
            state.r * sin_theta * std::cos(state.phi) * state.dphi,
    };

    return {
        metric_factor(state.r) * state.dt,
        state.r * state.r * state.r * state.r * angular_speed_squared,
        state.r * state.r * sin_theta * sin_theta * state.dphi,
        cross(position, velocity),
    };
}

PhotonState geodesic_rhs(const PhotonState& state) {
    const double r = state.r;
    const double f = metric_factor(r);
    const double sin_theta = std::sin(state.theta);
    if (std::abs(sin_theta) < polar_epsilon) {
        throw std::invalid_argument("geodesic coordinate chart is singular at a spherical pole");
    }
    const double cos_theta = std::cos(state.theta);
    const double angular_speed_squared =
        state.dtheta * state.dtheta + sin_theta * sin_theta * state.dphi * state.dphi;

    return {
        state.dt,
        state.dr,
        state.dtheta,
        state.dphi,
        -state.dt * state.dr / (r * r * f),
        -(f / (2.0 * r * r)) * state.dt * state.dt + state.dr * state.dr / (2.0 * r * r * f) +
            r * f * angular_speed_squared,
        -2.0 * state.dr * state.dtheta / r + sin_theta * cos_theta * state.dphi * state.dphi,
        -2.0 * state.dr * state.dphi / r - 2.0 * cos_theta * state.dtheta * state.dphi / sin_theta,
    };
}

PhotonState rk4_step(const PhotonState& state, double step) {
    if (!(step > 0.0) || !std::isfinite(step)) {
        throw std::invalid_argument("RK4 step must be finite and positive");
    }
    const PhotonState k1 = geodesic_rhs(state);
    const PhotonState k2 = geodesic_rhs(add_scaled(state, k1, step * 0.5));
    const PhotonState k3 = geodesic_rhs(add_scaled(state, k2, step * 0.5));
    const PhotonState k4 = geodesic_rhs(add_scaled(state, k3, step));
    return weighted_sum(state, k1, k2, k3, k4, step);
}

VacuumTraceResult trace_vacuum_adaptive(const PhotonState& initial_state,
                                        const AdaptiveIntegrationOptions& options) {
    VacuumTraceResult result;
    result.state = initial_state;
    result.closest_radius = initial_state.r;
    if (!is_finite(initial_state) || options.absolute_tolerance <= 0.0 ||
        options.relative_tolerance <= 0.0 || options.minimum_step <= 0.0 ||
        options.maximum_step < options.minimum_step || options.maximum_steps <= 0 ||
        options.capture_radius <= 1.0 || options.escape_radius <= options.capture_radius) {
        result.status = RayStatus::Invalid;
        return result;
    }

    double proposed_step = options.maximum_step;
    for (int iteration = 0; iteration < options.maximum_steps; ++iteration) {
        if (result.state.r <= options.capture_radius) {
            result.status = RayStatus::Captured;
            return result;
        }
        if (result.state.r >= options.escape_radius && result.state.dr > 0.0) {
            result.status = RayStatus::Escaped;
            return result;
        }

        const double radial_speed_bound = std::max(std::abs(result.state.dr), 0.25);
        // The capture surface is an event surface, not a numerical failure.
        // Once an inward ray lies within ten minimum resolved radial advances,
        // its crossing is bracketed at configured accuracy and can be
        // classified without forcing a sub-minimum RK step.
        if (result.state.dr < 0.0 && result.state.r - options.capture_radius <=
                                         10.0 * options.minimum_step * radial_speed_bound) {
            result.state.r = options.capture_radius;
            result.closest_radius = std::min(result.closest_radius, result.state.r);
            result.status = RayStatus::Captured;
            return result;
        }
        const double horizon_limited_step =
            0.2 * (result.state.r - options.capture_radius) / radial_speed_bound;
        const double curvature_limited_step =
            result.state.r < 3.0 ? options.maximum_step * 0.25 : options.maximum_step;
        double step = std::min(
            {proposed_step, horizon_limited_step, curvature_limited_step, options.maximum_step});
        if (!std::isfinite(step) || step < options.minimum_step) {
            result.status = RayStatus::Unresolved;
            return result;
        }

        bool accepted = false;
        for (int attempt = 0; attempt < 10; ++attempt) {
            try {
                const PhotonState whole = rk4_step(result.state, step);
                const PhotonState half = rk4_step(result.state, step * 0.5);
                const PhotonState fine = rk4_step(half, step * 0.5);
                if (!is_finite(whole) || !is_finite(fine) || fine.r <= 1.0) {
                    step *= 0.25;
                    continue;
                }
                const double error = normalized_step_error(whole, fine, options);
                if (error <= 1.0) {
                    result.state = fine;
                    result.closest_radius = std::min(result.closest_radius, fine.r);
                    ++result.accepted_steps;
                    const double factor =
                        error <= 1.0e-16 ? 2.0
                                         : std::clamp(0.9 * std::pow(1.0 / error, 0.2), 0.2, 2.0);
                    proposed_step =
                        std::clamp(step * factor, options.minimum_step, options.maximum_step);
                    accepted = true;
                    break;
                }
                step *= std::clamp(0.9 * std::pow(1.0 / error, 0.2), 0.1, 0.5);
            } catch (const std::exception&) {
                step *= 0.25;
            }
            if (step < options.minimum_step) {
                result.status = RayStatus::Unresolved;
                return result;
            }
        }
        if (!accepted) {
            result.status = RayStatus::Unresolved;
            return result;
        }
    }
    result.status = RayStatus::Unresolved;
    return result;
}

bool is_finite(const PhotonState& state) {
    constexpr std::array<double PhotonState::*, 8> components{
        &PhotonState::t,  &PhotonState::r,  &PhotonState::theta,  &PhotonState::phi,
        &PhotonState::dt, &PhotonState::dr, &PhotonState::dtheta, &PhotonState::dphi,
    };
    for (const auto component : components) {
        if (!std::isfinite(state.*component)) {
            return false;
        }
    }
    return true;
}

} // namespace blackhole::physics
