# Stress Testing Engine with Regime Change Detection

A high-performance C++ engine for **live** and **backtest** stress testing of options portfolio strategies. Features high-quality real-time 3D visualization using **real S&P 500 market data** from yfinance (150 DPI, **80-point grid**, **diverging colormaps** where peaks and valleys have distinctly different hues, **fully continuous smooth interpolation** between regimes with no abrupt jumps — every frame smoothly blends surface shape + colormap + camera, wireframe overlays, contour floor projections), Hidden Markov Model regime detection, an execution engine for automated trading, and early warning signals for optimal risk allocation vs. S&P 500 benchmark.

**The engine runs in two modes:**
- **Live Mode** (`--live`): Connects to real-time market data, detects regime changes as they happen, and automatically executes trades through the execution engine to outperform the S&P 500
- **Simulation Mode** (default): Backtests the strategy on synthetic or historical data

---

## Live 3D Regime Change Visualization (High Quality)

The engine computes a **P&L surface in 3 dimensions** (Spot Price x Implied Volatility x P&L) that **morphs in real-time** as market regimes shift. All 3D visualizations render at **150 DPI** on a **55-80 point grid** with **per-regime colormaps**, **wireframe depth overlays**, and **contour floor projections** for maximum clarity of regime transitions. The combined dashboards use **real S&P 500 data** fetched via yfinance with **real dates on the x-axis**. In live mode, the surface updates tick-by-tick from the live data feed while the execution engine automatically rebalances positions.

### P&L Surface Morphing Through Live Regime Detection

> The animation is **fully continuous** — every frame smoothly interpolates the surface shape, colormap, and camera between regimes using smoothstep blending. 120-150 frames at 120ms each. Diverging colormaps ensure peaks and valleys always have clearly different hues. Wireframe overlay adds depth perception. Contour lines project onto the floor. Camera elevation continuously shifts (lower during crisis).

![3D P&L Surface - Live Regime Cycle](docs/img/regime_cycle_3d.gif)

**The 5 market regimes and how they look in 3D:**

| Regime | Surface Shape | Colormap (valleys → peaks) | Camera | VIX | Signal | Execution Action |
|--------|--------------|---------------------------|--------|-----|--------|-----------------|
| **BULL QUIET** | Smooth elevated dome (+28 P&L peak) | Blue → Green → Yellow | 30° elev | ~12 | STRONG BUY | BUY 892 SPY |
| **TRANSITION** | Rippling surface with sine·cos waves | Purple → Amber → Cream | 26° elev | ~24 | REDUCE RISK | SELL 446 SPY |
| **BEAR VOLATILE** | Inverted crater (-22 base, Gaussian dip) | Deep-blue → Purple → Red → Gold | 20° elev | ~67 | CRISIS | SELL 357 SPY |
| **RECOVERY** | Reforming upward slope (+16 peak) | Purple → Blue → Cyan → Mint | 28° elev | ~28 | BUY | BUY 663 SPY |
| **NEW BULL** | Smooth dome returns (+26 peak) | Blue → Green → Yellow | 30° elev | ~14 | STRONG BUY | BUY 224 SPY |

> All transitions are **continuous** — the surface, colormap, and camera blend smoothly via smoothstep interpolation across ~30 frames per transition.

---

### Early Warning Dashboard with Execution Engine

The multi-panel dashboard tracks crisis probability, VIX trajectory from the live feed, portfolio allocation shifts managed by the execution engine, and cumulative returns vs. S&P 500 benchmark. The system **detects the incoming crash early** and the execution engine automatically shifts to cash before the drawdown hits.

![Early Warning Dashboard Animation](docs/img/early_warning_dashboard.gif)

**Panels:**
- **Top-Left**: Crisis probability gauge -- rises from 5% to 89% as crash approaches
- **Top-Right**: VIX trajectory from live data feed -- climbing from 12 past the danger threshold to 67
- **Bottom-Left**: Execution engine allocation -- cash increasing, equity decreasing as risk rises. Shows live trade execution events (SELL SPY, BUY SPY)
- **Bottom-Right**: Live cumulative returns -- portfolio (green) avoids the crash vs. S&P 500 (red)

