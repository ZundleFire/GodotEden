"""End-to-end multiplayer test: a local SpacetimeDB, the `eden` module published fresh, the game online
(multiplayer/_mp_test.gd) and a bot player (mp_bot.py) walking around it and digging.

    python demo_eden/multiplayer/mp_test.py <godot_console_exe> [--out <dir>] [--keep-data]
"""
import argparse
import os
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
# Its own SpacetimeDB instance: port 3180 on localhost (3000, SpacetimeDB's default, is often taken), data kept in
# multiplayer/.stdb (a dot folder, so Godot skips it)
PORT = 3180
SERVER = f"http://127.0.0.1:{PORT}"
PROJECT = os.path.dirname(HERE)


def port_open(port):
    with socket.socket() as s:
        s.settimeout(0.5)
        return s.connect_ex(("127.0.0.1", port)) == 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("godot")
    ap.add_argument("--out", default=os.path.join(HERE, "..", ".mp_test"))
    ap.add_argument("--keep-data", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    server = None
    if not port_open(PORT):
        print(f"MP starting spacetime on :{PORT}")
        server = subprocess.Popen(["spacetime", "start", "--listen-addr", f"127.0.0.1:{PORT}", "--data-dir", os.path.join(HERE, ".stdb"),
                          "--non-interactive"], stdout=open(os.path.join(a.out, "spacetime.log"), "w"), stderr=subprocess.STDOUT)
        for _ in range(60):
            if port_open(PORT):
                break
            time.sleep(0.5)
    publish = ["spacetime", "publish", "eden", "--module-path", os.path.join(HERE, "server", "spacetimedb"), "-s", SERVER, "-y"]
    if not a.keep_data:
        publish.append("--delete-data=always")
    r = subprocess.run(publish, capture_output=True, text=True)
    print("MP publish:", (r.stdout + r.stderr).strip().splitlines()[-1:] or r.returncode)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        return 1

    game = subprocess.Popen([os.path.abspath(a.godot), "--audio-driver", "Dummy", "--path", PROJECT, "--resolution", "960x540", "-s",
                             "res://multiplayer/_mp_test.gd", "--", "--online", "--name=Tester", "--out=" + a.out.replace("\\", "/")],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    bot = None
    ok_game = False
    for line in game.stdout:
        line = line.rstrip()
        if line.startswith("MP_TEST") or "EdenNet" in line or "ERROR" in line or "WARNING" in line or "not editable" in line:
            print(line)
        if line == "MP_TEST ready" and bot is None:
            bot = subprocess.Popen([sys.executable, os.path.join(HERE, "mp_bot.py"), "--seconds", "20"])
        if line.startswith("MP_TEST PASS"):
            ok_game = True
    game.wait()
    ok_bot = bot is not None and bot.wait(timeout=60) == 0
    if server:  # only the server this test started: the CLI and the database process it launched
        import psutil
        for proc in psutil.Process(server.pid).children(recursive=True) + [psutil.Process(server.pid)]:
            try:
                proc.kill()
            except psutil.Error:
                pass
    print("MP", "PASS" if ok_game and ok_bot else "FAIL")
    return 0 if ok_game and ok_bot else 1


if __name__ == "__main__":
    sys.exit(main())
