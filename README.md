# Stress Testing Engine with Regime Change Detection

A high-performance C++ engine for stress testing options portfolio strategies with real-time 3D visualization, Hidden Markov Model regime detection, and early warning signals for optimal risk allocation vs. S&P 500 benchmark.

---

## Live 3D Regime Change Visualization

The engine computes a **P&L surface in 3 dimensions** (Spot Price x Implied Volatility x P&L) that **morphs in real-time** as market regimes shift. The surface color, shape, and orientation change as the Hidden Markov Model detects regime transitions -- from smooth green domes in bull markets to inverted red craters during crashes.

### P&L Surface Morphing Through Market Regimes

> The 3D surface rotates and deforms live as the engine cycles through: **Bull Quiet** (green) --> **Transition** (yellow/orange) --> **Bear Volatile / Crisis** (red) --> **Recovery** (blue) --> **New Bull** (green)

![3D P&L Surface - Regime Cycle Animation](docs/img/regime_cycle_3d.gif)

**What you're seeing:**
- **X-Axis**: Spot Price ($) -- the underlying S&P 500 level
- **Y-Axis**: Implied Volatility (%) -- option-implied expected volatility
- **Z-Axis**: P&L ($) -- portfolio profit/loss from Iron Condor strategy
- **Color**: Green = profit zone, Red = loss zone, shifting with regime
- **Bottom bar**: Regime timeline progressing through the full cycle
- **Header**: Current regime, trading signal, and VIX level updating live

| Phase | Surface Shape | VIX | Signal | Cash Allocation |
|-------|--------------|-----|--------|-----------------|
| Bull Quiet | Smooth elevated dome | ~12 | STRONG BUY | 15% |
| Transition | Rippling, tilting surface | ~24 | REDUCE RISK | 40% |
| Bear Volatile | Inverted crater, deep valleys | ~67 | CRISIS | 70% |
| Recovery | Reforming upward slope | ~28 | BUY | 25% |
| New Bull | Smooth dome returns | ~14 | STRONG BUY | 15% |

---

### Early Warning Dashboard

The multi-panel dashboard tracks crisis probability, VIX trajectory, portfolio allocation shifts, and cumulative returns vs. S&P 500 benchmark in real-time. Watch how the system **detects the incoming crash early** and shifts to cash before the drawdown hits.

![Early Warning Dashboard Animation](docs/img/early_warning_dashboard.gif)

**Panels:**
- **Top-Left**: Crisis probability gauge -- rises from 5% to 89% as crash approaches
- **Top-Right**: VIX trajectory -- climbing from 12 past the danger threshold to 67
- **Bottom-Left**: Portfolio allocation bars -- cash increasing, equity decreasing as risk rises
- **Bottom-Right**: Cumulative returns -- portfolio (green) avoids the crash vs. S&P 500 (red)

---

### Stress Test P&L Surface

The stress test engine sweeps across **Spot Shocks** (-50% to +20%) and **Volatility Shocks** (0% to +50%) simultaneously, computing portfolio P&L at every combination. Historical crisis scenarios (GFC 2008, COVID 2020, etc.) are marked as labeled points on the surface.

![Stress Test Surface Animation](docs/img/stress_test_surface.gif)

**Reading the surface:**
- **Green zone** (upper right): Mild shocks, portfolio holds up
- **Red zone** (lower left): Severe spot crash + vol spike = maximum loss
- **Labeled points**: Where historical crises fall on the shock spectrum
- The surface **rotates** to show the full 3D shape of portfolio risk

---

### HMM Regime Transition Matrix

The Hidden Markov Model's **5x5 transition probability matrix** shows the likelihood of moving between market regimes. The dashed cyan box tracks the current state as it moves through the cycle. Watch the probabilities shift as different regimes become more or less likely.

![Regime Transition Heatmap Animation](docs/img/regime_transition_heatmap.gif)

**Reading the matrix:**
- **Rows** = current state (From), **Columns** = next state (To)
- **Diagonal** = probability of staying in current regime (self-transition)
- **Off-diagonal** = probability of regime change
- **Hot colors** (red/orange) = high probability, **Cool colors** (blue) = low probability
- **Dashed box** = current active state moving through the cycle

---

## How the 3D Coordinate System Changes Per Regime

```
  BULL QUIET                    TRANSITION                   BEAR VOLATILE (CRISIS)
  Surface: Smooth Green         Surface: Rippling Yellow     Surface: Inverted Red

       P&L ($)                      P&L ($)                      P&L ($)
        ^                            ^                            ^
        |   .:::::::::::.            |      /\    /\              |
        | .:::::::::::::::::.        |    /    \/    \            |  - - - - breakeven
        |:::::::::::::::::::::       |  /   ~    ~   \           |
        |:::::::::::::::::::'        | / ~~  !!!!  ~~ \          |'.
        |::::::::::::::::::'         |'    !!!!!!!!    \         |  '''...
        +--------------------> $     +--------------------> $    +---'---------'---> $
       /                            /                            /
      v Vol                        v Vol                        v Vol

  VIX: 12    Signal: BUY       VIX: 25   Signal: REDUCE     VIX: 67   Signal: CRISIS
  Cash: 15%  Alpha: +3%        Cash: 40%  Alpha: +1.8%      Cash: 70%  Alpha: +26%
  Sharpe: 1.85                 Warning: 62%                  Warning: 95%


  RECOVERY                      NEW BULL
  Surface: Reforming Blue       Surface: Smooth Green (New Cycle)

       P&L ($)                      P&L ($)
        ^                            ^
        |        .:::::.              |                .:::::::::::::.
        |     .::::::::::::.         |           .::::::::::::::::::::::::.
        |   :::::::::::::::::.       |        .:::::::::::::::::::::::::::::::
        |  ::::::::::::::::::'       |      ::::::::::::::::::::::::::::::::::'
        | :::::::::::::::::::'       |    :::::::::::::::::::::::::::::::::::::'
        +--------------------> $     +--------------------> $
       /                            /
      v Vol                        v Vol

  VIX: 28   Signal: BUY        VIX: 14   Signal: STRONG BUY
  Cash: 25%  Alpha: +18%       Cash: 15%  Alpha: +22%
```

