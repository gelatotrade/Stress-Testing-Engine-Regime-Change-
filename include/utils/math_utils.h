#pragma once
#include <vector>
#include <cmath>

namespace ste {

namespace math {

    double norm_cdf(double x);
    double norm_pdf(double x);
    double norm_inv(double p);

    double mean(const std::vector<double>& v);
    double stddev(const std::vector<double>& v);
    double variance(const std::vector<double>& v);
    double covariance(const std::vector<double>& a, const std::vector<double>& b);
    double correlation(const std::vector<double>& a, const std::vector<double>& b);

    double percentile(std::vector<double> v, double pct);
    double max_drawdown(const std::vector<double>& cumulative_returns);

    // Exponentially weighted moving average
    double ewma(const std::vector<double>& v, double lambda = 0.94);

    // Rolling statistics
    std::vector<double> rolling_mean(const std::vector<double>& v, int window);
    std::vector<double> rolling_std(const std::vector<double>& v, int window);

    // Linear interpolation
    double lerp(double a, double b, double t);

    // Clamp
    double clamp(double x, double lo, double hi);

} // namespace math
} // namespace ste
