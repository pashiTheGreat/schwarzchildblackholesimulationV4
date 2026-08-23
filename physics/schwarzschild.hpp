#pragma once

#include <cmath>

namespace blackhole::physics {

constexpr double gravitational_constant_si = 6.67430e-11;
constexpr double speed_of_light_si = 299792458.0;

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct SphericalPosition {
    double r = 1.0;
    double theta = 0.0;
    double phi = 0.0;
};

// Coordinates use Y as the polar axis:
// x = r sin(theta) cos(phi), y = r cos(theta), z = r sin(theta) sin(phi).
// theta is in [0, pi], phi is measured from +X towards +Z.
Vec3 spherical_to_cartesian(const SphericalPosition& position);
SphericalPosition cartesian_to_spherical(const Vec3& position);

double schwarzschild_radius(double mass_kg);
double metric_factor(double r);

// Contravariant coordinate components in dimensionless Schwarzschild units
// (r_s = 1).  The affine parameter is lambda / r_s.
struct PhotonState {
    double t = 0.0;
    double r = 1.0;
    double theta = 0.0;
    double phi = 0.0;
    double dt = 0.0;
    double dr = 0.0;
    double dtheta = 0.0;
    double dphi = 0.0;
};

struct PhotonInvariants {
    double energy = 0.0;
    double angular_momentum_squared = 0.0;
    double axial_angular_momentum = 0.0;
    Vec3 angular_momentum_vector{};
};

enum class RayStatus {
    Captured,
    Escaped,
    Unresolved,
    Invalid,
};

struct AdaptiveIntegrationOptions {
    double absolute_tolerance = 1.0e-8;
    double relative_tolerance = 1.0e-7;
    double minimum_step = 1.0e-5;
    double maximum_step = 0.05;
    int maximum_steps = 24000;
    double capture_radius = 1.0005;
    double escape_radius = 80.0;
};

struct VacuumTraceResult {
    RayStatus status = RayStatus::Invalid;
    PhotonState state{};
    int accepted_steps = 0;
    double closest_radius = 0.0;
};

// A local static-observer direction has components (n_r, n_theta, n_phi) in
// the orthonormal spherical basis.  It is normalized internally.  At the
// polar axis theta directions are undefined; requests there are rejected by
// throwing std::invalid_argument rather than silently selecting an axis.
PhotonState initialize_static_observer_photon(const SphericalPosition& observer,
                                              const Vec3& local_direction,
                                              double local_energy = 1.0);

double null_norm(const PhotonState& state);
PhotonInvariants photon_invariants(const PhotonState& state);

// Exact coordinate-basis geodesic right-hand side for the Schwarzschild
// metric.  The returned state contains dx^mu/dlambda in (t,r,theta,phi) and
// dk^mu/dlambda in (dt,dr,dtheta,dphi).
PhotonState geodesic_rhs(const PhotonState& state);
PhotonState rk4_step(const PhotonState& state, double step);

// Error-controlled RK4 step-doubling used as the CPU reference for GPU
// classification.  The accepted solution uses two half-steps; a ray that
// cannot meet the requested tolerance before its budget is exhausted is
// Unresolved, never Captured.
VacuumTraceResult trace_vacuum_adaptive(const PhotonState& initial_state,
                                        const AdaptiveIntegrationOptions& options = {});

bool is_finite(const PhotonState& state);

} // namespace blackhole::physics
