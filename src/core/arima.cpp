#include "core/arima.h"
#include "utils/math_utils.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace ste {

ARIMA::ARIMA(const ARIMAConfig& config, unsigned seed)
    : config_(config), rng_(seed) {
    ar_coeffs_.resize(config.p, 0.0);
    ma_coeffs_.resize(config.q, 0.0);
    initDefaultRegimeParams();
}

void ARIMA::initDefaultRegimeParams() {
    // Empirically calibrated from S&P 500 data per regime
    // AR1/AR2 capture momentum/mean-reversion, MA1 captures shock persistence
    // GARCH params capture vol clustering per regime
    regime_params_[MarketRegime::BullQuiet] = {
        0.0004,     // drift: ~10% annualized
        0.03,       // ar1: slight momentum
        -0.02,      // ar2: weak mean-reversion
        -0.05,      // ma1: small negative (vol dampening)
        0.05,       // garch_alpha: low shock sensitivity
        0.93        // garch_beta: high persistence
    };
    regime_params_[MarketRegime::BullVolatile] = {
        0.0006,     // drift: stronger
        0.05,       // ar1: momentum
        -0.03,      // ar2: mean-reversion
        -0.08,      // ma1: shock amplification
        0.10,       // garch_alpha: higher shock sensitivity
        0.87        // garch_beta: slightly less persistent
    };
    regime_params_[MarketRegime::BearQuiet] = {
        -0.0003,    // drift: slow decline
        -0.02,      // ar1: negative momentum
        0.01,       // ar2
        0.03,       // ma1: error persistence
        0.07,       // garch_alpha
        0.91        // garch_beta
    };
    regime_params_[MarketRegime::BearVolatile] = {
        -0.0015,    // drift: sharp decline
        -0.05,      // ar1: panic selling momentum
        0.02,       // ar2: oversold bounces
        0.10,       // ma1: high shock persistence
        0.15,       // garch_alpha: extreme shock sensitivity
        0.82        // garch_beta: vol regime shifts fast
    };
    regime_params_[MarketRegime::Transition] = {
        0.0000,     // drift: flat
        0.00,       // ar1: no direction
        -0.01,      // ar2
        0.02,       // ma1
        0.09,       // garch_alpha
        0.89        // garch_beta
    };
}

void ARIMA::setRegimeParams(MarketRegime regime, double drift,
                             double ar1, double ar2, double ma1,
                             double garch_alpha, double garch_beta) {
    regime_params_[regime] = {drift, ar1, ar2, ma1, garch_alpha, garch_beta};
}

double ARIMA::sampleInnovation() const {
    if (config_.student_t_df > 0 && config_.student_t_df < 100) {
        // Student-t via ratio of normal and chi-squared
        std::normal_distribution<double> norm(0.0, 1.0);
        std::gamma_distribution<double> gamma(config_.student_t_df / 2.0,
                                               2.0 / config_.student_t_df);
        double z = norm(rng_);
        double v = gamma(rng_);
        return z / std::sqrt(v);
    }
    return std::normal_distribution<double>(0.0, 1.0)(rng_);
}

void ARIMA::fit(const std::vector<double>& returns) {
    if (returns.size() < static_cast<size_t>(config_.p + config_.q + 10)) return;

    int n = static_cast<int>(returns.size());

    // Yule-Walker estimation for AR coefficients
    intercept_ = math::mean(returns);

    // Compute autocorrelations
    std::vector<double> centered(n);
    for (int i = 0; i < n; ++i) centered[i] = returns[i] - intercept_;

    double var = math::variance(returns);
    if (var < 1e-15) return;

    std::vector<double> acf(config_.p + 1, 0.0);
    for (int lag = 0; lag <= config_.p; ++lag) {
        double sum = 0.0;
        for (int i = lag; i < n; ++i)
            sum += centered[i] * centered[i - lag];
        acf[lag] = sum / n;
    }

    // Solve Yule-Walker (Levinson-Durbin for AR)
    if (config_.p >= 1 && acf[0] > 1e-15) {
        ar_coeffs_.resize(config_.p, 0.0);

        if (config_.p == 1) {
            ar_coeffs_[0] = acf[1] / acf[0];
        } else if (config_.p == 2) {
            double r1 = acf[1] / acf[0];
            double r2 = acf[2] / acf[0];
            double denom = 1.0 - r1 * r1;
            if (std::abs(denom) > 1e-12) {
                ar_coeffs_[0] = (r1 - r2 * r1) / denom;
                ar_coeffs_[1] = (r2 - r1 * r1) / denom;
            }
        }

        // Ensure stationarity: |coefficients| bounded
        for (auto& c : ar_coeffs_)
            c = math::clamp(c, -0.95, 0.95);
    }

    // Estimate MA coefficients from residuals
    if (config_.q >= 1) {
        ma_coeffs_.resize(config_.q, 0.0);
        std::vector<double> residuals(n, 0.0);
        for (int i = config_.p; i < n; ++i) {
            double predicted = intercept_;
            for (int j = 0; j < config_.p; ++j)
                predicted += ar_coeffs_[j] * (returns[i - 1 - j] - intercept_);
            residuals[i] = returns[i] - predicted;
        }

        // First-order MA from residual autocorrelation
        double res_var = 0.0, res_acf1 = 0.0;
        for (int i = config_.p + 1; i < n; ++i) {
            res_var += residuals[i] * residuals[i];
            res_acf1 += residuals[i] * residuals[i - 1];
        }
        if (res_var > 1e-15) {
            ma_coeffs_[0] = math::clamp(res_acf1 / res_var, -0.95, 0.95);
        }
    }
}

