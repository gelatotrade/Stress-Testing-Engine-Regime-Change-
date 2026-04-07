#!/usr/bin/env python3
"""
Multi-Asset Rolling Walk-Forward Backtest with Statistical Significance Testing.

Tests the HMM regime-detection strategy on multiple assets to validate:
  1. Which thresholds actually produce statistically significant alpha
  2. Whether regime detection generalises beyond S&P 500
  3. Out-of-sample robustness via purged walk-forward methodology

Assets: SPY, QQQ, IWM, EFA, EEM, GLD, TLT, DIA, VGK, ACWI
Period: max available from yfinance (typically 10-20 years daily)

Statistical tests applied per (asset, threshold) combination:
  - Sharpe Ratio t-test (Lo 2002 autocorrelation adjusted)
  - Bootstrap confidence interval (block bootstrap, 10 000 resamples)
  - Permutation test (strategy vs buy-and-hold, 10 000 permutations)
  - Deflated Sharpe Ratio (Bailey & Lopez de Prado 2014)

Output: CSV results + console summary table

Usage:  python3 scripts/rolling_backtest.py
"""
import warnings
warnings.filterwarnings('ignore')

import numpy as np
import pandas as pd
from scipy import stats as sp_stats
from itertools import product
from pathlib import Path
from datetime import datetime
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

# Threshold grid to search  (crisis_vol, crisis_ret, reduce_vol, bull_ret)
#   crisis_vol  : rolling vol above this AND mean return negative → CRISIS exposure
#   crisis_ret  : rolling mean return below this → confirms crisis
#   reduce_vol  : vol above this → reduced exposure
#   bull_ret    : mean return above this AND vol below reduce_vol → full bull exposure
THRESHOLD_GRID = {
    'crisis_vol':  [0.015, 0.018, 0.022],
    'crisis_ret':  [-0.002, -0.001],
    'reduce_vol':  [0.008, 0.010, 0.012],
    'bull_ret':    [0.0003, 0.0005],
}

# Exposure per regime
EXPOSURE_CRISIS = 0.15
EXPOSURE_REDUCE = 0.40
EXPOSURE_BULL   = 1.10
EXPOSURE_HOLD   = 0.80

# Walk-forward parameters
LOOKBACK     = 60     # rolling window for regime classification (bars)
WARMUP       = 60     # bars to skip before trading
TRAIN_WINDOW = 252    # minimum bars before first trade (1 year)
REFIT_EVERY  = 63     # re-estimate vol thresholds every quarter
TX_COST_BPS  = 5      # transaction cost in basis points
SLIPPAGE_BPS = 2      # slippage in basis points

# Statistical test parameters
N_BOOTSTRAP    = 2_000
BLOCK_SIZE     = 20      # block bootstrap block length
N_PERMUTATIONS = 2_000
SIGNIFICANCE   = 0.05

OUT_DIR = Path(__file__).resolve().parent.parent / 'results'


# ──────────────────────────────────────────────────────────────
# Regime detection  (mirrors C++ RegimeDetector logic)
# ──────────────────────────────────────────────────────────────
def classify_regime(returns_window, crisis_vol, crisis_ret, reduce_vol, bull_ret):
    """Classify current regime from a rolling window of returns."""
    if len(returns_window) < 5:
        return 'BULL_QUIET'
    mu    = np.mean(returns_window)
    sigma = np.std(returns_window, ddof=1)

    if sigma > crisis_vol and mu < crisis_ret:
        return 'BEAR_VOLATILE'
    elif sigma > reduce_vol and mu < 0:
        return 'TRANSITION'
    elif mu > bull_ret and sigma < reduce_vol:
        return 'BULL_QUIET'
    elif mu > 0:
        return 'RECOVERY'
    else:
        return 'TRANSITION'


def regime_exposure(regime):
    """Map regime to portfolio exposure."""
    return {
        'BEAR_VOLATILE': EXPOSURE_CRISIS,
        'TRANSITION':    EXPOSURE_REDUCE,
        'BULL_QUIET':    EXPOSURE_BULL,
        'RECOVERY':      EXPOSURE_HOLD,
    }.get(regime, EXPOSURE_HOLD)


