#include "core/types.h"
#include "core/portfolio.h"
#include "core/market_data.h"
#include "core/monte_carlo.h"
#include "strategies/options_strategies.h"
#include "strategies/strategy_manager.h"
#include "regime/regime_detector.h"
#include "stress/stress_engine.h"
#include "stress/scenario_generator.h"
#include "visualization/web_server.h"
#include "visualization/data_broadcaster.h"
#include "utils/csv_parser.h"

#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>

std::atomic<bool> g_running{true};

void signalHandler(int) {
    g_running = false;
}

void printBanner() {
    std::cout << R"(
 ____  _____ ____  _____ ____ ____    _____ _____ ____ _____ ___ _   _  ____
/ ___|_   _|  _ \| ____/ ___/ ___|  |_   _| ____/ ___|_   _|_ _| \ | |/ ___|
\___ \ | | | |_) |  _| \___ \___ \    | | |  _| \___ \ | |  | ||  \| | |  _
 ___) || | |  _ <| |___ ___) |__) |   | | | |___ ___) || |  | || |\  | |_| |
|____/ |_| |_| \_\_____|____/____/    |_| |_____|____/ |_| |___|_| \_|\____|

    _____ _   _  ____ ___ _   _ _____
   | ____| \ | |/ ___|_ _| \ | | ____|
   |  _| |  \| | |  _ | ||  \| |  _|
   | |___| |\  | |_| || || |\  | |___
   |_____|_| \_|\____|___|_| \_|_____|

    Options Portfolio Stress Testing with Regime Detection
    ======================================================
    3D Live Visualization | Hidden Markov Model Regimes
    Early Warning System  | SP500 Benchmark Tracking
)" << std::endl;
}

void printRegime(const ste::RegimeState& state) {
    const char* names[] = {"BULL QUIET", "BULL VOLATILE", "BEAR QUIET", "BEAR VOLATILE", "TRANSITION"};
    const char* colors[] = {"\033[32m", "\033[92m", "\033[31m", "\033[91m", "\033[33m"};
    int idx = static_cast<int>(state.current_regime);

    std::cout << colors[idx] << "  REGIME: " << names[idx]
              << " (confidence: " << std::fixed << std::setprecision(1)
              << state.confidence * 100.0 << "%, crisis prob: "
              << state.crisis_probability * 100.0 << "%)\033[0m" << std::endl;
}

