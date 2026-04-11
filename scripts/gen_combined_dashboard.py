#!/usr/bin/env python3
"""
Generate combined dashboard GIFs with REAL S&P 500 data from yfinance:
  1. combined_dashboard_daily.gif   - Real daily data (~3 years)
  2. combined_dashboard_hourly.gif  - Real hourly data (~60 days)
  3. combined_dashboard_minute.gif  - Real minute data (~5 days)

Left:  S&P 500 (Buy & Hold) vs Market-Maker Strategy with real dates on x-axis
Right: High-resolution 3D P&L surface (80x80 grid) with per-regime colormaps.

Strategy: ~100% base long + multi-level limit-order overlay.
Regimes: BULL, NORMAL, CAUTIOUS, CRISIS, RECOVERY.

Requires: pip install yfinance matplotlib numpy Pillow
"""
import matplotlib
matplotlib.use('Agg')

import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import LinearSegmentedColormap
from matplotlib.dates import DateFormatter, AutoDateLocator
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401
import matplotlib.gridspec as gridspec
import io
from PIL import Image
from pathlib import Path
from datetime import datetime

import yfinance as yf

# ---------------------------------------------------------------------------
# Style
# ---------------------------------------------------------------------------
BG   = '#080810'
BG2  = '#0e0e1a'
TEXT = '#cccccc'
GREEN  = '#00ff88'
RED    = '#ff3344'
YELLOW = '#ffaa00'
BLUE   = '#4488ff'
CYAN   = '#00ffcc'
WHITE  = '#ffffff'
GRID_C = '#1a1a2e'
DIM    = '#556677'
ORANGE = '#ff8800'

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

REGIME_CMAPS = {
    'BULL':      CMAP_BULL,
    'NORMAL':    CMAP_NORMAL,
    'CAUTIOUS':  CMAP_CAUTIOUS,
    'CRISIS':    CMAP_CRISIS,
    'RECOVERY':  CMAP_RECOV,
}
REGIME_COLORS = {
    'BULL': GREEN, 'NORMAL': BLUE, 'CAUTIOUS': YELLOW,
    'CRISIS': RED, 'RECOVERY': CYAN,
}
REGIME_ELEV = {
    'BULL': 30, 'NORMAL': 30, 'CAUTIOUS': 26, 'CRISIS': 20, 'RECOVERY': 28,
}
# MM parameters per regime
REGIME_SPREAD = {
    'BULL': 0.7, 'NORMAL': 1.0, 'CAUTIOUS': 2.0, 'CRISIS': 3.0, 'RECOVERY': 1.4,
}
REGIME_BASE = {
    'BULL': 1.02, 'NORMAL': 1.00, 'CAUTIOUS': 0.90, 'CRISIS': 0.70, 'RECOVERY': 1.03,
}

OUT_DIR = Path(__file__).resolve().parent.parent / 'docs' / 'img'
OUT_DIR.mkdir(parents=True, exist_ok=True)

DPI = 150
TOTAL_FRAMES = 120
N_GRID = 80

# ---------------------------------------------------------------------------
# 3D surfaces per regime
# ---------------------------------------------------------------------------
n_surf = N_GRID
spot_surf = np.linspace(78, 122, n_surf)
ivol_surf = np.linspace(8, 58, n_surf)
X_SURF, Y_SURF = np.meshgrid(spot_surf, ivol_surf)


def _bull_surface(X, Y, fi):
    return 28 - 0.014 * (X - 100)**2 - 0.006 * (Y - 16)**2

def _normal_surface(X, Y, fi):
    return 22 - 0.012 * (X - 100)**2 - 0.005 * (Y - 20)**2

def _cautious_surface(X, Y, fi):
    base = 10 - 0.010 * (X - 100)**2 - 0.005 * (Y - 28)**2
    waves = 6 * np.sin(0.4 * X + fi * 0.08) * np.cos(0.3 * Y + fi * 0.06)
    return base + waves

