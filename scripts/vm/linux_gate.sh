# scripts/vm/linux_gate.sh — runs inside the Linux guest as root (sent by test_vm.sh). Mirrors the CI `linux` job:
# build, make test, review-check. Needs clang, make, rsync and the dev libraries from .github/workflows/ci.yml.
set -u
export HOME=/root USER=root
SHARE_TAG="$1"; HOST="$2"; PORT="$3"
mkdir -p /mnt/vm-shared
mountpoint -q /mnt/vm-shared || mount -t 9p -o trans=virtio,version=9p2000.L,msize=262144 "$SHARE_TAG" /mnt/vm-shared
rm -rf /root/cfire-vm && mkdir -p /root/cfire-vm
rsync -a --no-owner --no-group /mnt/vm-shared/cfire/ /root/cfire-vm/
cd /root/cfire-vm
step() { echo "== $1"; shift; "$@"; local rc=$?; echo "rc=$rc"; [ $rc -eq 0 ] || FAILED=1; }
FAILED=0
step build bash -c 'make build > build.log 2>&1; r=$?; echo "C++ warnings: $(grep -c "warning:" build.log)"; grep -m10 -E "error|warning:" build.log; exit $r'
step test bash -c 'make test > test.log 2>&1; r=$?; grep -E "Doc-Test Results|^Conformance:|\[FAIL\]|^FAIL" test.log | head -40; exit $r'
step review-check bash -c 'make review-check > review.log 2>&1; r=$?; grep -E "\[FAIL\]|^FAIL" review.log | head -20; exit $r'
exit $FAILED
