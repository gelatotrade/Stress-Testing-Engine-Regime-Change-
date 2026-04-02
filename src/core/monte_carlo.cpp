#include "core/monte_carlo.h"
#include "core/black_scholes.h"
#include "utils/math_utils.h"
#include <algorithm>
#include <cmath>

namespace ste {

MonteCarlo::MonteCarlo(const MonteCarloConfig& config) : config_(config) {}

std::vector<PathResult> MonteCarlo::simulatePaths(double spot, double rate,
                                                    double vol, double T,
                                                    double dividend) const {
    std::mt19937 rng(config_.seed);
    std::normal_distribution<double> normal(0.0, 1.0);

    double dt = T / config_.num_steps;
    double drift = (rate - dividend - 0.5 * vol * vol) * dt;
    double diffusion = vol * std::sqrt(dt);

    int num_paths = config_.num_paths;
    if (config_.antithetic) num_paths /= 2;

    std::vector<PathResult> results;
    results.reserve(config_.num_paths);

    for (int p = 0; p < num_paths; ++p) {
        PathResult path, anti_path;
        path.prices.resize(config_.num_steps + 1);
        path.prices[0] = spot;

        if (config_.antithetic) {
            anti_path.prices.resize(config_.num_steps + 1);
            anti_path.prices[0] = spot;
        }

        for (int s = 1; s <= config_.num_steps; ++s) {
            double z = normal(rng);
            path.prices[s] = path.prices[s-1] * std::exp(drift + diffusion * z);
            if (config_.antithetic) {
                anti_path.prices[s] = anti_path.prices[s-1] * std::exp(drift - diffusion * z);
            }
        }

        path.final_price = path.prices.back();
        path.path_return = (path.final_price - spot) / spot;
        results.push_back(std::move(path));

        if (config_.antithetic) {
            anti_path.final_price = anti_path.prices.back();
            anti_path.path_return = (anti_path.final_price - spot) / spot;
            results.push_back(std::move(anti_path));
        }
    }

    return results;
}

double MonteCarlo::priceOption(const Option& opt, double spot, double rate,
                                double vol, double dividend) const {
    auto paths = simulatePaths(spot, rate, vol, opt.expiry, dividend);
    double sum = 0.0;
    for (const auto& path : paths) {
        double payoff;
        if (opt.type == OptionType::Call)
            payoff = std::max(path.final_price - opt.strike, 0.0);
        else
            payoff = std::max(opt.strike - path.final_price, 0.0);
        sum += payoff;
    }
    double discount = std::exp(-rate * opt.expiry);
    return discount * sum / paths.size();
}

MonteCarloResult MonteCarlo::stressSimulation(double spot, double rate, double vol,
                                               double T, double dividend) const {
    auto paths = simulatePaths(spot, rate, vol, T, dividend);

    MonteCarloResult result;
    std::vector<double> returns;
    returns.reserve(paths.size());

    for (const auto& p : paths) {
        returns.push_back(p.path_return);
        result.terminal_prices.push_back(p.final_price);
    }

    result.path_returns = returns;
    result.mean_price = math::mean(result.terminal_prices);
    result.std_error = math::stddev(result.terminal_prices) / std::sqrt(paths.size());
    result.var_95 = -math::percentile(returns, 0.05) * spot;
    result.cvar_95 = 0.0;

    std::sort(returns.begin(), returns.end());
    int cutoff = static_cast<int>(0.05 * returns.size());
    double sum = 0.0;
    for (int i = 0; i < cutoff && i < static_cast<int>(returns.size()); ++i)
        sum += returns[i];
    if (cutoff > 0)
        result.cvar_95 = -(sum / cutoff) * spot;

    // Max drawdown across all paths
    result.max_drawdown = 0.0;
    for (const auto& p : paths) {
        double peak = p.prices[0];
        for (double price : p.prices) {
            if (price > peak) peak = price;
            double dd = (peak - price) / peak;
            if (dd > result.max_drawdown) result.max_drawdown = dd;
        }
    }

    int losses = 0;
    int gains_10 = 0;
    for (double r : returns) {
        if (r < 0) losses++;
        if (r > 0.1) gains_10++;
    }
    result.prob_loss = static_cast<double>(losses) / returns.size();
    result.prob_gain_10pct = static_cast<double>(gains_10) / returns.size();
    result.expected_shortfall = result.cvar_95;

    return result;
}

MonteCarloResult MonteCarlo::regimeSwitchingSimulation(
    double spot, double rate, double T,
    const std::vector<double>& regime_vols,
    const std::vector<std::vector<double>>& transition_matrix,
    int initial_regime) const {

    std::mt19937 rng(config_.seed);
    std::normal_distribution<double> normal(0.0, 1.0);
    std::uniform_real_distribution<double> uniform(0.0, 1.0);

    double dt = T / config_.num_steps;
    int num_regimes = static_cast<int>(regime_vols.size());

    std::vector<PathResult> paths;
    paths.reserve(config_.num_paths);

    for (int p = 0; p < config_.num_paths; ++p) {
        PathResult path;
        path.prices.resize(config_.num_steps + 1);
        path.prices[0] = spot;
        int current_regime = initial_regime;

        for (int s = 1; s <= config_.num_steps; ++s) {
            // Regime transition
            double u = uniform(rng);
            double cum = 0.0;
            for (int r = 0; r < num_regimes; ++r) {
                cum += transition_matrix[current_regime][r];
                if (u <= cum) { current_regime = r; break; }
            }

            double vol = regime_vols[current_regime];
            double drift = (rate - 0.5 * vol * vol) * dt;
            double z = normal(rng);
            path.prices[s] = path.prices[s-1] * std::exp(drift + vol * std::sqrt(dt) * z);
        }

        path.final_price = path.prices.back();
        path.path_return = (path.final_price - spot) / spot;
        paths.push_back(std::move(path));
    }

    // Build result same as stressSimulation
    MonteCarloResult result;
    std::vector<double> returns;
    for (const auto& p : paths) {
        returns.push_back(p.path_return);
        result.terminal_prices.push_back(p.final_price);
    }

    result.path_returns = returns;
    result.mean_price = math::mean(result.terminal_prices);
    result.std_error = math::stddev(result.terminal_prices) / std::sqrt(paths.size());
    result.var_95 = -math::percentile(returns, 0.05) * spot;

    std::sort(returns.begin(), returns.end());
    int cutoff = static_cast<int>(0.05 * returns.size());
    double sum = 0.0;
    for (int i = 0; i < cutoff; ++i) sum += returns[i];
    result.cvar_95 = cutoff > 0 ? -(sum / cutoff) * spot : 0.0;

    result.max_drawdown = 0.0;
    for (const auto& p : paths) {
        double peak = p.prices[0];
        for (double price : p.prices) {
            if (price > peak) peak = price;
            double dd = (peak - price) / peak;
            if (dd > result.max_drawdown) result.max_drawdown = dd;
        }
    }

    int losses = 0, gains_10 = 0;
    for (double r : returns) {
        if (r < 0) losses++;
        if (r > 0.1) gains_10++;
    }
    result.prob_loss = static_cast<double>(losses) / returns.size();
    result.prob_gain_10pct = static_cast<double>(gains_10) / returns.size();
    result.expected_shortfall = result.cvar_95;

    return result;
}

} // namespace ste
