#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file review_report.py
## @brief Writes report.html: the one page to open after a campaign (procedure §8.5).
## @par Usage
##   python tests/common/review_report.py tests/out/<campaign>
## @par What the page shows, in review order
##   1. Banner: counts, new failures, known failures, findings ready to close.
##   2. One plot per criterion listed in tests/l1/review_plots.json, failures first:
##      judged signal with its limits (dashed), thresholds (dash-dot) and judged window
##      (shaded) drawn, then the signals that drive it.
##   3. Criteria whose verdict or measured value changed since the previous campaign.
##   4. Every criterion, in a collapsed table.
## @return 0; a missing scenario folder or plot library is reported on the page, not fatal.
"""Reads the campaign's *.log result lines and scenario folders; never changes a verdict."""
from pathlib import Path
import base64,csv,html,io,json,re,sys

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
from make_record import parse, findings_register, finding_for, sort_key  # noqa: E402

SPECS = REPO / 'tests/l1/review_plots.json'
SCENARIOS = REPO / 'tests/l1/scenarios'

# Reference palette (dataviz skill): categorical slots in fixed order, text inks, status red.
SERIES = ['#2a78d6', '#eb6834', '#1baf7a', '#eda100']
INK, INK2, MUTED, GRID = '#0b0b0b', '#52514e', '#8a8984', '#e4e2dc'
SURFACE, WINDOW_FILL, CRITICAL, GOOD = '#fcfcfb', '#ebe9e3', '#e34948', '#008300'


def first_number(text):
    """@brief First number in a measured-value string. @return float or None."""
    m = re.search(r'[-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?', text or '')
    return float(m.group(0)) if m else None


def previous_campaign(out):
    """@brief Most recent earlier campaign that ran the same test scripts (same log names).
    @return Path or None."""
    logs = {p.name for p in out.glob('*.log')}
    others = [p for p in out.parent.iterdir()
              if p.is_dir() and p != out and re.match(r'\d{4}-\d\d-\d\d_', p.name)
              and {q.name for q in p.glob('*.log')} == logs and p.stat().st_mtime < out.stat().st_mtime]
    return max(others, key=lambda p: p.stat().st_mtime, default=None)


def resolve(value, scenario):
    """@brief Read 'acceptance:key[,key]' from the scenario JSON, or return value as is."""
    if not (isinstance(value, str) and value.startswith('acceptance:')):
        return value, 'review_plots.json'
    keys = value.split(':', 1)[1].split(',')
    acc = scenario.get('acceptance', {})
    got = [acc[k] for k in keys]
    return (got[0] if len(got) == 1 else got), f'scenario acceptance ({", ".join(keys)})'


def load_csv(path):
    """@brief control.csv as {column: list of floats}."""
    rows = list(csv.DictReader(path.open()))
    return {k: [float(r[k]) for r in rows] for k in rows[0]}


