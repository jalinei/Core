#!/usr/bin/env python3
## SPDX-License-Identifier: LGPL-2.1
## @file review_report.py
## @brief Writes report.html: the one page to open after a campaign (procedure §8.5).
## @par Usage
##   python tests/common/review_report.py tests/out/<campaign>
## @par What the page shows, in review order
##   1. Banner: counts, new failures, known failures, findings ready to close.
##   2. One section per test case (scenario folder), failures first: purpose; conditions and
##      objectives; model snapshot and what the fixture changed (tests/l1/plant/snapshot.py);
##      results; figures from tests/l1/review_plots.json (limits dashed, thresholds dash-dot,
##      judged window shaded, driving signals below).
##   3. Criteria without a test-case folder (L0, block checks, BLOCKED cases).
##   4. Criteria whose verdict or measured value changed since the previous campaign.
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


MODES = {0: 'IDLE', 1: 'STARTUP', 2: 'BUSREG', 3: 'MPPT'}
SNAPSHOT = REPO / 'tests/l1/plant/snapshot.py'


def scenario_definition(meta):
    """@brief Scenario JSON of a run: the manifest copy, completed from tests/l1/scenarios by name
    for runs recorded before scenarios carried a title, purpose and criteria."""
    s = dict(meta.get('scenario', {}))
    for f in sorted(SCENARIOS.glob('*.json')):
        d = json.loads(f.read_text())
        if d.get('name') == s.get('name'):
            for k in ('title', 'purpose', 'criteria'):
                s.setdefault(k, d.get(k))
            s['_order'] = f.stem
    return s


def ensure_snapshot(folder):
    """@brief Write model.pulsim/model_power.png/model_changes.json if missing. @return error text or ''."""
    if (folder / 'model_power.png').exists() and (folder / 'model_changes.json').exists():
        return ''
    import os, subprocess
    env = {**os.environ, 'QT_QPA_PLATFORM': 'offscreen'}
    r = subprocess.run([sys.executable, str(SNAPSHOT), str(folder)], env=env, capture_output=True, text=True, timeout=300)
    return '' if r.returncode == 0 else (r.stderr.strip().splitlines() or ['snapshot failed'])[-1]


def model_parameter(model, name):
    """@brief Parameters of a named component in a saved .pulsim project (dict or None)."""
    found = []
    def walk(o):
        if isinstance(o, dict):
            if o.get('name') == name and 'parameters' in o:
                found.append(o['parameters'])
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)
    walk(model)
    return found[0] if found else None


def num(v):
    """@brief Short display of a parameter value (4 significant digits for numbers)."""
    return f'{v:.4g}' if isinstance(v, float) else str(v)


def si(value, unit):
    """@brief Engineering formatting, e.g. 0.00012 F -> '120 µF'."""
    for scale, prefix in ((1e6, 'M'), (1e3, 'k'), (1, ''), (1e-3, 'm'), (1e-6, 'µ'), (1e-9, 'n')):
        if abs(value) >= scale or scale == 1e-9:
            return f'{value / scale:.4g} {prefix}{unit}'


