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

# --- Custom diverging colormaps per regime (peaks vs valleys = different hues) ---
CMAP_BULL = LinearSegmentedColormap.from_list('bull', [
    '#0000aa', '#0055cc', '#22aa66', '#00ff88', '#eeffaa', '#ffffdd'])
CMAP_TRANSITION = LinearSegmentedColormap.from_list('trans', [
    '#440066', '#8833aa', '#cc7700', '#ffaa00', '#ffdd44', '#ffffcc'])
CMAP_CRISIS = LinearSegmentedColormap.from_list('crisis', [
    '#000044', '#220066', '#660088', '#cc0022', '#ff4444', '#ffcc44'])
CMAP_RECOVERY = LinearSegmentedColormap.from_list('recov', [
    '#330044', '#553388', '#0066aa', '#00ccee', '#66ffcc', '#eeffee'])

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
FPR = 14             # frames per regime (stable)
TPR = 10             # transition frames between regimes
NUM_TRANSITIONS = len(REGIMES) - 1
TOTAL = FPR * len(REGIMES) + TPR * NUM_TRANSITIONS
FIG_DPI = 150        # (was 100)
FIG_W, FIG_H = 10, 7.5  # (was 8, 6)

def smooth_interp(a, b, t):
    """Smooth-step interpolation (ease in/out)."""
    t = max(0, min(1, t))
    s = t * t * (3 - 2 * t)  # smoothstep
    return a * (1 - s) + b * s

# --- Build frame schedule: stable regimes + transition blends ---
# Each entry: (regime_idx, blend_t, is_transition, next_regime_idx)
#   blend_t in [0,1] for transitions (0=fully prev, 1=fully next)
frame_schedule = []
for ri in range(len(REGIMES)):
    for sf in range(FPR):
        frame_schedule.append((ri, 0.0, False, ri))
    if ri < len(REGIMES) - 1:
        for tf in range(TPR):
            blend = (tf + 1) / (TPR + 1)  # 0 < blend < 1
            frame_schedule.append((ri, blend, True, ri + 1))

def blend_cmap(cmap_a, cmap_b, blend_t, z_norm):
    """Blend two colormaps pixel-by-pixel."""
    ca = cmap_a(z_norm)
    cb = cmap_b(z_norm)
    return ca * (1 - blend_t) + cb * blend_t

def compute_surface(regime, fi_global):
    """Compute Z surface for a given regime config."""
    ri_idx = REGIMES.index(regime)
    base = regime["base"]
    vc = regime["vc"]
    sc = regime["scale"]
    z_boost = regime["z_boost"]

    spot_range = np.linspace(base * 0.83, base * 1.17, GRID_N)
    vol_range = np.linspace(max(0.04, vc - 0.14), vc + 0.18, GRID_N)
    S, V = np.meshgrid(spot_range, vol_range)

    Z = iron_condor_pnl(S, V, base, 20.0 / 365.0, 0.05) / sc * z_boost

    if ri_idx == 1:
        ripple = np.sin(S / 40 + fi_global * 0.4) * np.cos(V * 25 + fi_global * 0.3)
        Z += ripple * 5.0
    elif ri_idx == 2:
        turb = (np.sin(S / 30 + fi_global * 0.5) * np.cos(V * 30 + fi_global * 0.35)
                + 0.5 * np.sin(S / 15 + fi_global * 0.7) * np.cos(V * 15 + fi_global * 0.5))
        Z += turb * sc * 2.5
        cx, cy = base, vc
        dist = ((S - cx) / (base * 0.15))**2 + ((V - cy) / 0.15)**2
        Z -= 8.0 * np.exp(-dist * 0.5) * z_boost
    elif ri_idx == 3:
        turb = np.sin(S / 35 + fi_global * 0.3) * np.cos(V * 20 + fi_global * 0.2)
        Z += turb * 1.0

    return S, V, Z

