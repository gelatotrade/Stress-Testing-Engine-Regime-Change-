#!/usr/bin/env python3
"""
Generate combined dashboard GIFs for 3 timeframes:
  1. combined_dashboard_daily.gif   - Daily bars (756 days / ~3 years)
  2. combined_dashboard_hourly.gif  - Hourly bars (1680 hours / ~4 months)
  3. combined_dashboard_minute.gif  - Minute bars (2340 min / ~6 trading days)

Each GIF shows:
  Left:  S&P 500 benchmark vs Strategy performance chart
  Right: 3D regime change surface (synchronized)

Fully continuous smooth transitions, diverging colormaps.
"""
import matplotlib
matplotlib.use('Agg')

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401
import matplotlib.gridspec as gridspec
import io
from PIL import Image
from pathlib import Path

# ---------------------------------------------------------------------------
# Style
# ---------------------------------------------------------------------------
BG   = '#0a0a0a'
BG2  = '#111118'
TEXT = '#cccccc'
GREEN  = '#00ff88'
RED    = '#ff3344'
YELLOW = '#ffaa00'
BLUE   = '#00aaff'
CYAN   = '#00ffcc'
GRID_C = '#222233'
DIM    = '#444466'

CMAP_BULL = LinearSegmentedColormap.from_list('bull', [
    '#0000aa', '#0055cc', '#22aa66', '#00ff88', '#eeffaa', '#ffffdd'])
CMAP_TRANS = LinearSegmentedColormap.from_list('trans', [
    '#440066', '#8833aa', '#cc7700', '#ffaa00', '#ffdd44', '#ffffcc'])
CMAP_CRISIS = LinearSegmentedColormap.from_list('crisis', [
    '#000044', '#220066', '#660088', '#cc0022', '#ff4444', '#ffcc44'])
CMAP_RECOV = LinearSegmentedColormap.from_list('recov', [
    '#330044', '#553388', '#0066aa', '#00ccee', '#66ffcc', '#eeffee'])
REGIME_CMAPS = [CMAP_BULL, CMAP_TRANS, CMAP_CRISIS, CMAP_RECOV, CMAP_BULL]

OUT_DIR = Path(__file__).resolve().parent.parent / 'docs' / 'img'
OUT_DIR.mkdir(parents=True, exist_ok=True)

DPI = 150
TOTAL_FRAMES = 120

# ---------------------------------------------------------------------------
# 3D regime surfaces (shared across timeframes)
# ---------------------------------------------------------------------------
n_grid = 50
spot = np.linspace(78, 122, n_grid)
ivol = np.linspace(8, 58, n_grid)
X, Y = np.meshgrid(spot, ivol)

def _bull(X, Y):
    return 28 - 0.014 * (X - 100)**2 - 0.006 * (Y - 16)**2

def _transition(X, Y):
    base = 10 - 0.010 * (X - 100)**2 - 0.005 * (Y - 28)**2
    return base + 6 * np.sin(0.4 * X) * np.cos(0.3 * Y)

def _crisis(X, Y):
    crater = -10 * np.exp(-0.015 * ((X - 88)**2 + (Y - 52)**2))
    return -22 + 0.007 * (X - 92)**2 + 0.004 * (Y - 50)**2 + crater

def _recovery(X, Y):
    return 16 - 0.008 * (X - 100)**2 - 0.005 * (Y - 30)**2

surfaces = [_bull, _transition, _crisis, _recovery, _bull]
elevations = [30, 26, 20, 28, 30]
n_reg = len(surfaces)

def smoothstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)

def blend_cmap(cmap_a, cmap_b, bt, z_norm):
    return cmap_a(z_norm) * (1 - bt) + cmap_b(z_norm) * bt

def fig_to_img(fig, w=1800, h=820):
    buf = io.BytesIO()
    fig.savefig(buf, format='png', dpi=DPI, facecolor=fig.get_facecolor(),
                edgecolor='none', bbox_inches='tight', pad_inches=0.2)
    buf.seek(0)
    img = Image.open(buf).convert('RGBA').resize((w, h), Image.LANCZOS)
    return img

