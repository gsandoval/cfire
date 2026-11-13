# Local VM gates (`make test-vm-linux`, `make test-vm-windows`)

These run the CI `linux` and `windows` jobs from `.github/workflows/ci.yml` on your working tree, uncommitted
changes included, inside local [UTM](https://mac.getutm.app) virtual machines. Run them before a release, or after
any change to platform-specific code (`#if defined(_WIN32)`, `__linux__`, the build system, the runtime).

## One-time setup
1. Two UTM VMs, named `Linux` (Ubuntu) and `Windows` (Windows 10 or 11), each with UTM's guest tools so the QEMU
   guest agent runs. Other names: set `CFIRE_VM_LINUX` / `CFIRE_VM_WINDOWS` (all settings are in `config.sh`).
2. For Linux, a UTM shared directory (VirtFS) pointing at `CFIRE_VM_SHARE` (default `~/Programming/vm-shared`),
   with mount tag `share`. Windows fetches the source over HTTP instead, so it needs no share.
3. Install the toolchains in the guests (this downloads packages inside the VMs):
   ```bash
   scripts/vm/test_vm.sh setup-linux     # apt: clang, make and the CI dev libraries
   scripts/vm/test_vm.sh setup-windows   # MSYS2 CLANG64 from repo.msys2.org, plus the CI packages
   ```

## Running
```bash
make test-vm-linux
make test-vm-windows
```
Each target copies the working tree into the share, starts `serve.py` on the host-only address
(`CFIRE_VM_HOST_IP`, default `192.168.64.1`), starts the VM if needed, runs the gate there, prints its log, and
exits with the gate's exit code. The Windows VM on Apple silicon is emulated x64, so expect it to be slow.

## How it works
- `utmctl exec` only launches the job. Its own output capture can return before a guest command finishes, so
  every job uploads its log and exit code to `serve.py` (`PUT /results/<job>`) instead.
- On Windows the job runs as a scheduled task, because a child of the guest-agent call is killed when that call
  returns.
- `serve.py` listens only on the host-only address, serves the share read-only, and accepts uploads only under
  `results/`.
- Only one run per OS at a time: a run holds `<share>/.running_<os>` until it exits. If a killed run left the
  lock behind, the next run says so; delete the folder. Each Windows job gets its own task (`cfire_vm_<job>`),
  which deletes itself when the job ends.
