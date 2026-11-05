#!/usr/bin/env python3
# runs bearc on every prefix and every single-char deletion of each test file, reporting crashes and
# timeouts. failing inputs are saved to build-fuzz/crashes/.

import os
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / "build-fuzz"
BEARC = BUILD_DIR / "bearc" / "bearc"
OUT_DIR = BUILD_DIR / "crashes"
TEST_DIRS = [ROOT / "tests" / "parser", ROOT / "tests" / "hir"]

TIMEOUT_S = 0.5
WORKERS = os.cpu_count() or 1
MAX_FILE_SIZE = 64 * 1024  # skips huge generated tests (e.g. hir/a52.br)
BUILD_TYPE = "Debug"


def build():
    subprocess.run(["cmake", "-S", ROOT, "-B", BUILD_DIR, f"-DCMAKE_BUILD_TYPE={BUILD_TYPE}", "-DTEST=OFF"],
                   check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["cmake", "--build", BUILD_DIR, f"-j{WORKERS}"], check=True, stdout=subprocess.DEVNULL)


def cases(data):
    for i in range(1, len(data) + 1):
        yield f"prefix_{i}", data[:i]
    for i in range(len(data)):
        yield f"del_{i}", data[:i] + data[i + 1:]


def run(src, label, data):
    with tempfile.NamedTemporaryFile(suffix=".br") as f:
        f.write(data)
        f.flush()
        try:
            rc = subprocess.run([BEARC, "--silent", f.name], stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL, timeout=TIMEOUT_S).returncode
        except subprocess.TimeoutExpired:
            rc = "timeout"
    if rc == "timeout" or rc < 0:
        name = f"{src.parent.name}_{src.stem}__{label}.br"
        (OUT_DIR / name).write_bytes(data)
        return f"{'TIMEOUT' if rc == 'timeout' else f'SIGNAL {-rc}'}: {name}"
    return None


def main():
    build()
    OUT_DIR.mkdir(exist_ok=True)
    for old in OUT_DIR.glob("*.br"):
        old.unlink()

    files = [p for d in TEST_DIRS for p in sorted(d.glob("*.br")) if p.stat().st_size <= MAX_FILE_SIZE]
    failures = 0
    with ThreadPoolExecutor(WORKERS) as pool:
        for src in files:
            print(f"fuzzing {src.relative_to(ROOT)}", flush=True)
            jobs = [pool.submit(run, src, label, data) for label, data in cases(src.read_bytes())]
            for job in jobs:
                if msg := job.result():
                    failures += 1
                    print(f"  {msg}", flush=True)

    print(f"\n{failures} failure(s); inputs saved to {OUT_DIR}")


if __name__ == "__main__":
    main()
