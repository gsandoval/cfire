@echo off
rem scripts/vm/windows_setup.cmd — one-time setup of the Windows guest: MSYS2 (CLANG64) with the packages the CI
rem `windows` job installs. Run as Administrator inside the guest, or let test_vm.sh run it with `setup-windows`.
cd /d C:\
if not exist C:\msys64\usr\bin\bash.exe (
  curl -sSL -o "%TEMP%\msys2.sfx.exe" https://repo.msys2.org/distrib/msys2-x86_64-latest.sfx.exe
  "%TEMP%\msys2.sfx.exe" -y -oC:\
)
set MSYSTEM=CLANG64
set CHERE_INVOKING=1
C:\msys64\usr\bin\bash.exe -lc "pacman -Syuu --noconfirm"
C:\msys64\usr\bin\bash.exe -lc "pacman -Syuu --noconfirm"
C:\msys64\usr\bin\bash.exe -lc "pacman -S --noconfirm --needed mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-make mingw-w64-clang-x86_64-pkgconf mingw-w64-clang-x86_64-simdjson mingw-w64-clang-x86_64-zstd mingw-w64-clang-x86_64-libsodium mingw-w64-clang-x86_64-sqlite3 mingw-w64-clang-x86_64-python make tar"
C:\msys64\usr\bin\bash.exe -lc "clang++ --version"
