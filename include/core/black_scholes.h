#pragma once
#include "core/types.h"

namespace ste {

class BlackScholes {
public:
    // Option pricing
    static double price(const Option& opt, double spot, double vol,
                        double rate, double dividend = 0.0);

    // Individual Greeks
    static double delta(const Option& opt, double spot, double vol,
                        double rate, double dividend = 0.0);
    static double gamma(const Option& opt, double spot, double vol,
                        double rate, double dividend = 0.0);
    static double theta(const Option& opt, double spot, double vol,
                        double rate, double dividend = 0.0);
    static double vega(const Option& opt, double spot, double vol,
                       double rate, double dividend = 0.0);
    static double rho(const Option& opt, double spot, double vol,
                      double rate, double dividend = 0.0);

    // All Greeks at once
    static Greeks computeGreeks(const Option& opt, double spot, double vol,
                                double rate, double dividend = 0.0);

    // Implied volatility via Newton-Raphson
    static double impliedVol(const Option& opt, double market_price,
                             double spot, double rate, double dividend = 0.0,
                             double initial_guess = 0.25, int max_iter = 100);

    // P&L for a position at given spot/vol/time
    static double positionPnL(const Option& opt, double new_spot, double new_vol,
                              double new_time, double rate, double dividend = 0.0);

private:
    static double d1(double S, double K, double T, double r, double q, double sigma);
    static double d2(double S, double K, double T, double r, double q, double sigma);
    static double norm_cdf(double x);
    static double norm_pdf(double x);
};

} // namespace ste
