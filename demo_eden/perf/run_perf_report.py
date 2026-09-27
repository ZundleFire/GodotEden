"""Runs perf/_perf_report.gd at one or more resolutions while sampling the Godot process (CPU %, RSS, threads)
and the GPU (nvidia-smi: utilisation, VRAM, power, temperature, clock) every 0.5 s, labelled by the scenario the
game says it is measuring (status.txt). Writes <out>/<res>/perf.json (from the game), samples.json, shot_*.jpg.

    python demo_eden/perf/run_perf_report.py bin/godot.windows.editor.x86_64.console.exe <out_dir> [1920x1080 1280x720 1920x1080:passes]
"""
import json
import os
import subprocess
import sys
import time

import psutil

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
GPU_FIELDS = ["utilization.gpu", "memory.used", "power.draw", "temperature.gpu", "clocks.gr"]


def gpu_sample():
    try:
        line = subprocess.run(["nvidia-smi", "--query-gpu=" + ",".join(GPU_FIELDS), "--format=csv,noheader,nounits"],
                              capture_output=True, text=True, timeout=5).stdout.strip().splitlines()[0]
        return {k: float(v) for k, v in zip(GPU_FIELDS, line.split(", "))}
    except Exception:
        return {}


def run(exe, out, res):
    os.makedirs(out, exist_ok=True)
    status_path = os.path.join(out, "status.txt")
    if os.path.exists(status_path):
        os.remove(status_path)
    # "1920x1080:passes": the same run with --gpu-profile, for per-pass GPU times (profiling costs a little)
    size, _, mode = res.partition(":")
    extra = ["--gpu-profile"] if mode == "passes" else []
    proc = subprocess.Popen([exe, "--path", PROJECT, "--resolution", size] + extra + ["-s", "res://perf/_perf_report.gd", "--",
                             "--out=" + out.replace("\\", "/")], stdout=open(os.path.join(out, "log.txt"), "w"), stderr=subprocess.STDOUT)
    # The .console.exe is a small wrapper that starts the real binary: measure that child
    p = psutil.Process(proc.pid)
    for _ in range(20):
        kids = p.children()
        if kids:
            p = kids[0]
            break
        time.sleep(0.25)
    p.cpu_percent(None)
    samples = []
    t0 = time.time()
    while proc.poll() is None:
        time.sleep(0.5)
        try:
            status = open(status_path).read().strip()
        except OSError:
            status = "loading|"
        phase, _, name = status.partition("|")
        try:
            s = {"t": round(time.time() - t0, 2), "phase": phase, "scenario": name,
                 "cpu_pct": p.cpu_percent(None), "rss_mb": p.memory_info().rss / 1048576, "threads": p.num_threads()}
        except psutil.Error:
            break
        s.update(gpu_sample())
        samples.append(s)
        if time.time() - t0 > 1200:
            proc.kill()
            break
    log = open(os.path.join(out, "log.txt"), errors="replace").read()
    print("\n".join(l for l in log.splitlines() if l.startswith("PERF") or "ERROR" in l))
    json.dump({"resolution": res, "cores": psutil.cpu_count(), "ram_gb": psutil.virtual_memory().total / 2**30,
               "samples": samples}, open(os.path.join(out, "samples.json"), "w"), indent=1)


def main():
    exe, out = sys.argv[1], sys.argv[2]
    for res in sys.argv[3:] or ["1920x1080", "1280x720"]:
        print("== run", res)
        run(os.path.abspath(exe), os.path.join(out, res.replace(":", "_")), res)


if __name__ == "__main__":
    main()
