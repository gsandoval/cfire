#!/usr/bin/env python3
"""Cheap mutation fuzzer for `cfire check` (parser + Sema), usable without libFuzzer.

Seeds come from examples/, tests/golden/ and scripts/audit/probes/. Each iteration applies 1-6
token-aware mutations, runs `cfire check` in an isolated temp dir, and saves any input that
crashes (signal / abort / uncaught exception) or hangs to <out>/crashes/.

Usage:
    python3 -I scripts/audit/mutfuzz.py --out <scratch-dir> [--iterations 300] [--jobs 4] [--seed 1]
Prints per-job counts, then a grouped summary of crash messages.
"""
import argparse
import glob
import os
import random
import shutil
import subprocess
import sys
import tempfile
from collections import Counter
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TOKENS = [b"{", b"}", b"(", b")", b"[", b"]", b"match ", b"fn ", b"=>", b"move ", b"mut ", b"try ",
          b" or ", b"..", b"\"", b"{}", b"::", b"|x|", b"defer ", b"return ", b"none", b"\n", b"*",
          b"&", b"^", b"?", b"!", b"0x", b"1e", b"'", b"`", b"\\", b"import \"./pkg/zz\"\n", b"zz.f()"]


def seeds():
    paths = glob.glob(str(REPO / "examples/*/*.cf")) + glob.glob(str(REPO / "tests/golden/*.cf")) \
        + glob.glob(str(REPO / "scripts/audit/probes/*.cf"))
    return [Path(p).read_bytes() for p in paths]


def mutate(rng, data):
    s = bytearray(data)
    for _ in range(rng.randint(1, 6)):
        op, pos = rng.random(), rng.randrange(len(s) + 1)
        if op < 0.3 and len(s) > 1:
            del s[pos:pos + rng.randint(1, 20)]
        elif op < 0.7:
            s[pos:pos] = rng.choice(TOKENS)
        elif op < 0.85:
            s = s[:pos]
        else:
            a = rng.randrange(len(s) + 1)
            s[pos:pos] = s[a:a + rng.randint(1, 40)]
    return bytes(s)


def job(args):
    cfire, out, seed, iterations = args
    rng = random.Random(seed)
    corpus = seeds()
    crashes, hangs, messages = 0, 0, Counter()
    with tempfile.TemporaryDirectory(prefix="cfire_mutfuzz_") as tmp:
        f = Path(tmp) / "main.cf"
        for i in range(iterations):
            data = mutate(rng, rng.choice(corpus))
            f.write_bytes(data)
            try:
                r = subprocess.run([cfire, "check", "main.cf"], cwd=tmp, capture_output=True, timeout=10)
            except subprocess.TimeoutExpired:
                hangs += 1
                (out / f"hang_{seed}_{i}.cf").write_bytes(data)
                continue
            if r.returncode < 0 or r.returncode >= 128:
                crashes += 1
                (out / f"crash_{seed}_{i}_rc{r.returncode}.cf").write_bytes(data)
                err = (r.stderr or b"").decode(errors="replace").strip().splitlines()
                messages[err[-1][:140] if err else f"signal {r.returncode}"] += 1
    return seed, crashes, hangs, messages


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cfire", default=str(REPO / "cfire"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--iterations", type=int, default=300)
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    out = Path(args.out) / "crashes"
    out.mkdir(parents=True, exist_ok=True)
    # Copy the binary so a concurrent rebuild of ./cfire cannot change it mid-run.
    cfire = str(Path(args.out) / "cfire_fuzz_bin")
    shutil.copy(Path(args.cfire).resolve(), cfire)

    total = Counter()
    tasks = [(cfire, out, args.seed + k, args.iterations) for k in range(args.jobs)]
    with ProcessPoolExecutor(max_workers=args.jobs) as ex:
        for seed, crashes, hangs, messages in ex.map(job, tasks):
            print(f"seed={seed} iterations={args.iterations} crashes={crashes} hangs={hangs}")
            total.update(messages)
    print("\nCrash causes (grouped by last stderr line):")
    for msg, n in total.most_common():
        print(f"  {n:4d}  {msg}")
    print(f"\nInputs saved under {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
