#pragma once
#include "core/types.h"
#include "core/portfolio.h"
#include "core/monte_carlo.h"

namespace ste {

class StressEngine {
public:
    StressEngine();

    // Run all scenarios against portfolio
    std::vector<StressResult> runAllScenarios(const Portfolio& portfolio,
                                               const MarketSnapshot& market) const;

    // Run specific scenario
    StressResult runScenario(const Portfolio& portfolio,
                              const MarketSnapshot& market,
                              const StressScenario& scenario) const;

    // Monte Carlo stress test
    MonteCarloResult monteCarloStress(const Portfolio& portfolio,
                                       const MarketSnapshot& market,
                                       int num_paths = 50000) const;

    // Greeks sensitivity analysis
    struct SensitivityResult {
        std::vector<double> spot_shocks;
        std::vector<double> pnl_values;
        std::vector<double> delta_values;
        std::vector<double> gamma_values;
    };

    SensitivityResult sensitivityAnalysis(const Portfolio& portfolio,
                                           const MarketSnapshot& market,
                                           double range_pct = 0.20,
                                           int num_points = 100) const;

    // Add custom scenario
    void addScenario(const StressScenario& scenario);

    // Get all scenarios
    const std::vector<StressScenario>& scenarios() const { return scenarios_; }

    // Compute VaR and CVaR
    double computeVaR(const Portfolio& portfolio, const MarketSnapshot& market,
                       double confidence = 0.95, int num_sims = 50000) const;
    double computeCVaR(const Portfolio& portfolio, const MarketSnapshot& market,
                        double confidence = 0.95, int num_sims = 50000) const;

private:
    std::vector<StressScenario> scenarios_;
    MonteCarlo mc_;
};

} // namespace ste
