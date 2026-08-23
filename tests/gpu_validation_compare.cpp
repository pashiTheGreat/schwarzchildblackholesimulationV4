#include "physics/accretion_disk.hpp"
#include "physics/schwarzschild.hpp"
#include "physics/simulation_config.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct GpuRow {
    int x = 0;
    int y = 0;
    std::uint32_t status = 5;
    float initial_null = 0.0f;
    float initial_energy = 0.0f;
    float final_null = 0.0f;
};

struct TemperatureRow {
    double radius = 0.0;
    double temperature_default = 0.0;
    double temperature_tenfold = 0.0;
    double temperature_reversed = 0.0;
    double shift_positive = 0.0;
    double shift_negative = 0.0;
    double reversed_shift_positive = 0.0;
    double reversed_shift_negative = 0.0;
    double physical_flux_shape = 0.0;
    double display_flux_profile = 0.0;
};

bool parse_row(const std::string& line, GpuRow& row) {
    std::stringstream stream(line);
    std::string field;
    std::vector<std::string> fields;
    while (std::getline(stream, field, ','))
        fields.push_back(field);
    if (fields.size() != 11)
        return false;
    row.x = std::stoi(fields[0]);
    row.y = std::stoi(fields[1]);
    row.status = static_cast<std::uint32_t>(std::stoul(fields[2]));
    row.initial_null = std::stof(fields[3]);
    row.initial_energy = std::stof(fields[4]);
    row.final_null = std::stof(fields[7]);
    return true;
}

bool parse_temperature_row(const std::string& line, TemperatureRow& row) {
    std::stringstream stream(line);
    std::string field;
    std::vector<double> fields;
    while (std::getline(stream, field, ','))
        fields.push_back(std::stod(field));
    if (fields.size() != 10)
        return false;
    row = {fields[0], fields[1], fields[2], fields[3], fields[4],
           fields[5], fields[6], fields[7], fields[8], fields[9]};
    return true;
}

bool read_rows(const std::string& path, std::vector<GpuRow>& rows) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << "Unable to open GPU validation export: " << path << '\n';
        return false;
    }

    std::string line;
    std::getline(input, line);
    while (std::getline(input, line)) {
        if (line.empty())
            continue;
        GpuRow row;
        if (!parse_row(line, row)) {
            std::cerr << "Malformed GPU validation row in " << path << '\n';
            return false;
        }
        rows.push_back(row);
    }
    return true;
}

