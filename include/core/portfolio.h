#pragma once
#include "core/types.h"

namespace ste {

class Portfolio {
public:
    Portfolio() = default;

    void addStrategy(const Strategy& strategy);
    void removeStrategy(size_t index);
    void setCashAllocation(double pct);

    // Compute current state
    PortfolioState computeState(const MarketSnapshot& market) const;

    // P&L surface: vary spot and vol
    Surface3D computePnLSurface(const MarketSnapshot& market,
                                double spot_range_pct = 0.20,
                                double vol_range = 0.30,
                                int grid_size = 50) const;

    // P&L over time and spot
    Surface3D computeTimeSurface(const MarketSnapshot& market,
                                double spot_range_pct = 0.20,
                                double max_days = 30,
                                int grid_size = 50) const;

    // Greeks surface
    Surface3D computeGreeksSurface(const MarketSnapshot& market,
                                   const std::string& greek_name,
                                   double spot_range_pct = 0.20,
                                   double vol_range = 0.30,
                                   int grid_size = 30) const;

    // Aggregate Greeks
    Greeks totalGreeks(const MarketSnapshot& market) const;

    double totalValue(const MarketSnapshot& market) const;
    double totalPnL(const MarketSnapshot& market) const;

    const std::vector<Strategy>& strategies() const { return strategies_; }
    double cashAllocation() const { return cash_pct_; }

    // Track returns over time
    void recordReturn(double timestamp, double portfolio_ret, double benchmark_ret);
    double sharpeRatio() const;
    double maxDrawdown() const;
    double cumulativeReturn() const;
    double benchmarkReturn() const;

private:
    std::vector<Strategy> strategies_;
    double cash_pct_ = 0.3;  // 30% cash default
    double initial_value_ = 1000000.0;

    // Return history
    std::vector<double> timestamps_;
    std::vector<double> portfolio_returns_;
    std::vector<double> benchmark_returns_;
    double cumulative_return_ = 0.0;
    double benchmark_cumulative_ = 0.0;
    double peak_value_ = 1.0;
    double max_drawdown_ = 0.0;
};

} // namespace ste