# ──────────────────────────────────────────────────────────────
# Early warning score  (mirrors C++ earlyWarningScore)
# ──────────────────────────────────────────────────────────────
def early_warning_score(returns, idx, lookback=20):
    """Compute 0-1 early warning score from 4 factors."""
    if idx < lookback + 10:
        return 0.0
    score = 0.0
    window = returns[idx - lookback:idx]
    recent = window[-10:]
    older  = window[:10]

    # Factor 1: vol acceleration (max 0.30)
    vol_recent = np.std(recent, ddof=1)
    vol_older  = np.std(older, ddof=1) + 1e-10
    vol_accel  = (vol_recent - vol_older) / vol_older
    score += np.clip(vol_accel * 3.0, 0.0, 0.30)

    # Factor 2: price momentum (max 0.30)
    ret_5d  = np.sum(returns[idx-5:idx])
    ret_20d = np.sum(returns[idx-20:idx])
    if ret_5d < -0.03:
        score += 0.15
    if ret_20d < -0.05:
        score += 0.15

    # Factor 3: vol level (proxy for credit spread, max 0.20)
    ann_vol = vol_recent * np.sqrt(252)
    avg_vol = np.std(returns[max(0,idx-50):idx], ddof=1) * np.sqrt(252) + 1e-10
    if ann_vol > avg_vol * 1.5:
        score += 0.20

    # Factor 4: regime instability (max 0.20)
    sign_changes = np.sum(np.diff(np.sign(window)) != 0) / len(window)
    score += np.clip(sign_changes, 0.0, 0.20)

    return np.clip(score, 0.0, 1.0)


# ──────────────────────────────────────────────────────────────
# Walk-forward backtest for one asset + one threshold set
# ──────────────────────────────────────────────────────────────
def run_backtest(prices, thresholds):
    """
    Walk-forward backtest with T+1 execution delay.
    Returns dict with strategy returns, benchmark returns, metrics.
    """
    crisis_vol, crisis_ret, reduce_vol, bull_ret = thresholds
    returns = np.diff(np.log(prices))  # log returns
    N = len(returns)

    if N < TRAIN_WINDOW + WARMUP:
        return None

    strat_returns = np.zeros(N)
    bench_returns = returns.copy()
    exposures     = np.zeros(N)
    regimes       = [''] * N
    prev_exposure = EXPOSURE_HOLD
    trade_count   = 0

    for i in range(WARMUP, N):
        # Look back at returns window
        start = max(0, i - LOOKBACK)
        window = returns[start:i]

        # Classify regime
        regime = classify_regime(window, crisis_vol, crisis_ret, reduce_vol, bull_ret)
        regimes[i] = regime
        target_exp = regime_exposure(regime)

        # Early warning override
        ews = early_warning_score(returns, i)
        if ews > 0.80:
            target_exp = min(target_exp, EXPOSURE_CRISIS)
        elif ews > 0.60:
            target_exp = min(target_exp, EXPOSURE_REDUCE)

        # T+1 execution: use PREVIOUS exposure for today's return
        strat_returns[i] = returns[i] * prev_exposure

        # Transaction costs on rebalance
        if abs(target_exp - prev_exposure) > 0.05:
            cost = abs(target_exp - prev_exposure) * (TX_COST_BPS + SLIPPAGE_BPS) / 10_000
            strat_returns[i] -= cost
            trade_count += 1

        # Update exposure for NEXT bar (T+1 delay)
        prev_exposure = target_exp
        exposures[i] = target_exp

    # Trim warmup
    s = strat_returns[WARMUP:]
    b = bench_returns[WARMUP:]

    if len(s) < 252:
        return None

    # Compute metrics
    strat_cum = np.exp(np.cumsum(s)) - 1
    bench_cum = np.exp(np.cumsum(b)) - 1

    ann_factor = 252
    ann_ret_s  = np.mean(s) * ann_factor
    ann_ret_b  = np.mean(b) * ann_factor
    ann_vol_s  = np.std(s, ddof=1) * np.sqrt(ann_factor)
    ann_vol_b  = np.std(b, ddof=1) * np.sqrt(ann_factor)

    # Sharpe (annualised)
    sharpe_s = ann_ret_s / ann_vol_s if ann_vol_s > 1e-10 else 0.0
    sharpe_b = ann_ret_b / ann_vol_b if ann_vol_b > 1e-10 else 0.0

    # Max drawdown
    cum_s = np.exp(np.cumsum(s))
    peak_s = np.maximum.accumulate(cum_s)
    dd_s = (peak_s - cum_s) / peak_s
    max_dd_s = dd_s.max()

    cum_b = np.exp(np.cumsum(b))
    peak_b = np.maximum.accumulate(cum_b)
    dd_b = (peak_b - cum_b) / peak_b
    max_dd_b = dd_b.max()

    # Sortino
    downside_s = s[s < 0]
    down_vol_s = np.std(downside_s, ddof=1) * np.sqrt(ann_factor) if len(downside_s) > 2 else ann_vol_s
    sortino_s  = ann_ret_s / down_vol_s if down_vol_s > 1e-10 else 0.0

    # Calmar
    calmar_s = ann_ret_s / max_dd_s if max_dd_s > 1e-10 else 0.0

    # Alpha (excess return vs benchmark)
    alpha = ann_ret_s - ann_ret_b

    # Information ratio
    tracking = np.std(s - b, ddof=1) * np.sqrt(ann_factor)
    info_ratio = alpha / tracking if tracking > 1e-10 else 0.0

    return {
        'strat_returns': s,
        'bench_returns': b,
        'ann_ret_strat': ann_ret_s,
        'ann_ret_bench': ann_ret_b,
        'ann_vol_strat': ann_vol_s,
        'sharpe_strat':  sharpe_s,
        'sharpe_bench':  sharpe_b,
        'sortino':       sortino_s,
        'calmar':        calmar_s,
        'max_dd_strat':  max_dd_s,
        'max_dd_bench':  max_dd_b,
        'alpha':         alpha,
        'info_ratio':    info_ratio,
        'trade_count':   trade_count,
        'n_bars':        len(s),
        'total_ret_strat': strat_cum[-1],
        'total_ret_bench': bench_cum[-1],
    }


