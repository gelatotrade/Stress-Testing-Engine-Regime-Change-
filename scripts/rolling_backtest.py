#!/usr/bin/env python3
"""
Market-Maker Regime-Detection Backtest — Limit Orders & Maker Rebates.

Architecture:
  BASE POSITION = 100% long (captures equity premium, matches benchmark).
  OVERLAY = market-making via limit orders around fair value.
  REGIME  = controls spread width, overlay size, and base position trim.

Alpha sources:
  1. Spread capture: buy at bid, sell at ask → pocket the spread
  2. Maker rebates: +0.20 bps per filled limit order (not paying taker -0.30)
  3. Mean-reversion scalping: multiple limit levels catch intraday swings
  4. Crisis avoidance: trim base position 10-20% during bear volatile

Key: the strategy is ALWAYS ~90-100% invested. Alpha comes from the
market-making overlay ON TOP of the base position, not from timing.

OHLC fill model: multiple limit levels per bar. A limit at price P fills
if the bar's range [Low, High] crosses P. Number of fills per bar scales
with the day's range.

Walk-forward: params trained on first 60%, tested on last 40%.

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

# ── Fee structure (NYSE/NASDAQ typical) ──
MAKER_REBATE_BPS = 0.20
SEC_FEE_BPS      = 0.02
ADVERSE_SEL      = 0.45    # 45% of theoretical spread lost to adverse selection

# ── Assets ──
ASSETS = ['SPY', 'QQQ', 'IWM', 'EFA', 'EEM', 'GLD', 'TLT', 'DIA', 'VGK', 'ACWI']

# ── Walk-forward ──
TRAIN_PCT      = 0.60
MIN_TRAIN_BARS = 756
MIN_TEST_BARS  = 252
EMA_LEN        = 10

# ── Parameter grid (walk-forward searched) ──
PARAM_GRID = {
    'n_levels':       [3, 5, 7],       # limit levels per side
    'level_step_bps': [5, 8, 12],      # bps between levels
    'order_size':     [0.02, 0.03, 0.05], # per-level order as fraction of capital
    'crisis_vol':     [0.18, 0.22],    # annualised vol for crisis detection
    'crisis_trim':    [0.10, 0.15],    # crisis avoidance trim
}

# ── Stats ──
N_BOOTSTRAP    = 5_000
BLOCK_SIZE     = 20
N_PERMUTATIONS = 5_000
SIGNIFICANCE   = 0.05

OUT_DIR = Path(__file__).resolve().parent.parent / 'results'


# ──────────────────────────────────────────────────────────────
# Regime detection (simple, robust)
# ──────────────────────────────────────────────────────────────
def classify_regime(returns, idx):
    """Classify regime from 20d rolling vol, momentum, and vol trend."""
    if idx < 40:
        return 'NORMAL'
    w = returns[max(0, idx-20):idx]
    vol = np.std(w, ddof=1) * np.sqrt(252)
    mom = np.sum(w)
    # Check if vol is declining from a recent spike (recovery signal)
    w_prev = returns[max(0, idx-40):max(0, idx-20)]
    vol_prev = np.std(w_prev, ddof=1) * np.sqrt(252) if len(w_prev) > 2 else vol
    if vol > 0.30 and mom < -0.08:
        return 'CRISIS'
    if vol > 0.22 and mom < 0:
        return 'CAUTIOUS'
    # Recovery: vol was high but now declining, positive momentum
    if vol_prev > 0.25 and vol < vol_prev * 0.85 and mom > 0:
        return 'RECOVERY'
    if vol < 0.12 and mom > 0.01:
        return 'BULL'
    return 'NORMAL'


# ──────────────────────────────────────────────────────────────
# Market-Maker Backtest
# ──────────────────────────────────────────────────────────────
def run_mm_backtest(opens, highs, lows, closes, params):
    """
    Market-maker on top of a base long position.

    Each bar:
      1. Base position = ~100% long (trimmed in crisis)
      2. Compute fair value = EMA(close)
      3. Place N limit-buy levels below fair and N limit-sell levels above
      4. Each level that falls within [Low, High] is FILLED
      5. Filled buys add to position, filled sells reduce → captures spread
      6. Net overlay should be ~zero over time (buys ≈ sells)
      7. Each fill earns maker rebate
      8. P&L = base_position * return + spread_capture + rebates
    """
    n_levels = params['n_levels']
    step_bps = params['level_step_bps']
    order_sz = params['order_size']
    crisis_vol = params['crisis_vol']
    crisis_trim = params['crisis_trim']

    N = len(closes)

    # EMA for fair value
    ema = np.empty(N)
    ema[0] = closes[0]
    a = 2.0 / (EMA_LEN + 1)
    for i in range(1, N):
        ema[i] = a * closes[i] + (1 - a) * ema[i-1]

    # Returns
    rets = np.zeros(N)
    rets[1:] = np.diff(closes) / closes[:-1]

    daily_pnl   = np.zeros(N)
    daily_bench  = np.zeros(N)
    total_fills  = 0
    total_rebate = 0.0
    total_spread = 0.0

    # Overlay inventory from market-making (can be + or -)
    overlay_inv = 0.0
    max_overlay = min(order_sz * n_levels * 2, 0.50)  # cap at 50%

    for i in range(1, N):
        # ── Regime ──
        regime = classify_regime(rets, i)

        # Base position: 100%+ long, trimmed in crisis, boosted in bull/recovery
        if regime == 'CRISIS':
            base = 1.0 - crisis_trim * 2
            spread_mult = 3.0    # very wide spreads → max profit per fill
            size_mult = 0.5
        elif regime == 'CAUTIOUS':
            base = 1.0 - crisis_trim
            spread_mult = 2.0
            size_mult = 0.7
        elif regime == 'RECOVERY':
            base = 1.03           # slight overweight on recovery
            spread_mult = 1.4    # capture recovery vol
            size_mult = 1.2
        elif regime == 'BULL':
            base = 1.02           # slight leverage in bull
            spread_mult = 0.7    # tight spreads → max fills
            size_mult = 1.2
        else:
            base = 1.0
            spread_mult = 1.0
            size_mult = 1.0

        # ── Fair value (previous bar, no lookahead) ──
        fair = ema[i-1]
        lo, hi = lows[i], highs[i]

        # ── Place multi-level limit orders ──
        bar_spread_pnl = 0.0
        bar_rebate = 0.0
        bar_fills = 0
        sz = order_sz * size_mult

        for lv in range(1, n_levels + 1):
            offset = fair * step_bps * lv * spread_mult / 10_000

            bid = fair - offset
            ask = fair + offset

            # Buy limit fills if Low <= bid
            if lo <= bid:
                # Spread capture net of adverse selection
                capture = sz * offset / fair * (1.0 - ADVERSE_SEL)
                bar_spread_pnl += capture
                bar_rebate += sz * MAKER_REBATE_BPS / 10_000
                bar_spread_pnl -= sz * SEC_FEE_BPS / 10_000
                overlay_inv += sz
                bar_fills += 1

            # Sell limit fills if High >= ask
            if hi >= ask:
                capture = sz * offset / fair * (1.0 - ADVERSE_SEL)
                bar_spread_pnl += capture
                bar_rebate += sz * MAKER_REBATE_BPS / 10_000
                bar_spread_pnl -= sz * SEC_FEE_BPS / 10_000
                overlay_inv -= sz
                bar_fills += 1

        # ── Inventory mean-reversion: gradually reduce overlay ──
        # Decay overlay toward zero (don't accumulate directional risk)
        overlay_inv *= 0.85  # 15% decay per bar — faster inventory reduction

        # Clamp overlay
        overlay_inv = np.clip(overlay_inv, -max_overlay, max_overlay)

        # ── P&L ──
        # Base position earns the market return
        base_pnl = base * rets[i]
        # Overlay earns/loses on its inventory
        overlay_pnl = overlay_inv * rets[i]
        # Total
        daily_pnl[i] = base_pnl + overlay_pnl + bar_spread_pnl + bar_rebate
        daily_bench[i] = rets[i]

        total_fills += bar_fills
        total_rebate += bar_rebate
        total_spread += bar_spread_pnl

    return daily_pnl, daily_bench, total_fills, total_rebate, total_spread


def compute_metrics(s, b, fills, rebate, spread_pnl):
    n = len(s)
    if n < 60:
        return None
    ann = 252
    ann_s = np.mean(s) * ann
    ann_b = np.mean(b) * ann
    vol_s = np.std(s, ddof=1) * np.sqrt(ann)
    vol_b = np.std(b, ddof=1) * np.sqrt(ann)
    sh_s = ann_s / vol_s if vol_s > 1e-10 else 0
    sh_b = ann_b / vol_b if vol_b > 1e-10 else 0

    cum_s = np.exp(np.cumsum(s))
    dd_s = (np.maximum.accumulate(cum_s) - cum_s) / np.maximum.accumulate(cum_s)
    cum_b = np.exp(np.cumsum(b))
    dd_b = (np.maximum.accumulate(cum_b) - cum_b) / np.maximum.accumulate(cum_b)

    down = s[s < 0]
    dv = np.std(down, ddof=1) * np.sqrt(ann) if len(down) > 2 else vol_s
    sortino = ann_s / dv if dv > 1e-10 else 0
    alpha = ann_s - ann_b
    tr = np.std(s - b, ddof=1) * np.sqrt(ann)
    ir = alpha / tr if tr > 1e-10 else 0
    calmar = ann_s / dd_s.max() if dd_s.max() > 1e-10 else 0
    calmar_b = ann_b / dd_b.max() if dd_b.max() > 1e-10 else 0

    return {
        'ann_ret': ann_s, 'bench_ret': ann_b, 'alpha': alpha,
        'sharpe': sh_s, 'sharpe_bench': sh_b,
        'sortino': sortino, 'calmar': calmar, 'calmar_bench': calmar_b,
        'max_dd': dd_s.max(), 'max_dd_bench': dd_b.max(),
        'info_ratio': ir,
        'total_ret': np.exp(np.sum(s)) - 1,
        'bench_total': np.exp(np.sum(b)) - 1,
        'n_bars': n, 'total_fills': fills,
        'fills_per_day': fills / n,
        'rebate_ann': rebate / n * ann,
        'spread_ann': spread_pnl / n * ann,
    }


# ──────────────────────────────────────────────────────────────
# Walk-forward
# ──────────────────────────────────────────────────────────────
def walk_forward(o, h, l, c):
    N = len(c)
    split = int(N * TRAIN_PCT)
    if split < MIN_TRAIN_BARS or (N - split) < MIN_TEST_BARS:
        return None

    keys = list(PARAM_GRID.keys())
    combos = list(product(*[PARAM_GRID[k] for k in keys]))

    best_score = -999
    best_params = dict(zip(keys, combos[0]))

    for combo in combos:
        params = dict(zip(keys, combo))
        s, b, fl, rb, sp = run_mm_backtest(o[:split], h[:split], l[:split], c[:split], params)
        m = compute_metrics(s, b, fl, rb, sp)
        if m:
            # Blend: Sharpe + Calmar + alpha (weighted toward alpha generation)
            score = m['sharpe'] * 0.3 + m['calmar'] * 0.3 + m['alpha'] * 8.0
            if score > best_score:
                best_score = score
                best_params = params

    # Out-of-sample test
    s, b, fl, rb, sp = run_mm_backtest(o[split:], h[split:], l[split:], c[split:], best_params)
    m = compute_metrics(s, b, fl, rb, sp)
    if m is None:
        return None
    m.update(best_params)
    m['train_score'] = best_score
    m['train_bars'] = split
    m['test_bars'] = N - split
    m['strat_returns'] = s
    m['bench_returns'] = b
    return m


# ──────────────────────────────────────────────────────────────
# Statistical tests
# ──────────────────────────────────────────────────────────────
def sharpe_ttest(r):
    n = len(r)
    if n < 30: return {'p_value': 1.0, 'significant': False}
    mu = np.mean(r)*252; sig = np.std(r,ddof=1)*np.sqrt(252)
    sr = mu/sig if sig>1e-10 else 0
    rho = np.corrcoef(r[:-1],r[1:])[0,1]
    eta = max(1+2*rho, 0.5)
    se = np.sqrt(eta/n)*np.sqrt(1+0.5*sr**2)
    t = sr/se if se>1e-10 else 0
    p = 1-sp_stats.norm.cdf(t)
    return {'p_value':p, 'significant':p<SIGNIFICANCE, 'sharpe':sr}

def block_bootstrap(r):
    n=len(r); rng=np.random.default_rng(42)
    nb=(n//BLOCK_SIZE)+1; off=np.arange(BLOCK_SIZE)
    shs=np.empty(N_BOOTSTRAP)
    for b in range(N_BOOTSTRAP):
        st=rng.integers(0,n,size=nb)
        ix=(st[:,None]+off[None,:]).ravel()%n
        s=r[ix[:n]]; mu=np.mean(s)*252; sig=np.std(s,ddof=1)*np.sqrt(252)
        shs[b]=mu/sig if sig>1e-10 else 0
    return {'ci_lo':np.percentile(shs,2.5),'ci_hi':np.percentile(shs,97.5),
            'p_value':np.mean(shs<=0),'significant':np.percentile(shs,2.5)>0}

def perm_test(s,b):
    obs=np.mean(s)-np.mean(b); rng=np.random.default_rng(42)
    d=s-b; signs=rng.choice([-1.,1.],size=(N_PERMUTATIONS,len(d)))
    pd_=(signs*d[None,:]).mean(axis=1); p=np.mean(pd_>=obs)
    return {'alpha_ann':obs*252,'p_value':p,'significant':p<SIGNIFICANCE}

def deflated_sr(sr,nt,no):
    if nt<=1: return {'dsr':1.0,'significant':True}
    em=np.sqrt(2*np.log(nt))*(1-0.5772/np.sqrt(2*np.log(nt)))
    se=np.sqrt((1+0.5*sr**2)/no)
    if se<1e-10: return {'dsr':0,'significant':False}
    z=(sr-em)/se; p=sp_stats.norm.cdf(z)
    return {'dsr':p,'significant':p>0.95}


# ──────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────
def main():
    print('='*85)
    print('  MARKET-MAKER REGIME BACKTEST (Base Long + Limit-Order Overlay)')
    print('  Limit orders only | Maker rebates | Multi-level OHLC fills')
    print('  Walk-forward: 60% train / 40% test')
    print('='*85)

    print(f'\nDownloading {len(ASSETS)} assets (OHLC) ...')
    data = {}
    for t in ASSETS:
        try:
            df = yf.download(t, period='max', interval='1d', progress=False)
            if len(df) < MIN_TRAIN_BARS+MIN_TEST_BARS:
                print(f'  {t}: SKIP ({len(df)} bars)'); continue
            o=df['Open'].values.flatten(); h=df['High'].values.flatten()
            l=df['Low'].values.flatten(); c=df['Close'].values.flatten()
            m=~(np.isnan(o)|np.isnan(h)|np.isnan(l)|np.isnan(c))
            data[t]=(o[m],h[m],l[m],c[m])
            print(f'  {t}: {m.sum():>6} bars ({m.sum()/252:.1f}y)')
        except Exception as e:
            print(f'  {t}: ERROR ({e})')

    if not data: sys.exit('No data.')

    nc = len(list(product(*PARAM_GRID.values())))
    print(f'\nGrid: {nc} combos | Maker rebate +{MAKER_REBATE_BPS} bps | Multi-level limits')

    results = []; t0 = time.time()

    for tk,(o,h,l,c) in data.items():
        print(f'\n  {tk} ...', end=' ', flush=True)
        wf = walk_forward(o,h,l,c)
        if wf is None: print('SKIP'); continue

        sr = wf.pop('strat_returns'); br = wf.pop('bench_returns')
        st1=sharpe_ttest(sr); st2=block_bootstrap(sr)
        st3=perm_test(sr,br); st4=deflated_sr(wf['sharpe'],nc,wf['test_bars'])

        row={'asset':tk,**wf}
        row['sr_pval']=st1['p_value']; row['sr_sig']=st1['significant']
        row['boot_ci_lo']=st2['ci_lo']; row['boot_ci_hi']=st2['ci_hi']
        row['boot_pval']=st2['p_value']; row['boot_sig']=st2['significant']
        row['perm_pval']=st3['p_value']; row['perm_sig']=st3['significant']
        row['dsr']=st4['dsr']; row['dsr_sig']=st4['significant']
        results.append(row)

        ns=sum([st1['significant'],st2['significant'],st3['significant'],st4['significant']])
        print(f'Alpha={wf["alpha"]*100:+.2f}%  Sharpe={wf["sharpe"]:.3f}  Calmar={wf["calmar"]:.3f}  '
              f'Fill/d={wf["fills_per_day"]:.1f}  Spread={wf["spread_ann"]*100:.2f}%  '
              f'DD={wf["max_dd"]*100:.1f}%  [{ns}/4 sig]')

    if not results: sys.exit('No results.')
    df = pd.DataFrame(results)

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    csv = OUT_DIR / 'rolling_backtest_results.csv'
    df.to_csv(csv, index=False, float_format='%.6f')

    print('\n'+'='*105)
    print('  OUT-OF-SAMPLE RESULTS — Market-Maker (Base Long + Overlay)')
    print('='*105)

    print(f'\n{"Asset":<6} {"Alpha":>7} {"Sharpe":>7} {"S.Bn":>6} '
          f'{"Sortino":>8} {"Calmar":>7} {"C.Bn":>6} '
          f'{"MaxDD":>6} {"DD.Bn":>6} '
          f'{"Fil/d":>5} {"Spread":>7} {"Rebate":>7} '
          f'{"SR-p":>6} {"Bt-p":>6} {"Pm-p":>6} {"DSR":>5} '
          f'{"lvl":>4} {"stp":>4} {"sz":>5} {"crV":>5} {"trm":>5}')
    print('-'*140)

    pa=0; sc=0
    for _,r in df.sort_values('alpha',ascending=False).iterrows():
        ns=sum([r['sr_sig'],r['boot_sig'],r['perm_sig'],r['dsr_sig']])
        mk='**' if ns>=3 else ('*' if ns>=2 else '')
        if r['alpha']>0: pa+=1
        if ns>=2: sc+=1
        print(f'{r["asset"]:<6} {r["alpha"]*100:>+6.2f}% {r["sharpe"]:>7.3f} '
              f'{r["sharpe_bench"]:>6.3f} {r["sortino"]:>8.3f} '
              f'{r["calmar"]:>7.3f} {r["calmar_bench"]:>6.3f} '
              f'{r["max_dd"]*100:>5.1f}% {r["max_dd_bench"]*100:>5.1f}% '
              f'{r["fills_per_day"]:>5.1f} {r["spread_ann"]*100:>6.2f}% '
              f'{r["rebate_ann"]*100:>6.3f}% '
              f'{r["sr_pval"]:>6.3f} {r["boot_pval"]:>6.3f} {r["perm_pval"]:>6.3f} '
              f'{r["dsr"]:>5.2f} '
              f'{r["n_levels"]:>4} {r["level_step_bps"]:>4} '
              f'{r["order_size"]:>5.02f} {r["crisis_vol"]:>5.2f} '
              f'{r["crisis_trim"]:>5.2f} {mk}')

    print(f'\n── Summary ──')
    print(f'  Positive alpha:  {pa}/{len(df)}')
    print(f'  Significant:     {sc}/{len(df)}  (>=2 of 4 tests)')
    print(f'  Mean alpha:      {df["alpha"].mean()*100:+.2f}%')
    print(f'  Mean Sharpe:     {df["sharpe"].mean():.3f}  (bench: {df["sharpe_bench"].mean():.3f})')
    print(f'  Mean Sortino:    {df["sortino"].mean():.3f}')
    print(f'  Mean Calmar:     {df["calmar"].mean():.3f}  (bench: {df["calmar_bench"].mean():.3f})')
    print(f'  Mean MaxDD:      {df["max_dd"].mean()*100:.1f}%  (bench: {df["max_dd_bench"].mean()*100:.1f}%)')
    print(f'  Mean fills/day:  {df["fills_per_day"].mean():.1f}')
    print(f'  Mean spread/yr:  {df["spread_ann"].mean()*100:.2f}%')
    print(f'  Mean rebate/yr:  {df["rebate_ann"].mean()*100:.3f}%')
    dd = df['max_dd_bench'].mean()-df['max_dd'].mean()
    print(f'  DD improvement:  {dd*100:+.1f}%')
    print(f'\n  Time: {time.time()-t0:.1f}s | CSV: {csv}')


if __name__=='__main__':
    main()