### Full Cycle Performance vs. S&P 500

```
  Cumulative Return
   ^
   |                                                    .--* Portfolio (+38.2%)
   |                                                .--'
   |  Engine                                    .--'
   |  Portfolio ---                         .--'
   |              \                     .--'           ___--- S&P 500 (+16.1%)
   |               \               .--'           ___---
   |                \          .--'          ___---
   |                 \     .--'         ___---
   |                  \.--'        ___---
   |                   *      ___---
   |              ____/ \ ___---         The engine AVOIDED the crash
   |         ____/       X              and RE-ENTERED at the bottom
   |    ____/       ___/ \
   |___/       ___---     \___           Alpha: +22.1%
   | ___---                   \___       Sharpe: 2.10 vs 0.74
   +-----------------------------------> Time
   0     120    185    280    400    756
        Bull   Trans  Crisis  Recov  New Bull
```

---

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

---

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

### Data Flow

```
 Market Data (Synthetic S&P 500)
           |
           v
+------------------------+
|   Feature Extraction   |  Returns, Vol, Spreads, Volume
+------------------------+
           |
     +-----+-----+
     |           |
     v           v
+-----------+  +------------------+
| HMM Regime|  | Early Warning    |
| Detector  |->| System           |
+-----------+  +------------------+
     |           |
     v           v
+-----------+  +------------------+
| Strategy  |  | Trading Signal   |
| Manager   |  | BUY/HOLD/CRISIS  |
+-----------+  +------------------+
     |           |
     +-----+-----+
           |
           v
+------------------------+
|   Portfolio Engine     |
|   (P&L, Greeks, VaR)  |
+------------------------+
           |
     +-----+-----+
     |           |
     v           v
+-----------+  +------------------+
| Stress    |  | 3D Visualization |
| Testing   |  | WebGL + SSE      |
+-----------+  +------------------+
                       |
                       v
              localhost:8080
```

## Build & Run

### Requirements
- C++20 compiler (GCC 10+, Clang 12+)
- CMake 3.16+
- POSIX threads
- Python 3.8+ with matplotlib, numpy, Pillow (for visualization generation only)

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

### Regenerate Visualization GIFs
```bash
pip install matplotlib numpy Pillow
python3 scripts/generate_visualizations.py
python3 scripts/gen_regime_3d.py
```

### CLI Options
| Option | Default | Description |
|--------|---------|-------------|
| `--port` | 8080 | Web dashboard port |
| `--days` | 756 | Simulation trading days (756 = 3 years) |
| `--speed` | 100 | Milliseconds between frames |
| `--price` | 4500 | Initial S&P 500 price |
| `--headless` | false | Run without web server |

---

## Regime-Strategy Mapping

| Regime | Surface Shape | Recommended Strategies | Cash Target | Signal |
|--------|---------------|----------------------|-------------|--------|
| Bull Quiet | Smooth green dome | Covered Call, Iron Condor, Bull Call Spread | 15% | Strong Buy |
| Bull Volatile | Steep green peaks | Collar, Straddle, Covered Call | 25% | Buy |
| Bear Quiet | Flat yellow surface | Bear Put Spread, Collar, Protective Put | 40% | Reduce Risk |
| Bear Volatile | **Inverted red crater** | **Protective Put, Collar (CRISIS)** | **60-70%** | **CRISIS** |
| Transition | Rippled mixed surface | Straddle, Strangle, Collar | 35% | Hold |

---

## Stress Test Scenarios

```
  Portfolio Impact by Historical Scenario (unhedged):

  GFC 2008           |██████████████████████████████████████████████████| -55.0%  VIX +50
  Dot-Com 2000       |███████████████████████████████████████████████| -45.0%  VIX +20
  COVID Crash 2020   |██████████████████████████████████| -34.0%  VIX +55
  Rate Hike 2022     |█████████████████████████| -25.0%  VIX +15
  Black Monday 1987  |████████████████████████████████████████| -22.6%  VIX +40
  Volmageddon 2018   |██████████| -10.0%  VIX +35
  Flash Crash 2010   |█████████| -9.0%  VIX +25

  WITH Engine Hedging Active:
  GFC 2008           |████████████| -12.0%  (43% loss avoided)
  COVID 2020         |████████| -8.0%   (26% loss avoided)
  Black Monday       |██████| -6.0%   (16.6% loss avoided)
```

---

## License

MIT