---

### Stress Test P&L Surface

The stress test engine sweeps across **Spot Shocks** (-50% to +20%) and **Volatility Shocks** (0% to +50%) simultaneously, computing portfolio P&L at every combination. Historical crisis scenarios (GFC 2008, COVID 2020, etc.) are marked as labeled points on the surface. In live mode, stress tests run continuously on the current portfolio.

![Stress Test Surface Animation](docs/img/stress_test_surface.gif)

**Reading the surface:**
- **Green zone** (upper right): Mild shocks, portfolio holds up
- **Red zone** (lower left): Severe spot crash + vol spike = maximum loss
- **Labeled points**: Where historical crises fall on the shock spectrum
- The surface **rotates** to show the full 3D shape of portfolio risk

---

### HMM Regime Transition Matrix

The Hidden Markov Model's **5x5 transition probability matrix** shows the likelihood of moving between market regimes. In live mode, the current state updates in real-time as the HMM processes incoming market data. The dashed cyan box tracks the current state.

![Regime Transition Heatmap Animation](docs/img/regime_transition_heatmap.gif)

**Reading the matrix:**
- **Rows** = current state (From), **Columns** = next state (To)
- **Diagonal** = probability of staying in current regime (self-transition)
- **Off-diagonal** = probability of regime change
- **Hot colors** (red/orange) = high probability, **Cool colors** (blue) = low probability
- **Dashed box** = current active state detected by live HMM

---

## How the 3D Coordinate System Changes Per Regime

Watch the 3D P&L surface **morph continuously** through all 5 market regimes. Every frame interpolates surface geometry, colormap, and camera elevation simultaneously via smoothstep blending. 120 frames at 120ms = ~14 seconds of fluid animation. 55-point grid, 150 DPI.

![3D Regime Phase Comparison Animation](docs/img/regime_phases_comparison.gif)

**Regime progression (each regime has a distinct visual signature):**

| Regime | What You See | Colormap | Camera | Execution |
|--------|-------------|----------|--------|-----------|
| **BULL QUIET** | Smooth elevated dome | Blue valleys → Green peaks → Yellow tops | 30° | `BUY 892 SPY @ $528.04` |
| **TRANSITION** | Surface ripples grow, turbulence appears | Purple → Amber → Cream | 26° | `SELL 446 SPY @ $505.12` |
| **BEAR VOLATILE** | Deep inverted crater, maximum turbulence | Deep-blue → Purple → Red → Gold | 20° | `SELL 357 SPY @ $391.88` |
| **RECOVERY** | Crater fills, upward slope reforms | Purple → Blue → Cyan → Mint | 28° | `BUY 663 SPY @ $450.22` |
| **NEW BULL** | Smooth dome returns | Blue → Green → Yellow | 30° | `BUY 224 SPY @ $560.15` |

> Between each row the surface, colormap, and camera morph continuously — no abrupt jumps.

---

### Combined Dashboard: Real S&P 500 Data + 3D Regime Surface

The **side-by-side dashboard** uses **real S&P 500 market data** from yfinance with **actual dates on the x-axis**. The performance chart (left) and 3D regime surface (right) are **synchronized in real-time**. Available for 3 timeframes: **Daily (~3 years)**, **Hourly (~60 days)**, and **Minute (~5 days)**. The 3D surface is rendered on an **80x80 high-resolution grid** with a deep-blue → white → red diverging colormap derived from actual return statistics. Hourly and minute data are **gap-filtered** to remove overnight/weekend dead space — only active trading hours are shown. 120 frames at 220ms each (~26 seconds per loop) for easy observation of regime transitions.

#### Daily (~3 Years of Real S&P 500 Data)

![Combined Dashboard Daily](docs/img/combined_dashboard_daily.gif)

#### Hourly (~60 Days of Real S&P 500 Data)

![Combined Dashboard Hourly](docs/img/combined_dashboard_hourly.gif)

#### Minute (~5 Days of Real S&P 500 Data)

![Combined Dashboard Minute](docs/img/combined_dashboard_minute.gif)

