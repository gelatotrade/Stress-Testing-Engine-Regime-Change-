#pragma once
#include "core/types.h"

namespace ste {

// Generates synthetic market data for simulation
class MarketDataGenerator {
public:
    MarketDataGenerator(unsigned seed = 42);

    // Generate historical-like data with regime changes
    std::vector<MarketSnapshot> generateHistory(int num_days, double initial_price = 4500.0);

    // Generate a single next snapshot given current state
    MarketSnapshot generateNext(const MarketSnapshot& current, MarketRegime regime, double dt = 1.0/252.0);

    // Generate specific crisis scenario data
    std::vector<MarketSnapshot> generateCrisis(const MarketSnapshot& start, int duration_days);

    // Set regime parameters
    void setRegimeParams(MarketRegime regime, double drift, double vol);

private:
    std::mt19937 rng_;
    std::normal_distribution<double> normal_{0.0, 1.0};

    struct RegimeParams {
        double drift;
        double vol;
    };
    std::map<MarketRegime, RegimeParams> regime_params_;

    void initDefaultParams();
};

} // namespace ste
