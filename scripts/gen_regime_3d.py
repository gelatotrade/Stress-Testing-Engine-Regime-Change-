#!/usr/bin/env python3
"""
Generate the main 3D P&L surface regime cycle animation.

Simulates a LIVE data feed scenario: the engine connects to market data,
detects regime changes in real-time via HMM, and morphs the 3D P&L surface
as live regime transitions occur. The surface, signals, and execution
indicators update tick-by-tick as if running in --live mode.
"""
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
from PIL import Image
import io
import os
from datetime import datetime, timedelta

OUT = os.path.join(os.path.dirname(__file__), '..', 'docs', 'img', 'regime_cycle_3d.gif')

# --- Black-Scholes P&L surface computation ---
def norm_cdf(x):
    import math; return 0.5 * (1.0 + np.vectorize(math.erf)(np.asarray(x, dtype=float) / np.sqrt(2)))

def bs_call(S, K, T, r, sigma):
    T = np.maximum(T, 1e-6)
    d1 = (np.log(S/K) + (r + 0.5*sigma**2)*T) / (sigma*np.sqrt(T))
    d2 = d1 - sigma*np.sqrt(T)
    return S * norm_cdf(d1) - K * np.exp(-r*T) * norm_cdf(d2)

def bs_put(S, K, T, r, sigma):
    T = np.maximum(T, 1e-6)
    d1 = (np.log(S/K) + (r + 0.5*sigma**2)*T) / (sigma*np.sqrt(T))
    d2 = d1 - sigma*np.sqrt(T)
    return K * np.exp(-r*T) * norm_cdf(-d2) - S * norm_cdf(-d1)

def iron_condor_pnl(S, vol, base_spot, T, r):
    """Iron Condor: short put spread + short call spread around base_spot."""
    K_pl = base_spot * 0.90
    K_ps = base_spot * 0.95
    K_cs = base_spot * 1.05
    K_cl = base_spot * 1.10
    T0 = 30/365.0
    base_vol = 0.15
    prem = (-bs_put(base_spot, K_ps, T0, r, base_vol) + bs_put(base_spot, K_pl, T0, r, base_vol)
            -bs_call(base_spot, K_cs, T0, r, base_vol) + bs_call(base_spot, K_cl, T0, r, base_vol))
    curr = (-bs_put(S, K_ps, T, r, vol) + bs_put(S, K_pl, T, r, vol)
            -bs_call(S, K_cs, T, r, vol) + bs_call(S, K_cl, T, r, vol))
    return (prem - curr) * 100

# Regime configurations -- modeled on live market data feed
REGIMES = [
    {"name": "BULL QUIET",      "signal": "STRONG BUY",  "vix": 12.3, "color": "#00ff88",
     "base": 5280, "vol_center": 0.13, "spot_drift": 0.0, "surface_scale": 1.0,
     "cash": 15, "equity": 60, "options": 25, "warning": 12, "crisis": 3,
     "exec": "BUY 892 SPY @ $528.04  FILLED", "account": "$1,000,000", "positions": 1},
    {"name": "TRANSITION",      "signal": "REDUCE RISK", "vix": 24.5, "color": "#ffaa00",
     "base": 5050, "vol_center": 0.22, "spot_drift": -0.02, "surface_scale": 1.3,
     "cash": 40, "equity": 40, "options": 20, "warning": 58, "crisis": 25,
     "exec": "SELL 446 SPY @ $505.12  FILLED", "account": "$987,420", "positions": 1},
    {"name": "BEAR VOLATILE",   "signal": "CRISIS",      "vix": 67.2, "color": "#ff3344",
     "base": 3900, "vol_center": 0.50, "spot_drift": -0.08, "surface_scale": 2.5,
     "cash": 70, "equity": 20, "options": 10, "warning": 95, "crisis": 89,
     "exec": "SELL 357 SPY @ $391.88  FILLED", "account": "$932,180", "positions": 1},
    {"name": "RECOVERY",        "signal": "BUY",         "vix": 28.4, "color": "#00aaff",
     "base": 4500, "vol_center": 0.28, "spot_drift": 0.03, "surface_scale": 1.5,
     "cash": 25, "equity": 50, "options": 25, "warning": 22, "crisis": 11,
     "exec": "BUY 663 SPY @ $450.22  FILLED", "account": "$961,540", "positions": 1},
    {"name": "NEW BULL",        "signal": "STRONG BUY",  "vix": 13.8, "color": "#00ff88",
     "base": 5600, "vol_center": 0.14, "spot_drift": 0.01, "surface_scale": 1.0,
     "cash": 15, "equity": 60, "options": 25, "warning": 8, "crisis": 2,
     "exec": "BUY 224 SPY @ $560.15  FILLED", "account": "$1,148,920", "positions": 1},
]

FRAMES_PER_REGIME = 12
TOTAL_FRAMES = FRAMES_PER_REGIME * len(REGIMES)