void printSignal(const ste::TradingSignal& signal) {
    const char* sigNames[] = {"STRONG BUY", "BUY", "HOLD", "REDUCE RISK", "GO TO CASH", "CRISIS"};
    const char* sigColors[] = {"\033[92m", "\033[32m", "\033[33m", "\033[33m", "\033[31m", "\033[91m"};
    int idx = static_cast<int>(signal.signal);

    std::cout << sigColors[idx] << "  SIGNAL: " << sigNames[idx]
              << " | Cash: " << static_cast<int>(signal.target_cash_pct * 100) << "%"
              << " Equity: " << static_cast<int>(signal.target_equity_pct * 100) << "%"
              << " Options: " << static_cast<int>(signal.target_options_pct * 100) << "%"
              << "\033[0m" << std::endl;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    printBanner();

    // Configuration
    int port = 8080;
    int sim_days = 756;         // 3 years of trading days
    int speed_ms = 100;         // ms between frames
    double initial_price = 4500.0;
    bool headless = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "--days" && i + 1 < argc) sim_days = std::stoi(argv[++i]);
        else if (arg == "--speed" && i + 1 < argc) speed_ms = std::stoi(argv[++i]);
        else if (arg == "--price" && i + 1 < argc) initial_price = std::stod(argv[++i]);
        else if (arg == "--headless") headless = true;
        else if (arg == "--help") {
            std::cout << "Usage: stress_engine [options]\n"
                      << "  --port <N>     Web server port (default: 8080)\n"
                      << "  --days <N>     Simulation days (default: 756)\n"
                      << "  --speed <ms>   Speed in ms between frames (default: 100)\n"
                      << "  --price <N>    Initial SP500 price (default: 4500)\n"
                      << "  --headless     Run without web server\n"
                      << "  --help         Show this help\n";
            return 0;
        }
    }

    // Initialize components
    std::cout << "[*] Initializing market data generator...\n";
    ste::MarketDataGenerator datagen(42);

    std::cout << "[*] Generating " << sim_days << " days of synthetic market data...\n";
    auto history = datagen.generateHistory(sim_days, initial_price);
    std::cout << "    Generated " << history.size() << " market snapshots\n";

    // Save generated data
    ste::CsvParser::saveMarketData("data/market_data.csv", history);

    std::cout << "[*] Initializing regime detector (Hidden Markov Model)...\n";
    ste::RegimeDetector detector;

    std::cout << "[*] Initializing portfolio...\n";
    ste::Portfolio portfolio;
    ste::StrategyManager strategy_mgr;

    std::cout << "[*] Initializing stress testing engine...\n";
    ste::StressEngine stress;
    auto tail_scenarios = ste::ScenarioGenerator::generateTailRisk(10);
    for (const auto& s : tail_scenarios) stress.addScenario(s);
    std::cout << "    Loaded " << stress.scenarios().size() << " stress scenarios\n";

    // Web server
    ste::WebServer server(port);
    ste::DataBroadcaster broadcaster(server, portfolio, detector, stress);

    if (!headless) {
        std::cout << "[*] Starting web visualization server...\n";
        server.start();
    }

    std::cout << "\n[*] Starting simulation...\n\n";
    std::cout << std::string(70, '=') << std::endl;

    ste::MarketRegime last_regime = ste::MarketRegime::BullQuiet;
    int rebalance_counter = 0;

    for (size_t day = 0; day < history.size() && g_running.load(); ++day) {
        const auto& market = history[day];

        // Update regime detection
        auto regime_state = detector.update(market);

        // Generate trading signal
        auto signal = detector.generateSignal(regime_state, market);

        // Rebalance portfolio periodically or on regime change
        bool regime_changed = (regime_state.current_regime != last_regime);
        rebalance_counter++;

        if (regime_changed || rebalance_counter >= 20 || day == 0) {
            // Get strategy recommendations
            auto recommended = strategy_mgr.recommendStrategies(market, regime_state);

            // Clear and rebuild portfolio
            while (!portfolio.strategies().empty())
                portfolio.removeStrategy(0);
            for (const auto& strat : recommended)
                portfolio.addStrategy(strat);

            // Adjust cash allocation
            portfolio.setCashAllocation(signal.target_cash_pct);
            rebalance_counter = 0;
        }

        // Record returns
        if (day > 0) {
            double spot_ret = (market.spot_price - history[day-1].spot_price) /
                               history[day-1].spot_price;
            double port_ret = spot_ret * (1.0 - portfolio.cashAllocation());
            // Adjust portfolio return by option P&L
            double opt_pnl = portfolio.totalPnL(market);
            double prev_value = portfolio.totalValue(history[day-1]);
            if (prev_value > 0) {
                port_ret = (portfolio.totalValue(market) - prev_value) / prev_value;
            }
            portfolio.recordReturn(market.timestamp, port_ret, spot_ret);
        }

        // Print status every 20 days or on regime change
        if (day % 20 == 0 || regime_changed) {
            if (regime_changed) {
                std::cout << "\n\033[1m  >>> REGIME CHANGE DETECTED at day "
                          << day << " <<<\033[0m\n";
            }

            std::cout << "\n  Day " << std::setw(4) << day
                      << " | SP500: $" << std::fixed << std::setprecision(0) << market.spot_price
                      << " | VIX: " << std::setprecision(1) << market.vix;
            printRegime(regime_state);
            printSignal(signal);

            auto state = portfolio.computeState(market);
            std::cout << "  Portfolio: $" << std::setprecision(0) << state.total_value
                      << " | Return: " << std::setprecision(2)
                      << state.portfolio_return * 100.0 << "%"
                      << " | Benchmark: " << state.benchmark_return * 100.0 << "%"
                      << " | Alpha: "
                      << (state.portfolio_return - state.benchmark_return) * 100.0 << "%"
                      << " | Sharpe: " << state.sharpe_ratio
                      << " | MaxDD: " << std::setprecision(1)
                      << state.max_drawdown * 100.0 << "%\n";
            std::cout << "  Greeks: Delta=" << std::setprecision(2) << state.total_delta
                      << " Gamma=" << state.total_gamma
                      << " Theta=" << state.total_theta
                      << " Vega=" << state.total_vega << "\n";
        }

        last_regime = regime_state.current_regime;

        // Broadcast visualization frame
        if (!headless) {
            broadcaster.broadcast(market);
        }

        // Control simulation speed
        std::this_thread::sleep_for(std::chrono::milliseconds(speed_ms));
    }

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "\n[*] Simulation complete!\n\n";

    // Final summary
    auto final_state = portfolio.computeState(history.back());
    std::cout << "  === FINAL RESULTS ===\n"
              << "  Portfolio Value:  $" << std::fixed << std::setprecision(0) << final_state.total_value << "\n"
              << "  Portfolio Return: " << std::setprecision(2) << final_state.portfolio_return * 100.0 << "%\n"
              << "  SP500 Return:     " << final_state.benchmark_return * 100.0 << "%\n"
              << "  Alpha:            " << (final_state.portfolio_return - final_state.benchmark_return) * 100.0 << "%\n"
              << "  Sharpe Ratio:     " << final_state.sharpe_ratio << "\n"
              << "  Max Drawdown:     " << std::setprecision(1) << final_state.max_drawdown * 100.0 << "%\n"
              << "  Regime Changes:   " << detector.regimeHistory().size() << "\n\n";

    // Run final stress test
    std::cout << "  === FINAL STRESS TEST ===\n";
    auto results = stress.runAllScenarios(portfolio, history.back());
    ste::CsvParser::saveResults("data/stress_results.csv", results);

    for (const auto& r : results) {
        const char* color = r.portfolio_pnl >= 0 ? "\033[32m" : "\033[31m";
        std::cout << "  " << std::setw(25) << std::left << r.scenario_name
                  << color << std::right << std::setw(12)
                  << std::setprecision(0) << "$" << r.portfolio_pnl
                  << " (" << std::setprecision(1) << r.portfolio_pnl_pct * 100.0 << "%)"
                  << "\033[0m\n";
    }

    std::cout << "\n[*] Results saved to data/stress_results.csv\n";

    if (!headless) {
        std::cout << "\n[*] Dashboard still running at http://localhost:" << port
                  << "\n    Press Ctrl+C to exit...\n";
        while (g_running.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        server.stop();
    }

    return 0;
}
