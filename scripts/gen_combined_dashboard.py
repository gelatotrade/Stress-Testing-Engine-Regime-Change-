#!/usr/bin/env python3
"""
Generate combined dashboard GIFs with REAL S&P 500 data from yfinance:
  1. combined_dashboard_daily.gif   - Real daily data (~3 years)
  2. combined_dashboard_hourly.gif  - Real hourly data (~60 days)
  3. combined_dashboard_minute.gif  - Real minute data (~5 days)

Left:  S&P 500 (Buy & Hold) vs Strategy with real dates on x-axis
Right: High-resolution 3D P&L surface (80x80 grid, white-blue-red colormap)

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

# High-quality diverging colormap: deep-blue → white → red (like reference images)
SURFACE_CMAP = LinearSegmentedColormap.from_list('finance_surface', [
    '#000066', '#0033aa', '#2266cc', '#5599ee',
    '#99ccff', '#ccddff', '#ffffff',
    '#ffddcc', '#ff9966', '#ee5533', '#cc2200', '#880000'])

# Per-regime accent colormaps (for contour floor)
CMAP_BULL = LinearSegmentedColormap.from_list('bull', [
    '#0000aa', '#0055cc', '#22aa66', '#00ff88', '#eeffaa'])
CMAP_CRISIS = LinearSegmentedColormap.from_list('crisis', [
    '#000044', '#660088', '#cc0022', '#ff4444', '#ffcc44'])

OUT_DIR = Path(__file__).resolve().parent.parent / 'docs' / 'img'
OUT_DIR.mkdir(parents=True, exist_ok=True)

DPI = 150
TOTAL_FRAMES = 120
N_GRID = 80          # higher resolution grid (was 50)

# ---------------------------------------------------------------------------
# 3D surface functions
# ---------------------------------------------------------------------------
spot = np.linspace(-3, 3, N_GRID)
space_x, space_y = np.meshgrid(spot, spot)


def surface_from_returns(returns_window, fi):
    """
    Build a 3D surface from a window of real returns.
    The surface shape encodes market dynamics derived from
    return statistics (mean, volatility, skewness, kurtosis).
    """
    if len(returns_window) < 10:
        returns_window = np.zeros(50)

    mu = np.mean(returns_window)
    sigma = np.std(returns_window) + 1e-10
    skew = np.mean(((returns_window - mu) / sigma) ** 3)
    kurt = np.mean(((returns_window - mu) / sigma) ** 4) - 3
    beta = mu / sigma  # drift velocity

    # Negative returns fraction -> crisis depth
    neg_frac = np.mean(returns_window < 0)

    # Base surface: Gaussian dome
    r2 = space_x**2 + space_y**2
    Z = 15 * np.exp(-0.3 * r2)

    # Drift tilts the surface
    Z += beta * 80 * space_x

    # Volatility creates valleys / turbulence
    vol_effect = sigma * 2000
    Z -= vol_effect * np.exp(-0.5 * r2)

    # Skew creates asymmetry
    Z += skew * 3 * space_x * np.exp(-0.4 * r2)

    # Kurtosis creates sharp peaks / fat tails
    if kurt > 1:
        Z += kurt * 1.5 * np.exp(-1.5 * r2) * np.cos(2 * np.sqrt(r2 + 0.01))

    # Crisis: deep central crater + rim peaks (like reference image 3)
    if neg_frac > 0.55 and sigma > 0.01:
        crater_depth = (neg_frac - 0.5) * 60 * (sigma / 0.01)
        Z -= crater_depth * np.exp(-0.8 * r2)
        # Rim peaks
        Z += crater_depth * 0.4 * np.exp(-0.3 * (np.sqrt(r2) - 1.8)**2)

    # Ripples from return autocorrelation
    if len(returns_window) > 20:
        acf1 = np.corrcoef(returns_window[:-1], returns_window[1:])[0, 1]
        Z += acf1 * 8 * np.sin(2.5 * space_x + fi * 0.1) * np.cos(2.5 * space_y + fi * 0.08)

    return Z


def compute_regime(returns_window):
    """Simple regime classification from returns."""
    if len(returns_window) < 5:
        return 'BULL QUIET', GREEN
    mu = np.mean(returns_window)
    sigma = np.std(returns_window)

    if sigma > 0.018 and mu < -0.001:
        return 'BEAR VOLATILE', RED
    elif sigma > 0.012 and mu < 0:
        return 'TRANSITION', YELLOW
    elif mu > 0.0005 and sigma < 0.01:
        return 'BULL QUIET', GREEN
    elif mu > 0:
        return 'RECOVERY', CYAN
    else:
        return 'TRANSITION', YELLOW


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


def save_gif(frames, path, duration=120):
    rgb = []
    for f in frames:
        bg = Image.new('RGB', f.size, (8, 8, 16))
        bg.paste(f, mask=f.split()[3])
        rgb.append(bg)
    rgb[0].save(path, save_all=True, append_images=rgb[1:],
                duration=duration, loop=0, optimize=True)
    print(f'  Saved {path}  ({len(rgb)} frames, {path.stat().st_size / 1024:.0f} KB)')


def filter_market_gaps(prices, dates, tf_key):
    """Remove overnight/weekend gaps for hourly and minute data.

    Gaps appear as large time jumps between consecutive bars when the market
    is closed. We detect these and create a continuous index so the chart
    shows only trading hours without dead space.
    """
    if tf_key == 'daily':
        return prices, dates  # daily data has no intraday gaps

    from datetime import timedelta
    max_gap = timedelta(hours=2) if tf_key == 'hourly' else timedelta(minutes=5)

    mask = [True]  # always keep first bar
    for i in range(1, len(dates)):
        gap = dates[i] - dates[i - 1]
        if gap <= max_gap:
            mask.append(True)
        else:
            mask.append(True)  # keep the bar but we'll re-index

    # Re-index dates to be continuous (no gaps)
    continuous_dates = [dates[0]]
    step = timedelta(hours=1) if tf_key == 'hourly' else timedelta(minutes=1)
    for i in range(1, len(dates)):
        gap = dates[i] - dates[i - 1]
        if gap > max_gap:
            # Skip the gap, continue from last continuous date
            continuous_dates.append(continuous_dates[-1] + step)
        else:
            continuous_dates.append(continuous_dates[-1] + (gap))

    return prices, np.array(continuous_dates)


def generate_dashboard(tf_key, prices, dates, timeframe_label, date_fmt, x_label):
    """Generate a combined dashboard GIF from real price data."""
    print(f'\nGenerating combined_dashboard_{tf_key}.gif  ({timeframe_label}) ...')

    # Filter out market-closed gaps for intraday data
    prices, dates = filter_market_gaps(prices, dates, tf_key)
    N = len(prices)

    # Compute returns
    returns = np.diff(prices) / prices[:-1]
    returns = np.concatenate([[0], returns])

    # S&P 500 = buy & hold cumulative
    sp_cum = (prices / prices[0] - 1) * 100  # percent

    # Strategy: reduce exposure when rolling vol is high
    lookback = max(20, N // 30)
    port_value = np.ones(N) * prices[0]
    exposure = np.ones(N)
    for i in range(1, N):
        window = returns[max(0, i - lookback):i]
        vol = np.std(window) if len(window) > 2 else 0.005
        mean_r = np.mean(window) if len(window) > 2 else 0

        # Adaptive exposure: reduce in high vol / negative drift
        if vol > 0.015 and mean_r < -0.001:
            exp = 0.15  # crisis: 15% exposure
        elif vol > 0.01:
            exp = 0.4   # elevated vol: 40%
        elif mean_r > 0.0005:
            exp = 1.1   # bull: 110% (slight leverage)
        else:
            exp = 0.8

        exposure[i] = exp
        port_value[i] = port_value[i - 1] * (1 + returns[i] * exp)

    port_cum = (port_value / port_value[0] - 1) * 100

    # Drawdowns
    sp_peak = np.maximum.accumulate(prices)
    sp_dd = (sp_peak - prices) / sp_peak * 100
    port_peak = np.maximum.accumulate(port_value)
    port_dd = (port_peak - port_value) / port_peak * 100

    # Frame indices
    bar_indices = np.linspace(max(30, N // 20), N - 1, TOTAL_FRAMES, dtype=int)

    frames = []
    for fi, d in enumerate(bar_indices):
        fig = plt.figure(figsize=(22, 9.5), facecolor=BG)

        gs_outer = gridspec.GridSpec(2, 1, height_ratios=[0.55, 9.5], hspace=0.06, figure=fig)

        # --- Title with LaTeX formula ---
        ax_title = fig.add_subplot(gs_outer[0])
        ax_title.set_facecolor(BG); ax_title.axis('off')

        ret_window = returns[max(0, d - lookback):d]
        regime_name, regime_color = compute_regime(ret_window)
        cur_price = prices[d]
        cur_date = dates[d]

        alpha_pct = port_cum[d] - sp_cum[d]

        ax_title.text(0.5, 0.7,
                      f'\u25cf LIVE  |  Regime: {regime_name}  |  '
                      f'Exposure: {exposure[d]*100:.0f}%',
                      color=regime_color, fontsize=16, fontweight='bold',
                      ha='center', va='center', transform=ax_title.transAxes,
                      family='monospace')
        ax_title.text(0.5, 0.15,
                      f'\u25cf LIVE  {timeframe_label}  |  '
                      f'{cur_date}  |  S&P 500: ${cur_price:,.0f}  |  '
                      f'Strategy: {port_cum[d]:+.1f}%  |  '
                      f'Buy&Hold: {sp_cum[d]:+.1f}%  |  '
                      f'Alpha: {alpha_pct:+.1f}%',
                      color=TEXT, fontsize=10, ha='center', va='center',
                      transform=ax_title.transAxes, family='monospace')

        # --- Layout: chart left, 3D right ---
        gs_bottom = gridspec.GridSpecFromSubplotSpec(1, 2, subplot_spec=gs_outer[1],
                                                      width_ratios=[1.1, 1], wspace=0.06)
        gs_left = gridspec.GridSpecFromSubplotSpec(2, 1, subplot_spec=gs_bottom[0],
                                                    height_ratios=[3, 1.2], hspace=0.25)

        # ===== LEFT: Performance chart with real dates =====
        ax_r = fig.add_subplot(gs_left[0])
        ax_r.set_facecolor(BG2)
        ax_r.grid(True, color=GRID_C, alpha=0.4, linewidth=0.5)
        for spine in ax_r.spines.values():
            spine.set_color(GRID_C)
        ax_r.tick_params(colors=DIM, labelsize=7)
        ax_r.set_ylabel('Cumulative Return (%)', color=DIM, fontsize=9)

        # Plot with dates on x-axis
        date_arr = dates[:d + 1]
        ax_r.plot(date_arr, sp_cum[:d + 1], color=BLUE, linewidth=1.8, alpha=0.8, label='S&P 500 (Buy & Hold)')
        ax_r.plot(date_arr, port_cum[:d + 1], color=WHITE, linewidth=2.2, alpha=0.95, label='Strategy (Regime)')
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

        # Vertical line at current date
        ax_r.axvline(dates[d], color=regime_color, linewidth=1.5, alpha=0.7, linestyle=':')

        # --- Drawdown ---
        ax_dd = fig.add_subplot(gs_left[1])
        ax_dd.set_facecolor(BG2)
        ax_dd.grid(True, color=GRID_C, alpha=0.3, linewidth=0.5)
        for spine in ax_dd.spines.values():
            spine.set_color(GRID_C)
        ax_dd.tick_params(colors=DIM, labelsize=6)
        ax_dd.set_xlabel(x_label, color=DIM, fontsize=8)
        ax_dd.set_ylabel('Drawdown (%)', color=DIM, fontsize=8)

        ax_dd.fill_between(date_arr, -sp_dd[:d + 1], color=BLUE, alpha=0.2, label='S&P 500')
        ax_dd.fill_between(date_arr, -port_dd[:d + 1], color=WHITE, alpha=0.15, label='Strategy')
        ax_dd.plot(date_arr, -sp_dd[:d + 1], color=BLUE, linewidth=0.8, alpha=0.6)
        ax_dd.plot(date_arr, -port_dd[:d + 1], color=WHITE, linewidth=1.2)
        ax_dd.axhline(0, color=DIM, linewidth=0.3)
        ax_dd.legend(fontsize=6, loc='lower left', facecolor=BG2,
                     edgecolor=GRID_C, labelcolor=TEXT)

        ax_dd.xaxis.set_major_formatter(DateFormatter(date_fmt))
        ax_dd.xaxis.set_major_locator(AutoDateLocator(minticks=3, maxticks=8))
        plt.setp(ax_dd.xaxis.get_majorticklabels(), rotation=30, ha='right')

        # ===== RIGHT: 3D Surface (high-resolution, reference-style) =====
        ax3d = fig.add_subplot(gs_bottom[1], projection='3d')
        ax3d.set_facecolor(BG)
        ax3d.xaxis.pane.fill = False
        ax3d.yaxis.pane.fill = False
        ax3d.zaxis.pane.fill = False
        ax3d.xaxis.pane.set_edgecolor('#0d0d1a')
        ax3d.yaxis.pane.set_edgecolor('#0d0d1a')
        ax3d.zaxis.pane.set_edgecolor('#0d0d1a')
        ax3d.grid(True, color='#1a2233', alpha=0.25)
        ax3d.tick_params(colors='#445566', labelsize=5)

        # Build surface from real returns
        Z = surface_from_returns(ret_window, fi)

        # Normalize for colormap
        z_min, z_max = Z.min(), Z.max()
        z_range = z_max - z_min if z_max > z_min else 1.0
        z_norm = (Z - z_min) / z_range

        # Main surface with reference-style colormap
        ax3d.plot_surface(space_x, space_y, Z,
                          cmap=SURFACE_CMAP,
                          alpha=0.88, rstride=1, cstride=1,
                          edgecolor='none', antialiased=True, shade=True,
                          vmin=z_min, vmax=z_max)

        # Semi-transparent wireframe for structure
        ax3d.plot_wireframe(space_x[::3, ::3], space_y[::3, ::3], Z[::3, ::3],
                            color='white', alpha=0.04, linewidth=0.25)

        # Floor contour projection
        z_floor = z_min - z_range * 0.2
        try:
            ax3d.contour(space_x, space_y, Z,
                         levels=np.linspace(z_min, z_max, 10),
                         zdir='z', offset=z_floor,
                         cmap='coolwarm', alpha=0.25, linewidths=0.4)
        except Exception:
            pass

        ax3d.set_zlim(z_floor, z_max + z_range * 0.1)
        ax3d.set_xlabel('SPACE_X', color='#556677', fontsize=7, labelpad=5)
        ax3d.set_ylabel('SPACE_Y', color='#556677', fontsize=7, labelpad=5)
        ax3d.set_zlabel('', color='#556677', fontsize=7)

        # Y-axis as TIME (like reference)
        time_ticks = np.linspace(0, 100, 6)
        ax3d.set_yticks(np.linspace(-3, 3, 6))
        ax3d.set_yticklabels([f'{int(t)}' for t in time_ticks])
        ax3d.set_ylabel('TIME', color='#556677', fontsize=7, labelpad=5)

        # Dynamic camera
        vol = np.std(ret_window) if len(ret_window) > 2 else 0.005
        elev = max(15, min(35, 30 - vol * 500))
        azim = 220 + fi * 1.2
        ax3d.view_init(elev=elev, azim=azim)

        # Title with regime + formulas
        vol_pct = vol * 100 * np.sqrt(252 if tf_key == 'daily' else (252*6.5 if tf_key == 'hourly' else 252*390))
        ax3d.set_title(
            f'Regime: {regime_name}\n'
            f'Ann. Vol: {vol_pct:.1f}%',
            color=regime_color, fontsize=10, fontweight='bold',
            fontfamily='monospace', pad=6)

        frames.append(fig_to_img(fig, 1800, 900))
        plt.close(fig)
        if (fi + 1) % 20 == 0 or fi == 0:
            print(f'  [{tf_key}] Frame {fi + 1}/{TOTAL_FRAMES}')

    save_gif(frames, OUT_DIR / f'combined_dashboard_{tf_key}.gif', duration=220)


# ---------------------------------------------------------------------------
# Fetch real data and generate
# ---------------------------------------------------------------------------
if __name__ == '__main__':
    print(f'Output: {OUT_DIR}')

    # --- Daily (~3 years) ---
    print('\nFetching S&P 500 daily data ...')
    sp_daily = yf.download('^GSPC', period='3y', interval='1d', progress=False)
    prices_d = sp_daily['Close'].values.flatten()
    dates_d = sp_daily.index.to_pydatetime()
    print(f'  Got {len(prices_d)} daily bars: {dates_d[0].date()} to {dates_d[-1].date()}')

    generate_dashboard('daily', prices_d, dates_d,
                       '~3 Years (Daily)', '%Y-%m', 'Date')

    # --- Hourly (~60 days) ---
    print('\nFetching S&P 500 hourly data ...')
    sp_hourly = yf.download('^GSPC', period='60d', interval='1h', progress=False)
    prices_h = sp_hourly['Close'].values.flatten()
    dates_h = sp_hourly.index.to_pydatetime()
    print(f'  Got {len(prices_h)} hourly bars: {dates_h[0]} to {dates_h[-1]}')

    generate_dashboard('hourly', prices_h, dates_h,
                       '~60 Days (Hourly)', '%m-%d %H:%M', 'Date/Time')

    # --- Minute (~5 days) ---
    print('\nFetching S&P 500 minute data ...')
    sp_minute = yf.download('^GSPC', period='5d', interval='1m', progress=False)
    prices_m = sp_minute['Close'].values.flatten()
    dates_m = sp_minute.index.to_pydatetime()
    print(f'  Got {len(prices_m)} minute bars: {dates_m[0]} to {dates_m[-1]}')

    generate_dashboard('minute', prices_m, dates_m,
                       '~5 Days (1-Min)', '%m-%d %H:%M', 'Date/Time')

    print('\nAll combined dashboards generated with real S&P 500 data.')
