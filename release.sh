#!/usr/bin/env bash
set -e

# Usage: ./release.sh <version> [notes]
# Example: ./release.sh 1.0.2 "Add projects list --json flag"
#
# What this does:
#   1. Bumps version.h to <version>
#   2. Builds Linux (beekeeper) and Windows (beekeeper.exe) binaries
#   3. Commits, tags, and pushes to GitHub
#   4. Creates a GitHub release with both binaries as assets
#   5. Reminds you to bump CLI_VERSION in the beekeeper web app

VERSION="$1"
NOTES="${2:-Release $VERSION}"

if [ -z "$VERSION" ]; then
    echo "Usage: ./release.sh <version> [notes]"
    echo "Example: ./release.sh 1.0.2 \"Add projects list --json flag\""
    exit 1
fi

# Validate semver format
if ! echo "$VERSION" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "Error: version must be semver (e.g. 1.0.2)"
    exit 1
fi

echo "=== Releasing beekeeper CLI v${VERSION} ==="

# 1. Bump version.h
echo "#pragma once" > version.h
echo "#define BEEKEEPER_VERSION \"${VERSION}\"" >> version.h
echo "✓ version.h updated to ${VERSION}"

# 2. Build both targets
echo "Building Linux binary..."
make linux
echo "Building Windows binary..."
make windows
echo "✓ Both binaries built"

# Smoke test the Linux binary
BUILT_VERSION=$(./beekeeper --version 2>&1 | awk '{print $2}')
if [ "$BUILT_VERSION" != "$VERSION" ]; then
    echo "Error: binary reports version '$BUILT_VERSION', expected '$VERSION'"
    exit 1
fi
echo "✓ Linux binary verified: beekeeper $BUILT_VERSION"

# 3. Commit, tag, push
git add beekeeper.cpp version.h Makefile release.sh
git diff --cached --quiet || git commit -m "release: v${VERSION}"
git tag "v${VERSION}"
git push
git push origin "v${VERSION}"
echo "✓ Pushed tag v${VERSION}"

# 4. Create GitHub release with both binaries
gh release create "v${VERSION}" beekeeper beekeeper.exe \
    --title "v${VERSION}" \
    --notes "${NOTES}"
echo "✓ GitHub release v${VERSION} created"

echo ""
echo "=== Done ==="
echo ""
echo "Next: bump CLI_VERSION in the beekeeper web app:"
echo "  1. Edit beekeeper/routes/api_v1.py → CLI_VERSION = \"${VERSION}\""
echo "  2. git add routes/api_v1.py && git commit -m \"chore: bump CLI_VERSION to ${VERSION}\" && git push"
echo "  3. ./deploy.sh"
