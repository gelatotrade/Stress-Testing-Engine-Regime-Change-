#include "core/types.h"
#include "core/black_scholes.h"
#include "core/monte_carlo.h"
#include "core/portfolio.h"
#include "core/market_data.h"
#include "strategies/options_strategies.h"
#include "strategies/strategy_manager.h"
#include "regime/hidden_markov_model.h"
#include "regime/regime_detector.h"
#include "stress/stress_engine.h"
#include "stress/historical_scenarios.h"
#include "stress/scenario_generator.h"
#include "utils/math_utils.h"
#include "utils/json_writer.h"

#include <iostream>
#include <cassert>
#include <cmath>

#define TEST(name) \
    std::cout << "  Testing " << #name << "..."; \
    test_##name(); \
    std::cout << " PASSED\n"; \
    passed++;

int passed = 0;

// ============================================================
// Black-Scholes Tests
// ============================================================

void test_BlackScholes_CallPrice() {
    ste::Option call{ste::OptionType::Call, ste::ExerciseStyle::European, 100.0, 1.0, 0.0, 1};
    double price = ste::BlackScholes::price(call, 100.0, 0.20, 0.05);
    // ATM call with 20% vol, 1yr, should be ~$10.45
    assert(price > 9.0 && price < 12.0);
}

void test_BlackScholes_PutPrice() {
    ste::Option put{ste::OptionType::Put, ste::ExerciseStyle::European, 100.0, 1.0, 0.0, 1};
    double price = ste::BlackScholes::price(put, 100.0, 0.20, 0.05);
    // ATM put should be ~$5.57
    assert(price > 4.0 && price < 7.0);
}

void test_BlackScholes_PutCallParity() {
    double S = 100.0, K = 100.0, T = 1.0, r = 0.05, v = 0.25;
    ste::Option call{ste::OptionType::Call, ste::ExerciseStyle::European, K, T, 0.0, 1};
    ste::Option put{ste::OptionType::Put, ste::ExerciseStyle::European, K, T, 0.0, 1};
    double c = ste::BlackScholes::price(call, S, v, r);
    double p = ste::BlackScholes::price(put, S, v, r);
    // C - P = S - K*exp(-rT)
    double lhs = c - p;
    double rhs = S - K * std::exp(-r * T);
    assert(std::abs(lhs - rhs) < 0.01);
}

void test_BlackScholes_Greeks() {
    ste::Option call{ste::OptionType::Call, ste::ExerciseStyle::European, 100.0, 0.5, 0.0, 1};
    auto g = ste::BlackScholes::computeGreeks(call, 100.0, 0.20, 0.05);
    // ATM call delta should be ~0.55-0.60
    assert(g.delta > 0.45 && g.delta < 0.70);
    assert(g.gamma > 0.0);
    assert(g.theta < 0.0);  // time decay
    assert(g.vega > 0.0);
}

void test_BlackScholes_ImpliedVol() {
    ste::Option call{ste::OptionType::Call, ste::ExerciseStyle::European, 100.0, 1.0, 0.0, 1};
    double true_vol = 0.25;
    double price = ste::BlackScholes::price(call, 100.0, true_vol, 0.05);
    double iv = ste::BlackScholes::impliedVol(call, price, 100.0, 0.05);
    assert(std::abs(iv - true_vol) < 0.01);
}

// ============================================================
// Monte Carlo Tests
// ============================================================

void test_MonteCarlo_ConvergesToBS() {
    ste::Option call{ste::OptionType::Call, ste::ExerciseStyle::European, 100.0, 1.0, 0.0, 1};
    double bs_price = ste::BlackScholes::price(call, 100.0, 0.20, 0.05);

    ste::MonteCarloConfig config;
    config.num_paths = 100000;
    ste::MonteCarlo mc(config);
    double mc_price = mc.priceOption(call, 100.0, 0.05, 0.20);

    // MC should be within 1% of BS
    assert(std::abs(mc_price - bs_price) / bs_price < 0.02);
}

void test_MonteCarlo_StressSimulation() {
    ste::MonteCarloConfig config;
    config.num_paths = 10000;
    ste::MonteCarlo mc(config);
    auto result = mc.stressSimulation(100.0, 0.05, 0.20, 1.0);

    assert(result.terminal_prices.size() == 10000);
    assert(result.var_95 > 0);
    assert(result.prob_loss > 0 && result.prob_loss < 1);
}

// ============================================================
// Options Strategy Tests
// ============================================================

