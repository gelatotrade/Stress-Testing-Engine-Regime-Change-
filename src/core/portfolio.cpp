#include "core/portfolio.h"
#include "core/black_scholes.h"
#include "utils/math_utils.h"

namespace ste {

void Portfolio::addStrategy(const Strategy& strategy) {
    strategies_.push_back(strategy);
}

void Portfolio::removeStrategy(size_t index) {
    if (index < strategies_.size())
        strategies_.erase(strategies_.begin() + index);
}

void Portfolio::setCashAllocation(double pct) {
    cash_pct_ = math::clamp(pct, 0.0, 1.0);
}

Greeks Portfolio::totalGreeks(const MarketSnapshot& market) const {
    Greeks total{};
    for (const auto& strat : strategies_) {
        for (const auto& leg : strat.legs) {
            Greeks g = BlackScholes::computeGreeks(
                leg.option, market.spot_price, market.implied_vol,
                market.risk_free_rate, market.dividend_yield);
            total.delta += g.delta;
            total.gamma += g.gamma;
            total.theta += g.theta;
            total.vega  += g.vega;
            total.rho   += g.rho;
        }
        // Add delta from underlying shares
        total.delta += strat.underlying_shares;
    }
    return total;
}

double Portfolio::totalValue(const MarketSnapshot& market) const {
    double value = initial_value_ * cash_pct_;
    double equity_value = initial_value_ * (1.0 - cash_pct_);

    for (const auto& strat : strategies_) {
        for (const auto& leg : strat.legs) {
            double opt_val = BlackScholes::price(
                leg.option, market.spot_price, market.implied_vol,
                market.risk_free_rate, market.dividend_yield);
            value += opt_val * leg.option.quantity * 100.0;  // 100 shares per contract
        }
        value += strat.underlying_shares * market.spot_price;
    }

    return value + equity_value;
}

double Portfolio::totalPnL(const MarketSnapshot& market) const {
    double pnl = 0.0;
    for (const auto& strat : strategies_) {
        for (const auto& leg : strat.legs) {
            double current = BlackScholes::price(
                leg.option, market.spot_price, market.implied_vol,
                market.risk_free_rate, market.dividend_yield);
            pnl += (current - leg.option.premium) * leg.option.quantity * 100.0;
        }
    }
    return pnl;
}

PortfolioState Portfolio::computeState(const MarketSnapshot& market) const {
    PortfolioState state;
    Greeks g = totalGreeks(market);
    state.total_delta = g.delta;
    state.total_gamma = g.gamma;
    state.total_theta = g.theta;
    state.total_vega = g.vega;
    state.total_value = totalValue(market);
    state.cash_allocation = cash_pct_;
    state.equity_allocation = 1.0 - cash_pct_;
    state.options_allocation = 0.0;  // computed below

    if (state.total_value > 0) {
        double opt_val = 0.0;
        for (const auto& strat : strategies_) {
            for (const auto& leg : strat.legs) {
                double v = BlackScholes::price(leg.option, market.spot_price,
                    market.implied_vol, market.risk_free_rate, market.dividend_yield);
                opt_val += std::abs(v * leg.option.quantity * 100.0);
            }
        }
        state.options_allocation = opt_val / state.total_value;
    }

    state.sharpe_ratio = sharpeRatio();
    state.max_drawdown = max_drawdown_;
    state.portfolio_return = cumulative_return_;
    state.benchmark_return = benchmark_cumulative_;
    state.var_95 = 0.0;
    state.cvar_95 = 0.0;

    return state;
}

Surface3D Portfolio::computePnLSurface(const MarketSnapshot& market,
                                        double spot_range_pct,
                                        double vol_range,
                                        int grid_size) const {
    Surface3D surface;
    surface.label = "P&L Surface (Spot vs Volatility)";
    surface.rows = grid_size;
    surface.cols = grid_size;
    surface.grid.resize(grid_size, std::vector<Point3D>(grid_size));

    double spot_min = market.spot_price * (1.0 - spot_range_pct);
    double spot_max = market.spot_price * (1.0 + spot_range_pct);
    double vol_min = std::max(0.05, market.implied_vol - vol_range / 2.0);
    double vol_max = market.implied_vol + vol_range / 2.0;

    double max_pnl = -1e15, min_pnl = 1e15;

    for (int i = 0; i < grid_size; ++i) {
        for (int j = 0; j < grid_size; ++j) {
            double spot = spot_min + (spot_max - spot_min) * i / (grid_size - 1);
            double vol = vol_min + (vol_max - vol_min) * j / (grid_size - 1);

            double pnl = 0.0;
            for (const auto& strat : strategies_) {
                for (const auto& leg : strat.legs) {
                    double current = BlackScholes::price(
                        leg.option, spot, vol, market.risk_free_rate, market.dividend_yield);
                    pnl += (current - leg.option.premium) * leg.option.quantity * 100.0;
                }
                pnl += strat.underlying_shares * (spot - market.spot_price);
            }

            surface.grid[i][j].x = spot;
            surface.grid[i][j].y = vol;
            surface.grid[i][j].z = pnl;

            if (pnl > max_pnl) max_pnl = pnl;
            if (pnl < min_pnl) min_pnl = pnl;
        }
    }

    // Color mapping: green for profit, red for loss
    double range = std::max(std::abs(max_pnl), std::abs(min_pnl));
    if (range < 1e-10) range = 1.0;
    for (int i = 0; i < grid_size; ++i) {
        for (int j = 0; j < grid_size; ++j) {
            double norm = surface.grid[i][j].z / range;
            if (norm > 0) {
                surface.grid[i][j].color_r = 0.1;
                surface.grid[i][j].color_g = 0.3 + 0.7 * norm;
                surface.grid[i][j].color_b = 0.1;
            } else {
                surface.grid[i][j].color_r = 0.3 + 0.7 * std::abs(norm);
                surface.grid[i][j].color_g = 0.1;
                surface.grid[i][j].color_b = 0.1;
            }
            surface.grid[i][j].color_a = 0.85;
        }
    }

    return surface;
}

Surface3D Portfolio::computeTimeSurface(const MarketSnapshot& market,
                                         double spot_range_pct,
                                         double max_days,
                                         int grid_size) const {
    Surface3D surface;
    surface.label = "P&L Surface (Spot vs Time)";
    surface.rows = grid_size;
    surface.cols = grid_size;
    surface.grid.resize(grid_size, std::vector<Point3D>(grid_size));

    double spot_min = market.spot_price * (1.0 - spot_range_pct);
    double spot_max = market.spot_price * (1.0 + spot_range_pct);

    for (int i = 0; i < grid_size; ++i) {
        for (int j = 0; j < grid_size; ++j) {
            double spot = spot_min + (spot_max - spot_min) * i / (grid_size - 1);
            double days = max_days * j / (grid_size - 1);
            double time_decay = days / 365.0;

            double pnl = 0.0;
            for (const auto& strat : strategies_) {
                for (const auto& leg : strat.legs) {
                    Option shifted = leg.option;
                    shifted.expiry = std::max(0.001, leg.option.expiry - time_decay);
                    double current = BlackScholes::price(
                        shifted, spot, market.implied_vol,
                        market.risk_free_rate, market.dividend_yield);
                    pnl += (current - leg.option.premium) * leg.option.quantity * 100.0;
                }
                pnl += strat.underlying_shares * (spot - market.spot_price);
            }

            surface.grid[i][j].x = spot;
            surface.grid[i][j].y = days;
            surface.grid[i][j].z = pnl;

            // Color: gradient from blue (now) to red (expiry)
            double t_norm = days / max_days;
            double pnl_norm = pnl / std::max(std::abs(pnl) + 1.0, 1.0);
            surface.grid[i][j].color_r = t_norm;
            surface.grid[i][j].color_g = pnl_norm > 0 ? 0.5 * pnl_norm : 0.0;
            surface.grid[i][j].color_b = 1.0 - t_norm;
            surface.grid[i][j].color_a = 0.85;
        }
    }
    return surface;
}

Surface3D Portfolio::computeGreeksSurface(const MarketSnapshot& market,
                                           const std::string& greek_name,
                                           double spot_range_pct,
                                           double vol_range,
                                           int grid_size) const {
    Surface3D surface;
    surface.label = greek_name + " Surface";
    surface.rows = grid_size;
    surface.cols = grid_size;
    surface.grid.resize(grid_size, std::vector<Point3D>(grid_size));

    double spot_min = market.spot_price * (1.0 - spot_range_pct);
    double spot_max = market.spot_price * (1.0 + spot_range_pct);
    double vol_min = std::max(0.05, market.implied_vol - vol_range / 2.0);
    double vol_max = market.implied_vol + vol_range / 2.0;

    for (int i = 0; i < grid_size; ++i) {
        for (int j = 0; j < grid_size; ++j) {
            double spot = spot_min + (spot_max - spot_min) * i / (grid_size - 1);
            double vol = vol_min + (vol_max - vol_min) * j / (grid_size - 1);

            double value = 0.0;
            for (const auto& strat : strategies_) {
                for (const auto& leg : strat.legs) {
                    Greeks g = BlackScholes::computeGreeks(
                        leg.option, spot, vol, market.risk_free_rate, market.dividend_yield);
                    if (greek_name == "Delta") value += g.delta;
                    else if (greek_name == "Gamma") value += g.gamma;
                    else if (greek_name == "Theta") value += g.theta;
                    else if (greek_name == "Vega") value += g.vega;
                }
            }

            surface.grid[i][j] = {spot, vol, value, 0.3, 0.5, 0.9, 0.85};
        }
    }
    return surface;
}

void Portfolio::recordReturn(double timestamp, double portfolio_ret, double benchmark_ret) {
    timestamps_.push_back(timestamp);
    portfolio_returns_.push_back(portfolio_ret);
    benchmark_returns_.push_back(benchmark_ret);

    cumulative_return_ = (1.0 + cumulative_return_) * (1.0 + portfolio_ret) - 1.0;
    benchmark_cumulative_ = (1.0 + benchmark_cumulative_) * (1.0 + benchmark_ret) - 1.0;

    double current = 1.0 + cumulative_return_;
    if (current > peak_value_) peak_value_ = current;
    double dd = (peak_value_ - current) / peak_value_;
    if (dd > max_drawdown_) max_drawdown_ = dd;
}

double Portfolio::sharpeRatio() const {
    if (portfolio_returns_.size() < 2) return 0.0;
    double m = math::mean(portfolio_returns_);
    double s = math::stddev(portfolio_returns_);
    if (s < 1e-15) return 0.0;
    return (m * 252.0) / (s * std::sqrt(252.0));  // annualized
}

double Portfolio::maxDrawdown() const { return max_drawdown_; }
double Portfolio::cumulativeReturn() const { return cumulative_return_; }
double Portfolio::benchmarkReturn() const { return benchmark_cumulative_; }

} // namespace ste
