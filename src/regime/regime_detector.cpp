#include "regime/regime_detector.h"
#include "utils/math_utils.h"

namespace ste {

RegimeDetector::RegimeDetector() {
    hmm_.initMarketRegimeModel();
    current_state_.current_regime = MarketRegime::BullQuiet;
    current_state_.regime_probabilities = {0.4, 0.15, 0.15, 0.05, 0.25};
    current_state_.transition_probability = 0.03;
    current_state_.crisis_probability = 0.05;
    current_state_.regime_duration_days = 0;
    current_state_.confidence = 0.4;
}

MarketRegime RegimeDetector::stateToRegime(int state) {
    if (state < 0 || state >= static_cast<int>(MarketRegime::COUNT))
        return MarketRegime::Transition;
    return static_cast<MarketRegime>(state);
}

std::vector<double> RegimeDetector::extractFeatures(const MarketSnapshot& snapshot) const {
    std::vector<double> features(4, 0.0);

    // Feature 0: Daily return
    if (!price_history_.empty()) {
        double prev = price_history_.back();
        if (prev > 0) features[0] = (snapshot.spot_price - prev) / prev;
    }

    // Feature 1: Realized volatility (annualized)
    features[1] = snapshot.implied_vol;

    // Feature 2: Vol-of-vol (change in vol)
    if (!vol_history_.empty()) {
        features[2] = snapshot.implied_vol - vol_history_.back();
    }

    // Feature 3: Credit spread (proxy for risk sentiment)
    features[3] = snapshot.credit_spread;

    return features;
}

RegimeState RegimeDetector::update(const MarketSnapshot& snapshot) {
    auto features = extractFeatures(snapshot);

    // Update HMM
    auto probs = hmm_.update(features);

    // Store history
    price_history_.push_back(snapshot.spot_price);
    vol_history_.push_back(snapshot.implied_vol);
    volume_history_.push_back(snapshot.volume);
    spread_history_.push_back(snapshot.credit_spread);

    // Keep reasonable buffer size
    if (price_history_.size() > 500) {
        price_history_.erase(price_history_.begin());
        vol_history_.erase(vol_history_.begin());
        volume_history_.erase(volume_history_.begin());
        spread_history_.erase(spread_history_.begin());
    }

    // Determine current regime
    int best_state = hmm_.mostLikelyState();
    MarketRegime new_regime = stateToRegime(best_state);

    if (new_regime == current_state_.current_regime) {
        regime_duration_++;
    } else {
        regime_history_.push_back({snapshot.timestamp, new_regime});
        regime_duration_ = 1;
    }

    // Build regime state
    current_state_.current_regime = new_regime;
    for (int i = 0; i < 5; ++i) current_state_.regime_probabilities[i] = probs[i];
    current_state_.transition_probability = hmm_.transitionProbability();
    current_state_.crisis_probability = probs[static_cast<int>(MarketRegime::BearVolatile)];
    current_state_.regime_duration_days = regime_duration_;
    current_state_.confidence = *std::max_element(probs.begin(), probs.end());

    return current_state_;
}

TradingSignal RegimeDetector::generateSignal(const RegimeState& regime,
                                               const MarketSnapshot& market) const {
    TradingSignal signal;
    signal.timestamp = market.timestamp;

    double crisis_prob = regime.crisis_probability;
    double bear_prob = regime.regime_probabilities[static_cast<int>(MarketRegime::BearQuiet)] +
                       regime.regime_probabilities[static_cast<int>(MarketRegime::BearVolatile)];
    double bull_prob = regime.regime_probabilities[static_cast<int>(MarketRegime::BullQuiet)] +
                       regime.regime_probabilities[static_cast<int>(MarketRegime::BullVolatile)];
    double trans_prob = regime.transition_probability;

    // Early warning: VIX spike, credit spread widening, yield curve inversion
    double warning_score = earlyWarningScore();

    if (crisis_prob > 0.5 || warning_score > 0.8) {
        signal.signal = SignalType::Crisis;
        signal.target_cash_pct = 0.70;
        signal.target_equity_pct = 0.20;
        signal.target_options_pct = 0.10;
        signal.confidence = crisis_prob;
        signal.reason = "CRISIS DETECTED: High probability of severe market decline. "
                        "Move to maximum cash position immediately.";
    } else if (crisis_prob > 0.25 || bear_prob > 0.6 || warning_score > 0.6) {
        signal.signal = SignalType::GoToCash;
        signal.target_cash_pct = 0.55;
        signal.target_equity_pct = 0.30;
        signal.target_options_pct = 0.15;
        signal.confidence = bear_prob;
        signal.reason = "HIGH RISK: Elevated bear/crisis probability. "
                        "Reduce exposure and increase hedging.";
    } else if (bear_prob > 0.4 || trans_prob > 0.15 || warning_score > 0.4) {
        signal.signal = SignalType::ReduceRisk;
        signal.target_cash_pct = 0.40;
        signal.target_equity_pct = 0.40;
        signal.target_options_pct = 0.20;
        signal.confidence = (bear_prob + trans_prob) / 2.0;
        signal.reason = "CAUTION: Regime transition likely or bear probabilities rising. "
                        "Reduce risk incrementally.";
    } else if (bull_prob > 0.7 && market.implied_vol < 0.18) {
        signal.signal = SignalType::StrongBuy;
        signal.target_cash_pct = 0.15;
        signal.target_equity_pct = 0.60;
        signal.target_options_pct = 0.25;
        signal.confidence = bull_prob;
        signal.reason = "STRONG BULL: High confidence bull regime with low volatility. "
                        "Increase risk to outperform SP500 benchmark.";
    } else if (bull_prob > 0.5) {
        signal.signal = SignalType::Buy;
        signal.target_cash_pct = 0.25;
        signal.target_equity_pct = 0.50;
        signal.target_options_pct = 0.25;
        signal.confidence = bull_prob;
        signal.reason = "BULLISH: Moderate bull signal. Take measured risk positions.";
    } else {
        signal.signal = SignalType::Hold;
        signal.target_cash_pct = 0.35;
        signal.target_equity_pct = 0.45;
        signal.target_options_pct = 0.20;
        signal.confidence = 0.5;
        signal.reason = "NEUTRAL: Mixed signals. Maintain balanced positioning.";
    }

    return signal;
}

double RegimeDetector::earlyWarningScore() const {
    if (price_history_.size() < 20) return 0.0;

    double score = 0.0;
    int n = static_cast<int>(price_history_.size());

    // Factor 1: Accelerating volatility (vol-of-vol)
    if (vol_history_.size() >= 20) {
        auto recent_vol = std::vector<double>(vol_history_.end() - 10, vol_history_.end());
        auto older_vol = std::vector<double>(vol_history_.end() - 20, vol_history_.end() - 10);
        double recent_mean = math::mean(recent_vol);
        double older_mean = math::mean(older_vol);
        if (older_mean > 0) {
            double vol_accel = (recent_mean - older_mean) / older_mean;
            score += math::clamp(vol_accel * 3.0, 0.0, 0.3);  // max 0.3
        }
    }

    // Factor 2: Price decline momentum
    if (n >= 20) {
        double ret_5d = (price_history_[n-1] - price_history_[n-5]) / price_history_[n-5];
        double ret_20d = (price_history_[n-1] - price_history_[n-20]) / price_history_[n-20];
        if (ret_5d < -0.03) score += 0.15;
        if (ret_20d < -0.05) score += 0.15;
    }

    // Factor 3: Credit spread widening
    if (spread_history_.size() >= 10) {
        double recent_spread = spread_history_.back();
        double avg_spread = math::mean(std::vector<double>(
            spread_history_.end() - std::min(50, static_cast<int>(spread_history_.size())),
            spread_history_.end()));
        if (recent_spread > avg_spread * 1.5) score += 0.2;
    }

    // Factor 4: HMM transition probability
    score += current_state_.transition_probability * 0.2;

    return math::clamp(score, 0.0, 1.0);
}

double RegimeDetector::computeTransitionUrgency(const RegimeState& state) const {
    // How urgently should we act on regime change
    double urgency = state.transition_probability;
    if (state.crisis_probability > 0.3) urgency *= 2.0;
    if (state.regime_duration_days < 5) urgency *= 1.5;  // new regime, act fast
    return math::clamp(urgency, 0.0, 1.0);
}

} // namespace ste
