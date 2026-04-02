#pragma once
#include "core/types.h"
#include "strategies/options_strategies.h"

namespace ste {

// Manages strategy selection based on regime and signals
class StrategyManager {
public:
    StrategyManager() = default;

    // Select optimal strategies for current regime
    std::vector<Strategy> recommendStrategies(const MarketSnapshot& market,
                                               const RegimeState& regime,
                                               double risk_tolerance = 0.5) const;

    // Adjust portfolio allocation based on signal
    struct AllocationAdvice {
        double cash_pct;
        double equity_pct;
        double options_pct;
        std::vector<StrategyType> recommended_strategies;
        std::string rationale;
    };

    AllocationAdvice getAdvice(const RegimeState& regime,
                               const TradingSignal& signal,
                               double current_drawdown) const;

    // Risk-adjusted strategy scoring
    double scoreStrategy(StrategyType type, const RegimeState& regime,
                          const MarketSnapshot& market) const;

private:
    // Strategy suitability matrix per regime
    static double regimeSuitability(StrategyType type, MarketRegime regime);
};

} // namespace ste
