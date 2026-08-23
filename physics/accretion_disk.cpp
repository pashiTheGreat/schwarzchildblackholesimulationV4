#include "physics/accretion_disk.hpp"

#include "physics/schwarzschild.hpp"

#include <cmath>
#include <stdexcept>

namespace blackhole::physics {
namespace {
constexpr double pi = 3.141592653589793238462643383279502884;
}

double thin_disk_flux(double mass_kg, double accretion_rate_kg_per_s, double radius_m,
                      double inner_radius_m) {
    if (!(mass_kg > 0.0) || !(accretion_rate_kg_per_s >= 0.0) || !(radius_m > 0.0) ||
        !(inner_radius_m > 0.0)) {
        throw std::invalid_argument("thin-disk inputs must be physically non-negative");
    }
    if (radius_m <= inner_radius_m || accretion_rate_kg_per_s == 0.0)
        return 0.0;
    return 3.0 * gravitational_constant_si * mass_kg * accretion_rate_kg_per_s /
           (8.0 * pi * radius_m * radius_m * radius_m) *
           (1.0 - std::sqrt(inner_radius_m / radius_m));
}

double effective_temperature_from_flux(double flux_w_per_square_metre) {
    if (!(flux_w_per_square_metre >= 0.0) || !std::isfinite(flux_w_per_square_metre)) {
        throw std::invalid_argument("radiative flux must be finite and non-negative");
    }
    return std::pow(flux_w_per_square_metre / stefan_boltzmann_si, 0.25);
}

double circular_emitter_frequency_shift(double observer_radius, double emitter_radius,
                                        double photon_energy, double photon_axis_angular_momentum,
                                        double rotation_sign) {
    if (!(observer_radius > 1.0) || !(emitter_radius >= 3.0) || !(photon_energy > 0.0) ||
        std::abs(rotation_sign) != 1.0) {
        throw std::invalid_argument("invalid static-observer/circular-emitter configuration");
    }
    const double observer_ut = 1.0 / std::sqrt(1.0 - 1.0 / observer_radius);
    const double emitter_ut = 1.0 / std::sqrt(1.0 - 1.5 / emitter_radius);
    const double omega = std::sqrt(1.0 / (2.0 * emitter_radius * emitter_radius * emitter_radius));
    const double observer_frequency = photon_energy * observer_ut;
    const double emitter_frequency =
        emitter_ut * (photon_energy - rotation_sign * omega * photon_axis_angular_momentum);
    if (!(emitter_frequency > 0.0)) {
        throw std::invalid_argument("photon has non-positive emitter-frame frequency");
    }
    return observer_frequency / emitter_frequency;
}

double bolometric_intensity_factor(double frequency_shift) {
    if (!(frequency_shift > 0.0) || !std::isfinite(frequency_shift)) {
        throw std::invalid_argument("frequency shift must be finite and positive");
    }
    return std::pow(frequency_shift, 4.0);
}

} // namespace blackhole::physics
