#pragma once
#include "core/types.h"
#include "core/portfolio.h"
#include "regime/regime_detector.h"
#include "strategies/strategy_manager.h"
#include "stress/stress_engine.h"

namespace ste {

// Walk-Forward Out-of-Sample Backtester
// Eliminates look-ahead bias by strictly separating train/test periods.
// Uses expanding or rolling windows for parameter calibration.

struct BacktestConfig {
    Timeframe timeframe = Timeframe::Daily;
    int total_periods = 756;            // total bars (days/hours/minutes)
    int warmup_periods = 60;            // min bars before trading
    int train_window = 252;             // HMM training window (rolling bars)
    int refit_interval = 63;            // refit HMM every N bars
    int execution_delay = 1;            // trade on T+1 bar (avoids close-price bias)
    double transaction_cost_bps = 5.0;  // 5 bps per trade
    double slippage_bps = 2.0;          // 2 bps slippage
    bool use_rolling_window = true;     // rolling vs expanding window
    double initial_capital = 1000000.0;

    // Convenience: configure from a day-count, auto-scaling for timeframe
    static BacktestConfig fromDays(int days, Timeframe tf = Timeframe::Daily) {
        BacktestConfig cfg;
        cfg.timeframe = tf;
        cfg.total_periods   = daysToPeriodsCount(days, tf);
        cfg.warmup_periods  = daysToPeriodsCount(60, tf);
        cfg.train_window    = daysToPeriodsCount(252, tf);
        cfg.refit_interval  = daysToPeriodsCount(63, tf);
        cfg.execution_delay = (tf == Timeframe::Daily) ? 1 : daysToPeriodsCount(1, tf);
        return cfg;
    }

    double periodsPerYr() const { return periodsPerYear(timeframe); }
    double dt() const { return timeframeDt(timeframe); }
};

struct BacktestTrade {
    int day;
    std::string strategy_name;
    double notional;
    double cost;                    // transaction costs + slippage
};

struct PeriodRecord {
    int period;  // bar index (day/hour/minute depending on timeframe)
    double portfolio_value;
    double period_return;
    double benchmark_return;
    double cumulative_return;
    double benchmark_cumulative;
    double cash_pct;
    double drawdown;
    MarketRegime regime;
    SignalType signal;
    double crisis_prob;
    double warning_score;
    int num_trades;
};

struct BacktestResult {
    // Returns
    std::vector<PeriodRecord> period_records;
    std::vector<BacktestTrade> trades;

    // Performance
    double total_return;
    double annualized_return;
    double benchmark_total_return;
    double benchmark_annualized;
    double alpha;
    double beta;
    double sharpe_ratio;
    double sortino_ratio;
    double calmar_ratio;
    double max_drawdown;
    double max_drawdown_duration;   // periods (bars)
    double win_rate;
    double profit_factor;
    int total_trades;
    double total_transaction_costs;

    // Risk
    double var_95;
    double cvar_95;
    double volatility;
    double downside_vol;
    double information_ratio;
    double tracking_error;

    // Out-of-sample specific
    int train_periods;
    int test_periods;
    int num_refits;
};

class Backtester {
public:
    explicit Backtester(const BacktestConfig& config = {});

    // Run full walk-forward backtest
    BacktestResult run(const std::vector<MarketSnapshot>& data) const;

    // Run with k-fold purged cross-validation
    struct CrossValResult {
        std::vector<BacktestResult> fold_results;
        double mean_sharpe;
        double std_sharpe;
        double mean_return;
        double std_return;
        double probability_of_loss;
        int num_folds;
    };

    CrossValResult crossValidate(const std::vector<MarketSnapshot>& data,
                                  int num_folds = 5,
                                  int purge_days = 10) const;

    const BacktestConfig& config() const { return config_; }

private:
    BacktestConfig config_;

    // Apply execution delay and costs
    double applyFriction(double gross_return, int num_trades) const;

    // Compute all performance metrics from daily records
    void computeMetrics(BacktestResult& result) const;
};

} // namespace ste
