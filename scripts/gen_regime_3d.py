#!/usr/bin/env python3
"""
Generate high-quality 3D P&L surface regime cycle animation.

Improvements over previous version:
- 150 DPI, 60x60 grid (was 100 DPI, 40x40)
- 18 frames per regime (was 12) for smoother transitions
- Wireframe overlay for depth perception
- Contour projections on floor
- Stronger Z-range contrast between regimes
- Dramatic surface morphing during transitions
- Neon glow color palette with higher saturation
- Camera elevation shift per regime
"""
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap, Normalize
from mpl_toolkits.mplot3d import Axes3D
from PIL import Image, ImageFilter
import io
import os

OUT = os.path.join(os.path.dirname(__file__), '..', 'docs', 'img', 'regime_cycle_3d.gif')

# --- Black-Scholes ---
def norm_cdf(x):
    import math
    return 0.5 * (1.0 + np.vectorize(math.erf)(np.asarray(x, dtype=float) / np.sqrt(2)))

def bs_call(S, K, T, r, sigma):
    T = np.maximum(T, 1e-6)
    d1 = (np.log(S / K) + (r + 0.5 * sigma**2) * T) / (sigma * np.sqrt(T))
    d2 = d1 - sigma * np.sqrt(T)
    return S * norm_cdf(d1) - K * np.exp(-r * T) * norm_cdf(d2)

def bs_put(S, K, T, r, sigma):
    T = np.maximum(T, 1e-6)
    d1 = (np.log(S / K) + (r + 0.5 * sigma**2) * T) / (sigma * np.sqrt(T))
    d2 = d1 - sigma * np.sqrt(T)
    return K * np.exp(-r * T) * norm_cdf(-d2) - S * norm_cdf(-d1)

def iron_condor_pnl(S, vol, base_spot, T, r):
    K_pl = base_spot * 0.90
    K_ps = base_spot * 0.95
    K_cs = base_spot * 1.05
    K_cl = base_spot * 1.10
    T0 = 30 / 365.0
    base_vol = 0.15
    prem = (-bs_put(base_spot, K_ps, T0, r, base_vol) + bs_put(base_spot, K_pl, T0, r, base_vol)
            - bs_call(base_spot, K_cs, T0, r, base_vol) + bs_call(base_spot, K_cl, T0, r, base_vol))
    curr = (-bs_put(S, K_ps, T, r, vol) + bs_put(S, K_pl, T, r, vol)
            - bs_call(S, K_cs, T, r, vol) + bs_call(S, K_cl, T, r, vol))
    return (prem - curr) * 100

# --- Custom high-contrast colormaps per regime ---
CMAP_BULL = LinearSegmentedColormap.from_list('bull', [
    '#003310', '#005522', '#00aa44', '#00ff88', '#88ffbb', '#ccffee'])
CMAP_TRANSITION = LinearSegmentedColormap.from_list('trans', [
    '#331100', '#884400', '#cc7700', '#ffaa00', '#ffcc44', '#ffee88'])
CMAP_CRISIS = LinearSegmentedColormap.from_list('crisis', [
    '#110000', '#440000', '#990000', '#ff0022', '#ff4444', '#ff8866'])
CMAP_RECOVERY = LinearSegmentedColormap.from_list('recov', [
    '#001133', '#003366', '#0066aa', '#0099ee', '#44bbff', '#88ddff'])

# --- Regime configurations ---
REGIMES = [
    {"name": "BULL QUIET",    "signal": "STRONG BUY", "vix": 12.3, "color": "#00ff88",
     "base": 5280, "vc": 0.13, "scale": 1.0, "elev": 30,
     "cash": 15, "equity": 60, "options": 25, "warning": 12, "crisis": 3,
     "exec": "BUY 892 SPY @ $528.04", "cmap": CMAP_BULL, "z_boost": 1.5},
    {"name": "TRANSITION",    "signal": "REDUCE RISK", "vix": 24.5, "color": "#ffaa00",
     "base": 5050, "vc": 0.22, "scale": 1.3, "elev": 26,
     "cash": 40, "equity": 40, "options": 20, "warning": 58, "crisis": 25,
     "exec": "SELL 446 SPY @ $505.12", "cmap": CMAP_TRANSITION, "z_boost": 1.0},
    {"name": "BEAR VOLATILE", "signal": "CRISIS",     "vix": 67.2, "color": "#ff3344",
     "base": 3900, "vc": 0.50, "scale": 2.8, "elev": 22,
     "cash": 70, "equity": 20, "options": 10, "warning": 95, "crisis": 89,
     "exec": "SELL 357 SPY @ $391.88", "cmap": CMAP_CRISIS, "z_boost": 2.0},
    {"name": "RECOVERY",      "signal": "BUY",        "vix": 28.4, "color": "#00aaff",
     "base": 4500, "vc": 0.28, "scale": 1.5, "elev": 28,
     "cash": 25, "equity": 50, "options": 25, "warning": 22, "crisis": 11,
     "exec": "BUY 663 SPY @ $450.22", "cmap": CMAP_RECOVERY, "z_boost": 1.2},
    {"name": "NEW BULL",      "signal": "STRONG BUY", "vix": 13.8, "color": "#00ff88",
     "base": 5600, "vc": 0.14, "scale": 1.0, "elev": 30,
     "cash": 15, "equity": 60, "options": 25, "warning": 8, "crisis": 2,
     "exec": "BUY 224 SPY @ $560.15", "cmap": CMAP_BULL, "z_boost": 1.5},
]

