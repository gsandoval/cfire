#!/usr/bin/env python3
"""Language conformance corpus runner (CF-161).

Each tests/conformance/**/*.cf file pins one language expectation with leading `//` directives:

    // expect: stdout              exact program stdout follows, one `// | <line>` per line
    // | hello
    // expect: error cfire/CODE @L[:C]     (repeatable) the exact set of errors `cfire check` must report
    // expect: warning cfire/CODE @L       (repeatable) warnings that must be present
    // expect: panic "<substring>" [@L]    program must panic (non-zero exit, stderr contains `panic`)
    // expect: exit N                      program exit status (default 0, or non-zero with `panic`)
    // expect: xfail CF-xxx <reason>       ideal expectations above are NOT met yet (tracked hole)
    <code> //~ error cfire/CODE            inline form, preferred: expected error on this very line
    <code> //~ warning cfire/CODE          inline expected warning
    <code> //~ panic "<substring>"         inline expected panic attributed to this line
    // flags: --sanitize | --sanitize=thread   extra run flags (thread falls back to emit-cpp + TSan)

Files named *_test.cf run with `cfire test` instead of `cfire run`.

Outcomes: PASS, FAIL, XFAIL (known hole, expected), XPASS (hole now fixed: drop the xfail line).
Exit status is 1 if any FAIL or XPASS.

Usage: python3 -I scripts/run_conformance.py [--cfire ./cfire] [--filter glob] [--jobs N] [--run-flags=--sanitize] [-v]
"""
import argparse
import fnmatch
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CORPUS = REPO / "tests" / "conformance"
TSAN_REPORT = re.compile(r"WARNING: ThreadSanitizer")


@dataclass
class Spec:
    stdout: list | None = None
    errors: set = field(default_factory=set)        # {(code, line, col|None)}
    warnings: set = field(default_factory=set)      # {(code, line)}
    panic: tuple | None = None                      # (substring, line|None)
    exit: int | None = None
    xfail: str | None = None
    flags: list = field(default_factory=list)


class DirectiveError(Exception):
    pass


def parse_spec(text):
    spec = Spec()
    lines = text.splitlines()
    i = 0
    while i < len(lines) and lines[i].startswith("//"):
        body = lines[i][2:].strip()
        if body.startswith("expect:"):
            rest = body[len("expect:"):].strip()
            kind, _, arg = rest.partition(" ")
            arg = arg.strip()
            if kind == "stdout":
                spec.stdout = []
                while i + 1 < len(lines) and lines[i + 1].startswith("// |"):
                    i += 1
                    spec.stdout.append(lines[i][4:].removeprefix(" "))
            elif kind in ("error", "warning"):
                m = re.fullmatch(r"(cfire/[A-Z0-9-]+)\s+@(\d+)(?::(\d+))?", arg)
                if not m:
                    raise DirectiveError(f"bad {kind} directive: {body!r}")
                code, line, col = m.group(1), int(m.group(2)), m.group(3)
                if kind == "error":
                    spec.errors.add((code, line, int(col) if col else None))
                else:
                    spec.warnings.add((code, line))
            elif kind == "panic":
                m = re.fullmatch(r'"([^"]*)"(?:\s+@(\d+))?', arg)
                if not m:
                    raise DirectiveError(f"bad panic directive: {body!r}")
                spec.panic = (m.group(1), int(m.group(2)) if m.group(2) else None)
            elif kind == "exit":
                spec.exit = int(arg)
            elif kind == "xfail":
                if not re.match(r"CF-\d+", arg):
                    raise DirectiveError(f"xfail must cite a backlog ID: {body!r}")
                spec.xfail = arg
            else:
                raise DirectiveError(f"unknown expectation {kind!r}")
        elif body.startswith("flags:"):
            spec.flags = body[len("flags:"):].split()
        i += 1
    # Inline annotations travel with the code they describe, so adding or removing header directives
    # (e.g. dropping an `xfail` line) never invalidates expected line numbers:
    #     xs.push_back(1) //~ error cfire/UNKNOWN-METHOD
    #     println("{}", xs[i]) //~ panic "index out of bounds"
    for lineno, line in enumerate(lines, start=1):
        m = re.search(r"//~\s*(error|warning)\s+(cfire/[A-Z0-9-]+)\s*$", line)
        if m:
            if m.group(1) == "error":
                spec.errors.add((m.group(2), lineno, None))
            else:
                spec.warnings.add((m.group(2), lineno))
            continue
        m = re.search(r'//~\s*panic\s+"([^"]*)"\s*$', line)
        if m:
            if spec.panic is not None:
                raise DirectiveError("more than one panic expectation")
            spec.panic = (m.group(1), lineno)
        elif "//~" in line:
            raise DirectiveError(f"bad inline annotation on line {lineno}: {line.strip()!r}")
    if spec.stdout is None and not spec.errors and spec.panic is None and spec.exit is None and not spec.warnings:
        raise DirectiveError("file declares no expectation")
    return spec


