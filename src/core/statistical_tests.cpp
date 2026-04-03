#include "core/statistical_tests.h"
#include "utils/math_utils.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <sstream>

namespace ste {

// ================================================================
// Sharpe Ratio T-Test (Lo 2002 adjusted)
// ================================================================
HypothesisTestResult StatisticalTests::sharpeRatioTTest(
    const std::vector<double>& returns, double risk_free_rate) {
    HypothesisTestResult r;
    r.test_name = "Sharpe Ratio t-test (Lo 2002)";

    int n = static_cast<int>(returns.size());
    if (n < 10) {
        r.p_value = 1.0; r.interpretation = "Insufficient data"; return r;
    }

    double rf_daily = risk_free_rate / 252.0;
    std::vector<double> excess(n);
    for (int i = 0; i < n; ++i) excess[i] = returns[i] - rf_daily;

    double mean_ex = math::mean(excess);
    double std_ex = math::stddev(excess);
    if (std_ex < 1e-15) {
        r.p_value = 1.0; r.interpretation = "Zero variance"; return r;
    }

    double daily_sharpe = mean_ex / std_ex;

    // Autocorrelation correction (Lo 2002)
    // Compute first k autocorrelations
    int max_lag = std::min(10, n / 5);
    double var_ex = math::variance(excess);
    double correction = 1.0;
    for (int lag = 1; lag <= max_lag; ++lag) {
        double acf = 0.0;
        for (int i = lag; i < n; ++i)
            acf += (excess[i] - mean_ex) * (excess[i - lag] - mean_ex);
        acf /= (n * var_ex);
        correction += 2.0 * (1.0 - static_cast<double>(lag) / (max_lag + 1)) * acf;
    }
    correction = std::max(correction, 0.1);

    double se = std::sqrt(correction / n);
    r.test_statistic = daily_sharpe / se;

    // p-value from normal approximation (two-sided)
    r.p_value = 2.0 * (1.0 - math::norm_cdf(std::abs(r.test_statistic)));

    // Annualized confidence interval
    double ann_sharpe = daily_sharpe * std::sqrt(252.0 / correction);
    double ann_se = se * std::sqrt(252.0);
    r.confidence_interval_lower = ann_sharpe - 1.96 * ann_se;
    r.confidence_interval_upper = ann_sharpe + 1.96 * ann_se;

    r.significant_at_05 = r.p_value < 0.05;
    r.significant_at_01 = r.p_value < 0.01;

    std::ostringstream ss;
    ss << "Annualized Sharpe = " << std::fixed;
    ss.precision(3);
    ss << ann_sharpe << " [" << r.confidence_interval_lower << ", "
       << r.confidence_interval_upper << "] ";
    ss << (r.significant_at_05 ? "SIGNIFICANT" : "NOT significant") << " at 5%";
    r.interpretation = ss.str();

    return r;
}

// ================================================================
// Probabilistic Sharpe Ratio (Bailey & Lopez de Prado 2012)
// ================================================================
HypothesisTestResult StatisticalTests::probabilisticSharpe(
    const std::vector<double>& returns, double benchmark_sharpe,
    double risk_free_rate) {
    HypothesisTestResult r;
    r.test_name = "Probabilistic Sharpe Ratio (PSR)";

    int n = static_cast<int>(returns.size());
    if (n < 10) {
        r.p_value = 1.0; r.interpretation = "Insufficient data"; return r;
    }

    double rf = risk_free_rate / 252.0;
    std::vector<double> excess(n);
    for (int i = 0; i < n; ++i) excess[i] = returns[i] - rf;

    double mean_ex = math::mean(excess);
    double std_ex = math::stddev(excess);
    if (std_ex < 1e-15) { r.p_value = 1.0; return r; }

    double sr = mean_ex / std_ex;
    double sr_star = benchmark_sharpe / std::sqrt(252.0);  // daily benchmark

    // Skewness and kurtosis of returns
    double m3 = 0, m4 = 0;
    for (double x : excess) {
        double z = (x - mean_ex) / std_ex;
        m3 += z * z * z;
        m4 += z * z * z * z;
    }
    double skew = m3 / n;
    double kurt = m4 / n - 3.0;  // excess kurtosis

    // PSR formula
    double se_sr = std::sqrt((1.0 - skew * sr + (kurt / 4.0) * sr * sr) / n);

    if (se_sr < 1e-15) { r.p_value = 0.5; return r; }

    r.test_statistic = (sr - sr_star) / se_sr;
    r.p_value = 1.0 - math::norm_cdf(r.test_statistic);

    double ann_sr = sr * std::sqrt(252.0);
    r.confidence_interval_lower = (sr - 1.96 * se_sr) * std::sqrt(252.0);
    r.confidence_interval_upper = (sr + 1.96 * se_sr) * std::sqrt(252.0);
    r.significant_at_05 = r.p_value < 0.05;
    r.significant_at_01 = r.p_value < 0.01;

    std::ostringstream ss;
    ss << "PSR = " << std::fixed;
    ss.precision(1);
    ss << (1.0 - r.p_value) * 100.0 << "% probability that true Sharpe > "
       << benchmark_sharpe << ". ";
    ss << "Skew=" << std::fixed;
    ss.precision(2);
    ss << skew << " Kurt=" << kurt;
    r.interpretation = ss.str();

    return r;
}

// ================================================================
// Bootstrap Test
// ================================================================
HypothesisTestResult StatisticalTests::bootstrapTest(
    const std::vector<double>& returns,
    const std::function<double(const std::vector<double>&)>& statistic,
    const std::string& name, int num_bootstrap, double confidence,
    unsigned seed) {
    HypothesisTestResult r;
    r.test_name = name;

    int n = static_cast<int>(returns.size());
    if (n < 5) { r.p_value = 1.0; return r; }

    double observed = statistic(returns);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, n - 1);

