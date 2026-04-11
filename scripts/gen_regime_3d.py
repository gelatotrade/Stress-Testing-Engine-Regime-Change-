#!/usr/bin/env python3
"""
Generate high-quality 3D P&L surface regime cycle animation.

Strategy: ~100% base long + multi-level limit-order overlay.
Regimes: BULL, NORMAL, CAUTIOUS, CRISIS, RECOVERY.

Features:
- 150 DPI, 60x60 grid
- 18 frames per regime for smooth transitions
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
from PIL import Image
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

# --- Custom diverging colormaps per regime ---
CMAP_BULL = LinearSegmentedColormap.from_list('bull', [
    '#0000aa', '#0055cc', '#22aa66', '#00ff88', '#eeffaa', '#ffffdd'])
CMAP_NORMAL = LinearSegmentedColormap.from_list('normal', [
    '#001144', '#003388', '#2266aa', '#44aacc', '#88ddee', '#ccffff'])
CMAP_CAUTIOUS = LinearSegmentedColormap.from_list('cautious', [
    '#440066', '#8833aa', '#cc7700', '#ffaa00', '#ffdd44', '#ffffcc'])
CMAP_CRISIS = LinearSegmentedColormap.from_list('crisis', [
    '#000044', '#220066', '#660088', '#cc0022', '#ff4444', '#ffcc44'])
CMAP_RECOVERY = LinearSegmentedColormap.from_list('recov', [
    '#330044', '#553388', '#0066aa', '#00ccee', '#66ffcc', '#eeffee'])

# --- Regime configurations ---
REGIMES = [
    {"name": "BULL",     "spread": "0.7x", "base": 5280, "base_pct": "102%", "fills": 57,
     "color": "#00ff88", "vc": 0.13, "scale": 1.0, "elev": 30,
     "cmap": CMAP_BULL, "z_boost": 1.5},
    {"name": "NORMAL",   "spread": "1.0x", "base": 5150, "base_pct": "100%", "fills": 40,
     "color": "#00aaff", "vc": 0.16, "scale": 1.1, "elev": 30,
     "cmap": CMAP_NORMAL, "z_boost": 1.3},
    {"name": "CAUTIOUS", "spread": "2.0x", "base": 5050, "base_pct": "90%", "fills": 20,
     "color": "#ffaa00", "vc": 0.22, "scale": 1.3, "elev": 26,
     "cmap": CMAP_CAUTIOUS, "z_boost": 1.0},
    {"name": "CRISIS",   "spread": "3.0x", "base": 3900, "base_pct": "70%", "fills": 13,
     "color": "#ff3344", "vc": 0.50, "scale": 2.8, "elev": 22,
     "cmap": CMAP_CRISIS, "z_boost": 2.0},
    {"name": "RECOVERY", "spread": "1.4x", "base": 4500, "base_pct": "103%", "fills": 29,
     "color": "#00ffcc", "vc": 0.28, "scale": 1.5, "elev": 28,
     "cmap": CMAP_RECOVERY, "z_boost": 1.2},
]

GRID_N = 60
TOTAL = 150
FIG_DPI = 150
FIG_W, FIG_H = 10, 7.5

def smooth_interp(a, b, t):
    """Smooth-step interpolation (ease in/out)."""
    t = max(0, min(1, t))
    s = t * t * (3 - 2 * t)
    return a * (1 - s) + b * s

def blend_cmap(cmap_a, cmap_b, blend_t, z_norm):
    """Blend two colormaps pixel-by-pixel."""
    ca = cmap_a(z_norm)
    cb = cmap_b(z_norm)
    return ca * (1 - blend_t) + cb * blend_t

N_REG = len(REGIMES)

def get_regime_surface(ri, S, V, fi):
    """Compute Z for a specific regime with its distortions."""
    r = REGIMES[ri]
    Z = iron_condor_pnl(S, V, r["base"], 20.0 / 365.0, 0.05) / r["scale"] * r["z_boost"]

    if ri == 2:  # Cautious: ripples
        ripple = np.sin(S / 40 + fi * 0.35) * np.cos(V * 25 + fi * 0.25)
        Z += ripple * 5.0
    elif ri == 3:  # Crisis: turbulence + crater
        turb = (np.sin(S / 30 + fi * 0.4) * np.cos(V * 30 + fi * 0.3)
                + 0.5 * np.sin(S / 15 + fi * 0.6) * np.cos(V * 15 + fi * 0.45))
        Z += turb * r["scale"] * 2.5
        cx, cy = r["base"], r["vc"]
        dist = ((S - cx) / (r["base"] * 0.15))**2 + ((V - cy) / 0.15)**2
        Z -= 8.0 * np.exp(-dist * 0.5) * r["z_boost"]
    elif ri == 4:  # Recovery: mild turbulence
        turb = np.sin(S / 35 + fi * 0.25) * np.cos(V * 20 + fi * 0.15)
        Z += turb * 1.0

    return Z

frames = []
for fi in range(TOTAL):
    t = fi / (TOTAL - 1) * (N_REG - 1)
    ri_a = min(int(t), N_REG - 2)
    ri_b = ri_a + 1
    local_t = t - ri_a
    bt = smooth_interp(0, 1, local_t)

    curr = REGIMES[ri_a]
    nxt = REGIMES[ri_b]

    # Interpolate all parameters continuously
    base = smooth_interp(curr["base"], nxt["base"], bt)
    vc   = smooth_interp(curr["vc"],   nxt["vc"],   bt)
    elev = smooth_interp(curr["elev"], nxt["elev"], bt)

    # Common grid centered on interpolated base/vc
    spot_range = np.linspace(base * 0.83, base * 1.17, GRID_N)
    vol_range = np.linspace(max(0.04, vc - 0.14), vc + 0.18, GRID_N)
    S, V = np.meshgrid(spot_range, vol_range)

    Z_a = get_regime_surface(ri_a, S, V, fi)
    Z_b = get_regime_surface(ri_b, S, V, fi)
    Z = Z_a * (1 - bt) + Z_b * bt

    if bt < 0.15:
        tc = curr["color"]
        title_name = curr["name"]
        info_label = f'Spread: {curr["spread"]} | Base: {curr["base_pct"]} | Fills: {curr["fills"]}/bar'
    elif bt > 0.85:
        tc = nxt["color"]
        title_name = nxt["name"]
        info_label = f'Spread: {nxt["spread"]} | Base: {nxt["base_pct"]} | Fills: {nxt["fills"]}/bar'
    else:
        tc = nxt["color"]
        title_name = f'{curr["name"]} \u2192 {nxt["name"]}'
        info_label = f'TRANSITIONING ({bt*100:.0f}%) | Regime shift detected'

    # --- Rendering ---
    fig = plt.figure(figsize=(FIG_W, FIG_H), dpi=FIG_DPI, facecolor='#080810')
    ax = fig.add_subplot(111, projection='3d', facecolor='#080810')

    z_min, z_max = Z.min(), Z.max()
    z_range = z_max - z_min if z_max > z_min else 1.0
    z_norm = (Z - z_min) / z_range

    face_colors = blend_cmap(curr["cmap"], nxt["cmap"], bt, z_norm)

    ax.plot_surface(S, V * 100, Z, facecolors=face_colors, alpha=0.92,
                    edgecolor='none', antialiased=True, shade=True,
                    rstride=1, cstride=1)

    ax.plot_wireframe(S[::4, ::4], V[::4, ::4] * 100, Z[::4, ::4],
                      color='white', alpha=0.06, linewidth=0.3)

    z_floor = z_min - (z_range * 0.15)
    try:
        contour_levels = np.linspace(z_min, z_max, 8)
        ax.contour(S, V * 100, Z, levels=contour_levels, zdir='z', offset=z_floor,
                   cmap=curr["cmap"], alpha=0.35, linewidths=0.6)
    except Exception:
        pass

    ax.set_zlim(z_floor, z_max + z_range * 0.1)

    azim = 215 + fi * 1.5
    ax.view_init(elev=elev, azim=azim)

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
    fig.suptitle(
        f'\u25cf LIVE  |  Regime: {title_name}  |  {info_label}',
        color=tc, fontsize=14, fontweight='bold', y=0.96, fontfamily='monospace')

    # --- Info bar ---
    spread_now = smooth_interp(float(curr["spread"].rstrip('x')), float(nxt["spread"].rstrip('x')), bt)
    base_pct_now = smooth_interp(float(curr["base_pct"].rstrip('%')), float(nxt["base_pct"].rstrip('%')), bt)
    fills_now = smooth_interp(curr["fills"], nxt["fills"], bt)
    fig.text(0.03, 0.025,
             f'[MM Engine] Spread: {spread_now:.1f}x  |  Base: {base_pct_now:.0f}%  |  '
             f'Fills: {fills_now:.0f}/bar  |  '
             f'20d Vol: {vc*100:.0f}%  |  Spot: ${base:,.0f}',
             color='#778899', fontsize=8, fontfamily='monospace')

    # --- Regime timeline bar ---
    bar_y = 0.065
    bar_h = 0.018
    fig.patches.append(plt.Rectangle((0.04, bar_y), 0.92, bar_h,
                                      transform=fig.transFigure,
                                      facecolor='#12121e', edgecolor='#2a2a40',
                                      linewidth=0.8, zorder=1))
    regime_colors_bar = ['#00ff88', '#00aaff', '#ffaa00', '#ff3344', '#00ffcc']
    regime_labels = ['Bull', 'Normal', 'Cautious', 'Crisis', 'Recovery']
    seg_w = 0.92 / N_REG
    progress_frac = t / (N_REG - 1)
    for k in range(N_REG):
        x0 = 0.04 + k * seg_w
        fill_frac = min(1.0, max(0.0, (progress_frac * N_REG - k)))
        alpha_val = 0.25 + 0.70 * fill_frac
        fig.patches.append(plt.Rectangle((x0, bar_y), seg_w, bar_h,
                                          transform=fig.transFigure,
                                          facecolor=regime_colors_bar[k],
                                          alpha=alpha_val, zorder=2))
        is_active = (k == ri_a) or (k == ri_b and bt > 0.15)
        lbl_alpha = 1.0 if is_active else 0.4
        fig.text(x0 + seg_w / 2, bar_y + bar_h + 0.006, regime_labels[k],
                 color=regime_colors_bar[k], fontsize=7, ha='center',
                 fontweight='bold' if is_active else 'normal',
                 alpha=lbl_alpha, fontfamily='monospace')

    marker_x = 0.04 + progress_frac * 0.92
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
