#!/bin/sh
# cfire installer — cfire (pronounced sapphire) ISO C++23 systems toolchain
# Usage:
#   curl -fsSL https://cfire.memo.engineer/install.sh | sh
#   curl -fsSL https://cfire.memo.engineer/install.sh | sh -s -- --setup
#   curl -fsSL https://cfire.memo.engineer/install.sh | sh -s -- --from-source
set -e

RESET="\033[0m"
BOLD="\033[1m"
GREEN="\033[32m"
BLUE="\033[34m"
YELLOW="\033[33m"
RED="\033[31m"

RUN_SETUP="${CFIRE_SETUP:-0}"
FROM_SOURCE="${CFIRE_FROM_SOURCE:-0}"
ARG_PREFIX=""
ARG_VERSION=""

while [ $# -gt 0 ]; do
    case "$1" in
        --setup|--yes|-y) RUN_SETUP=1 ;;
        --from-source)    FROM_SOURCE=1 ;;
        --prefix)
            shift
            if [ $# -eq 0 ]; then
                printf "%b\n" "${RED}Error: --prefix requires a directory argument.${RESET}" >&2
                exit 1
            fi
            ARG_PREFIX="$1"
            ;;
        --prefix=*)
            ARG_PREFIX="${1#--prefix=}"
            ;;
        --version)
            shift
            if [ $# -eq 0 ]; then
                printf "%b\n" "${RED}Error: --version requires a version tag (e.g. v0.16.0).${RESET}" >&2
                exit 1
            fi
            ARG_VERSION="$1"
            ;;
        --version=*)
            ARG_VERSION="${1#--version=}"
            ;;
        -h|--help)
            cat << 'EOF'
cfire installer

Options:
  --from-source       Clone/build cfire from source instead of downloading a pre-compiled binary
  --prefix <dir>      Installation prefix (default: ~/.local, or /usr/local as root)
  --version <ver>     Release tag or version to install (e.g. v0.16.0)
  --setup, --yes, -y  Run 'cfire setup --yes' after installation

Environment overrides:
  CFIRE_INSTALL_PREFIX   Installation prefix (default: ~/.local)
  CFIRE_VERSION          Release tag (binary mode) or branch/SHA (--from-source)
  CFIRE_RELEASE_BASE_URL Custom release download base URL (for mirrors or offline archives)
  CFIRE_LOCAL_SRC        Local source checkout path (implies --from-source)
EOF
            exit 0
            ;;
        *)
            printf "%b\n" "${RED}Error: Unknown option '$1'. Pass --help for usage.${RESET}" >&2
            exit 1
            ;;
    esac
    shift
done

printf "%b\n" "${BOLD}Installing cfire (pronounced sapphire)...${RESET}"

# 1. Detect platform and architecture
OS="$(uname -s)"
RAW_ARCH="$(uname -m)"

case "$OS" in
    Darwin)
        PLATFORM="macOS"
        TARGET_OS="macos"
        ;;
    Linux)
        PLATFORM="Linux"
        TARGET_OS="linux"
        ;;
    *)
        printf "%b\n" "${RED}Error: Unsupported operating system: ${OS}.${RESET}" >&2
        printf "cfire binary releases currently support macOS, Linux, and Windows (use install.ps1).\n" >&2
        exit 1
        ;;
esac

case "$RAW_ARCH" in
    x86_64|amd64)   TARGET_ARCH="x86_64" ;;
    arm64|aarch64)  TARGET_ARCH="arm64" ;;
    *)
        printf "%b\n" "${RED}Error: Unsupported CPU architecture: ${RAW_ARCH}.${RESET}" >&2
        exit 1
        ;;
esac

if [ -n "$CFIRE_LOCAL_SRC" ] && [ -z "$CFIRE_RELEASE_BASE_URL" ]; then
    FROM_SOURCE=1
fi

if [ -n "$ARG_PREFIX" ]; then
    INSTALL_PREFIX="$ARG_PREFIX"