def conditions(folder, meta, scen):
    """@brief Rows (label, html) describing what the test ran on and what it was given."""
    e = html.escape
    rows = []
    model = json.loads((folder / 'model.pulsim').read_text()) if (folder / 'model.pulsim').exists() else {}
    ch = json.loads((folder / 'model_changes.json').read_text()) if (folder / 'model_changes.json').exists() else {'changed': [], 'added': []}
    rows.append(('Plant model', f'switched, <code>full_model.pulsim</code> sha256 {e(meta.get("model_sha256", "?")[:12])}…'
                 + (' with the <b>synthetic PV panel</b>' if meta.get('pv_model_available') else '')))
    src, rs = model_parameter(model, 'Vsource'), model_parameter(model, 'Rsource')
    if meta.get('pv_model_available'):
        ref = meta.get('pv_reference', {})
        rows.append(('Input source', e(f'synthetic PV panel, MPP {ref.get("Pmp_W", "?"):.3g} W at {ref.get("Vmp_V", "?"):.3g} V')
                     if ref else 'synthetic PV panel'))
    elif src:
        rows.append(('Input source', e(f'{src["waveform"].get("value")} V ideal DC') + (e(f' + {si(rs["resistance"], "Ω")}') if rs else '')))
    cbus = model_parameter(model, 'Cbus')
    if cbus:
        v0 = float(cbus.get('initial_voltage', 0) or 0)
        state = f'<b>PRECHARGED to {v0:g} V</b>' if v0 > 0 else '<b>discharged (0 V)</b>'
        rows.append(('DC bus at t = 0', f'{state}, {e(si(cbus["capacitance"], "F"))}'))
    other = [c for c in ch['changed'] if c['component'] != 'Cbus']
    if other:
        rows.append(('Other initial states', e(', '.join(f'{c["component"]} {c["parameter"]} = {c["this_run"]:.4g}' for c in other))))
    loads = []
    bleed = model_parameter(model, 'Rbus_bleed')
    if bleed:
        loads.append(f'{si(bleed["resistance"], "Ω")} bleed (board)')
    fixture = meta.get('fixture') or {}
    for a in ch['added']:
        if a['type'] == 'RESISTOR':
            when = f', switched in at {1e3 * fixture["load_step_s"]:g} ms' if a['component'] == 'DCDC_step_load' and 'load_step_s' in fixture else ', from t = 0'
            loads.append(f'{si(a["parameters"]["resistance"], "Ω")} {a["component"]}{when}')
    if loads:
        rows.append(('Loads on the bus', e('; '.join(loads))))
    grid = model_parameter(model, 'Vgrid')
    if grid and grid.get('waveform', {}).get('type') == 'sine':
        w = grid['waveform']
        rows.append(('Grid', e(f'{w["amplitude"] / 2 ** .5:.1f} V rms, {w["frequency"]:g} Hz, present from t = 0; inverter bridge and relay off')))
    cmds = []
    for q in scen.get('commands', []):
        cmds.append(f'{1e3 * q["time_s"]:g} ms: <b>{MODES.get(q["mode"], q["mode"])}</b>, v_bus_ref {q["v_bus_ref"]:g} V, '
                    f'sync {"requested" if q["sync_request"] else "off"}, VAC samples {"delivered" if q["vac_updated"] else "<b>lost</b>"}')
    if cmds:
        rows.append(('Operator commands', '<br>'.join(cmds)))
    fw = meta.get('firmware', {})
    if fw:
        rows.append(('Firmware', e(f'Core {fw.get("core_commit", "?")[:7]}{" (dirty)" if fw.get("core_dirty") else ""}, '
                                   f'profile {fw.get("profile", "?")}, inverter library {fw.get("inverter_commit", "?")[:7]}')))
    rows.append(('Simulation', e(f'{1e3 * scen.get("duration_s", 0):g} ms, step {si(scen.get("dt_s", 0), "s")}, '
                                 f'stored every {si(scen.get("store_period_s", 0), "s")}, Pulsim {meta.get("pulsim", "?")}, '
                                 f'{meta.get("wall_s", 0):.0f} s of computing')))
    return rows, [(f'{c["component"]}.{c["parameter"]}', c['accepted'], c['this_run']) for c in ch['changed']] + \
        [(f'{a["component"]} added ({a["type"].lower()})', '—', ', '.join(f'{k} {num(v)}' for k, v in a['parameters'].items() if k != 'enable_thermal_port')) for a in ch['added']]


