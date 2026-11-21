# scripts/vm/linux_gate.sh — runs inside the Linux guest as root (launched by tools/vmtest). Mirrors the CI `linux` job:
# `make check` and incremental `make check-deep`. Needs clang, make, rsync and the dev libraries from .github/workflows/ci.yml.
set -u
export HOME=/root USER=root
SHARE_TAG="$1"; HOST="$2"; PORT="$3"
mkdir -p /mnt/vm-shared /root/cfire-vm
mountpoint -q /mnt/vm-shared || mount -t 9p -o trans=virtio,version=9p2000.L,msize=262144 "$SHARE_TAG" /mnt/vm-shared
rsync -a --delete --no-owner --no-group \
  --filter='P /.cfire_gitsha' \
  --filter='P /.cfire_build.log' \
  --filter='P /.cfire_host.pch*' \
  --filter='P /.cfire_test.pch*' \
  --filter='P /.cfire_test_asan.pch*' \
  --filter='P /.cfire_tsan.d' \
  --filter='P /cfire' \
  --filter='P /.cfire_test_runner*' \
  --filter='P /.fuzz_replay_*' \
  --filter='P *.o' \
  --filter='P *.o.log' \
  --filter='P *.d' \
  --filter='P .cfire/' \
  /mnt/vm-shared/cfire/ /root/cfire-vm/
cd /root/cfire-vm
find . -maxdepth 3 \( -name '*.o' -o -name '*.pch' \) -size 0 -delete 2>/dev/null || true
step() { echo "== $1"; shift; "$@"; local rc=$?; echo "rc=$rc"; [ $rc -eq 0 ] || FAILED=1; }
FAILED=0
step check bash -c 'make check > check.log 2>&1; r=$?; grep -E "C\+\+ compiler warning|GUIDE Doc-Test Results|^Conformance:|\[FAIL\]|^FAIL|^cfire check failed|make check passed|^PASS \(all" check.log | head -60; [ $r -eq 0 ] || tail -30 check.log; exit $r'
step check-deep bash -c 'make check-deep > check-deep.log 2>&1; r=$?; grep -E "^=== |^PASS|^FAIL|\[FAIL\]|ERROR:|runtime error:|fuzz-replay|make check-deep passed" check-deep.log | tail -60; [ $r -eq 0 ] || tail -40 check-deep.log; exit $r'
exit $FAILED
