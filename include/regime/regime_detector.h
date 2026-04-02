#pragma once
#include "core/types.h"
#include "regime/hidden_markov_model.h"

namespace ste {

class RegimeDetector {
public:
    RegimeDetector();

    // Process new market data and update regime estimate
    RegimeState update(const MarketSnapshot& snapshot);

    // Get current regime assessment
    RegimeState currentState() const { return current_state_; }

    // Generate trading signal based on regime
    TradingSignal generateSignal(const RegimeState& regime,
                                  const MarketSnapshot& market) const;

    // Compute early warning score (0-1, higher = more danger)
    double earlyWarningScore() const;

    // Get regime change history
    const std::vector<std::pair<double, MarketRegime>>& regimeHistory() const {
        return regime_history_;
    }

    // Feature extraction from market snapshot
    std::vector<double> extractFeatures(const MarketSnapshot& snapshot) const;

private:
    HiddenMarkovModel hmm_;
    RegimeState current_state_;
    std::vector<std::pair<double, MarketRegime>> regime_history_;

    // Lookback buffers for feature computation
    std::vector<double> price_history_;
    std::vector<double> vol_history_;
    std::vector<double> volume_history_;
    std::vector<double> spread_history_;
    int regime_duration_ = 0;

    // Map HMM state index to MarketRegime
    static MarketRegime stateToRegime(int state);

    // Compute transition urgency
    double computeTransitionUrgency(const RegimeState& state) const;
};

} // namespace ste