def test_cases(out, rows):
    """@brief One entry per scenario folder of the campaign, with its criteria, conditions and snapshot."""
    cases = []
    for man in sorted(out.glob('*/manifest.json')):
        folder = man.parent
        meta = json.loads(man.read_text())
        scen = scenario_definition(meta)
        prefixes = tuple(scen.get('criteria') or [])
        mine = [r for r in rows if prefixes and r['id'].startswith(prefixes)]
        err = ensure_snapshot(folder)
        cond, changes = conditions(folder, meta, scen)
        cases.append({'folder': folder, 'title': scen.get('title') or scen.get('name', folder.name),
                      'purpose': scen.get('purpose'), 'rows': mine, 'conditions': cond, 'changes': changes,
                      'snapshot_error': err, 'order': scen.get('_order', folder.name)})
    # A criterion judged on several runs (the PV 25/50 ns pair) is listed under each of them.
    return cases


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

    e = html.escape
    cases = test_cases(out, rows)
    claimed = {r['id'] for c in cases for r in c['rows']}
    figures_for = {}
    for c in cases:
        figs = []
        for r in sorted([r for r in c['rows'] if r['id'] in specs], key=lambda r: (r['verdict'] != 'FAIL', r['id'])):
            spec = specs[r['id']]
            try:
                png, notes = plot(r['id'], spec, r, c['folder'])
            except Exception as ex:  # report, never hide, a plotting problem
                png, notes = None, f'plot failed: {ex}'
            figs.append((r, png, notes))
        figures_for[c['folder'].name] = figs
    cases.sort(key=lambda c: (not any(r['verdict'] == 'FAIL' for r in c['rows']), c['order']))

    p = []
    p.append(f'''<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Campaign review</title><style>
:root{{color-scheme:light;--bg:{SURFACE};--ink:{INK};--ink2:{INK2};--line:{GRID};--bad:{CRITICAL};--good:{GOOD}}}
body{{background:var(--bg);color:var(--ink);font:15px/1.5 system-ui,sans-serif;margin:0;padding:24px 16px;}}
main{{max-width:1000px;margin:auto}} h1{{font-size:22px;margin:0 0 4px}} h2{{font-size:19px;margin:40px 0 4px;border-bottom:2px solid var(--line);padding-bottom:4px}}
h3{{font-size:15px;margin:20px 0 6px;color:var(--ink2);text-transform:uppercase;letter-spacing:.04em}}
.sub{{color:var(--ink2);font-size:13px}} .banner{{border:1px solid var(--line);border-radius:8px;padding:12px 16px;margin:16px 0}}
.banner.bad{{border-color:var(--bad)}} .counts span{{margin-right:18px;font-weight:600}} .purpose{{margin:6px 0 4px}}
.badge{{display:inline-block;padding:1px 8px;border-radius:10px;font-size:12px;font-weight:600;border:1px solid var(--line);white-space:nowrap}}
.badge.pass{{color:var(--good)}} .badge.fail{{color:var(--bad)}} .badge.new{{background:var(--bad);color:#fff;border-color:var(--bad)}}
.badge.info,.badge.blocked{{color:var(--ink2)}} figure{{margin:12px 0 24px}} figure img{{max-width:100%;border:1px solid var(--line);border-radius:6px;background:#fff}}
figcaption{{font-size:13px;color:var(--ink2)}} table{{border-collapse:collapse;width:100%;font-size:13px;margin:4px 0}}
td,th{{border-bottom:1px solid var(--line);padding:4px 6px;text-align:left;vertical-align:top}} th{{color:var(--ink2);font-weight:600}}
table.kv td:first-child{{width:170px;color:var(--ink2)}} .check li{{margin:4px 0}} code{{font-size:12px}}
details summary{{cursor:pointer;color:var(--ink2)}} .wrap{{overflow-x:auto}} nav a{{margin-right:12px;font-size:13px}}
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
    if cases:
        p.append('<nav><b>Test cases:</b> ' + ' '.join(
            f'<a href="#{e(c["folder"].name)}">{"✗ " if any(r["verdict"] == "FAIL" for r in c["rows"]) else ""}{e(c["title"])}</a>' for c in cases) + '</nav>')
    p.append('''<h3>How to review (procedure §8.5)</h3><ol class="check">
<li><b>Banner.</b> No new failures, and counts as expected? A new failure needs an issue.</li>
<li><b>Each test case, failures first:</b> read the purpose and the <i>conditions</i> table (initial state, loads, commands) before the plots; then check each plot against its dashed limits inside the shaded window.</li>
<li><b>Changes</b> since the previous campaign: each one needs a reason you know.</li>
<li><b>Sign off</b> in <code>record.md</code>: <code>reviewed: &lt;name&gt;, &lt;date&gt;, OK | questions</code>.</li></ol>''')

    for c in cases:
        f = c['folder']
        failed_here = any(r['verdict'] == 'FAIL' for r in c['rows'])
        p.append(f'<h2 id="{e(f.name)}">{"✗ " if failed_here else ""}{e(c["title"])} <span class="sub">({e(f.name)}/)</span></h2>')
        if c['purpose']:
            p.append(f'<p class="purpose">{e(c["purpose"])}</p>')
        p.append('<h3>1 · Test conditions and objectives</h3><div class="wrap"><table class="kv">')
        for k, v in c['conditions']:
            p.append(f'<tr><td>{e(k)}</td><td>{v}</td></tr>')
        p.append('</table></div>')
        if c['rows']:
            p.append('<table><tr><th>Objective</th><th>What is checked</th><th>Limit</th></tr>')
            for r in sorted(c['rows'], key=lambda r: r['id']):
                p.append(f'<tr><td>{e(r["id"])}</td><td>{e(r["desc"])}</td><td>{e(r["limit"])}</td></tr>')
            p.append('</table>')
        p.append('<h3>2 · Model snapshot</h3>')
        if (f / 'model_power.png').exists():
            p.append(f'<figure><a href="{e(f.name)}/model_power.png"><img alt="Power stage of the model this test ran on" src="{e(f.name)}/model_power.png"></a>'
                     f'<figcaption>Power stage as run (click for full size). Everything, including the firmware block: '
                     f'<a href="{e(f.name)}/model_full.png">model_full.png</a>. Open it in PulsimGUI: '
                     f'<code>tests/l1/open_gui.sh --results {e(str(f.relative_to(REPO)) if f.is_relative_to(REPO) else str(f))}</code> '
                     f'(the exact project is <code>{e(f.name)}/model.pulsim</code>).</figcaption></figure>')
        else:
            p.append(f'<p class="sub">No snapshot: {e(c["snapshot_error"] or "not generated")}</p>')
        if c['changes']:
            p.append('<table><tr><th>Changed from the accepted model</th><th>Accepted</th><th>This test</th></tr>')
            for ch in c['changes']:
                p.append(f'<tr><td>{e(ch[0])}</td><td>{e(num(ch[1]))}</td><td><b>{e(num(ch[2]))}</b></td></tr>')
            p.append('</table>')
        p.append('<h3>3 · Results</h3><table><tr><th>Criterion</th><th>Verdict</th><th>Measured</th></tr>')
        for r in sorted(c['rows'], key=sort_key):
            p.append(f'<tr><td>{e(r["id"])}</td><td>{verdict_badge(r)}</td><td>{e(r["measured"])}</td></tr>')
        p.append('</table>')
        p.append('<h3>4 · Figures</h3>')
        figs = figures_for[f.name]
        if not figs:
            p.append('<p class="sub">No review plot is declared for this test case (tests/l1/review_plots.json).</p>')
        for r, png, notes in figs:
            p.append(f'<figure><div><b>{e(r["id"])}</b> {verdict_badge(r)} {e(r["desc"])}</div>')
            if png:
                p.append(f'<img alt="{e(r["id"])}: {e(r["desc"])}" src="data:image/png;base64,{png}">')
                p.append('<figcaption>' + ' '.join(e(n) for n in notes) + '</figcaption>')
            else:
                p.append(f'<figcaption>No plot: {e(str(notes))}</figcaption>')
            p.append('</figure>')

    rest = [r for r in rows if r['id'] not in claimed]
    if rest:
        p.append('<h2 id="other">Criteria without a test-case folder</h2><p class="sub">L0 tests, '
                 'L1 checks that run no plant scenario, fixture checks and cases still BLOCKED.</p>')
        p.append('<div class="wrap"><table><tr><th>Criterion</th><th>Verdict</th><th>Description</th><th>Limit</th><th>Measured</th></tr>')
        for r in sorted(rest, key=sort_key):
            p.append(f'<tr><td>{e(r["id"])}</td><td>{verdict_badge(r)}</td><td>{e(r["desc"])}</td><td>{e(r["limit"])}</td><td>{e(r["measured"])}</td></tr>')
        p.append('</table></div>')

    p.append('<h2>Changed since the previous campaign</h2>')
    if not prev:
        p.append('<p class="sub">No earlier campaign of the same scope in tests/out/.</p>')
    elif not changes:
        p.append('<p class="sub">Nothing changed: same verdicts, measured values within 1 %.</p>')
    else:
        p.append('<div class="wrap"><table><tr><th>Criterion</th><th>Change</th><th>Before</th><th>Now</th></tr>')
        for r, b, why in changes:
            p.append(f'<tr><td>{e(r["id"])}</td><td>{e(why)}</td><td>{e(b["measured"]) if b else "—"}</td><td>{e(r["measured"])}</td></tr>')
        p.append('</table></div>')
    p.append('</main></body></html>')
    (out / 'report.html').write_text('\n'.join(p), encoding='utf-8')
    print(f'Review: {(out / "report.html").relative_to(REPO) if (out / "report.html").is_relative_to(REPO) else out / "report.html"}')
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1]))
