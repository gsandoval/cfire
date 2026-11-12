# Language Audit Playbook

A repeatable, evidence-driven audit of the `cfire` language and toolchain. It's written so that an agent (or a human) with **no prior context** can run it from start to finish and produce a report plus prioritized backlog proposals.

To start a clean session, paste:

> Read `docs/planning/audit/PLAYBOOK.md` and execute it end to end. Do not commit anything. Ask me before deciding any design question.

- **Reports**: [`reports/`](./reports/). The latest report is the baseline for the next run.
- **Tools**: [`scripts/audit/`](../../../scripts/audit/): `run_probes.py`, `mutfuzz.py`, `lsp_smoke.py`, `probes/*.cf`.
- **Backlog**: [`../backlog.md`](../backlog.md). Audit-driven work lives in a lettered section (`## A.`, `## B.`, …) with its own ADRs, waves, and item specs.

---

## 0. Ground Rules

1. **Read-only on product code.** Don't edit `src/`, `runtime/`, `examples/`, `tests/`, or `pkg/`. The only files you write are:
   - the new report `docs/planning/audit/reports/YYYY-MM-DD.md`
   - new or updated probes in `scripts/audit/probes/`
   - (after the user approves) backlog/ADR edits in `docs/planning/backlog.md` and `active.md`
2. **Never commit, push, or tag.** The user commits.
3. **Use a scratch directory** for every probe program, build artifact, and fuzz output. Never write probes next to repository sources: `cfire run x.cf` also compiles sibling `.cf` files in the same directory.
4. **Evidence or it didn't happen.** Every finding needs a minimal repro (a probe file or command), the observed output, and a `file:line` reference to the cause where you can find it. Mark anything unreproduced as *suspected* and keep it out of the ranked list.
5. **Don't trust `[DONE]`.** For each recently closed backlog item, reproduce its acceptance criterion. A claim that doesn't hold is a **regression finding** against that item ID.
6. **Don't decide design questions.** When a fix has more than one reasonable semantics (naming, panic vs saturate, method vs function, …), stop and ask the user. Offer 2–4 options with your recommendation first, then record the answer as an ADR (see §7).
7. **Keep the user posted** with a one-line status at each phase boundary.

---

## 1. Orientation (≈15 min)

Read these in order:
1. `AGENTS.md` (architecture map).
2. `docs/planning/README.md` (quality gates).
3. `docs/planning/active.md` and the index/ADR sections of `docs/planning/backlog.md`.
4. The **latest report** in `docs/planning/audit/reports/`. Note its commit SHA, finding IDs, and which items the backlog marks as fixed since then.
5. `docs/GUIDE.md` headings and `docs/STDLIB_REFERENCE.md` module list (to learn the syntax; examples in `examples/*/main.cf`).

Then record:
- `git log --oneline <last-report-sha>..HEAD` and `git status --short`.
- Size map: `find src runtime tests -name '*.hpp' -o -name '*.cpp' | xargs wc -l | sort -n | tail -25`.
- **Bookkeeping drift**: compare the checkboxes in `active.md` against the `[DONE]` statuses in `backlog.md`.

## 2. Baseline Gates (run in the background)

```bash
make build 2>&1 | tee $SCRATCH/build.log | grep -c "warning:"
time make review-check > $SCRATCH/review.log 2>&1; echo EXIT=$?
```

Record the pass/fail, wall time, warning count, and GUIDE doc-test count. A red baseline is itself a P0 finding.

## 3. Probe Corpus (regression check of previous findings)

```bash
python3 -I scripts/audit/run_probes.py --json $SCRATCH/probes.json | tee $SCRATCH/probes.txt
python3 -I scripts/audit/lsp_smoke.py scripts/audit/probes/*.cf
```

Interpret each status:

| Status | Meaning | Action |
| :--- | :--- | :--- |
| `CRASH` | The toolchain aborted or signalled | P0 finding. Minimize it and backtrace it (§5). |
| `REGRESSION` | A `guard` probe no longer holds | P0/P1 finding. `git log -S` to find the culprit. |
| `OPEN` | A known hole is still present | Carry it forward, citing the existing backlog ID. |
| `FIXED` | A hole now behaves ideally | Verify by hand, flip its header to `kind: guard`, and note the fix in the report. |
| `ERROR` | The probe couldn't run (syntax drift, timeout) | Update the probe to the current syntax or API and rerun. |

When an ADR changes the API (for example a method rename), update the affected probes in the same audit, so `ideal:` always describes the *decided* behavior.

## 4. Exploratory Audit by Dimension

Write new probes in `$SCRATCH/probes/<topic>/main.cf` (one directory per probe). Use `cfire check`, `cfire run`, `cfire run --sanitize`, and the TSan recipe below. Every confirmed finding becomes a new probe file in `scripts/audit/probes/`, with the header described in `run_probes.py`.

