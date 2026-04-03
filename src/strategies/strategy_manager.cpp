#include "strategies/strategy_manager.h"

namespace ste {

double StrategyManager::regimeSuitability(StrategyType type, MarketRegime regime) {
    // Suitability matrix: higher = better fit
    //                          BullQ  BullV  BearQ  BearV  Trans
    static const double matrix[][5] = {
        /* CoveredCall   */ {  0.9,   0.7,   0.4,   0.2,   0.5  },
        /* ProtectivePut */ {  0.3,   0.5,   0.7,   0.9,   0.6  },
        /* BullCallSprd  */ {  0.8,   0.6,   0.2,   0.1,   0.4  },
        /* BearPutSprd   */ {  0.1,   0.3,   0.8,   0.7,   0.4  },
        /* IronCondor    */ {  0.8,   0.3,   0.6,   0.1,   0.4  },
        /* IronButtfly   */ {  0.7,   0.2,   0.5,   0.1,   0.3  },
        /* Straddle      */ {  0.2,   0.8,   0.3,   0.8,   0.7  },
        /* Strangle      */ {  0.2,   0.7,   0.3,   0.7,   0.7  },
        /* CalendarSprd  */ {  0.6,   0.5,   0.5,   0.3,   0.5  },
        /* RatioSpread   */ {  0.5,   0.4,   0.5,   0.3,   0.4  },
        /* Collar        */ {  0.5,   0.6,   0.6,   0.8,   0.7  },
        /* JadeLizard    */ {  0.7,   0.4,   0.3,   0.2,   0.4  },
        /* Custom        */ {  0.5,   0.5,   0.5,   0.5,   0.5  },
    };

    int type_idx = static_cast<int>(type);
    int regime_idx = static_cast<int>(regime);
    if (type_idx < 0 || type_idx > 12 || regime_idx < 0 || regime_idx > 4) return 0.5;
    return matrix[type_idx][regime_idx];
}

double StrategyManager::scoreStrategy(StrategyType type, const RegimeState& regime,
                                       const MarketSnapshot& market) const {
    double suitability = regimeSuitability(type, regime.current_regime);
    double confidence_bonus = regime.confidence * 0.2;

    // Bonus for vol-selling strategies in high-vol regimes
    double vol_score = 0.0;
    if (market.implied_vol > 0.25) {
        if (type == StrategyType::IronCondor || type == StrategyType::IronButterfly ||
            type == StrategyType::CoveredCall)
            vol_score = 0.15;
    }

    // Bonus for protective strategies when crisis prob is high
    if (regime.crisis_probability > 0.3) {
        if (type == StrategyType::ProtectivePut || type == StrategyType::Collar)
            vol_score += 0.2;
    }

    return suitability + confidence_bonus + vol_score;
}

std::vector<Strategy> StrategyManager::recommendStrategies(const MarketSnapshot& market,
                                                             const RegimeState& regime,
                                                             double risk_tolerance) const {
    struct Scored {
        StrategyType type;
        double score;
    };

    std::vector<Scored> scored;
    for (int i = 0; i <= static_cast<int>(StrategyType::JadeLizard); ++i) {
        auto type = static_cast<StrategyType>(i);
        double s = scoreStrategy(type, regime, market);
        // Adjust for risk tolerance
        if (type == StrategyType::Straddle || type == StrategyType::Strangle)
            s *= (0.5 + risk_tolerance * 0.5);
        if (type == StrategyType::ProtectivePut || type == StrategyType::Collar)
            s *= (1.5 - risk_tolerance * 0.5);
        scored.push_back({type, s});
    }

    std::sort(scored.begin(), scored.end(),
              [](const Scored& a, const Scored& b) { return a.score > b.score; });

    std::vector<Strategy> strategies;
    // FIX: Use spot_price for pricing but round strikes to standard increments
    // to avoid close-price look-ahead bias. In a real system, strikes are set
    // relative to the previous close or current bid/ask, not exact close.
    double raw_spot = market.spot_price;
    double strike_increment = (raw_spot > 1000) ? 5.0 : (raw_spot > 100 ? 1.0 : 0.5);
    double spot = std::round(raw_spot / strike_increment) * strike_increment;
    double vol = market.implied_vol;
    double rate = market.risk_free_rate;
    double expiry = 30.0 / 365.0;  // 30 DTE default

    // Return top 3 strategies
    for (int i = 0; i < 3 && i < static_cast<int>(scored.size()); ++i) {
        switch (scored[i].type) {
            case StrategyType::CoveredCall:
                strategies.push_back(OptionsStrategies::createCoveredCall(
                    spot, spot * 1.05, expiry, vol, rate));
                break;
            case StrategyType::ProtectivePut:
                strategies.push_back(OptionsStrategies::createProtectivePut(
                    spot, spot * 0.95, expiry, vol, rate));
                break;
            case StrategyType::BullCallSpread:
                strategies.push_back(OptionsStrategies::createBullCallSpread(
                    spot, spot * 0.98, spot * 1.05, expiry, vol, rate));
                break;
            case StrategyType::BearPutSpread:
                strategies.push_back(OptionsStrategies::createBearPutSpread(
                    spot, spot * 1.02, spot * 0.95, expiry, vol, rate));
                break;
            case StrategyType::IronCondor:
                strategies.push_back(OptionsStrategies::createIronCondor(
                    spot, spot * 0.90, spot * 0.95, spot * 1.05, spot * 1.10,
                    expiry, vol, rate));
                break;
            case StrategyType::IronButterfly:
                strategies.push_back(OptionsStrategies::createIronButterfly(
                    spot, spot, spot * 0.05, expiry, vol, rate));
                break;
            case StrategyType::Straddle:
                strategies.push_back(OptionsStrategies::createStraddle(
                    spot, spot, expiry, vol, rate, true));
                break;
            case StrategyType::Strangle:
                strategies.push_back(OptionsStrategies::createStrangle(
                    spot, spot * 0.95, spot * 1.05, expiry, vol, rate, true));
                break;
            case StrategyType::Collar:
                strategies.push_back(OptionsStrategies::createCollar(
                    spot, spot * 0.95, spot * 1.05, expiry, vol, rate));
                break;
            case StrategyType::CalendarSpread:
                strategies.push_back(OptionsStrategies::createCalendarSpread(
                    spot, spot, expiry, expiry * 2, vol, rate));
                break;
            default:
                strategies.push_back(OptionsStrategies::createIronCondor(
                    spot, spot * 0.90, spot * 0.95, spot * 1.05, spot * 1.10,
                    expiry, vol, rate));
                break;
        }
    }
    return strategies;
}

StrategyManager::AllocationAdvice StrategyManager::getAdvice(
    const RegimeState& regime, const TradingSignal& signal,
    double current_drawdown) const {

    AllocationAdvice advice;
    advice.cash_pct = signal.target_cash_pct;
    advice.equity_pct = signal.target_equity_pct;
    advice.options_pct = signal.target_options_pct;

    switch (regime.current_regime) {
        case MarketRegime::BullQuiet:
            advice.recommended_strategies = {
                StrategyType::CoveredCall, StrategyType::IronCondor,
                StrategyType::BullCallSpread};
            advice.rationale = "Bull quiet regime: sell premium, use directional bullish strategies";
            break;
        case MarketRegime::BullVolatile:
            advice.recommended_strategies = {
                StrategyType::Collar, StrategyType::Straddle,
                StrategyType::CoveredCall};
            advice.rationale = "Bull volatile: hedge with collars, capture vol premium";
            break;
        case MarketRegime::BearQuiet:
            advice.recommended_strategies = {
                StrategyType::BearPutSpread, StrategyType::Collar,
                StrategyType::ProtectivePut};
            advice.rationale = "Bear quiet: defensive positioning, protect downside";
            break;
        case MarketRegime::BearVolatile:
            advice.recommended_strategies = {
                StrategyType::ProtectivePut, StrategyType::Collar};
            advice.rationale = "CRISIS: Maximum protection, reduce exposure immediately";
            advice.cash_pct = std::max(advice.cash_pct, 0.60);
            break;
        case MarketRegime::Transition:
            advice.recommended_strategies = {
                StrategyType::Straddle, StrategyType::Strangle,
                StrategyType::Collar};
            advice.rationale = "Transition: play volatility, maintain hedges";
            break;
    }

    // Drawdown override
    if (current_drawdown > 0.15) {
        advice.cash_pct = std::max(advice.cash_pct, 0.50);
        advice.rationale += " [DRAWDOWN ALERT: Increasing cash allocation]";
    }

    return advice;
}

} // namespace ste