int compare_thickness_exports(const std::string& thin_path, const std::string& thick_path) {
    std::vector<GpuRow> thin_rows;
    std::vector<GpuRow> thick_rows;
    if (!read_rows(thin_path, thin_rows) || !read_rows(thick_path, thick_rows))
        return 2;
    if (thin_rows.empty() || thin_rows.size() != thick_rows.size()) {
        std::cerr << "Thickness exports must contain the same non-zero number of rows\n";
        return 2;
    }

    std::size_t changed_status_pixels = 0;
    std::size_t thin_disk_hits = 0;
    std::size_t thick_disk_hits = 0;
    std::size_t unresolved_or_invalid = 0;
    for (std::size_t index = 0; index < thin_rows.size(); ++index) {
        const GpuRow& thin = thin_rows[index];
        const GpuRow& thick = thick_rows[index];
        if (thin.x != thick.x || thin.y != thick.y) {
            std::cerr << "Thickness exports use different pixel ordering\n";
            return 2;
        }
        changed_status_pixels += thin.status != thick.status ? 1u : 0u;
        thin_disk_hits += thin.status == 2u ? 1u : 0u;
        thick_disk_hits += thick.status == 2u ? 1u : 0u;
        unresolved_or_invalid += thin.status == 4u || thin.status == 5u ? 1u : 0u;
        unresolved_or_invalid += thick.status == 4u || thick.status == 5u ? 1u : 0u;
    }

    // With the former max(thickness, 1.0) shader floor, both exports are
    // classified using the same one-r_s slab and changed_status_pixels is zero.
    // Requiring five percent also keeps the test from passing on incidental
    // implementation or driver noise at a classification boundary.
    const std::size_t minimum_changed_pixels = thin_rows.size() / 20;
    const bool passed = changed_status_pixels >= minimum_changed_pixels &&
                        thick_disk_hits > thin_disk_hits && unresolved_or_invalid == 0;
    std::cout << "GPU disk half-thickness regression\n"
              << "pixels_per_export=" << thin_rows.size() << '\n'
              << "thin_0.02_disk_hits=" << thin_disk_hits << '\n'
              << "thick_0.75_disk_hits=" << thick_disk_hits << '\n'
              << "changed_status_pixels=" << changed_status_pixels << '\n'
              << "minimum_changed_pixels=" << minimum_changed_pixels << '\n'
              << "unresolved_or_invalid=" << unresolved_or_invalid << '\n'
              << "overall=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}

int compare_temperature_export(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        std::cerr << "Unable to open GPU temperature export: " << path << '\n';
        return 2;
    }

    std::string line;
    std::getline(input, line);
    std::vector<TemperatureRow> rows;
    while (std::getline(input, line)) {
        if (line.empty())
            continue;
        TemperatureRow row;
        if (!parse_temperature_row(line, row)) {
            std::cerr << "Malformed GPU temperature row\n";
            return 2;
        }
        rows.push_back(row);
    }

    constexpr std::array<double, 5> expected_radii{3.5, 4.5, 6.0, 9.0, 12.0};
    if (rows.size() != expected_radii.size()) {
        std::cerr << "Expected five GPU temperature samples, received " << rows.size() << '\n';
        return 2;
    }

    using namespace blackhole::physics;
    const auto configuration = make_default_simulation_configuration();
    const double mass = configuration.black_hole.mass_kg;
    const double schwarzschild_radius = configuration.black_hole.schwarzschild_radius_m;
    const double inner_radius_in_schwarzschild_radii =
        configuration.disk.inner_radius_in_schwarzschild_radii;
    const double inner_radius = inner_radius_in_schwarzschild_radii * schwarzschild_radius;
    const double accretion_rate = configuration.disk.accretion_rate_kg_per_s;
    constexpr double temperature_relative_tolerance = 2.0e-5;
    constexpr double temperature_absolute_tolerance_kelvin = 0.05;
    constexpr double dimensionless_relative_tolerance = 2.0e-5;
    constexpr double dimensionless_absolute_tolerance = 2.0e-7;
    const auto close_float = [](double actual, double expected, double relative_tolerance,
                                double absolute_tolerance) {
        return std::abs(actual - expected) <=
               std::max(absolute_tolerance, std::abs(expected) * relative_tolerance);
    };

    bool passed = true;
    bool doppler_reversal_passed = true;
    double maximum_temperature_relative_error = 0.0;
    double maximum_rotation_temperature_delta = 0.0;
    double temperature_at_four_point_five = 0.0;
    double cpu_temperature_at_four_point_five = 0.0;
    double tenfold_temperature_at_four_point_five = 0.0;
    double shift_positive_at_four_point_five = 0.0;
    double shift_negative_at_four_point_five = 0.0;
    double reversed_shift_positive_at_four_point_five = 0.0;
    double reversed_shift_negative_at_four_point_five = 0.0;
    std::ostringstream sample_report;
    sample_report << std::setprecision(9);

    for (std::size_t index = 0; index < rows.size(); ++index) {
        const TemperatureRow& row = rows[index];
        const double radius = expected_radii[index];
        const double radius_m = radius * schwarzschild_radius;
        const double cpu_flux = thin_disk_flux(mass, accretion_rate, radius_m, inner_radius);
        const double cpu_tenfold_flux =
            thin_disk_flux(mass, 10.0 * accretion_rate, radius_m, inner_radius);
        const double cpu_temperature = effective_temperature_from_flux(cpu_flux);
        const double cpu_tenfold_temperature = effective_temperature_from_flux(cpu_tenfold_flux);
        const double no_torque_boundary =
            1.0 - std::sqrt(inner_radius_in_schwarzschild_radii / radius);
        const double physical_flux_shape = no_torque_boundary / (radius * radius * radius);
        const double normalized_radius = radius / inner_radius_in_schwarzschild_radii;
        const double display_flux_profile =
            no_torque_boundary / (normalized_radius * normalized_radius * normalized_radius);
        const double cpu_shift_positive = circular_emitter_frequency_shift(
            configuration.camera.radius_in_schwarzschild_radii, radius, 0.9, 2.1, 1.0);
        const double cpu_shift_negative = circular_emitter_frequency_shift(
            configuration.camera.radius_in_schwarzschild_radii, radius, 0.9, -2.1, 1.0);
        const double cpu_reversed_shift_positive = circular_emitter_frequency_shift(
            configuration.camera.radius_in_schwarzschild_radii, radius, 0.9, 2.1, -1.0);
        const double cpu_reversed_shift_negative = circular_emitter_frequency_shift(
            configuration.camera.radius_in_schwarzschild_radii, radius, 0.9, -2.1, -1.0);

        const bool row_passed =
            close_float(row.radius, radius, 0.0, 1.0e-6) &&
            close_float(row.temperature_default, cpu_temperature, temperature_relative_tolerance,
                        temperature_absolute_tolerance_kelvin) &&
            close_float(row.temperature_tenfold, cpu_tenfold_temperature,
                        temperature_relative_tolerance, temperature_absolute_tolerance_kelvin) &&
            close_float(row.temperature_reversed, cpu_temperature, temperature_relative_tolerance,
                        temperature_absolute_tolerance_kelvin) &&
            close_float(row.physical_flux_shape, physical_flux_shape,
                        dimensionless_relative_tolerance, dimensionless_absolute_tolerance) &&
            close_float(row.display_flux_profile, display_flux_profile,
                        dimensionless_relative_tolerance, dimensionless_absolute_tolerance) &&
            close_float(row.shift_positive, cpu_shift_positive, dimensionless_relative_tolerance,
                        dimensionless_absolute_tolerance) &&
            close_float(row.shift_negative, cpu_shift_negative, dimensionless_relative_tolerance,
                        dimensionless_absolute_tolerance) &&
            close_float(row.reversed_shift_positive, cpu_reversed_shift_positive,
                        dimensionless_relative_tolerance, dimensionless_absolute_tolerance) &&
            close_float(row.reversed_shift_negative, cpu_reversed_shift_negative,
                        dimensionless_relative_tolerance, dimensionless_absolute_tolerance) &&
            close_float(row.temperature_tenfold / row.temperature_default, std::pow(10.0, 0.25),
                        temperature_relative_tolerance, 0.0);
        passed = passed && row_passed;

        const bool row_doppler_reversal =
            row.shift_positive > row.shift_negative &&
            row.reversed_shift_positive < row.reversed_shift_negative &&
            close_float(row.shift_positive, row.reversed_shift_negative,
                        dimensionless_relative_tolerance, dimensionless_absolute_tolerance) &&
            close_float(row.shift_negative, row.reversed_shift_positive,
                        dimensionless_relative_tolerance, dimensionless_absolute_tolerance);
        doppler_reversal_passed = doppler_reversal_passed && row_doppler_reversal;
        maximum_rotation_temperature_delta =
            std::max(maximum_rotation_temperature_delta,
                     std::abs(row.temperature_default - row.temperature_reversed));
        maximum_temperature_relative_error =
            std::max(maximum_temperature_relative_error,
                     std::abs(row.temperature_default - cpu_temperature) / cpu_temperature);
        sample_report << "radius_r_s=" << radius << " gpu_K=" << row.temperature_default
                      << " cpu_K=" << cpu_temperature << " gpu_10x_K=" << row.temperature_tenfold
                      << " ratio=" << row.temperature_tenfold / row.temperature_default
                      << " result=" << (row_passed ? "PASS" : "FAIL") << '\n';
        if (radius == 4.5) {
            temperature_at_four_point_five = row.temperature_default;
            cpu_temperature_at_four_point_five = cpu_temperature;
            tenfold_temperature_at_four_point_five = row.temperature_tenfold;
            shift_positive_at_four_point_five = row.shift_positive;
            shift_negative_at_four_point_five = row.shift_negative;
            reversed_shift_positive_at_four_point_five = row.reversed_shift_positive;
            reversed_shift_negative_at_four_point_five = row.reversed_shift_negative;
        }
    }

    const bool rotation_temperature_passed =
        maximum_rotation_temperature_delta <= temperature_absolute_tolerance_kelvin;
    passed = passed && doppler_reversal_passed && rotation_temperature_passed;
    std::cout << std::setprecision(9) << "GPU/CPU disk temperature comparison\n"
              << sample_report.str() << "r4.5_gpu_K=" << temperature_at_four_point_five
              << " r4.5_cpu_K=" << cpu_temperature_at_four_point_five
              << " r4.5_gpu_10x_K=" << tenfold_temperature_at_four_point_five << '\n'
              << "r4.5_shift_l_positive=" << shift_positive_at_four_point_five
              << " shift_l_negative=" << shift_negative_at_four_point_five
              << " reversed_shift_l_positive=" << reversed_shift_positive_at_four_point_five
              << " reversed_shift_l_negative=" << reversed_shift_negative_at_four_point_five << '\n'
              << "temperature_relative_tolerance=" << temperature_relative_tolerance << '\n'
              << "temperature_absolute_tolerance_K=" << temperature_absolute_tolerance_kelvin
              << '\n'
              << "max_temperature_relative_error=" << maximum_temperature_relative_error << '\n'
              << "max_rotation_temperature_delta_K=" << maximum_rotation_temperature_delta << '\n'
              << "doppler_reversal=" << (doppler_reversal_passed ? "PASS" : "FAIL") << '\n'
              << "overall=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--temperature")
        return compare_temperature_export(argv[2]);

    if (argc == 4 && std::string(argv[1]) == "--thickness")
        return compare_thickness_exports(argv[2], argv[3]);

    if (argc != 2) {
        std::cerr << "Usage: GpuValidationCompare <gpu_validation.csv>\n"
                  << "       GpuValidationCompare --thickness <thin.csv> <thick.csv>\n"
                  << "       GpuValidationCompare --temperature <temperature.csv>\n";
        return 2;
    }
    std::vector<GpuRow> rows;
    if (!read_rows(argv[1], rows))
        return 2;
    constexpr int width = 64;
    constexpr int height = 48;
    if (rows.size() != static_cast<size_t>(width * height)) {
        std::cerr << "Expected " << width * height << " rows, received " << rows.size() << '\n';
        return 2;
    }

    using namespace blackhole::physics;
    AdaptiveIntegrationOptions options;
    options.absolute_tolerance = 1.0e-8;
    options.relative_tolerance = 1.0e-7;
    options.minimum_step = 1.0e-5;
    options.maximum_step = 0.10;
    options.maximum_steps = 24000;
    options.capture_radius = 1.0005;
    options.escape_radius = 80.0;
    constexpr double observer_radius = 4.9976;
    constexpr double tan_half_fov = 0.5773502691896257645;
    constexpr double aspect = static_cast<double>(width) / height;
    std::size_t mismatches = 0;
    std::size_t unresolved_or_invalid = 0;
    double maximum_initial_null = 0.0;
    double maximum_escaped_final_null = 0.0;
    double minimum_energy = 1.0e30;
    double maximum_energy = -1.0e30;
    double maximum_captured_angle = 0.0;
    double minimum_escaped_angle = 1.0e30;

    for (const GpuRow& row : rows) {
        const double u = (2.0 * (row.x + 0.5) / width - 1.0) * aspect * tan_half_fov;
        const double v = (1.0 - 2.0 * (row.y + 0.5) / height) * tan_half_fov;
        const double normalization = std::sqrt(1.0 + u * u + v * v);
        const PhotonState initial = initialize_static_observer_photon(
            {observer_radius, 1.5707963267948966, 0.0},
            {-1.0 / normalization, v / normalization, -u / normalization});
        const VacuumTraceResult cpu = trace_vacuum_adaptive(initial, options);
        const std::uint32_t cpu_status =
            cpu.status == RayStatus::Captured ? 0u : (cpu.status == RayStatus::Escaped ? 1u : 5u);
        if (row.status != cpu_status)
            ++mismatches;
        if (row.status == 4u || row.status == 5u)
            ++unresolved_or_invalid;
        maximum_initial_null =
            std::max(maximum_initial_null, std::abs(static_cast<double>(row.initial_null)));
        minimum_energy = std::min(minimum_energy, static_cast<double>(row.initial_energy));
        maximum_energy = std::max(maximum_energy, static_cast<double>(row.initial_energy));
        if (row.status == 1u) {
            maximum_escaped_final_null =
                std::max(maximum_escaped_final_null, std::abs(static_cast<double>(row.final_null)));
        }
        const double screen_radius = std::sqrt(u * u + v * v);
        const double angle = std::atan(screen_radius);
        if (row.status == 0u) {
            maximum_captured_angle = std::max(maximum_captured_angle, angle);
        } else if (row.status == 1u) {
            minimum_escaped_angle = std::min(minimum_escaped_angle, angle);
        }
    }

    constexpr double critical_impact_parameter = 2.5980762113533159403;
    const double analytical_shadow_angle = std::asin(
        critical_impact_parameter * std::sqrt(metric_factor(observer_radius)) / observer_radius);
    const double measured_shadow_angle = 0.5 * (maximum_captured_angle + minimum_escaped_angle);
    const double angular_sampling_half_width =
        0.5 * (minimum_escaped_angle - maximum_captured_angle);
    constexpr double radians_to_degrees = 57.2957795130823208768;
    constexpr double extra_edge_tolerance = 0.5 / radians_to_degrees;
    const bool shadow_edge_passed = std::abs(measured_shadow_angle - analytical_shadow_angle) <=
                                    std::abs(angular_sampling_half_width) + extra_edge_tolerance;

    constexpr std::size_t allowed_edge_mismatches = 8;
    const bool passed = mismatches <= allowed_edge_mismatches && unresolved_or_invalid == 0 &&
                        maximum_initial_null < 2.0e-5 && maximum_energy - minimum_energy < 2.0e-5 &&
                        maximum_escaped_final_null < 2.0e-3 && shadow_edge_passed;
    std::cout << "GPU/CPU deterministic ray comparison\n"
              << "pixels=" << rows.size() << " mismatches=" << mismatches
              << " allowed=" << allowed_edge_mismatches << '\n'
              << "unresolved_or_invalid=" << unresolved_or_invalid << '\n'
              << "max_initial_null=" << maximum_initial_null << '\n'
              << "energy_span=" << maximum_energy - minimum_energy << '\n'
              << "max_escaped_final_null=" << maximum_escaped_final_null << '\n'
              << "shadow_edge_degrees=" << measured_shadow_angle * radians_to_degrees
              << " analytical_degrees=" << analytical_shadow_angle * radians_to_degrees
              << " sampling_half_width_degrees="
              << std::abs(angular_sampling_half_width) * radians_to_degrees
              << " edge=" << (shadow_edge_passed ? "PASS" : "FAIL") << '\n'
              << "overall=" << (passed ? "PASS" : "FAIL") << '\n';
    return passed ? 0 : 1;
}
