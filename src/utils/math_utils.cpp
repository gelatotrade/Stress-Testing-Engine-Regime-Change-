#include "utils/math_utils.h"
#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace ste {
namespace math {

double norm_cdf(double x) {
    return 0.5 * std::erfc(-x * M_SQRT1_2);
}

double norm_pdf(double x) {
    static const double inv_sqrt_2pi = 0.3989422804014327;
    return inv_sqrt_2pi * std::exp(-0.5 * x * x);
}

// Rational approximation to inverse normal CDF (Beasley-Springer-Moro)
double norm_inv(double p) {
    if (p <= 0.0) return -1e10;
    if (p >= 1.0) return 1e10;

    static const double a[] = {
        -3.969683028665376e+01,  2.209460984245205e+02,
        -2.759285104469687e+02,  1.383577518672690e+02,
        -3.066479806614716e+01,  2.506628277459239e+00
    };
    static const double b[] = {
        -5.447609879822406e+01,  1.615858368580409e+02,
        -1.556989798598866e+02,  6.680131188771972e+01,
        -1.328068155288572e+01
    };
    static const double c[] = {
        -7.784894002430293e-03, -3.223964580411365e-01,
        -2.400758277161838e+00, -2.549732539343734e+00,
         4.374664141464968e+00,  2.938163982698783e+00
    };
    static const double d[] = {
         7.784695709041462e-03,  3.224671290700398e-01,
         2.445134137142996e+00,  3.754408661907416e+00
    };

    double q, r;
    if (p < 0.02425) {
        q = std::sqrt(-2.0 * std::log(p));
        return (((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    } else if (p <= 0.97575) {
        q = p - 0.5;
        r = q * q;
        return (((((a[0]*r+a[1])*r+a[2])*r+a[3])*r+a[4])*r+a[5])*q /
               (((((b[0]*r+b[1])*r+b[2])*r+b[3])*r+b[4])*r+1.0);
    } else {
        q = std::sqrt(-2.0 * std::log(1.0 - p));
        return -(((((c[0]*q+c[1])*q+c[2])*q+c[3])*q+c[4])*q+c[5]) /
                 ((((d[0]*q+d[1])*q+d[2])*q+d[3])*q+1.0);
    }
}

double mean(const std::vector<double>& v) {
    if (v.empty()) return 0.0;
    return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
}

double variance(const std::vector<double>& v) {
    if (v.size() < 2) return 0.0;
    double m = mean(v);
    double sum = 0.0;
    for (double x : v) sum += (x - m) * (x - m);
    return sum / (v.size() - 1);
}

double stddev(const std::vector<double>& v) {
    return std::sqrt(variance(v));
}

double covariance(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size() || a.size() < 2) return 0.0;
    double ma = mean(a), mb = mean(b);
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        sum += (a[i] - ma) * (b[i] - mb);
    return sum / (a.size() - 1);
}

double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    double sa = stddev(a), sb = stddev(b);
    if (sa < 1e-15 || sb < 1e-15) return 0.0;
    return covariance(a, b) / (sa * sb);
}

double percentile(std::vector<double> v, double pct) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    double idx = pct * (v.size() - 1);
    size_t lo = static_cast<size_t>(idx);
    size_t hi = std::min(lo + 1, v.size() - 1);
    double frac = idx - lo;
    return v[lo] * (1.0 - frac) + v[hi] * frac;
}

double max_drawdown(const std::vector<double>& cumulative_returns) {
    if (cumulative_returns.empty()) return 0.0;
    double peak = cumulative_returns[0];
    double max_dd = 0.0;
    for (double r : cumulative_returns) {
        if (r > peak) peak = r;
        double dd = (peak - r) / std::max(peak, 1e-10);
        if (dd > max_dd) max_dd = dd;
    }
    return max_dd;
}

double ewma(const std::vector<double>& v, double lambda) {
    if (v.empty()) return 0.0;
    double result = v[0];
    for (size_t i = 1; i < v.size(); ++i) {
        result = lambda * result + (1.0 - lambda) * v[i];
    }
    return result;
}

std::vector<double> rolling_mean(const std::vector<double>& v, int window) {
    std::vector<double> result;
    if (v.empty() || window <= 0) return result;
    double sum = 0.0;
    for (size_t i = 0; i < v.size(); ++i) {
        sum += v[i];
        if (static_cast<int>(i) >= window) sum -= v[i - window];
        if (static_cast<int>(i) >= window - 1)
            result.push_back(sum / window);
    }
    return result;
}

std::vector<double> rolling_std(const std::vector<double>& v, int window) {
    std::vector<double> result;
    if (v.empty() || window <= 1) return result;
    for (size_t i = window - 1; i < v.size(); ++i) {
        std::vector<double> slice(v.begin() + i - window + 1, v.begin() + i + 1);
        result.push_back(stddev(slice));
    }
    return result;
}

double lerp(double a, double b, double t) {
    return a + t * (b - a);
}

double clamp(double x, double lo, double hi) {
    return std::max(lo, std::min(hi, x));
}

} // namespace math
} // namespace ste
