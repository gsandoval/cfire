# scripts/vm/config.sh — settings for the UTM VM test targets (CF-184, Q48). Override any of them in the
# environment, e.g. `CFIRE_VM_LINUX=ubuntu make test-vm-linux`.
UTMCTL="${CFIRE_UTMCTL:-/Applications/UTM.app/Contents/MacOS/utmctl}"
VM_LINUX="${CFIRE_VM_LINUX:-Linux}"
VM_WINDOWS="${CFIRE_VM_WINDOWS:-Windows}"
# The host as seen from the guests (UTM's shared/host-only network) and the port the file server listens on.
VM_HOST_IP="${CFIRE_VM_HOST_IP:-192.168.64.1}"
VM_PORT="${CFIRE_VM_PORT:-8765}"
# A host folder the Linux guest mounts as the 9p share tag below; the Windows guest fetches over HTTP instead.
VM_SHARE="${CFIRE_VM_SHARE:-$HOME/Programming/vm-shared}"
VM_SHARE_TAG="${CFIRE_VM_SHARE_TAG:-share}"
