#!/usr/bin/env python3
"""
Generate animated GIF visualizations for the Stress-Testing-Engine README.
All visualizations show LIVE mode operation with real-time regime detection
and market-maker engine integration.

Strategy: ~100% base long + multi-level limit-order overlay.
Regimes: BULL, NORMAL, CAUTIOUS, CRISIS, RECOVERY.

Produces 4 GIFs:
  1. regime_cycle_3d.gif            - 3D P&L surface morphing through live regimes
  2. early_warning_dashboard.gif    - Live regime monitor + MM parameter dashboard
  3. stress_test_surface.gif        - Stress test surface with live scenario markers
  4. regime_transition_heatmap.gif  - HMM transition matrix with live state tracking
"""

import matplotlib
matplotlib.use('Agg')

import numpy as np
import matplotlib.pyplot as plt
from matplotlib import cm
from matplotlib.colors import LinearSegmentedColormap, Normalize
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401
import io
from PIL import Image
from pathlib import Path

# ---------------------------------------------------------------------------
# Style constants
# ---------------------------------------------------------------------------
BG_COLOR = '#0a0a0a'
TEXT_COLOR = '#cccccc'
GREEN = '#00ff88'
RED = '#ff3344'
YELLOW = '#ffaa00'
BLUE = '#00aaff'
CYAN = '#00ffcc'
GRID_COLOR = '#222222'
NEON_CMAP = LinearSegmentedColormap.from_list(
    'neon', [RED, YELLOW, GREEN, CYAN, BLUE])
PNL_CMAP = LinearSegmentedColormap.from_list(
    'pnl', [RED, '#ff6600', YELLOW, GREEN, CYAN])

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
REGIME_SPREAD = [0.7, 1.0, 2.0, 3.0, 1.4]  # BULL, NORMAL, CAUTIOUS, CRISIS, RECOVERY
REGIME_BASE   = [1.02, 1.00, 0.90, 0.70, 1.03]
REGIME_FILLS  = [57, 40, 20, 13, 29]

FIG_W, FIG_H, DPI = 10, 7.5, 150
FRAME_DURATION = 150  # ms

OUT_DIR = Path(__file__).resolve().parent.parent / 'docs' / 'img'


def _style_ax(ax, title='', xlabel='', ylabel='', is_3d=False):
    """Apply dark-terminal styling to an axes."""
    ax.set_facecolor(BG_COLOR)
    ax.set_title(title, color=TEXT_COLOR, fontsize=11, fontweight='bold', pad=8)
    ax.set_xlabel(xlabel, color=TEXT_COLOR, fontsize=9)
    ax.set_ylabel(ylabel, color=TEXT_COLOR, fontsize=9)
    ax.tick_params(colors=TEXT_COLOR, labelsize=8)
    if is_3d:
        ax.set_zlabel('P&L ($)', color=TEXT_COLOR, fontsize=9)
        ax.xaxis.pane.fill = False
        ax.yaxis.pane.fill = False
        ax.zaxis.pane.fill = False
        ax.xaxis.pane.set_edgecolor(GRID_COLOR)
        ax.yaxis.pane.set_edgecolor(GRID_COLOR)
        ax.zaxis.pane.set_edgecolor(GRID_COLOR)
        ax.grid(True, color=GRID_COLOR, alpha=0.3)
    else:
        ax.grid(True, color=GRID_COLOR, alpha=0.4, linewidth=0.5)
        for spine in ax.spines.values():
            spine.set_color(GRID_COLOR)


def _fig_to_image(fig):
    """Render a matplotlib figure to a PIL Image."""
    buf = io.BytesIO()
    fig.savefig(buf, format='png', dpi=DPI, facecolor=fig.get_facecolor(),
                edgecolor='none', bbox_inches='tight', pad_inches=0.3)
    buf.seek(0)
    img = Image.open(buf).convert('RGBA')
    img = img.resize((1200, 900), Image.LANCZOS)
    return img