double ARIMA::generateReturn(ARIMAState& state) const {
    double innovation = sampleInnovation();

    // GARCH(1,1) conditional variance
    double sigma = std::sqrt(std::max(state.conditional_var, 1e-10));

    // AR component
    double ar_part = 0.0;
    for (int j = 0; j < config_.p && j < static_cast<int>(state.returns.size()); ++j)
        ar_part += ar_coeffs_[j] * state.returns[state.returns.size() - 1 - j];

    // MA component
    double ma_part = 0.0;
    for (int j = 0; j < config_.q && j < static_cast<int>(state.errors.size()); ++j)
        ma_part += ma_coeffs_[j] * state.errors[state.errors.size() - 1 - j];

    double error = sigma * innovation;
    double ret = intercept_ + ar_part + ma_part + error;

    // Update GARCH variance
    double leverage = 0.0;
    if (config_.leverage_gamma > 0 && error < 0)
        leverage = config_.leverage_gamma * error * error;

    state.conditional_var = config_.garch_omega
        + config_.garch_alpha * (error * error + leverage)
        + config_.garch_beta * state.conditional_var;

    // Clamp to avoid explosion
    state.conditional_var = math::clamp(state.conditional_var, 1e-8, 0.01);

    // Update buffers
    state.returns.push_back(ret);
    state.errors.push_back(error);
    if (state.returns.size() > 50) state.returns.pop_front();
    if (state.errors.size() > 50) state.errors.pop_front();

    return ret;
}

std::vector<double> ARIMA::generatePath(double initial_price, int num_steps,
                                          double base_drift) {
    std::vector<double> path;
    path.reserve(num_steps + 1);
    path.push_back(initial_price);

    ARIMAState state;
    state.conditional_var = 0.0002;  // initial daily variance (~1.4% vol)
    state.last_price = initial_price;

    intercept_ = base_drift;

    for (int i = 0; i < num_steps; ++i) {
        double ret = generateReturn(state);
        double price = path.back() * std::exp(ret);
        path.push_back(price);
    }

    return path;
}

std::vector<MarketSnapshot> ARIMA::generateMarketData(
    const MarketSnapshot& initial, int num_days,
    const std::vector<std::pair<int, MarketRegime>>& regime_schedule) {

    std::vector<MarketSnapshot> data;
    data.reserve(num_days);
    data.push_back(initial);

    ARIMAState state;
    state.conditional_var = initial.implied_vol * initial.implied_vol / 252.0;
    state.last_price = initial.spot_price;

    // Build regime timeline
    std::vector<MarketRegime> daily_regime(num_days, MarketRegime::BullQuiet);
    for (const auto& [start_day, regime] : regime_schedule) {
        for (int d = start_day; d < num_days; ++d)
            daily_regime[d] = regime;
    }

    std::normal_distribution<double> small_noise(0.0, 0.02);

    for (int d = 1; d < num_days; ++d) {
        MarketRegime regime = daily_regime[d];

        // Switch ARIMA params per regime
        auto it = regime_params_.find(regime);
        if (it != regime_params_.end()) {
            const auto& rp = it->second;
            intercept_ = rp.drift;
            if (ar_coeffs_.size() >= 1) ar_coeffs_[0] = rp.ar1;
            if (ar_coeffs_.size() >= 2) ar_coeffs_[1] = rp.ar2;
            if (ma_coeffs_.size() >= 1) ma_coeffs_[0] = rp.ma1;
            config_.garch_alpha = rp.garch_alpha;
            config_.garch_beta = rp.garch_beta;
        }

        double ret = generateReturn(state);

        MarketSnapshot snap = data.back();
        snap.timestamp += 86400.0;
        snap.spot_price *= std::exp(ret);
        snap.sp500_level = snap.spot_price;

        // Realized vol from GARCH
        snap.realized_vol = std::sqrt(state.conditional_var * 252.0);
        snap.implied_vol = snap.realized_vol * (1.0 + 0.1 * small_noise(rng_));
        snap.vix = snap.implied_vol * 100.0;
        snap.vix = math::clamp(snap.vix, 9.0, 85.0);

        // Regime-dependent market features
        double regime_stress = (regime == MarketRegime::BearVolatile) ? 1.0 :
                               (regime == MarketRegime::BearQuiet) ? 0.3 :
                               (regime == MarketRegime::Transition) ? 0.15 : 0.0;

        snap.put_call_ratio = 0.7 + regime_stress * 0.5 + small_noise(rng_) * 0.05;
        snap.credit_spread = 1.0 + regime_stress * 3.0 + small_noise(rng_) * 0.1;
        snap.credit_spread = std::max(0.3, snap.credit_spread);
        snap.yield_curve_slope = 1.5 - regime_stress * 2.0 + small_noise(rng_) * 0.1;
        snap.volume = 3e9 * (1.0 + regime_stress * 0.5 + small_noise(rng_) * 0.1);
        snap.risk_free_rate = math::clamp(
            snap.risk_free_rate + small_noise(rng_) * 0.0005, 0.005, 0.08);
        snap.dividend_yield = 0.015;

        data.push_back(snap);
    }

    return data;
}

} // namespace ste