def plot(cid, spec, row, folder):
    """@brief Draw one review figure. @return (png_base64, notes) or (None, reason)."""
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    if not (folder / 'control.csv').exists():
        return None, f'no saved run in `{folder.name}/`'
    data = load_csv(folder / 'control.csv')
    scen_file = SCENARIOS / f'{spec["scenario"]}.json'
    scenario = json.loads(scen_file.read_text()) if scen_file.exists() else {}
    limits, limit_src = resolve(spec.get('limits'), scenario)
    window, window_src = resolve(spec.get('window'), scenario)
    t = [1e3 * x for x in data['time_s']]
    y = data[spec['signal']]
    panels = spec.get('panels', [])
    fig, axes = plt.subplots(1 + len(panels), 1, sharex=True, figsize=(9, 3.2 + 1.5 * len(panels)),
                             gridspec_kw={'height_ratios': [2.2] + [1] * len(panels)}, facecolor=SURFACE)
    axes = list(axes) if panels else [axes]
    for ax in axes:
        ax.set_facecolor(SURFACE)
        ax.grid(True, color=GRID, linewidth=0.6)
        for s in ax.spines.values():
            s.set_visible(False)
        ax.tick_params(colors=INK2, labelsize=8)
        if window:
            ax.axvspan(1e3 * window[0], 1e3 * window[1], color=WINDOW_FILL, zorder=0)
        for ev in spec.get('events', []):
            ax.axvline(1e3 * ev['t'], color=MUTED, linewidth=1, linestyle=':')
    ax = axes[0]
    if spec.get('reference'):
        ax.plot(t, data[spec['reference']], color=INK2, linewidth=1, label=spec['reference'])
    ax.plot(t, y, color=SERIES[0], linewidth=2, label=spec['signal'])
    notes = []
    for v in spec.get('thresholds', []):
        ax.axhline(v, color=MUTED, linewidth=1, linestyle='-.')
    if spec.get('thresholds'):
        notes.append(f'Thresholds {", ".join(f"{v:g}" for v in spec["thresholds"])} {spec["unit"]} (dash-dot): '
                     'crossing points, not a band the signal must stay in.')
    if limits:
        lo, hi = limits
        for v in (lo, hi):
            if v is not None:
                ax.axhline(v, color=INK2, linewidth=1, linestyle='--')
        inside = [(window is None or 1e3 * window[0] <= ti <= 1e3 * window[1]) for ti in t]
        bad = [(ti, yi) for ti, yi, w in zip(t, y, inside)
               if w and ((lo is not None and yi < lo) or (hi is not None and yi > hi))]
        if bad:
            ax.scatter(*zip(*bad), s=10, color=CRITICAL, zorder=3, label='outside limit')
        notes.append(f'Limit {lo if lo is not None else "−∞"} … {hi if hi is not None else "+∞"} '
                     f'{spec["unit"]} (dashed), from {limit_src}.')
    if window:
        notes.append(f'Judged window {1e3*window[0]:g}–{1e3*window[1]:g} ms (shaded), from {window_src}.')
    for ev in spec.get('events', []):
        ax.annotate(ev['label'], (1e3 * ev['t'], 1), xycoords=('data', 'axes fraction'),
                    xytext=(3, -12), textcoords='offset points', fontsize=8, color=INK2)
    if spec.get('peak_from_manifest') and (folder / 'manifest.json').exists():
        ext = json.loads((folder / 'manifest.json').read_text()).get('electrical_step_extrema', {})
        peak = ext.get(spec['peak_from_manifest'], {}).get('max')
        if peak is not None:
            notes.append(f'Physical peak between samples (manifest): {peak:.4g} A. The criterion judges this value.')
    ax.set_ylabel(spec['unit'], color=INK2, fontsize=9)
    ax.legend(loc='best', fontsize=8, frameon=False, labelcolor=INK)
    for ax, panel in zip(axes[1:], panels):
        for i, name in enumerate(panel['signals']):
            ax.plot(t, data[name], color=SERIES[i % len(SERIES)], linewidth=1.5, label=name)
        ax.set_ylabel(panel['unit'], color=INK2, fontsize=9)
        ax.legend(loc='best', fontsize=8, frameon=False, labelcolor=INK, ncol=min(4, len(panel['signals'])))
    axes[-1].set_xlabel('time (ms)', color=INK2, fontsize=9)
    fig.tight_layout()
    buf = io.BytesIO()
    fig.savefig(buf, format='png', dpi=110, facecolor=SURFACE)
    plt.close(fig)
    return base64.b64encode(buf.getvalue()).decode(), notes


def verdict_badge(r):
    """@brief HTML badge: verdict plus finding/new tag, never color alone."""
    v = r['verdict']
    tag = f' (known {r["finding"]["issue"]})' if r.get('finding') else (' NEW' if v == 'FAIL' else '')
    cls = {'PASS': 'pass', 'FAIL': 'fail', 'INFO': 'info', 'BLOCKED': 'blocked'}.get(v, 'info')
    icon = {'PASS': '✓', 'FAIL': '✗', 'INFO': 'i', 'BLOCKED': '–'}.get(v, '')
    return f'<span class="badge {cls}{" new" if tag == " NEW" else ""}">{icon} {v}{html.escape(tag)}</span>'


