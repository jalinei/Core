"""Resolve and correlate diagnostic mask records with nearby HRTIM entries."""
import argparse
import collections
import csv
import hashlib
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path

os.environ.setdefault('MPLCONFIGDIR', str(Path(tempfile.gettempdir()) / 'twist-matplotlib'))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('log', type=Path)
parser.add_argument('--elf', type=Path, required=True)
parser.add_argument('--addr2line', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
text = args.log.read_text().replace('\r', '')
summary = re.search(r'^IRQ_TRACE_SUMMARY,(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)$', text, re.M)
header = re.search(r'^IRQ_TRACE_BEGIN,(\d+),(\d+)$', text, re.M)
if summary is None or header is None or 'IRQ_TRACE_END' not in text or 'TIMING_COMPARISON_END' not in text:
    raise SystemExit('Incomplete mask trace')
hz, period_cycles, total_masks, pending_masks, long_pending_masks, max_busy, positives, recent_pending_positives, threshold_cycles = map(int, summary.groups())
recorded, dropped = map(int, header.groups())
pattern = r'^IRQ_TRACE,(\d+),(\d+),(\d+),(\d+),(\d+),(0x[0-9a-fA-F]+),(0x[0-9a-fA-F]+),(\d+),(\d+),(\d+),(\d+)$'
raw = re.findall(pattern, text, re.M)
if len(raw) != recorded:
    raise SystemExit('Trace record count mismatch')
if not hz or not period_cycles:
    raise SystemExit('Invalid clock metadata')
isr_match = re.search(r'TIMING_SECTION_BEGIN,ISR\n(.*?)TIMING_SECTION_END,ISR', text, re.S)
if isr_match is None:
    raise SystemExit('Missing ISR comparison section')
abnormal = re.search(r'Absolute jitter > 4 us: (\d+)', isr_match[1])
if abnormal is None or recorded + dropped != int(abnormal[1]):
    raise SystemExit('Trace totals do not match ISR histogram')

symbols = {}
def resolve(address):
    if address == 0: return 'unknown'
    address &= ~1
    if address not in symbols:
        output = subprocess.check_output([str(args.addr2line), '-e', str(args.elf), '-f', '-C', '-i', hex(address)], text=True).strip().splitlines()
        frames = [output[i] + ' @ ' + output[i+1] for i in range(0, len(output)-1, 2)]
        symbols[address] = ' ; '.join(frames)
    return symbols[address]

def context(ipsr):
    return {0: 'Thread', 11: 'SVCall', 14: 'PendSV', 15: 'SysTick', 83: 'HRTIM'}.get(ipsr, f'Exception {ipsr}')

rows = []
for row in raw:
    previous, now, sequence, start, end = map(int, row[:5])
    caller, restore = int(row[5], 16), int(row[6], 16)
    sleep, kind, ipsr, pending = map(int, row[7:])
    interval = (now - previous) & 0xffffffff
    jitter = interval - period_cycles
    duration = (end - start) & 0xffffffff
    if sleep > duration:
        raise SystemExit('Invalid sleep duration')
    age = (now - end) & 0xffffffff
    valid = sequence != 0
    recent = valid and age < period_cycles // 4
    entry = {'previous_hrtim_cycles': previous, 'hrtim_cycles': now,
             'mask_sequence': sequence, 'mask_start_cycles': start, 'mask_end_cycles': end,
             'caller_address': hex(caller), 'restore_address': hex(restore),
             'sleep_cycles': sleep, 'kind_flags': kind, 'starting_ipsr': ipsr,
             'starting_context': context(ipsr), 'hrtim_pending_while_masked': pending,
             'jitter_us': jitter * 1e6/hz, 'mask_duration_us': duration*1e6/hz,
             'sleep_us': sleep*1e6/hz, 'busy_us': (duration-sleep)*1e6/hz,
             'mask_end_to_hrtim_us': age*1e6/hz, 'recent_mask': recent,
             'caller_frames': resolve(caller), 'restore_frames': resolve(restore)}
    rows.append(entry)

args.output.parent.mkdir(parents=True, exist_ok=True)
with args.output.with_suffix('.csv').open('w', newline='') as out:
    writer = csv.DictWriter(out, fieldnames=list(rows[0]) if rows else [
        'previous_hrtim_cycles', 'hrtim_cycles', 'mask_sequence', 'mask_start_cycles', 'mask_end_cycles'])
    writer.writeheader()
    writer.writerows(rows)

positive_rows = [r for r in rows if r['jitter_us'] > threshold_cycles*1e6/hz]
associated = [r for r in positive_rows if r['recent_mask'] and r['hrtim_pending_while_masked']]
busy_associated = [r for r in associated if r['busy_us'] > threshold_cycles*1e6/hz]
by_context = collections.Counter(r['starting_context'] for r in associated)
by_caller = collections.Counter(r['caller_frames'] for r in associated)
diagnostics = {'cpu_hz': hz, 'period_cycles': period_cycles, 'recorded': recorded, 'dropped': dropped,
               'masked_intervals': total_masks, 'pending_masked_intervals': pending_masks,
               'pending_masks_busy_over_4us': long_pending_masks, 'max_busy_mask_us': max_busy*1e6/hz,
               'all_positive_abnormal_isr_intervals': positives,
               'all_positive_abnormal_with_recent_pending_mask': recent_pending_positives,
               'recorded_positive_intervals': len(positive_rows),
               'recorded_positive_with_recent_pending_mask': len(associated),
               'recorded_positive_with_recent_pending_mask_and_busy_over_4us': len(busy_associated),
               'associated_starting_contexts': dict(by_context),
               'associated_callers': dict(by_caller),
               'elf_sha256': hashlib.sha256(args.elf.read_bytes()).hexdigest(),
               'interpretation': 'Nearby-mask correlation; end timestamp precedes actual restore and trace-hook overhead. Idle sleep is separated. No absolute hardware release timestamp.'}
args.output.with_suffix('.json').write_text(json.dumps(diagnostics, indent=2))
lines = [f'Captured {recorded} abnormal ISR intervals; dropped {dropped}.',
         f'All positive abnormal ISR intervals: {positives}; recent pending-mask association: {recent_pending_positives}.',
         f'Recorded positive intervals: {len(positive_rows)}; with recent pending mask: {len(associated)}; busy >4 us: {len(busy_associated)}.',
         f'Total masked intervals: {total_masks}; HRTIM pending before restore: {pending_masks}; with busy >4 us: {long_pending_masks}.',
         f'Maximum traced busy mask: {max_busy*1e6/hz:.3f} us.',
         'Associated positive-event contexts: ' + str(dict(by_context)), 'Associated callers:']
lines.extend(f'{count} events: {caller}' for caller, count in by_caller.most_common())
lines.append('Timestamp boundaries include measurement uncertainty; correlation is not an absolute ISR-latency measurement.')
args.output.with_suffix('.txt').write_text('\n'.join(lines) + '\n')
print('\n'.join(lines))

plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10,
                     'axes.spines.top': False, 'axes.spines.right': False})
