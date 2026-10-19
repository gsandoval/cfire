#!/bin/sh
# cfire installer — Sapphire on Fire (ISO C++23 frontend)
# Usage: curl -fsSL https://cfire.memo.engineer/install.sh | sh
set -e

RESET="\033[0m"
BOLD="\033[1m"
GREEN="\033[32m"
BLUE="\033[34m"
YELLOW="\033[33m"
RED="\033[31m"

printf "%b\n" "${BOLD}Installing cfire (Sapphire on Fire)...${RESET}"

# 1. Detect platform and tools
OS="$(uname -s)"
ARCH="$(uname -m)"

case "$OS" in
    Darwin) PLATFORM="macOS" ;;
    Linux)  PLATFORM="Linux" ;;
    *)
        printf "%b\n" "${RED}Error: Unsupported operating system: ${OS}.${RESET}" >&2
        printf "cfire currently supports macOS and Linux.\n" >&2
        exit 1
        ;;
esac

# Check for download tool
if command -v curl >/dev/null 2>&1; then
    DOWNLOAD_CMD="curl -fsSL"
elif command -v wget >/dev/null 2>&1; then
    DOWNLOAD_CMD="wget -qO-"
else
    printf "%b\n" "${RED}Error: Neither 'curl' nor 'wget' was found.${RESET}" >&2
    printf "Please install curl or wget and run this script again.\n" >&2
    exit 1
fi

# Check for compiler
CXX=""
if command -v clang++ >/dev/null 2>&1; then
    CXX="clang++"
elif command -v g++ >/dev/null 2>&1; then
    CXX="g++"
else
    printf "%b\n" "${YELLOW}Warning: Neither 'clang++' nor 'g++' was found on your PATH.${RESET}" >&2
    if [ "$OS" = "Darwin" ]; then
        printf "Please install Xcode Command Line Tools by running:\n" >&2
        printf "  xcode-select --install\n\n" >&2
    else
        printf "Please install Clang or GCC by running:\n" >&2
        printf "  sudo apt-get install clang make   # Debian / Ubuntu\n" >&2
        printf "  sudo dnf install clang make       # Fedora / RHEL\n" >&2
        printf "  sudo pacman -S clang make         # Arch Linux\n\n" >&2
    fi
    exit 1
fi

if ! command -v make >/dev/null 2>&1; then
    printf "%b\n" "${RED}Error: 'make' utility is required to build cfire.${RESET}" >&2
    exit 1
fi

# 2. Determine target directories
if [ "$(id -u)" -eq 0 ]; then
    DEFAULT_BIN="/usr/local/bin"
    DEFAULT_DATA="/usr/local/share/cfire"
else
    DEFAULT_BIN="${HOME}/.local/bin"
    DEFAULT_DATA="${HOME}/.cfire"
fi

INSTALL_BIN="${CFIRE_INSTALL_DIR:-$DEFAULT_BIN}"
INSTALL_DATA="${CFIRE_DATA_DIR:-$DEFAULT_DATA}"
BRANCH="${CFIRE_VERSION:-main}"
TARBALL_URL="https://github.com/gsandoval/cfire/archive/refs/heads/${BRANCH}.tar.gz"

printf "  - Target:       %b%s (%s)%b\n" "${BLUE}" "$PLATFORM" "$ARCH" "${RESET}"
printf "  - Compiler:     %b%s%b\n" "${BLUE}" "$CXX" "${RESET}"
printf "  - Binary Dest:  %b%s/cfire%b\n" "${BLUE}" "$INSTALL_BIN" "${RESET}"
printf "  - Runtime Dest: %b%s/runtime%b\n" "${BLUE}" "$INSTALL_DATA" "${RESET}"

# 3. Create temp workspace
TMP_DIR="$(mktemp -d 2>/dev/null || mktemp -d -t 'cfire_install')"
cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT INT TERM

printf "\n%b==> Downloading cfire source from GitHub (%s)...%b\n" "${BOLD}" "$BRANCH" "${RESET}"
if ! $DOWNLOAD_CMD "$TARBALL_URL" | tar -xz -C "$TMP_DIR" --strip-components=1; then
    printf "%b\n" "${RED}Error: Failed to download or unpack repository archive from ${TARBALL_URL}.${RESET}" >&2
    exit 1
fi

printf "%b==> Compiling cfire with %s...%b\n" "${BOLD}" "$CXX" "${RESET}"
if ! make -C "$TMP_DIR" CXX="$CXX" -j4 >/dev/null; then
    printf "%b\n" "${RED}Compilation failed. Retrying with verbose output...${RESET}" >&2
    make -C "$TMP_DIR" CXX="$CXX"
    exit 1
fi

# 4. Install binary and runtime headers
mkdir -p "$INSTALL_BIN"
mkdir -p "$INSTALL_DATA/runtime"

cp "$TMP_DIR/cfire" "$INSTALL_BIN/cfire"
chmod 755 "$INSTALL_BIN/cfire"

cp -R "$TMP_DIR/runtime/"* "$INSTALL_DATA/runtime/"

# 5. Verify installation
if "$INSTALL_BIN/cfire" --version >/dev/null 2>&1 || "$INSTALL_BIN/cfire" --help >/dev/null 2>&1; then
    printf "\n%b✓ cfire was built and installed successfully!%b\n\n" "${GREEN}${BOLD}" "${RESET}"
else
    printf "\n%b✓ cfire installed to %s/cfire.%b\n\n" "${GREEN}" "$INSTALL_BIN" "${RESET}"
fi

# 6. Check PATH
case ":$PATH:" in
    *":$INSTALL_BIN:"*) ;;
    *)
        printf "%bNotice: %s is not currently in your \$PATH.%b\n" "${YELLOW}${BOLD}" "$INSTALL_BIN" "${RESET}"
        printf "To use 'cfire' directly from your shell, add it to your profile:\n\n"
        if [ "$OS" = "Darwin" ] || [ -n "$ZSH_VERSION" ]; then
            printf "  %becho 'export PATH=\"%s:\$PATH\"' >> ~/.zshrc%b\n" "${BOLD}" "$INSTALL_BIN" "${RESET}"
            printf "  %bsource ~/.zshrc%b\n\n" "${BOLD}" "${RESET}"
        else
            printf "  %becho 'export PATH=\"%s:\$PATH\"' >> ~/.bashrc%b\n" "${BOLD}" "$INSTALL_BIN" "${RESET}"
            printf "  %bsource ~/.bashrc%b\n\n" "${BOLD}" "${RESET}"
        fi
        ;;
esac

printf "Try running your first cfire program:\n"
printf "  %b%s/cfire run examples/01-hello-core/main.cf%b\n\n" "${BOLD}" "$INSTALL_BIN" "${RESET}"
printf "For documentation and guides, visit: %bhttps://cfire.memo.engineer%b\n" "${BLUE}" "${RESET}"