GRID_N = 60          # resolution (was 40)
FPR = 18             # frames per regime (was 12)
TOTAL = FPR * len(REGIMES)
FIG_DPI = 150        # (was 100)
FIG_W, FIG_H = 10, 7.5  # (was 8, 6)

def smooth_interp(a, b, t):
    """Smooth-step interpolation (ease in/out)."""
    t = max(0, min(1, t))
    s = t * t * (3 - 2 * t)  # smoothstep
    return a * (1 - s) + b * s

frames = []
for fi in range(TOTAL):
    ri = fi // FPR
    sf = fi % FPR
    t = sf / FPR

    curr = REGIMES[ri]
    nxt = REGIMES[min(ri + 1, len(REGIMES) - 1)]

    # Smooth interpolation of all parameters
    if ri < len(REGIMES) - 1:
        base = smooth_interp(curr["base"], nxt["base"], t)
        vc   = smooth_interp(curr["vc"],   nxt["vc"],   t)
        sc   = smooth_interp(curr["scale"],nxt["scale"], t)
        vix  = smooth_interp(curr["vix"],  nxt["vix"],  t)
        warn = smooth_interp(curr["warning"], nxt["warning"], t)
        crisis = smooth_interp(curr["crisis"], nxt["crisis"], t)
        elev = smooth_interp(curr["elev"], nxt["elev"], t)
        z_boost = smooth_interp(curr["z_boost"], nxt["z_boost"], t)
    else:
        base, vc, sc = curr["base"], curr["vc"], curr["scale"]
        vix, warn, crisis = curr["vix"], curr["warning"], curr["crisis"]
        elev, z_boost = curr["elev"], curr["z_boost"]

    # High-resolution grid
    spot_range = np.linspace(base * 0.83, base * 1.17, GRID_N)
    vol_range = np.linspace(max(0.04, vc - 0.14), vc + 0.18, GRID_N)
    S, V = np.meshgrid(spot_range, vol_range)

    Z = iron_condor_pnl(S, V, base, 20.0 / 365.0, 0.05) / sc * z_boost

    # Regime-specific surface distortions
    if ri == 1 or (ri == 0 and t > 0.5):
        # Transition: growing ripples
        ripple_strength = t * 5.0 if ri == 1 else (t - 0.5) * 3.0
        ripple = np.sin(S / 40 + fi * 0.4) * np.cos(V * 25 + fi * 0.3)
        Z += ripple * ripple_strength

    elif ri == 2:
        # Crisis: violent turbulence + deep crater
        turb = (np.sin(S / 30 + fi * 0.5) * np.cos(V * 30 + fi * 0.35)
                + 0.5 * np.sin(S / 15 + fi * 0.7) * np.cos(V * 15 + fi * 0.5))
        Z += turb * sc * 2.5
        # Deepen crater in center
        cx, cy = base, vc
        dist = ((S - cx) / (base * 0.15))**2 + ((V - cy) / 0.15)**2
        Z -= 8.0 * np.exp(-dist * 0.5) * z_boost

    elif ri == 3 and t < 0.5:
        # Recovery early: residual turbulence fading out
        fade = 1.0 - t * 2.0
        turb = np.sin(S / 35 + fi * 0.3) * np.cos(V * 20 + fi * 0.2)
        Z += turb * fade * 2.0

    # --- Rendering ---
    fig = plt.figure(figsize=(FIG_W, FIG_H), dpi=FIG_DPI, facecolor='#080810')
    ax = fig.add_subplot(111, projection='3d', facecolor='#080810')

    # Normalize colors
    z_min, z_max = Z.min(), Z.max()
    z_range = z_max - z_min if z_max > z_min else 1.0
    z_norm = (Z - z_min) / z_range

    # Use regime-specific colormap
    cmap = curr["cmap"]
    face_colors = cmap(z_norm)

    # Main surface
    ax.plot_surface(S, V * 100, Z, facecolors=face_colors, alpha=0.92,
                    edgecolor='none', antialiased=True, shade=True,
                    rstride=1, cstride=1)

    # Wireframe overlay for depth
    ax.plot_wireframe(S[::4, ::4], V[::4, ::4] * 100, Z[::4, ::4],
                      color='white', alpha=0.06, linewidth=0.3)

    # Contour projection on floor (Z-plane)
    z_floor = z_min - (z_range * 0.15)
    try:
        contour_levels = np.linspace(z_min, z_max, 8)
        ax.contour(S, V * 100, Z, levels=contour_levels, zdir='z', offset=z_floor,
                   cmap=cmap, alpha=0.35, linewidths=0.6)
    except Exception:
        pass

    ax.set_zlim(z_floor, z_max + z_range * 0.1)

    # Camera
    azim = 215 + fi * 2.0
    ax.view_init(elev=elev, azim=azim)

    # Axis styling
    ax.set_xlabel('Spot Price ($)', color='#667788', fontsize=9, labelpad=10)
    ax.set_ylabel('Implied Vol (%)', color='#667788', fontsize=9, labelpad=10)
    ax.set_zlabel('P&L ($)', color='#667788', fontsize=9, labelpad=10)
    ax.tick_params(colors='#445566', labelsize=7)
    ax.xaxis.pane.fill = False
    ax.yaxis.pane.fill = False
    ax.zaxis.pane.fill = False
    ax.xaxis.pane.set_edgecolor('#1a1a2e')
    ax.yaxis.pane.set_edgecolor('#1a1a2e')
    ax.zaxis.pane.set_edgecolor('#1a1a2e')
    ax.grid(True, alpha=0.12, color='#334455')

    # --- Title ---
    tc = curr["color"]
    fig.suptitle(
        f'\u25cf LIVE  |  Regime: {curr["name"]}  |  Signal: {curr["signal"]}  |  VIX: {vix:.1f}',
        color=tc, fontsize=14, fontweight='bold', y=0.96, fontfamily='monospace')

    # --- Info bar ---
    fig.text(0.03, 0.025,
             f'[Execution] {curr["exec"]}   |   '
             f'Cash: {curr["cash"]}%  Equity: {curr["equity"]}%  Options: {curr["options"]}%   |   '
             f'Warning: {warn:.0f}%  Crisis Prob: {crisis:.0f}%',
             color='#778899', fontsize=8, fontfamily='monospace')

    # --- Regime timeline bar ---
    bar_y = 0.065
    bar_h = 0.018
    fig.patches.append(plt.Rectangle((0.04, bar_y), 0.92, bar_h,
                                      transform=fig.transFigure,
                                      facecolor='#12121e', edgecolor='#2a2a40',
                                      linewidth=0.8, zorder=1))
    regime_colors_bar = ['#00ff88', '#ffaa00', '#ff3344', '#00aaff', '#00ff88']
    regime_labels = ['Bull', 'Trans', 'Crisis', 'Recov', 'Bull+']
    seg_w = 0.92 / 5
    for k in range(5):
        x0 = 0.04 + k * seg_w
        w = seg_w
        alpha_val = 0.95 if k <= ri else 0.15
        if k == ri:
            w = seg_w * t
            alpha_val = 0.95
        fig.patches.append(plt.Rectangle((x0, bar_y), w, bar_h,
                                          transform=fig.transFigure,
                                          facecolor=regime_colors_bar[k],
                                          alpha=alpha_val, zorder=2))
        # Label
        lbl_alpha = 1.0 if k == ri else 0.4
        fig.text(x0 + seg_w / 2, bar_y + bar_h + 0.006, regime_labels[k],
                 color=regime_colors_bar[k], fontsize=7, ha='center',
                 fontweight='bold' if k == ri else 'normal',
                 alpha=lbl_alpha, fontfamily='monospace')

    # Active marker
    marker_x = 0.04 + (ri + t) * seg_w
    fig.text(marker_x, bar_y - 0.012, '\u25b2', color='white', fontsize=8,
             ha='center', fontfamily='monospace')

    plt.tight_layout(rect=[0, 0.10, 1, 0.93])

    buf = io.BytesIO()
    fig.savefig(buf, format='png', facecolor='#080810', bbox_inches='tight', pad_inches=0.15)
    plt.close(fig)
    buf.seek(0)
    img = Image.open(buf).convert('RGB')
    frames.append(img)
    print(f'  Frame {fi + 1}/{TOTAL}')

print(f'Saving GIF to {OUT}...')
frames[0].save(OUT, save_all=True, append_images=frames[1:],
               duration=160, loop=0, optimize=True)
print(f'Done! {os.path.getsize(OUT) / 1024:.0f} KB')