elif [ -n "$CFIRE_INSTALL_PREFIX" ]; then
    INSTALL_PREFIX="$CFIRE_INSTALL_PREFIX"
elif [ -n "$CFIRE_INSTALL_DIR" ]; then
    INSTALL_PREFIX="$(dirname "$CFIRE_INSTALL_DIR")"
elif [ "$(id -u)" -eq 0 ]; then
    INSTALL_PREFIX="/usr/local"
else
    INSTALL_PREFIX="${HOME}/.local"
fi

INSTALL_BIN="${CFIRE_INSTALL_DIR:-${INSTALL_PREFIX}/bin}"
INSTALL_SHARE="${CFIRE_DATA_DIR:-${INSTALL_PREFIX}/share/cfire}"

download_file() {
    SRC_URL="$1"
    DEST_PATH="$2"
    case "$SRC_URL" in
        file://*)
            LOCAL_FILE="${SRC_URL#file://}"
            if [ ! -f "$LOCAL_FILE" ]; then
                return 1
            fi
            cp "$LOCAL_FILE" "$DEST_PATH"
            return 0
            ;;
    esac
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL "$SRC_URL" -o "$DEST_PATH"
    elif command -v wget >/dev/null 2>&1; then
        wget -qO "$DEST_PATH" "$SRC_URL"
    else
        printf "%b\n" "${RED}Error: Neither 'curl' nor 'wget' was found.${RESET}" >&2
        return 1
    fi
}

compute_sha256() {
    TARGET_FILE="$1"
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$TARGET_FILE" | awk '{print $1}'
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$TARGET_FILE" | awk '{print $1}'
    elif command -v openssl >/dev/null 2>&1; then
        openssl dgst -sha256 "$TARGET_FILE" | awk '{print $NF}'
    else
        printf "%b\n" "${RED}Error: No SHA-256 verification tool found (sha256sum, shasum, or openssl).${RESET}" >&2
        return 1
    fi
}

TMP_DIR="$(mktemp -d 2>/dev/null || mktemp -d -t 'cfire_install')"
cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT INT TERM