    std::vector<double> boot_stats;
    boot_stats.reserve(num_bootstrap);

    for (int b = 0; b < num_bootstrap; ++b) {
        std::vector<double> sample(n);
        for (int i = 0; i < n; ++i) sample[i] = returns[dist(rng)];
        boot_stats.push_back(statistic(sample));
    }

    std::sort(boot_stats.begin(), boot_stats.end());

    double alpha = (1.0 - confidence) / 2.0;
    int lo_idx = static_cast<int>(alpha * num_bootstrap);
    int hi_idx = static_cast<int>((1.0 - alpha) * num_bootstrap);
    r.confidence_interval_lower = boot_stats[lo_idx];
    r.confidence_interval_upper = boot_stats[hi_idx];

    // p-value: fraction of bootstrap samples with opposite sign
    int count_below_zero = 0;
    for (double s : boot_stats)
        if (s <= 0) count_below_zero++;
    r.p_value = static_cast<double>(count_below_zero) / num_bootstrap;
    if (observed < 0) r.p_value = 1.0 - r.p_value;

    r.test_statistic = observed;
    r.significant_at_05 = r.p_value < 0.05;
    r.significant_at_01 = r.p_value < 0.01;

    std::ostringstream ss;
    ss << "Observed=" << std::fixed;
    ss.precision(4);
    ss << observed << " 95% CI=[" << r.confidence_interval_lower
       << ", " << r.confidence_interval_upper << "]";
    r.interpretation = ss.str();

    return r;
}

