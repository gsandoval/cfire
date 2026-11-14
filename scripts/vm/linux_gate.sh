# scripts/vm/linux_gate.sh — runs inside the Linux guest as root (launched by tools/vmtest). Mirrors the CI `linux` job:
# `make check`, the every-commit tier (CF-186). Needs clang, make, rsync and the dev libraries from .github/workflows/ci.yml.
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
step check bash -c 'make check > check.log 2>&1; r=$?; grep -E "C\+\+ compiler warning|GUIDE Doc-Test Results|^Conformance:|\[FAIL\]|^FAIL|^cfire check failed|make check passed" check.log | head -60; exit $r'
exit $FAILED
