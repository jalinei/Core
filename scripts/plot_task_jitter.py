"""Plot the exact histogram exported by the DWT control-task monitor."""
import argparse
import csv
import os
import re
import tempfile
from pathlib import Path

os.environ.setdefault('MPLCONFIGDIR', str(Path(tempfile.gettempdir()) / 'twist-matplotlib'))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('log', type=Path)
parser.add_argument('--output', type=Path, required=True, help='Output prefix for PNG, SVG and CSV')
args = parser.parse_args()
text = args.log.read_text().replace('\r', '')
header = re.search(r'^JITTER_HISTOGRAM_BEGIN,(\d+),(\d+),(\d+),(\d+),(\d+)$', text, re.M)
if header is None or 'JITTER_HISTOGRAM_END' not in text:
    raise SystemExit('Incomplete histogram report')
cpu_hz, width_cycles, intervals, underflow, overflow = map(int, header.groups())
timing = re.search(r'Control timing: (\d+) invocations, (\d+) intervals, period (\d+) us, CPU (\d+) Hz', text)
if timing is None:
    raise SystemExit('Missing sample-count and period metadata')
invocations, metadata_intervals, period_us, metadata_hz = map(int, timing.groups())
if metadata_intervals != intervals or metadata_hz != cpu_hz:
    raise SystemExit('Inconsistent timing metadata')
uid = re.search(r'Board UID: (0x[0-9A-Fa-f]+)', text)
board_label = uid[1] if uid else 'unknown UID'
rows = re.findall(r'^JITTER_BIN,(\d+),(\d+)$', text, re.M)
if len(rows) != 161 or [int(i) for i, _ in rows] != list(range(161)):
    raise SystemExit('Expected exactly 161 ordered histogram bins')
counts = np.array([int(n) for _, n in rows], dtype=np.int64)
if counts.sum() + underflow + overflow != intervals:
    raise SystemExit('Histogram counts do not match interval count')
if cpu_hz <= 0 or width_cycles <= 0 or intervals <= 0:
    raise SystemExit('Invalid histogram metadata')
lower_cycles = (np.arange(161) - 80) * width_cycles - width_cycles // 2
lower_us = lower_cycles * 1e6 / cpu_hz
width_us = width_cycles * 1e6 / cpu_hz
upper_us = lower_us + width_us
centers = (lower_us + upper_us) / 2
summary = re.search(r'Period jitter \(us\): min ([\d.-]+), max ([\d.-]+), mean ([\d.-]+), mean absolute ([\d.-]+)', text)
exceed = re.search(r'Absolute jitter > 4 us: (\d+) \(([\d.]+)%\)', text)
episodes = re.search(r'Abnormal episodes: (\d+)', text)
gaps = re.search(r'Episode start gaps \(us\): count (\d+), min ([\d.]+), max ([\d.]+), mean ([\d.]+)', text)

args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.with_suffix('.csv').open('w', newline='') as output:
    writer = csv.writer(output)
    writer.writerow(['lower_cycles_inclusive', 'upper_cycles_exclusive', 'lower_us', 'upper_us', 'count'])
    for lower, low, high, count in zip(lower_cycles, lower_us, upper_us, counts):
        writer.writerow([int(lower), int(lower + width_cycles), f'{low:.9f}', f'{high:.9f}', int(count)])
    writer.writerow(['underflow', '', '', f'{lower_us[0]:.9f}', underflow])
    writer.writerow(['overflow', '', f'{upper_us[-1]:.9f}', '', overflow])

plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 11,
                     'axes.spines.top': False, 'axes.spines.right': False})
fig, axes = plt.subplots(1, 2, figsize=(13, 6))
fig.patch.set_facecolor('#f7f9fc')
colors = np.where((upper_us <= -4) | (lower_us >= 4), '#df6c45', '#3972a8')
for ax in axes:
    ax.bar(lower_us, counts, width=width_us * 0.96, align='edge', color=colors,
           linewidth=0)
    ax.set_xlabel('Period jitter (µs)')
    ax.set_ylabel('Number of measured intervals')
    ax.grid(axis='y', alpha=0.2)
    ax.set_axisbelow(True)
    ax.axvline(0, color='#354356', linewidth=0.8, alpha=0.55)
axes[0].set_title('Central distribution', loc='left', fontweight='bold')
axes[0].set_xlim(-2.5, 2.5)
axes[0].ticklabel_format(axis='y', style='plain')
axes[1].set_title('Distribution tails · logarithmic count', loc='left', fontweight='bold')
axes[1].set_yscale('log')
axes[1].set_ylim(0.8, max(counts.max() * 2, 10))
occupied = centers[counts > 0]
limit = max(5, max(abs(occupied.min()), abs(occupied.max())) + width_us)
axes[1].set_xlim(-limit, limit)
for x in [-4, 4]:
    axes[1].axvline(x, linestyle='--', color='#a94021', linewidth=1.1)
axes[1].text(0.98, 0.95, 'Dashed lines: ±4 µs', transform=axes[1].transAxes,
             ha='right', va='top', fontsize=10)
fig.suptitle('Measured control-task jitter', x=0.065, y=0.98, ha='left',
             fontsize=20, fontweight='bold', color='#203047')
fig.text(0.065, 0.89,
         f'Twist master {board_label} · idle mode · {period_us} µs period\n'
         f'{intervals:,} intervals / {invocations:,} callbacks · bin width {width_us:.3f} µs',
         fontsize=11, color='#506174', va='top')
notes = []
if summary:
    minimum, maximum, mean, absolute = map(float, summary.groups())
    notes.append(f'Jitter range {minimum:+.3f} to {maximum:+.3f} µs; mean absolute {absolute:.3f} µs.')
if exceed and episodes:
    notes.append(f'|jitter| > 4 µs: {int(exceed[1]):,} intervals ({float(exceed[2]):.4f}%), grouped into {int(episodes[1]):,} episodes.')
if gaps:
    count, minimum, maximum, mean = gaps.groups()
    notes.append(f'Episode start gaps: min {float(minimum)/1000:.3f} ms, mean {float(mean)/1000:.3f} ms, max {float(maximum)/1000:.3f} ms.')
notes.append('Adjacent abnormal intervals form one episode; positive/negative compensation pairs remain in the histogram.')
if underflow or overflow:
    notes.append(f'Outside plotted bin range: {underflow} underflow / {overflow} overflow intervals.')
fig.text(0.065, 0.04, '\n'.join(notes), fontsize=9.5, color='#506174', va='bottom')
fig.subplots_adjust(left=0.075, right=0.97, top=0.80, bottom=0.29, wspace=0.28)
for suffix in ['.png', '.svg']:
    output = args.output.with_suffix(suffix)
    fig.savefig(output, dpi=180, facecolor=fig.get_facecolor())
    print(output)
print(args.output.with_suffix('.csv'))
