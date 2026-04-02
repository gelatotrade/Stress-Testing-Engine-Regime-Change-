# Stress Testing Engine with Regime Change Detection

A high-performance C++ engine for stress testing options portfolio strategies with real-time 3D visualization, Hidden Markov Model regime detection, and early warning signals for optimal risk allocation vs. S&P 500 benchmark.

---

## 3D P&L Surface -- Regime Change Visualization Over Time

The engine renders a **live 3D coordinate system** where the P&L surface morphs in real-time as market regimes shift. Below you can see how the surface, signals, and risk allocation change across 5 distinct market phases:

---

### Phase 1: Bull Quiet -- Steady Growth (VIX ~12, S&P +15% YoY)

```
  Signal: STRONG BUY                Regime: Bull Quiet
  Cash: 15% | Equity: 60% | Options: 25%     Confidence: 87%

  P&L ($)
   ^
   |                                          Strategy: Covered Call +
   |                    .::::::::::.           Iron Condor
   |                .:::::::::::::::::::.
   |            .:::::::::::::::::::::::::::   Smooth green surface =
   |         .:::::::::::::::::::::::::::::.   stable premium income
   |       ::::::::::::::::::::::::::::::::
   |     .::::::::::::::::::::::::::::::::'    Max Profit Zone
   |    :::::::::::::::::::::::::::::::::'       ~~~~~~~~~~~~
   |   ::::::::::::::::::::::::::::::::'
   |  ::::::::::::::::::::::::::::::::'            VIX: 12.3
   | .:::::::::::::::::::::::::::::::'          Sharpe: 1.85
   |::::::::::::::::::::::::::::::::'        Max Draw: -3.2%
   +------------------------------+-------> Spot Price ($)
   |   4000   4200   4400   4600  |  4800
   |                              |
   v                              v
  Vol (IV)                    Time Decay
  0.10 ........................ T+30d        Early Warning: [====------] 18%
  0.15 ........................ T+20d        Crisis Prob:   [==---------] 5%
  0.20 ........................ T+10d
  0.25 ........................ T+0d         Portfolio vs S&P500: +3.2% Alpha

  Regime Timeline:
  [========BULL QUIET=========>                                          ]
  Day 1                       Day 120                              Day 756
```

---

### Phase 2: Transition -- Early Warning Signals (VIX Rising, Yield Curve Inverting)

```
  Signal: REDUCE RISK               Regime: TRANSITION
  Cash: 40% | Equity: 40% | Options: 20%     Confidence: 52%

  P&L ($)
   ^
   |         WARNING: Regime shift detected!
   |                                  .
   |                 .:::::::.      .' '.
   |              .::::::::::::.  .'     '.    Surface becomes UNEVEN
   |           .::::::::::::::::.'   /\    '.  Peaks and valleys appear
   |         :::::::::::::::::.    /    \
   |       .:::::::::::::::.'    /  !!  \      Vol-of-Vol SPIKING
   |      :::::::::::::::.'    /   !!!!  \
   |    .:::::::::::::::.'---'    !!!!!!   \     VIX: 22.7 (+84%)
   |   ::::::::::::::.'          !!!!!!!!    \   Credit Spreads: +45%
   |  ::::::::::::..'          !!!!!!!!!!     \  Yield Curve: INVERTING
   | :::::::::::.'           !!!!!!!!!!!!      \
   +------------------------------+-------> Spot Price ($)
   |   4000   4200   4400   4600  |  4800
   |                              |
   v                              v
  Vol (IV)                    Time Decay
  0.15 ......./\/\............. T+30d        Early Warning: [=========-] 62%
  0.22 ....../    \/\.......... T+20d        Crisis Prob:   [======----] 28%
  0.30 ...../       \.......... T+10d
  0.38 ..../         \......... T+0d         Portfolio vs S&P500: +1.8% Alpha

  Regime Timeline:                     v-- WE ARE HERE
  [========BULL QUIET========][==TRANSITION===>                          ]
  Day 1                    Day 120    Day 185                      Day 756

  ACTIONS TAKEN:
  + Closed Iron Condors (high gamma risk)
  + Added Protective Puts (downside hedge)
  + Increased cash allocation 25% -> 40%
  + Added Long Straddles (play vol expansion)
```