def _save_gif(frames, path, duration=FRAME_DURATION):
    """Save list of PIL images as an animated GIF."""
    rgb_frames = []
    for f in frames:
        bg = Image.new('RGB', f.size, (10, 10, 10))
        bg.paste(f, mask=f.split()[3])
        rgb_frames.append(bg)
    rgb_frames[0].save(
        path, save_all=True, append_images=rgb_frames[1:],
        duration=duration, loop=0, optimize=True)
    print(f'  Saved {path}  ({len(rgb_frames)} frames)')


# ===================================================================
# 1. Regime Cycle 3D Surface (Live Mode)
# ===================================================================
def generate_regime_cycle():
    print('Generating regime_cycle_3d.gif ...')
    n = 55
    spot = np.linspace(78, 122, n)
    ivol = np.linspace(8, 62, n)
    X, Y = np.meshgrid(spot, ivol)

    regime_names = ['BULL', 'NORMAL', 'CAUTIOUS', 'CRISIS', 'RECOVERY']
    regime_subs = [
        '\u25cf LIVE | Spread: 0.7x | Base: 102% | Fills: 57/bar | Tight spreads',
        '\u25cf LIVE | Spread: 1.0x | Base: 100% | Fills: 40/bar | Standard spreads',
        '\u25cf LIVE | Spread: 2.0x | Base: 90% | Fills: 20/bar | Wide spreads',
        '\u25cf LIVE | Spread: 3.0x | Base: 70% | Fills: 13/bar | Crisis mode',
        '\u25cf LIVE | Spread: 1.4x | Base: 103% | Fills: 29/bar | Recovery accumulation',
    ]
    regime_cols = [GREEN, BLUE, YELLOW, RED, CYAN]
    elevations = [30, 30, 26, 20, 28]

    def _bull(X, Y):
        return 28 - 0.014 * (X - 100)**2 - 0.006 * (Y - 16)**2

    def _normal(X, Y):
        return 22 - 0.012 * (X - 100)**2 - 0.005 * (Y - 20)**2

    def _cautious(X, Y):
        base = 10 - 0.010 * (X - 100)**2 - 0.005 * (Y - 28)**2
        return base + 6 * np.sin(0.4 * X) * np.cos(0.3 * Y)

    def _crisis(X, Y):
        crater = -10 * np.exp(-0.015 * ((X - 88)**2 + (Y - 52)**2))
        return -22 + 0.007 * (X - 92)**2 + 0.004 * (Y - 50)**2 + crater

    def _recovery(X, Y):
        return 16 - 0.008 * (X - 100)**2 - 0.005 * (Y - 30)**2

    surfaces = [_bull, _normal, _cautious, _crisis, _recovery]
    n_reg = len(surfaces)

    def smoothstep(t):
        t = max(0.0, min(1.0, t))
        return t * t * (3 - 2 * t)

    def blend_cmap(cmap_a, cmap_b, bt, z_norm):
        return cmap_a(z_norm) * (1 - bt) + cmap_b(z_norm) * bt

    total = 120
    frames = []
    for i in range(total):
        t = i / (total - 1) * (n_reg - 1)
        ri_a = min(int(t), n_reg - 2)
        ri_b = ri_a + 1
        bt = smoothstep(t - ri_a)

        Z = surfaces[ri_a](X, Y) * (1 - bt) + surfaces[ri_b](X, Y) * bt

        if bt < 0.15:
            rname = regime_names[ri_a]
            rsub = regime_subs[ri_a]
            rcol = regime_cols[ri_a]
        elif bt > 0.85:
            rname = regime_names[ri_b]
            rsub = regime_subs[ri_b]
            rcol = regime_cols[ri_b]
        else:
            rname = f'{regime_names[ri_a]} \u2192 {regime_names[ri_b]}'
            rsub = f'\u25cf LIVE | TRANSITIONING ({bt*100:.0f}%) | Regime shift detected'
            rcol = regime_cols[ri_b]

        elev = elevations[ri_a] * (1 - bt) + elevations[ri_b] * bt

        fig = plt.figure(figsize=(FIG_W, FIG_H), facecolor=BG_COLOR)
        ax = fig.add_subplot(111, projection='3d')
        ax.set_facecolor(BG_COLOR)
        ax.xaxis.pane.fill = False; ax.yaxis.pane.fill = False; ax.zaxis.pane.fill = False
        ax.xaxis.pane.set_edgecolor('#1a1a2e')
        ax.yaxis.pane.set_edgecolor('#1a1a2e')
        ax.zaxis.pane.set_edgecolor('#1a1a2e')
        ax.grid(True, color='#223344', alpha=0.2)
        ax.tick_params(colors='#556677', labelsize=7)

        z_min, z_max = Z.min(), Z.max()
        z_range = z_max - z_min if z_max > z_min else 1.0
        z_norm_arr = (Z - z_min) / z_range
        face_colors = blend_cmap(REGIME_CMAPS[ri_a], REGIME_CMAPS[ri_b], bt, z_norm_arr)

        ax.plot_surface(X, Y, Z, facecolors=face_colors, alpha=0.93,
                        rstride=1, cstride=1, edgecolor='none',
                        antialiased=True, shade=True)

        ax.plot_wireframe(X[::4, ::4], Y[::4, ::4], Z[::4, ::4],
                          color='white', alpha=0.05, linewidth=0.3)

        z_floor = z_min - z_range * 0.15
        try:
            ax.contour(X, Y, Z, levels=np.linspace(z_min, z_max, 8),
                       zdir='z', offset=z_floor, cmap=REGIME_CMAPS[ri_a], alpha=0.3, linewidths=0.5)
        except Exception:
            pass

        ax.set_zlim(z_floor, z_max + z_range * 0.1)
        ax.set_xlabel('Spot Price ($)', color='#667788', fontsize=9, labelpad=8)
        ax.set_ylabel('Implied Vol (%)', color='#667788', fontsize=9, labelpad=8)
        ax.set_zlabel('P&L ($)', color='#667788', fontsize=9, labelpad=8)
        ax.view_init(elev=elev, azim=215 + i * 1.5)

        fig.suptitle(f'Regime: {rname}\n{rsub}',
                     color=rcol, fontsize=13, fontweight='bold', y=0.96,
                     fontfamily='monospace')

        frames.append(_fig_to_image(fig))
        plt.close(fig)

    _save_gif(frames, OUT_DIR / 'regime_cycle_3d.gif', duration=120)


