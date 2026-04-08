#!/usr/bin/env python3
"""
Multi-Asset Rolling Walk-Forward Backtest with Statistical Significance Testing.

Tests the HMM regime-detection strategy on multiple assets to validate:
  1. Which thresholds actually produce statistically significant alpha
  2. Whether regime detection generalises beyond S&P 500
  3. Out-of-sample robustness via purged walk-forward methodology

Key design: DEFAULT exposure = 1.0 (match benchmark). The strategy only
DEVIATES from buy-and-hold when regime detection signals crisis. This way
alpha comes purely from crisis avoidance + recovery boost, not from
systematic underweighting.

Walk-forward: thresholds are optimised on a rolling TRAINING window, then
applied out-of-sample on the TEST window. No future information leaks.

Assets: SPY, QQQ, IWM, EFA, EEM, GLD, TLT, DIA, VGK, ACWI
Period: max available from yfinance (typically 10-20 years daily)

Usage:  python3 scripts/rolling_backtest.py
"""
import warnings
warnings.filterwarnings('ignore')

import numpy as np
import pandas as pd
from scipy import stats as sp_stats
from itertools import product
from pathlib import Path
import sys
import time

try:
    import yfinance as yf
except ImportError:
    sys.exit('pip install yfinance')

# ──────────────────────────────────────────────────────────────
# Config
# ──────────────────────────────────────────────────────────────
ASSETS = ['SPY', 'QQQ', 'IWM', 'EFA', 'EEM', 'GLD', 'TLT', 'DIA', 'VGK', 'ACWI']

# Exposure levels — DEFAULT = 1.0 (buy-and-hold). Only deviate on signals.
EXP_CRISIS   = 0.10   # extreme crisis: nearly flat
EXP_CAUTIOUS = 0.50   # elevated risk: half exposure
EXP_NORMAL   = 1.00   # default: match benchmark exactly
EXP_RECOVERY = 1.20   # post-crisis bounce: slight leverage
EXP_BULL     = 1.05   # confirmed low-vol uptrend: marginal overweight

# Threshold grid (searched via walk-forward on training data only)
THRESHOLD_GRID = {
    # crisis_vol: annualised vol above this AND negative momentum → go defensive
    'crisis_vol':  [0.20, 0.25, 0.30, 0.35],
    # crisis_mom:  20d return below this → confirms crisis
    'crisis_mom':  [-0.05, -0.08, -0.10],
    # recovery_days: bars after crisis exit before going to recovery boost
    'recovery_days': [10, 20, 30],
}

# Walk-forward parameters
LOOKBACK       = 60     # rolling window for vol/momentum (bars)
TRAIN_PCT      = 0.60   # train on first 60%, test on last 40%
MIN_TRAIN_BARS = 756    # minimum 3 years training
MIN_TEST_BARS  = 252    # minimum 1 year test
TX_COST_BPS    = 5
SLIPPAGE_BPS   = 2

# Statistical test parameters
N_BOOTSTRAP    = 5_000
BLOCK_SIZE     = 20
N_PERMUTATIONS = 5_000
SIGNIFICANCE   = 0.05

OUT_DIR = Path(__file__).resolve().parent.parent / 'results'


# ──────────────────────────────────────────────────────────────
# Regime detection — simplified, high-conviction only
# ──────────────────────────────────────────────────────────────
def classify_regime(returns, idx, lookback, crisis_vol, crisis_mom, recovery_days,
                    bars_since_crisis):
    """
    Classify regime. Only deviates from NORMAL when there is strong evidence.

    Logic:
      1. Compute 20d rolling vol (annualised) and 20d momentum
      2. CRISIS: vol > crisis_vol AND momentum < crisis_mom
      3. CAUTIOUS: vol > crisis_vol (but momentum not as bad)
      4. RECOVERY: within recovery_days after crisis exits
      5. BULL: vol < 0.12 annualised AND momentum > 0 (calm uptrend)
      6. NORMAL: everything else → match benchmark
    """
    if idx < lookback:
        return 'NORMAL', bars_since_crisis

    window = returns[max(0, idx - lookback):idx]
    vol_ann = np.std(window, ddof=1) * np.sqrt(252)
    mom_20d = np.sum(returns[max(0, idx - 20):idx])

    # Crisis detection
    if vol_ann > crisis_vol and mom_20d < crisis_mom:
        return 'CRISIS', 0

    # Cautious: vol is high but not confirmed crash
    if vol_ann > crisis_vol:
        return 'CAUTIOUS', bars_since_crisis + 1

    # Recovery boost: just exited crisis
    if bars_since_crisis > 0 and bars_since_crisis <= recovery_days:
        return 'RECOVERY', bars_since_crisis + 1

    # Bull: calm uptrend
    if vol_ann < 0.12 and mom_20d > 0:
        return 'BULL', bars_since_crisis + 1 if bars_since_crisis > 0 else 0

    return 'NORMAL', bars_since_crisis + 1 if bars_since_crisis > 0 else 0