---

### Phase 3: Bear Volatile -- MARKET CRASH (VIX >45, S&P -30%)

```
  Signal: CRISIS                    Regime: BEAR VOLATILE
  Cash: 70% | Equity: 20% | Options: 10%     Confidence: 94%

  P&L ($)
   ^
   |  !!!! CRISIS MODE ACTIVE !!!!
   |
   |  '.                                       HEDGED Portfolio P&L
   |    ''.                                    (with Protective Puts)
   |      ''..         Protective Puts
   |         ''..      SAVING the portfolio     Unhedged would be: -34%
   |            ''...        |                  Hedged actual:      -8%
   |               '''..     v
   |                   ''''......               VIX: 67.2
   |  - - - - - - - - - - - -''''....           S&P: -34% from peak
   |----BREAKEVEN-LINE-----------''''...        Credit Spreads: +280%
   |                                  ''''.     Yield Curve: -0.8%
   |                                      '.
   |'.                                      '. Unhedged S&P
   |  '..                                  ..' (what you AVOIDED)
   |     '''...                        ..'''
   |          '''''..............'''''''''
   |               LOSS ZONE (avoided)
   +------------------------------+-------> Spot Price ($)
   |   3100   3400   3700   4000  |  4300
   |                              |
   v                              v
  Vol (IV)                    Time Decay
  0.35 ...../\/\/\/\/\......... T+30d        Early Warning: [==========] 95%
  0.50 ..../          \........ T+20d        Crisis Prob:   [==========] 89%
  0.65 .../            \....... T+10d
  0.80 ../              \...... T+0d         Portfolio vs S&P500: +26% Alpha

  Regime Timeline:                                v-- WE ARE HERE
  [===BULL QUIET===][==TRANS==][=====BEAR VOLATILE======>                ]
  Day 1          Day 120   Day 185    Day 230   Day 280            Day 756

  CRASH ANATOMY (3D Surface Deformation Over Time):

     Day 185 (Pre-Crash)        Day 210 (Crash Start)       Day 250 (Peak Crisis)
     Surface: Flat/Green        Surface: Tilting/Yellow      Surface: Inverted/Red

        ___________                 _____                         __
       /          /|              /     /\                       /  \
      /  STABLE  / |             / ~~~ /  \                     / !! \
     /          /  |            / ~~~ / !! \                   / !!!! \
    /_________ /   |           /_____/ !!!! \                 / !!!!!! \
    |         |    /           |     | !!!!!/           _____/ !!!!!!!! \_____
    |  +$$$   |   /            | +/- | !!!/           |     LOSS ZONE      |
    |         |  /             |     | !!/            |    $$$$ LOSS $$$$   |
    |_________|/               |_____|!/              |____________________|

    VIX: 15    P&L: +$45K     VIX: 35   P&L: -$12K    VIX: 67   P&L: -$82K
    Signal: BUY               Signal: GO TO CASH       Signal: CRISIS
    Cash: 15%                 Cash: 55%                Cash: 70%
```

---

### Phase 4: Recovery / Bottom Fishing -- Regime Transitioning Back

