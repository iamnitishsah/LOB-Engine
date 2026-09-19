#!/usr/bin/env python3
"""
Performance & Backtest Analysis Tool for LOB-Engine.
Parses CSV output from backtest runs and latency benchmarks,
computes metrics, and plots equity curves / latency distributions.
"""

import sys
import os
import argparse
import csv

def analyze_backtest(csv_path, save_plot=None, summary_only=False):
    timestamps = []
    inventories = []
    cashes = []
    mark_prices = []
    realized_pnls = []
    unrealized_pnls = []
    total_pnls = []

    with open(csv_path, 'r', newline='') as f:
        reader = csv.DictReader(f)
        for row in reader:
            timestamps.append(int(row['timestamp']))
            inventories.append(int(row['inventory']))
            cashes.append(float(row['cash']))
            mark_prices.append(int(row['mark_price']))
            realized_pnls.append(float(row['realized_pnl']))
            unrealized_pnls.append(float(row['unrealized_pnl']))
            total_pnls.append(float(row['total_pnl']))

    if not total_pnls:
        print(f"Error: No data rows found in {csv_path}")
        return

    n = len(total_pnls)
    final_pnl = total_pnls[-1]
    peak_pnl = max(total_pnls)
    min_pnl = min(total_pnls)
    max_inv = max(inventories)
    min_inv = min(inventories)

    # Calculate max drawdown
    peak = total_pnls[0]
    max_dd = 0.0
    for p in total_pnls:
        if p > peak:
            peak = p
        dd = peak - p
        if dd > max_dd:
            max_dd = dd

    print("========================================")
    print(f" Backtest Analysis: {os.path.basename(csv_path)}")
    print("----------------------------------------")
    print(f"  Data points:    {n}")
    print(f"  Final PnL:      {final_pnl:,.2f}")
    print(f"  Peak PnL:       {peak_pnl:,.2f}")
    print(f"  Min PnL:        {min_pnl:,.2f}")
    print(f"  Max Drawdown:   {max_dd:,.2f}")
    print(f"  Max Inventory:  {max_inv}")
    print(f"  Min Inventory:  {min_inv}")
    print("========================================")

    if summary_only:
        return

    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt

        fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(10, 8), sharex=True)

        steps = list(range(len(total_pnls)))

        ax1.plot(steps, total_pnls, label='Total PnL', color='#0066cc', linewidth=1.5)
        ax1.plot(steps, realized_pnls, label='Realized PnL', color='#28a745', linestyle='--', linewidth=1.0)
        ax1.set_ylabel('PnL')
        ax1.set_title('Strategy Equity Curve')
        ax1.grid(True, linestyle=':', alpha=0.6)
        ax1.legend(loc='upper left')

        ax2.plot(steps, inventories, label='Inventory', color='#fd7e14', linewidth=1.2)
        ax2.axhline(0, color='black', linewidth=0.8, linestyle='--')
        ax2.set_ylabel('Position Qty')
        ax2.set_title('Inventory Exposure')
        ax2.grid(True, linestyle=':', alpha=0.6)
        ax2.legend(loc='upper left')

        ax3.plot(steps, mark_prices, label='Mark Price', color='#6f42c1', linewidth=1.2)
        ax3.set_ylabel('Price Ticks')
        ax3.set_xlabel('Event Index')
        ax3.set_title('Market Price Trajectory')
        ax3.grid(True, linestyle=':', alpha=0.6)
        ax3.legend(loc='upper left')

        plt.tight_layout()
        out_file = save_plot if save_plot else csv_path.rsplit('.', 1)[0] + '.png'
        plt.savefig(out_file, dpi=150)
        print(f"Saved plot to {out_file}")

    except ImportError:
        print("Note: matplotlib not installed. Skipping plot generation.")

def analyze_latency(csv_path, save_plot=None, summary_only=False):
    latencies = []
    with open(csv_path, 'r', newline='') as f:
        reader = csv.DictReader(f)
        for row in reader:
            latencies.append(int(row['latency_ns']))

    if not latencies:
        print(f"Error: No latency data found in {csv_path}")
        return

    latencies.sort()
    n = len(latencies)
    p50 = latencies[int(n * 0.50)]
    p90 = latencies[int(n * 0.90)]
    p99 = latencies[int(n * 0.99)]
    p99_9 = latencies[min(int(n * 0.999), n - 1)]

    print("========================================")
    print(f" Latency Analysis: {os.path.basename(csv_path)}")
    print("----------------------------------------")
    print(f"  Samples: {n}")
    print(f"  Min:     {latencies[0]} ns")
    print(f"  p50:     {p50} ns")
    print(f"  p90:     {p90} ns")
    print(f"  p99:     {p99} ns")
    print(f"  p99.9:   {p99_9} ns")
    print(f"  Max:     {latencies[-1]} ns")
    print("========================================")

    if summary_only:
        return

    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt

        plt.figure(figsize=(8, 5))
        plt.hist([l for l in latencies if l < p99_9 * 2], bins=50, color='#17a2b8', edgecolor='black', alpha=0.7)
        plt.title('Latency Distribution (ns)')
        plt.xlabel('Latency (ns)')
        plt.ylabel('Frequency')
        plt.grid(True, linestyle=':', alpha=0.6)

        out_file = save_plot if save_plot else csv_path.rsplit('.', 1)[0] + '_latency.png'
        plt.savefig(out_file, dpi=150)
        print(f"Saved latency plot to {out_file}")
    except ImportError:
        pass

def main():
    parser = argparse.ArgumentParser(description='LOB-Engine Analysis & Plotting Tool')
    parser.add_argument('csv_file', help='Path to CSV results file')
    parser.add_argument('--plot', help='Output image filename for plots', default=None)
    parser.add_argument('--summary-only', action='store_true', help='Only print metrics summary without plotting')

    args = parser.parse_args()

    if not os.path.exists(args.csv_file):
        print(f"Error: File {args.csv_file} does not exist.")
        sys.exit(1)

    with open(args.csv_file, 'r') as f:
        header = f.readline().strip()

    if 'total_pnl' in header:
        analyze_backtest(args.csv_file, args.plot, args.summary_only)
    elif 'latency_ns' in header:
        analyze_latency(args.csv_file, args.plot, args.summary_only)
    else:
        print(f"Unrecognized CSV format. Header: {header}")
        sys.exit(1)

if __name__ == '__main__':
    main()
