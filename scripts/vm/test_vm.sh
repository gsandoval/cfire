#!/usr/bin/env bash
# scripts/vm/test_vm.sh — runs the CI-equivalent gate inside a local UTM VM (CF-184, Q48).
#   test_vm.sh linux | windows             run the gate on the current working tree (uncommitted changes included)
#   test_vm.sh setup-linux | setup-windows install the toolchain in the guest once (apt packages / MSYS2 CLANG64)
# The guests need the QEMU guest agent (UTM installs it with its guest tools). Settings are in config.sh.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$DIR/../.." && pwd)"
# shellcheck source=config.sh
source "$DIR/config.sh"
MODE="${1:-}"
case "$MODE" in
    linux|setup-linux) VM="$VM_LINUX" ;;
    windows|setup-windows) VM="$VM_WINDOWS" ;;
    *) echo "usage: $0 linux|windows|setup-linux|setup-windows" >&2; exit 2 ;;
esac
TIMEOUT_S="${CFIRE_VM_TIMEOUT:-10800}"
JOB="${MODE}_$(date +%Y%m%d_%H%M%S)"
BASE="http://$VM_HOST_IP:$VM_PORT"

# One run per mode at a time: a second run would overwrite the share copy and the guest job under it.
mkdir -p "$VM_SHARE"
LOCK="$VM_SHARE/.running_$MODE"
if ! mkdir "$LOCK" 2>/dev/null; then
    echo "error: another '$MODE' run is in progress (delete $LOCK if it is not)" >&2
    exit 1
fi
SERVER_PID=""
trap 'rmdir "$LOCK" 2>/dev/null; [ -n "$SERVER_PID" ] && kill "$SERVER_PID" 2>/dev/null' EXIT

# 1. A fresh copy of the working tree in the share (a stale copy keeps files the repo deleted).
mkdir -p "$VM_SHARE/results" "$VM_SHARE/jobs"
rm -rf "$VM_SHARE/cfire" && mkdir -p "$VM_SHARE/cfire"
(cd "$REPO" && git ls-files -co --exclude-standard -z | rsync -a --from0 --files-from=- ./ "$VM_SHARE/cfire/")
find "$VM_SHARE/cfire" -name '._*' -delete
(cd "$VM_SHARE" && COPYFILE_DISABLE=1 tar --no-mac-metadata --no-xattrs -cf cfire.tar cfire 2>/dev/null || tar -cf cfire.tar cfire)

# 2. The file server (only if one isn't already running).
if ! curl -s -o /dev/null -m 2 "$BASE/cfire.tar"; then
    python3 "$DIR/serve.py" "$VM_SHARE" "$VM_HOST_IP" "$VM_PORT" &
    SERVER_PID=$!
    for _ in $(seq 1 20); do curl -s -o /dev/null -m 1 "$BASE/cfire.tar" && break; sleep 0.5; done
fi

# 3. The VM, started if needed, with its guest agent answering.
if ! "$UTMCTL" status "$VM" 2>/dev/null | grep -q started; then
    echo "==> starting VM '$VM'"
    "$UTMCTL" start "$VM"
fi
for _ in $(seq 1 90); do
    "$UTMCTL" ip-address "$VM" 2>/dev/null | grep -qE '^[0-9]+\.' && break
    sleep 4
done

# The guest agent occasionally fails a single call ("The system cannot find the file specified"); retry it.
guest_exec() {
    local attempt
    for attempt in 1 2 3; do
        "$UTMCTL" exec "$VM" "$@" > /dev/null && return 0
        sleep 5
    done
    echo "error: utmctl exec failed three times on '$VM'" >&2
    return 1
}

