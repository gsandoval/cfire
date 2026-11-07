#!/bin/sh
# cfire installer — cfire (pronounced sapphire) ISO C++23 systems toolchain
# Usage:
#   curl -fsSL https://cfire.memo.engineer/install.sh | sh
#   curl -fsSL https://cfire.memo.engineer/install.sh | sh -s -- --setup
set -e

RESET="\033[0m"
BOLD="\033[1m"
GREEN="\033[32m"
BLUE="\033[34m"
YELLOW="\033[33m"
RED="\033[31m"

RUN_SETUP="${CFIRE_SETUP:-0}"
for arg in "$@"; do
    case "$arg" in
        --setup|--yes|-y) RUN_SETUP=1 ;;
    esac
done

printf "%b\n" "${BOLD}Installing cfire (pronounced sapphire)...${RESET}"

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

# Auto-bootstrap base compiler/tools on bare Linux containers if root or --setup requested
bootstrap_base_linux() {
    SUDO=""
    if [ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    fi
    if command -v apt-get >/dev/null 2>&1; then
        printf "%b==> Bootstrapping base C++23 toolchain via apt-get...%b\n" "${BOLD}" "${RESET}"
        $SUDO apt-get update -qq
        $SUDO env DEBIAN_FRONTEND=noninteractive apt-get install -y -qq clang make curl tar ca-certificates pkg-config >/dev/null
    elif command -v dnf >/dev/null 2>&1; then
        printf "%b==> Bootstrapping base C++23 toolchain via dnf...%b\n" "${BOLD}" "${RESET}"
        $SUDO dnf install -y clang make curl tar ca-certificates pkgconf-pkg-config >/dev/null
    elif command -v pacman >/dev/null 2>&1; then
        printf "%b==> Bootstrapping base C++23 toolchain via pacman...%b\n" "${BOLD}" "${RESET}"
        $SUDO pacman -Sy --needed --noconfirm clang make curl tar ca-certificates pkgconf >/dev/null
    elif command -v apk >/dev/null 2>&1; then
        printf "%b==> Bootstrapping base C++23 toolchain via apk...%b\n" "${BOLD}" "${RESET}"
        $SUDO apk add --no-cache clang make curl tar ca-certificates pkgconf >/dev/null
    fi
}

if [ "$OS" = "Linux" ]; then
    if ! command -v clang++ >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1 || ! command -v curl >/dev/null 2>&1; then
        if [ "$(id -u)" -eq 0 ] || [ "$RUN_SETUP" = "1" ]; then
            bootstrap_base_linux
        fi
    fi
fi

# Check for download tool (unless installing from CFIRE_LOCAL_SRC)
DOWNLOAD_CMD=""
if [ -z "$CFIRE_LOCAL_SRC" ]; then
    if command -v curl >/dev/null 2>&1; then
        DOWNLOAD_CMD="curl -fsSL"
    elif command -v wget >/dev/null 2>&1; then
        DOWNLOAD_CMD="wget -qO-"
    else
        printf "%b\n" "${RED}Error: Neither 'curl' nor 'wget' was found.${RESET}" >&2
        printf "Please install curl or wget and run this script again.\n" >&2
        exit 1
    fi
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
TARBALL_URL="https://github.com/gsandoval/cfire/archive/${BRANCH}.tar.gz"
RESOLVED_SHA=""

printf "  - Target:       %b%s (%s)%b\n" "${BLUE}" "$PLATFORM" "$ARCH" "${RESET}"
printf "  - Compiler:     %b%s%b\n" "${BLUE}" "$CXX" "${RESET}"
printf "  - Binary Dest:  %b%s/cfire%b\n" "${BLUE}" "$INSTALL_BIN" "${RESET}"
printf "  - Runtime Dest: %b%s/runtime%b\n" "${BLUE}" "$INSTALL_DATA" "${RESET}"

# 3. Prepare source workspace
TMP_DIR="$(mktemp -d 2>/dev/null || mktemp -d -t 'cfire_install')"
cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT INT TERM

if [ -n "$CFIRE_LOCAL_SRC" ] && [ -d "$CFIRE_LOCAL_SRC" ]; then
    printf "\n%b==> Copying cfire source from %s...%b\n" "${BOLD}" "$CFIRE_LOCAL_SRC" "${RESET}"
    cp -R "$CFIRE_LOCAL_SRC/src" "$CFIRE_LOCAL_SRC/runtime" "$CFIRE_LOCAL_SRC/Makefile" "$TMP_DIR/"
    if command -v git >/dev/null 2>&1; then
        RESOLVED_SHA="$(git -C "$CFIRE_LOCAL_SRC" rev-parse --short HEAD 2>/dev/null || true)"
    fi
else
    printf "\n%b==> Downloading cfire source from GitHub (%s)...%b\n" "${BOLD}" "$BRANCH" "${RESET}"
    if ! $DOWNLOAD_CMD "$TARBALL_URL" | tar -xz -C "$TMP_DIR" --strip-components=1; then
        printf "%b\n" "${RED}Error: Failed to download or unpack repository archive from ${TARBALL_URL}.${RESET}" >&2
        exit 1
    fi
    if command -v git >/dev/null 2>&1; then
        RESOLVED_SHA="$(git ls-remote https://github.com/gsandoval/cfire.git "$BRANCH" 2>/dev/null | awk 'NR==1 {print substr($1, 1, 7)}' || true)"
    fi
fi

if [ -z "$RESOLVED_SHA" ]; then
    case "$BRANCH" in
        [0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]*)
            RESOLVED_SHA="$(printf "%.7s" "$BRANCH")"
            ;;
        *)
            RESOLVED_SHA="$BRANCH"
            ;;
    esac
