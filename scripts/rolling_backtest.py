#!/usr/bin/env python3
"""
Market-Maker Regime-Detection Backtest — IBKR API Optimized.

Architecture:
  BASE POSITION = 100% long (captures equity premium, matches benchmark).
  OVERLAY = market-making via limit orders around fair value.
  REGIME  = controls spread width, overlay size, and base position trim.

Alpha sources:
  1. Spread capture: buy at bid, sell at ask → pocket the spread
  2. Maker rebates: IBKR Tiered high-volume → net rebate per fill
  3. Mean-reversion scalping: intraday oscillations fill levels repeatedly
  4. Crisis avoidance: trim base position 10-30% during bear volatile

Key: the strategy is ALWAYS ~90-100% invested. Alpha comes from the
market-making overlay ON TOP of the base position, not from timing.

OHLC fill model: intraday oscillation model. For each bar, estimate
how many times price oscillates through each limit level based on
(High-Low)/step_distance. This gives realistic fill counts (30-80/day)
matching IBKR market-maker activity.

Walk-forward: params trained on first 60%, tested on last 40%.
Fee structure: IBKR Tiered (high-volume maker).

Usage:  python3 scripts/rolling_backtest.py
"""
import warnings
warnings.filterwarnings('ignore')

import numpy as np
import pandas as pd
from scipy import stats as sp_stats
from itertools import product
from pathlib import Path
import sys, time

try:
    import yfinance as yf
except ImportError:
    sys.exit('pip install yfinance')

# ── IBKR Tiered Fee Structure (high-volume maker, >3M shares/month) ──
# Commission: $0.0015/share → ~0.03 bps on a $500 stock
# Exchange maker rebate: $0.0020/share → ~0.04 bps
# Net per fill: -$0.0005/share → -0.01 bps (you GET paid)
# SEC fee: $0.0000278/$ on sells → ~0.28 bps on sells → ~0.14 bps amortised
# FINRA TAF: $0.000166/share → negligible
IBKR_NET_REBATE_BPS = 0.01   # net rebate after commission (maker wins)
IBKR_SEC_FEE_BPS    = 0.14   # SEC fee amortised across buys+sells
IBKR_FINRA_BPS      = 0.003  # FINRA TAF amortised
NET_COST_PER_FILL   = IBKR_SEC_FEE_BPS + IBKR_FINRA_BPS - IBKR_NET_REBATE_BPS  # 0.133 bps
ADVERSE_SEL         = 0.45   # 45% of theoretical spread lost to adverse selection

# ── Assets ──
# 10 global ETFs + MAG7 stocks + 3 sector ETFs = 20 assets
ASSETS = [
    'SPY', 'QQQ', 'IWM', 'EFA', 'EEM', 'GLD', 'TLT', 'DIA', 'VGK', 'ACWI',
    'AAPL', 'MSFT', 'GOOGL', 'AMZN', 'NVDA', 'META', 'TSLA',
    'XLK', 'XLF', 'XLV',
]

# ── Walk-forward ──
TRAIN_PCT      = 0.60
MIN_TRAIN_BARS = 756
MIN_TEST_BARS  = 252

# ── Parameter grid (walk-forward searched) — 1296 combos ──
PARAM_GRID = {
    'n_levels':       [8, 12, 16, 20],    # limit levels per side (4 options)
    'level_step_bps': [3, 5, 8],          # tight spacing for more fills
    'order_size':     [0.01, 0.02, 0.03], # per-level order
    'crisis_vol':     [0.16, 0.20, 0.25], # wider detection range
    'crisis_trim':    [0.10, 0.20, 0.30], # aggressive crisis avoidance
    'ema_len':        [5, 10],            # EMA fair-value responsiveness
    'inv_decay':      [0.80, 0.90],       # overlay inventory decay rate
}

# ── Stats ──
N_BOOTSTRAP    = 5_000
BLOCK_SIZE     = 20
N_PERMUTATIONS = 5_000
SIGNIFICANCE   = 0.05

OUT_DIR = Path(__file__).resolve().parent.parent / 'results'