// ================================================================
// Circular Block Bootstrap
// ================================================================
HypothesisTestResult StatisticalTests::blockBootstrap(
    const std::vector<double>& returns,
    const std::function<double(const std::vector<double>&)>& statistic,
    const std::string& name, int block_size, int num_bootstrap, unsigned seed) {
    HypothesisTestResult r;
    r.test_name = name;

    int n = static_cast<int>(returns.size());
    if (n < block_size * 2) { r.p_value = 1.0; return r; }

    double observed = statistic(returns);
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> dist(0, n - 1);

    int num_blocks = (n + block_size - 1) / block_size;
    std::vector<double> boot_stats;
    boot_stats.reserve(num_bootstrap);

    for (int b = 0; b < num_bootstrap; ++b) {
        std::vector<double> sample;
        sample.reserve(n);
        while (static_cast<int>(sample.size()) < n) {
            int start = dist(rng);
            for (int j = 0; j < block_size && static_cast<int>(sample.size()) < n; ++j)
                sample.push_back(returns[(start + j) % n]);  // circular
        }
        boot_stats.push_back(statistic(sample));
    }

    std::sort(boot_stats.begin(), boot_stats.end());
    r.confidence_interval_lower = boot_stats[static_cast<int>(0.025 * num_bootstrap)];
    r.confidence_interval_upper = boot_stats[static_cast<int>(0.975 * num_bootstrap)];

    int below = 0;
    for (double s : boot_stats)
        if (s <= 0) below++;
    r.p_value = static_cast<double>(below) / num_bootstrap;
    if (observed < 0) r.p_value = 1.0 - r.p_value;

    r.test_statistic = observed;
    r.significant_at_05 = r.p_value < 0.05;
    r.significant_at_01 = r.p_value < 0.01;

    std::ostringstream ss;
    ss << "Block(" << block_size << ") Observed=" << std::fixed;
    ss.precision(4);
    ss << observed << " 95% CI=[" << r.confidence_interval_lower
       << ", " << r.confidence_interval_upper << "]";
    r.interpretation = ss.str();

    return r;
}

// ================================================================
// Permutation Test
// ================================================================
HypothesisTestResult StatisticalTests::permutationTest(
    const std::vector<double>& strategy_returns,
    const std::vector<double>& benchmark_returns,
    int num_permutations, unsigned seed) {
    HypothesisTestResult r;
    r.test_name = "Permutation Test (Strategy vs Benchmark)";

    int n = std::min(static_cast<int>(strategy_returns.size()),
                     static_cast<int>(benchmark_returns.size()));
    if (n < 10) { r.p_value = 1.0; return r; }

    // Observed difference in means
    std::vector<double> diffs(n);
    for (int i = 0; i < n; ++i)
        diffs[i] = strategy_returns[i] - benchmark_returns[i];
    double observed_diff = math::mean(diffs);

    // Pool all returns and randomly permute assignment
    std::vector<double> pool(2 * n);
    for (int i = 0; i < n; ++i) {
        pool[i] = strategy_returns[i];
        pool[n + i] = benchmark_returns[i];
    }

    std::mt19937 rng(seed);
    int extreme_count = 0;

    for (int p = 0; p < num_permutations; ++p) {
        std::shuffle(pool.begin(), pool.end(), rng);
        double perm_diff = 0;
        for (int i = 0; i < n; ++i) perm_diff += pool[i] - pool[n + i];
        perm_diff /= n;
        if (perm_diff >= observed_diff) extreme_count++;
    }

    r.test_statistic = observed_diff * 252.0;  // annualized
    r.p_value = static_cast<double>(extreme_count) / num_permutations;
    r.significant_at_05 = r.p_value < 0.05;
    r.significant_at_01 = r.p_value < 0.01;

    std::ostringstream ss;
    ss << "Mean excess return = " << std::fixed;
    ss.precision(4);
    ss << observed_diff * 252 * 100 << "% annualized. p=" << r.p_value;
    r.interpretation = ss.str();

    return r;
}

// ================================================================
// Bonferroni Correction
// ================================================================
std::vector<HypothesisTestResult> StatisticalTests::bonferroniCorrection(
    std::vector<HypothesisTestResult> results) {
    int m = static_cast<int>(results.size());
    for (auto& r : results) {
        r.p_value = std::min(1.0, r.p_value * m);
        r.significant_at_05 = r.p_value < 0.05;
        r.significant_at_01 = r.p_value < 0.01;
        r.test_name += " (Bonferroni)";
    }
    return results;
}

