#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""End-to-end check: the desktop app driven by an AI agent through MCP.

Starts OpenRevers, starts `openrevers-cli mcp` as an agent would, and works a
generated scan (a noisy open box with a round hole and a loose crumb) through
import, analysis, repair, hole filling, remeshing, export and undo, checking
each answer. Also checks that the agent cannot write outside its folders.

    python tests/e2e_mcp.py --app build/src/app/OpenRevers --cli build/src/openrevers-cli

On Linux run it under a display (xvfb-run). Exit code 0 = everything held.
"""
import argparse
import base64
import json
import os
import random
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def make_scan(path: Path) -> None:
    """A 40 × 30 × 10 mm box scanned from above: open at the bottom, a round
    hole of r = 6 mm in the top, 0.01 mm of noise, and a loose crumb."""
    rnd = random.Random(3)
    pts = {}

    def g(i, j, z):
        k = (i, j, z)
        if k not in pts:
            pts[k] = (i + rnd.gauss(0, 0.01), j + rnd.gauss(0, 0.01), z + rnd.gauss(0, 0.01))
        return pts[k]

    tris = []

    def quad(a, b, c, d):
        tris.append((a, b, c))
        tris.append((a, c, d))

    for i in range(40):
        for j in range(30):
            if (i + 0.5 - 20) ** 2 + (j + 0.5 - 15) ** 2 < 36:
                continue
            quad(g(i, j, 10), g(i + 1, j, 10), g(i + 1, j + 1, 10), g(i, j + 1, 10))
    for i in range(40):
        for k in range(10):
            quad(g(i, 0, k), g(i + 1, 0, k), g(i + 1, 0, k + 1), g(i, 0, k + 1))
            quad(g(i + 1, 30, k), g(i, 30, k), g(i, 30, k + 1), g(i + 1, 30, k + 1))
    for j in range(30):
        for k in range(10):
            quad(g(40, j, k), g(40, j + 1, k), g(40, j + 1, k + 1), g(40, j, k + 1))
            quad(g(0, j + 1, k), g(0, j, k), g(0, j, k + 1), g(0, j + 1, k + 1))
    c = [(60, 0, 0), (61, 0, 0), (60.5, 1, 0), (60.5, 0.5, 1)]
    tris += [(c[0], c[2], c[1]), (c[0], c[1], c[3]), (c[1], c[2], c[3]), (c[2], c[0], c[3])]
    with open(path, "wb") as f:
        f.write(b"\0" * 80)
        f.write(struct.pack("<I", len(tris)))
        for t in tris:
            f.write(struct.pack("<3f", 0, 0, 0))
            for p in t:
                f.write(struct.pack("<3f", *p))
            f.write(b"\0\0")


class Agent:
    def __init__(self, cli: str, env: dict):
        self.p = subprocess.Popen([cli, "mcp", "--lang", "es"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True, encoding="utf-8", env=env)
        self.n = 0

    def rpc(self, method, params=None):
        self.n += 1
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "id": self.n, "method": method, "params": params or {}}) + "\n")
        self.p.stdin.flush()
        line = self.p.stdout.readline()
        if not line:
            raise RuntimeError("the MCP server closed: " + self.p.stderr.read())
        return json.loads(line)

    def notify(self, method):
        self.p.stdin.write(json.dumps({"jsonrpc": "2.0", "method": method}) + "\n")
        self.p.stdin.flush()

    def call(self, name, args=None):
        r = self.rpc("tools/call", {"name": name, "arguments": args or {}})["result"]
        return r["isError"], r.get("structuredContent"), r["content"]

    def close(self):
        self.p.stdin.close()
        self.p.wait(timeout=20)
        return self.p.stderr.read()


def check(ok: bool, what: str) -> None:
    print(("PASS " if ok else "FAIL ") + what, flush=True)
    if not ok:
        raise SystemExit(1)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", required=True)
    ap.add_argument("--cli", required=True)
    ap.add_argument("--picture", help="save the agent's view capture here")
    a = ap.parse_args()

    work = Path(tempfile.mkdtemp(prefix="openrevers-e2e-"))
    conf = work / "config"
    conf.mkdir()
    port = 47000 + os.getpid() % 900
    (conf / "agent.json").write_text(json.dumps({"port": port, "token": "e2e-token-0123456789abcdef", "enabled": True,
                                                 "allowedFolders": [str(work)]}))
    env = dict(os.environ, OPENREVERS_CONFIG_DIR=str(conf))
    scan = work / "scan.stl"
    make_scan(scan)

    app_log = open(work / "app.log", "w")
    app = subprocess.Popen([a.app], env=env, stdout=app_log, stderr=subprocess.STDOUT)
    try:
        agent = None
        for _ in range(60):  # wait for the app to listen
            time.sleep(0.5)
            agent = Agent(a.cli, env)
            init = agent.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "e2e", "version": "1"}})
            if "conectado a la aplicación" in init["result"]["instructions"]:
                break
            agent.close()
            agent = None
            if app.poll() is not None:
                break
        if agent is None:
            app_log.flush()
            print("--- app output ---\n" + (work / "app.log").read_text(errors="replace")[-3000:])
            print("app exit code:", app.poll())
        check(agent is not None, "the bridge connects to the running app")
        agent.notify("notifications/initialized")
        names = {t["name"] for t in agent.rpc("tools/list")["result"]["tools"]}
        check({"mesh_import", "mesh_repair", "view_capture", "history_undo"} <= names, f"tools listed ({len(names)})")

        err, r, _ = agent.call("mesh_import", {"path": str(scan)})
        check(not err and r["faces"] == 4980, "import: 4980 triangles")
        err, r, _ = agent.call("mesh_analyze", {"body": "b1"})
        check(r["holes"] == 2 and r["components"] == 2 and not r["closed"], "analyse: 2 holes, 2 pieces, open")
        err, r, _ = agent.call("mesh_repair", {"body": "b1"})
        check(not err and r["pieceFacesRemoved"] == 4 and r["after"]["components"] == 1, "repair: the crumb is gone")
        err, r, _ = agent.call("mesh_fill_holes", {"body": "b1", "maxEdges": 60})
        check(r["filled"] == 1 and r["leftOpen"] == 1, "fill holes: the round hole closed, the open bottom left open")
        err, r, _ = agent.call("mesh_remesh", {"body": "b1", "edgeLength": 0.8, "iterations": 5})
        check(not err and r["facesAfter"] > 8000, "remesh to 0.8 mm")
        err, r, _ = agent.call("mesh_analyze", {"body": "b1"})
        size = r["bounds"]["size"]
        check(abs(size[0] - 40) < 0.1 and abs(size[2] - 10) < 0.1, f"remesh keeps the sharp box: {size}")
        err, _, content = agent.call("mesh_export", {"body": "b1", "path": str(work.parent / "outside-openrevers.stl")})
        check(err and "outside the folders" in content[0]["text"], "the agent cannot write outside its folders")
        err, r, _ = agent.call("mesh_export", {"body": "b1", "path": str(work / "clean.stl")})
        check(not err and (work / "clean.stl").stat().st_size > 100000, "export inside the allowed folder")
        err, _, _ = agent.call("view_set", {"view": "iso"})
        err, r, content = agent.call("view_capture", {"width": 800, "height": 600})
        images = [c for c in content if c["type"] == "image"]
        check(not err and images and len(base64.b64decode(images[0]["data"])) > 2000, "view capture returns a picture")
        if a.picture and images:
            Path(a.picture).write_bytes(base64.b64decode(images[0]["data"]))
        err, r, _ = agent.call("history_undo")
        check(r["undone"] == "Remallar", "undo takes the remesh back")
        err, r, _ = agent.call("session_state")
        check(r["undo"] == ["Importar malla", "Reparar malla", "Rellenar agujeros"], "the history shows every step")
        err, r, _ = agent.call("features_segment", {"body": "b1"})
        planes = [f for f in (r or {}).get("features", []) if f["type"] == "plane"]
        check(not err and len(planes) >= 5, f"shapes found: {len(planes)} planes, noise {r.get('noise') if r else None} mm")
        if a.picture:
            agent.call("view_set", {"view": "iso"})
            err2, r2, content2 = agent.call("view_capture", {"width": 800, "height": 600})
            imgs = [c for c in content2 if c.get("type") == "image"]
            if imgs:
                p = Path(a.picture)
                p.with_name(p.stem + "-shapes" + p.suffix).write_bytes(base64.b64decode(imgs[0]["data"]))
        err, r, _ = agent.call("align_principal", {"body": "b1"})
        check(not err and abs(r["bounds"]["max"][0] + r["bounds"]["min"][0]) < 1.0, "align to principal axes centres the part")
        err, r, _ = agent.call("mesh_remesh_quads", {"body": "b1", "targetFaces": 1500})
        check(not err and r["quads"] > 800 and r["tsmReady"] is False, f"quad remesh: {r.get('quads')} quads, open so not a T-Spline yet")
        err, _, content = agent.call("mesh_export_tsm", {"body": "b1", "path": str(work / "part.tsm")})
        check(err and "abierta" in content[0]["text"], "the T-Spline export explains why it cannot go on")
        cloud = work / "cloud.xyz"
        with open(cloud, "w") as f:  # 6000 points on a sphere of radius 15 mm, centred at (80, 0, 0)
            import math, random
            random.seed(3)
            for _ in range(6000):
                x, y, z = (random.gauss(0, 1) for _ in range(3))
                r = math.sqrt(x * x + y * y + z * z)
                f.write(f"{80 + 15 * x / r} {15 * y / r} {15 * z / r}\n")
        err, r, _ = agent.call("mesh_import", {"path": str(cloud)})
        check(not err and r["pointCloud"] and r["vertices"] == 6000, "import a point cloud (XYZ)")
        err, r, _ = agent.call("points_estimate_normals", {"body": r["ref"]})
        check(not err and r["points"] == 6000, "estimate normals")
        err, r, _ = agent.call("points_to_mesh", {"body": "b2", "depth": 7})
        check(not err and r["closed"] and r["faces"] > 5000, f"Poisson: closed mesh of {r.get('faces')} triangles")
        e57 = work / "scan.e57"
        e57.write_bytes((Path(__file__).parent / "data" / "e57" / "scaled_pose.e57").read_bytes())
        err, r, _ = agent.call("mesh_import", {"path": str(e57)})
        check(not err and r["pointCloud"] and r["vertices"] == 5400, "import an E57 scan (5400 valid points)")
        if a.picture:
            agent.call("view_set", {"view": "iso", "wireframe": True})
            err, r, content = agent.call("view_capture", {"width": 800, "height": 600})
            images = [c for c in content if c.get("type") == "image"]
            if images:
                p = Path(a.picture)
                p.with_name(p.stem + "-quads" + p.suffix).write_bytes(base64.b64decode(images[0]["data"]))
        agent.close()
    finally:
        app.terminate()
        try:
            app.wait(timeout=10)
        except subprocess.TimeoutExpired:
            app.kill()
    print("All end-to-end checks held.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