def main(out):
    out = Path(out)
    rows = parse(sorted(out.glob('*.log')))
    findings = findings_register()
    for r in rows:
        r['finding'] = finding_for(r['id'], findings) if r['verdict'] == 'FAIL' else None
    count = {v: sum(r['verdict'] == v for r in rows) for v in ('PASS', 'FAIL', 'INFO', 'BLOCKED')}
    new = [r for r in rows if r['verdict'] == 'FAIL' and not r['finding']]
    known = [r for r in rows if r['finding']]
    closable = [f for f in findings if f['open'] and any(r['id'].startswith(tuple(f['criteria'])) for r in rows)
                and not any(r['verdict'] in ('FAIL', 'BLOCKED') for r in rows if r['id'].startswith(tuple(f['criteria'])))]
    specs = {k: v for k, v in json.loads(SPECS.read_text()).items() if not k.startswith('_')} if SPECS.exists() else {}

    prev = previous_campaign(out)
    changes = []
    if prev:
        before = {r['id']: r for r in parse(sorted(prev.glob('*.log')))}
        for r in rows:
            b = before.get(r['id'])
            if b is None:
                changes.append((r, None, 'new criterion'))
            elif b['verdict'] != r['verdict']:
                changes.append((r, b, f'{b["verdict"]} → {r["verdict"]}'))
            else:
                x, y = first_number(b['measured']), first_number(r['measured'])
                if x is not None and y is not None and abs(y - x) > 0.01 * max(abs(x), 1e-9):
                    changes.append((r, b, 'measured value moved by more than 1 %'))
        for cid in before:
            if cid not in {r['id'] for r in rows}:
                changes.append(({'id': cid, 'verdict': '—', 'desc': before[cid]['desc'], 'measured': '', 'finding': None},
                                before[cid], 'not run in this campaign'))

    figures = []
    for r in sorted([r for r in rows if r['id'] in specs], key=lambda r: (r['verdict'] != 'FAIL', not (r['verdict'] == 'FAIL' and not r['finding']), r['id'])):
        spec = specs[r['id']]
        try:
            png, notes = plot(r['id'], spec, r, out / spec['scenario'])
        except Exception as e:  # report, never hide, a plotting problem
            png, notes = None, f'plot failed: {e}'
        figures.append((r, png, notes))

    e = html.escape
    p = []
    p.append(f'''<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Campaign review</title><style>
:root{{color-scheme:light;--bg:{SURFACE};--ink:{INK};--ink2:{INK2};--line:{GRID};--bad:{CRITICAL};--good:{GOOD}}}
body{{background:var(--bg);color:var(--ink);font:15px/1.5 system-ui,sans-serif;margin:0;padding:24px 16px;}}
main{{max-width:980px;margin:auto}} h1{{font-size:22px;margin:0 0 4px}} h2{{font-size:18px;margin:32px 0 8px;border-bottom:1px solid var(--line);padding-bottom:4px}}
.sub{{color:var(--ink2);font-size:13px}} .banner{{border:1px solid var(--line);border-radius:8px;padding:12px 16px;margin:16px 0}}
.banner.bad{{border-color:var(--bad)}} .counts span{{margin-right:18px;font-weight:600}}
.badge{{display:inline-block;padding:1px 8px;border-radius:10px;font-size:12px;font-weight:600;border:1px solid var(--line)}}
.badge.pass{{color:var(--good)}} .badge.fail{{color:var(--bad)}} .badge.new{{background:var(--bad);color:#fff;border-color:var(--bad)}}
.badge.info,.badge.blocked{{color:var(--ink2)}} figure{{margin:16px 0 28px}} figure img{{max-width:100%;border:1px solid var(--line);border-radius:6px}}
figcaption{{font-size:13px;color:var(--ink2)}} table{{border-collapse:collapse;width:100%;font-size:13px}} td,th{{border-bottom:1px solid var(--line);padding:4px 6px;text-align:left;vertical-align:top}}
.check li{{margin:4px 0}} code{{font-size:12px}} details summary{{cursor:pointer;color:var(--ink2)}} .wrap{{overflow-x:auto}}
</style></head><body><main>
<h1>Campaign review</h1><div class="sub">{e(out.name)}{f" · compared with {e(prev.name)}" if prev else " · no earlier campaign to compare"}</div>''')
    bad = bool(new) or not rows
    p.append(f'<div class="banner{" bad" if bad else ""}"><div class="counts">'
             f'<span>{count["PASS"]} PASS</span><span>{count["FAIL"]} FAIL</span><span>{count["INFO"]} INFO</span><span>{count["BLOCKED"]} BLOCKED</span></div>')
    p.append('<div><b>New failures:</b> ' + (', '.join(e(r['id']) for r in new) if new else 'none') + '</div>')
    p.append('<div><b>Known failures:</b> ' + (', '.join(f'{e(r["id"])} ({e(r["finding"]["issue"])})' for r in known) if known else 'none') + '</div>')
    if closable:
        p.append('<div><b>Ready to close:</b> ' + ', '.join(e(f['issue']) for f in closable) + '</div>')
    p.append('</div>')
    p.append('''<h2>How to review (procedure §8.5)</h2><ol class="check">
<li><b>Banner.</b> No new failures, and counts as expected? If a new failure shows, it needs an issue.</li>
<li><b>Plots, failures first.</b> Check the judged signal against the dashed limits inside the shaded window, then glance at the driving signals below it.</li>
<li><b>Changes.</b> Every verdict or measured value that moved since the previous campaign must have a reason you know.</li>
<li><b>Sign off</b> in <code>record.md</code>: <code>reviewed: &lt;name&gt;, &lt;date&gt;, OK | questions</code>. Quote the campaign ID in the issue or commit it supports.</li></ol>''')

    p.append('<h2>Plots</h2>')
    if not figures:
        p.append('<p class="sub">No criterion with a review plot ran in this campaign.</p>')
    for r, png, notes in figures:
        p.append(f'<figure><div><b>{e(r["id"])}</b> {verdict_badge(r)} {e(r["desc"])}</div>'
                 f'<div class="sub">Limit: {e(r["limit"])} · Measured: {e(r["measured"])}</div>')
        if png:
            p.append(f'<img alt="{e(r["id"])}: {e(r["desc"])}" src="data:image/png;base64,{png}">')
            p.append('<figcaption>' + ' '.join(e(n) for n in notes) + '</figcaption>')
        else:
            p.append(f'<figcaption>No plot: {e(str(notes))}</figcaption>')
        p.append('</figure>')

    p.append('<h2>Changed since the previous campaign</h2>')
    if not prev:
        p.append('<p class="sub">No earlier campaign in tests/out/.</p>')
    elif not changes:
        p.append('<p class="sub">Nothing changed: same verdicts, measured values within 1 %.</p>')
    else:
        p.append('<div class="wrap"><table><tr><th>Criterion</th><th>Change</th><th>Before</th><th>Now</th></tr>')
        for r, b, why in changes:
            p.append(f'<tr><td>{e(r["id"])}</td><td>{e(why)}</td><td>{e(b["measured"]) if b else "—"}</td><td>{e(r["measured"])}</td></tr>')
        p.append('</table></div>')

    p.append('<h2>All criteria</h2><details><summary>Show the full table</summary><div class="wrap"><table>'
             '<tr><th>Criterion</th><th>Verdict</th><th>Description</th><th>Limit</th><th>Measured</th></tr>')
    for r in sorted(rows, key=sort_key):
        p.append(f'<tr><td>{e(r["id"])}</td><td>{verdict_badge(r)}</td><td>{e(r["desc"])}</td><td>{e(r["limit"])}</td><td>{e(r["measured"])}</td></tr>')
    p.append('</table></div></details></main></body></html>')
    (out / 'report.html').write_text('\n'.join(p), encoding='utf-8')
    print(f'Review: {(out / "report.html").relative_to(REPO) if (out / "report.html").is_relative_to(REPO) else out / "report.html"}')
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
