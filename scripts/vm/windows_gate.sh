# scripts/vm/windows_gate.sh — runs inside the Windows guest under MSYS2 CLANG64 (fetched by the job that
# test_vm.sh starts). Mirrors the CI `windows` job in .github/workflows/ci.yml.
set -u
HOST="$1"; PORT="$2"
mkdir -p /c/cfire-work && cd /c/cfire-work && rm -rf src && mkdir src && cd src
curl -s -o cfire.tar "http://$HOST:$PORT/cfire.tar" && tar -xf cfire.tar && cd cfire || exit 1
FLAGS='-std=c++2b -O2 -Wall -Wextra -Wno-parentheses-equality -Iruntime -Isrc'
LIBS='-lws2_32 -ladvapi32 -lshell32'
step() { echo "== $1"; shift; "$@"; local rc=$?; echo "rc=$rc"; [ $rc -eq 0 ] || FAILED=1; }
FAILED=0
step build bash -c "clang++ $FLAGS -DCFIRE_GIT_SHA=\\\"vm\\\" src/main.cpp -o cfire.exe $LIBS 2> build.err; r=\$?; echo \"C++ warnings: \$(grep -c 'warning:' build.err)\"; grep -m10 -E 'error|warning:' build.err; exit \$r"
step doctor ./cfire.exe doctor
step fmt ./cfire.exe fmt --check examples/
step doc ./cfire.exe doc --check
step stdlib-suite ./cfire.exe test tests/stdlib_suite_test.cf
step unit-build bash -c "clang++ $FLAGS -DCFIRE_GIT_SHA=\\\"dev\\\" tests/unit/test_frontend.cpp tests/unit/test_sema.cpp tests/unit/test_stdlib.cpp tests/e2e/test_canonical.cpp tests/test_main.cpp -o cfire_test_runner.exe $LIBS -lgdi32 2> unit_build.err; r=\$?; echo \"C++ warnings: \$(grep -c 'warning:' unit_build.err)\"; grep -m20 -E 'error|warning:' unit_build.err; exit \$r"
step unit bash -c "./cfire_test_runner.exe unit > unit.log 2>&1; r=\$?; grep -aE '\[FAIL\]|^FAIL|PASS \(all' unit.log | head -60; exit \$r"
step conformance bash -c "python3 -I scripts/run_conformance.py --cfire ./cfire.exe > conf.log 2>&1; r=\$?; grep -E '^FAIL|^ +- |^Conformance:|^note:' conf.log | head -60; exit \$r"
exit $FAILED
