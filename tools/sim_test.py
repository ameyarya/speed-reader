#!/usr/bin/env python3
"""Runs the firmware in the desktop simulator against every script in sim/scenarios.

Each scenario starts from empty storage. A first line like
    # books: Tiny.txt, Front Matter.txt
copies those books from sim/fixtures onto the simulated stick before it boots
(without one, the stick boots fresh and creates its Welcome book).

Screenshots land in .build/sim/<scenario>/ as PNG files.

    tools/sim_test.py              run everything
    tools/sim_test.py chapters     run scenarios whose name contains "chapters"
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCENARIOS = ROOT / "sim" / "scenarios"
FIXTURES = ROOT / "sim" / "fixtures"
OUT = ROOT / ".build" / "sim"
PIO = Path.home() / ".platformio" / "penv" / "bin" / "pio"
PROGRAM = ROOT / ".pio" / "build" / "sim" / "program"


def build():
    env = dict(os.environ, HOMEBREW_PREFIX=os.environ.get("HOMEBREW_PREFIX", "/opt/homebrew"))
    result = subprocess.run([str(PIO), "run", "-e", "sim"], cwd=ROOT, env=env, capture_output=True, text=True)
    if result.returncode != 0:
        print(result.stdout[-4000:], result.stderr[-2000:])
        sys.exit("simulator build failed")


def books_for(script: Path):
    first = script.read_text().splitlines()[0] if script.read_text() else ""
    if not first.startswith("# books:"):
        return None
    return [b.strip() for b in first[len("# books:"):].split(",") if b.strip()]


def run(script: Path) -> bool:
    out = OUT / script.stem
    shutil.rmtree(out, ignore_errors=True)
    storage = out / "storage"
    storage.mkdir(parents=True)
    books = books_for(script)
    if books is not None:
        (storage / "books").mkdir()
        for b in books:
            shutil.copy(FIXTURES / b, storage / "books" / b)

    env = dict(os.environ, SIM_FS=str(storage), SIM_OUT=str(out), SIM_SCRIPT=str(script))
    try:
        result = subprocess.run([str(PROGRAM)], cwd=ROOT, env=env, capture_output=True, text=True, timeout=300)
        log, code = result.stdout + result.stderr, result.returncode
    except subprocess.TimeoutExpired as e:
        log, code = (e.stdout or b"").decode() + "\n[sim] timed out", 1
    (out / "log.txt").write_text(log)

    for ppm in out.glob("*.ppm"):
        subprocess.run(["sips", "-s", "format", "png", str(ppm), "--out", str(ppm.with_suffix(".png"))],
                       capture_output=True)
        ppm.unlink()

    failed = [l for l in log.splitlines() if "FAIL" in l or "timed out" in l]
    ok = code == 0 and not failed
    print(f"{'PASS' if ok else 'FAIL'}  {script.stem}")
    for line in failed:
        print("      " + line[:240])
    if not ok and not failed:
        print("      exit code", code, "\n      " + "\n      ".join(log.splitlines()[-8:]))
    return ok


def main():
    pattern = sys.argv[1] if len(sys.argv) > 1 else ""
    build()
    scripts = sorted(p for p in SCENARIOS.glob("*.sim") if pattern in p.stem)
    results = [run(s) for s in scripts]
    print(f"\n{sum(results)}/{len(results)} scenarios passed. Screenshots: {OUT.relative_to(ROOT)}/")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