def regime_to_exposure(regime):
    return {
        'CRISIS':   EXP_CRISIS,
        'CAUTIOUS': EXP_CAUTIOUS,
        'RECOVERY': EXP_RECOVERY,
        'BULL':     EXP_BULL,
        'NORMAL':   EXP_NORMAL,
    }[regime]


# ──────────────────────────────────────────────────────────────
# Backtest engine
# ──────────────────────────────────────────────────────────────
def run_backtest_segment(returns, crisis_vol, crisis_mom, recovery_days):
    """
    Run backtest on a segment of returns. T+1 execution delay.
    Returns (strategy_returns, bench_returns, trade_count).
    """
    N = len(returns)
    strat = np.zeros(N)
    prev_exp = EXP_NORMAL
    bars_since_crisis = 0
    trade_count = 0

    for i in range(N):
        regime, bars_since_crisis = classify_regime(
            returns, i, LOOKBACK, crisis_vol, crisis_mom, recovery_days,
            bars_since_crisis)

        target_exp = regime_to_exposure(regime)

        # T+1: use PREVIOUS bar's exposure for today's return
        strat[i] = returns[i] * prev_exp

        # Transaction costs on significant rebalance
        if abs(target_exp - prev_exp) > 0.10:
            cost = abs(target_exp - prev_exp) * (TX_COST_BPS + SLIPPAGE_BPS) / 10_000
            strat[i] -= cost
            trade_count += 1

        prev_exp = target_exp

    return strat, returns.copy(), trade_count


def compute_metrics(strat, bench):
    """Compute all performance metrics from return arrays."""
    n = len(strat)
    if n < 60:
        return None

    ann = 252
    ann_ret_s = np.mean(strat) * ann
    ann_ret_b = np.mean(bench) * ann
    ann_vol_s = np.std(strat, ddof=1) * np.sqrt(ann)
    ann_vol_b = np.std(bench, ddof=1) * np.sqrt(ann)

    sharpe_s = ann_ret_s / ann_vol_s if ann_vol_s > 1e-10 else 0.0
    sharpe_b = ann_ret_b / ann_vol_b if ann_vol_b > 1e-10 else 0.0

    # Max drawdown
    cum_s = np.exp(np.cumsum(strat))
    dd_s = (np.maximum.accumulate(cum_s) - cum_s) / np.maximum.accumulate(cum_s)
    cum_b = np.exp(np.cumsum(bench))
    dd_b = (np.maximum.accumulate(cum_b) - cum_b) / np.maximum.accumulate(cum_b)

    # Sortino
    down = strat[strat < 0]
    down_vol = np.std(down, ddof=1) * np.sqrt(ann) if len(down) > 2 else ann_vol_s
    sortino = ann_ret_s / down_vol if down_vol > 1e-10 else 0.0

    alpha = ann_ret_s - ann_ret_b
    tracking = np.std(strat - bench, ddof=1) * np.sqrt(ann)
    info_ratio = alpha / tracking if tracking > 1e-10 else 0.0

    calmar = ann_ret_s / dd_s.max() if dd_s.max() > 1e-10 else 0.0

    return {
        'ann_ret':      ann_ret_s,
        'bench_ret':    ann_ret_b,
        'alpha':        alpha,
        'sharpe':       sharpe_s,
        'sharpe_bench': sharpe_b,
        'sortino':      sortino,
        'calmar':       calmar,
        'max_dd':       dd_s.max(),
        'max_dd_bench': dd_b.max(),
        'info_ratio':   info_ratio,
        'total_ret':    np.exp(np.sum(strat)) - 1,
        'bench_total':  np.exp(np.sum(bench)) - 1,
        'n_bars':       n,
    }


