#!/usr/bin/env python3
"""
Generate animated GIFs showing LIVE mode operation:
  1. regime_phases_comparison.gif  - 3D surface morphing through live regime detection
  2. performance_vs_sp500.gif     - Live MM portfolio vs S&P 500 with regime-adjusted overlay

Strategy: ~100% base long + multi-level limit-order overlay.
Regimes: BULL, NORMAL, CAUTIOUS, CRISIS, RECOVERY.
"""

import matplotlib
matplotlib.use('Agg')

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap, Normalize
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401
import matplotlib.gridspec as gridspec
import io
from PIL import Image
from pathlib import Path

# ---------------------------------------------------------------------------
# Style constants
# ---------------------------------------------------------------------------
BG       = '#0a0a0a'
BG2      = '#111118'
TEXT     = '#cccccc'
GREEN    = '#00ff88'
RED      = '#ff3344'
YELLOW   = '#ffaa00'
BLUE     = '#00aaff'
CYAN     = '#00ffcc'
ORANGE   = '#ff8800'
MAGENTA  = '#ff44cc'
GRID_C   = '#222233'
DIM      = '#444466'

PNL_CMAP = LinearSegmentedColormap.from_list('pnl', [RED, '#ff6600', YELLOW, GREEN, CYAN])

# 5-regime colormaps
CMAP_BULL = LinearSegmentedColormap.from_list('bull', [
    '#0000aa', '#0055cc', '#22aa66', '#00ff88', '#eeffaa', '#ffffdd'])
CMAP_NORMAL = LinearSegmentedColormap.from_list('normal', [
    '#001144', '#003388', '#2266aa', '#44aacc', '#88ddee', '#ccffff'])
CMAP_CAUTIOUS = LinearSegmentedColormap.from_list('cautious', [
    '#440066', '#8833aa', '#cc7700', '#ffaa00', '#ffdd44', '#ffffcc'])
CMAP_CRISIS = LinearSegmentedColormap.from_list('crisis', [
    '#000044', '#220066', '#660088', '#cc0022', '#ff4444', '#ffcc44'])
CMAP_RECOV = LinearSegmentedColormap.from_list('recov', [
    '#330044', '#553388', '#0066aa', '#00ccee', '#66ffcc', '#eeffee'])
REGIME_CMAPS = [CMAP_BULL, CMAP_NORMAL, CMAP_CAUTIOUS, CMAP_CRISIS, CMAP_RECOV]

# MM parameters per regime
REGIME_SPREAD = {'BULL': 0.7, 'NORMAL': 1.0, 'CAUTIOUS': 2.0, 'CRISIS': 3.0, 'RECOVERY': 1.4}
REGIME_BASE   = {'BULL': 1.02, 'NORMAL': 1.00, 'CAUTIOUS': 0.90, 'CRISIS': 0.70, 'RECOVERY': 1.03}

OUT_DIR = Path(__file__).resolve().parent.parent / 'docs' / 'img'
OUT_DIR.mkdir(parents=True, exist_ok=True)

DPI = 150
FRAME_MS = 150


def fig_to_img(fig, w=1400, h=820):
    buf = io.BytesIO()
    fig.savefig(buf, format='png', dpi=DPI, facecolor=fig.get_facecolor(),
                edgecolor='none', bbox_inches='tight', pad_inches=0.25)
    buf.seek(0)
    img = Image.open(buf).convert('RGBA').resize((w, h), Image.LANCZOS)
    return img


def save_gif(frames, path, duration=FRAME_MS):
    rgb = []
    for f in frames:
        bg = Image.new('RGB', f.size, (10, 10, 10))
        bg.paste(f, mask=f.split()[3])
        rgb.append(bg)
    rgb[0].save(path, save_all=True, append_images=rgb[1:],
                duration=duration, loop=0, optimize=True)
    print(f'  Saved {path}  ({len(rgb)} frames, {path.stat().st_size/1024:.0f} KB)')


