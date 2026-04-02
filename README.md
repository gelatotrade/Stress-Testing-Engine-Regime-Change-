# Stress Testing Engine with Regime Change Detection

A high-performance C++ engine for stress testing options portfolio strategies with real-time 3D visualization, Hidden Markov Model regime detection, and early warning signals for optimal risk allocation vs. S&P 500 benchmark.

## Features

### Options Pricing & Greeks
- **Black-Scholes** analytical pricing with full Greeks (Delta, Gamma, Theta, Vega, Rho)
- **Monte Carlo simulation** with antithetic variates and control variates
- **Implied volatility** solver via Newton-Raphson
- **Regime-switching Monte Carlo** with stochastic volatility transitions

### 10+ Options Strategies
- Covered Call, Protective Put, Collar
- Bull Call Spread, Bear Put Spread
- Iron Condor, Iron Butterfly
- Long/Short Straddle, Long/Short Strangle
- Calendar Spread, Ratio Spread

### Hidden Markov Model Regime Detection
- 5-state market regime model: Bull Quiet, Bull Volatile, Bear Quiet, Bear Volatile, Transition
- Online Bayesian updating with Forward algorithm
- Viterbi decoding for most likely regime sequence
- Baum-Welch training for parameter optimization
- Multi-factor feature extraction: returns, volatility, credit spreads, volume

### Early Warning System
- **Crisis probability tracking** with real-time alerts
- **Multi-factor warning score**: vol acceleration, price momentum, credit spread widening, HMM transition probability
- **Trading signals**: Strong Buy, Buy, Hold, Reduce Risk, Go To Cash, Crisis
- **Dynamic allocation targets**: Cash/Equity/Options percentages per regime

### Stress Testing
- **8 historical scenarios**: Black Monday 1987, Dot-Com 2000, GFC 2008, Flash Crash 2010, Volmageddon 2018, COVID 2020, Meme Stocks 2021, Rate Hike 2022
- **Parametric grid stress tests**
- **Tail risk scenario generation**
- **Correlated multi-factor scenarios**
- **Reverse stress testing** (find scenarios causing target loss)
- **VaR and CVaR** computation

### 3D Live Visualization Dashboard
- **Real-time 3D P&L surface** (Spot x Volatility x P&L) using Three.js/WebGL
- **Regime change timeline** with color-coded history
- **Trading signal display** with allocation bars
- **Stress test results table**
- **Portfolio metrics panel**: Value, Return, Alpha, Sharpe, Max Drawdown, Greeks
- **Early warning progress bar**
- **Regime probability distribution** bars
- Auto-rotating 3D camera with orbit controls

## Architecture

```
src/
├── core/               # Pricing engine
│   ├── black_scholes   # Analytical options pricing & Greeks
│   ├── monte_carlo     # MC simulation with regime switching
│   ├── portfolio       # Portfolio management & P&L surfaces
│   └── market_data     # Synthetic market data generator
├── strategies/         # Options strategy library
│   ├── options_strategies  # 10+ strategy factories
│   └── strategy_manager    # Regime-based strategy selection
├── regime/             # Regime detection
│   ├── hidden_markov_model # Full HMM implementation
│   └── regime_detector     # Feature extraction & signal generation
├── stress/             # Stress testing
│   ├── stress_engine       # Main stress test runner
│   ├── scenario_generator  # Parametric & tail risk scenarios
│   └── historical_scenarios # Pre-built crisis scenarios
├── visualization/      # Live dashboard
│   ├── web_server      # Embedded HTTP + SSE server
│   └── data_broadcaster # Real-time data serialization
└── utils/              # Utilities
    ├── math_utils      # Statistical functions
    ├── json_writer     # JSON serialization
    └── csv_parser      # Data I/O
```

## Build & Run

### Requirements
- C++20 compiler (GCC 10+, Clang 12+)
- CMake 3.16+
- POSIX threads

### Build
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Run Tests
```bash
./build/run_tests
```

### Run Engine
```bash
# Full simulation with live 3D dashboard
./build/stress_engine

# Then open http://localhost:8080 in your browser

# Custom options
./build/stress_engine --port 3000 --days 1000 --speed 50 --price 5000

# Headless mode (terminal only)
./build/stress_engine --headless --days 500 --speed 0
```

### CLI Options
| Option | Default | Description |
|--------|---------|-------------|
| `--port` | 8080 | Web dashboard port |
| `--days` | 756 | Simulation trading days (756 = 3 years) |
| `--speed` | 100 | Milliseconds between frames |
| `--price` | 4500 | Initial S&P 500 price |
| `--headless` | false | Run without web server |

## How It Works

1. **Market Data Generation**: Synthetic S&P 500 data with realistic regime transitions using a Markov chain
2. **Regime Detection**: HMM processes daily features (returns, vol, credit spreads) and computes regime probabilities
3. **Signal Generation**: Early warning system combines crisis probability, vol acceleration, price momentum, and credit spread widening
4. **Portfolio Management**: Strategy manager selects optimal options strategies per regime (e.g., Iron Condors in Bull Quiet, Protective Puts in Bear Volatile)
5. **Dynamic Allocation**: Cash/equity/options ratios adjusted based on regime signals to outperform S&P 500 with lower drawdown
6. **Stress Testing**: Every frame runs all scenarios against current portfolio
7. **3D Visualization**: P&L surface, regime timeline, and metrics streamed via SSE to browser

## Regime-Strategy Mapping

| Regime | Recommended Strategies | Cash Target |
|--------|----------------------|-------------|
| Bull Quiet | Covered Call, Iron Condor, Bull Call Spread | 15% |
| Bull Volatile | Collar, Straddle, Covered Call | 25% |
| Bear Quiet | Bear Put Spread, Collar, Protective Put | 40% |
| Bear Volatile | Protective Put, Collar (CRISIS MODE) | 60-70% |
| Transition | Straddle, Strangle, Collar | 35% |