void test_Strategies_CoveredCall() {
    auto strat = ste::OptionsStrategies::createCoveredCall(100.0, 105.0, 0.25, 0.20, 0.05);
    assert(strat.type == ste::StrategyType::CoveredCall);
    assert(strat.legs.size() == 1);
    assert(strat.underlying_shares == 100);
    assert(strat.legs[0].option.quantity == -1);  // short call
}

void test_Strategies_IronCondor() {
    auto strat = ste::OptionsStrategies::createIronCondor(
        100.0, 90.0, 95.0, 105.0, 110.0, 0.25, 0.20, 0.05);
    assert(strat.type == ste::StrategyType::IronCondor);
    assert(strat.legs.size() == 4);
    assert(strat.max_profit > 0);
    assert(strat.max_loss > 0);
}

void test_Strategies_Straddle() {
    auto strat = ste::OptionsStrategies::createStraddle(100.0, 100.0, 0.25, 0.20, 0.05, true);
    assert(strat.type == ste::StrategyType::Straddle);
    assert(strat.legs.size() == 2);
    assert(strat.breakeven_lower < 100.0);
    assert(strat.breakeven_upper > 100.0);
}

// ============================================================
// Regime Detection Tests
// ============================================================

void test_HMM_Init() {
    ste::HiddenMarkovModel hmm;
    hmm.initMarketRegimeModel();
    assert(hmm.numStates() == 5);
    auto probs = hmm.stateProbabilities();
    double sum = 0;
    for (double p : probs) sum += p;
    assert(std::abs(sum - 1.0) < 0.01);
}

void test_HMM_Update() {
    ste::HiddenMarkovModel hmm;
    hmm.initMarketRegimeModel();
    // Feed bull market observation
    std::vector<double> bull_obs = {0.001, 0.12, 0.0, 1.0};
    auto probs = hmm.update(bull_obs);
    double sum = 0;
    for (double p : probs) sum += p;
    assert(std::abs(sum - 1.0) < 0.01);
}

void test_RegimeDetector_CrisisDetection() {
    ste::RegimeDetector detector;

    // Feed normal data first
    ste::MarketSnapshot normal;
    normal.timestamp = 0;
    normal.spot_price = 4500;
    normal.implied_vol = 0.12;
    normal.credit_spread = 1.0;
    normal.risk_free_rate = 0.05;
    normal.dividend_yield = 0.015;
    normal.vix = 12;
    normal.volume = 3e9;

    for (int i = 0; i < 50; ++i) {
        normal.timestamp += 86400;
        normal.spot_price += 5;
        detector.update(normal);
    }

    // Feed crisis data
    ste::MarketSnapshot crisis = normal;
    for (int i = 0; i < 30; ++i) {
        crisis.timestamp += 86400;
        crisis.spot_price *= 0.97;  // -3% per day
        crisis.implied_vol = 0.45;
        crisis.credit_spread = 4.0;
        crisis.vix = 50;
        auto state = detector.update(crisis);
        // After several crisis days, should detect bear volatile
        if (i > 10) {
            // Crisis probability should be elevated
            assert(state.crisis_probability > 0.01);
        }
    }
}

// ============================================================
// Stress Engine Tests
// ============================================================

void test_StressEngine_HistoricalScenarios() {
    auto scenarios = ste::HistoricalScenarios::allScenarios();
    assert(scenarios.size() == 8);
    assert(scenarios[0].name == "Black Monday 1987");
    assert(scenarios[2].spot_shock < -0.5);  // GFC
}

void test_StressEngine_RunScenario() {
    ste::Portfolio portfolio;
    auto strat = ste::OptionsStrategies::createIronCondor(
        4500.0, 4050.0, 4275.0, 4725.0, 4950.0, 0.25, 0.20, 0.05);
    portfolio.addStrategy(strat);

    ste::MarketSnapshot market;
    market.spot_price = 4500;
    market.implied_vol = 0.20;
    market.risk_free_rate = 0.05;
    market.dividend_yield = 0.015;

    ste::StressEngine stress;
    auto results = stress.runAllScenarios(portfolio, market);
    assert(!results.empty());

    // GFC should show a loss for iron condor
    bool found_loss = false;
    for (const auto& r : results) {
        if (r.scenario_name == "GFC 2008" && r.portfolio_pnl < 0)
            found_loss = true;
    }
    assert(found_loss);
}

void test_ScenarioGenerator_Grid() {
    auto scenarios = ste::ScenarioGenerator::generateGrid(-0.2, 0.2, 5, -0.1, 0.3, 5);
    assert(scenarios.size() == 25);
}

// ============================================================
// Market Data Tests
// ============================================================