### 4.1 Recently Addressed Work
- For each backlog item closed since the last report, reproduce its **Acceptance Criteria** with a probe.
- Grep each item's claims (for example "mkdtemp everywhere", "exact ranges", "AST pretty-printer") against the code. Partial delivery is a finding.

### 4.2 Soundness (memory, ownership, UB)
Try to make `cfire check` accept a program that then fails under `--sanitize` or prints garbage:
- [ ] Loans through function returns, temporaries, struct fields, map element refs, closures that capture views, and loop-carried views.
- [ ] Any method that mutates a container without being modelled by the loan tracker (raw C++ members, new stdlib methods).
- [ ] Moves: conditional moves, moves in loops, moves inside closures, moves through `match` arms.
- [ ] Null and optional: deref without a guard, `T?` arithmetic, `none` comparisons.
- [ ] Arithmetic: overflow (release vs `cfire test`), shifts `>=` width, division by a runtime zero, `MIN / -1`, float→int out of range, narrowing casts, literals out of range.
- [ ] Indexing and slicing: out-of-range, inverted ranges, negative runtime bounds, UTF-8 mid-codepoint string slices.
- [ ] Generics: interface constraints actually enforced, and error messages that are cfire's own (not clang's).

### 4.3 Concurrency
- [ ] Contention correctness (`Atomic`, `Mutex[T]`, `Channel`) at 8 workers × 10k ops. Results must be exact.
- [ ] Rejected patterns stay rejected: outer mutation in `spawn` (`ESCAPING-CAPTURE-MUT`).
- [ ] Racy patterns: captured `&mut` / `*mut` pointers, views, `shared[T]` without a lock, maps written from two tasks.
- [ ] Lifecycle: `defer` ordering in `async.scope`, nested spawn, a panic inside a task, channel close/drain/timeout, context cancel cascade.
- TSan recipe (until `cfire run --sanitize=thread` exists):
  ```bash
  ./cfire emit-cpp $P/main.cf > $P/main.cpp
  clang++ -std=c++2b -O1 -g -fsanitize=thread -Iruntime $P/main.cpp -o $P/tsan && $P/tsan
  ```

### 4.4 Uniformity
Build a **method matrix** for every builtin type: `str`, each integer width, `f32`/`f64`, `bool`, `[]T`, `[N]T`, `map[K]V`, `T?`, and `T!`. For each common operation (length, emptiness, search, transform, convert, parse, limits, compare, iterate), record whether it's a method, a module function, an operator, or missing.

The quick way is a loop of one-line `cfire check` probes; see the 2026-11-11 report §F/§Numbers for the shape. Flag each of these:
- [ ] The same operation spelled differently across types (`.len()` vs `.size()` vs `.len`).
- [ ] Method vs free-function inconsistency within one type.
- [ ] Absence conventions mixed (`T?` vs sentinel `-1` vs a panic).
- [ ] Raw C++ API leaking through (any `std::string`/`std::vector` member that compiles).
- [ ] Constructor naming (`make_*` vs `new_*` vs `open`/`connect`/`dial`), the separator rules (ADR `Q3`/`Q13`), and field naming.
- [ ] Docs and examples using forms the ADRs reject.

### 4.5 Orthogonality
- [ ] Two ways to do one thing: duplicate APIs, flag + separate function, synonyms. Check the "Explicitly Excluded" list in `backlog.md` §3 so you don't re-propose rejected items.
- [ ] Feature interactions: generics × interfaces, closures × spawn × loans, `try`/`or` × `defer`, `match` × generic enums, struct tags × generics.
- [ ] Documented patterns that a rule rejects in exactly the context where they'd be used (for example `with_lock` inside `spawn`).

### 4.6 Standard Library Completeness
Compare each core module against a baseline of what systems languages ship. For `core:math` that means constants, rounding, pow/exp/log, trig, min/max/abs/clamp, NaN/inf predicates, integer bit ops, checked/wrapping/saturating per width, and gcd/lcm. Do the same for `str`, slices, maps, `time`, `fs`, and `os`. Report gaps as one finding per module, not per function.

### 4.7 Diagnostics Quality
- [ ] User errors reported by clang (`cfire/TYPE-ERROR` with C++ text) rather than by a cfire code.
- [ ] Hard-coded columns: `grep -c '{}:{}:1:' src/sema/*.hpp`.
- [ ] Duplicated code suffixes in CLI output, and missing or incorrect fix-its.
- [ ] `UNKNOWN-TYPE` warnings in `examples/`: `for d in examples/*/; do ./cfire check $d; done`.
- [ ] The diagnostic code inventory (`grep -rhoE 'cfire/[A-Z][A-Z0-9-]+' src | sort -u`) vs the docs' rule tables.