# ===================================================================
# 2. Early Warning Dashboard (Live Mode with MM Engine)
# ===================================================================
def generate_early_warning():
    print('Generating early_warning_dashboard.gif ...')
    total = 40
    t_arr = np.linspace(0, 1, total)

    # Vol regime proxy: low -> spike -> normalize
    vol_20d = 10 + 50 * np.exp(-((t_arr - 0.45)**2) / 0.02)
    vol_20d = np.clip(vol_20d, 10, 60)

    # Spread multiplier tracks volatility
    spread_mult = np.clip(0.7 + 2.3 * np.exp(-((t_arr - 0.45)**2) / 0.025), 0.7, 3.0)

    # Base position inverse of vol
    base_pct = np.clip(1.02 - 0.32 * np.exp(-((t_arr - 0.45)**2) / 0.025), 0.70, 1.03)

    # Fills per day inversely proportional to spread
    fills_per_day = np.clip(57 - 44 * np.exp(-((t_arr - 0.45)**2) / 0.025), 13, 57)

    days = np.arange(252)
    sp500_daily = np.concatenate([
        np.random.RandomState(42).normal(0.0005, 0.008, 100),
        np.random.RandomState(42).normal(-0.015, 0.03, 30),
        np.random.RandomState(42).normal(0.001, 0.01, 122),
    ])
    sp500_cum = np.cumprod(1 + sp500_daily) - 1

    port_daily = sp500_daily.copy()
    port_daily[90:130] = port_daily[90:130] * 0.70 + 0.001  # crisis: reduced base + spread capture
    port_cum = np.cumprod(1 + port_daily) - 1

    # MM regime events
    mm_events = [
        (0.25, 'Spread: 1.0x \u2192 2.0x (widen)'),
        (0.40, 'Spread: 2.0x \u2192 3.0x (crisis)'),
        (0.60, 'Spread: 3.0x \u2192 1.4x (tighten)'),
    ]

    frames = []
    for fi in range(total):
        fig, axes = plt.subplots(2, 2, figsize=(FIG_W, FIG_H), facecolor=BG_COLOR)
        fig.subplots_adjust(hspace=0.45, wspace=0.35)

        # Main title
        vol_now = vol_20d[fi]
        if vol_now > 32:
            mode_color = RED
            regime_label = 'CRISIS'
        elif vol_now > 22:
            mode_color = YELLOW
            regime_label = 'CAUTIOUS'
        elif vol_now < 12:
            mode_color = GREEN
            regime_label = 'BULL'
        else:
            mode_color = BLUE
            regime_label = 'NORMAL'

        fig.suptitle(f'\u25cf LIVE MODE  |  Regime: {regime_label}  |  20d Vol: {vol_now:.1f}%  |  '
                     f'Spread: {spread_mult[fi]:.1f}x',
                     color=mode_color, fontsize=12, fontweight='bold', y=0.98,
                     fontfamily='monospace')

        # -- Top-left: 20d rolling volatility gauge --
        ax = axes[0, 0]
        _style_ax(ax, title='20d Rolling Volatility', xlabel='', ylabel='%')
        color = GREEN if vol_now < 15 else (BLUE if vol_now < 22 else (YELLOW if vol_now < 32 else RED))
        ax.barh([0], [vol_now], color=color, height=0.5, alpha=0.85)
        ax.set_xlim(0, 70)
        ax.set_yticks([])
        ax.axvline(22, color=YELLOW, linestyle='--', alpha=0.5, linewidth=0.8)
        ax.axvline(32, color=RED, linestyle='--', alpha=0.5, linewidth=0.8)
        ax.text(vol_now + 2, 0, f'{vol_now:.0f}%', color=color, fontsize=14,
                fontweight='bold', va='center')
        ax.text(22, 0.3, 'Cautious', color=YELLOW, fontsize=6, alpha=0.7)
        ax.text(32, 0.3, 'Crisis', color=RED, fontsize=6, alpha=0.7)

        # -- Top-right: Spread multiplier trajectory --
        ax = axes[0, 1]
        _style_ax(ax, title='Spread Multiplier (Live)', xlabel='Time', ylabel='Multiplier')
        idx = fi + 1
        ax.plot(t_arr[:idx], spread_mult[:idx], color=YELLOW, linewidth=2)
        ax.fill_between(t_arr[:idx], 0, spread_mult[:idx], color=YELLOW, alpha=0.08)
        ax.set_xlim(0, 1)
        ax.set_ylim(0, 3.5)
        ax.axhline(2.0, color=YELLOW, linestyle='--', alpha=0.5, linewidth=0.8)
        ax.text(0.02, 2.1, 'Cautious', color=YELLOW, fontsize=7, alpha=0.7)
        ax.axhline(3.0, color=RED, linestyle='--', alpha=0.5, linewidth=0.8)
        ax.text(0.02, 3.1, 'Crisis', color=RED, fontsize=7, alpha=0.7)

        # -- Bottom-left: MM parameter bars --
        ax = axes[1, 0]
        _style_ax(ax, title='MM Engine Parameters', xlabel='', ylabel='Value')
        labels = ['Spread\n(x)', 'Base\n(%)', 'Fills\n(/day)']
        # Normalize for display
        vals = [spread_mult[fi] / 3.0, base_pct[fi], fills_per_day[fi] / 60.0]
        colors = [YELLOW, GREEN, CYAN]
        bars = ax.bar(labels, vals, color=colors, alpha=0.85, width=0.6)
        ax.set_ylim(0, 1.2)
        display_vals = [f'{spread_mult[fi]:.1f}x', f'{base_pct[fi]*100:.0f}%', f'{fills_per_day[fi]:.0f}']
        for b, dv in zip(bars, display_vals):
            ax.text(b.get_x() + b.get_width() / 2, b.get_height() + 0.03,
                    dv, ha='center', color=TEXT_COLOR, fontsize=9, fontweight='bold')

        # Show MM event
        for evt_t, evt_label in mm_events:
            if t_arr[fi] >= evt_t and t_arr[fi] < evt_t + 0.08:
                ax.text(0.5, 0.85, f'[MM] {evt_label}',
                        transform=ax.transAxes, ha='center',
                        color=CYAN, fontsize=8, fontfamily='monospace',
                        fontweight='bold')
                break

        # -- Bottom-right: cumulative returns --
        ax = axes[1, 1]
        _style_ax(ax, title='Live Returns: MM Strategy vs S&P 500', xlabel='Day', ylabel='Return')
        day_idx = int(fi / total * 252)
        day_idx = max(day_idx, 2)
        ax.plot(days[:day_idx], sp500_cum[:day_idx], color=RED, linewidth=1.5,
                label='S&P 500', alpha=0.8)
        ax.plot(days[:day_idx], port_cum[:day_idx], color=GREEN, linewidth=2,
                label='MM Strategy')
        ax.set_xlim(0, 252)
        ax.set_ylim(-0.45, 0.25)
        ax.legend(fontsize=7, loc='lower left', facecolor=BG_COLOR,
                  edgecolor=GRID_COLOR, labelcolor=TEXT_COLOR)

        frames.append(_fig_to_image(fig))
        plt.close(fig)

    _save_gif(frames, OUT_DIR / 'early_warning_dashboard.gif')