def run(cmd, cwd, timeout):
    try:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, errors="replace", timeout=timeout)
        return r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired:
        return None, "", "TIMEOUT"


# Q47 (CF-184): clang's Windows targets have no ThreadSanitizer. Files that ask for it still run there, unsanitized,
# so their output is checked; the summary notes how many ran without TSan.
TSAN_UNSUPPORTED = sys.platform == "win32" or os.environ.get("MSYSTEM", "").upper().startswith(("MINGW", "CLANG", "UCRT"))
TSAN_SKIPPED = []


def run_program(cfire, work, name, spec, is_test, timeout):
    if "--sanitize=thread" in spec.flags and TSAN_UNSUPPORTED:
        TSAN_SKIPPED.append(name)
        spec.flags = [f for f in spec.flags if f != "--sanitize=thread"]
    if "--sanitize=thread" in spec.flags and not is_test:
        rc, out, err = run([cfire, "run", "--sanitize=thread", name], work, timeout)
        if "unrecognized flag" not in err and "unrecognized flag" not in out:
            return rc, out, err
        # Fallback until CF-164 adds a native mode.
        rc, cpp, err = run([cfire, "emit-cpp", name], work, timeout)
        if rc != 0:
            return rc, cpp, err
        (work / "prog.cpp").write_text(cpp)
        cxx = os.environ.get("CXX", "clang++")
        rc, out, err = run([cxx, "-std=c++2b", "-O1", "-g", "-fsanitize=thread", f"-I{REPO / 'runtime'}",
                            "prog.cpp", "-o", "prog_tsan", "-pthread"], work, timeout)
        if rc != 0:
            return rc, out, err
        return run([str(work / "prog_tsan")], work, timeout)
    flags = [f for f in spec.flags if f != "--sanitize=thread"]
    return run([cfire, "test" if is_test else "run", *flags, name], work, timeout)


