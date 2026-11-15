#!/bin/sh
# scripts/release.sh — Atomic version bump, verification, local portable packaging & git tagging for cfire
# Usage:
#   ./scripts/release.sh <x.y.z> [--dry-run] [--no-tag]
#   ./scripts/release.sh --package-local <dest_dir>
#   make release VERSION=0.10.0
set -e

NEW_VER=""
DRY_RUN=0
NO_TAG=0
PACKAGE_LOCAL_DIR=""

while [ $# -gt 0 ]; do
    arg="$1"
    shift
    case "$arg" in
        --dry-run) DRY_RUN=1 ;;
        --no-tag)  NO_TAG=1 ;;
        --package-local)
            if [ $# -lt 1 ]; then
                echo "error: --package-local requires a destination directory" >&2
                exit 1
            fi
            PACKAGE_LOCAL_DIR="$1"
            shift
            ;;
        --package-local=*)
            PACKAGE_LOCAL_DIR="${arg#--package-local=}"
            ;;
        -h|--help)
            echo "Usage: ./scripts/release.sh <x.y.z> [--dry-run] [--no-tag]"
            echo "       ./scripts/release.sh --package-local <dest_dir>"
            exit 0
            ;;
        *)
            if [ -z "$NEW_VER" ]; then
                NEW_VER="$arg"
            else
                echo "error: unexpected argument '$arg'" >&2
                exit 1
            fi
            ;;
    esac
done

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

if [ -n "$PACKAGE_LOCAL_DIR" ]; then
    HOST_OS="$(uname -s)"
    HOST_ARCH="$(uname -m)"
    case "$HOST_OS" in
        Darwin) TARGET_OS="macos" ;;
        Linux)  TARGET_OS="linux" ;;
        *)      TARGET_OS="$(printf '%s' "$HOST_OS" | tr '[:upper:]' '[:lower:]')" ;;
    esac
    case "$HOST_ARCH" in
        arm64|aarch64) TARGET_ARCH="arm64" ;;
        x86_64|amd64)  TARGET_ARCH="x86_64" ;;
        *)             TARGET_ARCH="$HOST_ARCH" ;;
    esac
    CUR_VER="$(tr -d '[:space:]' < VERSION 2>/dev/null || echo '0.16.0')"
    CUR_VER="${CUR_VER#v}"
    BUNDLE_NAME="cfire-v${CUR_VER}-${TARGET_OS}-${TARGET_ARCH}"

    echo "==> Packaging portable cfire bundle (${BUNDLE_NAME}) into ${PACKAGE_LOCAL_DIR}..."
    if [ "$HOST_OS" = "Darwin" ]; then
        make -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" build \
            EXTRA_CXXFLAGS="-mmacosx-version-min=13.3" \
            EXTRA_LDFLAGS="-mmacosx-version-min=13.3"
    else
        make -j"$(nproc 2>/dev/null || echo 4)" build
    fi

    mkdir -p "$PACKAGE_LOCAL_DIR"
    rm -rf "${PACKAGE_LOCAL_DIR}/${BUNDLE_NAME}"
    ./cfire install --portable "${PACKAGE_LOCAL_DIR}/${BUNDLE_NAME}"

    STRAY="$(find "${PACKAGE_LOCAL_DIR}/${BUNDLE_NAME}" \( -name '*.pch' -o -name '*.a' -o -name '*.o' \))"
    if [ -n "$STRAY" ]; then
        echo "error: portable bundle contains stale build artifacts:" >&2
        printf "%s\n" "$STRAY" >&2
        exit 1
    fi

    if [ ! -f "${PACKAGE_LOCAL_DIR}/${BUNDLE_NAME}/runtime/vendor/sqlite3/sqlite3.c" ] ||
       [ ! -f "${PACKAGE_LOCAL_DIR}/${BUNDLE_NAME}/runtime/vendor/simdjson/simdjson.cpp" ] ||
       [ ! -f "${PACKAGE_LOCAL_DIR}/${BUNDLE_NAME}/runtime/cfire-runtime.version" ]; then
        echo "error: portable bundle is missing vendored runtime files or cfire-runtime.version" >&2
        exit 1
    fi

    ARCHIVE_PATH="${PACKAGE_LOCAL_DIR}/${BUNDLE_NAME}.tar.gz"
    tar -czf "$ARCHIVE_PATH" -C "$PACKAGE_LOCAL_DIR" "$BUNDLE_NAME"
    if command -v sha256sum >/dev/null 2>&1; then
        (cd "$PACKAGE_LOCAL_DIR" && sha256sum "${BUNDLE_NAME}.tar.gz" > "${BUNDLE_NAME}.tar.gz.sha256")
    elif command -v shasum >/dev/null 2>&1; then
        (cd "$PACKAGE_LOCAL_DIR" && shasum -a 256 "${BUNDLE_NAME}.tar.gz" > "${BUNDLE_NAME}.tar.gz.sha256")
    fi
    echo "✓ Packaged portable bundle: ${ARCHIVE_PATH}"
    exit 0
fi

if [ -z "$NEW_VER" ]; then
    echo "error: missing target version (e.g. ./scripts/release.sh 0.10.0)" >&2
    exit 1
fi

case "$NEW_VER" in
    v*) NEW_VER="${NEW_VER#v}" ;;
esac

if ! printf "%s" "$NEW_VER" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "error: invalid semantic version '$NEW_VER' (expected X.Y.Z)" >&2
    exit 1
fi

echo "==> Preparing cfire release v${NEW_VER} (dry_run=${DRY_RUN}, no_tag=${NO_TAG})"

# 1. Update src/version.hpp and VERSION using pure POSIX sed
sed "s/kVersion = \"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*\"/kVersion = \"${NEW_VER}\"/" src/version.hpp > src/version.hpp.tmp
mv -f src/version.hpp.tmp src/version.hpp
printf "%s\n" "$NEW_VER" > VERSION

# 2. Build cfire and run `./cfire doc` to propagate version across runtime headers, docs, and plugin manifests
echo "==> Building cfire v${NEW_VER} and synchronizing docs & manifests..."
make build
./cfire doc

# 3. Rebuild cfire so any updated runtime header stamps are compiled in, then run full quality gate
echo "==> Running full conformance and documentation synchronization gates (make review-check)..."
make review-check

if [ "$DRY_RUN" = "1" ]; then
    echo "✓ Dry run for cfire v${NEW_VER} succeeded (skipping git commit and tag)."
    exit 0
fi

# 4. Commit and tag release if working tree has changes or tag does not exist yet
if ! git diff --quiet HEAD -- VERSION src/version.hpp runtime/cfire_prelude.hpp docs/ .claude-plugin/; then
    git add VERSION .gitattributes src/version.hpp runtime/cfire_prelude.hpp docs/ .claude-plugin/
    git commit -m "chore(release): v${NEW_VER}"
    echo "✓ Created release commit for v${NEW_VER}"
fi

if [ "$NO_TAG" = "0" ]; then
    if git rev-parse "v${NEW_VER}" >/dev/null 2>&1; then
        echo "✓ Tag v${NEW_VER} already exists at $(git rev-parse --short "v${NEW_VER}")"
    else
        git tag -a "v${NEW_VER}" -m "cfire v${NEW_VER}"
        echo "✓ Created annotated git tag v${NEW_VER}"
    fi
    # Rebuild once on clean tagged commit so local ./cfire binary has clean SHA without -dirty
    make build
    ./cfire --version
fi