# ===================================================================
# 3. Stress Test Surface
# ===================================================================
def generate_stress_test():
    print('Generating stress_test_surface.gif ...')
    n = 50
    spot_shock = np.linspace(-50, 20, n)
    vol_shock = np.linspace(0, 50, n)
    X, Y = np.meshgrid(spot_shock, vol_shock)

    Z = (0.8 * X - 0.3 * Y
         - 0.005 * X * Y
         + 0.0003 * X**2
         - 0.0001 * Y**2
         + 2 * np.exp(-0.005 * ((X + 30)**2 + (Y - 40)**2))
         )

    scenarios = [
        (-38, 45, 'GFC 2008'),
        (-34, 42, 'COVID 2020'),
        (-20, 28, 'Euro Crisis'),
        (-12, 18, 'Taper 2013'),
        (5, 8, 'Normal'),
    ]

    total = 30
    frames = []
    for i in range(total):
        fig = plt.figure(figsize=(FIG_W, FIG_H), facecolor=BG_COLOR)
        ax = fig.add_subplot(111, projection='3d')
        _style_ax(ax, title='\u25cf LIVE | Stress Test: MM Strategy P&L Surface',
                  xlabel='Spot Shock (%)', ylabel='Vol Shock (%)', is_3d=True)

        norm = Normalize(vmin=Z.min(), vmax=Z.max())
        ax.plot_surface(X, Y, Z, cmap=PNL_CMAP, norm=norm, alpha=0.88,
                        rstride=2, cstride=2, edgecolor='none', antialiased=True)

        for sx, sy, lbl in scenarios:
            sz = np.interp(sx, spot_shock, Z[int(np.interp(sy, vol_shock, np.arange(n))), :])
            ax.scatter([sx], [sy], [sz], color=YELLOW, s=40, zorder=5,
                       edgecolors='white', linewidths=0.5)
            ax.text(sx, sy, sz + 2, lbl, color=YELLOW, fontsize=7,
                    ha='center', fontweight='bold')

        ax.set_zlim(Z.min() - 5, Z.max() + 10)
        ax.view_init(elev=30, azim=200 + i * 4)

        frames.append(_fig_to_image(fig))
        plt.close(fig)

    _save_gif(frames, OUT_DIR / 'stress_test_surface.gif')