def evaluate(cfire, path, timeout, run_flags=()):
    """Returns (spec, met: bool, reasons: list[str])."""
    spec = parse_spec(path.read_text())
    for flag in run_flags:
        if flag not in spec.flags:
            spec.flags.append(flag)
    reasons = []
    is_test = path.name.endswith("_test.cf")
    with tempfile.TemporaryDirectory(prefix="cfire_conf_") as tmp:
        work = Path(tmp)
        name = "main_test.cf" if is_test else "main.cf"
        shutil.copy(path, work / name)

        rc, out, err = run([cfire, "check", "--json", name], work, timeout)
        if rc is None:
            return spec, False, ["check timed out"]
        if rc < 0 or rc >= 128:
            return spec, False, [f"cfire check crashed (rc={rc}): {err.strip()[-200:]}"]
        try:
            report = json.loads(out[out.index("{"):])
        except (ValueError, json.JSONDecodeError):
            return spec, False, [f"unparseable check --json output: {(out + err)[:200]}"]
        diags = report.get("diagnostics", [])
        got_errors = {(d.get("code", ""), d.get("line", 0), d.get("col", 0)) for d in diags if d.get("severity") == "error"}
        got_warnings = {(d.get("code", ""), d.get("line", 0)) for d in diags if d.get("severity") == "warning"}

        # ICE gate (CF-148): `cfire/TYPE-ERROR` means clang rejected generated C++ for a program Sema
        # accepted. It is never an acceptable expectation; Sema must report a cfire diagnostic instead.
        if any(c == "cfire/TYPE-ERROR" for c, _, _ in spec.errors):
            raise DirectiveError("cfire/TYPE-ERROR (a clang backend error) can't be an expected diagnostic")
        for code, line, _ in sorted(got_errors):
            if code == "cfire/TYPE-ERROR":
                reasons.append(f"internal compiler error: clang rejected generated C++ at line {line} for a program Sema accepted")
        got_errors = {e for e in got_errors if e[0] != "cfire/TYPE-ERROR"}
        want_errors = {(c, l) for c, l, _ in spec.errors}
        have_errors = {(c, l) for c, l, _ in got_errors}
        if want_errors != have_errors:
            missing = sorted(want_errors - have_errors)
            extra = sorted(have_errors - want_errors)
            if missing:
                reasons.append(f"missing errors {missing}")
            if extra:
                reasons.append(f"unexpected errors {extra}")
        for code, line, col in spec.errors:
            if col is not None and (code, line, col) not in got_errors:
                reasons.append(f"error {code} @{line} expected at column {col}")
        for w in sorted(spec.warnings - got_warnings):
            reasons.append(f"missing warning {w}")

        needs_run = spec.stdout is not None or spec.panic is not None or spec.exit is not None
        if needs_run and not have_errors:
            rc, out, err = run_program(cfire, work, name, spec, is_test, timeout)
            if rc is None:
                return spec, False, reasons + ["program timed out"]
            combined = out + err
            if spec.panic is not None:
                sub, line = spec.panic
                if rc == 0 or "panic" not in combined.lower() or sub not in combined:
                    reasons.append(f"expected panic containing {sub!r}, got rc={rc}: {combined.strip()[-160:]!r}")
                elif line is not None and not re.search(rf"\.cf:{line}\b", combined):
                    reasons.append(f"panic not attributed to line {line}: {combined.strip()[-160:]!r}")
            elif spec.exit is not None:
                if rc != spec.exit:
                    reasons.append(f"expected exit {spec.exit}, got {rc}")
            elif rc != 0:
                reasons.append(f"program exited {rc}: {combined.strip()[-200:]!r}")
            if spec.stdout is not None:
                got = [l.rstrip() for l in out.rstrip("\n").splitlines()]
                want = [l.rstrip() for l in spec.stdout]
                if is_test:
                    # Test-runner framing lines are allowed around the expected lines (subsequence match).
                    it = iter(got)
                    if not all(any(w == g for g in it) for w in want):
                        reasons.append(f"test output missing {want!r}; got {got[-8:]!r}")
                elif got != want:
                    reasons.append(f"stdout mismatch: want {want!r}, got {got!r}")
            if "--sanitize=thread" in spec.flags and TSAN_REPORT.search(combined):
                reasons.append("ThreadSanitizer reported a data race")
            if "--sanitize" in spec.flags and re.search(r"AddressSanitizer|runtime error:", combined):
                reasons.append("ASan/UBSan reported an error")
        elif needs_run and have_errors and not spec.errors:
            pass  # already reported as unexpected errors
    return spec, not reasons, reasons


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cfire", default=str(REPO / "cfire"))
    ap.add_argument("--filter", default="*", help="glob over paths relative to tests/conformance")
    ap.add_argument("--jobs", type=int, default=max(1, min(8, (os.cpu_count() or 2) // 2)))
    ap.add_argument("--timeout", type=int, default=300)
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--corpus", default=str(CORPUS))
    ap.add_argument("--run-flags", default="", help="extra flags for every program run, e.g. --sanitize")
    ap.add_argument("--self-test", action="store_true", help="verify the runner's own classification logic")
    args = ap.parse_args()
    cfire = str(Path(args.cfire).resolve())
    if args.self_test:
        return self_test(cfire, args.timeout)
    corpus = Path(args.corpus)

    files = sorted(p for p in corpus.rglob("*.cf")
                   if fnmatch.fnmatch(str(p.relative_to(corpus)), args.filter))
    if not files:
        print(f"no conformance files match {args.filter!r}", file=sys.stderr)
        return 1

    def task(path):
        rel = str(path.relative_to(corpus))
        try:
            spec, met, reasons = evaluate(cfire, path, args.timeout, args.run_flags.split())
        except DirectiveError as e:
            return rel, "FAIL", [f"directive error: {e}"], None
        if spec.xfail:
            return rel, ("XPASS" if met else "XFAIL"), reasons, spec.xfail
        return rel, ("PASS" if met else "FAIL"), reasons, None

    counts = {"PASS": 0, "FAIL": 0, "XFAIL": 0, "XPASS": 0}
    with ThreadPoolExecutor(max_workers=args.jobs) as ex:
        for rel, status, reasons, xfail in ex.map(task, files):
            counts[status] += 1
            tag = f"  [{xfail.split()[0]}]" if xfail else ""
            if status in ("FAIL", "XPASS") or args.verbose:
                print(f"{status:<6} {rel}{tag}")
                if status == "XPASS":
                    print(f"         hole is fixed: remove the `expect: xfail {xfail}` line")
                for r in reasons if status != "XPASS" else []:
                    print(f"         - {r}")
            elif status == "XFAIL" and args.verbose:
                print(f"XFAIL  {rel}{tag}")
    print(f"Conformance: {counts['PASS']} passed, {counts['XFAIL']} xfail (tracked holes), "
          f"{counts['XPASS']} xpass, {counts['FAIL']} failed (total {len(files)})")
    if TSAN_SKIPPED:
        print(f"note: {len(TSAN_SKIPPED)} file(s) asked for ThreadSanitizer, which this platform lacks; they ran unsanitized (Q47)")
    return 1 if counts["FAIL"] or counts["XPASS"] else 0


SELF_TEST_CASES = {
    # name: (source, expected classification)
    "pass_stdout.cf": ('// expect: stdout\n// | hi\npackage main\nfn main() {\n    println("hi")\n}\n', "PASS"),
    "fail_stdout.cf": ('// expect: stdout\n// | bye\npackage main\nfn main() {\n    println("hi")\n}\n', "FAIL"),
    "fail_extra_error.cf": ('// expect: stdout\n// | 1\npackage main\nfn main() {\n    x := 1\n    x = 2\n    println("{}", x)\n}\n', "FAIL"),
    "pass_error.cf": ('// expect: error cfire/ASSIGN-IMMUTABLE @5\npackage main\nfn main() {\n    x := 1\n    x = 2\n}\n', "PASS"),
    "fail_wrong_line.cf": ('// expect: error cfire/ASSIGN-IMMUTABLE @4\npackage main\nfn main() {\n    x := 1\n    x = 2\n}\n', "FAIL"),
    "xfail_unmet.cf": ('// expect: stdout\n// | bye\n// expect: xfail CF-000 demo\npackage main\nfn main() {\n    println("hi")\n}\n', "XFAIL"),
    "xpass_met.cf": ('// expect: stdout\n// | hi\n// expect: xfail CF-000 demo\npackage main\nfn main() {\n    println("hi")\n}\n', "XPASS"),
    "bad_directive.cf": ('// expect: stdot\npackage main\nfn main() {\n}\n', "FAIL"),
    "pass_inline_error.cf": ('// expect: xfail CF-000 shifts must not matter\n// expect: stdout\n// | never\npackage main\nfn main() {\n    x := 1\n    x = 2 //~ error cfire/ASSIGN-IMMUTABLE\n}\n', "XPASS"),
    "bad_inline.cf": ('package main\nfn main() {\n    x := 1 //~ eror cfire/X\n}\n', "FAIL"),
    "bad_type_error_expectation.cf": ('package main\nfn main() {\n    x := 1 //~ error cfire/TYPE-ERROR\n}\n', "FAIL"),
    "pass_panic.cf": ('// expect: panic "index out of bounds" @6\npackage main\nfn main() {\n    xs: []i32 = [1]\n    i := 3\n    println("{}", xs[i])\n}\n', "PASS"),
}


def classify(cfire, path, timeout):
    try:
        spec, met, _ = evaluate(cfire, path, timeout)
    except DirectiveError:
        return "FAIL"
    if spec.xfail:
        return "XPASS" if met else "XFAIL"
    return "PASS" if met else "FAIL"


def self_test(cfire, timeout):
    bad = 0
    with tempfile.TemporaryDirectory(prefix="cfire_conf_selftest_") as tmp:
        for name, (src, want) in SELF_TEST_CASES.items():
            p = Path(tmp) / name
            p.write_text(src)
            got = classify(cfire, p, timeout)
            ok = got == want
            bad += not ok
            print(f"{'ok ' if ok else 'BAD'} {name:<22} want={want:<6} got={got}")
    print(f"runner self-test: {len(SELF_TEST_CASES) - bad}/{len(SELF_TEST_CASES)} classifications correct")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
