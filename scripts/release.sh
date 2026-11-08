#!/bin/sh
# scripts/release.sh — Atomic version bump, doc/manifest sync, verification & git tagging for cfire
# Usage:
#   ./scripts/release.sh <x.y.z> [--dry-run] [--no-tag]
#   make release VERSION=0.10.0
set -e

NEW_VER=""
DRY_RUN=0
NO_TAG=0

for arg in "$@"; do
    case "$arg" in
        --dry-run) DRY_RUN=1 ;;
        --no-tag)  NO_TAG=1 ;;
        -h|--help)
            echo "Usage: ./scripts/release.sh <x.y.z> [--dry-run] [--no-tag]"
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

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

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
