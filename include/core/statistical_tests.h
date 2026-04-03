#pragma once
#include "core/types.h"
#include "core/backtester.h"

namespace ste {

// Statistical significance testing for backtest results.
// Tests whether observed alpha / Sharpe are significantly different
// from zero or from random chance.

struct HypothesisTestResult {
    std::string test_name;
    double test_statistic;
    double p_value;
    double confidence_interval_lower;
    double confidence_interval_upper;
    bool significant_at_05;     // p < 0.05
    bool significant_at_01;     // p < 0.01
    std::string interpretation;
};

class StatisticalTests {
public:
    // ================================================================
    // Sharpe Ratio Tests
    // ================================================================

    // T-test: H0: Sharpe Ratio = 0
    // Uses Lo (2002) adjustment for autocorrelation in returns
    static HypothesisTestResult sharpeRatioTTest(
        const std::vector<double>& returns,
        double risk_free_rate = 0.0);

    // Probabilistic Sharpe Ratio (Bailey & Lopez de Prado, 2012)
    // Tests if observed Sharpe > benchmark Sharpe accounting for
    // skewness, kurtosis, and sample size
    static HypothesisTestResult probabilisticSharpe(
        const std::vector<double>& returns,
        double benchmark_sharpe = 0.0,
        double risk_free_rate = 0.0);

    // ================================================================
    // Bootstrap Tests
    // ================================================================

    // Bootstrap confidence interval for any statistic
    static HypothesisTestResult bootstrapTest(
        const std::vector<double>& returns,
        const std::function<double(const std::vector<double>&)>& statistic,
        const std::string& name = "Bootstrap",
        int num_bootstrap = 10000,
        double confidence = 0.95,
        unsigned seed = 42);

    // Circular block bootstrap (preserves autocorrelation)
    static HypothesisTestResult blockBootstrap(
        const std::vector<double>& returns,
        const std::function<double(const std::vector<double>&)>& statistic,
        const std::string& name = "Block Bootstrap",
        int block_size = 20,
        int num_bootstrap = 10000,
        unsigned seed = 42);

    // ================================================================
    // Permutation Tests
    // ================================================================

    // Test if strategy returns are significantly different from benchmark
    // by randomly permuting the assignment of returns to strategy vs benchmark
    static HypothesisTestResult permutationTest(
        const std::vector<double>& strategy_returns,
        const std::vector<double>& benchmark_returns,
        int num_permutations = 10000,
        unsigned seed = 42);

    // ================================================================
    // Multiple Testing Correction
    // ================================================================

    // Bonferroni correction for testing multiple strategies
    static std::vector<HypothesisTestResult> bonferroniCorrection(
        std::vector<HypothesisTestResult> results);

    // Benjamini-Hochberg FDR control
    static std::vector<HypothesisTestResult> fdrCorrection(
        std::vector<HypothesisTestResult> results,
        double fdr_level = 0.05);

    // ================================================================
    // Deflated Sharpe Ratio (Bailey & Lopez de Prado, 2014)
    // ================================================================

    // Accounts for multiple testing / strategy selection bias
    // Given N strategies tried, what is the probability that the best
    // Sharpe is significant after accounting for all trials?
    static HypothesisTestResult deflatedSharpeRatio(
        const std::vector<double>& best_returns,
        int num_strategies_tried,
        double risk_free_rate = 0.0,
        unsigned seed = 42);

    // ================================================================
    // Regime-Specific Tests
    // ================================================================

    // Test if regime detection adds value vs. random regime assignment
    static HypothesisTestResult regimeValueTest(
        const std::vector<double>& strategy_returns,
        const std::vector<MarketRegime>& detected_regimes,
        int num_permutations = 5000,
        unsigned seed = 42);

    // ================================================================
    // Stationarity & Model Validity
    // ================================================================

    // Augmented Dickey-Fuller test for stationarity of returns
    static HypothesisTestResult adfTest(
        const std::vector<double>& series,
        int max_lags = 10);

    // Ljung-Box test for autocorrelation in residuals
    static HypothesisTestResult ljungBoxTest(
        const std::vector<double>& residuals,
        int num_lags = 20);

    // ================================================================
    // Summary Report
    // ================================================================

    // Run full battery of tests on a backtest result
    struct FullTestReport {
        HypothesisTestResult sharpe_ttest;
        HypothesisTestResult probabilistic_sharpe;
        HypothesisTestResult bootstrap_return;
        HypothesisTestResult bootstrap_sharpe;
        HypothesisTestResult permutation_vs_benchmark;
        HypothesisTestResult regime_value;
        HypothesisTestResult adf_returns;
        HypothesisTestResult ljung_box;
        bool overall_significant;
        std::string summary;
    };

    static FullTestReport fullReport(
        const BacktestResult& result,
        const std::vector<double>& benchmark_returns);
};

} // namespace ste