fi

printf "%b==> Compiling cfire with %s...%b\n" "${BOLD}" "$CXX" "${RESET}"
if ! make -C "$TMP_DIR" build CXX="$CXX" GIT_SHA="$RESOLVED_SHA" -j4 >/dev/null; then
    printf "%b\n" "${RED}Compilation failed. Retrying with verbose output...${RESET}" >&2
    make -C "$TMP_DIR" build CXX="$CXX" GIT_SHA="$RESOLVED_SHA"
    exit 1
fi

# 4. Install binary and runtime headers (atomic temp-file + mv to prevent macOS SIGKILL code-signing cache invalidation)
mkdir -p "$INSTALL_BIN"
mkdir -p "$INSTALL_DATA/runtime"

cp "$TMP_DIR/cfire" "$INSTALL_BIN/.cfire.tmp.$$"
chmod 755 "$INSTALL_BIN/.cfire.tmp.$$"
mv -f "$INSTALL_BIN/.cfire.tmp.$$" "$INSTALL_BIN/cfire"

cp -R "$TMP_DIR/runtime/"* "$INSTALL_DATA/runtime/"

# 5. Verify installation & run doctor/setup
if "$INSTALL_BIN/cfire" --version >/dev/null 2>&1; then
    VER_STR="$("$INSTALL_BIN/cfire" --version)"
    printf "\n%b✓ %s installed successfully!%b\n\n" "${GREEN}${BOLD}" "$VER_STR" "${RESET}"
else
    printf "\n%b✓ cfire installed to %s/cfire.%b\n\n" "${GREEN}" "$INSTALL_BIN" "${RESET}"
fi

if [ "$RUN_SETUP" = "1" ]; then
    "$INSTALL_BIN/cfire" setup --yes || true
else
    "$INSTALL_BIN/cfire" doctor || true
fi

# 6. Check PATH
case ":$PATH:" in
    *":$INSTALL_BIN:"*) ;;
    *)
        printf "\n%bNotice: %s is not currently in your \$PATH.%b\n" "${YELLOW}${BOLD}" "$INSTALL_BIN" "${RESET}"
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

printf "\nTry running your first cfire program:\n"
printf "  %b%s/cfire eval 'println(\"Hello from cfire!\")'%b\n\n" "${BOLD}" "$INSTALL_BIN" "${RESET}"
printf "For documentation and guides, visit: %bhttps://cfire.memo.engineer%b\n" "${BLUE}" "${RESET}"