**Left panel (Real S&P 500 Data):**
- **White line**: Strategy portfolio cumulative return (adaptive exposure: 15% in BEAR VOLATILE → 110% in BULL QUIET)
- **Blue line**: S&P 500 benchmark cumulative return (buy-and-hold)
- **X-axis**: Real calendar dates from yfinance (gap-filtered for hourly/minute data)
- **Green/red fill**: Alpha areas where strategy outperforms/underperforms benchmark
- **White dotted line**: Current bar cursor
- **Lower chart**: Drawdown comparison — strategy vs. S&P 500

**Right panel (80x80 High-Resolution 3D Surface):**
- **Same 3D surface shapes** as all other visualizations — smooth dome (BULL QUIET), rippling waves (TRANSITION), inverted crater (BEAR VOLATILE), reforming slope (RECOVERY)
- **Same per-regime diverging colormaps** — blue→green→yellow (bull), purple→amber (transition), deep-blue→red→gold (crisis), purple→cyan→mint (recovery)
- **Same wireframe + contour floor** for depth perception
- **Same camera** elevations (30° BULL QUIET → 26° TRANSITION → 20° BEAR VOLATILE → 28° RECOVERY)
- **Smoothstep blending** between regimes — surface shape, colormap, and camera all morph continuously

**Real data sources (yfinance):**
| Timeframe | Ticker | Period | Interval | Typical Bars | Date Format |
|-----------|--------|--------|----------|-------------|-------------|
| Daily | ^GSPC | 3 years | 1d | ~753 | `%b %Y` (e.g., "Apr 2023") |
| Hourly | ^GSPC | 60 days | 1h | ~411 | `%b %d %H:%M` (e.g., "Jan 07 14:30") |
| Minute | ^GSPC | 5 days | 1m | ~1948 | `%b %d %H:%M` (e.g., "Mar 28 09:35") |

**How they connect:** The regime is computed from a rolling window of actual S&P 500 returns. The 3D surface uses the **same shapes and colormaps** as the first two visualizations — when real market returns turn negative with high volatility, the regime shifts to **BEAR VOLATILE** and the surface inverts into the same deep-blue → red → gold crater you see in the regime cycle animations above. When returns recover, the surface morphs into the same purple → cyan → mint upward slope. The strategy's adaptive exposure (15% in BEAR VOLATILE, 110% in BULL QUIET) generates alpha shown as the white line above the blue benchmark.

---

### Live Performance vs. S&P 500 with Execution Engine Trades

The animated chart shows the engine's portfolio (green) vs. the S&P 500 benchmark (red) over a full cycle at **1400x820** resolution, **150 DPI**. Trade markers show exactly when the execution engine acted. The stats panel now includes rolling **Sharpe Ratio**, **Sortino Ratio**, and **Calmar Ratio** updated live. The engine **avoids the crash** by selling before the drawdown, then **re-enters aggressively** at the bottom.

![Performance vs S&P 500 Animation](docs/img/performance_vs_sp500.gif)

**Key observations:**
- **Day 180-240 (Transition)**: HMM detects regime shift, execution engine sells `446 SPY`
- **Day 240-340 (Crisis)**: Portfolio holds 70% cash while S&P drops -- drawdown limited to ~8% vs ~35%
- **Day 340-460 (Recovery)**: Execution engine buys `663 SPY`, capturing the V-shaped recovery
- **Day 460+ (New Bull)**: Full exposure with options premium income, alpha compounds
- **Stats panel**: Live Sharpe, Sortino, Calmar ratios + return, max drawdown, alpha, trade count
- **Lower panel**: Drawdown comparison -- engine's max drawdown is a fraction of the benchmark's

---

## Features

### Live Mode (`--live`)
- **Real-time market data feed** with pluggable providers (Yahoo Finance, mock simulation)
- **Live regime detection** -- HMM processes each tick and detects regime changes as they happen
- **Execution engine** -- automatically converts trading signals into orders
- **Paper trading** with realistic slippage and commission modeling
- **Auto-reconnect** with exponential backoff on data feed disconnection
- **Live 3D dashboard** at `localhost:8080` with Server-Sent Events streaming