def save_gif(frames, path, duration=120):
    rgb = []
    for f in frames:
        bg = Image.new('RGB', f.size, (10, 10, 10))
        bg.paste(f, mask=f.split()[3])
        rgb.append(bg)
    rgb[0].save(path, save_all=True, append_images=rgb[1:],
                duration=duration, loop=0, optimize=True)
    print(f'  Saved {path}  ({len(rgb)} frames, {path.stat().st_size / 1024:.0f} KB)')


# ---------------------------------------------------------------------------
# Timeframe configurations
# ---------------------------------------------------------------------------
TIMEFRAMES = {
    'daily': {
        'filename': 'combined_dashboard_daily.gif',
        'n_bars': 756,
        'x_label': 'Trading Day',
        'time_unit': 'Day',
        'total_label': '~3 Years',
        'regime_bounds': [0, 180, 240, 340, 460, 756],
        'regime_names': ['Bull Quiet', 'Transition', 'Bear Volatile', 'Recovery', 'New Bull'],
        'regime_colors': [GREEN, YELLOW, RED, CYAN, GREEN],
        # Daily drift/vol
        'regime_drifts': [0.0005, -0.0002, -0.0028, 0.0015, 0.0006],
        'regime_vols':   [0.008,   0.014,   0.032,   0.016,  0.009],
        # Portfolio multipliers per regime
        'port_mult':   [1.0,  0.4,  0.18, 1.3,  1.1],
        'port_offset': [0.0,  0.0003, 0.0005, 0.0003, 0.0002],
        # Trades
        'trade_bars':   [0, 180, 240, 340, 460],
        'trade_labels': ['BUY 892 SPY', 'SELL 446', 'SELL 357 (hedge)', 'BUY 663', 'BUY 224'],
        'trade_colors': [GREEN, RED, RED, GREEN, GREEN],
        'seed': 42,
        'sp_label': '$5,280',
    },
    'hourly': {
        'filename': 'combined_dashboard_hourly.gif',
        'n_bars': 1680,   # ~4 months of hourly data (6.5h/day × 65 days per month × 4)
        'x_label': 'Hour',
        'time_unit': 'Hour',
        'total_label': '~4 Months (1H)',
        'regime_bounds': [0, 400, 540, 760, 1030, 1680],
        'regime_names': ['Bull Quiet', 'Transition', 'Bear Volatile', 'Recovery', 'New Bull'],
        'regime_colors': [GREEN, YELLOW, RED, CYAN, GREEN],
        # Hourly: drift/vol ~ daily / sqrt(6.5)
        'regime_drifts': [0.000077, -0.000031, -0.00043, 0.00023,  0.000092],
        'regime_vols':   [0.0031,    0.0055,    0.0125,  0.0063,   0.0035],
        'port_mult':   [1.0,  0.4,  0.18, 1.3,  1.1],
        'port_offset': [0.0, 0.000046, 0.000077, 0.000046, 0.000031],
        'trade_bars':   [0, 400, 540, 760, 1030],
        'trade_labels': ['BUY 892 SPY', 'SELL 446', 'SELL 357 (hedge)', 'BUY 663', 'BUY 224'],
        'trade_colors': [GREEN, RED, RED, GREEN, GREEN],
        'seed': 123,
        'sp_label': '$5,280',
    },
    'minute': {
        'filename': 'combined_dashboard_minute.gif',
        'n_bars': 2340,   # 6 trading days × 390 min/day
        'x_label': 'Minute',
        'time_unit': 'Min',
        'total_label': '~6 Days (1M)',
        'regime_bounds': [0, 550, 780, 1200, 1650, 2340],
        'regime_names': ['Bull Quiet', 'Transition', 'Bear Volatile', 'Recovery', 'New Bull'],
        'regime_colors': [GREEN, YELLOW, RED, CYAN, GREEN],
        # Minute: drift/vol ~ daily / sqrt(390)
        'regime_drifts': [0.0000025, -0.000001, -0.000014, 0.0000076, 0.000003],
        'regime_vols':   [0.00040,    0.00071,   0.0016,   0.00081,   0.00046],
        'port_mult':   [1.0,  0.4,  0.18, 1.3,  1.1],
        'port_offset': [0.0, 0.0000015, 0.0000025, 0.0000015, 0.000001],
        'trade_bars':   [0, 550, 780, 1200, 1650],
        'trade_labels': ['BUY 892 SPY', 'SELL 446', 'SELL 357 (hedge)', 'BUY 663', 'BUY 224'],
        'trade_colors': [GREEN, RED, RED, GREEN, GREEN],
        'seed': 777,
        'sp_label': '$5,280',
    },
}


