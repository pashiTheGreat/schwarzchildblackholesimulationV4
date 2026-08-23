#pragma once

namespace blackhole::physics {

constexpr double stefan_boltzmann_si = 5.670374419e-8;

// Newtonian zero-torque thin-disk surface flux used as an explicitly
// approximate emitter model on top of exact Schwarzschild vacuum geodesics.
double thin_disk_flux(double mass_kg, double accretion_rate_kg_per_s, double radius_m,
                      double inner_radius_m);
double effective_temperature_from_flux(double flux_w_per_square_metre);

// Frequency ratio g=(-k.u_observer)/(-k.u_emitter) for a static observer and
// circular Schwarzschild emitter.  Radii are in r_s units, E and L_axis are
// conserved dimensionless photon quantities, and rotation_sign is +/-1.
double circular_emitter_frequency_shift(double observer_radius, double emitter_radius,
                                        double photon_energy, double photon_axis_angular_momentum,
                                        double rotation_sign);

double bolometric_intensity_factor(double frequency_shift);

} // namespace blackhole::physics