frames = []
for frame_idx in range(TOTAL_FRAMES):
    regime_idx = frame_idx // FRAMES_PER_REGIME
    sub_frame = frame_idx % FRAMES_PER_REGIME
    t = sub_frame / FRAMES_PER_REGIME

    curr = REGIMES[regime_idx]
    nxt = REGIMES[min(regime_idx + 1, len(REGIMES) - 1)]

    base = curr["base"] * (1 - t) + nxt["base"] * t if regime_idx < len(REGIMES) - 1 else curr["base"]
    vc = curr["vol_center"] * (1 - t) + nxt["vol_center"] * t if regime_idx < len(REGIMES) - 1 else curr["vol_center"]
    sc = curr["surface_scale"] * (1 - t) + nxt["surface_scale"] * t if regime_idx < len(REGIMES) - 1 else curr["surface_scale"]
    vix = curr["vix"] * (1 - t) + nxt["vix"] * t if regime_idx < len(REGIMES) - 1 else curr["vix"]
    warn = curr["warning"] * (1 - t) + nxt["warning"] * t if regime_idx < len(REGIMES) - 1 else curr["warning"]
    crisis = curr["crisis"] * (1 - t) + nxt["crisis"] * t if regime_idx < len(REGIMES) - 1 else curr["crisis"]

    # Build surface grid
    spot_range = np.linspace(base * 0.85, base * 1.15, 40)
    vol_range = np.linspace(max(0.05, vc - 0.12), vc + 0.15, 40)
    S_grid, V_grid = np.meshgrid(spot_range, vol_range)

    T_opt = 20.0 / 365.0
    r = 0.05
    Z = iron_condor_pnl(S_grid, V_grid, base, T_opt, r) / sc

    if regime_idx in [1, 2]:
        noise = np.sin(S_grid / 50 + frame_idx * 0.3) * np.cos(V_grid * 20 + frame_idx * 0.2)
        Z += noise * sc * 3

    fig = plt.figure(figsize=(8, 6), dpi=100, facecolor='#0d0d0d')
    ax = fig.add_subplot(111, projection='3d', facecolor='#0d0d0d')

    z_norm = (Z - Z.min()) / (Z.max() - Z.min() + 1e-10)
    if regime_idx == 0 or regime_idx == 4:
        colors = plt.cm.Greens(z_norm * 0.7 + 0.3)
    elif regime_idx == 1:
        colors = plt.cm.YlOrRd(1.0 - z_norm * 0.6)
    elif regime_idx == 2:
        colors = plt.cm.hot(1.0 - z_norm * 0.7)
    else:
        colors = plt.cm.cool(z_norm * 0.7 + 0.2)

    ax.plot_surface(S_grid, V_grid * 100, Z, facecolors=colors, alpha=0.88,
                    edgecolor='none', antialiased=True, shade=True)

    angle = 220 + frame_idx * 2.5
    ax.view_init(elev=28, azim=angle)

    ax.set_xlabel('Spot Price ($)', color='#888888', fontsize=8, labelpad=8)
    ax.set_ylabel('Implied Vol (%)', color='#888888', fontsize=8, labelpad=8)
    ax.set_zlabel('P&L ($)', color='#888888', fontsize=8, labelpad=8)

    ax.tick_params(colors='#555555', labelsize=6)
    ax.xaxis.pane.fill = False
    ax.yaxis.pane.fill = False
    ax.zaxis.pane.fill = False
    ax.xaxis.pane.set_edgecolor('#222222')
    ax.yaxis.pane.set_edgecolor('#222222')
    ax.zaxis.pane.set_edgecolor('#222222')
    ax.grid(True, alpha=0.15)

    # Title with LIVE indicator and regime info
    title_color = curr["color"]
    fig.suptitle(f'\u25cf LIVE  |  Regime: {curr["name"]}  |  Signal: {curr["signal"]}  |  VIX: {vix:.1f}',
                 color=title_color, fontsize=11, fontweight='bold', y=0.97)

    # Info bar: execution engine status + allocations
    fig.text(0.02, 0.02,
             f'[Execution] {curr["exec"]}   |   '
             f'Cash: {curr["cash"]}%  Equity: {curr["equity"]}%  Options: {curr["options"]}%   '
             f'Warning: {warn:.0f}%  Crisis: {crisis:.0f}%',
             color='#888888', fontsize=7, fontfamily='monospace')

    # Regime timeline bar
    bar_y = 0.06
    fig.patches.append(plt.Rectangle((0.05, bar_y), 0.9, 0.012, transform=fig.transFigure,
                                      facecolor='#1a1a1a', edgecolor='#333333', linewidth=0.5))
    regime_colors_bar = ['#00ff88', '#ffaa00', '#ff3344', '#00aaff', '#00ff88']
    for ri in range(5):
        x0 = 0.05 + ri * 0.18
        w = 0.18
        alpha = 1.0 if ri <= regime_idx else 0.2
        if ri == regime_idx:
            w = 0.18 * t
        fig.patches.append(plt.Rectangle((x0, bar_y), w, 0.012, transform=fig.transFigure,
                                          facecolor=regime_colors_bar[ri], alpha=alpha))

    plt.tight_layout(rect=[0, 0.08, 1, 0.94])

    buf = io.BytesIO()
    fig.savefig(buf, format='png', facecolor='#0d0d0d', bbox_inches='tight', pad_inches=0.2)
    plt.close(fig)
    buf.seek(0)
    img = Image.open(buf).convert('RGB')
    frames.append(img)
    print(f'  Frame {frame_idx+1}/{TOTAL_FRAMES}')

print(f'Saving GIF to {OUT}...')
frames[0].save(OUT, save_all=True, append_images=frames[1:], duration=180, loop=0, optimize=True)
print(f'Done! {os.path.getsize(OUT)} bytes')