# ===================================================================
# 1. Regime Phases Comparison (Live Mode - 3D surface with MM overlay)
# ===================================================================
def generate_regime_phases():
    print('Generating regime_phases_comparison.gif ...')
    n = 55
    spot = np.linspace(78, 122, n)
    ivol = np.linspace(8, 58, n)
    X, Y = np.meshgrid(spot, ivol)

    # Regime keyframe surfaces
    def bull_surface(X, Y):
        return 28 - 0.014*(X-100)**2 - 0.006*(Y-16)**2

    def normal_surface(X, Y):
        return 22 - 0.012*(X-100)**2 - 0.005*(Y-20)**2

    def cautious_surface(X, Y):
        base = 10 - 0.010*(X-100)**2 - 0.005*(Y-28)**2
        waves = 6 * np.sin(0.4*X) * np.cos(0.3*Y)
        return base + waves

    def crisis_surface(X, Y):
        crater = -10 * np.exp(-0.015*((X-88)**2 + (Y-52)**2))
        return -22 + 0.007*(X-92)**2 + 0.004*(Y-50)**2 + crater

    def recovery_surface(X, Y):
        return 16 - 0.008*(X-100)**2 - 0.005*(Y-30)**2

    surfaces = [bull_surface, normal_surface, cautious_surface, crisis_surface, recovery_surface]

    phase_names  = ['BULL', 'NORMAL', 'CAUTIOUS', 'CRISIS', 'RECOVERY']
    phase_colors = [GREEN, BLUE, YELLOW, RED, CYAN]
    phase_infos  = [
        'LIVE | Spread: 0.7x | Base: 102% | Fills: 57/bar\n'
        'MM Engine: Tight spreads, full base + overlay capture',
        'LIVE | Spread: 1.0x | Base: 100% | Fills: 40/bar\n'
        'MM Engine: Standard spreads, neutral base position',
        'LIVE | Spread: 2.0x | Base: 90% | Fills: 20/bar\n'
        'MM Engine: Wide spreads, reduced base, cautious overlay',
        'LIVE | Spread: 3.0x | Base: 70% | Fills: 13/bar\n'
        'MM Engine: Maximum spreads, trimmed base, crisis mode',
        'LIVE | Spread: 1.4x | Base: 103% | Fills: 29/bar\n'
        'MM Engine: Tightening spreads, recovery accumulation',
    ]
    elevations = [30, 30, 26, 20, 28]
    n_phases = len(surfaces)

    tl_colors = [GREEN, BLUE, YELLOW, RED, CYAN]
    tl_labels = ['Bull', 'Normal', 'Cautious', 'Crisis', 'Recovery']

    total = 120

    def smoothstep(t):
        t = max(0.0, min(1.0, t))
        return t * t * (3 - 2 * t)

    def blend_cmap(cmap_a, cmap_b, bt, z_norm):
        return cmap_a(z_norm) * (1 - bt) + cmap_b(z_norm) * bt

    frames = []
    for fi in range(total):
        t = fi / (total - 1) * (n_phases - 1)
        ri_a = min(int(t), n_phases - 2)
        ri_b = ri_a + 1
        local_t = t - ri_a
        bt = smoothstep(local_t)

        Z_a = surfaces[ri_a](X, Y)
        Z_b = surfaces[ri_b](X, Y)
        Z = Z_a * (1 - bt) + Z_b * bt

        if bt < 0.15:
            name = phase_names[ri_a]
            color = phase_colors[ri_a]
            info = phase_infos[ri_a]
        elif bt > 0.85:
            name = phase_names[ri_b]
            color = phase_colors[ri_b]
            info = phase_infos[ri_b]
        else:
            name = f'{phase_names[ri_a]} \u2192 {phase_names[ri_b]}'
            color = phase_colors[ri_b]
            info = f'TRANSITIONING ({bt*100:.0f}%) | Regime shift detected'

        elev = elevations[ri_a] * (1 - bt) + elevations[ri_b] * bt

        fig = plt.figure(figsize=(14, 8.2), facecolor=BG)
        gs = gridspec.GridSpec(5, 1, height_ratios=[0.6, 0.1, 5.5, 0.5, 0.8],
                               hspace=0.05)

        # --- Title ---
        ax_title = fig.add_subplot(gs[0])
        ax_title.set_facecolor(BG); ax_title.axis('off')
        ax_title.text(0.5, 0.5,
                      f'\u25cf LIVE MODE  //  Regime: {name}  //  MM Engine Active',
                      color=color, fontsize=18, fontweight='bold',
                      ha='center', va='center', transform=ax_title.transAxes,
                      family='monospace')

        ax_sp = fig.add_subplot(gs[1])
        ax_sp.set_facecolor(BG); ax_sp.axis('off')

        # --- 3D Surface ---
        ax3d = fig.add_subplot(gs[2], projection='3d')
        ax3d.set_facecolor(BG)
        ax3d.xaxis.pane.fill = False; ax3d.yaxis.pane.fill = False; ax3d.zaxis.pane.fill = False
        ax3d.xaxis.pane.set_edgecolor('#1a1a2e')
        ax3d.yaxis.pane.set_edgecolor('#1a1a2e')
        ax3d.zaxis.pane.set_edgecolor('#1a1a2e')
        ax3d.grid(True, color='#223344', alpha=0.2)
        ax3d.tick_params(colors='#556677', labelsize=7)

        z_min, z_max = Z.min(), Z.max()
        z_range = z_max - z_min if z_max > z_min else 1.0
        z_norm_arr = (Z - z_min) / z_range
        face_colors = blend_cmap(REGIME_CMAPS[ri_a], REGIME_CMAPS[ri_b], bt, z_norm_arr)

        ax3d.plot_surface(X, Y, Z, facecolors=face_colors, alpha=0.93,
                          rstride=1, cstride=1, edgecolor='none', antialiased=True,
                          shade=True)

        ax3d.plot_wireframe(X[::4, ::4], Y[::4, ::4], Z[::4, ::4],
                            color='white', alpha=0.05, linewidth=0.3)

        z_floor = z_min - z_range * 0.15
        try:
            ax3d.contour(X, Y, Z, levels=np.linspace(z_min, z_max, 8),
                         zdir='z', offset=z_floor, cmap=REGIME_CMAPS[ri_a], alpha=0.3,
                         linewidths=0.5)
        except Exception:
            pass

        ax3d.set_zlim(z_floor, z_max + z_range * 0.1)
        ax3d.set_xlabel('Spot Price ($)', color=DIM, fontsize=9, labelpad=8)
        ax3d.set_ylabel('Implied Vol (%)', color=DIM, fontsize=9, labelpad=8)
        ax3d.set_zlabel('P&L ($K)', color=DIM, fontsize=9, labelpad=8)
        ax3d.view_init(elev=elev, azim=210 + fi * 1.2)

        # --- Info panel ---
        ax_info = fig.add_subplot(gs[3])
        ax_info.set_facecolor(BG); ax_info.axis('off')
        ax_info.text(0.5, 0.5, info, color=color, fontsize=10,
                     ha='center', va='center', transform=ax_info.transAxes,
                     family='monospace', alpha=0.9)

        # --- Timeline bar ---
        ax_tl = fig.add_subplot(gs[4])
        ax_tl.set_facecolor(BG); ax_tl.axis('off')
        ax_tl.set_xlim(0, 1); ax_tl.set_ylim(0, 1)

        seg_w = 1.0 / n_phases
        progress_frac = t / (n_phases - 1)
        for k in range(n_phases):
            x0 = k * seg_w
            fill = min(1.0, max(0.0, progress_frac * n_phases - k))
            alpha_v = 0.25 + 0.65 * fill
            is_active = (k == ri_a) or (k == ri_b and bt > 0.15)
            ec = 'white' if is_active else 'none'
            lw = 2.5 if is_active else 0
            rect = plt.Rectangle((x0, 0.3), seg_w, 0.4,
                                 facecolor=tl_colors[k], alpha=alpha_v,
                                 edgecolor=ec, linewidth=lw)
            ax_tl.add_patch(rect)
            lbl_col = 'white' if is_active else DIM
            ax_tl.text(x0 + seg_w/2, 0.5, tl_labels[k], color=lbl_col,
                       fontsize=8, ha='center', va='center', fontweight='bold',
                       family='monospace')

        ax_tl.plot([progress_frac], [0.15], marker='^', color='white',
                   markersize=8, transform=ax_tl.transAxes, clip_on=False)

        frames.append(fig_to_img(fig, 1400, 820))
        plt.close(fig)

    save_gif(frames, OUT_DIR / 'regime_phases_comparison.gif', duration=120)


