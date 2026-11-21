#!/usr/bin/env bash
# scripts/vm/windows_gate.sh — runs inside the Windows guest under MSYS2 CLANG64 (fetched by the job that
# tools/vmtest starts). Mirrors the CI `windows` job in .github/workflows/ci.yml incrementally.
set -u
HOST="$1"; PORT="$2"
mkdir -p /c/cfire-work /c/cfire-work/cfire
rm -rf /c/cfire-work/src /c/cfire-work/stage
mkdir -p /c/cfire-work/stage
curl -s -f -o /c/cfire-work/cfire.tar "http://$HOST:$PORT/cfire.tar" && tar -xf /c/cfire-work/cfire.tar -C /c/cfire-work/stage || exit 1

if [ -f /c/cfire-work/cfire/.vmtest_manifest ]; then
  comm -23 <(tr -d '\r' < /c/cfire-work/cfire/.vmtest_manifest | sort) <(tr -d '\r' < /c/cfire-work/stage/cfire/.vmtest_manifest | sort) | while IFS= read -r stale; do
    [ -n "$stale" ] && rm -f "/c/cfire-work/cfire/$stale"
  done
fi

(cd /c/cfire-work/stage/cfire && grep -v '^$' .vmtest_manifest | tr '\n' '\0' | xargs -0 sha256sum) > /c/cfire-work/stage/new.sums || exit 1
(cd /c/cfire-work/cfire && grep -v '^$' /c/cfire-work/stage/cfire/.vmtest_manifest | tr '\n' '\0' | xargs -0 sha256sum 2>/dev/null) > /c/cfire-work/stage/old.sums || true

awk -v oldf=/c/cfire-work/stage/old.sums '
  BEGIN { while ((getline line < oldf) > 0) { sub(/\r$/, "", line); old[substr(line,67)] = substr(line,1,64) } close(oldf) }
  { sub(/\r$/, ""); h=substr($0,1,64); p=substr($0,67); if (!(p in old) || old[p] != h) print p }
' /c/cfire-work/stage/new.sums > /c/cfire-work/stage/changed.list

copied=0
while IFS= read -r rel; do
  [ -n "$rel" ] || continue
  src="/c/cfire-work/stage/cfire/$rel"
  dst="/c/cfire-work/cfire/$rel"
  mkdir -p "$(dirname "$dst")" && cp -pf "$src" "$dst" || exit 1
  copied=$((copied + 1))
done < /c/cfire-work/stage/changed.list
total=$(wc -l < /c/cfire-work/stage/new.sums)
echo "== incremental sync: copied $copied / $total files"

cp -pf /c/cfire-work/stage/cfire/.vmtest_manifest /c/cfire-work/cfire/.vmtest_manifest
rm -rf /c/cfire-work/stage /c/cfire-work/cfire.tar
cd /c/cfire-work/cfire || exit 1

JOBS="${NUMBER_OF_PROCESSORS:-4}"
export JOBS
step() { echo "== $1"; shift; "$@"; local rc=$?; echo "rc=$rc"; [ $rc -eq 0 ] || FAILED=1; }
FAILED=0

step build bash -c '
  [ -f .cfire_build.log ] || rm -f cfire.exe
  [ -f .cfire_host.pch.log ] || rm -f .cfire_host.pch
  [ -f .cfire_test.pch.log ] || rm -f .cfire_test.pch
  [ -f .cfire_test_runner.log ] || rm -f .cfire_test_runner.exe
  for o in src/*.o src/*/*.o tests/*.o tests/*/*.o; do [ -e "$o" ] || continue; [ -f "${o}.log" ] || rm -f "$o"; done
  make -j"$JOBS" build .cfire_test_runner.exe GIT_SHA=vm > build.log 2>&1
  r=$?
  make check-warnings >> build.log 2>&1
  wr=$?
  grep -E "zero C\+\+ compiler warning|warning:|error:" build.log | head -20
  [ $r -eq 0 ] && [ $wr -eq 0 ] || tail -30 build.log
  [ $r -eq 0 ] && [ $wr -eq 0 ]
'
step single-tu-build bash -c 'clang++ -std=c++2b -Wall -Wextra -Wno-parentheses-equality -fsyntax-only -Iruntime -DCFIRE_GIT_SHA=\"vm\" src/main.cpp 2> single_build.err; r=$?; w=$(grep -c "warning:" single_build.err); echo "C++ warnings: $w"; grep -m10 -E "error|warning:" single_build.err; [ $r -eq 0 ] && [ "$w" -eq 0 ]'
step doctor bash -c './cfire.exe --version && ./cfire.exe doctor'
step fmt ./cfire.exe fmt --check examples/
step doc ./cfire.exe doc --check
step stdlib-suite ./cfire.exe test tests/stdlib_suite_test.cf
step unit bash -c './.cfire_test_runner.exe unit --jobs "$JOBS" > unit.log 2>&1; r=$?; if [ $r -ne 0 ]; then grep -aE -A 8 "\[FAIL\]|^FAIL" unit.log | head -120; else grep -aE "PASS \(all|^\[PASS\]" unit.log | tail -40; fi; exit $r'
step conformance bash -c './.cfire_test_runner.exe conformance --cfire ./cfire.exe --jobs "$JOBS" > conf.log 2>&1; r=$?; if [ $r -ne 0 ]; then grep -E -A 8 "^FAIL" conf.log | head -120; fi; grep -E "^Conformance:|^note:" conf.log; exit $r'
exit $FAILED