// ================================================================
// Benjamini-Hochberg FDR Correction
// ================================================================
std::vector<HypothesisTestResult> StatisticalTests::fdrCorrection(
    std::vector<HypothesisTestResult> results, double fdr_level) {
    int m = static_cast<int>(results.size());

    // Sort by p-value
    std::vector<int> idx(m);
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(),
              [&](int a, int b) { return results[a].p_value < results[b].p_value; });

    // BH procedure
    for (int rank = 0; rank < m; ++rank) {
        double threshold = fdr_level * (rank + 1) / m;
        results[idx[rank]].significant_at_05 = results[idx[rank]].p_value <= threshold;
        results[idx[rank]].test_name += " (BH-FDR)";
    }
    return results;
}

// ================================================================
// Deflated Sharpe Ratio
// ================================================================
HypothesisTestResult StatisticalTests::deflatedSharpeRatio(
    const std::vector<double>& best_returns, int num_strategies_tried,
    double risk_free_rate, unsigned seed) {
    HypothesisTestResult r;
    r.test_name = "Deflated Sharpe Ratio (DSR)";

    int n = static_cast<int>(best_returns.size());
    if (n < 10) { r.p_value = 1.0; return r; }

    double rf = risk_free_rate / 252.0;
    std::vector<double> excess(n);
    for (int i = 0; i < n; ++i) excess[i] = best_returns[i] - rf;

    double sr = math::mean(excess) / math::stddev(excess);

    // Expected max Sharpe under null (from N trials of random strategies)
    // E[max(SR)] ~ sqrt(2 * log(N)) - (log(log(N)) + log(4*pi)) / (2*sqrt(2*log(N)))
    double N = std::max(static_cast<double>(num_strategies_tried), 2.0);
    double log_n = std::log(N);
    double sr0 = std::sqrt(2.0 * log_n)
                 - (std::log(std::log(N)) + std::log(4.0 * M_PI)) / (2.0 * std::sqrt(2.0 * log_n));

    // PSR against deflated benchmark
    auto psr = probabilisticSharpe(best_returns, sr0 * std::sqrt(252.0), risk_free_rate);

    r.test_statistic = psr.test_statistic;
    r.p_value = psr.p_value;
    r.confidence_interval_lower = psr.confidence_interval_lower;
    r.confidence_interval_upper = psr.confidence_interval_upper;
    r.significant_at_05 = r.p_value < 0.05;
    r.significant_at_01 = r.p_value < 0.01;

    std::ostringstream ss;
    ss << "Tried " << num_strategies_tried << " strategies. ";
    ss << "Expected Sharpe under null = " << std::fixed;
    ss.precision(2);
    ss << sr0 * std::sqrt(252.0) << ". ";
    ss << "Observed Sharpe = " << sr * std::sqrt(252.0) << ". ";
    ss << (r.significant_at_05 ? "SURVIVES deflation" : "DOES NOT survive deflation");
    r.interpretation = ss.str();

    return r;
}

// ================================================================
// Regime Value Test
// ================================================================
HypothesisTestResult StatisticalTests::regimeValueTest(
    const std::vector<double>& strategy_returns,
    const std::vector<MarketRegime>& detected_regimes,
    int num_permutations, unsigned seed) {
    HypothesisTestResult r;
    r.test_name = "Regime Detection Value Test";

    int n = std::min(static_cast<int>(strategy_returns.size()),
                     static_cast<int>(detected_regimes.size()));
    if (n < 30) { r.p_value = 1.0; return r; }

    // Observed: mean return conditioned on regime signals
    double observed_stat = 0.0;
    for (int i = 0; i < n; ++i) {
        // Reward: positive return in bull, negative exposure in bear
        bool bullish = (detected_regimes[i] == MarketRegime::BullQuiet ||
                        detected_regimes[i] == MarketRegime::BullVolatile);
        if (bullish)
            observed_stat += strategy_returns[i];
        else
            observed_stat -= strategy_returns[i];  // profit from bearish positioning
    }
    observed_stat /= n;

    // Permute regime labels and recompute
    std::mt19937 rng(seed);
    std::vector<MarketRegime> shuffled(detected_regimes.begin(),
                                        detected_regimes.begin() + n);
    int extreme = 0;
    for (int p = 0; p < num_permutations; ++p) {
        std::shuffle(shuffled.begin(), shuffled.end(), rng);
        double perm_stat = 0.0;
        for (int i = 0; i < n; ++i) {
            bool bullish = (shuffled[i] == MarketRegime::BullQuiet ||
                            shuffled[i] == MarketRegime::BullVolatile);
            if (bullish) perm_stat += strategy_returns[i];
            else perm_stat -= strategy_returns[i];
        }
        perm_stat /= n;
        if (perm_stat >= observed_stat) extreme++;
    }

    r.test_statistic = observed_stat * 252.0;
    r.p_value = static_cast<double>(extreme) / num_permutations;
    r.significant_at_05 = r.p_value < 0.05;
    r.significant_at_01 = r.p_value < 0.01;

    r.interpretation = r.significant_at_05 ?
        "Regime detection adds SIGNIFICANT value vs random regime assignment" :
        "Regime detection does NOT add significant value over random assignment";

    return r;
}