frames = []
for fi, (ri, blend_t, is_trans, nri) in enumerate(frame_schedule):
    curr = REGIMES[ri]

    if is_trans:
        nxt = REGIMES[nri]
        # Smooth-step the blend
        bt = smooth_interp(0, 1, blend_t)

        # Compute both surfaces on a common grid
        base = smooth_interp(curr["base"], nxt["base"], bt)
        vc   = smooth_interp(curr["vc"],   nxt["vc"],   bt)
        spot_range = np.linspace(base * 0.83, base * 1.17, GRID_N)
        vol_range = np.linspace(max(0.04, vc - 0.14), vc + 0.18, GRID_N)
        S, V = np.meshgrid(spot_range, vol_range)

        sc_a = curr["scale"]; sc_b = nxt["scale"]
        sc = smooth_interp(sc_a, sc_b, bt)
        zb_a = curr["z_boost"]; zb_b = nxt["z_boost"]
        zb = smooth_interp(zb_a, zb_b, bt)
        Z_a = iron_condor_pnl(S, V, curr["base"], 20.0/365.0, 0.05) / sc_a * zb_a
        Z_b = iron_condor_pnl(S, V, nxt["base"], 20.0/365.0, 0.05) / sc_b * zb_b

        # Add regime-specific distortions to each
        if ri == 1:
            ripple = np.sin(S/40 + fi*0.4) * np.cos(V*25 + fi*0.3)
            Z_a += ripple * 5.0 * (1 - bt)
        if ri == 2:
            turb = (np.sin(S/30 + fi*0.5) * np.cos(V*30 + fi*0.35)
                    + 0.5*np.sin(S/15 + fi*0.7) * np.cos(V*15 + fi*0.5))
            Z_a += turb * sc_a * 2.5 * (1 - bt)
        if nri == 2:
            turb = (np.sin(S/30 + fi*0.5) * np.cos(V*30 + fi*0.35)
                    + 0.5*np.sin(S/15 + fi*0.7) * np.cos(V*15 + fi*0.5))
            Z_b += turb * sc_b * 2.5 * bt

        Z = Z_a * (1 - bt) + Z_b * bt

        vix  = smooth_interp(curr["vix"],  nxt["vix"],  bt)
        warn = smooth_interp(curr["warning"], nxt["warning"], bt)
        crisis_p = smooth_interp(curr["crisis"], nxt["crisis"], bt)
        elev = smooth_interp(curr["elev"], nxt["elev"], bt)

        # Title shows transition
        tc = nxt["color"]
        title_name = f'{curr["name"]} \u2192 {nxt["name"]}'
        title_signal = f'TRANSITIONING ({bt*100:.0f}%)'
    else:
        S_c, V_c, Z = compute_surface(curr, fi)
        S, V = S_c, V_c
        vix = curr["vix"]
        warn = curr["warning"]
        crisis_p = curr["crisis"]
        elev = curr["elev"]
        tc = curr["color"]
        title_name = curr["name"]
        title_signal = curr["signal"]

    # --- Rendering ---
    fig = plt.figure(figsize=(FIG_W, FIG_H), dpi=FIG_DPI, facecolor='#080810')
    ax = fig.add_subplot(111, projection='3d', facecolor='#080810')

    # Normalize Z for coloring
    z_min, z_max = Z.min(), Z.max()
    z_range = z_max - z_min if z_max > z_min else 1.0
    z_norm = (Z - z_min) / z_range

    # Colormap: blend between regime cmaps during transitions
    if is_trans:
        face_colors = blend_cmap(curr["cmap"], nxt["cmap"], bt, z_norm)
    else:
        face_colors = curr["cmap"](z_norm)

    # Main surface
    ax.plot_surface(S, V * 100, Z, facecolors=face_colors, alpha=0.92,
                    edgecolor='none', antialiased=True, shade=True,
                    rstride=1, cstride=1)

    # Wireframe overlay for depth
    ax.plot_wireframe(S[::4, ::4], V[::4, ::4] * 100, Z[::4, ::4],
                      color='white', alpha=0.06, linewidth=0.3)

    # Contour projection on floor
    z_floor = z_min - (z_range * 0.15)
    try:
        contour_levels = np.linspace(z_min, z_max, 8)
        cmap_for_contour = curr["cmap"]
        ax.contour(S, V * 100, Z, levels=contour_levels, zdir='z', offset=z_floor,
                   cmap=cmap_for_contour, alpha=0.35, linewidths=0.6)
    except Exception:
        pass

    ax.set_zlim(z_floor, z_max + z_range * 0.1)

    # Camera
    azim = 215 + fi * 1.8
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
    fig.suptitle(
        f'\u25cf LIVE  |  Regime: {title_name}  |  Signal: {title_signal}  |  VIX: {vix:.1f}',
        color=tc, fontsize=14, fontweight='bold', y=0.96, fontfamily='monospace')

    # --- Info bar ---
    exec_label = curr["exec"] if not is_trans else f'{curr["exec"]} -> {nxt["exec"]}'
    cash_pct = int(smooth_interp(curr["cash"], nxt["cash"], blend_t)) if is_trans else curr["cash"]
    eq_pct = int(smooth_interp(curr["equity"], nxt["equity"], blend_t)) if is_trans else curr["equity"]
    opt_pct = int(smooth_interp(curr["options"], nxt["options"], blend_t)) if is_trans else curr["options"]
    fig.text(0.03, 0.025,
             f'[Execution] {exec_label}   |   '
             f'Cash: {cash_pct}%  Equity: {eq_pct}%  Options: {opt_pct}%   |   '
             f'Warning: {warn:.0f}%  Crisis Prob: {crisis_p:.0f}%',
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
    progress = ri + (blend_t if is_trans else 0)
    for k in range(5):
        x0 = 0.04 + k * seg_w
        if k < ri or (k == ri and not is_trans):
            w = seg_w
            alpha_val = 0.95
        elif k == ri and is_trans:
            w = seg_w
            alpha_val = 0.95
        else:
            w = seg_w
            alpha_val = 0.15
        fig.patches.append(plt.Rectangle((x0, bar_y), w, bar_h,
                                          transform=fig.transFigure,
                                          facecolor=regime_colors_bar[k],
                                          alpha=alpha_val, zorder=2))
        lbl_alpha = 1.0 if k == ri or (is_trans and k == nri) else 0.4
        fig.text(x0 + seg_w / 2, bar_y + bar_h + 0.006, regime_labels[k],
                 color=regime_colors_bar[k], fontsize=7, ha='center',
                 fontweight='bold' if (k == ri or (is_trans and k == nri)) else 'normal',
                 alpha=lbl_alpha, fontfamily='monospace')

    # Active marker
    marker_x = 0.04 + progress * seg_w
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
