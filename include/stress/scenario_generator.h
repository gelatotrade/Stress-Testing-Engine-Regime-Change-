#pragma once
#include "core/types.h"

namespace ste {

class ScenarioGenerator {
public:
    // Generate parametric scenarios
    static std::vector<StressScenario> generateGrid(
        double spot_min, double spot_max, int spot_steps,
        double vol_min, double vol_max, int vol_steps);

    // Generate tail-risk scenarios
    static std::vector<StressScenario> generateTailRisk(int num_scenarios = 20);

    // Generate correlated multi-factor scenarios
    static std::vector<StressScenario> generateCorrelated(
        double correlation, int num_scenarios = 50);

    // Reverse stress test: find scenarios that cause max loss
    static StressScenario reverseStressTest(
        const std::function<double(const StressScenario&)>& loss_function,
        double target_loss, int max_iterations = 1000);
};

} // namespace ste
