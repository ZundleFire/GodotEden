"""Builds the shareable performance page from run_perf_report.py output: a summary table over resolutions, resource
timelines (process CPU/RAM, GPU utilisation/VRAM/power), and per scenario the screenshot, frame-time trace and GPU
pass breakdown. Charts are inline SVG; screenshots are copied next to index.html (shots/<run>_<n>.jpg).

    python demo_eden/perf/perf_page.py <out_dir> [findings.html]
"""
import html
import json
import os
import shutil
import sys

RUN_ORDER = ["1920x1080", "1280x720", "1920x1080_passes"]


def load(out):
    runs = {}
    for name in RUN_ORDER:
        d = os.path.join(out, name)
        if os.path.exists(os.path.join(d, "perf.json")):
            runs[name] = {"perf": json.load(open(os.path.join(d, "perf.json"))),
                          "samples": json.load(open(os.path.join(d, "samples.json")))["samples"], "dir": d}
    return runs


def fps_chip(fps):
    cls = "good" if fps >= 55 else "ok" if fps >= 30 else "bad"
    return f'<span class="chip {cls}">{fps:.0f}</span>'


def esc(s):
    return html.escape(str(s))


def polyline(values, w, h, vmax, x0=0.0, color="var(--accent)", width=1.5):
    if not values:
        return ""
    n = max(len(values) - 1, 1)
    pts = " ".join(f"{x0 + i * w / n:.1f},{h - min(v, vmax) / vmax * h:.1f}" for i, v in enumerate(values))
    return f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linejoin="round"/>'


def frame_chart(series_by_run, vmax=None):
    """Frame time per frame over the sample, one line per resolution; budget lines at 16.7 and 33.3 ms."""
    w, h, pad = 520, 150, 34
    all_v = [v for s in series_by_run.values() for v in s]
    if not all_v:
        return ""
    vmax = vmax or max(40.0, min(max(all_v) * 1.1, 200.0))
    out = [f'<svg viewBox="0 0 {w + pad + 8} {h + 26}" class="chart" role="img" aria-label="Frame time per frame">']
    # (the 60 fps line is left out on tall scales, where it would sit on top of the 30 fps one)
    for ms, label in [(16.7, "60 fps"), (33.3, "30 fps"), (vmax, f"{vmax:.0f} ms")]:
        if ms <= vmax and not (ms == 16.7 and vmax > 80):
            y = h - ms / vmax * h + 4
            out.append(f'<line x1="{pad}" x2="{w + pad}" y1="{y:.1f}" y2="{y:.1f}" class="grid"/>')
            out.append(f'<text x="{pad - 4}" y="{y + 3:.1f}" class="tick" text-anchor="end">{label}</text>')
    colors = {"1920x1080": "var(--accent)", "1280x720": "var(--accent2)"}
    for run, s in series_by_run.items():
        out.append(f'<g transform="translate({pad},4)">{polyline(s, w, h, vmax, color=colors.get(run, "var(--ink)"))}</g>')
    out.append(f'<text x="{pad}" y="{h + 22}" class="tick">frames over the 8 s sample, ms per frame</text>')
    out.append("</svg>")
    return "".join(out)


