# Local VM gates (`make check-platforms`)

`make check-platforms` (= `make test-vm-linux` + `make test-vm-windows`) runs the CI `linux` and `windows` jobs from
`.github/workflows/ci.yml` on your working tree, uncommitted changes included, inside local
[UTM](https://mac.getutm.app) virtual machines. It is one of the three test tiers (`docs/planning/README.md`): run it
before a CF item is marked done and before a release.

The host side is a C++ program, `tools/vmtest/` (built by make as `.vmtest`); it needs only clang, `git` and `tar`.
The files here run inside the guests.

## One-time setup
1. Two UTM VMs, named `Linux` (Ubuntu) and `Windows` (Windows 10 or 11), each with UTM's guest tools so the QEMU
   guest agent runs.
2. For Linux, a UTM shared directory (VirtFS) pointing at the share folder (default `~/Programming/vm-shared`), with
   mount tag `share`. Windows fetches the source over HTTP instead, so it needs no share.
3. Install the toolchains in the guests (this downloads packages inside the VMs):
   ```bash
   make vm-setup-linux     # apt: clang, make and the CI dev libraries
   make vm-setup-windows   # MSYS2 CLANG64 from repo.msys2.org, plus the CI packages
   ```

## Settings
Every setting is an environment variable with a default, so the same targets work with other VM names or networks:

| Variable | Default | Meaning |
| :--- | :--- | :--- |
| `CFIRE_VM_LINUX` / `CFIRE_VM_WINDOWS` | `Linux` / `Windows` | UTM VM names |
| `CFIRE_VM_HOST_IP` | `192.168.64.1` | the host as the guests see it (UTM's shared network) |
| `CFIRE_VM_PORT` | `8765` | the driver's file server port |
| `CFIRE_VM_SHARE` | `~/Programming/vm-shared` | host folder the Linux guest mounts |
| `CFIRE_VM_SHARE_TAG` | `share` | its VirtFS mount tag |
| `CFIRE_VM_TIMEOUT` | `10800` | seconds to wait for a whole gate run |
| `CFIRE_UTMCTL` | `/Applications/UTM.app/Contents/MacOS/utmctl` | the UTM command-line tool |

Example: `CFIRE_VM_LINUX=ubuntu make test-vm-linux`.

## How it works
- The driver copies the working tree (`git ls-files -co --exclude-standard`) into the share, plus `cfire.tar`, and
  serves the share on the host-only address. It serves files read-only and accepts uploads only as
  `PUT /results/<name>`.
- It starts the VM if needed, waits for its guest agent, and launches the job with `utmctl exec`. That call can return
  before a guest command finishes, so every job uploads its log and exit code to the file server instead, and the
  driver waits for them, printing the log and exiting with the guest's exit code.
- In the Linux guest the job runs `linux_gate.sh` (`make check`). In the Windows guest it runs `windows_gate.sh` under
  MSYS2 CLANG64 as a scheduled task, because a child of the guest-agent call is killed when that call returns. Each
  job gets its own task (`cfire_vm_<job>`), which deletes itself and reports whether it is gone.
- Only one run per OS at a time: a run holds `<share>/.running_<os>` (with its PID) until it exits, on every exit
  path. A lock whose owner process is gone is taken over automatically.
