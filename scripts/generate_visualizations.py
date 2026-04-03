#!/usr/bin/env python3
"""
Generate animated GIF visualizations for the Stress-Testing-Engine README.
Produces 4 GIFs showing 3D P&L surfaces, dashboards, stress tests, and heatmaps.
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

FIG_W, FIG_H, DPI = 8, 6, 100
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
    # Resize to exact 800x600
    img = img.resize((800, 600), Image.LANCZOS)
    return img


def _save_gif(frames, path, duration=FRAME_DURATION):
    """Save list of PIL images as an animated GIF."""
    # Convert RGBA -> RGB on dark bg for GIF compatibility
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
# 1. Regime Cycle 3D Surface
# ===================================================================
def generate_regime_cycle():
    print('Generating regime_cycle_3d.gif ...')
    n = 40  # grid size
    spot = np.linspace(80, 120, n)
    ivol = np.linspace(10, 60, n)
    X, Y = np.meshgrid(spot, ivol)

    regimes = [
        ('Bull Quiet',    'VIX 12 | Signal: HOLD',  GREEN),
        ('Transition',    'VIX 24 | Signal: HEDGE',  YELLOW),
        ('Bear / Crisis', 'VIX 67 | Signal: EXIT',   RED),
        ('Recovery',      'VIX 28 | Signal: ENTER',  CYAN),
        ('New Bull',      'VIX 14 | Signal: HOLD',   GREEN),
    ]
    frames_per = 12
    total = frames_per * len(regimes)

    def surface(t):
        """Return Z values for normalised time t in [0,1] across all regimes."""
        phase = t * len(regimes)
        idx = min(int(phase), len(regimes) - 1)
        local = phase - idx

        # Base shapes per regime
        def _bull(X, Y):
            return 20 - 0.01 * (X - 100)**2 - 0.005 * (Y - 20)**2

        def _transition(X, Y, ripple):
            base = 10 - 0.008 * (X - 100)**2 - 0.004 * (Y - 30)**2
            return base + ripple * 3 * np.sin(0.3 * X) * np.cos(0.2 * Y)

        def _crisis(X, Y):
            return -15 + 0.005 * (X - 95)**2 + 0.003 * (Y - 50)**2 - 8 * np.exp(-0.01 * ((X - 90)**2 + (Y - 55)**2))

        def _recovery(X, Y, frac):
            return (-5 + 15 * frac) - 0.006 * (X - 100)**2 - 0.004 * (Y - 35)**2

        if idx == 0:
            return _bull(X, Y)
        elif idx == 1:
            return (1 - local) * _bull(X, Y) + local * _transition(X, Y, local)
        elif idx == 2:
            return (1 - local) * _transition(X, Y, 1.0) + local * _crisis(X, Y)
        elif idx == 3:
            return (1 - local) * _crisis(X, Y) + local * _recovery(X, Y, local)
        else:
            return (1 - local) * _recovery(X, Y, 1.0) + local * _bull(X, Y)

    frames = []
    for i in range(total):
        t = i / total
        Z = surface(t)
        regime_idx = min(int(t * len(regimes)), len(regimes) - 1)
        rname, rsub, rcol = regimes[regime_idx]

        fig = plt.figure(figsize=(FIG_W, FIG_H), facecolor=BG_COLOR)
        ax = fig.add_subplot(111, projection='3d')
        _style_ax(ax, xlabel='Spot Price', ylabel='Implied Vol (%)', is_3d=True)

        norm = Normalize(vmin=-25, vmax=25)
        ax.plot_surface(X, Y, Z, cmap=PNL_CMAP, norm=norm, alpha=0.92,
                        edgecolor='none', rstride=2, cstride=2,
                        antialiased=True)
        ax.set_zlim(-30, 30)
        ax.view_init(elev=28, azim=220 + i * 2)

        ax.set_title(f'Regime: {rname}\n{rsub}',
                     color=rcol, fontsize=13, fontweight='bold', pad=12)

        frames.append(_fig_to_image(fig))
        plt.close(fig)

    _save_gif(frames, OUT_DIR / 'regime_cycle_3d.gif')


# ===================================================================
# 2. Early Warning Dashboard
# ===================================================================
def generate_early_warning():
    print('Generating early_warning_dashboard.gif ...')
    total = 40
    t_arr = np.linspace(0, 1, total)

    # Simulated series
    crisis_prob = 5 + 84 * np.exp(-((t_arr - 0.45)**2) / 0.02)
    crisis_prob = np.clip(crisis_prob, 5, 89)
    vix = 12 + 55 * np.exp(-((t_arr - 0.45)**2) / 0.025)
    vix = np.clip(vix, 12, 67)

    # Portfolio allocations over time
    equity_frac = np.clip(0.6 - 0.5 * np.exp(-((t_arr - 0.45)**2) / 0.03), 0.1, 0.6)
    cash_frac = np.clip(0.1 + 0.5 * np.exp(-((t_arr - 0.45)**2) / 0.03), 0.1, 0.6)
    options_frac = 1.0 - equity_frac - cash_frac

    # Cumulative returns
    days = np.arange(252)
    sp500_daily = np.concatenate([
        np.random.RandomState(42).normal(0.0005, 0.008, 100),
        np.random.RandomState(42).normal(-0.015, 0.03, 30),
        np.random.RandomState(42).normal(0.001, 0.01, 122),
    ])
    sp500_cum = np.cumprod(1 + sp500_daily) - 1

    # Portfolio avoids crash via hedging
    port_daily = sp500_daily.copy()
    port_daily[90:130] = port_daily[90:130] * 0.25 + 0.001
    port_cum = np.cumprod(1 + port_daily) - 1

    frames = []
    for fi in range(total):
        fig, axes = plt.subplots(2, 2, figsize=(FIG_W, FIG_H), facecolor=BG_COLOR)
        fig.subplots_adjust(hspace=0.45, wspace=0.35)

        # -- Top-left: crisis probability gauge --
        ax = axes[0, 0]
        _style_ax(ax, title='Crisis Probability', xlabel='', ylabel='%')
        prob = crisis_prob[fi]
        color = GREEN if prob < 30 else (YELLOW if prob < 60 else RED)
        ax.barh([0], [prob], color=color, height=0.5, alpha=0.85)
        ax.set_xlim(0, 100)
        ax.set_yticks([])
        ax.text(prob + 2, 0, f'{prob:.0f}%', color=color, fontsize=14,
                fontweight='bold', va='center')

        # -- Top-right: VIX trajectory --
        ax = axes[0, 1]
        _style_ax(ax, title='VIX Index', xlabel='Time', ylabel='VIX')
        idx = fi + 1
        ax.plot(t_arr[:idx], vix[:idx], color=YELLOW, linewidth=2)
        ax.fill_between(t_arr[:idx], 0, vix[:idx], color=YELLOW, alpha=0.08)
        ax.set_xlim(0, 1)
        ax.set_ylim(0, 80)
        ax.axhline(30, color=RED, linestyle='--', alpha=0.5, linewidth=0.8)
        ax.text(0.02, 32, 'Danger', color=RED, fontsize=7, alpha=0.7)

        # -- Bottom-left: allocation bars --
        ax = axes[1, 0]
        _style_ax(ax, title='Portfolio Allocation', xlabel='', ylabel='Weight')
        labels = ['Equity', 'Cash', 'Options']
        vals = [equity_frac[fi], cash_frac[fi], options_frac[fi]]
        colors = [BLUE, GREEN, CYAN]
        bars = ax.bar(labels, vals, color=colors, alpha=0.85, width=0.6)
        ax.set_ylim(0, 0.75)
        for b, v in zip(bars, vals):
            ax.text(b.get_x() + b.get_width() / 2, v + 0.02,
                    f'{v:.0%}', ha='center', color=TEXT_COLOR, fontsize=9)

        # -- Bottom-right: cumulative returns --
        ax = axes[1, 1]
        _style_ax(ax, title='Cumulative Returns', xlabel='Day', ylabel='Return')
        day_idx = int(fi / total * 252)
        day_idx = max(day_idx, 2)
        ax.plot(days[:day_idx], sp500_cum[:day_idx], color=RED, linewidth=1.5,
                label='S&P 500', alpha=0.8)
        ax.plot(days[:day_idx], port_cum[:day_idx], color=GREEN, linewidth=2,
                label='Portfolio')
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

    # P&L model: losses from spot drops and vol spikes, with nonlinearity
    Z = (0.8 * X - 0.3 * Y
         - 0.005 * X * Y
         + 0.0003 * X**2
         - 0.0001 * Y**2
         + 2 * np.exp(-0.005 * ((X + 30)**2 + (Y - 40)**2))  # GFC-like pocket
         )

    # Historical scenarios: (spot_shock, vol_shock, label)
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
        _style_ax(ax, title='Stress Test: Portfolio P&L Surface',
                  xlabel='Spot Shock (%)', ylabel='Vol Shock (%)', is_3d=True)

        norm = Normalize(vmin=Z.min(), vmax=Z.max())
        ax.plot_surface(X, Y, Z, cmap=PNL_CMAP, norm=norm, alpha=0.88,
                        rstride=2, cstride=2, edgecolor='none', antialiased=True)

        # Plot scenario markers
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
    states = ['Bull\nQuiet', 'Bull\nVol', 'Neutral', 'Bear\nMild', 'Bear\nCrisis']
    n_states = len(states)
    total = 30

    # Base transition matrix (rows=from, cols=to)
    base = np.array([
        [0.70, 0.15, 0.10, 0.04, 0.01],
        [0.20, 0.45, 0.20, 0.10, 0.05],
        [0.10, 0.15, 0.50, 0.15, 0.10],
        [0.05, 0.05, 0.20, 0.50, 0.20],
        [0.02, 0.03, 0.10, 0.30, 0.55],
    ])

    # Current state cycles through regimes
    state_seq = np.array([0]*6 + [1]*6 + [2]*6 + [3]*6 + [4]*6)

    frames = []
    for fi in range(total):
        current = state_seq[fi]

        # Perturb matrix slightly each frame for animation
        rng = np.random.RandomState(fi)
        noise = rng.uniform(-0.03, 0.03, (n_states, n_states))
        mat = base + noise
        mat = np.clip(mat, 0.01, None)
        mat = mat / mat.sum(axis=1, keepdims=True)

        fig, ax = plt.subplots(figsize=(FIG_W, FIG_H), facecolor=BG_COLOR)
        _style_ax(ax, title=f'HMM Regime Transition Matrix  |  Current State: {states[current].replace(chr(10), " ")}',
                  xlabel='To State', ylabel='From State')

        im = ax.imshow(mat, cmap=NEON_CMAP, vmin=0, vmax=0.75, aspect='auto')

        ax.set_xticks(range(n_states))
        ax.set_yticks(range(n_states))
        ax.set_xticklabels(states, fontsize=8, color=TEXT_COLOR)
        ax.set_yticklabels(states, fontsize=8, color=TEXT_COLOR)

        # Annotate cells
        for r in range(n_states):
            for c in range(n_states):
                val = mat[r, c]
                txt_col = BG_COLOR if val > 0.35 else TEXT_COLOR
                ax.text(c, r, f'{val:.2f}', ha='center', va='center',
                        color=txt_col, fontsize=9, fontweight='bold')

        # Highlight current state row and column
        for edge in range(n_states):
            rect = plt.Rectangle((edge - 0.5, current - 0.5), 1, 1,
                                 linewidth=2, edgecolor=CYAN, facecolor='none')
            ax.add_patch(rect)
            rect2 = plt.Rectangle((current - 0.5, edge - 0.5), 1, 1,
                                  linewidth=2, edgecolor=CYAN, facecolor='none',
                                  linestyle='--', alpha=0.5)
            ax.add_patch(rect2)

        # Colorbar
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