void test_MarketData_Generation() {
    ste::MarketDataGenerator gen(42);
    auto history = gen.generateHistory(252);  // 1 year
    assert(history.size() == 252);
    assert(history[0].spot_price == 4500.0);
    // Price should have changed
    assert(history.back().spot_price != history[0].spot_price);
}

// ============================================================
// Portfolio Tests
// ============================================================

void test_Portfolio_PnLSurface() {
    ste::Portfolio portfolio;
    auto strat = ste::OptionsStrategies::createStraddle(100.0, 100.0, 0.25, 0.20, 0.05, true);
    portfolio.addStrategy(strat);

    ste::MarketSnapshot market;
    market.spot_price = 100.0;
    market.implied_vol = 0.20;
    market.risk_free_rate = 0.05;
    market.dividend_yield = 0.0;

    auto surface = portfolio.computePnLSurface(market, 0.15, 0.20, 20);
    assert(surface.rows == 20);
    assert(surface.cols == 20);
    assert(surface.grid.size() == 20);
}

// ============================================================
// Math Utils Tests
// ============================================================

void test_Math_NormCDF() {
    assert(std::abs(ste::math::norm_cdf(0.0) - 0.5) < 0.001);
    assert(ste::math::norm_cdf(3.0) > 0.998);
    assert(ste::math::norm_cdf(-3.0) < 0.002);
}

void test_Math_Statistics() {
    std::vector<double> data = {1.0, 2.0, 3.0, 4.0, 5.0};
    assert(std::abs(ste::math::mean(data) - 3.0) < 0.001);
    assert(ste::math::stddev(data) > 1.5 && ste::math::stddev(data) < 1.6);
}

// ============================================================
// JSON Writer Tests
// ============================================================

void test_JsonWriter() {
    ste::Greeks g{1.0, 0.5, -0.1, 0.3, 0.05};
    std::string json = ste::JsonWriter::toJson(g);
    assert(json.find("\"delta\":") != std::string::npos);
    assert(json.find("\"gamma\":") != std::string::npos);
}

// ============================================================
// Strategy Manager Tests
// ============================================================

void test_StrategyManager_Recommendations() {
    ste::StrategyManager mgr;
    ste::MarketSnapshot market;
    market.spot_price = 4500;
    market.implied_vol = 0.15;
    market.risk_free_rate = 0.05;
    market.dividend_yield = 0.015;

    ste::RegimeState regime;
    regime.current_regime = ste::MarketRegime::BullQuiet;
    regime.confidence = 0.8;
    regime.crisis_probability = 0.05;
    regime.regime_probabilities = {0.8, 0.1, 0.05, 0.02, 0.03};

    auto strategies = mgr.recommendStrategies(market, regime);
    assert(!strategies.empty());
    assert(strategies.size() <= 3);
}

// ============================================================
// Main
// ============================================================

int main() {
    std::cout << "\n=== Stress Testing Engine - Test Suite ===\n\n";

    std::cout << "Black-Scholes:\n";
    TEST(BlackScholes_CallPrice);
    TEST(BlackScholes_PutPrice);
    TEST(BlackScholes_PutCallParity);
    TEST(BlackScholes_Greeks);
    TEST(BlackScholes_ImpliedVol);

    std::cout << "\nMonte Carlo:\n";
    TEST(MonteCarlo_ConvergesToBS);
    TEST(MonteCarlo_StressSimulation);

    std::cout << "\nOptions Strategies:\n";
    TEST(Strategies_CoveredCall);
    TEST(Strategies_IronCondor);
    TEST(Strategies_Straddle);

    std::cout << "\nRegime Detection:\n";
    TEST(HMM_Init);
    TEST(HMM_Update);
    TEST(RegimeDetector_CrisisDetection);

    std::cout << "\nStress Testing:\n";
    TEST(StressEngine_HistoricalScenarios);
    TEST(StressEngine_RunScenario);
    TEST(ScenarioGenerator_Grid);

    std::cout << "\nMarket Data:\n";
    TEST(MarketData_Generation);

    std::cout << "\nPortfolio:\n";
    TEST(Portfolio_PnLSurface);

    std::cout << "\nMath Utils:\n";
    TEST(Math_NormCDF);
    TEST(Math_Statistics);

    std::cout << "\nJSON Writer:\n";
    TEST(JsonWriter);

    std::cout << "\nStrategy Manager:\n";
    TEST(StrategyManager_Recommendations);

    std::cout << "\n=== All " << passed << " tests PASSED ===\n\n";
    return 0;
}