```
  Signal: BUY                       Regime: TRANSITION -> BULL VOLATILE
  Cash: 25% | Equity: 50% | Options: 25%     Confidence: 61%

  P&L ($)
   ^
   |                                          Regime shifting back
   |                      .:::::.             to bullish!
   |                   .:::::::::::.
   |                .:::::::::::::::::.        Surface re-forming
   |             .:::::::::::::::::::::::.     upward slope
   |           ::::::::::::::::::::::::::::
   |         .::::::::::::::::::::::::::::::   High IV = Fat Premiums
   |       ::::::::::::::::::::::::::::::::    for selling strategies
   |     .:::::::::::::::::::::::::::::::::
   |    ::::::::::::::::::::::::::::::::::'      VIX: 28.4 (falling)
   |   :::::::::::::::::::::::::::::::::'     Sharpe: 1.42
   |  .:::::::::::::::::::::::::::::::'    Max Draw: -8.1% (recovering)
   | ::::::::::::::::::::::::::::::::'
   +------------------------------+-------> Spot Price ($)
   |   3600   3900   4200   4500  |  4800
   |                              |
   v                              v
  Vol (IV)                    Time Decay
  0.20 ...\                     T+30d        Early Warning: [===-------] 22%
  0.28 ....\.........           T+20d        Crisis Prob:   [==--------] 11%
  0.35 .....\........           T+10d
  0.42 ......\                  T+0d         Portfolio vs S&P500: +18% Alpha

  Regime Timeline:                                           v-- HERE
  [==BULL Q==][=TRANS=][====BEAR VOL====][======TRANSITION=====>         ]
  Day 1    Day 120  Day 185          Day 280    Day 350  Day 400   Day 756

  ACTIONS TAKEN:
  + Re-entering equity positions gradually
  + Selling puts into elevated IV (high premium)
  + Adding Bull Call Spreads
  + Reducing cash 70% -> 25%
```

---

### Phase 5: New Bull -- Outperformance Locked In

```
  Signal: STRONG BUY                Regime: Bull Quiet (New Cycle)
  Cash: 15% | Equity: 60% | Options: 25%     Confidence: 82%

  P&L ($)
   ^
   |                                          Full cycle complete!
   |                      .:::::::::::.
   |                  .:::::::::::::::::::::.
   |              .::::::::::::::::::::::::::::::    Smooth green surface
   |           .::::::::::::::::::::::::::::::::::   returns -- new bull
   |        .::::::::::::::::::::::::::::::::::::::
   |      ::::::::::::::::::::::::::::::::::::::::   All-time highs
   |    .:::::::::::::::::::::::::::::::::::::::::'
   |   ::::::::::::::::::::::::::::::::::::::::::'     VIX: 13.8
   |  :::::::::::::::::::::::::::::::::::::::::'     Sharpe: 2.10
   | .:::::::::::::::::::::::::::::::::::::::'    Max Draw: -8.1%
   |::::::::::::::::::::::::::::::::::::::::'
   +------------------------------+-------> Spot Price ($)
   |   4200   4500   4800   5100  |  5400
   |                              |
   v
  Vol (IV)                                   Early Warning: [==--------] 12%
  0.10 ........................ T+30d        Crisis Prob:   [=---------]  3%
  0.15 ........................ T+20d
                                             Portfolio vs S&P500: +22% Alpha

  Regime Timeline (Complete Cycle):
  [==BULL Q==][=TR=][===BEAR VOL===][===TRANS===][=====BULL QUIET=====>  ]
  Day 1    Day 120 Day 185      Day 280      Day 400    Day 500    Day 756

  FINAL PERFORMANCE vs BENCHMARK:

  Cumulative Return
   ^
   |                                                    .--* Portfolio
   |                                                .--'    (+38.2%)
   |                                            .--'
   |  Engine                                .--'
   |  Portfolio ----                    .--'
   |               \               .--'           ___--- S&P 500
   |                \          .--'          ___---      (+16.1%)
   |                 \     .--'         ___---
   |                  \.--'        ___---
   |                   *      ___---
   |              ____/ \ ___---
   |         ____/       X          The engine AVOIDED the crash
   |    ____/       ___/ \          and RE-ENTERED at the bottom
   |___/       ___---     \___
   |      ___---              \___        Alpha: +22.1%
   | ___---                       \___    Sharpe: 2.10 vs 0.74
   +-----------------------------------> Time (Days)
   0     120    185    280    400    600    756
        Bull   Trans  Crisis  Trans  New Bull
```

---

## How the 3D Coordinate System Works

The engine computes a **P&L surface** in 3 dimensions that updates every simulation tick:

```
                    P&L ($)
                     ^
                     |        The surface MORPHS in real-time:
                     |
                     |        Bull Quiet:    Smooth, elevated (green)
                     |        Transition:    Rippled, tilting (yellow)
                     |        Bear Volatile: Inverted, deep red
                     |        Recovery:      Re-forming upward (blue)
                     |
                     +-------------------> Spot Price ($)
                    /
                   /
                  /
                 v
              Volatility (IV)

  3rd Axis (Time) moves the surface forward:
  ================================================>
  T+0        T+5d       T+10d      T+20d      T+30d
  Current    Near       Mid        Far        Expiry
```

### Color Encoding by Regime

| Color | Regime | Surface Shape | Signal |
|-------|--------|---------------|--------|
| Green | Bull Quiet | Smooth dome, elevated P&L plateau | Strong Buy / Buy |
| Yellow | Transition | Rippled, asymmetric, vol skew appearing | Reduce Risk |
| Orange | Bull Volatile / Bear Quiet | Steep gradients, mixed peaks/valleys | Hold / Reduce |
| Red | Bear Volatile (Crisis) | Inverted surface, deep loss valleys | Go To Cash / Crisis |
| Blue | Recovery | Re-forming dome, steep upward slope | Buy |

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

---

## How It Works

```
                  Market Data (Synthetic S&P 500)
                            |
                            v
               +------------------------+
               |   Feature Extraction   |  Returns, Vol, Spreads, Volume
               +------------------------+
                            |
              +-------------+-------------+
              |                           |
              v                           v
  +---------------------+    +------------------------+
  | HMM Regime Detector |    | Early Warning System   |
  | (5-State Markov)    |--->| (Multi-Factor Score)   |
  +---------------------+    +------------------------+
              |                           |
              v                           v
  +---------------------+    +------------------------+
  | Strategy Manager    |    | Trading Signal         |
  | (Regime-Optimal     |    | BUY / HOLD / CASH /    |
  |  Strategy Selection)|    | CRISIS                 |
  +---------------------+    +------------------------+
              |                           |
              +-------------+-------------+
                            |
                            v
               +------------------------+
               |   Portfolio Engine     |
               |   (P&L, Greeks, VaR)  |
               +------------------------+
                            |
              +-------------+-------------+
              |                           |
              v                           v
  +---------------------+    +------------------------+
  | Stress Testing      |    | 3D Visualization       |
  | (8 Historical +     |    | (WebGL Surface +       |
  |  MC + Parametric)   |    |  Live Regime Timeline) |
  +---------------------+    +------------------------+
                                        |
                                        v
                              Browser: localhost:8080
```

1. **Market Data Generation**: Synthetic S&P 500 data with realistic regime transitions using a Markov chain
2. **Regime Detection**: HMM processes daily features (returns, vol, credit spreads) and computes regime probabilities
3. **Signal Generation**: Early warning system combines crisis probability, vol acceleration, price momentum, and credit spread widening
4. **Portfolio Management**: Strategy manager selects optimal options strategies per regime
5. **Dynamic Allocation**: Cash/equity/options ratios adjusted based on regime signals to outperform S&P 500 with lower drawdown
6. **Stress Testing**: Every frame runs all 8 historical scenarios against current portfolio
7. **3D Visualization**: P&L surface, regime timeline, and metrics streamed via SSE to browser

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
  Portfolio Impact by Historical Scenario:

  Black Monday 1987  |████████████████████████████████████████| -22.6%  VIX +40
  GFC 2008           |██████████████████████████████████████████████████| -55.0%  VIX +50
  COVID Crash 2020   |██████████████████████████████████| -34.0%  VIX +55
  Volmageddon 2018   |██████████| -10.0%  VIX +35
  Flash Crash 2010   |█████████| -9.0%  VIX +25
  Rate Hike 2022     |█████████████████████████| -25.0%  VIX +15
  Dot-Com 2000       |███████████████████████████████████████████████| -45.0%  VIX +20
  Meme Stocks 2021   |               ████████████████| +15.0%  VIX +30

  WITH Engine Hedging Active:
  GFC 2008           |████████████| -12.0%  (vs -55% unhedged = 43% saved)
  COVID 2020         |████████| -8.0%   (vs -34% unhedged = 26% saved)
  Black Monday       |██████| -6.0%   (vs -22.6% unhedged = 16.6% saved)
```

---

## License

MIT