def timeline(samples, key, label, unit, vmax=None, color="var(--accent)"):
    """One resource over the whole run, with the measured scenario windows shaded and named."""
    w, h, pad = 440, 110, 44
    vals = [s.get(key, 0.0) for s in samples]
    if not vals:
        return ""
    t_end = samples[-1]["t"] or 1.0
    vmax = vmax or max(max(vals) * 1.15, 1.0)
    out = [f'<svg viewBox="0 0 {w + pad + 6} {h + 22}" class="chart timeline" role="img" aria-label="{esc(label)}">']
    # Scenario sampling windows
    start = None
    for i, s in enumerate(samples + [{"phase": "", "t": t_end, "scenario": ""}]):
        if s["phase"] == "sample" and start is None:
            start, name = s["t"], s["scenario"]
        elif s["phase"] != "sample" and start is not None:
            x0, x1 = pad + start / t_end * w, pad + s["t"] / t_end * w
            out.append(f'<rect x="{x0:.1f}" y="2" width="{max(x1 - x0, 1):.1f}" height="{h}" class="band"><title>{esc(name)}</title></rect>')
            start = None
    for frac in (0.5, 1.0):
        y = h - frac * h + 2
        out.append(f'<line x1="{pad}" x2="{w + pad}" y1="{y:.1f}" y2="{y:.1f}" class="grid"/>')
        out.append(f'<text x="{pad - 4}" y="{y + 3:.1f}" class="tick" text-anchor="end">{vmax * frac:.0f}</text>')
    pts = " ".join(f"{pad + s['t'] / t_end * w:.1f},{h - min(v, vmax) / vmax * h + 2:.1f}" for s, v in zip(samples, vals))
    out.append(f'<polyline points="{pts}" fill="none" stroke="{color}" stroke-width="1.5"/>')
    out.append(f'<text x="{pad}" y="{h + 18}" class="tick">{esc(label)} ({esc(unit)}) over {t_end:.0f} s; shaded = measured windows</text>')
    out.append("</svg>")
    return "".join(out)


def pass_bars(passes, total_gpu):
    if not passes:
        return '<p class="muted">No pass breakdown for this scenario.</p>'
    top = passes[:8]
    vmax = max(p[1] for p in top)
    rows = []
    for name, ms in top:
        pct = ms / total_gpu * 100 if total_gpu else 0
        rows.append(f'<div class="bar"><span class="bar-name" title="{esc(name)}">{esc(name)}</span>'
                    f'<span class="bar-track"><span class="bar-fill" style="width:{ms / vmax * 100:.1f}%"></span></span>'
                    f'<span class="num">{ms:.1f} ms</span><span class="num muted">{pct:.0f}%</span></div>')
    return "".join(rows)