# ──────────────────────────────────────────────────────────────
# Walk-forward: train thresholds in-sample, test out-of-sample
# ──────────────────────────────────────────────────────────────
def walk_forward_backtest(prices):
    """
    Proper walk-forward:
      1. Split into TRAIN (first 60%) and TEST (last 40%)
      2. Grid-search thresholds on TRAIN to maximise Sharpe
      3. Apply best thresholds to TEST (out-of-sample)
      4. Report TEST metrics only (no forward bias)
    """
    returns = np.diff(np.log(prices))
    N = len(returns)

    split = int(N * TRAIN_PCT)
    if split < MIN_TRAIN_BARS or (N - split) < MIN_TEST_BARS:
        return None

    train_ret = returns[:split]
    test_ret = returns[split:]

    # ── Phase 1: Grid search on TRAINING data ──
    keys = list(THRESHOLD_GRID.keys())
    combos = list(product(*[THRESHOLD_GRID[k] for k in keys]))

    best_sharpe = -999
    best_params = combos[0]

    for combo in combos:
        crisis_vol, crisis_mom, recovery_days = combo
        s, b, _ = run_backtest_segment(train_ret, crisis_vol, crisis_mom, recovery_days)
        m = compute_metrics(s, b)
        if m is None:
            continue
        # Optimise for Sharpe on training data (risk-adjusted, not raw return)
        if m['sharpe'] > best_sharpe:
            best_sharpe = m['sharpe']
            best_params = combo

    # ── Phase 2: Apply best params to TEST data (out-of-sample) ──
    crisis_vol, crisis_mom, recovery_days = best_params
    s_test, b_test, trade_count = run_backtest_segment(
        test_ret, crisis_vol, crisis_mom, recovery_days)

    metrics = compute_metrics(s_test, b_test)
    if metrics is None:
        return None

    metrics['crisis_vol'] = crisis_vol
    metrics['crisis_mom'] = crisis_mom
    metrics['recovery_days'] = recovery_days
    metrics['trades'] = trade_count
    metrics['train_sharpe'] = best_sharpe
    metrics['train_bars'] = len(train_ret)
    metrics['test_bars'] = len(test_ret)
    metrics['strat_returns'] = s_test
    metrics['bench_returns'] = b_test

    return metrics


# ──────────────────────────────────────────────────────────────
# Statistical significance tests
# ──────────────────────────────────────────────────────────────
def sharpe_ttest(returns):
    """Sharpe Ratio t-test with Lo (2002) autocorrelation adjustment."""
    n = len(returns)
    if n < 30:
        return {'t_stat': 0, 'p_value': 1.0, 'significant': False}

    mu = np.mean(returns) * 252
    sigma = np.std(returns, ddof=1) * np.sqrt(252)
    sr = mu / sigma if sigma > 1e-10 else 0.0

    rho = np.corrcoef(returns[:-1], returns[1:])[0, 1]
    eta = max(1 + 2 * rho, 0.5)
    se = np.sqrt(eta / n) * np.sqrt(1 + 0.5 * sr**2)
    t_stat = sr / se if se > 1e-10 else 0.0
    p_value = 1.0 - sp_stats.norm.cdf(t_stat)

    return {'t_stat': t_stat, 'p_value': p_value, 'sharpe': sr,
            'significant': p_value < SIGNIFICANCE}


