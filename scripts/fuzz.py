#!/usr/bin/env python3
# runs bearc on every prefix and every single-char deletion of each test file, reporting crashes and
# timeouts. failing inputs are saved to build-fuzz/crashes/. by default only a random ~1/SAMPLE_RATE subset
# of cases is run; pass --full to run every case, or --seed N to reproduce a previous run.

import argparse
import os
import random
import sys
import subprocess
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = ROOT / "build-fuzz"
BEARC = BUILD_DIR / "bearc" / "bearc"
OUT_DIR = BUILD_DIR / "crashes"
TEST_DIRS = [ROOT / "tests" / "parser", ROOT / "tests" / "hir", ROOT / "tests" / "junk"]

TIMEOUT_S = 0.5
WORKERS = os.cpu_count() or 1
MAX_FILE_SIZE = 64 * 1024  # skips huge generated tests (e.g. hir/a52.br)
BUILD_TYPE = "Debug"
SAMPLE_RATE = 50


def build():
    subprocess.run(
        [
            "cmake",
            "-S",
            ROOT,
            "-B",
            BUILD_DIR,
            f"-DCMAKE_BUILD_TYPE={BUILD_TYPE}",
            "-DTEST=OFF",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    subprocess.run(
        ["cmake", "--build", BUILD_DIR, f"-j{WORKERS}"],
        check=True,
        stdout=subprocess.DEVNULL,
    )


def cases(data):
    for i in range(1, len(data) + 1):
        yield f"prefix_{i}", data[:i]
    for i in range(len(data)):
        yield f"del_{i}", data[:i] + data[i + 1 :]


def run(src, label, data):
    with tempfile.NamedTemporaryFile(suffix=".br") as f:
        f.write(data)
        f.flush()
        try:
            rc = subprocess.run(
                [BEARC, "--silent", f.name],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
                timeout=TIMEOUT_S,
            ).returncode
        except subprocess.TimeoutExpired:
            rc = "timeout"
    if rc == "timeout" or rc < 0:
        name = f"{src.parent.name}_{src.stem}__{label}.br"
        (OUT_DIR / name).write_bytes(data)
        return f"{'TIMEOUT' if rc == 'timeout' else f'SIGNAL {-rc}'}: {name}"
    return None


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed", type=int, help="random seed for case sampling (default: random)")
    parser.add_argument("--full", action="store_true", help="run every case instead of a random subset")
    return parser.parse_args()


def main():
    args = parse_args()
    seed = args.seed if args.seed is not None else random.randrange(2**32)
    rng = random.Random(seed)
    print(f"seed: {seed}" + (" (full)" if args.full else f" (sampling ~1/{SAMPLE_RATE})"), flush=True)

    build()
    OUT_DIR.mkdir(exist_ok=True)
    for old in OUT_DIR.glob("*.br"):
        old.unlink()

    files = [
        p
        for d in TEST_DIRS
        for p in sorted(d.glob("*.br"))
        if p.stat().st_size <= MAX_FILE_SIZE
    ]
    failures = 0
    total = 0
    with ThreadPoolExecutor(WORKERS) as pool:
        for src in files:
            print(f"fuzzing {src.relative_to(ROOT)}", flush=True)
            jobs = [
                pool.submit(run, src, label, data)
                for label, data in cases(src.read_bytes())
                if args.full or rng.randrange(SAMPLE_RATE) == 0
            ]
            total += len(jobs)
            for job in jobs:
                if msg := job.result():
                    failures += 1
                    print(f"  {msg}", flush=True)

    print(f"\n{total - failures}/{total} passed; {failures} failure(s) saved to {OUT_DIR}")
    print(f"seed: {seed}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