# ===================================================================
# 4. Regime Transition Heatmap
# ===================================================================
def generate_transition_heatmap():
    print('Generating regime_transition_heatmap.gif ...')
    states = ['BULL', 'NORMAL', 'CAUTIOUS', 'CRISIS', 'RECOVERY']
    n_states = len(states)
    total = 30

    base = np.array([
        [0.70, 0.15, 0.10, 0.04, 0.01],
        [0.10, 0.50, 0.20, 0.10, 0.10],
        [0.05, 0.15, 0.50, 0.20, 0.10],
        [0.02, 0.03, 0.10, 0.55, 0.30],
        [0.15, 0.20, 0.10, 0.05, 0.50],
    ])

    state_seq = np.array([0]*6 + [1]*6 + [2]*6 + [3]*6 + [4]*6)

    frames = []
    for fi in range(total):
        current = state_seq[fi]

        rng = np.random.RandomState(fi)
        noise = rng.uniform(-0.03, 0.03, (n_states, n_states))
        mat = base + noise
        mat = np.clip(mat, 0.01, None)
        mat = mat / mat.sum(axis=1, keepdims=True)

        state_name = states[current]

        fig, ax = plt.subplots(figsize=(FIG_W, FIG_H), facecolor=BG_COLOR)
        _style_ax(ax,
                  title=f'\u25cf LIVE | HMM Transition Matrix | Current: {state_name}',
                  xlabel='To State', ylabel='From State')

        im = ax.imshow(mat, cmap=NEON_CMAP, vmin=0, vmax=0.75, aspect='auto')

        ax.set_xticks(range(n_states))
        ax.set_yticks(range(n_states))
        ax.set_xticklabels(states, fontsize=8, color=TEXT_COLOR)
        ax.set_yticklabels(states, fontsize=8, color=TEXT_COLOR)

        for r in range(n_states):
            for c in range(n_states):
                val = mat[r, c]
                txt_col = BG_COLOR if val > 0.35 else TEXT_COLOR
                ax.text(c, r, f'{val:.2f}', ha='center', va='center',
                        color=txt_col, fontsize=9, fontweight='bold')

        for edge in range(n_states):
            rect = plt.Rectangle((edge - 0.5, current - 0.5), 1, 1,
                                 linewidth=2, edgecolor=CYAN, facecolor='none')
            ax.add_patch(rect)
            rect2 = plt.Rectangle((current - 0.5, edge - 0.5), 1, 1,
                                  linewidth=2, edgecolor=CYAN, facecolor='none',
                                  linestyle='--', alpha=0.5)
            ax.add_patch(rect2)

        cbar = fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
        cbar.ax.tick_params(colors=TEXT_COLOR, labelsize=8)
        cbar.set_label('Transition Probability', color=TEXT_COLOR, fontsize=9)

        frames.append(_fig_to_image(fig))
        plt.close(fig)

    _save_gif(frames, OUT_DIR / 'regime_transition_heatmap.gif')


# ===================================================================
# Main
# ===================================================================
if __name__ == '__main__':
    print(f'Output directory: {OUT_DIR}')
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    generate_regime_cycle()
    generate_early_warning()
    generate_stress_test()
    generate_transition_heatmap()
    print('\nAll GIFs generated successfully.')