def generate_dashboard(tf_key):
    cfg = TIMEFRAMES[tf_key]
    print(f'Generating {cfg["filename"]} ({cfg["total_label"]}) ...')

    np.random.seed(cfg['seed'])
    N = cfg['n_bars']
    bounds = cfg['regime_bounds']
    names = cfg['regime_names']
    colors = cfg['regime_colors']

    # Generate S&P returns
    sp_ret = np.zeros(N)
    for i in range(len(names)):
        a, b = bounds[i], bounds[i + 1]
        sp_ret[a:b] = np.random.normal(cfg['regime_drifts'][i], cfg['regime_vols'][i], b - a)
    sp_cum = np.cumprod(1 + sp_ret) - 1

    # Strategy returns
    port_ret = sp_ret.copy()
    for i in range(len(names)):
        a, b = bounds[i], bounds[i + 1]
        port_ret[a:b] = sp_ret[a:b] * cfg['port_mult'][i] + cfg['port_offset'][i]
    port_cum = np.cumprod(1 + port_ret) - 1

    def bar_to_regime_t(d):
        for i in range(len(bounds) - 1):
            if d < bounds[i + 1]:
                local = (d - bounds[i]) / (bounds[i + 1] - bounds[i])
                return i + local
        return n_reg - 1

    bar_indices = np.linspace(5, N, TOTAL_FRAMES, dtype=int)

    frames_list = []
    for fi, d in enumerate(bar_indices):
        fig = plt.figure(figsize=(20, 8.5), facecolor=BG)
        gs_outer = gridspec.GridSpec(2, 1, height_ratios=[0.6, 9], hspace=0.08,
                                     figure=fig)

        # --- Title ---
        ax_title = fig.add_subplot(gs_outer[0])
        ax_title.set_facecolor(BG); ax_title.axis('off')

        cur_regime = 0
        for ri in range(len(names)):
            if d >= bounds[ri]:
                cur_regime = ri
        rc = colors[cur_regime]
        rn = names[cur_regime]

        port_pct = port_cum[d - 1] * 100 if d > 0 else 0
        sp_pct = sp_cum[d - 1] * 100 if d > 0 else 0
        alpha_pct = port_pct - sp_pct

        ax_title.text(0.5, 0.55,
                      f'\u25cf LIVE {cfg["total_label"]}  |  '
                      f'{cfg["time_unit"]} {d}/{N}  |  Regime: {rn}  |  '
                      f'Portfolio: {port_pct:+.1f}%  |  S&P 500: {sp_pct:+.1f}%  |  '
                      f'Alpha: {alpha_pct:+.1f}%',
                      color=rc, fontsize=13, fontweight='bold', ha='center', va='center',
                      transform=ax_title.transAxes, family='monospace')

        # --- Layout ---
        gs_bottom = gridspec.GridSpecFromSubplotSpec(1, 2, subplot_spec=gs_outer[1],
                                                      width_ratios=[1.1, 1], wspace=0.08)
        gs_left = gridspec.GridSpecFromSubplotSpec(2, 1, subplot_spec=gs_bottom[0],
                                                    height_ratios=[3, 1.2], hspace=0.3)

        # ===== LEFT: Performance chart =====
        ax_r = fig.add_subplot(gs_left[0])
        ax_r.set_facecolor(BG2)
        ax_r.grid(True, color=GRID_C, alpha=0.3, linewidth=0.5)
        for spine in ax_r.spines.values():
            spine.set_color(GRID_C)
        ax_r.tick_params(colors=DIM, labelsize=7)
        ax_r.set_ylabel('Cumulative Return (%)', color=DIM, fontsize=9)
        ax_r.set_xlim(0, N)
        y_lo = min(sp_cum.min(), port_cum.min()) * 105
        y_hi = max(sp_cum.max(), port_cum.max()) * 135
        ax_r.set_ylim(y_lo, y_hi)

        # Regime bands
        for ri in range(len(names)):
            a, b = bounds[ri], min(bounds[ri + 1], d)
            if a < d:
                ax_r.axvspan(a, b, color=colors[ri], alpha=0.06)
                if b - a > N * 0.05:
                    ax_r.text((a + b) / 2, y_hi * 0.88, names[ri],
                              color=colors[ri], fontsize=6, ha='center',
                              alpha=0.6, family='monospace')

        x = np.arange(d)
        ax_r.plot(x, sp_cum[:d] * 100, color=RED, linewidth=1.5, alpha=0.7, label='S&P 500')
        ax_r.fill_between(x, 0, sp_cum[:d] * 100, color=RED, alpha=0.04)
        ax_r.plot(x, port_cum[:d] * 100, color=GREEN, linewidth=2.5, label='Strategy')
        ax_r.fill_between(x, 0, port_cum[:d] * 100, color=GREEN, alpha=0.07)

        # Trade markers
        for ti, td in enumerate(cfg['trade_bars']):
            if 0 < td < d:
                ax_r.axvline(td, color=cfg['trade_colors'][ti], linestyle='--', alpha=0.4, linewidth=0.8)
                y_pos = y_hi * 0.78 - (ti % 3) * (y_hi * 0.06)
                ax_r.annotate(cfg['trade_labels'][ti], xy=(td, port_cum[td] * 100),
                              xytext=(td + N * 0.02, y_pos),
                              color=cfg['trade_colors'][ti], fontsize=5.5,
                              fontfamily='monospace', alpha=0.8,
                              arrowprops=dict(arrowstyle='->', color=cfg['trade_colors'][ti], alpha=0.3))

        ax_r.axhline(0, color=DIM, linewidth=0.6, linestyle='--', alpha=0.4)
        ax_r.legend(fontsize=8, loc='upper left', facecolor=BG2,
                    edgecolor=GRID_C, labelcolor=TEXT)
        if d > 1:
            ax_r.axvline(d, color='white', linewidth=1.2, alpha=0.6, linestyle=':')

        # --- Drawdown ---
        port_peak = np.maximum.accumulate(1 + port_cum[:d])
        port_dd = (port_peak - (1 + port_cum[:d])) / port_peak
        sp_peak = np.maximum.accumulate(1 + sp_cum[:d])
        sp_dd = (sp_peak - (1 + sp_cum[:d])) / sp_peak

        ax_dd = fig.add_subplot(gs_left[1])
        ax_dd.set_facecolor(BG2)
        ax_dd.grid(True, color=GRID_C, alpha=0.3, linewidth=0.5)
        for spine in ax_dd.spines.values():
            spine.set_color(GRID_C)
        ax_dd.tick_params(colors=DIM, labelsize=6)
        ax_dd.set_xlabel(cfg['x_label'], color=DIM, fontsize=8)
        ax_dd.set_ylabel('Drawdown (%)', color=DIM, fontsize=8)
        ax_dd.set_xlim(0, N)

        max_dd = max(sp_dd.max(), port_dd.max()) if d > 1 else 0.01
        ax_dd.set_ylim(-max_dd * 130, 2)

        ax_dd.fill_between(x, -sp_dd * 100, color=RED, alpha=0.25, label='S&P 500')
        ax_dd.fill_between(x, -port_dd * 100, color=GREEN, alpha=0.25, label='Strategy')
        ax_dd.plot(x, -sp_dd * 100, color=RED, linewidth=0.8, alpha=0.6)
        ax_dd.plot(x, -port_dd * 100, color=GREEN, linewidth=1.2)
        ax_dd.axhline(0, color=DIM, linewidth=0.4)
        ax_dd.legend(fontsize=6, loc='lower left', facecolor=BG2,
                     edgecolor=GRID_C, labelcolor=TEXT)

        # ===== RIGHT: 3D Surface =====
        ax3d = fig.add_subplot(gs_bottom[1], projection='3d')
        ax3d.set_facecolor(BG)
        ax3d.xaxis.pane.fill = False
        ax3d.yaxis.pane.fill = False
        ax3d.zaxis.pane.fill = False
        ax3d.xaxis.pane.set_edgecolor('#1a1a2e')
        ax3d.yaxis.pane.set_edgecolor('#1a1a2e')
        ax3d.zaxis.pane.set_edgecolor('#1a1a2e')
        ax3d.grid(True, color='#223344', alpha=0.2)
        ax3d.tick_params(colors='#556677', labelsize=6)

        t = bar_to_regime_t(d)
        t = min(t, n_reg - 1 - 1e-6)
        ri_a = min(int(t), n_reg - 2)
        ri_b = ri_a + 1
        bt = smoothstep(t - ri_a)

        Z_a = surfaces[ri_a](X, Y)
        Z_b = surfaces[ri_b](X, Y)
        Z = Z_a * (1 - bt) + Z_b * bt

        z_min, z_max = Z.min(), Z.max()
        z_range = z_max - z_min if z_max > z_min else 1.0
        z_norm = (Z - z_min) / z_range
        face_colors = blend_cmap(REGIME_CMAPS[ri_a], REGIME_CMAPS[ri_b], bt, z_norm)

        ax3d.plot_surface(X, Y, Z, facecolors=face_colors, alpha=0.92,
                          rstride=1, cstride=1, edgecolor='none',
                          antialiased=True, shade=True)
        ax3d.plot_wireframe(X[::4, ::4], Y[::4, ::4], Z[::4, ::4],
                            color='white', alpha=0.05, linewidth=0.3)

        z_floor = z_min - z_range * 0.15
        try:
            ax3d.contour(X, Y, Z, levels=np.linspace(z_min, z_max, 8),
                         zdir='z', offset=z_floor, cmap=REGIME_CMAPS[ri_a],
                         alpha=0.3, linewidths=0.5)
        except Exception:
            pass

        ax3d.set_zlim(z_floor, z_max + z_range * 0.1)
        ax3d.set_xlabel('Spot ($)', color='#667788', fontsize=8, labelpad=6)
        ax3d.set_ylabel('Vol (%)', color='#667788', fontsize=8, labelpad=6)
        ax3d.set_zlabel('P&L ($K)', color='#667788', fontsize=8, labelpad=6)

        elev = elevations[ri_a] * (1 - bt) + elevations[ri_b] * bt
        azim = 215 + fi * 1.5
        ax3d.view_init(elev=elev, azim=azim)

        ax3d.set_title(f'Regime: {rn}\nP&L Surface ({cfg["total_label"]})',
                       color=rc, fontsize=11, fontweight='bold',
                       fontfamily='monospace', pad=8)

        frames_list.append(fig_to_img(fig, 1800, 820))
        plt.close(fig)
        print(f'  [{tf_key}] Frame {fi + 1}/{TOTAL_FRAMES}')

    save_gif(frames_list, OUT_DIR / cfg['filename'], duration=120)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------
if __name__ == '__main__':
    print(f'Output: {OUT_DIR}')
    generate_dashboard('daily')
    generate_dashboard('hourly')
    generate_dashboard('minute')
    print('\nAll combined dashboards generated.')
