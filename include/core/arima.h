#pragma once
#include "core/types.h"
#include <deque>

namespace ste {

// ARIMA(p,d,q) + GARCH(1,1) model for realistic synthetic data generation
// Eliminates i.i.d. assumption bias -- captures autocorrelation, volatility
// clustering, and mean-reversion found in real market data.
struct ARIMAConfig {
    int p = 2;          // AR order (autoregressive lags)
    int d = 1;          // differencing order
    int q = 1;          // MA order (moving average of errors)

    // GARCH(1,1) for volatility clustering
    bool use_garch = true;
    double garch_omega = 0.00001;   // long-run variance weight
    double garch_alpha = 0.08;      // ARCH term (recent shock)
    double garch_beta  = 0.90;      // GARCH term (persistence)

    // Fat tails: Student-t degrees of freedom (0 = normal)
    double student_t_df = 5.0;

    // Leverage effect: asymmetric vol response to down moves
    double leverage_gamma = 0.15;
};

struct ARIMAState {
    std::deque<double> returns;     // recent returns (for AR)
    std::deque<double> errors;      // recent errors (for MA)
    double conditional_var;         // current GARCH variance
    double last_price;
};

class ARIMA {
public:
    explicit ARIMA(const ARIMAConfig& config = {}, unsigned seed = 42);

    // Fit AR and MA coefficients from historical returns
    void fit(const std::vector<double>& returns);

    // Generate one-step-ahead return
    double generateReturn(ARIMAState& state) const;

    // Generate full price path
    std::vector<double> generatePath(double initial_price, int num_steps,
                                      double base_drift = 0.0);

    // Generate market snapshots with ARIMA returns + GARCH vol
    std::vector<MarketSnapshot> generateMarketData(
        const MarketSnapshot& initial, int num_days,
        const std::vector<std::pair<int, MarketRegime>>& regime_schedule = {});

    // Regime-specific parameter sets
    void setRegimeParams(MarketRegime regime, double drift,
                          double ar1, double ar2, double ma1,
                          double garch_alpha, double garch_beta);

    const ARIMAConfig& config() const { return config_; }
    const std::vector<double>& arCoeffs() const { return ar_coeffs_; }
    const std::vector<double>& maCoeffs() const { return ma_coeffs_; }

private:
    ARIMAConfig config_;
    mutable std::mt19937 rng_;
    std::vector<double> ar_coeffs_;
    std::vector<double> ma_coeffs_;
    double intercept_ = 0.0;

    struct RegimeARIMA {
        double drift;
        double ar1, ar2, ma1;
        double garch_alpha, garch_beta;
    };
    std::map<MarketRegime, RegimeARIMA> regime_params_;

    double sampleInnovation() const;  // normal or student-t
    void initDefaultRegimeParams();
};

} // namespace ste
