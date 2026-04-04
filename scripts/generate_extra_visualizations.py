#!/usr/bin/env python3
"""
Generate animated GIFs showing LIVE mode operation:
  1. regime_phases_comparison.gif  - 3D surface morphing through live regime detection
  2. performance_vs_sp500.gif     - Live portfolio vs S&P 500 with execution engine trades
"""

import matplotlib
matplotlib.use('Agg')

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap, Normalize
from matplotlib.patches import FancyBboxPatch
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

OUT_DIR = Path(__file__).resolve().parent.parent / 'docs' / 'img'
OUT_DIR.mkdir(parents=True, exist_ok=True)

DPI = 100
FRAME_MS = 160


def fig_to_img(fig, w=1200, h=700):
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
# 1. Regime Phases Comparison (Live Mode - 3D surface with execution)
# ===================================================================
def generate_regime_phases():
    print('Generating regime_phases_comparison.gif ...')
    n = 35
    spot = np.linspace(80, 120, n)
    ivol = np.linspace(10, 55, n)
    X, Y = np.meshgrid(spot, ivol)

    def bull_quiet(X, Y):
        return 22 - 0.012*(X-100)**2 - 0.005*(Y-18)**2

    def transition(X, Y, ripple=1.0):
        base = 8 - 0.009*(X-100)**2 - 0.004*(Y-28)**2
        return base + ripple*4*np.sin(0.35*X)*np.cos(0.25*Y)

    def crisis(X, Y):
        return -18 + 0.006*(X-92)**2 + 0.003*(Y-50)**2 \
               - 10*np.exp(-0.012*((X-88)**2 + (Y-52)**2))

    def recovery(X, Y, frac=0.5):
        return (-6 + 18*frac) - 0.007*(X-100)**2 - 0.004*(Y-32)**2

    def new_bull(X, Y):
        return 20 - 0.011*(X-102)**2 - 0.005*(Y-16)**2

    phases = [
        ('BULL QUIET',    GREEN,
         'LIVE | SP500: $5,280 | VIX 12 | Signal: STRONG BUY\n'
         '[Exec] BUY 892 SPY @ $528.04 | Account: $1,000,000',
         lambda X,Y,t: bull_quiet(X,Y)),
        ('TRANSITION',    YELLOW,
         'LIVE | SP500: $5,050 | VIX 24 | Signal: REDUCE RISK\n'
         '[Exec] SELL 446 SPY @ $505.12 | Account: $987,420',
         lambda X,Y,t: transition(X,Y, 0.3+0.7*t)),
        ('BEAR VOLATILE', RED,
         'LIVE | SP500: $3,900 | VIX 67 | Signal: CRISIS\n'
         '[Exec] SELL 357 SPY @ $391.88 | Account: $932,180',
         lambda X,Y,t: crisis(X,Y)),
        ('RECOVERY',      CYAN,
         'LIVE | SP500: $4,500 | VIX 28 | Signal: BUY\n'
         '[Exec] BUY 663 SPY @ $450.22 | Account: $961,540',
         lambda X,Y,t: recovery(X,Y, 0.2+0.8*t)),
        ('NEW BULL',      GREEN,
         'LIVE | SP500: $5,600 | VIX 14 | Signal: STRONG BUY\n'
         '[Exec] BUY 224 SPY @ $560.15 | Account: $1,148,920',
         lambda X,Y,t: new_bull(X,Y)),
    ]

    frames_per_phase = 14
    total = frames_per_phase * len(phases)

    tl_starts = [0, 0.16, 0.25, 0.37, 0.53]
    tl_ends   = [0.16, 0.25, 0.37, 0.53, 1.0]
    tl_colors = [GREEN, YELLOW, RED, CYAN, GREEN]
    tl_labels = ['Bull', 'Trans', 'Crisis', 'Recov', 'New Bull']

    frames = []
    for fi in range(total):
        phase_idx = fi // frames_per_phase
        local_t = (fi % frames_per_phase) / (frames_per_phase - 1)
        name, color, info, surf_fn = phases[phase_idx]

        Z = surf_fn(X, Y, local_t)

        fig = plt.figure(figsize=(12, 7), facecolor=BG)
        gs = gridspec.GridSpec(5, 1, height_ratios=[0.6, 0.1, 5.5, 0.5, 0.8],
                               hspace=0.05)

        # --- Title row with LIVE indicator ---
        ax_title = fig.add_subplot(gs[0])
        ax_title.set_facecolor(BG)
        ax_title.axis('off')
        ax_title.text(0.5, 0.5,
                      f'\u25cf LIVE MODE  //  Regime: {name}  //  Execution Engine Active',
                      color=color, fontsize=16, fontweight='bold',
                      ha='center', va='center', transform=ax_title.transAxes,
                      family='monospace')

        ax_sp = fig.add_subplot(gs[1])
        ax_sp.set_facecolor(BG); ax_sp.axis('off')

        # --- 3D Surface ---
        ax3d = fig.add_subplot(gs[2], projection='3d')
        ax3d.set_facecolor(BG)
        ax3d.xaxis.pane.fill = False; ax3d.yaxis.pane.fill = False; ax3d.zaxis.pane.fill = False
        ax3d.xaxis.pane.set_edgecolor(GRID_C)
        ax3d.yaxis.pane.set_edgecolor(GRID_C)
        ax3d.zaxis.pane.set_edgecolor(GRID_C)
        ax3d.grid(True, color=GRID_C, alpha=0.3)
        ax3d.tick_params(colors=DIM, labelsize=7)

        norm = Normalize(vmin=-28, vmax=28)
        ax3d.plot_surface(X, Y, Z, cmap=PNL_CMAP, norm=norm, alpha=0.92,
                          rstride=2, cstride=2, edgecolor='none', antialiased=True)
        ax3d.set_zlim(-32, 32)
        ax3d.set_xlabel('Spot Price ($)', color=DIM, fontsize=8, labelpad=5)
        ax3d.set_ylabel('Implied Vol (%)', color=DIM, fontsize=8, labelpad=5)
        ax3d.set_zlabel('P&L ($K)', color=DIM, fontsize=8, labelpad=5)

        azim = 210 + fi * 1.8
        ax3d.view_init(elev=26, azim=azim)

        # --- Info panel with execution data ---
        ax_info = fig.add_subplot(gs[3])
        ax_info.set_facecolor(BG); ax_info.axis('off')
        ax_info.text(0.5, 0.5, info, color=color, fontsize=10,
                     ha='center', va='center', transform=ax_info.transAxes,
                     family='monospace', alpha=0.9)

        # --- Timeline bar ---
        ax_tl = fig.add_subplot(gs[4])
        ax_tl.set_facecolor(BG); ax_tl.axis('off')
        ax_tl.set_xlim(0, 1); ax_tl.set_ylim(0, 1)

        for k in range(len(phases)):
            x0, x1 = tl_starts[k], tl_ends[k]
            alpha = 0.9 if k == phase_idx else 0.25
            lw = 3 if k == phase_idx else 0
            ec = 'white' if k == phase_idx else 'none'
            rect = plt.Rectangle((x0, 0.3), x1-x0, 0.4,
                                 facecolor=tl_colors[k], alpha=alpha,
                                 edgecolor=ec, linewidth=lw)
            ax_tl.add_patch(rect)
            ax_tl.text((x0+x1)/2, 0.5, tl_labels[k], color='white' if k==phase_idx else DIM,
                       fontsize=8, ha='center', va='center', fontweight='bold',
                       family='monospace')

        global_frac = fi / total
        ax_tl.plot([global_frac], [0.15], marker='^', color='white',
                   markersize=8, transform=ax_tl.transAxes, clip_on=False)

        frames.append(fig_to_img(fig, 1200, 700))
        plt.close(fig)

    save_gif(frames, OUT_DIR / 'regime_phases_comparison.gif', duration=FRAME_MS)