fig, axes = plt.subplots(1, 2, figsize=(13, 6))
fig.patch.set_facecolor('#f7f9fc')
recent_rows = [r for r in rows if r['recent_mask']]
for pending, color, label in [(0, '#df6c45', 'HRTIM not pending at mask-end hook'),
                              (1, '#3972a8', 'HRTIM pending at mask-end hook')]:
    subset = [r for r in recent_rows if r['hrtim_pending_while_masked'] == pending]
    if subset:
        axes[0].scatter([r['busy_us'] for r in subset], [r['jitter_us'] for r in subset],
                        s=14, alpha=0.5, color=color, label=label)
axes[0].axhline(4, color='#8993a3', linestyle='--', linewidth=1)
axes[0].axhline(-4, color='#8993a3', linestyle='--', linewidth=1)
axes[0].set_xlabel('Most recent mask: busy time excluding sleep (µs)')
axes[0].set_ylabel('HRTIM handler-entry period jitter (µs)')
axes[0].set_title('Nearby masking vs abnormal interval jitter', loc='left', fontweight='bold')
axes[0].grid(alpha=0.2)
axes[0].legend(fontsize=8, loc='best')

candidates = [r for r in busy_associated if not (r['kind_flags'] & 4)] or associated
if candidates:
    event = max(candidates, key=lambda r: r['jitter_us'])
    end = -event['mask_end_to_hrtim_us']
    start = end - event['mask_duration_us']
    reference = -event['jitter_us']
    axes[1].broken_barh([(start, end-start)], (0.35, 0.3), facecolors='#3972a8')
    axes[1].axvline(end, color='#3972a8', linestyle=':', label='End timestamp, before restore')
    axes[1].axvline(0, color='#203047', linewidth=1.7, label='HRTIM handler entry')
    axes[1].axvline(reference, color='#df6c45', linestyle='--', label='Previous handler + configured period')
    axes[1].set_ylim(0, 1)
    axes[1].set_yticks([])
    axes[1].set_xlim(min(start, reference)-3, 3)
    axes[1].set_xlabel('Time relative to HRTIM handler entry (µs)')
    axes[1].set_title('One positive-jitter event', loc='left', fontweight='bold')
    axes[1].legend(fontsize=8, loc='upper left')
    axes[1].text(0.02, 0.1,
                 f'Jitter +{event["jitter_us"]:.3f} µs · mask busy {event["busy_us"]:.3f} µs\n'
                 f'Context: {event["starting_context"]} · pending HRTIM: {event["hrtim_pending_while_masked"]}',
                 transform=axes[1].transAxes, fontsize=10)
else:
    axes[1].text(0.5, 0.5, 'No positive event with a recent pending mask', ha='center', transform=axes[1].transAxes)
fig.suptitle('Interrupt masking and HRTIM timing', x=0.065, y=0.98, ha='left', fontsize=20, fontweight='bold', color='#203047')
fig.text(0.065, 0.89, f'Twist master 0x002B001A · {period_cycles*1e6/hz:.0f} µs period · diagnostic build · idle mode\n'
         f'{recorded:,} abnormal intervals retained; {dropped:,} omitted after trace buffer filled', va='top', fontsize=11, color='#506174')
notes = [f'Positive abnormal intervals with a recent pending mask: {recent_pending_positives:,} / {positives:,} over the whole window.',
         f'In retained records: {len(associated):,} / {len(positive_rows):,}; {len(busy_associated):,} also have traced busy time >4 µs.',
         'Recent means the mask-end timestamp is less than one-quarter period before the handler entry.',
         'End timestamp is taken before unmasking and hook overhead. The dashed reference is not a hardware release timestamp.',
         'Kernel tracing adds overhead; normal idle sleep is excluded from busy time. Full caller chains are in the CSV.']
fig.text(0.065, 0.025, '\n'.join(notes), fontsize=9.5, color='#506174', va='bottom')
fig.subplots_adjust(left=0.075, right=0.97, top=0.75, bottom=0.29, wspace=0.30)
for suffix in ['.png', '.svg']:
    output = args.output.with_suffix(suffix)
    fig.savefig(output, dpi=180, facecolor=fig.get_facecolor())
    print(output)
print(args.output.with_suffix('.csv'))