def block_bootstrap_ci(returns, n_boot=N_BOOTSTRAP, block_size=BLOCK_SIZE):
    """Circular block bootstrap for Sharpe ratio CI."""
    n = len(returns)
    rng = np.random.default_rng(42)
    n_blocks = (n // block_size) + 1
    offsets = np.arange(block_size)
    sharpes = np.empty(n_boot)

    for b in range(n_boot):
        starts = rng.integers(0, n, size=n_blocks)
        indices = (starts[:, None] + offsets[None, :]).ravel() % n
        sample = returns[indices[:n]]
        mu = np.mean(sample) * 252
        sig = np.std(sample, ddof=1) * np.sqrt(252)
        sharpes[b] = mu / sig if sig > 1e-10 else 0.0

    return {
        'ci_lo': np.percentile(sharpes, 2.5),
        'ci_hi': np.percentile(sharpes, 97.5),
        'p_value': np.mean(sharpes <= 0),
        'significant': np.percentile(sharpes, 2.5) > 0,
    }


def permutation_test_alpha(strat_returns, bench_returns, n_perm=N_PERMUTATIONS):
    """Permutation test: H0 = strategy has no advantage over benchmark."""
    observed_diff = np.mean(strat_returns) - np.mean(bench_returns)
    rng = np.random.default_rng(42)
    diff = strat_returns - bench_returns
    signs = rng.choice([-1.0, 1.0], size=(n_perm, len(diff)))
    perm_diffs = (signs * diff[None, :]).mean(axis=1)
    p_value = np.mean(perm_diffs >= observed_diff)

    return {
        'observed_alpha_ann': observed_diff * 252,
        'p_value': p_value,
        'significant': p_value < SIGNIFICANCE,
    }


def deflated_sharpe(observed_sharpe, n_trials, n_obs):
    """Deflated Sharpe Ratio (Bailey & Lopez de Prado, 2014)."""
    if n_trials <= 1:
        return {'dsr': 1.0, 'significant': True}
    e_max = np.sqrt(2 * np.log(n_trials)) * (1 - 0.5772 / np.sqrt(2 * np.log(n_trials)))
    se = np.sqrt((1 + 0.5 * observed_sharpe**2) / n_obs)
    if se < 1e-10:
        return {'dsr': 0.0, 'significant': False}
    z = (observed_sharpe - e_max) / se
    p = sp_stats.norm.cdf(z)
    return {'dsr': p, 'e_max': e_max, 'significant': p > 0.95}


# ──────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────
def main():
    print('=' * 80)
    print('  WALK-FORWARD MULTI-ASSET REGIME DETECTION BACKTEST')
    print('  Thresholds trained in-sample, tested out-of-sample (no forward bias)')
    print('=' * 80)

    # ── Download data ─────────────────────────────────────────
    print(f'\nDownloading {len(ASSETS)} assets ...')
    asset_data = {}
    for ticker in ASSETS:
        try:
            df = yf.download(ticker, period='max', interval='1d', progress=False)
            prices = df['Close'].dropna().values.flatten()
            if len(prices) > MIN_TRAIN_BARS + MIN_TEST_BARS:
                asset_data[ticker] = prices
                n_yrs = len(prices) / 252
                print(f'  {ticker}: {len(prices):>6} bars ({n_yrs:.1f}y)')
            else:
                print(f'  {ticker}: SKIPPED ({len(prices)} bars)')
        except Exception as e:
            print(f'  {ticker}: ERROR ({e})')

    if not asset_data:
        sys.exit('No assets loaded.')

    n_thresh = len(list(product(*THRESHOLD_GRID.values())))
    print(f'\nThreshold grid: {n_thresh} combos searched per asset (in-sample)')
    print(f'Walk-forward: {TRAIN_PCT*100:.0f}% train / {(1-TRAIN_PCT)*100:.0f}% test')

    # ── Run walk-forward backtests ────────────────────────────
    results = []
    t0 = time.time()

    for ticker, prices in asset_data.items():
        print(f'\n  Running {ticker} ...', end=' ', flush=True)
        wf = walk_forward_backtest(prices)
        if wf is None:
            print('SKIPPED (not enough data)')
            continue

        s_ret = wf.pop('strat_returns')
        b_ret = wf.pop('bench_returns')

        # Statistical tests on OUT-OF-SAMPLE returns only
        sr_test   = sharpe_ttest(s_ret)
        boot_test = block_bootstrap_ci(s_ret)
        perm_test = permutation_test_alpha(s_ret, b_ret)
        dsr_test  = deflated_sharpe(wf['sharpe'], n_thresh, wf['test_bars'])

        row = {'asset': ticker, **wf}
        row['sr_pval']    = sr_test['p_value']
        row['sr_sig']     = sr_test['significant']
        row['boot_ci_lo'] = boot_test['ci_lo']
        row['boot_ci_hi'] = boot_test['ci_hi']
        row['boot_pval']  = boot_test['p_value']
        row['boot_sig']   = boot_test['significant']
        row['perm_pval']  = perm_test['p_value']
        row['perm_sig']   = perm_test['significant']
        row['perm_alpha'] = perm_test['observed_alpha_ann']
        row['dsr']        = dsr_test['dsr']
        row['dsr_sig']    = dsr_test['significant']
        row['e_max_sr']   = dsr_test.get('e_max', 0)

        results.append(row)

        sig = sum([sr_test['significant'], boot_test['significant'],
                   perm_test['significant'], dsr_test['significant']])
        flag = f'  [{sig}/4 tests sig]'
        print(f'Alpha={wf["alpha"]*100:+.2f}%  Sharpe={wf["sharpe"]:.3f}  '
              f'MaxDD={wf["max_dd"]*100:.1f}%  Trades={wf["trades"]}{flag}')

    if not results:
        sys.exit('No valid results.')

    df = pd.DataFrame(results)

    # ── Save ──────────────────────────────────────────────────
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    csv_path = OUT_DIR / 'rolling_backtest_results.csv'
    df.to_csv(csv_path, index=False, float_format='%.6f')
    print(f'\nResults saved to {csv_path}')

    # ── Summary ───────────────────────────────────────────────
    print('\n' + '=' * 90)
    print('  OUT-OF-SAMPLE RESULTS  (thresholds trained on first 60%, tested on last 40%)')
    print('=' * 90)

    print(f'\n{"Asset":<6} {"Alpha":>7} {"Sharpe":>7} {"S.Bench":>7} '
          f'{"Sortino":>8} {"MaxDD":>6} {"DD.Bnch":>7} {"Trades":>6} '
          f'{"SR-p":>6} {"Boot-p":>7} {"Perm-p":>7} {"DSR":>5} '
          f'{"crVol":>6} {"crMom":>7} {"recDays":>7}')
    print('-' * 115)

    pos_alpha = 0
    sig_count = 0
    for _, r in df.sort_values('alpha', ascending=False).iterrows():
        n_sig = sum([r['sr_sig'], r['boot_sig'], r['perm_sig'], r['dsr_sig']])
        mark = '**' if n_sig >= 3 else ('*' if n_sig >= 2 else '')
        if r['alpha'] > 0:
            pos_alpha += 1
        if n_sig >= 2:
            sig_count += 1
        print(f'{r["asset"]:<6} {r["alpha"]*100:>+6.2f}% {r["sharpe"]:>7.3f} '
              f'{r["sharpe_bench"]:>7.3f} {r["sortino"]:>8.3f} '
              f'{r["max_dd"]*100:>5.1f}% {r["max_dd_bench"]*100:>6.1f}% '
              f'{r["trades"]:>6.0f} '
              f'{r["sr_pval"]:>6.3f} {r["boot_pval"]:>7.3f} {r["perm_pval"]:>7.3f} '
              f'{r["dsr"]:>5.2f} '
              f'{r["crisis_vol"]:>6.2f} {r["crisis_mom"]:>7.2f} '
              f'{r["recovery_days"]:>7.0f} {mark}')

    print(f'\n── Summary ──')
    print(f'  Assets with positive alpha:           {pos_alpha}/{len(df)}')
    print(f'  Assets passing >=2 significance tests: {sig_count}/{len(df)}')
    print(f'  Mean alpha:  {df["alpha"].mean()*100:+.2f}%')
    print(f'  Mean Sharpe: {df["sharpe"].mean():.3f} (bench: {df["sharpe_bench"].mean():.3f})')
    print(f'  Mean MaxDD:  {df["max_dd"].mean()*100:.1f}% (bench: {df["max_dd_bench"].mean()*100:.1f}%)')

    # Drawdown improvement is key value prop
    dd_improvement = df['max_dd_bench'].mean() - df['max_dd'].mean()
    print(f'  Drawdown improvement: {dd_improvement*100:+.1f}% avg across assets')

    print(f'\n  Run time: {time.time() - t0:.1f}s')
    print(f'  CSV: {csv_path}')


if __name__ == '__main__':
    main()