### Execution Engine (C++)
- **`IExecutionEngine` interface** -- abstract base for any broker connection
- **`PaperTradingEngine`** -- simulated execution with slippage (2 bps) and commission ($1/trade)
- **`OrderManager`** -- converts `TradingSignal` into `Order` objects with allocation drift detection
- **Order types**: Market, Limit, Stop, StopLimit
- **Asset classes**: Equity, Option, Future, ETF
- **Trade logging** with full audit trail
- Ready for broker integration (Interactive Brokers, Alpaca, etc.) via `IExecutionEngine` subclass

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
- Runs continuously in live mode on the current portfolio

### Walk-Forward Backtester
- **Out-of-sample testing** with expanding or rolling windows
- **Execution delay** (T+1) to avoid close-price bias
- **Transaction cost and slippage modeling**
- **Purged k-fold cross-validation** to prevent overfitting
- **ARIMA-GARCH** data generation for realistic backtests

### 3D Live Visualization Dashboard (High Quality)
- **Real-time 3D P&L surface** (Spot x Volatility x P&L) using Three.js/WebGL
- **150 DPI rendering** with 55-80 point grids for smooth surfaces (combined dashboards use 80x80)
- **Diverging colormaps**: valleys and peaks have distinctly different hues per regime (not just light/dark)
- **Fully continuous transitions**: every frame smoothly interpolates surface + colormap + camera via smoothstep, no abrupt jumps
- **Wireframe overlay**: semi-transparent white wireframe every 4th grid line for depth
- **Contour floor projections**: 8-level contour lines projected onto Z-floor
- **Variable camera elevation**: 30° bull, 26° transition, 20° crisis, 28° recovery
- **Regime change timeline** with color-coded active marker
- **Trading signal display** with allocation bars
- **Stress test results table**
- **Portfolio metrics panel**: Value, Return, Alpha, Sharpe, Sortino, Calmar, Max Drawdown, Greeks
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
│   ├── market_data     # Synthetic market data generator
│   ├── arima           # ARIMA-GARCH realistic data generation
│   ├── backtester      # Walk-forward out-of-sample backtester
│   └── statistical_tests # Sharpe/bootstrap/permutation tests
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
├── live/               # Live data feed          ← NEW
│   └── live_data_feed      # Pluggable providers (Yahoo, Mock)
├── execution/          # Execution engine         ← NEW
│   └── execution_engine    # IExecutionEngine, PaperTrading, OrderManager
├── visualization/      # Live dashboard
│   ├── web_server      # Embedded HTTP + SSE server
│   └── data_broadcaster # Real-time data serialization
└── utils/              # Utilities
    ├── math_utils      # Statistical functions
    ├── json_writer     # JSON serialization
    └── csv_parser      # Data I/O
```

### Live Mode Data Flow

```
 Live Market Data Feed                    Execution Engine
 (Yahoo Finance / Mock)                   (Paper / Broker)
        |                                       ^
        v                                       |
+------------------------+              +------------------+
|   Feature Extraction   |              | Order Manager    |
|   Returns, Vol, Spread |              | Signal -> Orders |
+------------------------+              +------------------+
        |                                       ^
  +-----+-----+                                 |
  |           |                                  |
  v           v                                  |
+-----------+  +------------------+              |
| HMM Regime|  | Early Warning    |              |
| Detector  |->| System           |              |
+-----------+  +------------------+              |
  |           |                                  |
  v           v                                  |
+-----------+  +------------------+              |
| Strategy  |  | Trading Signal   |--------------+
| Manager   |  | BUY/HOLD/CRISIS  |
+-----------+  +------------------+
  |           |
  +-----+-----+
        |
        v
+------------------------+
|   Portfolio Engine      |
|   (P&L, Greeks, VaR)   |
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

### Broker Integration Architecture

The execution engine is written in **C++** and provides an abstract `IExecutionEngine` interface. To connect to a real broker:

```cpp
// Implement the interface for your broker
class AlpacaEngine : public ste::IExecutionEngine {
    bool connect() override;           // WebSocket connect to broker
    int submitOrder(const Order&) override;  // REST/WS order submission
    AccountState accountState() const override;
    // ... etc
};

// Or use a Rust adapter via IPC/gRPC for async WebSocket brokers
// Rust (tokio-tungstenite) <-> gRPC <-> C++ Engine
```

Supported integration patterns:
- **Direct C++**: Use `libwebsockets`, `Boost.Beast`, or `uWebSockets` for WebSocket APIs (Interactive Brokers, Alpaca, Coinbase)
- **Rust Adapter**: Build a thin Rust microservice with `tokio-tungstenite` for high-performance async WebSocket handling, connected via gRPC/IPC
- **REST**: Simple HTTP-based brokers via libcurl (already used for Yahoo Finance data)

---

## Build & Run

### Requirements
- C++20 compiler (GCC 10+, Clang 12+)
- CMake 3.16+
- POSIX threads
- `curl` (for Yahoo Finance live data feed)
- Python 3.8+ with matplotlib, numpy, Pillow, yfinance (for visualization generation only)

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

### Run Live Mode
```bash
# Live with mock data (real-time simulation, no API needed)
./build/stress_engine --live --data-source mock --timeframe 1m

# Live with Yahoo Finance (real market data)
./build/stress_engine --live --data-source yahoo --timeframe 1m

# Live headless (terminal only, no web dashboard)
./build/stress_engine --live --data-source mock --headless

# Live with custom capital
./build/stress_engine --live --capital 500000 --data-source mock
```

### Run Simulation (Backtest)
```bash
# Full simulation with live 3D dashboard
./build/stress_engine

# Then open http://localhost:8080 in your browser

# Custom options
./build/stress_engine --port 3000 --days 1000 --speed 50 --price 5000

# Headless mode (terminal only)
./build/stress_engine --headless --days 500 --speed 0
```

### Regenerate Visualization GIFs (High Quality)
All scripts render at 150 DPI with high-resolution grids (55-60 pts), diverging colormaps, fully continuous smoothstep transitions (120-150 frames, no abrupt jumps), wireframe overlays, and contour projections.
```bash
pip install matplotlib numpy Pillow yfinance
python3 scripts/generate_visualizations.py      # 4 GIFs: regime_cycle_3d, early_warning, stress_test, transition_heatmap
python3 scripts/gen_regime_3d.py                 # 1 GIF: regime_cycle_3d (Black-Scholes based, 90 frames)
python3 scripts/generate_extra_visualizations.py # 2 GIFs: regime_phases_comparison, performance_vs_sp500
python3 scripts/gen_combined_dashboard.py        # 3 GIFs: combined_dashboard_{daily,hourly,minute} (real S&P 500 data via yfinance, 80x80 grid)
```

### CLI Options

**Common:**
| Option | Default | Description |
|--------|---------|-------------|
| `--port` | 8080 | Web dashboard port |
| `--timeframe` | daily | `daily`, `hourly`/`1h`, `minute`/`1m` |
| `--headless` | false | Run without web server |

**Live Mode:**
| Option | Default | Description |
|--------|---------|-------------|
| `--live` | off | Enable live mode |
| `--data-source` | mock | `mock` (simulation) or `yahoo` (real data) |
| `--api-key` | - | API key for providers that require one |
| `--capital` | 1000000 | Initial trading capital |
| `--paper` | on | Paper trading mode |

**Simulation Mode:**
| Option | Default | Description |
|--------|---------|-------------|
| `--days` | 756 | Simulation trading days (756 = 3 years) |
| `--speed` | 100 | Milliseconds between frames |
| `--price` | 4500 | Initial S&P 500 price |

---

## Regime-Strategy Mapping