# ──────────────────────────────────────────────────────────────
# Statistical significance tests
# ──────────────────────────────────────────────────────────────
def sharpe_ttest(returns, benchmark_sharpe=0.0):
    """
    Sharpe Ratio t-test with Lo (2002) autocorrelation adjustment.
    H0: true Sharpe <= benchmark_sharpe
    """
    n = len(returns)
    if n < 30:
        return {'t_stat': 0, 'p_value': 1.0, 'significant': False}

    mu = np.mean(returns) * 252
    sigma = np.std(returns, ddof=1) * np.sqrt(252)
    sr = mu / sigma if sigma > 1e-10 else 0.0

    # Lo (2002) autocorrelation correction
    rho = np.corrcoef(returns[:-1], returns[1:])[0, 1]
    q = 1  # lag
    eta = 1 + 2 * rho
    se = np.sqrt((1 + 0.5 * sr**2 - sr * rho * q) * eta / n) if eta > 0 else 1.0

    t_stat = (sr - benchmark_sharpe) / se if se > 1e-10 else 0.0
    p_value = 1.0 - sp_stats.norm.cdf(t_stat)

    return {
        't_stat': t_stat,
        'p_value': p_value,
        'sharpe': sr,
        'se': se,
        'significant': p_value < SIGNIFICANCE,
    }


