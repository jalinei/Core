"""Plot simultaneous HRTIM-handler and control-callback jitter measurements."""
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
parser.add_argument('--output', required=True, type=Path)
args = parser.parse_args()
text = args.log.read_text().replace('\r', '')
if 'TIMING_COMPARISON_END' not in text:
    raise SystemExit('Incomplete comparison report')

data = {}
for name in ['CALLBACK', 'ISR']:
    match = re.search(r'TIMING_SECTION_BEGIN,' + name + r'\n(.*?)TIMING_SECTION_END,' + name, text, re.S)
    if match is None:
        raise SystemExit('Missing ' + name + ' section')
    section = match[1]
    metadata = re.search(r'Control timing: (\d+) invocations, (\d+) intervals, period (\d+) us, CPU (\d+) Hz', section)
    header = re.search(r'JITTER_HISTOGRAM_BEGIN,(\d+),(\d+),(\d+),(\d+),(\d+)', section)
    rows = re.findall(r'^JITTER_BIN,(\d+),(\d+)$', section, re.M)
    if metadata is None or header is None or len(rows) != 161 or [int(i) for i, _ in rows] != list(range(161)):
        raise SystemExit('Invalid ' + name + ' histogram')
    invocations, intervals, period, hz = map(int, metadata.groups())
    header_hz, width, header_intervals, low, high = map(int, header.groups())
    counts = np.array([int(n) for _, n in rows], dtype=np.int64)
    if header_hz != hz or intervals != header_intervals or int(counts.sum()) + low + high != intervals:
        raise SystemExit('Inconsistent ' + name + ' metadata/counts')
    summary = re.search(r'Period jitter \(us\): min ([\d.-]+), max ([\d.-]+), mean ([\d.-]+), mean absolute ([\d.-]+)', section)
    exceed = re.search(r'Absolute jitter > 4 us: (\d+)', section)
    if summary is None or exceed is None:
        raise SystemExit('Missing ' + name + ' summary')
    data[name] = {'invocations': invocations, 'intervals': intervals, 'period': period, 'hz': hz,
                  'width': width, 'counts': counts, 'underflow': low, 'overflow': high,
                  'summary': list(map(float, summary.groups())), 'exceed': int(exceed[1])}

callback, isr = data['CALLBACK'], data['ISR']
for key in ['invocations', 'intervals', 'period', 'hz', 'width']:
    if callback[key] != isr[key]:
        raise SystemExit('Measurements are not paired: ' + key)
hz, width = isr['hz'], isr['width']
edges_cycles = (np.arange(162) - 80) * width - width // 2
edges_us = edges_cycles * 1e6 / hz
pair = re.search(r'Paired abnormal intervals: both (\d+), ISR only (\d+), callback only (\d+)', text)
delay = re.search(r'ISR-to-callback \(us\): samples (\d+), min ([\d.]+), max ([\d.]+), mean ([\d.]+)', text)
event_header = re.search(r'JITTER_EVENTS_BEGIN,(\d+),(\d+)', text)
rows = re.findall(r'^JITTER_EVENT,(\d+),(-?\d+),(-?\d+),(\d+)$', text, re.M)
if pair is None or delay is None or event_header is None or 'JITTER_EVENTS_END' not in text:
    raise SystemExit('Missing paired diagnostics')
both, isr_only, callback_only = map(int, pair.groups())
recorded, dropped = map(int, event_header.groups())
if len(rows) != recorded or recorded + dropped != both + isr_only + callback_only:
    raise SystemExit('Event trace count mismatch')
if both + isr_only != isr['exceed'] or both + callback_only != callback['exceed']:
    raise SystemExit('Paired abnormal counts do not match histograms')
events = np.array([list(map(int, row)) for row in rows], dtype=np.int64).reshape((-1, 4))
if int(delay[1]) != callback['invocations']:
    raise SystemExit('Delay sample count mismatch')

args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.with_suffix('.csv').open('w', newline='') as out:
    writer = csv.writer(out)
    writer.writerow(['lower_cycles_inclusive', 'upper_cycles_exclusive', 'lower_us', 'upper_us', 'isr_count', 'callback_count'])
    for i in range(161):
        writer.writerow([int(edges_cycles[i]), int(edges_cycles[i+1]), f'{edges_us[i]:.9f}', f'{edges_us[i+1]:.9f}', int(isr['counts'][i]), int(callback['counts'][i])])
    writer.writerow(['underflow', '', '', '', isr['underflow'], callback['underflow']])
    writer.writerow(['overflow', '', '', '', isr['overflow'], callback['overflow']])
event_csv = args.output.parent / (args.output.name + '-events.csv')
with event_csv.open('w', newline='') as out:
    writer = csv.writer(out)
    writer.writerow(['invocation', 'isr_jitter_cycles', 'callback_jitter_cycles', 'dispatch_cycles', 'isr_jitter_us', 'callback_jitter_us', 'dispatch_us'])
    for invocation, ij, cj, dispatch in events:
        writer.writerow([invocation, ij, cj, dispatch, f'{ij*1e6/hz:.9f}', f'{cj*1e6/hz:.9f}', f'{dispatch*1e6/hz:.9f}'])

plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10,
                     'axes.spines.top': False, 'axes.spines.right': False})
fig, axes = plt.subplots(2, 2, figsize=(13, 9))
fig.patch.set_facecolor('#f7f9fc')
for ax in axes.flat:
    ax.grid(alpha=0.18)
    ax.set_axisbelow(True)
for ax in axes[0]:
    for name, color, label in [('ISR', '#3972a8', 'HRTIM handler entry'), ('CALLBACK', '#df6c45', 'Control callback entry')]:
        ax.stairs(data[name]['counts'], edges_us, color=color, label=label, linewidth=1.6)
    ax.set_xlabel('Period jitter (µs)')
    ax.set_ylabel('Measured interval count')
    ax.legend(fontsize=9)
axes[0, 0].set_title('Central distribution', loc='left', fontweight='bold')
axes[0, 0].set_xlim(-2.5, 2.5)
axes[0, 1].set_title('Distribution tails · logarithmic count', loc='left', fontweight='bold')
axes[0, 1].set_yscale('log')
axes[0, 1].set_ylim(0.8, max(isr['counts'].max(), callback['counts'].max()) * 2)
occupied = (isr['counts'] + callback['counts']) > 0
centers = (edges_us[:-1] + edges_us[1:]) / 2
limit = max(5, np.max(np.abs(centers[occupied])) + width * 1e6 / hz)
axes[0, 1].set_xlim(-limit, limit)
for x in [-4, 4]:
    axes[0, 1].axvline(x, color='#a94021', linestyle='--', linewidth=0.9)
axes[1, 0].set_title('Paired abnormal intervals', loc='left', fontweight='bold')
axes[1, 0].set_xlabel('HRTIM handler-entry jitter (µs)')
axes[1, 0].set_ylabel('Control callback-entry jitter (µs)')
axes[1, 1].set_title('ISR-to-callback delay at abnormal intervals', loc='left', fontweight='bold')
axes[1, 1].set_xlabel('Nominal elapsed time (s)')
axes[1, 1].set_ylabel('ISR-to-callback delay (µs)')
if len(events):
    ij, cj, ds = events[:, 1]*1e6/hz, events[:, 2]*1e6/hz, events[:, 3]*1e6/hz
    abnormal_isr, abnormal_callback = np.abs(ij) > 4, np.abs(cj) > 4
    for mask, color, label in [(abnormal_isr & abnormal_callback, '#3972a8', 'Both abnormal'),
                               (abnormal_isr & ~abnormal_callback, '#8b62ad', 'ISR only'),
                               (~abnormal_isr & abnormal_callback, '#df6c45', 'Callback only')]:
        if mask.any():
            axes[1, 0].scatter(ij[mask], cj[mask], s=12, alpha=0.6, color=color, label=label)
            axes[1, 1].scatter((events[mask, 0]-1)*callback['period']/1e6, ds[mask], s=10, alpha=0.6, color=color)
    extent = max(5, np.max(np.abs(np.r_[ij, cj])) + 1)
    axes[1, 0].plot([-extent, extent], [-extent, extent], '--', color='#8993a3', linewidth=1, label='Equal jitter')
    axes[1, 0].set_xlim(-extent, extent)
    axes[1, 0].set_ylim(-extent, extent)
    axes[1, 0].legend(fontsize=8)
else:
    axes[1, 0].text(0.5, 0.5, 'No abnormal intervals', transform=axes[1, 0].transAxes, ha='center')
fig.suptitle('HRTIM handler entry vs control callback entry', x=0.065, y=0.98, ha='left', fontsize=19, fontweight='bold', color='#203047')
fig.text(0.065, 0.925, f'Same {callback["invocations"]:,} invocations / {callback["intervals"]:,} intervals · {callback["period"]} µs period · idle mode\n'
         f'Bin width {width*1e6/hz:.3f} µs · master UID 0x002B001A', va='top', fontsize=11, color='#506174')
notes = [f'|jitter| > 4 µs: ISR {isr["exceed"]:,}; callback {callback["exceed"]:,}. Paired: both {both:,}, ISR only {isr_only:,}, callback only {callback_only:,}.',
         f'ISR-to-callback delay over all invocations: min {float(delay[2]):.3f} µs, mean {float(delay[4]):.3f} µs, max {float(delay[3]):.3f} µs.',
         f'Abnormal paired records plotted: {recorded:,}; omitted after buffer filled: {dropped:,}. No control updates skipped.',
         'Handler timestamp follows the Zephyr ISR wrapper; these are period-jitter measurements, not absolute timer-release latency.']
if isr['underflow'] or isr['overflow'] or callback['underflow'] or callback['overflow']:
    notes.append('Overflow counts are retained in the exported histogram CSV.')
fig.text(0.065, 0.025, '\n'.join(notes), va='bottom', fontsize=9.5, color='#506174')
fig.subplots_adjust(left=0.075, right=0.97, top=0.82, bottom=0.19, hspace=0.38, wspace=0.28)
for suffix in ['.png', '.svg']:
    output = args.output.with_suffix(suffix)
    fig.savefig(output, dpi=180, facecolor=fig.get_facecolor())
    print(output)
print(args.output.with_suffix('.csv'))
print(event_csv)