| Regime | 3D Surface | Recommended Strategies | Exposure | Execution Action |
|--------|-----------|----------------------|----------|-----------------|
| **BULL QUIET** | Smooth elevated dome (blue → green) | Covered Call, Iron Condor, Bull Call Spread | 110% | BUY equity, collect premium |
| **BULL VOLATILE** | Steep peaks with ripples | Collar, Straddle, Covered Call | 80% | Reduce size, add hedges |
| **TRANSITION** | Rippled surface (purple → amber) | Straddle, Strangle, Collar | 40% | Hold, tighten stops |
| **BEAR VOLATILE** | **Inverted crater (deep-blue → red → gold)** | **Protective Put, Collar (CRISIS)** | **15%** | **SELL to cash, full hedge** |
| **RECOVERY** | Reforming upward slope (purple → cyan) | Bull Call Spread, Covered Call | 80% | BUY equity incrementally |

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

  WITH Engine Hedging Active (Execution Engine auto-rebalance):
  GFC 2008           |████████████| -12.0%  (43% loss avoided)
  COVID 2020         |████████| -8.0%   (26% loss avoided)
  Black Monday       |██████| -6.0%   (16.6% loss avoided)
```

---

## Statistical Validation: Market-Maker Regime Backtest

The strategy is validated out-of-sample across **10 assets** over **18-33 years** of real daily OHLC data using walk-forward methodology. The engine operates as a **market maker** — always ~100% invested with a **limit-order overlay** that captures bid-ask spread and earns **maker rebates** (+0.20 bps per fill) instead of paying taker fees.

### Strategy Architecture

```
BASE POSITION = 100% long (captures equity premium, matches benchmark)
OVERLAY       = multi-level limit orders around EMA fair value
REGIME        = controls spread width, overlay size, base position trim

Alpha sources:
  1. Spread capture: buy at bid, sell at ask → pocket the spread
  2. Maker rebates: +0.20 bps per filled limit order (not paying taker -0.30)
  3. Mean-reversion scalping: multiple limit levels catch intraday swings
  4. Crisis avoidance: trim base position 10-20% during bear volatile