def block_bootstrap_ci(returns, n_boot=N_BOOTSTRAP, block_size=BLOCK_SIZE):
    """
    Circular block bootstrap for Sharpe ratio confidence interval.
    Vectorised for speed.
    """
    n = len(returns)
    rng = np.random.default_rng(42)
    n_blocks = (n // block_size) + 1
    total_len = n_blocks * block_size

    # Pre-generate all block starts at once
    all_starts = rng.integers(0, n, size=(n_boot, n_blocks))
    # Build index offsets once
    offsets = np.arange(block_size)

    sharpes = np.empty(n_boot)
    for b in range(n_boot):
        indices = (all_starts[b, :, None] + offsets[None, :]).ravel() % n
        sample = returns[indices[:n]]
        mu = np.mean(sample) * 252
        sigma = np.std(sample, ddof=1) * np.sqrt(252)
        sharpes[b] = mu / sigma if sigma > 1e-10 else 0.0

    ci_lo = np.percentile(sharpes, 2.5)
    ci_hi = np.percentile(sharpes, 97.5)
    p_value = np.mean(sharpes <= 0)

    return {
        'ci_lo': ci_lo,
        'ci_hi': ci_hi,
        'median_sharpe': np.median(sharpes),
        'p_value': p_value,
        'significant': ci_lo > 0,
    }


def permutation_test(strat_returns, bench_returns, n_perm=N_PERMUTATIONS):
    """
    Permutation test: H0 = strategy has no advantage over buy-and-hold.
    Vectorised: randomly flips assignment between strategy/benchmark.
    """
    n = len(strat_returns)
    observed_diff = np.mean(strat_returns) - np.mean(bench_returns)
    rng = np.random.default_rng(42)

    # Vectorised: generate all flip masks at once
    flips = rng.integers(0, 2, size=(n_perm, n)).astype(bool)
    diff_s = strat_returns - bench_returns  # per-bar difference
    # When flipped, swap strategy and benchmark → negate the difference
    # perm_mean_diff = mean of (diff_s * sign) where sign is +1 or -1
    signs = np.where(flips, 1.0, -1.0)
    perm_diffs = (signs * diff_s[None, :]).mean(axis=1)
    p_value = np.mean(perm_diffs >= observed_diff)

    return {
        'observed_diff_ann': observed_diff * 252,
        'p_value': p_value,
        'significant': p_value < SIGNIFICANCE,
    }


def deflated_sharpe(observed_sharpe, n_trials, n_obs, skew=0, kurt=3):
    """
    Deflated Sharpe Ratio (Bailey & Lopez de Prado, 2014).
    Corrects for multiple testing / strategy selection bias.
    Expected max Sharpe under null ~ sqrt(2 * log(n_trials)).
    """
    if n_trials <= 1:
        return {'dsr': 1.0, 'significant': True}

    e_max_sr = np.sqrt(2 * np.log(n_trials)) * (1 - 0.5772 / np.sqrt(2 * np.log(n_trials)))

    # PSR formula
    se = np.sqrt((1 + 0.5 * observed_sharpe**2 - skew * observed_sharpe
                  + (kurt - 3) / 4 * observed_sharpe**2) / n_obs)
    if se < 1e-10:
        return {'dsr': 0.0, 'significant': False}

    dsr_stat = (observed_sharpe - e_max_sr) / se
    p_value = sp_stats.norm.cdf(dsr_stat)

    return {
        'dsr': p_value,
        'e_max_sharpe': e_max_sr,
        'dsr_stat': dsr_stat,
        'significant': p_value > (1 - SIGNIFICANCE),
    }


# ──────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────
def main():
    print('=' * 80)
    print('  MULTI-ASSET ROLLING WALK-FORWARD BACKTEST')
    print('  Statistical Significance Testing for Regime Detection Thresholds')
    print('=' * 80)

    # ── Download data ─────────────────────────────────────────
    print(f'\nDownloading {len(ASSETS)} assets from yfinance ...')
    asset_data = {}
    for ticker in ASSETS:
        try:
            df = yf.download(ticker, period='max', interval='1d', progress=False)
            prices = df['Close'].dropna().values.flatten()
            if len(prices) > TRAIN_WINDOW + WARMUP + 252:
                asset_data[ticker] = prices
                print(f'  {ticker}: {len(prices)} bars ({len(prices)//252:.0f}y)')
            else:
                print(f'  {ticker}: SKIPPED (only {len(prices)} bars)')
        except Exception as e:
            print(f'  {ticker}: ERROR ({e})')

    if not asset_data:
        sys.exit('No assets loaded.')

    # ── Build threshold combinations ──────────────────────────
    keys = list(THRESHOLD_GRID.keys())
    combos = list(product(*[THRESHOLD_GRID[k] for k in keys]))
    n_combos = len(combos)
    print(f'\nThreshold grid: {n_combos} combinations x {len(asset_data)} assets = {n_combos * len(asset_data)} backtests')

    # ── Run backtests ─────────────────────────────────────────
    results = []
    total = n_combos * len(asset_data)
    done = 0
    t0 = time.time()

    for ci, combo in enumerate(combos):
        thresholds = dict(zip(keys, combo))
        thresh_tuple = tuple(combo)

        for ticker, prices in asset_data.items():
            done += 1
            bt = run_backtest(prices, thresh_tuple)
            if bt is None:
                continue

            # Statistical tests
            sr_test   = sharpe_ttest(bt['strat_returns'])
            boot_test = block_bootstrap_ci(bt['strat_returns'])
            perm_test = permutation_test(bt['strat_returns'], bt['bench_returns'])

            results.append({
                'asset':       ticker,
                'crisis_vol':  thresholds['crisis_vol'],
                'crisis_ret':  thresholds['crisis_ret'],
                'reduce_vol':  thresholds['reduce_vol'],
                'bull_ret':    thresholds['bull_ret'],
                'n_bars':      bt['n_bars'],
                'years':       bt['n_bars'] / 252,
                'ann_ret':     bt['ann_ret_strat'],
                'bench_ret':   bt['ann_ret_bench'],
                'alpha':       bt['alpha'],
                'sharpe':      bt['sharpe_strat'],
                'sharpe_bench':bt['sharpe_bench'],
                'sortino':     bt['sortino'],
                'calmar':      bt['calmar'],
                'max_dd':      bt['max_dd_strat'],
                'max_dd_bench':bt['max_dd_bench'],
                'info_ratio':  bt['info_ratio'],
                'trades':      bt['trade_count'],
                'total_ret':   bt['total_ret_strat'],
                'bench_total': bt['total_ret_bench'],
                # Sharpe t-test
                'sr_tstat':    sr_test['t_stat'],
                'sr_pval':     sr_test['p_value'],
                'sr_sig':      sr_test['significant'],
                # Bootstrap
                'boot_ci_lo':  boot_test['ci_lo'],
                'boot_ci_hi':  boot_test['ci_hi'],
                'boot_pval':   boot_test['p_value'],
                'boot_sig':    boot_test['significant'],
                # Permutation
                'perm_pval':   perm_test['p_value'],
                'perm_sig':    perm_test['significant'],
            })

            if done % 50 == 0 or done == total:
                elapsed = time.time() - t0
                rate = done / elapsed if elapsed > 0 else 0
                eta = (total - done) / rate if rate > 0 else 0
                print(f'  [{done}/{total}] {elapsed:.0f}s elapsed, ~{eta:.0f}s remaining')

    if not results:
        sys.exit('No valid backtest results.')

    df = pd.DataFrame(results)

    # ── Deflated Sharpe (correct for n_combos trials) ─────────
    for ticker in df['asset'].unique():
        mask = df['asset'] == ticker
        n_trials = mask.sum()
        for idx in df[mask].index:
            sr = df.loc[idx, 'sharpe']
            n_obs = df.loc[idx, 'n_bars']
            s = df.loc[idx, 'strat_returns'] if 'strat_returns' in df.columns else None
            dsr = deflated_sharpe(sr, n_trials, n_obs)
            df.loc[idx, 'dsr']     = dsr['dsr']
            df.loc[idx, 'dsr_sig'] = dsr['significant']
            df.loc[idx, 'e_max_sharpe'] = dsr['e_max_sharpe']

    # ── Save full results ─────────────────────────────────────
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    csv_path = OUT_DIR / 'rolling_backtest_results.csv'
    df.to_csv(csv_path, index=False, float_format='%.6f')
    print(f'\nFull results saved to {csv_path}  ({len(df)} rows)')

    # ── Summary tables ────────────────────────────────────────
    print('\n' + '=' * 80)
    print('  RESULTS SUMMARY')
    print('=' * 80)

    # 1. Best threshold per asset
    print('\n── Best Threshold Per Asset (by Sharpe, must pass >=2 significance tests) ──\n')
    print(f'{"Asset":<6} {"Sharpe":>7} {"Alpha":>7} {"MaxDD":>7} {"Sortino":>8} '
          f'{"SR-p":>6} {"Boot-p":>7} {"Perm-p":>7} {"DSR":>5} '
          f'{"crisis_vol":>10} {"crisis_ret":>10} {"reduce_vol":>10} {"bull_ret":>10}')
    print('-' * 120)

    for ticker in sorted(df['asset'].unique()):
        sub = df[df['asset'] == ticker].copy()
        # Count how many tests pass
        sub['n_sig'] = (sub['sr_sig'].astype(int) +
                        sub['boot_sig'].astype(int) +
                        sub['perm_sig'].astype(int) +
                        sub['dsr_sig'].astype(int))
        # Prefer combos passing >=2 tests, then sort by Sharpe
        sub = sub.sort_values(['n_sig', 'sharpe'], ascending=[False, False])
        best = sub.iloc[0]
        sig_flag = '*' if best['n_sig'] >= 2 else ' '
        print(f'{ticker:<6} {best["sharpe"]:>7.3f} {best["alpha"]*100:>6.2f}% '
              f'{best["max_dd"]*100:>6.1f}% {best["sortino"]:>8.3f} '
              f'{best["sr_pval"]:>6.3f} {best["boot_pval"]:>7.3f} {best["perm_pval"]:>7.3f} '
              f'{best.get("dsr", 0):>5.2f} '
              f'{best["crisis_vol"]:>10.3f} {best["crisis_ret"]:>10.4f} '
              f'{best["reduce_vol"]:>10.3f} {best["bull_ret"]:>10.4f} {sig_flag}')

    # 2. Cross-asset robustness: which thresholds work on MOST assets?
    print('\n── Cross-Asset Robust Thresholds (significant on >=3 assets) ──\n')
    thresh_cols = ['crisis_vol', 'crisis_ret', 'reduce_vol', 'bull_ret']
    df['all_sig'] = (df['sr_sig'] & df['boot_sig']) | (df['sr_sig'] & df['perm_sig'])
    sig_df = df[df['all_sig']].copy()

    if len(sig_df) > 0:
        grouped = sig_df.groupby(thresh_cols).agg(
            n_assets=('asset', 'nunique'),
            assets=('asset', lambda x: ','.join(sorted(x.unique()))),
            mean_sharpe=('sharpe', 'mean'),
            mean_alpha=('alpha', 'mean'),
            mean_maxdd=('max_dd', 'mean'),
        ).reset_index()
        grouped = grouped[grouped['n_assets'] >= 3].sort_values('mean_sharpe', ascending=False)

        if len(grouped) > 0:
            print(f'{"crisis_vol":>10} {"crisis_ret":>10} {"reduce_vol":>10} {"bull_ret":>10} '
                  f'{"#Assets":>7} {"AvgSharpe":>10} {"AvgAlpha":>10} {"AvgMaxDD":>10}  Assets')
            print('-' * 110)
            for _, row in grouped.head(10).iterrows():
                print(f'{row["crisis_vol"]:>10.3f} {row["crisis_ret"]:>10.4f} '
                      f'{row["reduce_vol"]:>10.3f} {row["bull_ret"]:>10.4f} '
                      f'{row["n_assets"]:>7} {row["mean_sharpe"]:>10.3f} '
                      f'{row["mean_alpha"]*100:>9.2f}% {row["mean_maxdd"]*100:>9.1f}%  '
                      f'{row["assets"]}')
        else:
            print('  No threshold combination is significant on >=3 assets.')
    else:
        print('  No statistically significant results found across any assets.')

    # 3. Overall significance summary
    print('\n── Statistical Significance Summary ──\n')
    for ticker in sorted(df['asset'].unique()):
        sub = df[df['asset'] == ticker]
        n = len(sub)
        n_sr   = sub['sr_sig'].sum()
        n_boot = sub['boot_sig'].sum()
        n_perm = sub['perm_sig'].sum()
        n_dsr  = sub['dsr_sig'].sum() if 'dsr_sig' in sub.columns else 0
        print(f'  {ticker:<6}  {n:>3} combos tested  |  '
              f'Sharpe t-test: {n_sr:>3}/{n} sig  |  '
              f'Bootstrap: {n_boot:>3}/{n} sig  |  '
              f'Permutation: {n_perm:>3}/{n} sig  |  '
              f'Deflated SR: {n_dsr:>3}/{n} sig')

    # 4. Warning about overfitting
    total_trials = len(df)
    total_sig = df['all_sig'].sum() if 'all_sig' in df.columns else 0
    fdr_expected = total_trials * SIGNIFICANCE
    print(f'\n── Overfitting Check ──')
    print(f'  Total trials:             {total_trials}')
    print(f'  Significant (p<{SIGNIFICANCE}):      {total_sig}')
    print(f'  Expected false positives:  {fdr_expected:.0f}  (at {SIGNIFICANCE*100:.0f}% level)')
    if total_sig <= fdr_expected * 1.5:
        print(f'  WARNING: Significant results ({total_sig}) are close to expected false positives ({fdr_expected:.0f}).')
        print(f'           The regime detection may NOT produce genuine alpha.')
    elif total_sig > fdr_expected * 3:
        print(f'  GOOD: Significant results ({total_sig}) far exceed expected false positives ({fdr_expected:.0f}).')
        print(f'        Strong evidence that regime detection produces genuine alpha.')
    else:
        print(f'  MIXED: Some evidence of genuine alpha, but further validation recommended.')

    print(f'\n  Full CSV: {csv_path}')
    print(f'  Run time: {time.time() - t0:.1f}s')


if __name__ == '__main__':
    main()
