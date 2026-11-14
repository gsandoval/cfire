# Language Conformance Corpus (`CF-161`)

Each `.cf` file in this tree pins down **one** language or stdlib expectation. The files are grouped by feature directory: `syntax/`, `types/`, `ownership/`, `errors/`, `match/`, `generics/`, `closures/`, `arith/`, `imports/`, `concurrency/`, `stdlib/{math,str,slices,maps}/`, and `security/`.

Run the corpus:

```bash
make test-conformance                                        # also part of `make check`
./.cfire_test_runner conformance --filter 'ownership/*' -v   # after `make .cfire_test_runner`
```

## Directives

Directives are leading `//` comment lines placed before `package`. Prefer **inline annotations** for anything tied to a line. They stay correct when header directives are added or removed (for example when an `xfail` line is dropped):

```cf
    xs.push_back(1) //~ error cfire/UNKNOWN-METHOD
    println("{}", xs[i]) //~ panic "index out of bounds"
    out := os.exec(cmd) or "" //~ warning cfire/SHELL-INJECTION-RISK
```

The header forms with `@L` (absolute line numbers, directive lines included) are still accepted.

| Directive | Meaning |
| :--- | :--- |
| `// expect: stdout` followed by `// \| <line>` … | The program's stdout must match exactly (for `*_test.cf`, the lines must appear in order). |
| `//~ error cfire/CODE` (inline) or `// expect: error cfire/CODE @L[:C]` | Repeatable. Together these are the **exact** set of errors `cfire check` reports. With any error expected, the program isn't run. |
| `//~ warning cfire/CODE` (inline) or `// expect: warning cfire/CODE @L` | Repeatable. These warnings must be present; other warnings are ignored. |
| `//~ panic "<substring>"` (inline) or `// expect: panic "<substring>" [@L]` | The program must exit non-zero, print `panic`, contain the substring, and attribute the panic to `.cf` line `L`. |
| `// expect: exit N` | The program's exit status must be N. |
| `// expect: xfail CF-xxx <reason>` | The expectations above describe the **decided ideal**, which isn't met yet. The cited backlog item must fix it. |
| `// flags: --sanitize` / `--sanitize=thread` | Extra run flags. `--sanitize=thread` falls back to emit-cpp plus clang TSan until `CF-164` lands. Any sanitizer report fails the file. |

Files named `*_test.cf` run with `cfire test`; all others run with `cfire run`.

## Outcomes

| Outcome | Meaning | Effect |
| :--- | :--- | :--- |
| `PASS` | Expectations met | ok |
| `FAIL` | Expectations not met (or malformed directives) | **fails the build** |
| `XFAIL` | Tracked hole, still open | ok |
| `XPASS` | Tracked hole now behaves ideally | **fails the build**. Remove the `xfail` line in the same change that fixed it. |

## Policy

- Every change to language semantics, diagnostics, or stdlib behavior adds or updates at least one file here.
- Every backlog item that fixes a hole flips its `xfail` files to `PASS` in the same change.
- When an ADR changes an API, update the affected files in the same change, so `expect:` always describes the decided behavior.
- One expectation per file. Name files `NNN_what_it_checks.cf`.
- Never write a file whose expectation is "whatever the compiler does today" unless that behavior is decided. If it's undecided, open a backlog item and ask.

The runner is `tests/conformance_runner.hpp` (part of `.cfire_test_runner`); its own classification tests are in `tests/runner_self_tests.hpp`, run by the `unit` suite.
