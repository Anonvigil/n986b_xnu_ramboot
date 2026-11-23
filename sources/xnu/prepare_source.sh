#!/usr/bin/env bash
# Reconstruct the N986B XNU source overlay from pinned public source.
# This script performs host filesystem and network operations only.
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 SCION_CHECKOUT [h6|h7]" >&2
  exit 2
fi

scion=$(cd "$1" && pwd)
stage=${2:-h6}
[[ "$stage" == h6 || "$stage" == h7 ]] || { echo "stage must be h6 or h7" >&2; exit 2; }
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
expected_scion=025f97557ac439651b08f2a906c01addfad77ece
tree="$scion/xnu-src/xnu-8796.121.2"

[[ $(git -C "$scion" rev-parse HEAD) == "$expected_scion" ]] || {
  echo "Scion checkout must be pinned to $expected_scion" >&2; exit 1;
}
[[ ! -e "$scion/devices/n986b" ]] || {
  echo "devices/n986b already exists; use a fresh Scion checkout" >&2; exit 1;
}
[[ ! -e "$tree" ]] || {
  echo "XNU source tree already exists; use a fresh Scion checkout" >&2; exit 1;
}

cp -a "$here/device/n986b" "$scion/devices/n986b"
cd "$scion"
make n986b_defconfig
export OS_PROFILE=ios-16.5
./xnu/apply.sh
patch --batch --dry-run -d "$tree" -p1 < "$here/patches/0001-n986b-ios165-h6.patch"
patch --batch -s -d "$tree" -p1 < "$here/patches/0001-n986b-ios165-h6.patch"
if [[ "$stage" == h7 ]]; then
  patch --batch --dry-run -d "$tree" -p1 < "$here/patches/0002-n986b-ios165-h7-host-only.patch"
  patch --batch -s -d "$tree" -p1 < "$here/patches/0002-n986b-ios165-h7-host-only.patch"
fi
python3 "$here/verify_source.py" --tree "$tree" --stage "$stage"
echo "Prepared $stage source at $tree. Kernel build and device tests are separate steps."