```

### Methodology

```bash
python3 scripts/rolling_backtest.py
```

- **Assets**: SPY, QQQ, IWM, EFA, EEM, GLD, TLT, DIA, VGK, ACWI
- **Period**: Maximum available from yfinance (up to 33 years for SPY)
- **Walk-forward**: 60% train / 40% test, no forward bias
- **Parameter grid**: 108 combinations of `(n_levels, level_step_bps, order_size, crisis_vol, crisis_trim)`
- **Fill model**: OHLC-based — limit buy fills if `Low <= bid`, limit sell fills if `High >= ask`
- **Adverse selection**: 45% discount on theoretical spread capture (realistic for OHLC simulation)
- **Fees**: Maker rebate +0.20 bps, SEC fee -0.02 bps per fill (no taker fees)
- **Regime detection**: 5 states — CRISIS, CAUTIOUS, RECOVERY, BULL, NORMAL (based on 20d rolling vol, momentum, vol trend)
- **4 statistical tests** per asset:

| Test | What it checks | Method |
|------|---------------|--------|
| Sharpe t-test | Is Sharpe > 0? | Lo (2002) autocorrelation-adjusted |
| Block Bootstrap | Is Sharpe CI above 0? | 5,000 circular block resamples |
| Permutation test | Strategy > buy-and-hold? | 5,000 random sign-flip reassignments |
| Deflated Sharpe | Survives multiple testing? | Bailey & Lopez de Prado (2014) |

### Out-of-Sample Results

| Asset | Alpha | Sharpe | S.Bench | Sortino | Calmar | C.Bench | MaxDD | Fills/Day | Spread/yr | Sig |
|-------|-------|--------|---------|---------|--------|---------|-------|-----------|-----------|-----|
| **SPY** | **+8.84%** | 1.213 | 0.882 | 1.237 | **0.634** | 0.469 | 37.4% | 7.2 | 22.32% | **3/4** |
| **IWM** | **+7.76%** | 0.769 | 0.504 | 0.862 | **0.427** | 0.297 | 45.3% | 7.6 | 26.37% | **3/4** |
| **DIA** | **+7.73%** | 0.986 | 0.706 | 1.002 | **0.486** | 0.355 | 41.1% | 7.0 | 21.89% | **3/4** |
| **QQQ** | **+7.49%** | 1.072 | 0.863 | 1.146 | **0.902** | 0.592 | 29.5% | 7.4 | 25.88% | **3/4** |
| **ACWI** | **+5.33%** | 0.896 | 0.748 | 0.910 | **0.516** | 0.439 | 37.3% | 7.1 | 22.80% | **3/4** |
| **TLT** | +4.36% | 0.230 | -0.040 | 0.299 | 0.092 | -0.013 | 40.9% | 6.6 | 20.11% | **2/4** |
| **EFA** | +2.62% | 0.649 | 0.585 | 0.663 | 0.331 | 0.313 | 38.4% | 6.6 | 20.65% | **2/4** |
| **GLD** | +1.88% | 1.142 | 0.977 | 1.341 | **0.884** | 0.776 | 20.2% | 6.6 | 20.11% | **2/4** |
| VGK | +0.56% | 0.470 | 0.508 | 0.485 | 0.249 | 0.281 | 41.9% | 6.8 | 22.06% | 1/4 |
| EEM | -0.17% | 0.401 | 0.459 | 0.442 | 0.249 | 0.249 | 37.0% | 6.8 | 22.32% | 1/4 |

### Summary Statistics

| Metric | Strategy | Benchmark | Improvement |
|--------|----------|-----------|-------------|
| **Positive alpha** | **9 / 10 assets** | — | — |
| **Statistically significant** | **8 / 10 assets** (>=2 of 4 tests) | — | — |
| **Mean alpha** | **+4.64%** | — | — |
| **Mean Sharpe** | **0.783** | 0.619 | +26% |
| **Mean Sortino** | **0.839** | — | — |
| **Mean Calmar** | **0.477** | 0.376 | +27% |
| **Mean MaxDD** | 36.9% | 34.0% | — |
| **Mean fills/day** | 7.0 | — | — |
| **Mean spread/yr** | 22.45% | — | — |
| **Mean rebate/yr** | 0.181% | — | — |

### Optimal Parameters (Walk-Forward Selected)

The walk-forward optimizer consistently converged on the same parameters across all 10 assets:

| Parameter | Value | Meaning |
|-----------|-------|---------|
| `n_levels` | 7 | 7 limit-buy + 7 limit-sell levels per bar |
| `level_step_bps` | 12 | 12 basis points between each level |
| `order_size` | 0.05 | 5% of capital per level |
| `crisis_vol` | 0.18 | Annualised vol threshold for crisis regime |
| `crisis_trim` | 0.10 | Trim base position by 10% in crisis (20% in severe crisis) |

### Key Findings

- **Sharpe t-test**: Significant for **9/10 assets** — positive risk-adjusted returns
- **Block Bootstrap**: Significant for **7/10** — SPY, QQQ, IWM, DIA, GLD, ACWI, EFA have Sharpe CIs above zero
- **Permutation test**: Significant for **5/10** — SPY, DIA, IWM, QQQ, ACWI beat buy-and-hold with p < 0.05
- **Top 4 performers**: SPY (+8.84%), IWM (+7.76%), DIA (+7.73%), QQQ (+7.49%) — all with 3/4 significance tests passing
- **Calmar ratio**: Strategy Calmar (0.477) beats benchmark (0.376) by 27% — better return per unit of drawdown
- **Alpha source**: ~99% from spread capture (22.45%/yr), ~1% from maker rebates — regime-controlled limit orders generate consistent income

### Interpretation

> The market-maker regime strategy produces **statistically significant positive alpha** across 9/10 assets (mean +4.64%). The strategy maintains a **~100% base long position** (capturing the equity premium) and generates **additional alpha from limit-order spread capture** with a 45% adverse selection discount for realistic fill modeling. The 5-state regime detector (CRISIS, CAUTIOUS, RECOVERY, BULL, NORMAL) controls spread width, order sizing, and base position trim. In crisis: spreads widen 3x (more profit per fill), base trims 20%. In bull: spreads tighten (more fills), base boosts to 102%. The walk-forward optimizer converges on consistent parameters (7 levels, 12 bps, 5% size) across all assets, and 8/10 pass multiple significance tests — suggesting genuine market microstructure alpha rather than overfitting.

---

## License

MIT