// ================================================================
// ADF Test (simplified)
// ================================================================
HypothesisTestResult StatisticalTests::adfTest(
    const std::vector<double>& series, int max_lags) {
    HypothesisTestResult r;
    r.test_name = "Augmented Dickey-Fuller (Stationarity)";

    int n = static_cast<int>(series.size());
    if (n < max_lags + 10) { r.p_value = 1.0; return r; }

    // First differences
    std::vector<double> dy(n - 1);
    for (int i = 1; i < n; ++i) dy[i-1] = series[i] - series[i-1];

    // Simple DF regression: dy_t = alpha + beta * y_{t-1} + epsilon
    // Test statistic: t-stat on beta
    double sum_y = 0, sum_dy = 0, sum_y2 = 0, sum_ydy = 0;
    int m = n - 1;
    for (int i = 0; i < m; ++i) {
        double y = series[i];
        sum_y += y;
        sum_dy += dy[i];
        sum_y2 += y * y;
        sum_ydy += y * dy[i];
    }

    double beta = (m * sum_ydy - sum_y * sum_dy) / (m * sum_y2 - sum_y * sum_y);
    double alpha_hat = (sum_dy - beta * sum_y) / m;

    // Residual variance
    double sse = 0;
    for (int i = 0; i < m; ++i) {
        double res = dy[i] - alpha_hat - beta * series[i];
        sse += res * res;
    }
    double se_beta = std::sqrt(sse / (m - 2)) / std::sqrt(sum_y2 - sum_y * sum_y / m);

    r.test_statistic = beta / se_beta;

    // Critical values (MacKinnon): -3.43 (1%), -2.86 (5%), -2.57 (10%)
    if (r.test_statistic < -3.43) r.p_value = 0.005;
    else if (r.test_statistic < -2.86) r.p_value = 0.03;
    else if (r.test_statistic < -2.57) r.p_value = 0.08;
    else r.p_value = 0.5;

    r.significant_at_05 = r.p_value < 0.05;
    r.significant_at_01 = r.p_value < 0.01;
    r.interpretation = r.significant_at_05 ?
        "Series IS stationary (unit root rejected)" :
        "Series is NOT stationary (cannot reject unit root)";

    return r;
}

// ================================================================
// Ljung-Box Test
// ================================================================
HypothesisTestResult StatisticalTests::ljungBoxTest(
    const std::vector<double>& residuals, int num_lags) {
    HypothesisTestResult r;
    r.test_name = "Ljung-Box (Autocorrelation in Residuals)";

    int n = static_cast<int>(residuals.size());
    if (n < num_lags + 5) { r.p_value = 1.0; return r; }

    double mean_r = math::mean(residuals);
    double var_r = 0;
    for (double x : residuals) var_r += (x - mean_r) * (x - mean_r);

    if (var_r < 1e-15) { r.p_value = 1.0; return r; }

    double Q = 0;
    for (int k = 1; k <= num_lags; ++k) {
        double acf_k = 0;
        for (int i = k; i < n; ++i)
            acf_k += (residuals[i] - mean_r) * (residuals[i-k] - mean_r);
        acf_k /= var_r;
        Q += (acf_k * acf_k) / (n - k);
    }
    Q *= n * (n + 2);

    r.test_statistic = Q;

    // Chi-squared approximation (df = num_lags)
    // Simplified p-value using Wilson-Hilferty approximation
    double df = num_lags;
    double z = std::pow(Q / df, 1.0/3.0) - (1.0 - 2.0/(9.0*df));
    z /= std::sqrt(2.0/(9.0*df));
    r.p_value = 1.0 - math::norm_cdf(z);

    r.significant_at_05 = r.p_value < 0.05;
    r.significant_at_01 = r.p_value < 0.01;
    r.interpretation = r.significant_at_05 ?
        "SIGNIFICANT autocorrelation in residuals (model may be misspecified)" :
        "No significant autocorrelation (model residuals are clean)";

    return r;
}