# 4. Launch the job detached in the guest; it uploads <job>.log and <job>.rc to results/ when it finishes.
rm -f "$VM_SHARE/results/$JOB.log" "$VM_SHARE/results/$JOB.rc"
if [ "$VM" = "$VM_LINUX" ]; then
    if [ "$MODE" = "setup-linux" ]; then
        body='export DEBIAN_FRONTEND=noninteractive; apt-get update -q && apt-get install -y -q clang libclang-rt-dev make git pkg-config rsync curl python3 cmake libzstd-dev libsimdjson-dev libsqlite3-dev libsodium-dev libssl-dev libasound2-dev libgtk-3-dev libwebkit2gtk-4.1-dev'
    else
        body="bash /mnt/vm-shared/cfire/scripts/vm/linux_gate.sh $VM_SHARE_TAG $VM_HOST_IP $VM_PORT"
    fi
    wrapper="mkdir -p /mnt/vm-shared; mountpoint -q /mnt/vm-shared || mount -t 9p -o trans=virtio,version=9p2000.L,msize=262144 $VM_SHARE_TAG /mnt/vm-shared
( $body ) > /var/tmp/$JOB.log 2>&1; echo \$? > /var/tmp/$JOB.rc
curl -s -T /var/tmp/$JOB.log $BASE/results/$JOB.log; curl -s -T /var/tmp/$JOB.rc $BASE/results/$JOB.rc"
    b64="$(printf '%s\n' "$wrapper" | base64 | tr -d '\n')"
    guest_exec --cmd /bin/sh -c "echo $b64 | base64 -d > /var/tmp/$JOB.sh; nohup /bin/bash /var/tmp/$JOB.sh > /dev/null 2>&1 &"
else
    if [ "$MODE" = "setup-windows" ]; then
        run='curl -s -o C:\cfire-work\setup.cmd '"$BASE"'/cfire/scripts/vm/windows_setup.cmd
call C:\cfire-work\setup.cmd > C:\cfire-work\'"$JOB"'.log 2>&1
echo %ERRORLEVEL% > C:\cfire-work\'"$JOB"'.rc'
    else
        run='set MSYSTEM=CLANG64
set CHERE_INVOKING=1
C:\msys64\usr\bin\bash.exe -lc "curl -s -o /c/cfire-work/gate.sh '"$BASE"'/cfire/scripts/vm/windows_gate.sh; bash /c/cfire-work/gate.sh '"$VM_HOST_IP $VM_PORT"' > /c/cfire-work/'"$JOB"'.log 2>&1; echo $? > /c/cfire-work/'"$JOB"'.rc"'
    fi
    printf '@echo off\n%s\ncurl -s -T C:\\cfire-work\\%s.log %s/results/%s.log\ncurl -s -T C:\\cfire-work\\%s.rc %s/results/%s.rc\nschtasks /delete /tn cfire_vm_%s /f\n' \
        "$run" "$JOB" "$BASE" "$JOB" "$JOB" "$BASE" "$JOB" "$JOB" | perl -pe 's/\n/\r\n/' > "$VM_SHARE/jobs/$JOB.cmd"
    # A scheduled task outlives the guest-agent call (a plain `start /b` child does not). Each job gets its own
    # task name, since `schtasks /run` silently does nothing while a task of the same name is still running.
    guest_exec --cmd 'C:\Windows\System32\cmd.exe' /c "mkdir C:\\cfire-work 2>nul & curl -s -o C:\\cfire-work\\$JOB.cmd $BASE/jobs/$JOB.cmd & schtasks /create /tn cfire_vm_$JOB /tr C:\\cfire-work\\$JOB.cmd /sc once /st 23:59 /ru SYSTEM /f & schtasks /run /tn cfire_vm_$JOB"
fi
echo "==> $MODE running in '$VM' (job $JOB); waiting for results"

# 5. Wait for the guest's exit code, then show its log.
for _ in $(seq 1 $((TIMEOUT_S / 10))); do
    [ -s "$VM_SHARE/results/$JOB.rc" ] && break
    sleep 10
done
if [ ! -s "$VM_SHARE/results/$JOB.rc" ]; then
    echo "error: no result from '$VM' after ${TIMEOUT_S}s (job $JOB)" >&2
    exit 1
fi
tr -d '\r' < "$VM_SHARE/results/$JOB.log" | grep -av 'LIBARCHIVE.xattr'
rc="$(tr -dc '0-9' < "$VM_SHARE/results/$JOB.rc")"
echo "==> $MODE finished in '$VM' with exit code ${rc:-?}"
exit "${rc:-1}"
