#include "stress/stress_engine.h"
#include "stress/historical_scenarios.h"
#include "core/black_scholes.h"
#include "utils/math_utils.h"

namespace ste {

StressEngine::StressEngine() {
    // Load all historical scenarios by default
    scenarios_ = HistoricalScenarios::allScenarios();
}

void StressEngine::addScenario(const StressScenario& scenario) {
    scenarios_.push_back(scenario);
}

StressResult StressEngine::runScenario(const Portfolio& portfolio,
                                        const MarketSnapshot& market,
                                        const StressScenario& scenario) const {
    StressResult result;
    result.scenario_name = scenario.name;

    // Apply shocks to market
    MarketSnapshot shocked = market;
    shocked.spot_price *= (1.0 + scenario.spot_shock);
    shocked.implied_vol += scenario.vol_shock;
    shocked.implied_vol = std::max(0.05, shocked.implied_vol);
    shocked.risk_free_rate += scenario.rate_shock;
    shocked.risk_free_rate = std::max(0.001, shocked.risk_free_rate);

    // Compute P&L
    double original_value = portfolio.totalValue(market);
    double shocked_value = portfolio.totalValue(shocked);
    result.portfolio_pnl = shocked_value - original_value;
    result.portfolio_pnl_pct = (original_value > 0) ?
        result.portfolio_pnl / original_value : 0.0;

    // Find worst leg
    result.worst_leg_pnl = 0.0;
    result.worst_leg_name = "N/A";
    for (const auto& strat : portfolio.strategies()) {
        for (const auto& leg : strat.legs) {
            double orig = BlackScholes::price(leg.option, market.spot_price,
                market.implied_vol, market.risk_free_rate, market.dividend_yield);
            double shock = BlackScholes::price(leg.option, shocked.spot_price,
                shocked.implied_vol, shocked.risk_free_rate, market.dividend_yield);
            double leg_pnl = (shock - orig) * leg.option.quantity * 100.0;
            if (leg_pnl < result.worst_leg_pnl) {
                result.worst_leg_pnl = leg_pnl;
                result.worst_leg_name = strat.name;
            }
        }
    }

    result.portfolio_greeks = portfolio.totalGreeks(shocked);
    result.var_impact = result.portfolio_pnl_pct;
    result.margin_impact = std::abs(result.portfolio_pnl_pct) * 1.5;

    return result;
}

std::vector<StressResult> StressEngine::runAllScenarios(const Portfolio& portfolio,
                                                         const MarketSnapshot& market) const {
    std::vector<StressResult> results;
    results.reserve(scenarios_.size());
    for (const auto& scenario : scenarios_) {
        results.push_back(runScenario(portfolio, market, scenario));
    }
    return results;
}

MonteCarloResult StressEngine::monteCarloStress(const Portfolio& portfolio,
                                                  const MarketSnapshot& market,
                                                  int num_paths) const {
    MonteCarloConfig config;
    config.num_paths = num_paths;
    config.num_steps = 63;  // ~3 months
    MonteCarlo mc(config);
    return mc.stressSimulation(market.spot_price, market.risk_free_rate,
                               market.implied_vol, 0.25, market.dividend_yield);
}

StressEngine::SensitivityResult StressEngine::sensitivityAnalysis(
    const Portfolio& portfolio, const MarketSnapshot& market,
    double range_pct, int num_points) const {

    SensitivityResult result;
    double spot_min = market.spot_price * (1.0 - range_pct);
    double spot_max = market.spot_price * (1.0 + range_pct);

    for (int i = 0; i < num_points; ++i) {
        double spot = spot_min + (spot_max - spot_min) * i / (num_points - 1);
        double shock = (spot - market.spot_price) / market.spot_price;
        result.spot_shocks.push_back(shock);

        MarketSnapshot shifted = market;
        shifted.spot_price = spot;
        double pnl = portfolio.totalValue(shifted) - portfolio.totalValue(market);
        result.pnl_values.push_back(pnl);

        Greeks g = portfolio.totalGreeks(shifted);
        result.delta_values.push_back(g.delta);
        result.gamma_values.push_back(g.gamma);
    }

    return result;
}

double StressEngine::computeVaR(const Portfolio& portfolio, const MarketSnapshot& market,
                                  double confidence, int num_sims) const {
    MonteCarloConfig config;
    config.num_paths = num_sims;
    config.num_steps = 1;  // 1-day VaR
    MonteCarlo mc(config);
    auto result = mc.stressSimulation(market.spot_price, market.risk_free_rate,
                                       market.implied_vol, 1.0/252.0, market.dividend_yield);
    return result.var_95;
}

double StressEngine::computeCVaR(const Portfolio& portfolio, const MarketSnapshot& market,
                                   double confidence, int num_sims) const {
    MonteCarloConfig config;
    config.num_paths = num_sims;
    config.num_steps = 1;
    MonteCarlo mc(config);
    auto result = mc.stressSimulation(market.spot_price, market.risk_free_rate,
                                       market.implied_vol, 1.0/252.0, market.dividend_yield);
    return result.cvar_95;
}

} // namespace ste