def build(out, findings_html=""):
    runs = load(out)
    main = runs.get("1920x1080") or next(iter(runs.values()))
    meta = main["perf"]
    shots_dir = os.path.join(out, "page", "shots")
    os.makedirs(shots_dir, exist_ok=True)
    names = [s["name"] for s in main["perf"]["scenarios"]]

    def scen(run, name):
        for s in runs.get(run, {}).get("perf", {}).get("scenarios", []):
            if s["name"] == name:
                return s
        return None

    # Summary table
    rows = []
    for name in names:
        a, b = scen("1920x1080", name), scen("1280x720", name)
        cells = [f'<th scope="row">{esc(name)}</th>']
        for s in (a, b):
            if s:
                cells += [f"<td>{fps_chip(s['fps_avg'])}</td>", f'<td class="num">{s["fps_1pct_low"]:.0f}</td>',
                          f'<td class="num">{s["frame_ms_p95"]:.1f}</td>', f'<td class="num">{s["gpu_ms_avg"]:.1f}</td>']
            else:
                cells += ['<td class="muted">-</td>'] * 4
        cells += [f'<td class="num">{a["draw_calls"]:,.0f}</td>', f'<td class="num">{a["primitives"] / 1e6:.1f} M</td>',
                  f'<td class="num">{a["vram_mb"]:,.0f}</td>', f'<td class="num">{a["static_mem_mb"]:,.0f}</td>']
        rows.append("<tr>" + "".join(cells) + "</tr>")
    table = f"""
<div class="table-wrap"><table>
<thead>
<tr><th rowspan="2" scope="col">Scenario</th><th colspan="4" scope="colgroup">1920 × 1080</th><th colspan="4" scope="colgroup">1280 × 720</th><th colspan="4" scope="colgroup">Scene load at 1080p</th></tr>
<tr><th>FPS</th><th>1% low</th><th>p95 ms</th><th>GPU ms</th><th>FPS</th><th>1% low</th><th>p95 ms</th><th>GPU ms</th><th>Draw calls</th><th>Primitives</th><th>VRAM MB</th><th>Heap MB</th></tr>
</thead><tbody>{''.join(rows)}</tbody></table></div>"""

    # Resource timelines
    tl = []
    for run in ("1920x1080", "1280x720"):
        if run not in runs:
            continue
        sm = [s for s in runs[run]["samples"] if s["phase"] in ("warmup", "sample")]
        tl.append(f'<section class="tl"><h3>{run.replace("x", " × ")}</h3><div class="tl-grid">'
                  + timeline(sm, "cpu_pct", "Process CPU", "% of one core", color="var(--accent)")
                  + timeline(sm, "rss_mb", "Process memory (RSS)", "MB", color="var(--accent2)")
                  + timeline(sm, "utilization.gpu", "GPU utilisation", "%", vmax=100, color="var(--warn)")
                  + timeline(sm, "memory.used", "GPU memory in use (whole card)", "MB", vmax=4096, color="var(--ink-soft)")
                  + "</div></section>")

    # Scenario cards
    cards = []
    for i, name in enumerate(names):
        a, b, pz = scen("1920x1080", name), scen("1280x720", name), scen("1920x1080_passes", name)
        shot = ""
        for run in ("1920x1080", "1280x720"):
            s = scen(run, name)
            src = os.path.join(runs.get(run, {}).get("dir", ""), s["shot"]) if s else ""
            if s and os.path.exists(src):
                dst = f"{run}_{i}.jpg"
                shutil.copy(src, os.path.join(shots_dir, dst))
                if not shot:
                    shot = f'<img src="shots/{dst}" alt="{esc(name)} at {run}" loading="lazy" width="1280" height="720">'
        series = {r: s["series_frame_ms"] for r, s in (("1920x1080", a), ("1280x720", b)) if s}
        stats = [("Player state", a["player_state"]), ("Weather", a["weather"]),
                 ("Frame p50 / p99 / max", f'{a["frame_ms_p50"]:.1f} / {a["frame_ms_p99"]:.1f} / {a["frame_ms_max"]:.0f} ms'),
                 ("Render CPU (main thread)", f'{a["render_cpu_ms_avg"]:.2f} ms'),
                 ("Physics step", f'{a["physics_ms_avg"]:.2f} ms'),
                 ("EdenAmbience CPU", f'{a["ambience_cpu_us"]:.0f} µs'),
                 ("Visible objects", f'{a["objects"]:,.0f}'), ("Nodes", f'{a["nodes"]:,.0f}'),
                 ("VRAM: textures / buffers", f'{a["vram_texture_mb"]:.0f} / {a["vram_buffer_mb"]:.0f} MB')]
        dl = "".join(f"<div><dt>{esc(k)}</dt><dd>{esc(v)}</dd></div>" for k, v in stats)
        cards.append(f"""
<article class="scenario" id="s{i}">
  <header><h3>{esc(name)}</h3><p class="fps-line">{fps_chip(a['fps_avg'])} fps at 1080p{f" · {fps_chip(b['fps_avg'])} at 720p" if b else ""}</p></header>
  <div class="scenario-grid">
    <figure class="shot">{shot}</figure>
    <div class="scenario-data">
      {frame_chart(series)}
      <p class="legend"><span class="key k1"></span>1920 × 1080 <span class="key k2"></span>1280 × 720</p>
      <dl class="stats">{dl}</dl>
    </div>
  </div>
  <div class="passes"><h4>Where the GPU time goes (1080p, per frame)</h4>{pass_bars(pz["passes"] if pz else [], pz["gpu_ms_avg"] if pz else 0)}</div>
</article>""")

    hw = f'{esc(meta["gpu"])} · {esc(meta["cpu"])} ({meta["cores"]} threads) · {esc(meta["os"])} · Godot {esc(meta["engine"])}'
    page = TEMPLATE.replace("{{HW}}", hw).replace("{{TABLE}}", table).replace("{{TIMELINES}}", "".join(tl)) \
        .replace("{{CARDS}}", "".join(cards)).replace("{{FINDINGS}}", findings_html)
    path = os.path.join(out, "page", "index.html")
    open(path, "w", encoding="utf-8").write(page)
    print(path)


TEMPLATE = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "perf_page_template.html"), encoding="utf-8").read()

if __name__ == "__main__":
    out = sys.argv[1]
    findings = open(sys.argv[2], encoding="utf-8").read() if len(sys.argv) > 2 else ""
    build(out, findings)
