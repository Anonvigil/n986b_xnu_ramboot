#!/usr/bin/env bash
set -euo pipefail

if (($# != 1)); then
    echo "usage: bash $0 EXYNOS990-compile-only-Mach-O" >&2
    exit 2
fi

# The positive fixtures are pinned to three inspected compile-only builds.
# Other Mach-O builds require their own measured contract.
artifact=$(realpath "$1")
actual=$(sha256sum "$artifact")
actual=${actual%% *}
case "$actual" in
    22dba00d98f8f36a484a533a83048911ca7ad6b5572a9e3ffddf5546c67ff51b)
        echo 'fixture: EXYNOS990 original compile-only Mach-O'
        ;;
    097a7c8486e9182df845bfcabf3d4573772e9cccbe699f67c6be77d57303fdd5)
        echo 'fixture: EXYNOS990 CPU0/GIC source-patch compile-only Mach-O'
        ;;
    ddcfd2df955e82fbf09f2157a4b7a1701e02678b029d4e12f0edc19bf7838918)
        echo 'fixture: EXYNOS990 CPU0/GIC plus 960MiB user-VM-page policy compile-only Mach-O'
        ;;
    *)
        echo "unrecognized EXYNOS990 Mach-O SHA-256: $actual" >&2
        exit 1
        ;;
esac

cd "$(dirname "$0")/.."
binary=$(mktemp /tmp/n986b-layout-planner.XXXXXX)
trap 'rm -f "$binary"' EXIT
clang -std=c11 -Wall -Wextra -Werror -o "$binary" \
    tests/layout_planner_test.c Xnu8796Layout.c MachO.c Arena.c DeviceTree.c
"$binary" "$artifact"