// ================================================================
// Full Report
// ================================================================
StatisticalTests::FullTestReport StatisticalTests::fullReport(
    const BacktestResult& result,
    const std::vector<double>& benchmark_returns) {
    FullTestReport report;

    // Extract returns
    std::vector<double> port_rets, regimes_vec;
    std::vector<MarketRegime> regimes;
    for (const auto& rec : result.daily_records) {
        port_rets.push_back(rec.daily_return);
        regimes.push_back(rec.regime);
    }

    // Sharpe tests
    report.sharpe_ttest = sharpeRatioTTest(port_rets, 0.04);
    report.probabilistic_sharpe = probabilisticSharpe(port_rets, 0.0, 0.04);

    // Bootstrap tests
    auto sharpe_fn = [](const std::vector<double>& r) -> double {
        double m = math::mean(r);
        double s = math::stddev(r);
        return s > 1e-15 ? m / s * std::sqrt(252.0) : 0.0;
    };
    auto return_fn = [](const std::vector<double>& r) -> double {
        double cum = 1.0;
        for (double x : r) cum *= (1.0 + x);
        return cum - 1.0;
    };

    report.bootstrap_return = bootstrapTest(port_rets, return_fn,
        "Bootstrap: Total Return", 10000, 0.95);
    report.bootstrap_sharpe = blockBootstrap(port_rets, sharpe_fn,
        "Block Bootstrap: Sharpe Ratio", 20, 10000);

    // Permutation test
    int n = std::min(static_cast<int>(port_rets.size()),
                     static_cast<int>(benchmark_returns.size()));
    std::vector<double> bench_trim(benchmark_returns.begin(),
                                    benchmark_returns.begin() + n);
    std::vector<double> port_trim(port_rets.begin(), port_rets.begin() + n);
    report.permutation_vs_benchmark = permutationTest(port_trim, bench_trim);

    // Regime value test
    report.regime_value = regimeValueTest(port_trim, regimes);

    // Stationarity
    report.adf_returns = adfTest(port_rets);

    // Ljung-Box on residuals (use returns as proxy)
    report.ljung_box = ljungBoxTest(port_rets, 20);

    // Overall assessment
    int sig_count = 0;
    if (report.sharpe_ttest.significant_at_05) sig_count++;
    if (report.probabilistic_sharpe.significant_at_05) sig_count++;
    if (report.bootstrap_sharpe.significant_at_05) sig_count++;
    if (report.permutation_vs_benchmark.significant_at_05) sig_count++;
    if (report.regime_value.significant_at_05) sig_count++;

    report.overall_significant = sig_count >= 3;

    std::ostringstream ss;
    ss << "STATISTICAL SIGNIFICANCE SUMMARY: " << sig_count << "/5 tests significant at 5%.\n";
    ss << (report.overall_significant ? "OVERALL: Strategy shows SIGNIFICANT edge.\n" :
           "OVERALL: Insufficient evidence of significant edge.\n");
    ss << "Returns stationary: " << (report.adf_returns.significant_at_05 ? "YES" : "NO") << "\n";
    ss << "Residual autocorrelation: " << (report.ljung_box.significant_at_05 ? "PRESENT (caution)" : "CLEAN");
    report.summary = ss.str();

    return report;
}

} // namespace ste