### 4.8 Tooling
- [ ] **LSP**: `lsp_smoke.py` over all probes and fuzz crash seeds; hover, rename, and diagnostic ranges on one shadowing-heavy file.
- [ ] **Formatter**: format a deliberately ugly file (mixed spacing, one-liners, comments, strings containing `//`). Check idempotence, that it still compiles, and that the generated C++ is unchanged modulo `#line`.
- [ ] **Rename**: shadowing in blocks, closure params, match bindings, and struct fields vs locals. Check that the `--diff` output applies with `git apply`.
- [ ] **Test runner**: a failing assert, a panic, and a sanitizer trap inside `test` blocks must each name the test and its location.
- [ ] **CLI**: `cfire <sub> --help` for every subcommand; behavior when given a single file whose directory has siblings.
- [ ] **Docs**: `grep -rc "file:///" docs/*.md docs/*.html`; code blocks that aren't doc-tested (`grep -c '^```cfire' docs/GUIDE.md` vs the doc-test count); `doc --check` blind spots.
- [ ] **Build ergonomics**: phony targets, rebuild counts in `review-check`, gate text vs Makefile flags.

### 4.9 Security
Run these and review every hit:
```bash
grep -rnE '\b(std::)?system\(|popen\(' src runtime          # shell construction & quoting
grep -rn 'temp_directory_path()' src runtime tests          # predictable/shared temp paths
grep -rn 'x-cfire-' runtime                                 # in-band control headers
grep -rnE 'git clone|curl |checkout' src                    # option injection, --proto, verification
grep -rnE 'accept\(|SO_RCVTIMEO|set_timeout' runtime/core/net  # server timeouts & concurrency
grep -rn 'catch' src/lsp src/mcp.hpp                         # exception barriers on long-lived servers
```
- [ ] HTTP server: idle-client DoS (open a socket and send nothing, then issue a normal request), `Content-Length` parsing, duplicate headers, CL+TE, bare LF.
- [ ] TLS: every way verification can be disabled, and whether any of them is reachable from data.
- [ ] Supply chain: cache-hit verification against `build.lock`, fallback cache location, integrity when no hash is given.
- [ ] Lints: try to bypass `SHELL-INJECTION-RISK` / `SQL-INJECTION-RISK` through locals, helper functions, and format strings.
- [ ] MCP: path confinement, and what `cfire_eval` / `cfire_test` can reach.
- [ ] Defaults: privileged default users, insecure default flags, non-atomic secret updates.

### 4.10 Regression Testing
- [ ] Are fuzz targets built and run anywhere? `grep -rln LLVMFuzzerTestOneInput Makefile .github scripts`
- [ ] Sanitizer coverage per suite and per OS in `.github/workflows/*.yml` and `make test-asan`.
- [ ] Windows/Linux CI parity with macOS.
- [ ] Hygiene: fixed temp paths in `tests/`, wall-clock asserts, test filtering, per-test isolation.
- [ ] Conformance corpus coverage (`tests/conformance/`, once `CF-161` lands). Is every ADR and diagnostic code covered by at least one file?
- [ ] Hooks: does any git hook stage files the user didn't stage?

## 5. Fuzzing & Crash Triage

```bash
python3 -I scripts/audit/mutfuzz.py --out $SCRATCH/fuzz --iterations 300 --jobs 4
```
- Group crashes by the printed message. For each group, take the smallest input, minimize it by hand to under 10 lines, and get a backtrace:
  ```bash
  lldb --batch -o "breakpoint set -n __cxa_throw" -o "run check main.cf" -o "bt 10" -- ./cfire
  ```
  (On Linux: `gdb -batch -ex "catch throw" -ex run -ex bt --args ./cfire check main.cf`.)
- Check whether the same input kills `cfire lsp` (`lsp_smoke.py`). If it does, the crash is a separate, higher-impact finding.
- If libFuzzer is available (Homebrew LLVM or Linux clang), also run the targets in `tests/fuzz/` for about 5 minutes each.

## 6. Severity Rubric

| Priority | Criteria |
| :---: | :--- |
| **P0** | Toolchain crash or hang on any input. Safe-looking code that `check` accepts and that then hits UB, a UAF, a data race, or silently wrong results. A red baseline. A regression of a `[DONE]` item. |
| **P1** | Exploitable security weakness (DoS, TLS downgrade, supply-chain poisoning, injection-lint bypass). Language rules that differ by context (uniformity/orthogonality breaks in core types). User type errors reported by clang. Missing core stdlib basics. Missing CI coverage for an existing safety claim. |
| **P2** | Tooling correctness (rename, formatter, diagnostics ranges), doc drift, stdlib naming inconsistency, test hygiene. |
| **P3** | Cosmetics and UX papercuts. |

