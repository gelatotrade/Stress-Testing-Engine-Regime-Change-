#pragma once
#include <string>
#include <vector>
#include <map>
#include <cmath>
#include <chrono>
#include <functional>
#include <memory>
#include <array>
#include <numeric>
#include <algorithm>
#include <random>
#include <mutex>
#include <atomic>
#include <optional>

namespace ste {

// ============================================================
// Timeframe Configuration
// ============================================================

enum class Timeframe {
    Daily,      // 1 bar = 1 trading day   (252 bars/year)
    Hourly,     // 1 bar = 1 hour          (252 * 6.5 = 1638 bars/year)
    Minute      // 1 bar = 1 minute        (252 * 6.5 * 60 = 98280 bars/year)
};

// Returns the number of bars in one trading year for a given timeframe
inline double periodsPerYear(Timeframe tf) {
    switch (tf) {
        case Timeframe::Hourly:  return 252.0 * 6.5;          // 1638
        case Timeframe::Minute:  return 252.0 * 6.5 * 60.0;   // 98280
        case Timeframe::Daily:
        default:                 return 252.0;
    }
}

// Returns dt as fraction of a year for one bar
inline double timeframeDt(Timeframe tf) {
    return 1.0 / periodsPerYear(tf);
}

// Returns the number of bars equivalent to N trading days
inline int daysToPeriodsCount(int days, Timeframe tf) {
    switch (tf) {
        case Timeframe::Hourly:  return days * static_cast<int>(6.5);
        case Timeframe::Minute:  return days * static_cast<int>(6.5 * 60);
        case Timeframe::Daily:
        default:                 return days;
    }
}

// Returns a human-readable label for the timeframe
inline const char* timeframeName(Timeframe tf) {
    switch (tf) {
        case Timeframe::Hourly:  return "Hourly";
        case Timeframe::Minute:  return "Minute";
        case Timeframe::Daily:
        default:                 return "Daily";
    }
}

// ============================================================
// Market & Option Types
// ============================================================

enum class OptionType { Call, Put };
enum class ExerciseStyle { European, American };

struct Option {
    OptionType type;
    ExerciseStyle style = ExerciseStyle::European;
    double strike;
    double expiry;         // years to expiration
    double premium;        // price paid/received
    int quantity;           // positive=long, negative=short
    double underlying_price = 0.0;
};

struct Greeks {
    double delta = 0.0;
    double gamma = 0.0;
    double theta = 0.0;
    double vega  = 0.0;
    double rho   = 0.0;
};

struct MarketSnapshot {
    double timestamp;       // epoch seconds
    double spot_price;
    double risk_free_rate;
    double dividend_yield;
    double implied_vol;
    double realized_vol;
    double vix;
    double sp500_level;
    double volume;
    double put_call_ratio;
    double credit_spread;
    double yield_curve_slope;
};

// ============================================================
// Portfolio & Strategy Types
// ============================================================

enum class StrategyType {
    CoveredCall,
    ProtectivePut,
    BullCallSpread,
    BearPutSpread,
    IronCondor,
    IronButterfly,
    Straddle,
    Strangle,
    CalendarSpread,
    RatioSpread,
    Collar,
    JadeLizard,
    Custom
};

struct StrategyLeg {
    Option option;
    Greeks greeks;
    double market_value = 0.0;
};

struct Strategy {
    StrategyType type;
    std::string name;
    std::vector<StrategyLeg> legs;
    double underlying_shares = 0.0;  // for covered strategies
    double max_profit = 0.0;
    double max_loss = 0.0;
    double breakeven_lower = 0.0;
    double breakeven_upper = 0.0;
};

struct PortfolioState {
    double total_value;
    double cash_allocation;     // fraction 0-1
    double equity_allocation;
    double options_allocation;
    double total_delta;
    double total_gamma;
    double total_theta;
    double total_vega;
    double var_95;              // Value at Risk 95%
    double cvar_95;             // Conditional VaR
    double sharpe_ratio;
    double max_drawdown;
    double benchmark_return;    // SP500 cumulative
    double portfolio_return;    // our cumulative
};

// ============================================================
// Regime Detection Types
// ============================================================

enum class MarketRegime {
    BullQuiet,      // low vol uptrend
    BullVolatile,   // high vol uptrend
    BearQuiet,      // low vol downtrend
    BearVolatile,   // high vol downtrend (crisis)
    Transition,     // regime change in progress
    COUNT = 5
};

struct RegimeState {
    MarketRegime current_regime;
    std::array<double, 5> regime_probabilities;
    double transition_probability;    // prob of regime change
    double crisis_probability;        // prob of entering crisis
    int regime_duration_days;
    double confidence;
};

// ============================================================
// Signal Types
// ============================================================

enum class SignalType {
    StrongBuy,
    Buy,
    Hold,
    ReduceRisk,
    GoToCash,
    Crisis
};

struct TradingSignal {
    SignalType signal;
    double confidence;
    double target_cash_pct;     // recommended cash allocation
    double target_equity_pct;
    double target_options_pct;
    std::string reason;
    double timestamp;
};

// ============================================================
// Stress Test Types
// ============================================================

struct StressScenario {
    std::string name;
    double spot_shock;          // percentage change
    double vol_shock;           // absolute change in vol
    double rate_shock;          // absolute change in rates
    double time_shock;          // days forward
    double correlation_shock;
    double liquidity_shock;     // bid-ask spread multiplier
};

struct StressResult {
    std::string scenario_name;
    double portfolio_pnl;
    double portfolio_pnl_pct;
    double worst_leg_pnl;
    std::string worst_leg_name;
    Greeks portfolio_greeks;
    double var_impact;
    double margin_impact;
};

// ============================================================
// 3D Visualization Data
// ============================================================

struct Point3D {
    double x, y, z;
    double color_r, color_g, color_b, color_a;
};

struct Surface3D {
    std::string label;
    std::vector<std::vector<Point3D>> grid;  // [row][col]
    int rows, cols;
};

struct VisualizationFrame {
    double timestamp;
    MarketRegime regime;
    std::vector<Surface3D> surfaces;
    PortfolioState portfolio;
    TradingSignal signal;
    std::vector<StressResult> stress_results;
    RegimeState regime_state;
};

} // namespace ste