# ===================================================================
# 2. Live Performance vs S&P 500 with Execution Engine Trades
# ===================================================================
def generate_performance_chart():
    print('Generating performance_vs_sp500.gif ...')

    np.random.seed(42)
    days = 756

    # Regime schedule (trading days)
    regime_bounds = [0, 180, 240, 340, 460, days]
    regime_names  = ['Bull Quiet', 'Transition', 'Bear Volatile', 'Recovery', 'New Bull']
    regime_colors = [GREEN, YELLOW, RED, CYAN, GREEN]
    regime_drifts = [0.0005, -0.0002, -0.0028, 0.0015, 0.0006]
    regime_vols   = [0.008, 0.014, 0.032, 0.016, 0.009]

    # S&P 500 daily returns per regime
    sp_ret = np.zeros(days)
    for i in range(len(regime_names)):
        a, b = regime_bounds[i], regime_bounds[i+1]
        sp_ret[a:b] = np.random.normal(regime_drifts[i], regime_vols[i], b-a)

    sp_cum = np.cumprod(1 + sp_ret) - 1

    # Engine portfolio: execution engine hedges during transition/crisis
    port_ret = sp_ret.copy()
    port_ret[180:240] = sp_ret[180:240] * 0.4 + 0.0003   # reduce exposure
    port_ret[240:340] = sp_ret[240:340] * 0.18 + 0.0005   # heavy hedge, 20% exposure
    port_ret[340:460] = sp_ret[340:460] * 1.3 + 0.0003    # re-enter aggressively
    port_ret[460:] = sp_ret[460:] * 1.1 + 0.0002          # full exposure + premium

    port_cum = np.cumprod(1 + port_ret) - 1

    # Drawdown
    port_peak = np.maximum.accumulate(1 + port_cum)
    port_dd = (port_peak - (1 + port_cum)) / port_peak
    sp_peak = np.maximum.accumulate(1 + sp_cum)
    sp_dd = (sp_peak - (1 + sp_cum)) / sp_peak

    # Execution engine trades at regime boundaries
    trade_days = [0, 180, 240, 340, 460]
    trade_labels = [
        'BUY 892 SPY',
        'SELL 446 SPY',
        'SELL 357 SPY\n(CRISIS hedge)',
        'BUY 663 SPY\n(Recovery entry)',
        'BUY 224 SPY',
    ]
    trade_colors = [GREEN, RED, RED, GREEN, GREEN]

    total_frames = 60
    day_indices = np.linspace(5, days, total_frames, dtype=int)

    frames = []
    for fi, d in enumerate(day_indices):
        fig = plt.figure(figsize=(12, 7), facecolor=BG)
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

        ax_t.text(0.5, 0.6,
                  f'\u25cf LIVE  |  PORTFOLIO vs S&P 500  |  Day {d}/{days}  |  Regime: {rn}',
                  color=rc, fontsize=14, fontweight='bold', ha='center', va='center',
                  transform=ax_t.transAxes, family='monospace')
        ax_t.text(0.5, 0.1,
                  f'Portfolio: {port_ret_now:+.1f}%    S&P 500: {sp_ret_now:+.1f}%    Alpha: {alpha_now:+.1f}%',
                  color=TEXT, fontsize=11, ha='center', va='center',
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
        ax_r.plot(x, sp_cum[:d]*100, color=RED, linewidth=1.5, alpha=0.7, label='S&P 500')
        ax_r.fill_between(x, 0, sp_cum[:d]*100, color=RED, alpha=0.04)
        ax_r.plot(x, port_cum[:d]*100, color=GREEN, linewidth=2.5, label='Engine Portfolio')
        ax_r.fill_between(x, 0, port_cum[:d]*100, color=GREEN, alpha=0.06)

        # Mark execution engine trades
        for ti, td in enumerate(trade_days):
            if td < d and td > 0:
                ax_r.axvline(td, color=trade_colors[ti], linestyle='--', alpha=0.5, linewidth=1)
                y_pos = ax_r.get_ylim()[1]*0.85 - (ti % 2)*5
                ax_r.annotate(trade_labels[ti], xy=(td, port_cum[td]*100),
                             xytext=(td+15, y_pos),
                             color=trade_colors[ti], fontsize=6,
                             fontfamily='monospace', alpha=0.8,
                             arrowprops=dict(arrowstyle='->', color=trade_colors[ti], alpha=0.4))

        ax_r.axhline(0, color=DIM, linewidth=0.8, linestyle='--', alpha=0.5)
        ax_r.legend(fontsize=8, loc='upper left', facecolor=BG2,
                    edgecolor=GRID_C, labelcolor=TEXT)

        # --- Stats panel ---
        ax_s = fig.add_subplot(gs[1, 1])
        ax_s.set_facecolor(BG2); ax_s.axis('off')

        stats = [
            ('PORTFOLIO', GREEN),
            (f'Return: {port_ret_now:+.1f}%', GREEN),
            (f'MaxDD: {port_dd[:d].max()*100:.1f}%', YELLOW if port_dd[:d].max()<0.15 else RED),
            ('', TEXT),
            ('S&P 500', RED),
            (f'Return: {sp_ret_now:+.1f}%', RED),
            (f'MaxDD: {sp_dd[:d].max()*100:.1f}%', RED),
            ('', TEXT),
            (f'ALPHA: {alpha_now:+.1f}%', GREEN if alpha_now > 0 else RED),
            ('', TEXT),
            ('EXECUTION', CYAN),
            (f'Trades: {sum(1 for td in trade_days if td < d)}', CYAN),
            ('Mode: Paper', DIM),
        ]

        for si, (txt, col) in enumerate(stats):
            y = 0.95 - si * 0.075
            fs = 11 if si in (0, 4, 8, 10) else 9
            fw = 'bold' if si in (0, 4, 8, 10) else 'normal'
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
        ax_dd.fill_between(x, -port_dd[:d]*100, color=GREEN, alpha=0.3, label='Portfolio DD')
        ax_dd.plot(x, -sp_dd[:d]*100, color=RED, linewidth=1, alpha=0.6)
        ax_dd.plot(x, -port_dd[:d]*100, color=GREEN, linewidth=1.5)
        ax_dd.axhline(0, color=DIM, linewidth=0.5)
        ax_dd.legend(fontsize=7, loc='lower left', facecolor=BG2,
                     edgecolor=GRID_C, labelcolor=TEXT)

        # --- Allocation bar ---
        ax_al = fig.add_subplot(gs[2, 1])
        ax_al.set_facecolor(BG2); ax_al.axis('off')
        ax_al.set_xlim(0, 1); ax_al.set_ylim(0, 1)

        alloc_map = {
            0: (0.15, 0.60, 0.25),
            1: (0.40, 0.40, 0.20),
            2: (0.70, 0.20, 0.10),
            3: (0.25, 0.50, 0.25),
            4: (0.15, 0.60, 0.25),
        }
        cash, eq, opt = alloc_map[cur_regime]

        ax_al.text(0.5, 0.95, 'ALLOCATION', color=TEXT, fontsize=10,
                   fontweight='bold', ha='center', va='top',
                   transform=ax_al.transAxes, family='monospace')

        bar_data = [('Cash', cash, GREEN), ('Equity', eq, BLUE), ('Options', opt, CYAN)]
        for bi, (lbl, val, col) in enumerate(bar_data):
            y = 0.72 - bi * 0.28
            ax_al.add_patch(plt.Rectangle((0.08, y-0.06), 0.84*val, 0.12,
                                          facecolor=col, alpha=0.7,
                                          transform=ax_al.transAxes))
            ax_al.add_patch(plt.Rectangle((0.08, y-0.06), 0.84, 0.12,
                                          facecolor='none', edgecolor=GRID_C,
                                          linewidth=1, transform=ax_al.transAxes))
            ax_al.text(0.06, y, f'{lbl}', color=col, fontsize=8, ha='right',
                       va='center', transform=ax_al.transAxes, family='monospace')
            ax_al.text(0.08 + 0.84*val + 0.02, y, f'{val:.0%}', color=col, fontsize=9,
                       ha='left', va='center', transform=ax_al.transAxes,
                       family='monospace', fontweight='bold')

        frames.append(fig_to_img(fig, 1200, 700))
        plt.close(fig)

    save_gif(frames, OUT_DIR / 'performance_vs_sp500.gif', duration=FRAME_MS)


# ===================================================================
if __name__ == '__main__':
    print(f'Output: {OUT_DIR}')
    generate_regime_phases()
    generate_performance_chart()
    print('\nDone.')
