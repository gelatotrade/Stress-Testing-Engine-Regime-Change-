#include "core/market_data.h"
#include "utils/math_utils.h"

namespace ste {

MarketDataGenerator::MarketDataGenerator(unsigned seed) : rng_(seed) {
    initDefaultParams();
}

void MarketDataGenerator::initDefaultParams() {
    // Realistic regime parameters (annualized)
    regime_params_[MarketRegime::BullQuiet]    = {0.12, 0.12};  // steady bull
    regime_params_[MarketRegime::BullVolatile] = {0.15, 0.25};  // volatile rally
    regime_params_[MarketRegime::BearQuiet]    = {-0.08, 0.15}; // slow decline
    regime_params_[MarketRegime::BearVolatile] = {-0.30, 0.45}; // crisis
    regime_params_[MarketRegime::Transition]   = {0.0, 0.20};   // uncertain
}

void MarketDataGenerator::setRegimeParams(MarketRegime regime, double drift, double vol) {
    regime_params_[regime] = {drift, vol};
}

MarketSnapshot MarketDataGenerator::generateNext(const MarketSnapshot& current,
                                                   MarketRegime regime, double dt) {
    const auto& params = regime_params_[regime];
    double drift = params.drift * dt;
    double diffusion = params.vol * std::sqrt(dt) * normal_(rng_);

    MarketSnapshot next = current;
    next.timestamp = current.timestamp + dt * 365.0 * 86400.0;  // seconds
    next.spot_price = current.spot_price * std::exp(drift + diffusion);

    // VIX tracks regime volatility with noise
    double target_vix = params.vol * 100.0;
    next.vix = 0.9 * current.vix + 0.1 * target_vix + normal_(rng_) * 1.0;
    next.vix = std::max(9.0, std::min(80.0, next.vix));

    next.implied_vol = next.vix / 100.0;
    next.realized_vol = 0.8 * current.realized_vol + 0.2 * std::abs(diffusion / std::sqrt(dt));

    // SP500 level tracks spot
    next.sp500_level = next.spot_price;

    // Volume spikes in volatile regimes
    double base_vol = 3.0e9;
    next.volume = base_vol * (1.0 + 0.5 * (next.vix - 15.0) / 15.0 + normal_(rng_) * 0.1);

    // Put/Call ratio rises in bear markets
    next.put_call_ratio = 0.7 + (regime == MarketRegime::BearVolatile ? 0.5 : 0.0)
                         + (regime == MarketRegime::BearQuiet ? 0.2 : 0.0)
                         + normal_(rng_) * 0.05;

    // Credit spreads widen in crisis
    next.credit_spread = 1.0 + (regime == MarketRegime::BearVolatile ? 3.0 : 0.0)
                        + (regime == MarketRegime::BearQuiet ? 0.5 : 0.0)
                        + normal_(rng_) * 0.1;
    next.credit_spread = std::max(0.3, next.credit_spread);

    // Yield curve flattens/inverts before crisis
    next.yield_curve_slope = 1.5 - (regime == MarketRegime::BearVolatile ? 2.0 : 0.0)
                            - (regime == MarketRegime::Transition ? 1.0 : 0.0)
                            + normal_(rng_) * 0.1;

    next.risk_free_rate = std::max(0.01, current.risk_free_rate + normal_(rng_) * 0.001);
    next.dividend_yield = 0.015;

    return next;
}

std::vector<MarketSnapshot> MarketDataGenerator::generateHistory(int num_days,
                                                                   double initial_price) {
    std::vector<MarketSnapshot> history;
    history.reserve(num_days);

    // Regime transition matrix (daily probabilities)
    // BullQ, BullV, BearQ, BearV, Trans
    std::vector<std::vector<double>> transitions = {
        {0.980, 0.008, 0.005, 0.002, 0.005},  // BullQuiet
        {0.010, 0.960, 0.005, 0.010, 0.015},  // BullVolatile
        {0.005, 0.005, 0.970, 0.010, 0.010},  // BearQuiet
        {0.005, 0.010, 0.010, 0.960, 0.015},  // BearVolatile
        {0.020, 0.015, 0.020, 0.015, 0.930},  // Transition
    };

    // Initial snapshot
    MarketSnapshot snap;
    snap.timestamp = 0;
    snap.spot_price = initial_price;
    snap.sp500_level = initial_price;
    snap.risk_free_rate = 0.05;
    snap.dividend_yield = 0.015;
    snap.implied_vol = 0.15;
    snap.realized_vol = 0.14;
    snap.vix = 15.0;
    snap.volume = 3.0e9;
    snap.put_call_ratio = 0.70;
    snap.credit_spread = 1.0;
    snap.yield_curve_slope = 1.5;
    history.push_back(snap);

    MarketRegime regime = MarketRegime::BullQuiet;
    std::uniform_real_distribution<double> uniform(0.0, 1.0);

    for (int d = 1; d < num_days; ++d) {
        // Regime transition
        double u = uniform(rng_);
        double cum = 0.0;
        int r_idx = static_cast<int>(regime);
        for (int r = 0; r < 5; ++r) {
            cum += transitions[r_idx][r];
            if (u <= cum) {
                regime = static_cast<MarketRegime>(r);
                break;
            }
        }

        snap = generateNext(snap, regime, 1.0 / 252.0);
        history.push_back(snap);
    }

    return history;
}

std::vector<MarketSnapshot> MarketDataGenerator::generateCrisis(const MarketSnapshot& start,
                                                                  int duration_days) {
    std::vector<MarketSnapshot> crisis;
    crisis.reserve(duration_days);

    MarketSnapshot snap = start;
    crisis.push_back(snap);

    // Phase 1: Sharp decline (40% of duration)
    int phase1 = duration_days * 4 / 10;
    for (int d = 0; d < phase1; ++d) {
        snap = generateNext(snap, MarketRegime::BearVolatile, 1.0 / 252.0);
        crisis.push_back(snap);
    }

    // Phase 2: Volatile bottom (30% of duration)
    int phase2 = duration_days * 3 / 10;
    for (int d = 0; d < phase2; ++d) {
        snap = generateNext(snap, MarketRegime::Transition, 1.0 / 252.0);
        crisis.push_back(snap);
    }

    // Phase 3: Recovery (30% of duration)
    for (int d = 0; d < duration_days - phase1 - phase2; ++d) {
        snap = generateNext(snap, MarketRegime::BullVolatile, 1.0 / 252.0);
        crisis.push_back(snap);
    }

    return crisis;
}

} // namespace ste
