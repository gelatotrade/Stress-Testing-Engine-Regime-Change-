#pragma once
#include "core/types.h"

namespace ste {

struct MonteCarloConfig {
    int num_paths = 100000;
    int num_steps = 252;       // trading days per year
    unsigned seed = 42;
    bool antithetic = true;
    bool use_control_variate = true;
};

struct PathResult {
    std::vector<double> prices;
    double final_price;
    double path_return;
};

struct MonteCarloResult {
    double mean_price;
    double std_error;
    double var_95;
    double cvar_95;
    double max_drawdown;
    std::vector<double> terminal_prices;
    std::vector<double> path_returns;
    double prob_loss;
    double prob_gain_10pct;
    double expected_shortfall;
};

class MonteCarlo {
public:
    explicit MonteCarlo(const MonteCarloConfig& config = {});

    // Simulate GBM paths
    std::vector<PathResult> simulatePaths(double spot, double rate,
                                          double vol, double T,
                                          double dividend = 0.0) const;

    // Price option via Monte Carlo
    double priceOption(const Option& opt, double spot, double rate,
                       double vol, double dividend = 0.0) const;

    // Full portfolio stress simulation
    MonteCarloResult stressSimulation(double spot, double rate, double vol,
                                     double T, double dividend = 0.0) const;

    // Simulate with regime-switching volatility
    MonteCarloResult regimeSwitchingSimulation(
        double spot, double rate, double T,
        const std::vector<double>& regime_vols,
        const std::vector<std::vector<double>>& transition_matrix,
        int initial_regime = 0) const;

    void setConfig(const MonteCarloConfig& config) { config_ = config; }

private:
    MonteCarloConfig config_;
};

} // namespace ste
