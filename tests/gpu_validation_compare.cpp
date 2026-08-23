#include "physics/schwarzschild.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
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

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: GpuValidationCompare <gpu_validation.csv>\n";
        return 2;
    }
    std::ifstream input(argv[1]);
    if (!input) {
        std::cerr << "Unable to open GPU validation export: " << argv[1] << '\n';
        return 2;
    }
    std::string line;
    std::getline(input, line);
    std::vector<GpuRow> rows;
    while (std::getline(input, line)) {
        if (line.empty())
            continue;
        GpuRow row;
        if (!parse_row(line, row)) {
            std::cerr << "Malformed GPU validation row\n";
            return 2;
        }
        rows.push_back(row);
    }
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
