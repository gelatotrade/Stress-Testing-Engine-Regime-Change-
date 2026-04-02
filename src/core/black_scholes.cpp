#include "core/black_scholes.h"
#include "utils/math_utils.h"
#include <cmath>
#include <algorithm>

namespace ste {

double BlackScholes::norm_cdf(double x) { return math::norm_cdf(x); }
double BlackScholes::norm_pdf(double x) { return math::norm_pdf(x); }

double BlackScholes::d1(double S, double K, double T, double r, double q, double sigma) {
    if (T <= 0.0 || sigma <= 0.0) return (S > K) ? 10.0 : -10.0;
    return (std::log(S / K) + (r - q + 0.5 * sigma * sigma) * T) / (sigma * std::sqrt(T));
}

double BlackScholes::d2(double S, double K, double T, double r, double q, double sigma) {
    return d1(S, K, T, r, q, sigma) - sigma * std::sqrt(T);
}

double BlackScholes::price(const Option& opt, double spot, double vol,
                            double rate, double dividend) {
    double T = opt.expiry;
    double K = opt.strike;

    if (T <= 0.0) {
        // At expiry
        if (opt.type == OptionType::Call)
            return std::max(spot - K, 0.0);
        else
            return std::max(K - spot, 0.0);
    }

    double d1_val = d1(spot, K, T, rate, dividend, vol);
    double d2_val = d2(spot, K, T, rate, dividend, vol);

    if (opt.type == OptionType::Call) {
        return spot * std::exp(-dividend * T) * norm_cdf(d1_val) -
               K * std::exp(-rate * T) * norm_cdf(d2_val);
    } else {
        return K * std::exp(-rate * T) * norm_cdf(-d2_val) -
               spot * std::exp(-dividend * T) * norm_cdf(-d1_val);
    }
}

double BlackScholes::delta(const Option& opt, double spot, double vol,
                            double rate, double dividend) {
    if (opt.expiry <= 0.0) {
        if (opt.type == OptionType::Call) return (spot > opt.strike) ? 1.0 : 0.0;
        else return (spot < opt.strike) ? -1.0 : 0.0;
    }
    double d1_val = d1(spot, opt.strike, opt.expiry, rate, dividend, vol);
    double disc = std::exp(-dividend * opt.expiry);
    if (opt.type == OptionType::Call)
        return disc * norm_cdf(d1_val);
    else
        return disc * (norm_cdf(d1_val) - 1.0);
}

double BlackScholes::gamma(const Option& opt, double spot, double vol,
                            double rate, double dividend) {
    if (opt.expiry <= 0.0 || vol <= 0.0) return 0.0;
    double d1_val = d1(spot, opt.strike, opt.expiry, rate, dividend, vol);
    return std::exp(-dividend * opt.expiry) * norm_pdf(d1_val) /
           (spot * vol * std::sqrt(opt.expiry));
}

double BlackScholes::theta(const Option& opt, double spot, double vol,
                            double rate, double dividend) {
    if (opt.expiry <= 0.0) return 0.0;
    double T = opt.expiry;
    double K = opt.strike;
    double d1_val = d1(spot, K, T, rate, dividend, vol);
    double d2_val = d2(spot, K, T, rate, dividend, vol);

    double term1 = -spot * std::exp(-dividend * T) * norm_pdf(d1_val) * vol / (2.0 * std::sqrt(T));

    if (opt.type == OptionType::Call) {
        return (term1 - rate * K * std::exp(-rate * T) * norm_cdf(d2_val)
                + dividend * spot * std::exp(-dividend * T) * norm_cdf(d1_val)) / 252.0;
    } else {
        return (term1 + rate * K * std::exp(-rate * T) * norm_cdf(-d2_val)
                - dividend * spot * std::exp(-dividend * T) * norm_cdf(-d1_val)) / 252.0;
    }
}

double BlackScholes::vega(const Option& opt, double spot, double vol,
                           double rate, double dividend) {
    if (opt.expiry <= 0.0) return 0.0;
    double d1_val = d1(spot, opt.strike, opt.expiry, rate, dividend, vol);
    return spot * std::exp(-dividend * opt.expiry) * norm_pdf(d1_val) *
           std::sqrt(opt.expiry) / 100.0;  // per 1% vol change
}

double BlackScholes::rho(const Option& opt, double spot, double vol,
                          double rate, double dividend) {
    if (opt.expiry <= 0.0) return 0.0;
    double d2_val = d2(spot, opt.strike, opt.expiry, rate, dividend, vol);
    if (opt.type == OptionType::Call)
        return opt.strike * opt.expiry * std::exp(-rate * opt.expiry) *
               norm_cdf(d2_val) / 100.0;
    else
        return -opt.strike * opt.expiry * std::exp(-rate * opt.expiry) *
               norm_cdf(-d2_val) / 100.0;
}

Greeks BlackScholes::computeGreeks(const Option& opt, double spot, double vol,
                                    double rate, double dividend) {
    return {
        delta(opt, spot, vol, rate, dividend) * opt.quantity,
        gamma(opt, spot, vol, rate, dividend) * opt.quantity,
        theta(opt, spot, vol, rate, dividend) * opt.quantity,
        vega(opt, spot, vol, rate, dividend) * opt.quantity,
        rho(opt, spot, vol, rate, dividend) * opt.quantity
    };
}

double BlackScholes::impliedVol(const Option& opt, double market_price,
                                 double spot, double rate, double dividend,
                                 double initial_guess, int max_iter) {
    double vol = initial_guess;
    for (int i = 0; i < max_iter; ++i) {
        double p = price(opt, spot, vol, rate, dividend);
        double v = vega(opt, spot, vol, rate, dividend) * 100.0;  // undo /100
        if (std::abs(v) < 1e-12) break;
        double diff = p - market_price;
        if (std::abs(diff) < 1e-8) break;
        vol -= diff / v;
        vol = std::max(0.01, std::min(5.0, vol));
    }
    return vol;
}

double BlackScholes::positionPnL(const Option& opt, double new_spot, double new_vol,
                                  double new_time, double rate, double dividend) {
    Option shifted = opt;
    shifted.expiry = new_time;
    double current_value = price(shifted, new_spot, new_vol, rate, dividend);
    double entry_value = opt.premium;
    return (current_value - entry_value) * opt.quantity;
}

} // namespace ste