# ===================================================================
# 2. Live Performance vs S&P 500 with Market-Maker Overlay
# ===================================================================
def generate_performance_chart():
    print('Generating performance_vs_sp500.gif ...')

    np.random.seed(42)
    days = 756

    # Regime schedule (trading days)
    regime_bounds = [0, 150, 260, 340, 460, days]
    regime_names  = ['BULL', 'NORMAL', 'CAUTIOUS', 'CRISIS', 'RECOVERY']
    regime_colors = [GREEN, BLUE, YELLOW, RED, CYAN]
    regime_drifts = [0.0005, 0.0002, -0.0002, -0.0028, 0.0015]
    regime_vols   = [0.008, 0.010, 0.014, 0.032, 0.016]

    # S&P 500 daily returns per regime
    sp_ret = np.zeros(days)
    for i in range(len(regime_names)):
        a, b = regime_bounds[i], regime_bounds[i+1]
        sp_ret[a:b] = np.random.normal(regime_drifts[i], regime_vols[i], b-a)

    sp_cum = np.cumprod(1 + sp_ret) - 1

    # MM portfolio: base position + spread capture, regime-adjusted
    port_ret = np.zeros(days)
    spread_arr = np.ones(days)
    base_arr = np.ones(days)
    fills_arr = np.zeros(days)
    for i in range(len(regime_names)):
        a, b = regime_bounds[i], regime_bounds[i+1]
        rn = regime_names[i]
        base_pct = REGIME_BASE[rn]
        spread_m = REGIME_SPREAD[rn]
        spread_arr[a:b] = spread_m
        base_arr[a:b] = base_pct
        fills_arr[a:b] = max(5, 40 * (1.0 / spread_m))
        for d in range(a, b):
            base_pnl = base_pct * sp_ret[d]
            daily_vol = abs(sp_ret[d])
            spread_capture = daily_vol * 0.15 * (1.0 / spread_m)
            port_ret[d] = base_pnl + spread_capture

    port_cum = np.cumprod(1 + port_ret) - 1

    # Drawdown
    port_peak = np.maximum.accumulate(1 + port_cum)
    port_dd = (port_peak - (1 + port_cum)) / port_peak
    sp_peak = np.maximum.accumulate(1 + sp_cum)
    sp_dd = (sp_peak - (1 + sp_cum)) / sp_peak

    # Compute rolling Sharpe, Sortino, Calmar
    def rolling_sharpe(rets, window=60):
        out = np.zeros(len(rets))
        for i in range(len(rets)):
            w = rets[max(0,i-window):i+1]
            if len(w) < 5:
                out[i] = 0
            else:
                std = np.std(w)
                out[i] = (np.mean(w) * 252) / (std * np.sqrt(252)) if std > 1e-10 else 0
        return out

    def rolling_sortino(rets, window=60):
        out = np.zeros(len(rets))
        for i in range(len(rets)):
            w = rets[max(0,i-window):i+1]
            if len(w) < 5:
                out[i] = 0
            else:
                neg = w[w < 0]
                dd = np.sqrt(np.mean(neg**2)) if len(neg) > 0 else 1e-10
                out[i] = (np.mean(w) * 252) / (dd * np.sqrt(252)) if dd > 1e-10 else 0
        return out

    port_sharpe = rolling_sharpe(port_ret)
    port_sortino = rolling_sortino(port_ret)

    # Regime transition markers (MM actions, not BUY/SELL)
    trade_days = [0, 150, 260, 340, 460]
    trade_labels = [
        'Spread: 0.7x\nBase: 102%',
        'Spread: 1.0x\nBase: 100%',
        'Spread: 2.0x\nBase: 90%',
        'Spread: 3.0x\nBase: 70%',
        'Spread: 1.4x\nBase: 103%',
    ]
    trade_colors = [GREEN, BLUE, YELLOW, RED, CYAN]

    total_frames = 60
    day_indices = np.linspace(5, days, total_frames, dtype=int)

    frames = []
    for fi, d in enumerate(day_indices):
        fig = plt.figure(figsize=(14, 8.5), facecolor=BG)
        gs = gridspec.GridSpec(3, 2, height_ratios=[0.5, 4, 2.5],
                               width_ratios=[3, 1], hspace=0.35, wspace=0.25)

        # --- Title ---
        ax_t = fig.add_subplot(gs[0, :])
        ax_t.set_facecolor(BG); ax_t.axis('off')

        cur_regime = 0
        for ri in range(len(regime_names)):
            if d >= regime_bounds[ri]:
                cur_regime = ri
        rc = regime_colors[cur_regime]
        rn = regime_names[cur_regime]

        port_ret_now = port_cum[d-1]*100 if d > 0 else 0
        sp_ret_now = sp_cum[d-1]*100 if d > 0 else 0
        alpha_now = port_ret_now - sp_ret_now
        sharpe_now = port_sharpe[min(d-1, len(port_sharpe)-1)] if d > 0 else 0
        sortino_now = port_sortino[min(d-1, len(port_sortino)-1)] if d > 0 else 0

        ax_t.text(0.5, 0.65,
                  f'\u25cf LIVE  |  MM Strategy vs S&P 500  |  Day {d}/{days}  |  Regime: {rn}',
                  color=rc, fontsize=15, fontweight='bold', ha='center', va='center',
                  transform=ax_t.transAxes, family='monospace')
        ax_t.text(0.5, 0.12,
                  f'Strategy: {port_ret_now:+.1f}%    S&P 500: {sp_ret_now:+.1f}%    '
                  f'Alpha: {alpha_now:+.1f}%    Sharpe: {sharpe_now:.2f}    Sortino: {sortino_now:.2f}',
                  color=TEXT, fontsize=10, ha='center', va='center',
                  transform=ax_t.transAxes, family='monospace')

        # --- Main return chart ---
        ax_r = fig.add_subplot(gs[1, 0])
        ax_r.set_facecolor(BG2)
        ax_r.grid(True, color=GRID_C, alpha=0.3, linewidth=0.5)
        for spine in ax_r.spines.values():
            spine.set_color(GRID_C)
        ax_r.tick_params(colors=DIM, labelsize=8)
        ax_r.set_xlabel('Trading Day', color=DIM, fontsize=9)
        ax_r.set_ylabel('Cumulative Return (%)', color=DIM, fontsize=9)
        ax_r.set_xlim(0, days)
        ax_r.set_ylim(min(sp_cum.min(), port_cum.min())*105, max(sp_cum.max(), port_cum.max())*130)

        # Regime background bands
        for ri in range(len(regime_names)):
            a, b = regime_bounds[ri], min(regime_bounds[ri+1], d)
            if a < d:
                ax_r.axvspan(a, b, color=regime_colors[ri], alpha=0.06)
                if b - a > 30:
                    ax_r.text((a+b)/2, ax_r.get_ylim()[1]*0.92, regime_names[ri],
                              color=regime_colors[ri], fontsize=7, ha='center',
                              alpha=0.6, family='monospace')

        x = np.arange(d)
        ax_r.plot(x, sp_cum[:d]*100, color=RED, linewidth=1.8, alpha=0.7, label='S&P 500')
        ax_r.fill_between(x, 0, sp_cum[:d]*100, color=RED, alpha=0.04)
        ax_r.plot(x, port_cum[:d]*100, color=GREEN, linewidth=2.8, label='MM Strategy (Base+Overlay)')
        ax_r.fill_between(x, 0, port_cum[:d]*100, color=GREEN, alpha=0.08)

        # Mark regime transition points
        for ti, td in enumerate(trade_days):
            if td < d and td > 0:
                ax_r.axvline(td, color=trade_colors[ti], linestyle='--', alpha=0.5, linewidth=1)
                y_pos = ax_r.get_ylim()[1]*0.85 - (ti % 2)*5
                ax_r.annotate(trade_labels[ti], xy=(td, port_cum[td]*100),
                             xytext=(td+15, y_pos),
                             color=trade_colors[ti], fontsize=7,
                             fontfamily='monospace', alpha=0.8,
                             arrowprops=dict(arrowstyle='->', color=trade_colors[ti], alpha=0.4))

        ax_r.axhline(0, color=DIM, linewidth=0.8, linestyle='--', alpha=0.5)
        ax_r.legend(fontsize=9, loc='upper left', facecolor=BG2,
                    edgecolor=GRID_C, labelcolor=TEXT)

        # --- Stats panel ---
        ax_s = fig.add_subplot(gs[1, 1])
        ax_s.set_facecolor(BG2); ax_s.axis('off')

        calmar_now = 0.0
        if d > 5:
            mdd = port_dd[:d].max()
            if mdd > 1e-10:
                ann_ret = port_cum[d-1] * (252.0 / d)
                calmar_now = ann_ret / mdd

        stats = [
            ('MM STRATEGY', GREEN),
            (f'Return: {port_ret_now:+.1f}%', GREEN),
            (f'MaxDD: {port_dd[:d].max()*100:.1f}%', YELLOW if port_dd[:d].max()<0.15 else RED),
            (f'Sharpe:  {sharpe_now:.2f}', BLUE),
            (f'Sortino: {sortino_now:.2f}', BLUE),
            (f'Calmar:  {calmar_now:.2f}', BLUE),
            ('', TEXT),
            ('S&P 500', RED),
            (f'Return: {sp_ret_now:+.1f}%', RED),
            (f'MaxDD: {sp_dd[:d].max()*100:.1f}%', RED),
            ('', TEXT),
            (f'ALPHA: {alpha_now:+.1f}%', GREEN if alpha_now > 0 else RED),
            ('', TEXT),
            ('MM PARAMS', CYAN),
            (f'Spread: {spread_arr[min(d-1,days-1)]:.1f}x', CYAN),
            (f'Base:   {base_arr[min(d-1,days-1)]*100:.0f}%', CYAN),
            (f'Fills:  {fills_arr[min(d-1,days-1)]:.0f}/d', CYAN),
        ]

        header_idx = {0, 7, 11, 13}
        for si, (txt, col) in enumerate(stats):
            y = 0.97 - si * 0.058
            fs = 11 if si in header_idx else 9
            fw = 'bold' if si in header_idx else 'normal'
            ax_s.text(0.1, y, txt, color=col, fontsize=fs, fontweight=fw,
                      transform=ax_s.transAxes, family='monospace')

        # --- Drawdown chart ---
        ax_dd = fig.add_subplot(gs[2, 0])
        ax_dd.set_facecolor(BG2)
        ax_dd.grid(True, color=GRID_C, alpha=0.3, linewidth=0.5)
        for spine in ax_dd.spines.values():
            spine.set_color(GRID_C)
        ax_dd.tick_params(colors=DIM, labelsize=7)
        ax_dd.set_xlabel('Trading Day', color=DIM, fontsize=8)
        ax_dd.set_ylabel('Drawdown (%)', color=DIM, fontsize=8)
        ax_dd.set_xlim(0, days)
        ax_dd.set_ylim(-max(sp_dd.max(), port_dd.max())*130, 2)

        ax_dd.fill_between(x, -sp_dd[:d]*100, color=RED, alpha=0.3, label='S&P 500 DD')
        ax_dd.fill_between(x, -port_dd[:d]*100, color=GREEN, alpha=0.3, label='MM Strategy DD')
        ax_dd.plot(x, -sp_dd[:d]*100, color=RED, linewidth=1, alpha=0.6)
        ax_dd.plot(x, -port_dd[:d]*100, color=GREEN, linewidth=1.5)
        ax_dd.axhline(0, color=DIM, linewidth=0.5)
        ax_dd.legend(fontsize=7, loc='lower left', facecolor=BG2,
                     edgecolor=GRID_C, labelcolor=TEXT)

        # --- MM Parameters panel (replaces old allocation bar) ---
        ax_al = fig.add_subplot(gs[2, 1])
        ax_al.set_facecolor(BG2); ax_al.axis('off')
        ax_al.set_xlim(0, 1); ax_al.set_ylim(0, 1)

        cur_spread = spread_arr[min(d-1, days-1)]
        cur_base = base_arr[min(d-1, days-1)]
        cur_fills = fills_arr[min(d-1, days-1)]

        ax_al.text(0.5, 0.95, 'MM PARAMETERS', color=TEXT, fontsize=10,
                   fontweight='bold', ha='center', va='top',
                   transform=ax_al.transAxes, family='monospace')

        # Spread width bar (0.7 - 3.0 range, normalized)
        bar_data = [
            ('Spread', cur_spread / 3.0, YELLOW),
            ('Base',   cur_base, GREEN),
            ('Fills',  cur_fills / 60.0, CYAN),
        ]
        for bi, (lbl, val, col) in enumerate(bar_data):
            y = 0.72 - bi * 0.28
            ax_al.add_patch(plt.Rectangle((0.08, y-0.06), 0.84*min(val, 1.0), 0.12,
                                          facecolor=col, alpha=0.7,
                                          transform=ax_al.transAxes))
            ax_al.add_patch(plt.Rectangle((0.08, y-0.06), 0.84, 0.12,
                                          facecolor='none', edgecolor=GRID_C,
                                          linewidth=1, transform=ax_al.transAxes))
            ax_al.text(0.06, y, f'{lbl}', color=col, fontsize=8, ha='right',
                       va='center', transform=ax_al.transAxes, family='monospace')
            if lbl == 'Spread':
                display_val = f'{cur_spread:.1f}x'
            elif lbl == 'Base':
                display_val = f'{cur_base*100:.0f}%'
            else:
                display_val = f'{cur_fills:.0f}/d'
            ax_al.text(0.08 + 0.84*min(val, 1.0) + 0.02, y, display_val, color=col, fontsize=9,
                       ha='left', va='center', transform=ax_al.transAxes,
                       family='monospace', fontweight='bold')

        frames.append(fig_to_img(fig, 1400, 820))
        plt.close(fig)

    save_gif(frames, OUT_DIR / 'performance_vs_sp500.gif', duration=FRAME_MS)


# ===================================================================
if __name__ == '__main__':
    print(f'Output: {OUT_DIR}')
    generate_regime_phases()
    generate_performance_chart()
    print('\nDone.')