Size: **S** ≤ 1 day, **M** 2–5 days, **L** > 1 week.

## 7. Outputs

### 7.1 Report: `docs/planning/audit/reports/YYYY-MM-DD.md`
```markdown
# cfire State-of-the-Language Audit — YYYY-MM-DD
Scope: `main` @ `<sha>` (vX.Y.Z). Read-only; nothing committed. Previous report: <link>.
**Baseline:** <review-check result, time, warnings, doc-tests>
## 1. Since last audit
<closed items verified ✓ / regressed ✗; probe status table summary; bookkeeping drift>
## 2. Findings, ranked
### P0 …  ### P1 …  ### P2 …   (tables: # | Finding | Repro / evidence; IDs continue the letter
scheme: A=soundness, B=uniformity/orthogonality, C=tooling, D=security, E=testing,
F=stdlib, G=concurrency; number from 1 per report)
## 3. Open design questions
<numbered, each with options + recommendation>
```

### 7.2 Design Questions → ADRs
Ask the user the questions from §3 of the report, at most 4 at a time, with your recommended option first. Record each answer as a numbered ADR (`Q<n>`, continuing the backlog's numbering) in the audit section of `backlog.md`. If an ADR amends an older one, annotate the older one inline ("*(amended by `Qn`)*").

### 7.3 Backlog Proposals
**Only after the user approves.** Add or extend a lettered backlog section, following the format of `backlog.md` §A:
1. ADR list.
2. Mermaid dependency graph.
3. **Prioritized index** (Rank | ID | Phase | Priority | Size | Component | Summary | Audit Ref | Depends On | Status).
4. **Waves table** (lanes + exit gates).
5. Per-item specs: Problem & Evidence / Decision / Files Touched / Implementation / Tests / Acceptance Criteria.

Ordering rules (ADR `Q17`):
1. Crash-proofing and the regression net.
2. Soundness.
3. Security.
4. Type-system prerequisites.
5. Breaking API work.
6. Polish.

Two rules apply throughout:
- **No item may depend on a later-ranked item**, so check the graph against the ranks before finishing.
- New P0s found mid-wave join the *current* wave as blockers.

IDs continue from the highest `CF-` number in `backlog.md`. Fold duplicates into existing items instead of re-filing them, and cite the new evidence.

### 7.4 Closing Checklist
- [ ] Report written; every finding has a repro and a probe file (or a reason it can't be probed).
- [ ] `scripts/audit/probes/` updated: new holes added, `FIXED` ones flipped to `guard`.
- [ ] Design questions asked and recorded as ADRs.
- [ ] Backlog updated, ranks and dependencies consistent, waves updated.
- [ ] `./cfire doc --check` and `./cfire fmt --check examples/` still pass (docs-only edits shouldn't break them).
- [ ] `git status` shows only report, probe, and planning-doc changes. Nothing is committed.

---

## Appendix A — Syntax & Environment Gotchas (as of v0.11.0)
- Slice literals are `xs: []i32 = [1, 2, 3]`. `[]i32{1, 2, 3}` mis-parses into confusing `NO-EFFECT-STMT` errors.
- Structured concurrency is `async.scope(workers: N) |scope| { scope.spawn || { … } }`, not `sync.scope`.
- There are no block comments (`/* */`).
- Single-file commands compile sibling `.cf` files, so always use one directory per probe.
- `make build` is phony, so every make target relinks `cfire` (~30s).
- Apple clang ships without libFuzzer; use Homebrew LLVM or the `mutfuzz.py` fallback.
- `cfire test` names a test file `*_test.cf`. A sanitizer trap inside a test now reports `--- FAIL: <name> (… test crashed: …)` (`CF-157`).
- **Keep scratch `.cf` files out of `/tmp` while running `make test`.** The LSP treats `/tmp` as a workspace (a stray `/tmp/.cfire/` cache is a root marker) and loads every `.cf` beneath it, which breaks the LSP suites (`CF-178`). If the session's scratch directory is under `/tmp`, put probe programs in `$TMPDIR`-based `mktemp -d` dirs or under `~/.cache/`, and remove them before running the gates.
- New gates: `make test-conformance`, `make test-tsan`, and `make fuzz-replay`.

## Appendix B — Time Budget (single agent)
| Phase | Time |
| :--- | :--- |
| Orientation + baseline (gates run in the background) | 30 min |
| Probe corpus + LSP smoke | 15 min |
| Exploratory dimensions | 2–4 h |
| Fuzzing + triage | 30 min |
| Report + questions + backlog | 1 h |
