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
#include "live/live_data_feed.h"
#include "execution/execution_engine.h"

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

// ============================================================
// Simulation Mode (original backtest)
// ============================================================
int runSimulation(int port, int sim_days, int speed_ms, double initial_price,
                  bool headless, ste::Timeframe timeframe) {

    int sim_periods = ste::daysToPeriodsCount(sim_days, timeframe);

    std::cout << "[*] Initializing market data generator...\n";
    ste::MarketDataGenerator datagen(42);

    std::cout << "[*] Generating " << sim_periods << " " << ste::timeframeName(timeframe)
              << " bars (" << sim_days << " trading days) of synthetic market data...\n";
    auto history = datagen.generateHistory(sim_periods, initial_price, timeframe);
    std::cout << "    Generated " << history.size() << " market snapshots ("
              << ste::timeframeName(timeframe) << " timeframe)\n";

    ste::CsvParser::saveMarketData("data/market_data.csv", history);

    std::cout << "[*] Initializing regime detector (Hidden Markov Model, "
              << ste::timeframeName(timeframe) << ")...\n";
    ste::RegimeDetector detector(timeframe);

    std::cout << "[*] Initializing portfolio...\n";
    ste::Portfolio portfolio;
    ste::StrategyManager strategy_mgr;

    std::cout << "[*] Initializing stress testing engine...\n";
    ste::StressEngine stress;
    auto tail_scenarios = ste::ScenarioGenerator::generateTailRisk(10);
    for (const auto& s : tail_scenarios) stress.addScenario(s);
    std::cout << "    Loaded " << stress.scenarios().size() << " stress scenarios\n";

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
    int print_interval = ste::daysToPeriodsCount(20, timeframe);
    int rebalance_interval = ste::daysToPeriodsCount(20, timeframe);

    for (size_t day = 0; day < history.size() && g_running.load(); ++day) {
        const auto& market = history[day];

        auto regime_state = detector.update(market);
        auto signal = detector.generateSignal(regime_state, market);

        bool regime_changed = (regime_state.current_regime != last_regime);
        rebalance_counter++;

        if (regime_changed || rebalance_counter >= rebalance_interval || day == 0) {
            auto recommended = strategy_mgr.recommendStrategies(market, regime_state);
            while (!portfolio.strategies().empty())
                portfolio.removeStrategy(0);
            for (const auto& strat : recommended)
                portfolio.addStrategy(strat);
            portfolio.setCashAllocation(signal.target_cash_pct);
            rebalance_counter = 0;
        }

        if (day > 0) {
            double spot_ret = (market.spot_price - history[day-1].spot_price) /
                               history[day-1].spot_price;
            double port_ret = spot_ret * (1.0 - portfolio.cashAllocation());
            double prev_value = portfolio.totalValue(history[day-1]);
            if (prev_value > 0) {
                port_ret = (portfolio.totalValue(market) - prev_value) / prev_value;
            }
            portfolio.recordReturn(market.timestamp, port_ret, spot_ret);
        }

        if (day % print_interval == 0 || regime_changed) {
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

        if (!headless) {
            broadcaster.broadcast(market);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(speed_ms));
    }

    std::cout << "\n" << std::string(70, '=') << std::endl;
    std::cout << "\n[*] Simulation complete!\n\n";

    auto final_state = portfolio.computeState(history.back());
    std::cout << "  === FINAL RESULTS ===\n"
              << "  Portfolio Value:  $" << std::fixed << std::setprecision(0) << final_state.total_value << "\n"
              << "  Portfolio Return: " << std::setprecision(2) << final_state.portfolio_return * 100.0 << "%\n"
              << "  SP500 Return:     " << final_state.benchmark_return * 100.0 << "%\n"
              << "  Alpha:            " << (final_state.portfolio_return - final_state.benchmark_return) * 100.0 << "%\n"
              << "  Sharpe Ratio:     " << final_state.sharpe_ratio << "\n"
              << "  Max Drawdown:     " << std::setprecision(1) << final_state.max_drawdown * 100.0 << "%\n"
              << "  Regime Changes:   " << detector.regimeHistory().size() << "\n\n";

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

// ============================================================
// Live Mode - Real-time regime detection and execution
// ============================================================
int runLive(int port, bool headless, ste::Timeframe timeframe,
            const std::string& data_source, const std::string& api_key,
            double initial_capital, bool paper_trade) {

    std::cout << "\n\033[1;36m" << std::string(70, '=') << "\033[0m\n";
    std::cout << "\033[1;36m  LIVE MODE";
    if (paper_trade) std::cout << " (PAPER TRADING)";
    std::cout << "\033[0m\n";
    std::cout << "\033[1;36m" << std::string(70, '=') << "\033[0m\n\n";

    // --- Live Data Feed ---
    std::cout << "[*] Initializing live data feed...\n";
    ste::LiveFeedConfig feed_config;
    feed_config.api_provider = data_source;
    feed_config.api_key = api_key;
    feed_config.timeframe = timeframe;

    // Set poll interval based on timeframe
    if (timeframe == ste::Timeframe::Minute) {
        feed_config.poll_interval_ms = 5000;      // 5 seconds
    } else if (timeframe == ste::Timeframe::Hourly) {
        feed_config.poll_interval_ms = 60000;     // 1 minute
    } else {
        feed_config.poll_interval_ms = 300000;    // 5 minutes for daily
    }

    ste::LiveDataFeed feed(feed_config);

    // Create appropriate provider
    if (data_source == "mock") {
        feed.setProvider(std::make_unique<ste::MockLiveProvider>());
    }
    // else: default provider from config (yahoo)

    // --- Regime Detector ---
    std::cout << "[*] Initializing regime detector (HMM, " << ste::timeframeName(timeframe) << ")...\n";
    ste::RegimeDetector detector(timeframe);

    // --- Portfolio & Strategy ---
    std::cout << "[*] Initializing portfolio...\n";
    ste::Portfolio portfolio;
    ste::StrategyManager strategy_mgr;

    // --- Stress Engine ---
    std::cout << "[*] Initializing stress testing engine...\n";
    ste::StressEngine stress;
    auto tail_scenarios = ste::ScenarioGenerator::generateTailRisk(10);
    for (const auto& s : tail_scenarios) stress.addScenario(s);

    // --- Execution Engine ---
    std::cout << "[*] Initializing execution engine...\n";
    auto exec_engine = std::make_unique<ste::PaperTradingEngine>(initial_capital);
    exec_engine->connect();

    ste::OrderManagerConfig order_config;
    order_config.equity_symbol = "SPY";
    ste::OrderManager order_mgr(*exec_engine, order_config);

    // --- Web Server ---
    ste::WebServer server(port);
    ste::DataBroadcaster broadcaster(server, portfolio, detector, stress);

    if (!headless) {
        std::cout << "[*] Starting web visualization server...\n";
        server.start();
    }

    // --- Main Live Loop State ---
    ste::MarketRegime last_regime = ste::MarketRegime::BullQuiet;
    int tick_count = 0;
    int rebalance_counter = 0;
    int print_interval = ste::daysToPeriodsCount(1, timeframe); // print every "day equivalent"
    int rebalance_interval = ste::daysToPeriodsCount(5, timeframe); // rebalance every 5 days
    ste::MarketSnapshot prev_snapshot{};
    bool has_prev = false;

    // --- Register data callback ---
    feed.onData([&](const ste::MarketSnapshot& market) {
        tick_count++;

        // Update market prices in execution engine
        exec_engine->updateMarketPrice("SPY", market.spot_price / 10.0);
        exec_engine->updateMarketPrice("^GSPC", market.spot_price);

        // Regime detection
        auto regime_state = detector.update(market);
        auto signal = detector.generateSignal(regime_state, market);

        bool regime_changed = (regime_state.current_regime != last_regime);
        rebalance_counter++;

        // Portfolio strategy update
        if (regime_changed || rebalance_counter >= rebalance_interval || tick_count == 1) {
            auto recommended = strategy_mgr.recommendStrategies(market, regime_state);
            while (!portfolio.strategies().empty())
                portfolio.removeStrategy(0);
            for (const auto& strat : recommended)
                portfolio.addStrategy(strat);
            portfolio.setCashAllocation(signal.target_cash_pct);
            rebalance_counter = 0;

            // Execute trades through execution engine
            auto batch = order_mgr.processSignal(signal, market, regime_state);
            if (!batch.orders.empty()) {
                order_mgr.executeBatch(batch);
            }
        }

        // Record returns
        if (has_prev && prev_snapshot.spot_price > 0) {
            double spot_ret = (market.spot_price - prev_snapshot.spot_price) / prev_snapshot.spot_price;
            double prev_value = portfolio.totalValue(prev_snapshot);
            double port_ret = (prev_value > 0) ?
                (portfolio.totalValue(market) - prev_value) / prev_value :
                spot_ret * (1.0 - portfolio.cashAllocation());
            portfolio.recordReturn(market.timestamp, port_ret, spot_ret);
        }

        // Print status
        if (tick_count % print_interval == 0 || regime_changed || tick_count == 1) {
            if (regime_changed) {
                std::cout << "\n\033[1;91m  >>> LIVE REGIME CHANGE DETECTED <<<\033[0m\n";
            }

            std::cout << "\n  Tick " << std::setw(6) << tick_count
                      << " | SP500: $" << std::fixed << std::setprecision(2) << market.spot_price
                      << " | VIX: " << std::setprecision(1) << market.vix;
            printRegime(regime_state);
            printSignal(signal);

            // Account state from execution engine
            auto account = exec_engine->accountState();
            auto port_state = portfolio.computeState(market);
            std::cout << "  Account: $" << std::setprecision(0) << account.total_equity
                      << " (Cash: $" << account.cash << ")"
                      << " | Positions: " << account.positions.size()
                      << " | Open Orders: " << account.open_orders << "\n";
            std::cout << "  Portfolio: Return=" << std::setprecision(2)
                      << port_state.portfolio_return * 100.0 << "%"
                      << " | Benchmark=" << port_state.benchmark_return * 100.0 << "%"
                      << " | Alpha=" << (port_state.portfolio_return - port_state.benchmark_return) * 100.0 << "%"
                      << " | Sharpe=" << port_state.sharpe_ratio << "\n";
        }

        last_regime = regime_state.current_regime;
        prev_snapshot = market;
        has_prev = true;

        // Broadcast to web dashboard
        if (!headless) {
            broadcaster.broadcast(market);
        }
    });

    // --- Start Feed ---
    std::cout << "\n[*] Starting live data feed...\n";
    if (!feed.start()) {
        std::cerr << "[!] Failed to start live data feed. Aborting.\n";
        return 1;
    }

    std::cout << "\n\033[1;32m[*] LIVE MODE ACTIVE - Press Ctrl+C to stop.\033[0m\n";
    if (!headless) {
        std::cout << "[*] Dashboard: http://localhost:" << port << "\n";
    }
    std::cout << std::string(70, '=') << "\n\n";

    // --- Keep running ---
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // Periodic feed health check
        auto stats = feed.stats();
        if (!stats.connected && stats.errors > 5) {
            std::cerr << "\n[!] Feed disconnected with " << stats.errors << " errors.\n";
        }
    }

    // --- Shutdown ---
    std::cout << "\n\n[*] Shutting down live mode...\n";
    feed.stop();

    // Print final execution summary
    auto final_account = exec_engine->accountState();
    auto paper_stats = exec_engine->paperStats();
    auto final_port = portfolio.computeState(prev_snapshot);

    std::cout << "\n  === LIVE SESSION RESULTS ===\n"
              << "  Ticks Processed:   " << tick_count << "\n"
              << "  Account Equity:    $" << std::fixed << std::setprecision(0) << final_account.total_equity << "\n"
              << "  P&L:               $" << std::setprecision(2) << paper_stats.total_pnl << "\n"
              << "  Total Trades:      " << paper_stats.total_trades << "\n"
              << "  Win Rate:          " << std::setprecision(1) << paper_stats.win_rate * 100.0 << "%\n"
              << "  Portfolio Return:  " << std::setprecision(2) << final_port.portfolio_return * 100.0 << "%\n"
              << "  Benchmark Return:  " << final_port.benchmark_return * 100.0 << "%\n"
              << "  Alpha:             " << (final_port.portfolio_return - final_port.benchmark_return) * 100.0 << "%\n"
              << "  Regime Changes:    " << detector.regimeHistory().size() << "\n\n";

    // Trade log
    const auto& trade_log = order_mgr.tradeLog();
    if (!trade_log.empty()) {
        std::cout << "  === TRADE LOG ===\n";
        for (const auto& t : trade_log) {
            std::cout << "  " << t.action << " | $" << std::setprecision(0) << t.amount
                      << " | " << t.reason << "\n";
        }
    }

    exec_engine->disconnect();

    if (!headless) {
        server.stop();
    }

    return 0;
}

// ============================================================
// Main Entry Point
// ============================================================
int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    printBanner();

    // Configuration
    int port = 8080;
    int sim_days = 756;
    int speed_ms = 100;
    double initial_price = 4500.0;
    double initial_capital = 1000000.0;
    bool headless = false;
    bool live_mode = false;
    bool paper_trade = true;
    std::string data_source = "mock";   // "yahoo", "mock"
    std::string api_key;
    ste::Timeframe timeframe = ste::Timeframe::Daily;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) port = std::stoi(argv[++i]);
        else if (arg == "--days" && i + 1 < argc) sim_days = std::stoi(argv[++i]);
        else if (arg == "--speed" && i + 1 < argc) speed_ms = std::stoi(argv[++i]);
        else if (arg == "--price" && i + 1 < argc) initial_price = std::stod(argv[++i]);
        else if (arg == "--capital" && i + 1 < argc) initial_capital = std::stod(argv[++i]);
        else if (arg == "--timeframe" && i + 1 < argc) {
            std::string tf = argv[++i];
            if (tf == "hourly" || tf == "1h") timeframe = ste::Timeframe::Hourly;
            else if (tf == "minute" || tf == "1m") timeframe = ste::Timeframe::Minute;
            else timeframe = ste::Timeframe::Daily;
        }
        else if (arg == "--live") live_mode = true;
        else if (arg == "--data-source" && i + 1 < argc) data_source = argv[++i];
        else if (arg == "--api-key" && i + 1 < argc) api_key = argv[++i];
        else if (arg == "--paper") paper_trade = true;
        else if (arg == "--headless") headless = true;
        else if (arg == "--help") {
            std::cout << "Usage: stress_engine [options]\n\n"
                      << "  MODES:\n"
                      << "    (default)           Simulation/backtest mode\n"
                      << "    --live              Live mode with real-time regime detection\n\n"
                      << "  SIMULATION OPTIONS:\n"
                      << "    --days <N>          Simulation days (default: 756)\n"
                      << "    --speed <ms>        Speed between frames (default: 100)\n"
                      << "    --price <N>         Initial SP500 price (default: 4500)\n\n"
                      << "  LIVE OPTIONS:\n"
                      << "    --data-source <s>   Data source: mock, yahoo (default: mock)\n"
                      << "    --api-key <key>     API key for data provider\n"
                      << "    --capital <N>       Initial capital (default: 1000000)\n"
                      << "    --paper             Paper trading mode (default: on)\n\n"
                      << "  COMMON OPTIONS:\n"
                      << "    --port <N>          Web server port (default: 8080)\n"
                      << "    --timeframe <tf>    daily, hourly/1h, minute/1m (default: daily)\n"
                      << "    --headless          Run without web server\n"
                      << "    --help              Show this help\n\n"
                      << "  EXAMPLES:\n"
                      << "    stress_engine                           # Backtest simulation\n"
                      << "    stress_engine --live --data-source mock # Live with mock data\n"
                      << "    stress_engine --live --data-source yahoo --timeframe 1m\n"
                      << "    stress_engine --live --headless         # Live, console only\n";
            return 0;
        }
    }

    if (live_mode) {
        return runLive(port, headless, timeframe, data_source, api_key,
                       initial_capital, paper_trade);
    } else {
        return runSimulation(port, sim_days, speed_ms, initial_price, headless, timeframe);
    }
}
