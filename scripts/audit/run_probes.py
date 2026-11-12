#!/usr/bin/env python3
"""Run the language-audit probe corpus and classify each probe against its ideal behavior.

Each probe in scripts/audit/probes/*.cf starts with a header:

    // probe: <finding-id> <one-line description>
    // kind: hole | guard
    // mode: check | run | sanitize | tsan | test
    // ideal: reject [cfire/CODE] | panic [substring] | stdout <substring> | clean

Status per probe:
    OK          guard whose ideal holds (expected)
    REGRESSION  guard whose ideal no longer holds (investigate first)
    OPEN        hole whose ideal does not hold yet (known issue)
    FIXED       hole whose ideal now holds (flip it to `kind: guard`, update backlog)
    CRASH       the toolchain itself crashed (abort/signal/uncaught exception) — always a P0
    ERROR       probe could not be evaluated (bad header, timeout, missing tool)

Usage:
    python3 -I scripts/audit/run_probes.py [--cfire ./cfire] [--filter substr] [--json out.json]
Exit code is 1 if any REGRESSION or CRASH is present, else 0.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
PROBES = Path(__file__).resolve().parent / "probes"
SAN_PATTERN = re.compile(r"AddressSanitizer|runtime error:|LeakSanitizer|UndefinedBehaviorSanitizer")
TSAN_PATTERN = re.compile(r"ThreadSanitizer")
CRASH_PATTERN = re.compile(r"terminating due to uncaught exception|Assertion failed|Segmentation fault|libc\+\+abi")


def parse_header(path):
    meta = {}
    for line in path.read_text().splitlines():
        if not line.startswith("//"):
            break
        m = re.match(r"//\s*(probe|kind|mode|ideal):\s*(.*)$", line)
        if m:
            meta[m.group(1)] = m.group(2).strip()
    missing = {"probe", "kind", "mode", "ideal"} - meta.keys()
    if missing:
        raise ValueError(f"missing header fields: {sorted(missing)}")
    return meta


def run(cmd, cwd, timeout):
    try:
        # errors="replace": probes deliberately produce invalid UTF-8 (e.g. mid-codepoint str slices).
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, errors="replace", timeout=timeout)
        return r.returncode, r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return None, "TIMEOUT"


def toolchain_crashed(rc, out):
    return rc is not None and (rc < 0 or rc in (134, 139)) and bool(CRASH_PATTERN.search(out) or rc < 0)


def tsan_run(cfire, work, src, timeout):
    """Prefer a native `--sanitize=thread` mode; fall back to emit-cpp + clang++ -fsanitize=thread."""
    rc, out = run([cfire, "run", "--sanitize=thread", src.name], work, timeout)
    lowered = out.lower()
    if rc is not None and not any(w in lowered for w in ("unrecognized flag", "unknown flag", "invalid flag")):
        return rc, out, "native"
    rc, cpp = run([cfire, "emit-cpp", src.name], work, timeout)
    if rc != 0:
        return rc, cpp, "emit-cpp"
    (work / "probe.cpp").write_text(cpp)
    cxx = os.environ.get("CXX", "clang++")
    rc, out = run([cxx, "-std=c++2b", "-O1", "-g", "-fsanitize=thread", f"-I{REPO / 'runtime'}",
                   "probe.cpp", "-o", "probe_tsan"], work, timeout)
    if rc != 0:
        return rc, out, "clang"
    rc, out = run([str(work / "probe_tsan")], work, timeout)
    return rc, out, "fallback"


def evaluate(cfire, path, timeout):
    meta = parse_header(path)
    mode, ideal = meta["mode"], meta["ideal"]
    kind, _, arg = ideal.partition(" ")
    arg = arg.strip()
    with tempfile.TemporaryDirectory(prefix="cfire_probe_") as tmp:
        work = Path(tmp)
        name = "main_test.cf" if mode == "test" else "main.cf"
        src = work / name
        shutil.copy(path, src)

        if mode == "check":
            rc, out = run([cfire, "check", name], work, timeout)
        elif mode == "run":
            rc, out = run([cfire, "run", name], work, timeout)
        elif mode == "sanitize":
            rc, out = run([cfire, "run", "--sanitize", name], work, timeout)
        elif mode == "tsan":
            rc, out, _ = tsan_run(cfire, work, src, timeout)
        elif mode == "test":
            rc, out = run([cfire, "test", name], work, timeout)
        else:
            raise ValueError(f"unknown mode {mode!r}")

    if rc is None:
        return meta, "ERROR", "timeout"
    if toolchain_crashed(rc, out) and mode in ("check",):
        return meta, "CRASH", first_line(out)

    if kind == "reject":
        met = rc != 0 and "error" in out and (not arg or arg in out)
    elif kind == "panic":
        met = rc != 0 and "panic" in out.lower() and (not arg or arg in out)
    elif kind == "stdout":
        # A `cfire test` run that correctly reports a failing test exits non-zero.
        met = (rc == 0 or mode == "test") and arg in out
    elif kind == "clean":
        pattern = TSAN_PATTERN if mode == "tsan" else SAN_PATTERN
        met = rc == 0 and not pattern.search(out)
    else:
        raise ValueError(f"unknown ideal {kind!r}")

    if mode == "tsan" and TSAN_PATTERN.search(out):
        met = False

    is_guard = meta["kind"] == "guard"
    status = ("OK" if met else "REGRESSION") if is_guard else ("FIXED" if met else "OPEN")
    return meta, status, "" if met else first_line(out)


def first_line(out):
    for line in out.splitlines():
        line = line.strip()
        if line and not line.startswith("clang++"):
            return line[:160]
    return f"(no output)"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cfire", default=str(REPO / "cfire"))
    ap.add_argument("--filter", default="")
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--json", default="")
    args = ap.parse_args()
    cfire = str(Path(args.cfire).resolve())

    results = []
    for path in sorted(PROBES.glob("*.cf")):
        if args.filter and args.filter not in path.name:
            continue
        try:
            meta, status, detail = evaluate(cfire, path, args.timeout)
        except ValueError as e:
            meta, status, detail = {"probe": path.name, "kind": "?", "mode": "?", "ideal": "?"}, "ERROR", str(e)
        results.append({"file": path.name, "status": status, "detail": detail, **meta})
        print(f"{status:<10} {path.name:<38} {meta.get('probe', '')}", flush=True)

    counts = {}
    for r in results:
        counts[r["status"]] = counts.get(r["status"], 0) + 1
    print("\n| Status | Count |\n| :--- | ---: |")
    for k in ("CRASH", "REGRESSION", "OPEN", "FIXED", "OK", "ERROR"):
        if k in counts:
            print(f"| {k} | {counts[k]} |")
    print("\n| File | Status | Finding | Detail |\n| :--- | :--- | :--- | :--- |")
    for r in results:
        detail = r["detail"].replace("|", "\\|")
        print(f"| `{r['file']}` | {r['status']} | {r['probe']} | {detail} |")
    if args.json:
        Path(args.json).write_text(json.dumps(results, indent=2))
    return 1 if counts.get("REGRESSION") or counts.get("CRASH") else 0


if __name__ == "__main__":
    sys.exit(main())