if [ "$FROM_SOURCE" = "1" ]; then
    # Source-build path (requires C++23 compiler & make)
    if ! command -v make >/dev/null 2>&1; then
        printf "%b\n" "${RED}Error: 'make' utility is required for --from-source.${RESET}" >&2
        exit 1
    fi
    CXX=""
    if command -v clang++ >/dev/null 2>&1; then
        CXX="clang++"
    elif command -v g++ >/dev/null 2>&1; then
        CXX="g++"
    else
        printf "%b\n" "${RED}Error: Neither 'clang++' nor 'g++' was found for --from-source.${RESET}" >&2
        exit 1
    fi

    BRANCH="${ARG_VERSION:-${CFIRE_VERSION:-main}}"
    TARBALL_URL="https://github.com/gsandoval/cfire/archive/${BRANCH}.tar.gz"
    RESOLVED_SHA=""

    printf "  - Mode:         %bSource Build%b\n" "${BLUE}" "${RESET}"
    printf "  - Target:       %b%s (%s)%b\n" "${BLUE}" "$PLATFORM" "$TARGET_ARCH" "${RESET}"
    printf "  - Compiler:     %b%s%b\n" "${BLUE}" "$CXX" "${RESET}"
    printf "  - Binary Dest:  %b%s/cfire%b\n" "${BLUE}" "$INSTALL_BIN" "${RESET}"
    printf "  - Runtime Dest: %b%s/runtime%b\n" "${BLUE}" "$INSTALL_SHARE" "${RESET}"

    if [ -n "$CFIRE_LOCAL_SRC" ] && [ -d "$CFIRE_LOCAL_SRC" ]; then
        printf "\n%b==> Copying cfire source from %s...%b\n" "${BOLD}" "$CFIRE_LOCAL_SRC" "${RESET}"
        cp -R "$CFIRE_LOCAL_SRC/src" "$CFIRE_LOCAL_SRC/runtime" "$CFIRE_LOCAL_SRC/Makefile" "$TMP_DIR/"
        [ -d "$CFIRE_LOCAL_SRC/pkg" ] && cp -R "$CFIRE_LOCAL_SRC/pkg" "$TMP_DIR/"
        [ -f "$CFIRE_LOCAL_SRC/VERSION" ] && cp "$CFIRE_LOCAL_SRC/VERSION" "$TMP_DIR/"
        if command -v git >/dev/null 2>&1; then
            RESOLVED_SHA="$(git -C "$CFIRE_LOCAL_SRC" rev-parse --short=7 HEAD 2>/dev/null || true)"
            if [ -n "$RESOLVED_SHA" ] && ! git -C "$CFIRE_LOCAL_SRC" diff --quiet --ignore-submodules HEAD 2>/dev/null; then
                RESOLVED_SHA="${RESOLVED_SHA}-dirty"
            fi
        fi
    else
        printf "\n%b==> Downloading cfire source from GitHub (%s)...%b\n" "${BOLD}" "$BRANCH" "${RESET}"
        SRC_TGZ="$TMP_DIR/src.tar.gz"
        if ! download_file "$TARBALL_URL" "$SRC_TGZ"; then
            printf "%b\n" "${RED}Error: Failed to download repository archive from ${TARBALL_URL}.${RESET}" >&2
            exit 1
        fi
        tar -xzf "$SRC_TGZ" -C "$TMP_DIR" --strip-components=1
        if [ -f "$TMP_DIR/src/version.hpp" ]; then
            ARCHIVE_SHA="$(sed -n 's/.*kArchiveGitSha = "\([^"$]*\)".*/\1/p' "$TMP_DIR/src/version.hpp" | head -n 1)"
            if [ -n "$ARCHIVE_SHA" ] && [ "$ARCHIVE_SHA" != "dev" ]; then
                RESOLVED_SHA="$ARCHIVE_SHA"
            fi
        fi
    fi

    [ -z "$RESOLVED_SHA" ] && RESOLVED_SHA="$BRANCH"
    printf "%b==> Compiling cfire with %s...%b\n" "${BOLD}" "$CXX" "${RESET}"
    NJOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    if ! make -C "$TMP_DIR" build CXX="$CXX" GIT_SHA="$RESOLVED_SHA" -j"$NJOBS" >/dev/null; then
        printf "%b\n" "${RED}Compilation failed. Retrying with verbose output...${RESET}" >&2
        make -C "$TMP_DIR" build CXX="$CXX" GIT_SHA="$RESOLVED_SHA"
        exit 1
    fi

    mkdir -p "$INSTALL_BIN" "$INSTALL_SHARE/runtime"
    cp "$TMP_DIR/cfire" "$INSTALL_BIN/.cfire.tmp.$$"
    chmod 755 "$INSTALL_BIN/.cfire.tmp.$$"
    mv -f "$INSTALL_BIN/.cfire.tmp.$$" "$INSTALL_BIN/cfire"

    cp -R "$TMP_DIR/runtime/"* "$INSTALL_SHARE/runtime/"
    rm -f "$INSTALL_SHARE/runtime/VERSION"
    INSTALLED_VER="$(tr -d '[:space:]' < "$TMP_DIR/VERSION" 2>/dev/null || sed -n 's/.*kVersion = "\([^"]*\)".*/\1/p' "$TMP_DIR/src/version.hpp" | head -n 1)"
    [ -n "$INSTALLED_VER" ] && printf "%s %s\n" "$INSTALLED_VER" "$RESOLVED_SHA" > "$INSTALL_SHARE/runtime/cfire-runtime.version"
    if [ -d "$TMP_DIR/pkg" ]; then
        mkdir -p "$INSTALL_SHARE/pkg"
        cp -R "$TMP_DIR/pkg/"* "$INSTALL_SHARE/pkg/"
    fi
else
    # Default: pre-compiled portable release binary + SHA-256 verification (no make required)
    REQ_VER="${ARG_VERSION:-${CFIRE_VERSION:-}}"
    if [ -n "$REQ_VER" ]; then
        case "$REQ_VER" in
            v*) TAG="$REQ_VER" ;;
            *)  TAG="v${REQ_VER}" ;;
        esac
    elif [ -n "$CFIRE_RELEASE_BASE_URL" ]; then
        printf "%b\n" "${RED}Error: CFIRE_VERSION must be specified when using CFIRE_RELEASE_BASE_URL without GitHub latest redirect.${RESET}" >&2
        exit 1
    else
        if command -v curl >/dev/null 2>&1; then
            LATEST_URL="$(curl -fsSL -o /dev/null -w '%{url_effective}' https://github.com/gsandoval/cfire/releases/latest 2>/dev/null || true)"
            TAG="${LATEST_URL##*/}"
        fi
        if [ -z "$TAG" ] || [ "$TAG" = "latest" ]; then
            printf "%b\n" "${RED}Error: Could not resolve latest release tag from GitHub. Pass CFIRE_VERSION=vX.Y.Z or --from-source.${RESET}" >&2
            exit 1
        fi
    fi

    if [ -n "$CFIRE_RELEASE_BASE_URL" ]; then
        BASE_URL="${CFIRE_RELEASE_BASE_URL%/}"
    else
        BASE_URL="https://github.com/gsandoval/cfire/releases/download/${TAG}"
    fi

    BUNDLE_NAME="cfire-${TAG}-${TARGET_OS}-${TARGET_ARCH}"
    ARCHIVE_NAME="${BUNDLE_NAME}.tar.gz"
    ARCHIVE_URL="${BASE_URL}/${ARCHIVE_NAME}"
    SUMS_URL="${BASE_URL}/SHA256SUMS.txt"

    printf "  - Mode:         %bPre-compiled Release Binary (%s)%b\n" "${BLUE}" "$TAG" "${RESET}"
    printf "  - Target:       %b%s (%s-%s)%b\n" "${BLUE}" "$PLATFORM" "$TARGET_OS" "$TARGET_ARCH" "${RESET}"
    printf "  - Binary Dest:  %b%s/cfire%b\n" "${BLUE}" "$INSTALL_BIN" "${RESET}"
    printf "  - Runtime Dest: %b%s/runtime%b\n" "${BLUE}" "$INSTALL_SHARE" "${RESET}"

    ARCHIVE_PATH="${TMP_DIR}/${ARCHIVE_NAME}"
    SUMS_PATH="${TMP_DIR}/SHA256SUMS.txt"

    printf "\n%b==> Downloading %s and SHA256SUMS.txt...%b\n" "${BOLD}" "$ARCHIVE_NAME" "${RESET}"
    if ! download_file "$ARCHIVE_URL" "$ARCHIVE_PATH"; then
        printf "%b\n" "${RED}Error: Failed to download release archive from ${ARCHIVE_URL}.${RESET}" >&2
        exit 1
    fi
    if ! download_file "$SUMS_URL" "$SUMS_PATH"; then
        printf "%b\n" "${RED}Error: Failed to download SHA256SUMS.txt from ${SUMS_URL}.${RESET}" >&2
        exit 1
    fi

    EXPECTED_SHA="$(awk -v name="$ARCHIVE_NAME" '{
        f = $2;
        sub(/^\*/, "", f);
        sub(/^\.\//, "", f);
        if (f == name || $2 == name) { print tolower($1); exit }
    }' "$SUMS_PATH")"
    if [ -z "$EXPECTED_SHA" ]; then
        printf "%b\n" "${RED}Error: Checksum entry for '${ARCHIVE_NAME}' not found in SHA256SUMS.txt.${RESET}" >&2
        exit 1
    fi

    ACTUAL_SHA="$(compute_sha256 "$ARCHIVE_PATH" | tr '[:upper:]' '[:lower:]')"
    if [ "$ACTUAL_SHA" != "$EXPECTED_SHA" ]; then
        printf "%b\n" "${RED}Error: SHA-256 verification failed for ${ARCHIVE_NAME}!${RESET}" >&2
        printf "  Expected: %s\n" "$EXPECTED_SHA" >&2
        printf "  Actual:   %s\n" "$ACTUAL_SHA" >&2
        exit 1
    fi
    printf "  ✓ SHA-256 verified (%s)\n" "$ACTUAL_SHA"

    UNPACK_DIR="${TMP_DIR}/unpack"
    mkdir -p "$UNPACK_DIR"
    tar -xzf "$ARCHIVE_PATH" -C "$UNPACK_DIR"

    if [ -x "${UNPACK_DIR}/${BUNDLE_NAME}/cfire" ]; then
        BUNDLE_ROOT="${UNPACK_DIR}/${BUNDLE_NAME}"
    elif [ -x "${UNPACK_DIR}/cfire" ]; then
        BUNDLE_ROOT="${UNPACK_DIR}"
    else
        FOUND_BIN="$(find "$UNPACK_DIR" -maxdepth 3 -name cfire -type f | head -n 1)"
        if [ -z "$FOUND_BIN" ]; then
            printf "%b\n" "${RED}Error: Downloaded archive did not contain a 'cfire' executable.${RESET}" >&2
            exit 1
        fi
        BUNDLE_ROOT="$(dirname "$FOUND_BIN")"
    fi

    mkdir -p "$INSTALL_BIN" "$INSTALL_SHARE/runtime"
    cp "${BUNDLE_ROOT}/cfire" "$INSTALL_BIN/.cfire.tmp.$$"
    chmod 755 "$INSTALL_BIN/.cfire.tmp.$$"
    mv -f "$INSTALL_BIN/.cfire.tmp.$$" "$INSTALL_BIN/cfire"

    if [ -d "${BUNDLE_ROOT}/runtime" ]; then
        cp -R "${BUNDLE_ROOT}/runtime/"* "$INSTALL_SHARE/runtime/"
        rm -f "$INSTALL_SHARE/runtime/VERSION"
    fi
    if [ -d "${BUNDLE_ROOT}/pkg" ]; then
        mkdir -p "$INSTALL_SHARE/pkg"
        cp -R "${BUNDLE_ROOT}/pkg/"* "$INSTALL_SHARE/pkg/"
    fi
fi

# 3. Verify installation & check compiler availability
if "$INSTALL_BIN/cfire" --version >/dev/null 2>&1; then
    VER_STR="$("$INSTALL_BIN/cfire" --version)"
    printf "\n%b✓ %s installed successfully!%b\n\n" "${GREEN}${BOLD}" "$VER_STR" "${RESET}"
else
    printf "\n%b✓ cfire installed to %s/cfire.%b\n\n" "${GREEN}" "$INSTALL_BIN" "${RESET}"
fi

if ! command -v clang++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1; then
    printf "%bNote: No C++23 compiler (clang++) found on your PATH yet.%b\n" "${YELLOW}${BOLD}" "${RESET}"
    printf "cfire uses Clang as its C++23 backend when compiling '.cf' programs.\n"
    if [ "$OS" = "Darwin" ]; then
        printf "  Install Apple Clang via: xcode-select --install\n\n"
    else
        printf "  Install Clang via:       sudo apt-get install -y clang   # or dnf/pacman/apk\n\n"
    fi
fi

if [ "$RUN_SETUP" = "1" ]; then
    "$INSTALL_BIN/cfire" setup --yes || true
else
    "$INSTALL_BIN/cfire" doctor || true
fi

# 4. Check PATH
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
