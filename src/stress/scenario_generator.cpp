#include "stress/scenario_generator.h"
#include <random>
#include <cmath>

namespace ste {

std::vector<StressScenario> ScenarioGenerator::generateGrid(
    double spot_min, double spot_max, int spot_steps,
    double vol_min, double vol_max, int vol_steps) {

    std::vector<StressScenario> scenarios;
    scenarios.reserve(spot_steps * vol_steps);

    for (int i = 0; i < spot_steps; ++i) {
        for (int j = 0; j < vol_steps; ++j) {
            double spot_shock = spot_min + (spot_max - spot_min) * i / (spot_steps - 1);
            double vol_shock = vol_min + (vol_max - vol_min) * j / (vol_steps - 1);

            StressScenario s;
            s.name = "Grid_S" + std::to_string(static_cast<int>(spot_shock*100))
                   + "_V" + std::to_string(static_cast<int>(vol_shock*100));
            s.spot_shock = spot_shock;
            s.vol_shock = vol_shock;
            s.rate_shock = 0.0;
            s.time_shock = 0.0;
            s.correlation_shock = 0.0;
            s.liquidity_shock = 1.0;
            scenarios.push_back(s);
        }
    }
    return scenarios;
}

std::vector<StressScenario> ScenarioGenerator::generateTailRisk(int num_scenarios) {
    std::vector<StressScenario> scenarios;
    std::mt19937 rng(42);
    std::normal_distribution<double> normal(0.0, 1.0);

    for (int i = 0; i < num_scenarios; ++i) {
        StressScenario s;
        // Generate extreme scenarios (3+ sigma events)
        double z1 = normal(rng);
        double z2 = normal(rng);

        // Make sure we get tail events
        if (std::abs(z1) < 2.0) z1 *= 2.5;
        if (std::abs(z2) < 2.0) z2 *= 2.5;

        s.spot_shock = z1 * 0.08;  // 8% per sigma
        s.vol_shock = std::abs(z2) * 0.10;  // vol always increases in tail
        s.rate_shock = z1 * 0.005;
        s.time_shock = 0;
        s.correlation_shock = std::abs(z1) > 3.0 ? 0.3 : 0.0;
        s.liquidity_shock = 1.0 + std::abs(z1) * 0.5;
        s.name = "TailRisk_" + std::to_string(i);
        scenarios.push_back(s);
    }
    return scenarios;
}

std::vector<StressScenario> ScenarioGenerator::generateCorrelated(
    double correlation, int num_scenarios) {
    std::vector<StressScenario> scenarios;
    std::mt19937 rng(42);
    std::normal_distribution<double> normal(0.0, 1.0);

    for (int i = 0; i < num_scenarios; ++i) {
        double z1 = normal(rng);
        double z2 = correlation * z1 + std::sqrt(1.0 - correlation*correlation) * normal(rng);

        StressScenario s;
        s.name = "Corr_" + std::to_string(i);
        s.spot_shock = z1 * 0.05;
        s.vol_shock = -z1 * 0.08 + z2 * 0.02;  // vol inversely correlated with spot
        s.rate_shock = z2 * 0.002;
        s.time_shock = 0;
        s.correlation_shock = 0;
        s.liquidity_shock = 1.0 + std::max(0.0, -z1) * 0.3;
        scenarios.push_back(s);
    }
    return scenarios;
}

StressScenario ScenarioGenerator::reverseStressTest(
    const std::function<double(const StressScenario&)>& loss_function,
    double target_loss, int max_iterations) {

    // Gradient-free optimization (Nelder-Mead like)
    StressScenario best;
    best.name = "ReverseStress";
    best.spot_shock = -0.10;
    best.vol_shock = 0.15;
    best.rate_shock = 0.01;
    best.time_shock = 0;
    best.correlation_shock = 0;
    best.liquidity_shock = 1.0;

    double best_diff = std::abs(loss_function(best) - target_loss);

    std::mt19937 rng(42);
    std::normal_distribution<double> normal(0.0, 1.0);

    for (int i = 0; i < max_iterations; ++i) {
        StressScenario candidate = best;
        double step = 0.01 * std::exp(-0.003 * i);  // cooling schedule

        candidate.spot_shock += normal(rng) * step * 3.0;
        candidate.vol_shock += std::abs(normal(rng)) * step * 2.0;
        candidate.rate_shock += normal(rng) * step * 0.5;

        double diff = std::abs(loss_function(candidate) - target_loss);
        if (diff < best_diff) {
            best = candidate;
            best_diff = diff;
        }
    }

    return best;
}

} // namespace ste