def _crisis_surface(X, Y, fi):
    crater = -10 * np.exp(-0.015 * ((X - 88)**2 + (Y - 52)**2))
    turb = (np.sin(X / 30 + fi * 0.12) * np.cos(Y / 8 + fi * 0.1)
            + 0.5 * np.sin(X / 15 + fi * 0.15) * np.cos(Y / 5 + fi * 0.12))
    return -22 + 0.007 * (X - 92)**2 + 0.004 * (Y - 50)**2 + crater + turb * 3

def _recovery_surface(X, Y, fi):
    base = 16 - 0.008 * (X - 100)**2 - 0.005 * (Y - 30)**2
    turb = np.sin(X / 35 + fi * 0.06) * np.cos(Y / 6 + fi * 0.04)
    return base + turb * 1.0


REGIME_SURFACES = {
    'BULL':      _bull_surface,
    'NORMAL':    _normal_surface,
    'CAUTIOUS':  _cautious_surface,
    'CRISIS':    _crisis_surface,
    'RECOVERY':  _recovery_surface,
}


def smoothstep(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def blend_cmap(cmap_a, cmap_b, bt, z_norm):
    return cmap_a(z_norm) * (1 - bt) + cmap_b(z_norm) * bt


def classify_regime(returns, idx, lookback=20):
    """Classify regime — same logic as rolling_backtest.py."""
    if idx < 40:
        return 'NORMAL'
    w = returns[max(0, idx - lookback):idx]
    vol = np.std(w, ddof=1) * np.sqrt(252) if len(w) > 2 else 0.10
    mom = np.sum(w)
    w_prev = returns[max(0, idx - 2 * lookback):max(0, idx - lookback)]
    vol_prev = np.std(w_prev, ddof=1) * np.sqrt(252) if len(w_prev) > 2 else vol

    if vol > 0.32 and mom < -0.06:
        return 'CRISIS'
    if vol > 0.22 and mom < 0:
        return 'CAUTIOUS'
    if vol_prev > 0.27 and vol < vol_prev * 0.85 and mom > 0:
        return 'RECOVERY'
    if vol < 0.12 and mom > 0.01:
        return 'BULL'
    return 'NORMAL'


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def fig_to_img(fig, w=1800, h=900):
    buf = io.BytesIO()
    fig.savefig(buf, format='png', dpi=DPI, facecolor=fig.get_facecolor(),
                edgecolor='none', bbox_inches='tight', pad_inches=0.15)
    buf.seek(0)
    img = Image.open(buf).convert('RGBA').resize((w, h), Image.LANCZOS)
    return img


def save_gif(frames, path, duration=220):
    rgb = []
    for f in frames:
        bg = Image.new('RGB', f.size, (8, 8, 16))
        bg.paste(f, mask=f.split()[3])
        rgb.append(bg)
    rgb[0].save(path, save_all=True, append_images=rgb[1:],
                duration=duration, loop=0, optimize=True)
    print(f'  Saved {path}  ({len(rgb)} frames, {path.stat().st_size / 1024:.0f} KB)')


def filter_market_gaps(prices, dates, tf_key):
    if tf_key == 'daily':
        return prices, dates
    from datetime import timedelta
    max_gap = timedelta(hours=2) if tf_key == 'hourly' else timedelta(minutes=5)
    continuous_dates = [dates[0]]
    step = timedelta(hours=1) if tf_key == 'hourly' else timedelta(minutes=1)
    for i in range(1, len(dates)):
        gap = dates[i] - dates[i - 1]
        if gap > max_gap:
            continuous_dates.append(continuous_dates[-1] + step)
        else:
            continuous_dates.append(continuous_dates[-1] + gap)
    return prices, np.array(continuous_dates)


# ---------------------------------------------------------------------------
# Market-maker strategy simulation on real data
# ---------------------------------------------------------------------------
def simulate_mm_strategy(prices, returns):
    """Simulate market-maker overlay on real price data.
    Base ~100% long + spread capture from limit orders, regime-adjusted."""
    N = len(prices)
    port_value = np.ones(N) * prices[0]
    regimes = ['NORMAL'] * N
    spreads = np.ones(N)
    bases = np.ones(N)
    fills_per_day = np.zeros(N)

    for i in range(1, N):
        regime = classify_regime(returns, i)
        regimes[i] = regime
        spread_mult = REGIME_SPREAD[regime]
        base = REGIME_BASE[regime]
        spreads[i] = spread_mult
        bases[i] = base

        # Base position return
        base_pnl = base * returns[i]

        # Spread capture (simplified: proportional to daily range proxy)
        daily_vol = abs(returns[i])
        spread_capture = daily_vol * 0.15 * (1.0 / spread_mult)  # tighter = more fills
        fills_per_day[i] = max(5, 40 * (1.0 / spread_mult))

        port_value[i] = port_value[i - 1] * (1 + base_pnl + spread_capture)

    return port_value, regimes, spreads, bases, fills_per_day


def generate_dashboard(tf_key, prices, dates, timeframe_label, date_fmt, x_label):
    print(f'\nGenerating combined_dashboard_{tf_key}.gif  ({timeframe_label}) ...')

    prices, dates = filter_market_gaps(prices, dates, tf_key)
    N = len(prices)
    returns = np.zeros(N)
    returns[1:] = np.diff(prices) / prices[:-1]

    # S&P 500 buy & hold
    sp_cum = (prices / prices[0] - 1) * 100

    # Market-maker strategy
    port_value, regimes, spreads, bases, fills = simulate_mm_strategy(prices, returns)
    port_cum = (port_value / port_value[0] - 1) * 100

    # Drawdowns
    sp_peak = np.maximum.accumulate(prices)
    sp_dd = (sp_peak - prices) / sp_peak * 100
    port_peak = np.maximum.accumulate(port_value)
    port_dd = (port_peak - port_value) / port_peak * 100

    # Frame indices
    bar_indices = np.linspace(max(30, N // 20), N - 1, TOTAL_FRAMES, dtype=int)

    prev_regime = regimes[bar_indices[0]]
    blend_t = 1.0
    from_regime = prev_regime

    frames = []
    for fi, d in enumerate(bar_indices):
        fig = plt.figure(figsize=(22, 9.5), facecolor=BG)

        gs_outer = gridspec.GridSpec(2, 1, height_ratios=[0.55, 9.5],
                                     hspace=0.06, figure=fig)

        # --- Title ---
        ax_title = fig.add_subplot(gs_outer[0])
        ax_title.set_facecolor(BG); ax_title.axis('off')

        regime_name = regimes[d]
        regime_color = REGIME_COLORS[regime_name]
        cur_price = prices[d]
        alpha_pct = port_cum[d] - sp_cum[d]

        if regime_name != prev_regime:
            blend_t = 0.0
            from_regime = prev_regime
            prev_regime = regime_name
        else:
            from_regime = regime_name
        blend_t = min(1.0, blend_t + 0.08)
        bt = smoothstep(blend_t)

        ax_title.text(0.5, 0.7,
                      f'\u25cf LIVE  |  Regime: {regime_name}  |  '
                      f'Spread: {spreads[d]:.1f}x  |  Base: {bases[d]*100:.0f}%  |  '
                      f'Fills/d: {fills[d]:.0f}',
                      color=regime_color, fontsize=14, fontweight='bold',
                      ha='center', va='center', transform=ax_title.transAxes,
                      family='monospace')
        ax_title.text(0.5, 0.15,
                      f'{timeframe_label}  |  '
                      f'S&P 500: ${cur_price:,.0f}  |  '
                      f'Strategy: {port_cum[d]:+.1f}%  |  '
                      f'Benchmark: {sp_cum[d]:+.1f}%  |  '
                      f'Alpha: {alpha_pct:+.1f}%',
                      color=TEXT, fontsize=10, ha='center', va='center',
                      transform=ax_title.transAxes, family='monospace')

        # --- Layout ---
        gs_bottom = gridspec.GridSpecFromSubplotSpec(
            1, 2, subplot_spec=gs_outer[1], width_ratios=[1.1, 1], wspace=0.06)
        gs_left = gridspec.GridSpecFromSubplotSpec(
            2, 1, subplot_spec=gs_bottom[0], height_ratios=[3, 1.2], hspace=0.25)

        # ===== LEFT: Performance chart =====
        ax_r = fig.add_subplot(gs_left[0])
        ax_r.set_facecolor(BG2)
        ax_r.grid(True, color=GRID_C, alpha=0.4, linewidth=0.5)
        for spine in ax_r.spines.values():
            spine.set_color(GRID_C)
        ax_r.tick_params(colors=DIM, labelsize=7)
        ax_r.set_ylabel('Cumulative Return (%)', color=DIM, fontsize=9)

        date_arr = dates[:d + 1]
        ax_r.plot(date_arr, sp_cum[:d + 1], color=BLUE, linewidth=1.8,
                  alpha=0.8, label='S&P 500 (Buy & Hold)')
        ax_r.plot(date_arr, port_cum[:d + 1], color=WHITE, linewidth=2.2,
                  alpha=0.95, label='MM Strategy (Base+Overlay)')
        ax_r.fill_between(date_arr, sp_cum[:d + 1], port_cum[:d + 1],
                          where=port_cum[:d + 1] > sp_cum[:d + 1],
                          color=GREEN, alpha=0.08)
        ax_r.fill_between(date_arr, sp_cum[:d + 1], port_cum[:d + 1],
                          where=port_cum[:d + 1] <= sp_cum[:d + 1],
                          color=RED, alpha=0.08)

        ax_r.axhline(0, color=DIM, linewidth=0.5, linestyle='--', alpha=0.4)
        ax_r.legend(fontsize=8, loc='upper left', facecolor=BG2,
                    edgecolor=GRID_C, labelcolor=TEXT)

        ax_r.xaxis.set_major_formatter(DateFormatter(date_fmt))
        ax_r.xaxis.set_major_locator(AutoDateLocator(minticks=4, maxticks=10))
        plt.setp(ax_r.xaxis.get_majorticklabels(), rotation=30, ha='right')

        ax_r.axvline(dates[d], color=regime_color, linewidth=1.5,
                     alpha=0.7, linestyle=':')

        # --- Drawdown ---
        ax_dd = fig.add_subplot(gs_left[1])
        ax_dd.set_facecolor(BG2)
        ax_dd.grid(True, color=GRID_C, alpha=0.3, linewidth=0.5)
        for spine in ax_dd.spines.values():
            spine.set_color(GRID_C)
        ax_dd.tick_params(colors=DIM, labelsize=6)
        ax_dd.set_xlabel(x_label, color=DIM, fontsize=8)
        ax_dd.set_ylabel('Drawdown (%)', color=DIM, fontsize=8)

        ax_dd.fill_between(date_arr, -sp_dd[:d + 1], color=BLUE, alpha=0.2,
                           label='S&P 500')
        ax_dd.fill_between(date_arr, -port_dd[:d + 1], color=WHITE, alpha=0.15,
                           label='MM Strategy')
        ax_dd.plot(date_arr, -sp_dd[:d + 1], color=BLUE, linewidth=0.8, alpha=0.6)
        ax_dd.plot(date_arr, -port_dd[:d + 1], color=WHITE, linewidth=1.2)
        ax_dd.axhline(0, color=DIM, linewidth=0.3)
        ax_dd.legend(fontsize=6, loc='lower left', facecolor=BG2,
                     edgecolor=GRID_C, labelcolor=TEXT)

        ax_dd.xaxis.set_major_formatter(DateFormatter(date_fmt))
        ax_dd.xaxis.set_major_locator(AutoDateLocator(minticks=3, maxticks=8))
        plt.setp(ax_dd.xaxis.get_majorticklabels(), rotation=30, ha='right')

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
        ax3d.tick_params(colors='#556677', labelsize=5)

        surf_fn_to = REGIME_SURFACES[regime_name]
        surf_fn_from = REGIME_SURFACES.get(from_regime, surf_fn_to)
        Z_from = surf_fn_from(X_SURF, Y_SURF, fi)
        Z_to = surf_fn_to(X_SURF, Y_SURF, fi)
        Z = Z_from * (1 - bt) + Z_to * bt

        z_min, z_max = Z.min(), Z.max()
        z_range = z_max - z_min if z_max > z_min else 1.0
        z_norm = (Z - z_min) / z_range

        cmap_to = REGIME_CMAPS[regime_name]
        cmap_from = REGIME_CMAPS.get(from_regime, cmap_to)
        face_colors = blend_cmap(cmap_from, cmap_to, bt, z_norm)

        ax3d.plot_surface(X_SURF, Y_SURF, Z, facecolors=face_colors,
                          alpha=0.93, rstride=1, cstride=1,
                          edgecolor='none', antialiased=True, shade=True)

        ax3d.plot_wireframe(X_SURF[::4, ::4], Y_SURF[::4, ::4], Z[::4, ::4],
                            color='white', alpha=0.05, linewidth=0.3)

        z_floor = z_min - z_range * 0.15
        try:
            ax3d.contour(X_SURF, Y_SURF, Z,
                         levels=np.linspace(z_min, z_max, 8),
                         zdir='z', offset=z_floor,
                         cmap=cmap_to, alpha=0.3, linewidths=0.5)
        except Exception:
            pass

        ax3d.set_zlim(z_floor, z_max + z_range * 0.1)
        ax3d.set_xlabel('Spot Price ($)', color='#667788', fontsize=7, labelpad=6)
        ax3d.set_ylabel('Implied Vol (%)', color='#667788', fontsize=7, labelpad=6)
        ax3d.set_zlabel('P&L ($)', color='#667788', fontsize=7, labelpad=6)

        elev = REGIME_ELEV.get(from_regime, 30) * (1 - bt) + \
               REGIME_ELEV.get(regime_name, 30) * bt
        azim = 215 + fi * 1.2
        ax3d.view_init(elev=elev, azim=azim)

        ax3d.set_title(
            f'Regime: {regime_name}  |  Spread: {spreads[d]:.1f}x',
            color=regime_color, fontsize=10, fontweight='bold',
            fontfamily='monospace', pad=6)

        frames.append(fig_to_img(fig, 1800, 900))
        plt.close(fig)
        if (fi + 1) % 20 == 0 or fi == 0:
            print(f'  [{tf_key}] Frame {fi + 1}/{TOTAL_FRAMES}')

    save_gif(frames, OUT_DIR / f'combined_dashboard_{tf_key}.gif')


# ---------------------------------------------------------------------------
# Fetch real data and generate
# ---------------------------------------------------------------------------
if __name__ == '__main__':
    print(f'Output: {OUT_DIR}')

    print('\nFetching S&P 500 daily data ...')
    sp_daily = yf.download('^GSPC', period='3y', interval='1d', progress=False)
    prices_d = sp_daily['Close'].values.flatten()
    dates_d = sp_daily.index.to_pydatetime()
    print(f'  Got {len(prices_d)} daily bars: {dates_d[0].date()} to {dates_d[-1].date()}')

    generate_dashboard('daily', prices_d, dates_d,
                       '~3 Years (Daily)', '%Y-%m', 'Date')

    print('\nFetching S&P 500 hourly data ...')
    sp_hourly = yf.download('^GSPC', period='60d', interval='1h', progress=False)
    prices_h = sp_hourly['Close'].values.flatten()
    dates_h = sp_hourly.index.to_pydatetime()
    print(f'  Got {len(prices_h)} hourly bars: {dates_h[0]} to {dates_h[-1]}')

    generate_dashboard('hourly', prices_h, dates_h,
                       '~60 Days (Hourly)', '%m-%d %H:%M', 'Date/Time')

    print('\nFetching S&P 500 minute data ...')
    sp_minute = yf.download('^GSPC', period='5d', interval='1m', progress=False)
    prices_m = sp_minute['Close'].values.flatten()
    dates_m = sp_minute.index.to_pydatetime()
    print(f'  Got {len(prices_m)} minute bars: {dates_m[0]} to {dates_m[-1]}')

    generate_dashboard('minute', prices_m, dates_m,
                       '~5 Days (1-Min)', '%m-%d %H:%M', 'Date/Time')

    print('\nAll combined dashboards generated with real S&P 500 data.')
